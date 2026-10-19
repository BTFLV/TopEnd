#pragma once
#include "IControl.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace topend::gui {
using namespace iplug;
using namespace igraphics;

inline const IColor backgroundTop{255, 18, 22, 27}, backgroundBottom{255, 11, 13, 16};
inline const IColor cardFill{255, 22, 27, 33}, cardEdge{18, 255, 255, 255};
inline const IColor track{255, 38, 45, 52}, grid{14, 255, 255, 255};
inline const IColor ink{255, 238, 241, 238}, muted{255, 138, 151, 156}, faint{255, 82, 93, 100};
inline const IColor amber{255, 240, 184, 110}, mint{255, 111, 224, 200}, alert{255, 255, 107, 94};

inline constexpr const char *kRegular = "TopEndRegular";
inline constexpr const char *kBold = "TopEndBold";

inline IText text(float size, const IColor &color = ink, EAlign align = EAlign::Center,
                  const char *font = kRegular) {
  return IText(size, color, font, align);
}
inline IColor withAlpha(const IColor &c, float alpha) {
  return IColor(static_cast<int>(255 * alpha), c.R, c.G, c.B);
}
// Knob arcs span 270 degrees, measured clockwise from 12 o'clock.
inline constexpr float kArcStart = -135.f, kArcSpan = 270.f;
inline float arcAngle(double normalized) {
  return kArcStart + kArcSpan * static_cast<float>(normalized);
}
inline IStrokeOptions roundCaps() {
  IStrokeOptions options;
  options.mCapOption = ELineCap::Round;
  options.mJoinOption = ELineJoin::Round;
  return options;
}
// Arc with a soft halo: wider translucent strokes under the main stroke.
inline void glowArc(IGraphics &g, const IColor &color, float cx, float cy, float r, float a1,
                    float a2, float thickness) {
  if (a2 - a1 < 0.05f)
    return;
  for (const auto [extra, alpha] : {std::pair{8.f, .07f}, std::pair{4.f, .13f}}) {
    g.PathClear();
    g.PathArc(cx, cy, r, a1, a2);
    g.PathStroke(withAlpha(color, alpha), thickness + extra, roundCaps());
  }
  g.PathClear();
  g.PathArc(cx, cy, r, a1, a2);
  g.PathStroke(color, thickness, roundCaps());
}
inline void drawCard(IGraphics &g, const IRECT &r, float radius = 12.f) {
  g.FillRoundRect(cardFill, r, radius);
  g.DrawRoundRect(cardEdge, r, radius, nullptr, 1.f);
}
inline float toDb(double gain) {
  return 20.f * static_cast<float>(std::log10(std::max(gain, 1e-6)));
}
} // namespace topend::gui
