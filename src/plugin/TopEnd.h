#pragma once
#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"
#include "dsp/TopEndProcessor.h"
#include <atomic>
#include <cstdint>

// Append only. IDs are the cross-format automation contract.
enum EParams { kMidAir = 0, kHighAir = 1, kLink = 2, kTrim = 3, kBypass = 4, kNumParams = 5 };
enum ECtrlTags {
  kCtrlSurface,
  kCtrlMid,
  kCtrlHigh,
  kCtrlLink,
  kCtrlDetails,
  kCtrlDisplay,
  kCtrlLevels
};
inline constexpr int kNumPresets = 1;
using namespace iplug;
using namespace igraphics;

class TopEnd final : public Plugin {
public:
  explicit TopEnd(const InstanceInfo &info);
  void ProcessBlock(sample **inputs, sample **outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
  bool SerializeState(IByteChunk &chunk) const override;
  int UnserializeState(const IByteChunk &chunk, int startPos) override;

private:
  void Arrange(IGraphics &g, bool details);
  void SendMeters(const std::array<float, 4> &values);

  topend::TopEndProcessor processor_;
  // Audio thread: activity merged over blocks, sent about 60 times per second.
  ISender<4> meterSender_;
  topend::Meters pending_;
  int pendingFrames_ = 0;
  std::atomic<std::uint32_t> meterPackets_{0};
  // Main thread only.
  std::uint32_t seenPackets_ = 0;
  int quietTicks_ = 0;
  bool details_ = false; // Editor view; kept while the instance lives.
};
