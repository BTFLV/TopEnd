# Architecture

## Framework and dependencies

TopEnd uses **iPlug2** with C++20 and CMake for AUv2, VST3 and CLAP, built for
Apple Silicon (arm64, macOS 11 or newer). IGraphics
provides vector drawing, HiDPI scaling and a NanoVG/Metal backend. The DSP core
has no framework dependency, and no JUCE code is linked.

iPlug2 has no current stable numbered release, so it is pinned as a Git
submodule at `d54f69050f517e43b941d88c2a170f0a840b9ee4`. VST3 SDK 3.8.0, CLAP
1.2.10 and clap-helpers (`29389e43489b1af44b654ef5a7b4ee0af4bbc13b`) are pinned
submodules as well. `tools/bootstrap.py` connects them to iPlug2's expected
layout without downloads. Python tools and formatters use pinned requirement
files; validator downloads have versioned URLs and SHA-256 checks
(`tools/fetch_validators.py`).

Licenses: iPlug2, WDL, NanoVG and NanoSVG are zlib style; MetalNanoVG, CLAP,
clap-helpers and the VST3 SDK are MIT. Lato Regular and Bold are unmodified
SIL OFL 1.1 fonts. Notices are bundled with every format as
`THIRD_PARTY_NOTICES.txt` together with the OFL text. Pluginval and the CLAP
validator are separate development tools, not runtime dependencies.

## Source layout

| Path | Responsibility |
| --- | --- |
| `src/dsp` | Filters, level detectors, ramps, the processor and the display response model. C++ standard library only. |
| `src/common/State.h` | Versioned, validated little-endian state payload. |
| `src/plugin` | iPlug2 adapter: parameters, reset, processing, meters, state and editor layout. |
| `src/gui` | Theme and vector controls. |
| `tools/` | Renderer, headless AU/CLAP hosts, adapter checks, validation, install, packaging, product graphs and listening pairs. |
| `.github/workflows` | CI on every push; tagged releases with optional signing and notarization. |
| `tests/` | DSP/state tests, benchmark and a REAPER smoke script. |

No licensing, account, network access or analytics exists in the plugin.

## Threading and parameters

Stable automation IDs are independent of format. Each audio block snapshots
iPlug2's atomic parameter values; the core has one owner and needs no
synchronization. Changes ramp per sample over a fixed time. The pinned framework
delivers host automation at block granularity; sample-offset handling is future
work. Hosts commonly reset a plugin before restoring a session, so the core
applies values set before the first processed sample immediately.

Editor activity travels from the audio thread through iPlug2's lock-free
`ISender` queue at about 60 packets per second. The main-thread idle timer
forwards packets to the controls and lets displays settle when audio stops.

## Marked compatibility overlays

`tools/patch_iplug.py` verifies exact pinned source matches and writes plainly
marked copies into the build tree; submodules stay unmodified.

- CLAP: partial stream writes, host notification after restoring values,
  display value bounds, string terminators and mono/stereo port reporting.
- AU: portable storage instead of a variable-length stack array in a property path.
- Host bypass (AU `BypassEffect`, VST3 `kIsBypass`): upstream reset the plugin
  from the AU property thread, racing the render thread, and both formats
  hard-switched to pass-through. The overlays only record the flag and keep
  processing; `ProcessBlock` routes it into TopEnd's 21 ms crossfade.
- Cocoa: explicit enum casts for Clang 21.
- VST3 SDK: an exactly representable bound in `String::printFloat`, avoiding an
  out-of-range integer conversion.
- Build: Metal's Objective-C classes get a TopEnd-and-format prefix and a
  separate object target per format, so formats and other plugins loaded in one
  process cannot collide. Unused OpenGL, FlexBox and web-view targets are excluded.
