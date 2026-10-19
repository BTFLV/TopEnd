#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace topend {
struct State {
  float mid = 0, high = 0, trimDb = 0;
  bool link = false, bypass = false;
};
inline constexpr std::size_t stateBytes = 24;
using StateData = std::array<std::uint8_t, stateBytes>;

inline bool validState(const State &s) noexcept {
  return std::isfinite(s.mid) && std::isfinite(s.high) && std::isfinite(s.trimDb) && s.mid >= 0 &&
         s.mid <= 100 && s.high >= 0 && s.high <= 100 && s.trimDb >= -12 && s.trimDb <= 0;
}
inline StateData encodeState(State s) noexcept {
  if (!validState(s))
    s = {};
  StateData bytes{'T', 'E', 'N', 'D', 1, 0, 0, 0};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto v = std::bit_cast<std::uint32_t>(i == 0 ? s.mid : i == 1 ? s.high : s.trimDb);
    for (std::size_t b = 0; b < 4; ++b)
      bytes[8 + 4 * i + b] = static_cast<std::uint8_t>(v >> (8 * b));
  }
  bytes[20] = s.link ? 1 : 0;
  bytes[21] = s.bypass ? 1 : 0;
  return bytes;
}
inline std::optional<State> decodeState(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() != stateBytes || bytes[0] != 'T' || bytes[1] != 'E' || bytes[2] != 'N' ||
      bytes[3] != 'D' || bytes[4] != 1 || bytes[5] != 0 || bytes[6] != 0 || bytes[7] != 0 ||
      bytes[20] > 1 || bytes[21] > 1 || bytes[22] != 0 || bytes[23] != 0)
    return {};
  std::array<float, 3> values{};
  for (std::size_t i = 0; i < 3; ++i) {
    std::uint32_t v = 0;
    for (std::size_t b = 0; b < 4; ++b)
      v |= std::uint32_t(bytes[8 + 4 * i + b]) << (8 * b);
    values[i] = std::bit_cast<float>(v);
  }
  State s{values[0], values[1], values[2], bytes[20] != 0, bytes[21] != 0};
  if (!validState(s))
    return {};
  return s;
}
} // namespace topend
