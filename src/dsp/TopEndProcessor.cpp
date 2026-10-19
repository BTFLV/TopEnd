#include "TopEndProcessor.h"
#include "ControlMapping.h"
#include <algorithm>
#include <cmath>

namespace topend {
namespace {
// Controls and trim ramp over 512 samples at 48 kHz, bypass over 1024, fixed in
// time so results never depend on the host's buffer size.
constexpr double kControlRampSeconds = 512.0 / 48000.0;
constexpr double kBypassRampSeconds = 1024.0 / 48000.0;

// Every dynamic stage is a 4:1 downward compressor acting on its path's gain.
// Thresholds and times were tuned by measurement (docs/dsp-design.md).
constexpr double kSlope = 0.75; // 1 - 1/ratio for 4:1

// Mid: peak level, instant attack, two-stage release.
constexpr double kMidThresholdDb = -30.0;
constexpr double kMidRelease1 = 0.075, kMidRelease2 = 0.033;

// High stage 1: mean-square level, log-gain attack/release.
constexpr double kHighThresholdDb = -39.7;
constexpr double kHighPowerTime = 0.010;
constexpr double kHighAttack = 0.001, kHighRelease = 0.075;

// High stage 2: peaks of the stage-1-reduced band, scaled with the control
// amount, so stronger settings compress harder.
constexpr double kSecondThresholdDb = -27.3;
constexpr double kSecondRelease1 = 0.00075, kSecondRelease2 = 0.00015;
constexpr double kSecondAttack = 0.016, kSecondRelease = 0.025;
constexpr double kAmountExponent = 0.64;

double finiteClamp(double v, double lo, double hi) noexcept {
  return std::isfinite(v) ? std::clamp(v, lo, hi) : 0.0;
}
ControlPoint mapping(const std::array<ControlPoint, 101> &points, double percent) noexcept {
  const auto index = static_cast<std::size_t>(percent);
  if (index >= 100)
    return points[100];
  const double t = percent - static_cast<double>(index);
  return {std::lerp(points[index].direct, points[index + 1].direct, t),
          std::lerp(points[index].amount, points[index + 1].amount, t)};
}
double logThreshold(double db) noexcept {
  return db * std::log(10.0) / 20.0;
}
// Gain reduction in natural-log units for a level; zero at or below threshold.
double logGain(double level, double logThreshold) noexcept {
  if (level <= 0.0)
    return 0.0;
  return std::min(0.0, -kSlope * (std::log(level) - logThreshold));
}
} // namespace

ControlPoint TopEndProcessor::midControl(double percent) noexcept {
  return mapping(kMidMapping, finiteClamp(percent, 0, 100));
}
ControlPoint TopEndProcessor::highControl(double percent) noexcept {
  return mapping(kHighMapping, finiteClamp(percent, 0, 100));
}

void TopEndProcessor::prepare(double sampleRate) noexcept {
  if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0)
    sampleRate = 48000.0;
  for (auto &f : midFilter_)
    f.highpass(sampleRate, kMidHz, kMidQ);
  for (auto &f : highFilter_)
    f.highpass(sampleRate, kHighHz, kHighQ);
  midThreshold_ = std::exp(logThreshold(kMidThresholdDb));
  highLogThreshold_ = logThreshold(kHighThresholdDb);
  secondLogThreshold_ = logThreshold(kSecondThresholdDb);
  midEnvelope_.prepare(sampleRate, kMidRelease1, kMidRelease2);
  highLevel_.prepare(sampleRate, kHighPowerTime, kHighPowerTime);
  highGain_.prepare(sampleRate, kHighAttack, kHighRelease);
  secondEnvelope_.prepare(sampleRate, kSecondRelease1, kSecondRelease2);
  secondGain_.prepare(sampleRate, kSecondAttack, kSecondRelease);
  midRamp_.prepare(sampleRate, kControlRampSeconds);
  highRamp_.prepare(sampleRate, kControlRampSeconds);
  trimRamp_.prepare(sampleRate, kControlRampSeconds);
  bypassRamp_.prepare(sampleRate, kBypassRampSeconds);
  reset();
}
void TopEndProcessor::setParameters(Parameters p) noexcept {
  p.mid = static_cast<float>(finiteClamp(p.mid, 0, 100));
  p.high = static_cast<float>(finiteClamp(p.high, 0, 100));
  p.trimDb = static_cast<float>(finiteClamp(p.trimDb, -12, 0));
  target_ = p;
  // Hosts often reset before restoring a session's values. Until audio flows,
  // apply changes immediately, as a freshly configured instance would.
  if (pristine_)
    snapControls();
}
void TopEndProcessor::snapControls() noexcept {
  midRamp_.reset(target_.mid);
  highRamp_.reset(target_.high);
  trimRamp_.reset(target_.trimDb);
  bypassRamp_.reset(target_.bypass ? 0.0 : 1.0);
  trimDb_ = target_.trimDb;
  trimGain_ = std::pow(10.0, trimDb_ / 20.0);
}
void TopEndProcessor::reset() noexcept {
  for (auto &f : midFilter_)
    f.reset();
  for (auto &f : highFilter_)
    f.reset();
  midEnvelope_.reset();
  highLevel_.reset();
  highGain_.reset();
  secondEnvelope_.reset();
  secondGain_.reset();
  snapControls();
  detectorAmount_ = -1.0;
  meters_ = {};
  pristine_ = true;
}

template <class T>
void TopEndProcessor::processTyped(const T *const *input, T *const *output, std::size_t channels,
                                   std::size_t frames) noexcept {
  if (frames == 0 || channels == 0 || !input || !output)
    return;
  channels = std::min(channels, std::size_t{2});
  pristine_ = false;
  Meters meters;
  for (std::size_t i = 0; i < frames; ++i) {
    const auto mid = mapping(kMidMapping, midRamp_.process(target_.mid));
    const auto high = mapping(kHighMapping, highRamp_.process(target_.high));
    // Trim ramps linearly in decibels; convert only while it moves.
    if (const double db = trimRamp_.process(target_.trimDb); db != trimDb_) {
      trimDb_ = db;
      trimGain_ = std::pow(10.0, db / 20.0);
    }
    const double trim = trimGain_;
    const double wet = bypassRamp_.process(target_.bypass ? 0.0 : 1.0);
    std::array<double, 2> x{}, m{}, h{};
    double midPeak = 0, highPeak = 0, inputPeak = 0;
    for (std::size_t ch = 0; ch < channels; ++ch) {
      x[ch] = input[ch] ? finiteClamp(input[ch][i], -1e12, 1e12) : 0.0;
      m[ch] = midFilter_[ch].process(x[ch]);
      h[ch] = highFilter_[ch].process(x[ch]);
      midPeak = std::max(midPeak, std::abs(m[ch]));
      highPeak = std::max(highPeak, std::abs(h[ch]));
      inputPeak = std::max(inputPeak, std::abs(x[ch]));
    }
    // Stereo-linked detection changes gain only; channels are never summed into audio.
    const double midLevel = midEnvelope_.process(midPeak);
    const double mg = midLevel <= midThreshold_ ? 1.0 : std::pow(midLevel / midThreshold_, -kSlope);
    const double g1 =
        std::exp(highGain_.process(logGain(highLevel_.process(highPeak), highLogThreshold_)));
    // The control-dependent detector scale is constant once automation settles.
    if (high.amount != detectorAmount_) {
      detectorAmount_ = high.amount;
      detectorScale_ = std::pow(high.amount, kAmountExponent);
    }
    const double second = secondEnvelope_.process(highPeak * g1 * detectorScale_);
    const double hg = g1 * std::exp(secondGain_.process(logGain(second, secondLogThreshold_)));
    const double direct = 1.0 + mid.direct + high.direct;
    double outputPeak = 0;
    for (std::size_t ch = 0; ch < channels; ++ch) {
      const double processed =
          (direct * x[ch] + mid.amount * mg * m[ch] + high.amount * hg * h[ch]) * trim;
      const double y = x[ch] + wet * (processed - x[ch]);
      outputPeak = std::max(outputPeak, std::abs(y));
      if (output[ch])
        output[ch][i] = static_cast<T>(y);
    }
    meters.midGain = std::min(meters.midGain, static_cast<float>(mg));
    meters.highGain = std::min(meters.highGain, static_cast<float>(hg));
    meters.inputPeak = std::max(meters.inputPeak, static_cast<float>(inputPeak));
    meters.outputPeak = std::max(meters.outputPeak, static_cast<float>(outputPeak));
  }
  meters_ = meters;
}
void TopEndProcessor::process(const float *const *in, float *const *out, std::size_t c,
                              std::size_t n) noexcept {
  processTyped(in, out, c, n);
}
void TopEndProcessor::process(const double *const *in, double *const *out, std::size_t c,
                              std::size_t n) noexcept {
  processTyped(in, out, c, n);
}
} // namespace topend
