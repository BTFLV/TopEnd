# Changelog

## 1.0.0 — 2026-10-19

First public release.

### Sound
- Mid Air (presence above about 3 kHz) and High Air (air above about 9 kHz) as
  parallel enhancement paths with stereo-linked 4:1 dynamics: quiet detail is
  lifted most, loud bright material is enhanced less.
- High Air reduces within a millisecond and recovers along a smooth curve over a
  few hundred milliseconds; its response to noise does not depend on sample rate.
- Controls and Output glide over about 11 ms, Bypass crossfades over 21 ms,
  independent of buffer size. Saved sessions start at their stored settings.
- Zero latency, no oversampling or saturation, sample-exact neutral and bypass.

### Editor
- Compact 600 × 360 vector editor with ring knobs whose bright arc shows the
  enhancement acting right now, Link, Output and Bypass.
- Details view with the live frequency response, per-band reduction and
  input/output peak meters with a hold above 0 dBFS.
- Wheel, typed and linked edits are reported to hosts as single edit gestures.

### Formats and hosts
- AUv2, VST3 and CLAP for Apple Silicon Macs (macOS 11 or newer).
- AU and VST3 host bypass uses TopEnd's crossfade and keeps processing.
- Validated with auval, the VST3 SDK validator, the CLAP validator and
  pluginval at strictness 5.

### Project
- MIT license.
- Signed and notarized release pipeline in GitHub Actions, with an unsigned
  fallback when no credentials are configured.
