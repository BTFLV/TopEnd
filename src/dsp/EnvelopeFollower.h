#pragma once
#include <algorithm>
#include <cmath>

namespace topend {
inline double onePole(double rate, double seconds) noexcept {
  return std::exp(-1.0 / (rate * seconds));
}

// Instant peak acquisition with two release stages. Resetting both stages on
// a new peak gives a flat initial release slope and suppresses inter-cycle ripple.
class PeakEnvelope {
public:
  void prepare(double rate, double seconds1, double seconds2) noexcept {
    c1_ = onePole(rate, seconds1);
    c2_ = onePole(rate, seconds2);
    reset();
  }
  double process(double magnitude) noexcept {
    if (magnitude >= e2_) {
      e1_ = e2_ = magnitude;
    } else {
      e1_ = std::max(magnitude, c1_ * e1_);
      e2_ = c2_ * e2_ + (1.0 - c2_) * e1_;
    }
    if (e2_ < 1e-24)
      e1_ = e2_ = 0.0;
    return e2_;
  }
  void reset() noexcept {
    e1_ = e2_ = 0;
  }

private:
  double c1_ = 0, c2_ = 0, e1_ = 0, e2_ = 0;
};

// Mean-square level with separate rise and fall times. The square root is
// scaled by sqrt(2), so a steady sine reads its peak amplitude.
class MeanSquareLevel {
public:
  void prepare(double rate, double attackSeconds, double releaseSeconds) noexcept {
    attack_ = onePole(rate, attackSeconds);
    release_ = onePole(rate, releaseSeconds);
    reset();
  }
  double process(double magnitude) noexcept {
    const double square = magnitude * magnitude;
    v_ = square + (square > v_ ? attack_ : release_) * (v_ - square);
    if (v_ < 1e-30)
      v_ = 0.0;
    return std::sqrt(2.0 * v_);
  }
  void reset() noexcept {
    v_ = 0;
  }

private:
  double attack_ = 0, release_ = 0, v_ = 0;
};

// Two cascaded one-poles acting on log gain. Attack applies while the target is
// below the output (more reduction), release otherwise. Settled values snap to
// the target, so unity gain becomes exact and states never become subnormal.
class LogGainSmoother {
public:
  void prepare(double rate, double attackSeconds, double releaseSeconds) noexcept {
    attack_ = onePole(rate, attackSeconds);
    release_ = onePole(rate, releaseSeconds);
    reset();
  }
  double process(double target) noexcept {
    const double c = target < s2_ ? attack_ : release_;
    s1_ = target + c * (s1_ - target);
    s2_ = s1_ + c * (s2_ - s1_);
    if (std::abs(s1_ - target) < 1e-12 && std::abs(s2_ - target) < 1e-12)
      s1_ = s2_ = target;
    return s2_;
  }
  void reset() noexcept {
    s1_ = s2_ = 0;
  }

private:
  double attack_ = 0, release_ = 0, s1_ = 0, s2_ = 0;
};

// Fixed-duration linear ramp. A new target restarts the ramp from the current
// value, so settling time is defined and independent of the host block size.
class LinearRamp {
public:
  void prepare(double rate, double seconds) noexcept {
    steps_ = std::max(1L, std::lround(rate * seconds));
  }
  void reset(double value) noexcept {
    value_ = target_ = value;
    remaining_ = 0;
  }
  double process(double target) noexcept {
    if (target != target_) {
      target_ = target;
      remaining_ = steps_;
      increment_ = (target_ - value_) / static_cast<double>(steps_);
    }
    if (remaining_ > 0)
      value_ = --remaining_ ? value_ + increment_ : target_;
    return value_;
  }

private:
  double value_ = 0, target_ = 0, increment_ = 0;
  long steps_ = 1, remaining_ = 0;
};
} // namespace topend
