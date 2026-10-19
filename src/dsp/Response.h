#pragma once
#include "TopEndProcessor.h"
#include <cmath>
#include <complex>

namespace topend {
// Steady-state magnitude response of TopEnd for display. The band gains are the
// dynamic reductions of each enhancement path (1 = none), as reported by Meters.
class ResponseCurve {
public:
  void prepare(double rate) noexcept {
    rate_ = rate;
    mid_.highpass(rate, TopEndProcessor::kMidHz, TopEndProcessor::kMidQ);
    high_.highpass(rate, TopEndProcessor::kHighHz, TopEndProcessor::kHighQ);
  }
  [[nodiscard]] double gainDb(double hz, const Parameters &p, double midGain = 1.0,
                              double highGain = 1.0) const noexcept {
    if (p.bypass)
      return 0.0;
    const auto m = TopEndProcessor::midControl(p.mid);
    const auto h = TopEndProcessor::highControl(p.high);
    const std::complex<double> response = 1.0 + m.direct + h.direct +
                                          m.amount * midGain * mid_.response(hz, rate_) +
                                          h.amount * highGain * high_.response(hz, rate_);
    return 20.0 * std::log10(std::max(std::abs(response), 1e-12)) + p.trimDb;
  }

private:
  double rate_ = 48000.0;
  Biquad mid_, high_;
};
} // namespace topend
