#include "TopEnd.h"
#include "IPlug_include_in_plug_src.h"
#include "common/State.h"
#include "gui/Controls.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>
#ifdef TOPEND_ENABLE_GUI_CAPTURE
#include "../../tools/gui_capture.h"
#endif

TopEnd::TopEnd(const InstanceInfo &info)
    : iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets)) {
  GetParam(kMidAir)->InitDouble("Mid Air", 0, 0, 100, .1, "%");
  GetParam(kHighAir)->InitDouble("High Air", 0, 0, 100, .1, "%");
  GetParam(kLink)->InitBool("Link", false);
  GetParam(kTrim)->InitDouble("Output Trim", 0, -12, 0, .1, "dB");
  GetParam(kBypass)->InitBool("Bypass", false);
  MakeDefaultPreset("Neutral", 1);
#if IPLUG_EDITOR
  mMakeGraphicsFunc = [this]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS,
                        GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };
  mLayoutFunc = [this](IGraphics *g) {
    using namespace topend::gui;
    if (g->NControls())
      return; // Scale existing vector controls; do not duplicate on resize.
    g->AttachCornerResizer(EUIResizerMode::Scale, false);
    g->EnableTooltips(true);
    g->LoadFont(kRegular, "Lato-Regular.ttf");
    g->LoadFont(kBold, "Lato-Bold.ttf");
    g->AttachControl(new Surface(IRECT(0, 0, 600, 360), g->LoadBitmap("topend-icon.png")),
                     kCtrlSurface);
    g->AttachControl(new AirKnob(IRECT(), kMidAir, kHighAir, kLink, kBypass, true, "MID AIR",
                                 "Presence  ·  3 kHz", amber),
                     kCtrlMid);
    g->AttachControl(new AirKnob(IRECT(), kHighAir, kMidAir, kLink, kBypass, false, "HIGH AIR",
                                 "Air  ·  9 kHz", mint),
                     kCtrlHigh);
    g->AttachControl(new LinkButton(IRECT(), kLink), kCtrlLink);
    g->AttachControl(new ResponseDisplay(IRECT(), kMidAir, kHighAir, kTrim, kBypass), kCtrlDisplay);
    g->AttachControl(new LevelMeters(IRECT()), kCtrlLevels);
    g->AttachControl(new Toggle(IRECT(408, 18, 496, 42), kNoParameter, "DETAILS", "DETAILS",
                                Toggle::Icon::Curve, mint,
                                [this, g](bool on) { Arrange(*g, details_ = on); }),
                     kCtrlDetails);
    g->GetControlWithTag(kCtrlDetails)->SetValue(details_ ? 1 : 0);
    g->AttachControl(new Toggle(IRECT(504, 18, 582, 42), kBypass, "ACTIVE", "BYPASS",
                                Toggle::Icon::Power, amber));
    g->AttachControl(new TrimSlider(IRECT(330, 312, 576, 342), kTrim));
    Arrange(*g, details_);
#ifdef TOPEND_ENABLE_GUI_CAPTURE
    g->AttachControl(new TopEndCapture(IRECT(0, 0, 600, 360)));
    // Development capture only: "mid,high,trim" sets the controls shown.
    if (const char *values = std::getenv("TOPEND_CAPTURE_VALUES")) {
      float v[3] = {0, 0, 0};
      std::sscanf(values, "%f,%f,%f", &v[0], &v[1], &v[2]);
      for (const auto [index, value] :
           {std::pair{kMidAir, v[0]}, std::pair{kHighAir, v[1]}, std::pair{kTrim, v[2]}}) {
        GetParam(index)->Set(value);
        const double normalized = GetParam(index)->GetNormalized();
        g->ForControlWithParam(index,
                               [normalized](IControl *c) { c->SetValueFromDelegate(normalized); });
      }
    }
    if (std::getenv("TOPEND_CAPTURE_DETAILS")) {
      g->GetControlWithTag(kCtrlDetails)->SetValue(1);
      Arrange(*g, details_ = true);
    }
#endif
  };
#endif
  processor_.prepare(48000);
}
// Compact: two large knobs. Details: smaller knobs, levels and the live response.
void TopEnd::Arrange(IGraphics &g, bool details) {
  using namespace topend::gui;
  const auto place = [&g](int tag, const IRECT &r, bool visible = true) {
    IControl *c = g.GetControlWithTag(tag);
    c->SetTargetAndDrawRECTs(r);
    c->Hide(!visible);
  };
  static_cast<Surface *>(g.GetControlWithTag(kCtrlSurface))->SetDetails(details);
  static_cast<AirKnob *>(g.GetControlWithTag(kCtrlMid))->SetLarge(!details);
  static_cast<AirKnob *>(g.GetControlWithTag(kCtrlHigh))->SetLarge(!details);
  if (details) {
    place(kCtrlMid, IRECT(24, 64, 136, 198));
    place(kCtrlHigh, IRECT(148, 64, 260, 198));
    place(kCtrlLink, IRECT(122, 118, 163, 144));
    place(kCtrlLevels, IRECT(12, 212, 260, 294));
    place(kCtrlDisplay, IRECT(272, 68, 588, 294));
  } else {
    place(kCtrlMid, IRECT(60, 72, 280, 296));
    place(kCtrlHigh, IRECT(320, 72, 540, 296));
    place(kCtrlLink, IRECT(250, 160, 350, 194));
    place(kCtrlLevels, IRECT(12, 212, 260, 294), false);
    place(kCtrlDisplay, IRECT(272, 68, 588, 294), false);
  }
  g.SetAllControlsDirty();
}
void TopEnd::SendMeters(const std::array<float, 4> &values) {
  for (const int tag : {kCtrlMid, kCtrlHigh, kCtrlDisplay, kCtrlLevels}) {
    const topend::gui::MeterData data(tag, values);
    SendControlMsgFromDelegate(tag, ISender<4>::kUpdateMessage, sizeof(data), &data);
  }
}
void TopEnd::OnIdle() {
  const auto packets = meterPackets_.load(std::memory_order_acquire);
  if (packets != seenPackets_) {
    seenPackets_ = packets;
    quietTicks_ = 0;
    meterSender_.TransmitDataToControlsWithTags(*this,
                                                {kCtrlMid, kCtrlHigh, kCtrlDisplay, kCtrlLevels});
  } else if (++quietTicks_ > 5 && quietTicks_ < 150) {
    // No audio (stopped transport): let the displays settle instead of freezing.
    SendMeters({1.f, 1.f, 0.f, 0.f});
  }
}
void TopEnd::OnReset() {
  processor_.setParameters({static_cast<float>(GetParam(kMidAir)->Value()),
                            static_cast<float>(GetParam(kHighAir)->Value()),
                            static_cast<float>(GetParam(kTrim)->Value()),
                            GetParam(kBypass)->Bool()});
  processor_.prepare(GetSampleRate());
  pending_ = {};
  pendingFrames_ = 0;
}
void TopEnd::ProcessBlock(sample **inputs, sample **outputs, int frames) {
  if (frames <= 0)
    return;
  // iPlug parameters use atomic values. Snapshot each process block. Smoothing
  // is sample continuous; the pinned adapter delivers automation at block granularity.
  // Host bypass (VST3 kIsBypass, AU BypassEffect) reaches here through the marked
  // adapter overlays and shares the parameter's crossfade.
  processor_.setParameters({static_cast<float>(GetParam(kMidAir)->Value()),
                            static_cast<float>(GetParam(kHighAir)->Value()),
                            static_cast<float>(GetParam(kTrim)->Value()),
                            GetParam(kBypass)->Bool() || GetBypassed()});
  const int channels = std::min(NInChansConnected(), NOutChansConnected());
  if (channels > 0)
    processor_.process(inputs, outputs, static_cast<std::size_t>(channels),
                       static_cast<std::size_t>(frames));
  for (int c = channels; c < NOutChansConnected(); ++c)
    std::fill_n(outputs[c], frames, static_cast<sample>(0));
  // Editor activity: merge per-block meters, then queue about 60 packets/s.
  // The queue is lock-free; a full queue (editor closed) drops packets.
  const auto m = processor_.meters();
  pending_.midGain = std::min(pending_.midGain, m.midGain);
  pending_.highGain = std::min(pending_.highGain, m.highGain);
  pending_.inputPeak = std::max(pending_.inputPeak, m.inputPeak);
  pending_.outputPeak = std::max(pending_.outputPeak, m.outputPeak);
  pendingFrames_ += frames;
  if (pendingFrames_ >= static_cast<int>(GetSampleRate() / 60)) {
    meterSender_.PushData(
        {kCtrlMid, {pending_.midGain, pending_.highGain, pending_.inputPeak, pending_.outputPeak}});
    meterPackets_.fetch_add(1, std::memory_order_release);
    pending_ = {};
    pendingFrames_ = 0;
  }
}
bool TopEnd::SerializeState(IByteChunk &chunk) const {
  const auto data = topend::encodeState({static_cast<float>(GetParam(kMidAir)->Value()),
                                         static_cast<float>(GetParam(kHighAir)->Value()),
                                         static_cast<float>(GetParam(kTrim)->Value()),
                                         GetParam(kLink)->Bool(), GetParam(kBypass)->Bool()});
  return chunk.PutBytes(data.data(), static_cast<int>(data.size())) > 0;
}
int TopEnd::UnserializeState(const IByteChunk &chunk, int startPos) {
  topend::StateData data{};
  const int end = chunk.GetBytes(data.data(), static_cast<int>(data.size()), startPos);
  if (end < 0)
    return -1;
  const auto state = topend::decodeState(data);
  if (!state)
    return -1;
  GetParam(kMidAir)->Set(state->mid);
  GetParam(kHighAir)->Set(state->high);
  GetParam(kTrim)->Set(state->trimDb);
  GetParam(kLink)->Set(state->link ? 1 : 0);
  GetParam(kBypass)->Set(state->bypass ? 1 : 0);
  OnParamReset(kPresetRecall);
  return end;
}
