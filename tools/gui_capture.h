#pragma once
// Opt-in development renderer. Never included in installed production builds.
#include "IControl.h"
#include "IGraphics.h"
#include <cstdio>
#include <cstdlib>

class TopEndCapture final : public igraphics::IControl {
public:
  explicit TopEndCapture(const igraphics::IRECT &bounds) : IControl(bounds) {
    SetIgnoreMouse(true);
  }
  void Draw(igraphics::IGraphics &g) override {
    const char *path = std::getenv("TOPEND_CAPTURE_PATH");
    if (done_ || !path)
      return;
    // TOPEND_CAPTURE_WITH_SIGNAL waits until the editor has shown real audio
    // activity for a while (for example while pluginval processes noise).
    const bool waitForSignal = std::getenv("TOPEND_CAPTURE_WITH_SIGNAL") != nullptr;
    if (frames_++ < 3 || (waitForSignal && topend::gui::captureSignalPackets < 12)) {
      SetDirty(false);
      return;
    }
    done_ = true;
    // Draw exactly the production controls into an offscreen GPU layer, then
    // read that layer. This captures our own renderer, without screen permissions.
    g.StartLayer(this, mRECT);
    for (int i = 0; i < g.NControls(); ++i)
      if (g.GetControl(i) != this && !g.GetControl(i)->IsHidden())
        g.GetControl(i)->Draw(g);
    auto layer = g.EndLayer();
    igraphics::RawBitmapData pixels;
    g.GetLayerBitmapData(layer, pixels);
    const auto *bitmap = layer->GetAPIBitmap();
    if (FILE *file = std::fopen(path, "wb")) {
      const int width = bitmap->GetWidth(), height = bitmap->GetHeight();
      std::fwrite(&width, sizeof(width), 1, file);
      std::fwrite(&height, sizeof(height), 1, file);
      std::fwrite(pixels.Get(), 1, static_cast<std::size_t>(pixels.GetSize()), file);
      std::fclose(file);
    }
  }

private:
  bool done_ = false;
  int frames_ = 0;
};
