#pragma once
#include "ISender.h"
#include "Theme.h"
#include "dsp/Response.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>

namespace topend::gui {
// Meter packets from the audio thread: Mid gain, High gain, input peak, output peak.
using MeterData = ISenderData<4, float>;
#ifdef TOPEND_ENABLE_GUI_CAPTURE
// Development capture only: counts packets that carried real input signal.
inline int captureSignalPackets = 0;
#endif

// Display ballistics, applied per message (about 50 per second): reductions
// appear at once and recover smoothly; peak readouts fall by 30 dB/s.
struct Activity {
  float midGain = 1, highGain = 1, inputDb = -120, outputDb = -120;
  int clipHold = 0;
  bool update(int msgTag, int size, const void *data) {
    if (msgTag != ISender<4>::kUpdateMessage || size != static_cast<int>(sizeof(MeterData)))
      return false;
    const auto &d = *static_cast<const MeterData *>(data);
#ifdef TOPEND_ENABLE_GUI_CAPTURE
    captureSignalPackets += d.vals[2] > 0.f;
#endif
    const auto gain = [](float &shown, float value) {
      shown = value < shown ? value : shown + .25f * (value - shown);
    };
    const auto peak = [](float &shown, float value) {
      const float db = toDb(value);
      shown = db > shown ? db : std::max(db, shown - .6f);
    };
    gain(midGain, d.vals[0]);
    gain(highGain, d.vals[1]);
    peak(inputDb, d.vals[2]);
    peak(outputDb, d.vals[3]);
    clipHold = d.vals[3] > 1.f ? 75 : std::max(0, clipHold - 1);
    return true;
  }
};

class Surface final : public IControl {
public:
  Surface(const IRECT &r, IBitmap logo) : IControl(r), logo_(logo) {
    SetIgnoreMouse(true);
  }
  void SetDetails(bool details) {
    details_ = details;
    SetDirty(false);
  }
  void Draw(IGraphics &g) override {
    g.PathClear();
    g.PathRect(mRECT);
    g.PathFill(IPattern::CreateLinearGradient(mRECT, EDirection::Vertical,
                                              {{backgroundTop, 0.f}, {backgroundBottom, 1.f}}));
    // A faint warm-to-cool wash behind the header echoes the two bands.
    g.PathClear();
    g.PathRect(IRECT(0, 0, 600, 2));
    g.PathFill(IPattern::CreateLinearGradient(
        0, 0, 600, 0, {{withAlpha(amber, .7f), 0.f}, {withAlpha(mint, .7f), 1.f}}));
    g.DrawFittedBitmap(logo_, IRECT(14, 10, 54, 50)); // Icon art leaves room for its shadow.
    g.DrawText(text(20, ink, EAlign::Near, kBold), "TopEnd", IRECT(60, 12, 260, 34));
    g.DrawText(text(10, muted, EAlign::Near), "Dynamic presence & air", IRECT(61, 32, 300, 46));
    g.DrawLine(cardEdge, 18, 58, 582, 58);
    drawCard(g, IRECT(12, 304, 588, 350), 11);
    g.DrawText(text(10, faint, EAlign::Near), "Presence.  Definition.  Air.",
               IRECT(28, 304, 300, 350));
  }

private:
  IBitmap logo_;
  bool details_ = false;
};

// IGraphics brackets mouse drags with host gestures, but sends wheel and typed
// edits without them. Bracket those edits too, so hosts record and undo them.
class GestureKnob : public IKnobControlBase {
public:
  using IKnobControlBase::IKnobControlBase;
  void OnMouseWheel(float x, float y, const IMouseMod &mod, float d) override {
    BeginEdit();
    IKnobControlBase::OnMouseWheel(x, y, mod, d);
    EndEdit();
  }
  void SetValueFromUserInput(double value, int index = 0) override {
    BeginEdit();
    IKnobControlBase::SetValueFromUserInput(value, index);
    EndEdit();
  }

protected:
  virtual void BeginEdit() {
    GetDelegate()->BeginInformHostOfParamChangeFromUI(GetParamIdx());
  }
  virtual void EndEdit() {
    GetDelegate()->EndInformHostOfParamChangeFromUI(GetParamIdx());
  }
  bool Bypassed(int bypassParameter) {
    return GetDelegate()->GetParam(bypassParameter)->Bool();
  }
};

// Main control. The dim arc is the setting; the bright arc shows how much of it
// acts right now, shrinking while the band's dynamics reduce the enhancement.
class AirKnob final : public GestureKnob {
public:
  AirKnob(const IRECT &bounds, int parameter, int peer, int linkParameter, int bypassParameter,
          bool mid, const char *label, const char *subtitle, IColor accent)
      : GestureKnob(bounds, parameter), peer_(peer), linkParameter_(linkParameter),
        bypassParameter_(bypassParameter), mid_(mid), label_(label), subtitle_(subtitle),
        accent_(accent) {
    mDisablePrompt = false;
    SetActionFunction([this](IControl *) {
      const double delta = GetValue() - previous_;
      previous_ = GetValue();
      if (!Linked())
        return;
      const double next =
          std::clamp(GetDelegate()->GetParam(peer_)->GetNormalized() + delta, 0.0, 1.0);
      // Normally the peer gesture spans the whole edit; Link may change mid-edit.
      const bool single = !peerGesture_;
      if (single)
        GetDelegate()->BeginInformHostOfParamChangeFromUI(peer_);
      GetDelegate()->SendParameterValueFromUI(peer_, next);
      GetUI()->ForControlWithParam(peer_, [next](IControl *c) { c->SetValueFromDelegate(next); });
      if (single)
        GetDelegate()->EndInformHostOfParamChangeFromUI(peer_);
    });
  }
  void SetLarge(bool large) {
    large_ = large;
    SetDirty(false);
  }
  void SetValueFromDelegate(double value, int index = 0) override {
    GestureKnob::SetValueFromDelegate(value, index);
    previous_ = GetValue();
  }
  void OnMsgFromDelegate(int msgTag, int size, const void *data) override {
    if (activity_.update(msgTag, size, data))
      SetDirty(false);
  }
  void OnMouseDown(float x, float y, const IMouseMod &mod) override {
    previous_ = GetValue();
    BeginPeer();
    GestureKnob::OnMouseDown(x, y, mod);
  }
  void OnMouseUp(float x, float y, const IMouseMod &mod) override {
    GestureKnob::OnMouseUp(x, y, mod);
    EndPeer();
  }
  void OnMouseDblClick(float, float, const IMouseMod &) override {
    const float cx = mRECT.MW(), cy = CenterY();
    PromptUserInput(IRECT(cx - 44, cy - 16, cx + 44, cy + 16));
  }
  void Draw(IGraphics &g) override {
    const float r = Radius(), cx = mRECT.MW(), cy = CenterY();
    const float stroke = large_ ? 7.f : 5.f;
    const bool bypassed = Bypassed(bypassParameter_);
    const IColor accent = bypassed ? withAlpha(accent_, .35f) : accent_;
    g.DrawText(text(large_ ? 11.f : 10.f, mMouseIsOver ? ink : muted, EAlign::Center, kBold),
               label_, IRECT(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 18));
    // Body with a soft top light, then the track and arcs.
    const float body = r - stroke - (large_ ? 9.f : 6.f);
    g.PathClear();
    g.PathCircle(cx, cy, body);
    g.PathFill(IPattern::CreateRadialGradient(
        cx, cy - body * .45f, body * 1.4f,
        {{IColor(255, 40, 47, 55), 0.f}, {IColor(255, 21, 25, 30), 1.f}}));
    g.DrawCircle(cardEdge, cx, cy, body, nullptr, 1.f);
    g.PathClear();
    g.PathArc(cx, cy, r, kArcStart, kArcStart + kArcSpan);
    g.PathStroke(track, stroke, roundCaps());
    const double value = GetValue();
    const double gain = mid_ ? activity_.midGain : activity_.highGain;
    if (value > 0.001) {
      g.PathClear();
      g.PathArc(cx, cy, r, kArcStart, arcAngle(value));
      g.PathStroke(withAlpha(accent, .32f), stroke, roundCaps());
      glowArc(g, accent, cx, cy, r, kArcStart, arcAngle(value * gain), stroke);
    }
    const float a = arcAngle(value) * static_cast<float>(3.141592653589793 / 180.0);
    g.FillCircle(ink, cx + std::sin(a) * (body - 7), cy - std::cos(a) * (body - 7),
                 large_ ? 2.5f : 2.f);
    char number[16];
    std::snprintf(number, sizeof(number), "%.1f", value * 100);
    const float size = large_ ? 30.f : 19.f;
    g.DrawText(text(size, bypassed ? muted : ink, EAlign::Center, kBold), number,
               IRECT(cx - body, cy - size * .62f, cx + body, cy + size * .38f));
    g.DrawText(text(large_ ? 10.f : 8.f, accent), "%",
               IRECT(cx - 20, cy + size * .38f, cx + 20, cy + size * .38f + 14));
    if (large_) {
      char detail[48];
      const float reduction = toDb(gain);
      if (reduction < -0.1f && !bypassed)
        std::snprintf(detail, sizeof(detail), "%s  ·  %.1f dB", subtitle_, reduction);
      else
        std::snprintf(detail, sizeof(detail), "%s", subtitle_);
      g.DrawText(text(10, muted), detail, IRECT(mRECT.L, cy + r + 10, mRECT.R, cy + r + 26));
    }
  }

private:
  float Radius() const {
    return mRECT.W() * (large_ ? .36f : .37f);
  }
  float CenterY() const {
    return mRECT.T + 26 + Radius();
  }
  bool Linked() {
    return GetDelegate()->GetParam(linkParameter_)->Bool();
  }
  // One host gesture for the linked peer per edit, matching the dragged control.
  void BeginPeer() {
    if (!peerGesture_ && Linked()) {
      peerGesture_ = true;
      GetDelegate()->BeginInformHostOfParamChangeFromUI(peer_);
    }
  }
  void EndPeer() {
    if (peerGesture_) {
      peerGesture_ = false;
      GetDelegate()->EndInformHostOfParamChangeFromUI(peer_);
    }
  }
  void BeginEdit() override {
    GestureKnob::BeginEdit();
    BeginPeer();
  }
  void EndEdit() override {
    EndPeer();
    GestureKnob::EndEdit();
  }
  int peer_, linkParameter_, bypassParameter_;
  bool mid_, large_ = true, peerGesture_ = false;
  const char *label_;
  const char *subtitle_;
  IColor accent_;
  Activity activity_;
  double previous_ = 0;
};

// Pill-shaped switch with an icon. Bound to a parameter, or to a callback when
// parameter is kNoParameter.
class Toggle final : public IControl {
public:
  enum class Icon { Power, Curve };
  Toggle(const IRECT &r, int parameter, const char *off, const char *on, Icon icon, IColor onColor,
         std::function<void(bool)> onChange = {})
      : IControl(r, parameter), off_(off), on_(on), icon_(icon), onColor_(onColor),
        onChange_(std::move(onChange)) {
    // A quick second click toggles again, instead of resetting to the default
    // outside a host gesture.
    mDblAsSingleClick = true;
  }
  void OnMouseDown(float, float, const IMouseMod &) override {
    SetValue(GetValue() >= .5 ? 0 : 1);
    SetDirty(true);
    if (onChange_)
      onChange_(GetValue() >= .5);
  }
  void Draw(IGraphics &g) override {
    const bool on = GetValue() >= .5;
    const IColor color = on ? onColor_ : (mMouseIsOver ? ink : muted);
    const float radius = mRECT.H() / 2;
    g.FillRoundRect(on ? withAlpha(onColor_, .14f) : cardFill, mRECT, radius);
    g.DrawRoundRect(on ? withAlpha(onColor_, .8f) : (mMouseIsOver ? faint : cardEdge), mRECT,
                    radius, nullptr, 1.f);
    const float ix = mRECT.L + 15, iy = mRECT.MH();
    g.PathClear();
    if (icon_ == Icon::Power) {
      g.PathArc(ix, iy, 4.5f, 40, 320);
      g.PathMoveTo(ix, iy - 6);
      g.PathLineTo(ix, iy - 1);
    } else {
      g.PathMoveTo(ix - 6, iy + 3);
      g.PathCubicBezierTo(ix - 2, iy + 3, ix - 1, iy - 4, ix + 6, iy - 4);
    }
    g.PathStroke(color, 1.5f, roundCaps());
    g.DrawText(text(9.5f, color, EAlign::Near, kBold), on ? on_ : off_,
               IRECT(ix + 10, mRECT.T, mRECT.R - 8, mRECT.B));
  }

private:
  const char *off_;
  const char *on_;
  Icon icon_;
  IColor onColor_;
  std::function<void(bool)> onChange_;
};

// Chain button between the knobs; when linked, a gradient joins both rings.
class LinkButton final : public IControl {
public:
  LinkButton(const IRECT &r, int parameter) : IControl(r, parameter) {
    mDblAsSingleClick = true;
    SetTooltip("Link: moving one control moves the other by the same amount");
  }
  bool IsHit(float x, float y) const override {
    return std::hypot(x - mRECT.MW(), y - mRECT.MH()) <= Radius() + 2;
  }
  void OnMouseDown(float, float, const IMouseMod &) override {
    SetValue(GetValue() >= .5 ? 0 : 1);
    SetDirty(true);
  }
  void Draw(IGraphics &g) override {
    const bool on = GetValue() >= .5;
    const float cx = mRECT.MW(), cy = mRECT.MH(), r = Radius();
    if (on) {
      g.PathClear();
      g.PathMoveTo(mRECT.L, cy);
      g.PathLineTo(cx - r, cy);
      g.PathMoveTo(cx + r, cy);
      g.PathLineTo(mRECT.R, cy);
      g.PathStroke(IPattern::CreateLinearGradient(
                       mRECT.L, cy, mRECT.R, cy,
                       {{withAlpha(amber, .55f), 0.f}, {withAlpha(mint, .55f), 1.f}}),
                   1.5f, roundCaps());
    }
    g.FillCircle(on ? IColor(255, 30, 38, 42) : cardFill, cx, cy, r);
    g.DrawCircle(on ? withAlpha(mint, .8f) : (mMouseIsOver ? faint : cardEdge), cx, cy, r, nullptr,
                 1.f);
    const IColor color = on ? ink : (mMouseIsOver ? ink : muted);
    const float w = r * .42f, h = r * .26f;
    for (const float dx : {-w * .55f, w * .55f}) {
      g.PathClear();
      g.PathRoundRect(IRECT(cx + dx - w / 2, cy - h / 2, cx + dx + w / 2, cy + h / 2), h / 2);
      g.PathStroke(color, 1.4f);
    }
  }

private:
  float Radius() const {
    return std::min(mRECT.H() / 2.f, 17.f);
  }
};

class TrimSlider final : public GestureKnob {
public:
  TrimSlider(const IRECT &r, int parameter) : GestureKnob(r, parameter, EDirection::Horizontal) {
    mDisablePrompt = false;
  }
  void OnMouseDblClick(float, float, const IMouseMod &) override {
    PromptUserInput(IRECT(mRECT.R - 64, mRECT.MH() - 12, mRECT.R, mRECT.MH() + 12));
  }
  void Draw(IGraphics &g) override {
    const float cy = mRECT.MH(), l = mRECT.L + 58, rr = mRECT.R - 70;
    g.DrawText(text(9.5f, mMouseIsOver ? ink : muted, EAlign::Near, kBold), "OUTPUT",
               IRECT(mRECT.L, mRECT.T, l, mRECT.B));
    const float x = l + (rr - l) * static_cast<float>(GetValue());
    g.PathClear();
    g.PathMoveTo(l, cy);
    g.PathLineTo(rr, cy);
    g.PathStroke(track, 4.f, roundCaps());
    g.PathClear();
    g.PathMoveTo(x, cy);
    g.PathLineTo(rr, cy);
    g.PathStroke(withAlpha(ink, .35f), 4.f, roundCaps());
    g.FillCircle(ink, x, cy, mMouseIsOver ? 6.5f : 5.5f);
    char value[24];
    std::snprintf(value, sizeof(value), "%+.1f dB", GetParam()->FromNormalized(GetValue()));
    g.DrawText(text(14, ink, EAlign::Far, kBold), value, IRECT(rr + 8, mRECT.T, mRECT.R, mRECT.B));
  }
};

// Live frequency response: what quiet material receives versus what the
// current signal receives, with each band's present reduction below.
class ResponseDisplay final : public IControl {
public:
  ResponseDisplay(const IRECT &r, int mid, int high, int trim, int bypass)
      : IControl(r), ids_{mid, high, trim, bypass} {
    SetIgnoreMouse(true);
    curve_.prepare(48000.0);
  }
  void OnMsgFromDelegate(int msgTag, int size, const void *data) override {
    if (activity_.update(msgTag, size, data))
      SetDirty(false);
  }
  bool IsDirty() override {
    return IControl::IsDirty() || !Same(Read(), drawn_);
  }
  void Draw(IGraphics &g) override {
    drawn_ = Read();
    drawCard(g, mRECT, 12);
    const IRECT plot(mRECT.L + 34, mRECT.T + 30, mRECT.R - 14, mRECT.B - 64);
    // Grid: decibel lines and decade/half-decade frequency lines.
    for (int db = 0; db <= 24; db += 6) {
      const float y = Y(plot, db);
      g.DrawLine(db == 0 ? withAlpha(ink, .16f) : grid, plot.L, y, plot.R, y);
      char label[8];
      std::snprintf(label, sizeof(label), db ? "+%d" : "0", db);
      g.DrawText(text(8, faint, EAlign::Far), label, IRECT(mRECT.L, y - 6, plot.L - 6, y + 6));
    }
    for (const double hz : {50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000.}) {
      const float x = X(plot, hz);
      g.DrawLine(grid, x, plot.T, x, plot.B);
      const bool major = hz == 100. || hz == 1000. || hz == 10000.;
      if (major)
        g.DrawText(text(8, faint), hz >= 1000 ? (hz == 1000 ? "1k" : "10k") : "100",
                   IRECT(x - 16, plot.B + 2, x + 16, plot.B + 14));
    }
    g.DrawText(text(9.5f, muted, EAlign::Near, kBold), "RESPONSE",
               IRECT(mRECT.L + 14, mRECT.T + 8, mRECT.L + 120, mRECT.T + 24));
    // Legend.
    g.DrawLine(withAlpha(ink, .35f), mRECT.R - 168, mRECT.T + 16, mRECT.R - 154, mRECT.T + 16,
               nullptr, 1.2f);
    g.DrawText(text(8.5f, muted, EAlign::Near), "quiet input",
               IRECT(mRECT.R - 150, mRECT.T + 8, mRECT.R - 92, mRECT.T + 24));
    g.PathClear();
    g.PathMoveTo(mRECT.R - 82, mRECT.T + 16);
    g.PathLineTo(mRECT.R - 68, mRECT.T + 16);
    g.PathStroke(Spectrum(plot, 1.f), 2.f, roundCaps());
    g.DrawText(text(8.5f, muted, EAlign::Near), "now",
               IRECT(mRECT.R - 64, mRECT.T + 8, mRECT.R - 14, mRECT.T + 24));

    const Parameters p = drawn_;
    std::array<float, kPoints> quiet{}, now{};
    for (int i = 0; i < kPoints; ++i) {
      const double hz = Frequency(i);
      quiet[i] = static_cast<float>(curve_.gainDb(hz, p));
      now[i] = static_cast<float>(curve_.gainDb(hz, p, activity_.midGain, activity_.highGain));
    }
    // Area under the present curve, then both outlines.
    g.PathClear();
    g.PathMoveTo(plot.L, Y(plot, 0));
    for (int i = 0; i < kPoints; ++i)
      g.PathLineTo(X(plot, Frequency(i)), Y(plot, now[i]));
    g.PathLineTo(plot.R, Y(plot, 0));
    g.PathClose();
    g.PathFill(Spectrum(plot, .16f));
    Line(g, plot, quiet);
    g.PathStroke(withAlpha(ink, .35f), 1.2f, roundCaps());
    Line(g, plot, now);
    g.PathStroke(Spectrum(plot, p.bypass ? .3f : 1.f), 2.f, roundCaps());
    if (p.bypass)
      g.DrawText(text(11, muted, EAlign::Center, kBold), "BYPASSED", plot);

    // Present reduction of each band's enhancement.
    const float top = mRECT.B - 40, w = (mRECT.W() - 42) / 2;
    Reduction(g, IRECT(mRECT.L + 14, top, mRECT.L + 14 + w, top + 28), "MID", activity_.midGain,
              amber, p.mid > 0 && !p.bypass);
    Reduction(g, IRECT(mRECT.R - 14 - w, top, mRECT.R - 14, top + 28), "HIGH", activity_.highGain,
              mint, p.high > 0 && !p.bypass);
  }

private:
  static constexpr int kPoints = 120;
  static constexpr double kLow = 20.0, kHigh = 20000.0;
  static constexpr float kDbMin = -6.f, kDbMax = 27.f;
  static double Frequency(int i) {
    return kLow * std::pow(kHigh / kLow, i / (kPoints - 1.0));
  }
  static float X(const IRECT &r, double hz) {
    return r.L + r.W() * static_cast<float>(std::log(hz / kLow) / std::log(kHigh / kLow));
  }
  static float Y(const IRECT &r, float db) {
    return r.B - r.H() * (std::clamp(db, kDbMin, kDbMax) - kDbMin) / (kDbMax - kDbMin);
  }
  static IPattern Spectrum(const IRECT &r, float alpha) {
    return IPattern::CreateLinearGradient(
        X(r, 1500), 0, X(r, 10000), 0,
        {{withAlpha(amber, alpha), 0.f}, {withAlpha(mint, alpha), 1.f}});
  }
  static void Line(IGraphics &g, const IRECT &r, const std::array<float, kPoints> &db) {
    g.PathClear();
    for (int i = 0; i < kPoints; ++i) {
      const float x = X(r, Frequency(i)), y = Y(r, db[i]);
      if (i)
        g.PathLineTo(x, y);
      else
        g.PathMoveTo(x, y);
    }
  }
  static void Reduction(IGraphics &g, const IRECT &r, const char *label, float gain,
                        const IColor &color, bool active) {
    const float db = active ? toDb(gain) : 0.f;
    g.DrawText(text(9, muted, EAlign::Near, kBold), label, IRECT(r.L, r.T, r.L + 40, r.T + 12));
    char value[16];
    std::snprintf(value, sizeof(value), active ? "%.1f dB" : "—", db);
    g.DrawText(text(9, active ? ink : faint, EAlign::Far), value,
               IRECT(r.R - 60, r.T, r.R, r.T + 12));
    const IRECT bar(r.L, r.T + 17, r.R, r.T + 22);
    g.FillRoundRect(track, bar, 2.5f);
    const float fraction = std::clamp(-db / 24.f, 0.f, 1.f);
    if (fraction > 0.002f)
      g.FillRoundRect(color, IRECT(bar.L, bar.T, bar.L + bar.W() * fraction, bar.B), 2.5f);
  }
  Parameters Read() {
    auto *d = GetDelegate();
    return {static_cast<float>(d->GetParam(ids_[0])->Value()),
            static_cast<float>(d->GetParam(ids_[1])->Value()),
            static_cast<float>(d->GetParam(ids_[2])->Value()), d->GetParam(ids_[3])->Bool()};
  }
  static bool Same(const Parameters &a, const Parameters &b) {
    return a.mid == b.mid && a.high == b.high && a.trimDb == b.trimDb && a.bypass == b.bypass;
  }
  std::array<int, 4> ids_;
  ResponseCurve curve_;
  Activity activity_;
  Parameters drawn_{-1, -1, 1, false};
};

// Input and output peak levels. TopEnd does not limit, so output above 0 dBFS
// is marked and held briefly.
class LevelMeters final : public IControl {
public:
  explicit LevelMeters(const IRECT &r) : IControl(r) {
    SetIgnoreMouse(true);
  }
  void OnMsgFromDelegate(int msgTag, int size, const void *data) override {
    if (activity_.update(msgTag, size, data))
      SetDirty(false);
  }
  void Draw(IGraphics &g) override {
    drawCard(g, mRECT, 12);
    g.DrawText(text(9.5f, muted, EAlign::Near, kBold), "LEVELS",
               IRECT(mRECT.L + 14, mRECT.T + 8, mRECT.L + 120, mRECT.T + 24));
    g.DrawText(text(8.5f, faint, EAlign::Far), "dBFS peak",
               IRECT(mRECT.R - 100, mRECT.T + 8, mRECT.R - 14, mRECT.T + 24));
    const IRECT rows(mRECT.L + 14, mRECT.T + 30, mRECT.R - 14, mRECT.B - 10);
    const float h = rows.H() / 2;
    Row(g, IRECT(rows.L, rows.T, rows.R, rows.T + h), "IN", activity_.inputDb, false);
    Row(g, IRECT(rows.L, rows.B - h, rows.R, rows.B), "OUT", activity_.outputDb,
        activity_.clipHold > 0);
  }

private:
  static constexpr float kMin = -48.f, kMax = 6.f;
  static void Row(IGraphics &g, const IRECT &r, const char *label, float db, bool clipped) {
    g.DrawText(text(9, muted, EAlign::Near, kBold), label, IRECT(r.L, r.T, r.L + 30, r.B));
    char value[16];
    std::snprintf(value, sizeof(value), db > -100 ? "%.1f" : "—", db);
    g.DrawText(text(9, clipped ? alert : ink, EAlign::Far), value, IRECT(r.R - 34, r.T, r.R, r.B));
    const IRECT bar(r.L + 32, r.MH() - 2.5f, r.R - 40, r.MH() + 2.5f);
    g.FillRoundRect(track, bar, 2.5f);
    const float zero = bar.L + bar.W() * (0 - kMin) / (kMax - kMin);
    const float fraction = std::clamp((db - kMin) / (kMax - kMin), 0.f, 1.f);
    if (fraction > 0.f) {
      g.PathClear();
      g.PathRoundRect(IRECT(bar.L, bar.T, bar.L + bar.W() * fraction, bar.B), 2.5f);
      g.PathFill(IPattern::CreateLinearGradient(bar.L, 0, bar.R, 0,
                                                {{mint, 0.f}, {amber, .8f}, {alert, .9f}}));
    }
    g.DrawLine(withAlpha(ink, .5f), zero, bar.T - 3, zero, bar.B + 3);
  }
  Activity activity_;
};
} // namespace topend::gui
