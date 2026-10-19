# Editor design

The editor is a compact 600 × 360 logical-pixel vector interface, scaled by
IGraphics for Retina and resizable from 480 × 288 to 1200 × 720 with the corner
handle. It uses a dark blue-grey palette with amber for Mid Air and mint for High
Air, Lato Regular and Bold (SIL OFL 1.1), and the TopEnd icon
(`assets/branding/topend-icon.svg`, a lifting response curve) in the header. Theme tokens live in `src/gui/Theme.h`, controls in `src/gui/Controls.h`
and the layout in `src/plugin/TopEnd.cpp`.

![Compact view](../assets/screenshots/compact.png)

## Compact view

Two large ring knobs show each control's setting as a dim arc. A bright arc with
a soft halo shows how much of that setting acts right now: it shrinks while the
band's dynamics reduce the enhancement, and the subtitle shows the reduction in
dB. A chain button between the knobs toggles Link; when linked, a gradient joins
both rings. The header holds **Details** and **Active/Bypass** switches; the
footer holds the Output slider.

## Details view

![Details view](../assets/screenshots/details.png)

**Details** rearranges the same window. Smaller knobs move to the left, above
input and output peak meters (dBFS; output above 0 dBFS turns red and holds,
since TopEnd does not limit). The response panel draws the steady-state
frequency response twice: for quiet input, and for the current signal using the
live band gains. Bars below show each band's present reduction. The curve comes
from `ResponseCurve` in the DSP core; tests check it against processed tones.

The view stays the same size so it works identically in every host. It is kept
while the plugin instance exists but is not stored in sessions.

## Interaction

Drag or scroll a knob; Shift or Command gives fine control; double-click to
type a value. Link moves both controls by the same normalized amount until one
reaches a limit. Mouse drags, wheel moves and typed values all report one host
edit gesture, covering the linked control too. A quick second click on a switch
toggles again.

## Implementation notes

Drawing happens on the editor thread and redraws only dirty controls, at most
30 frames per second. Meter packets arrive at about 60 per second through a
lock-free queue; displays reduce at once, recover smoothly and settle when
audio stops. The response panel also redraws when parameters change without
audio.

Screenshots are real renders of the AU editor at 2× scale while it processes
audio. A development build (`-DTOPEND_ENABLE_GUI_CAPTURE=ON`) adds a capture
control that writes the editor's own GPU layer to `TOPEND_CAPTURE_PATH`; with
`TOPEND_CAPTURE_WITH_SIGNAL=1` it waits until the displays have shown real input,
and `TOPEND_CAPTURE_DETAILS=1` opens the details view. `topend-au-host --editor
seconds` shows the editor while looping an input file in real time, for example:

```sh
TOPEND_CAPTURE_WITH_SIGNAL=1 TOPEND_CAPTURE_PATH=/tmp/compact.raw \
  build/topend-au-host --bundle "$PWD/build-preview/out/TopEnd.component" \
  --render pink.f32 /dev/null 48000 2 256 0=45 1=62 3=-3 --editor 3
```

The README screenshots use pink noise at about −26 dBFS RMS with Mid 45%, High
62% and Output −3 dB. Production builds contain no capture code.
