#include "common/State.h"
#include "dsp/Response.h"
#include "dsp/TopEndProcessor.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

static std::atomic<int> allocations{0};
void *operator new(std::size_t n) {
  ++allocations;
  if (void *p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept {
  std::free(p);
}
void *operator new[](std::size_t n) {
  return ::operator new(n);
}
void operator delete[](void *p) noexcept {
  ::operator delete(p);
}
static void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
static std::vector<float> render(const std::vector<float> &input, topend::Parameters p, double rate,
                                 std::size_t block) {
  topend::TopEndProcessor dsp;
  dsp.setParameters(p);
  dsp.prepare(rate);
  std::vector<float> output(input.size());
  for (std::size_t start = 0; start < input.size(); start += block) {
    const float *in[] = {input.data() + start};
    float *out[] = {output.data() + start};
    dsp.process(in, out, 1, std::min(block, input.size() - start));
  }
  return output;
}
struct Event {
  std::size_t frame;
  topend::Parameters parameters;
};
// Stereo render with parameter changes at exact frames, in fixed-size blocks.
static std::vector<float> renderEvents(const std::vector<float> &left,
                                       const std::vector<float> &right,
                                       const std::vector<Event> &events, std::size_t block,
                                       std::vector<float> *rightOut = nullptr) {
  topend::TopEndProcessor dsp;
  dsp.setParameters(events.front().parameters);
  dsp.prepare(48000);
  std::vector<float> outLeft(left.size()), outRight(right.size());
  std::size_t next = 1;
  for (std::size_t start = 0; start < left.size();) {
    for (; next < events.size() && events[next].frame <= start; ++next)
      dsp.setParameters(events[next].parameters);
    std::size_t n = std::min(block, left.size() - start);
    if (next < events.size())
      n = std::min(n, events[next].frame - start);
    const float *in[] = {left.data() + start, right.data() + start};
    float *out[] = {outLeft.data() + start, outRight.data() + start};
    dsp.process(in, out, 2, n);
    start += n;
  }
  if (rightOut)
    *rightOut = outRight;
  return outLeft;
}
int main() {
  try {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-1, 1);
    std::vector<float> noise(24000);
    for (auto &v : noise)
      v = dist(rng);
    for (const double rate : {44100., 48000., 88200., 96000., 192000.}) {
      require(render(noise, {}, rate, 17) == noise, "Neutral must be sample transparent");
      require(render(noise, {100, 100, -12, true}, rate, 511) == noise,
              "Static bypass must be transparent");
      const auto trimmed = render(noise, {0, 0, -6, false}, rate, 128);
      const float gain = static_cast<float>(std::pow(10., -6. / 20.));
      for (std::size_t i = 0; i < noise.size(); ++i)
        require(std::abs(trimmed[i] - noise[i] * gain) < 1e-7f, "Trim linearity");
      auto expected = render(noise, {100, 100, 0, false}, rate, 512);
      for (const std::size_t block : std::array<std::size_t, 5>{1, 17, 127, 511, 1024})
        require(render(noise, {100, 100, 0, false}, rate, block) == expected,
                "Block size must not change DSP");
      std::vector<float> extreme(24000);
      for (auto p : {topend::Parameters{}, topend::Parameters{100, 100, 0, false}}) {
        for (std::size_t i = 0; i < extreme.size(); ++i)
          extreme[i] = (i % 2 ? 1.f : -1.f) * 1e12f;
        for (auto v : render(extreme, p, rate, 513))
          require(std::isfinite(v), "Extreme finite input stability");
        std::fill(extreme.begin(), extreme.end(), 0.f);
        for (auto v : render(extreme, p, rate, 513))
          require(v == 0, "Silence noise floor");
        extreme[0] = 1.f;
        for (auto v : render(extreme, p, rate, 513))
          require(std::isfinite(v), "Impulse stability");
        std::fill(extreme.begin(), extreme.end(), 1e-35f);
        for (auto v : render(extreme, p, rate, 513))
          require(std::isfinite(v), "Tiny-input stability");
        // Genuinely subnormal float input; neutral settings must pass it unchanged.
        for (std::size_t i = 0; i < extreme.size(); ++i)
          extreme[i] = (i % 3 ? 1.f : -1.f) * std::numeric_limits<float>::denorm_min() * 37;
        const auto subnormal = render(extreme, p, rate, 513);
        for (std::size_t i = 0; i < extreme.size(); ++i)
          require(std::isfinite(subnormal[i]) && (p.mid > 0 || subnormal[i] == extreme[i]),
                  "Subnormal input");
      }
    }
    // In-place processing and the double entry point must use identical signal history.
    const auto expectedFloat = render(noise, {35, 63, -3, false}, 48000, 127);
    std::vector<double> doubleAudio(noise.begin(), noise.end());
    topend::TopEndProcessor doubleDsp;
    doubleDsp.setParameters({35, 63, -3, false});
    doubleDsp.prepare(48000);
    const double *doubleIn[] = {doubleAudio.data()};
    double *doubleOut[] = {doubleAudio.data()};
    doubleDsp.process(doubleIn, doubleOut, 1, doubleAudio.size());
    for (std::size_t i = 0; i < noise.size(); ++i)
      require(static_cast<float>(doubleAudio[i]) == expectedFloat[i],
              "Double/in-place equivalence");
    for (double rate : {8000., 22050., 384000., 0., std::numeric_limits<double>::quiet_NaN()}) {
      for (float value : render(noise, {100, 100, 0, false}, rate, 1024))
        require(std::isfinite(value), "Boundary sample rate stability");
    }
    topend::TopEndProcessor dsp;
    dsp.prepare(48000);
    std::array<float, 512> left{}, right{}, outLeft{}, outRight{};
    left[0] = 1;
    const float *in[] = {left.data(), right.data()};
    float *out[] = {outLeft.data(), outRight.data()};
    dsp.setParameters({100, 100, 0, false});
    dsp.reset();
    const auto before = allocations.load();
    dsp.process(static_cast<const float *const *>(nullptr), static_cast<float *const *>(nullptr), 2,
                0);
    dsp.process(in, out, 2, 512);
    require(before == allocations.load(), "Audio callback allocated");
    for (auto v : outRight)
      require(v == 0, "Audio channel crosstalk");
    for (int i = 0; i < 512; ++i)
      left[static_cast<std::size_t>(i)] = right[static_cast<std::size_t>(i)] =
          std::sin(static_cast<float>(i) * .12f) * .4f;
    for (int step = 0; step < 200; ++step) {
      dsp.setParameters(
          {step % 2 ? 100.f : 0.f, step % 3 ? 0.f : 100.f, step % 4 ? -12.f : 0.f, step % 5 == 0});
      const auto count = allocations.load();
      dsp.process(in, out, 2, 512);
      require(count == allocations.load(), "Automation allocated");
      for (std::size_t i = 0; i < 512; ++i) {
        require(std::isfinite(outLeft[i]), "Automation stability");
        require(outLeft[i] == outRight[i], "Stereo equality");
      }
    }
    // Mid-buffer bypass: constant input isolates the continuity of the dry/trim crossfade.
    dsp.setParameters({0, 0, -12, false});
    dsp.reset();
    left.fill(.5f);
    right.fill(.5f);
    dsp.process(in, out, 2, 512);
    const float prior = outLeft.back();
    dsp.setParameters({0, 0, -12, true});
    dsp.process(in, out, 2, 512);
    require(std::abs(outLeft[0] - prior) < .002f, "Bypass click discontinuity");
    dsp.setParameters({std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(), -100, false});
    dsp.reset();
    left[0] = std::numeric_limits<float>::quiet_NaN();
    left[1] = std::numeric_limits<float>::infinity();
    dsp.process(in, out, 2, 512);
    for (auto v : outLeft)
      require(std::isfinite(v), "Nonfinite sanitization");
    // Reset must erase signal history and reproduce a fresh instance.
    dsp.setParameters({50, 50, 0, false});
    dsp.reset();
    left.fill(0);
    right.fill(0);
    dsp.process(in, out, 2, 512);
    for (auto v : outLeft)
      require(v == 0, "Reset leaked history");
    dsp.prepare(96000);
    dsp.process(in, out, 2, 512);
    for (auto v : outLeft)
      require(v == 0, "Sample rate reset history");
    // Values set after reset but before audio (a host restoring a session) apply
    // immediately; later changes ramp.
    {
      topend::TopEndProcessor restored;
      restored.prepare(48000);
      restored.setParameters({35, 50, -3, false});
      std::vector<float> restoredOut(noise.size());
      const float *restoredIn[] = {noise.data()};
      float *o[] = {restoredOut.data()};
      restored.process(restoredIn, o, 1, noise.size());
      require(restoredOut == render(noise, {35, 50, -3, false}, 48000, noise.size()),
              "Parameters restored before audio did not apply immediately");
    }
    // Ramps settle in a fixed time: trim moves linearly in dB over 512 samples at
    // 48 kHz, and bypass becomes exact after 1024 samples, for any block partition.
    {
      const std::vector<float> ones(4096, .25f);
      const auto trimmed =
          renderEvents(ones, ones, {{0, {0, 0, 0, false}}, {1000, {0, 0, -12, false}}}, 64);
      for (std::size_t i = 1000; i < 1000 + 512; ++i) {
        const double db = 20 * std::log10(trimmed[i] / .25);
        require(std::abs(db + 12.0 * static_cast<double>(i - 999) / 512.0) < 1e-4,
                "Trim ramp is not linear in dB");
      }
      require(trimmed[1511] == trimmed[4095], "Trim ramp did not settle at 512 samples");
      require(trimmed[1510] != trimmed[1511], "Trim ramp settled early");
      const auto bypassed =
          renderEvents(noise, noise, {{0, {80, 90, -3, false}}, {700, {80, 90, -3, true}}}, 128);
      for (std::size_t i = 700; i < noise.size(); ++i)
        require((i >= 700 + 1023) == (bypassed[i] == noise[i]), "Bypass ramp duration");
    }
    // Automation at arbitrary frames must not depend on the host block size.
    {
      const std::vector<Event> events{{0, {10, 0, 0, false}},        {333, {70, 100, -6, false}},
                                      {4001, {70, 100, -6, true}},   {4500, {5, 40, 0, false}},
                                      {9000, {100, 20, -12, false}}, {9001, {0, 85, -1, false}}};
      const std::vector<float> reversed(noise.rbegin(), noise.rend());
      const auto expected = renderEvents(noise, reversed, events, 512);
      for (const std::size_t block : std::array<std::size_t, 4>{1, 17, 127, 1031})
        require(renderEvents(noise, reversed, events, block) == expected,
                "Automation depends on block size");
    }
    // Shared detection: identical stereo channels equal mono; swapping channels swaps outputs.
    {
      const auto mono = render(noise, {60, 80, -2, false}, 48000, 256);
      std::vector<float> monoRight;
      const std::vector<Event> events{{0, {60, 80, -2, false}}};
      require(renderEvents(noise, noise, events, 256, &monoRight) == mono && monoRight == mono,
              "Identical stereo channels differ from mono");
      std::vector<float> other(noise.size());
      for (std::size_t i = 0; i < other.size(); ++i)
        other[i] = .3f * noise[(i * 7 + 11) % noise.size()];
      std::vector<float> swappedRight, unswappedRight;
      const auto unswapped = renderEvents(noise, other, events, 256, &unswappedRight);
      const auto swapped = renderEvents(other, noise, events, 256, &swappedRight);
      require(swapped == unswappedRight && swappedRight == unswapped, "Channel swap asymmetry");
    }
    // The editor's response curve must describe the processor: quiet tones match
    // the low-level curve, and a loud tone matches the curve at the reported gains.
    {
      topend::ResponseCurve curve;
      curve.prepare(48000);
      const auto measure = [](const topend::Parameters &p, double hz, double level,
                              topend::Meters *meters) {
        topend::TopEndProcessor dsp;
        dsp.setParameters(p);
        dsp.prepare(48000);
        std::vector<float> x(48000), y(x.size());
        for (std::size_t i = 0; i < x.size(); ++i)
          x[i] = static_cast<float>(
              level * std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / 48000));
        for (std::size_t start = 0; start < x.size(); start += 480) {
          const float *in[] = {x.data() + start};
          float *out[] = {y.data() + start};
          dsp.process(in, out, 1, 480);
        }
        if (meters)
          *meters = dsp.meters();
        double sx = 0, sy = 0;
        for (std::size_t i = 24000; i < x.size(); ++i) {
          sx += double(x[i]) * x[i];
          sy += double(y[i]) * y[i];
        }
        return 10 * std::log10(sy / sx);
      };
      const topend::Parameters quiet{35, 50, -3, false};
      for (const double hz : {200., 1000., 2500., 3500., 6000., 10000., 16000.})
        require(std::abs(measure(quiet, hz, 0.001, nullptr) - curve.gainDb(hz, quiet)) < 0.01,
                "Low-level display curve differs from processing");
      topend::Meters meters;
      const topend::Parameters loud{100, 100, 0, false};
      const double measured = measure(loud, 10000, 0.5, &meters);
      require(meters.midGain < 0.5f && meters.highGain < 0.5f, "Loud tone not reported");
      require(std::abs(measured - curve.gainDb(10000, loud, meters.midGain, meters.highGain)) < 0.1,
              "Display curve at reported gains differs from processing");
      require(std::abs(meters.inputPeak - 0.5f) < 1e-3f, "Input peak meter");
    }
    const topend::State state{35.5f, 78.25f, -5.5f, true, true};
    const auto bytes = topend::encodeState(state);
    const auto restored = topend::decodeState(bytes);
    require(restored && restored->mid == state.mid && restored->high == state.high &&
                restored->trimDb == state.trimDb && restored->link && restored->bypass,
            "State round trip");
    for (std::size_t n = 0; n < bytes.size(); ++n)
      require(!topend::decodeState(std::span(bytes.data(), n)), "Truncated state accepted");
    auto corrupt = bytes;
    corrupt[4] = 2;
    require(!topend::decodeState(corrupt), "Unknown state version accepted");
    corrupt = bytes;
    corrupt[20] = 9;
    require(!topend::decodeState(corrupt), "Invalid boolean accepted");
    corrupt = bytes;
    corrupt[10] = 255;
    corrupt[11] = 127;
    require(!topend::decodeState(corrupt), "Nonfinite state accepted");
    std::cout << "DSP/state tests passed: 5 rates, block invariance, silence, transparency, trim, "
                 "automation, ramps, reset, finite/subnormal extremes, stereo symmetry, mono, "
                 "zero allocations\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
