#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace topend {
// RBJ bilinear-transform highpass, transposed direct form II. Double state
// avoids loss of accuracy at high sample rates; coefficients change only in prepare.
class Biquad {
public:
  void highpass(double rate, double hz, double q) noexcept {
    hz = std::clamp(hz, 10.0, rate * 0.45);
    const double w = 2.0 * std::numbers::pi * hz / rate;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    const double inv = 1.0 / (1.0 + alpha);
    b0_ = (1.0 + c) * 0.5 * inv;
    b1_ = -2.0 * b0_;
    b2_ = b0_;
    a1_ = -2.0 * c * inv;
    a2_ = (1.0 - alpha) * inv;
    reset();
  }
  double process(double x) noexcept {
    const double y = b0_ * x + z1_;
    z1_ = zap(b1_ * x - a1_ * y + z2_);
    z2_ = zap(b2_ * x - a2_ * y);
    return y;
  }
  void reset() noexcept {
    z1_ = z2_ = 0.0;
  }
  // Complex frequency response at hz for the given sample rate.
  [[nodiscard]] std::complex<double> response(double hz, double rate) const noexcept {
    const auto z = std::polar(1.0, -2.0 * std::numbers::pi * hz / rate);
    return (b0_ + (b1_ + b2_ * z) * z) / (1.0 + (a1_ + a2_ * z) * z);
  }

private:
  static double zap(double v) noexcept {
    return std::abs(v) < 1e-30 ? 0.0 : v;
  }
  double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z1_ = 0, z2_ = 0;
};
} // namespace topend
