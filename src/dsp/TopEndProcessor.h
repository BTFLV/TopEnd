#pragma once
#include "ControlMapping.h"
#include "EnvelopeFollower.h"
#include "Filters.h"
#include <array>
#include <cstddef>

namespace topend {
struct Parameters {
  float mid = 0;    // Percent, 0..100
  float high = 0;   // Percent, 0..100
  float trimDb = 0; // -12..0 dB
  bool bypass = false;
};

// Per-block activity for displays: the strongest reduction of each band's
// enhancement (1 = none) and the peak input and output sample magnitudes.
struct Meters {
  float midGain = 1, highGain = 1, inputPeak = 0, outputPeak = 0;
};

class TopEndProcessor {
public:
  static constexpr double kMidHz = 3000.0, kMidQ = 0.63, kHighHz = 9000.0, kHighQ = 0.707;
  // Direct and wet amounts for a control position, interpolated from measurements.
  [[nodiscard]] static ControlPoint midControl(double percent) noexcept;
  [[nodiscard]] static ControlPoint highControl(double percent) noexcept;
  void prepare(double sampleRate) noexcept;
  void reset() noexcept;
  void setParameters(Parameters parameters) noexcept;
  [[nodiscard]] Parameters parameters() const noexcept {
    return target_;
  }
  // Activity of the most recent process() call; read on the audio thread.
  [[nodiscard]] Meters meters() const noexcept {
    return meters_;
  }
  static constexpr int latencySamples = 0;
  // One audio-thread owner. Parameter transfer/synchronization belongs to the
  // adapter; no locks, allocations, host calls or I/O occur in this class.
  void process(const float *const *input, float *const *output, std::size_t channels,
               std::size_t frames) noexcept;
  void process(const double *const *input, double *const *output, std::size_t channels,
               std::size_t frames) noexcept;

private:
  template <class T>
  void processTyped(const T *const *, T *const *, std::size_t, std::size_t) noexcept;
  void snapControls() noexcept;
  std::array<Biquad, 2> midFilter_, highFilter_;
  PeakEnvelope midEnvelope_, secondEnvelope_;
  MeanSquareLevel highLevel_;
  LogGainSmoother highGain_, secondGain_;
  LinearRamp midRamp_, highRamp_, trimRamp_, bypassRamp_;
  Parameters target_;
  Meters meters_;
  double midThreshold_ = 0, highLogThreshold_ = 0, secondLogThreshold_ = 0;
  double detectorAmount_ = -1.0, detectorScale_ = 0.0, trimDb_ = 0.0, trimGain_ = 1.0;
  bool pristine_ = true; // No audio processed since prepare() or reset().
};
} // namespace topend
