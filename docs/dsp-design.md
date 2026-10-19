# DSP design

TopEnd is built from standard parts: second-order high-pass filters, level
detectors, 4:1 gain computers and smoothing. There are no waveshapers, no
oversampling and no impulse-response convolution.

## Signal model

For each channel, a transparent dry contribution and two parallel enhancement
paths are summed before output trim:

`y = trim · ((1 + dm + dh) · x + am · gm · HPmid(x) + ah · gh · HPhigh(x))`

- `HPmid` is an RBJ second-order high-pass at 3 kHz, Q 0.63; `HPhigh` is at
  9 kHz, Q 0.707. Coefficients and state are double precision.
- `dm, dh` add a small broadband contribution and `am, ah` set each path's
  amount. Both come from a measured 101-position calibration table per control
  (`src/dsp/ControlMapping.h`), linearly interpolated. The control law is
  deliberately nonlinear: High 50% gives about a third of the 100% amount.
- `gm, gh` are dynamic gains (at most 1) that reduce each path's enhancement as
  its level rises. At zero amount and zero trim the output equals the input
  sample for sample.

Detection is stereo linked: each detector takes the larger channel magnitude
and both channels share one gain. Audio is never summed between channels, and
identical stereo channels produce exactly the mono result.

## Dynamics

Every stage is a 4:1 downward compressor acting on its path's gain:
`g = min(1, (level / threshold)^-0.75)`.

| Stage | Level detector | Threshold | Timing |
| --- | --- | --- | --- |
| Mid | Peak of the 3 kHz path, instant attack | −30 dBFS | Two-stage release, 75 ms and 33 ms |
| High 1 | Mean square of the 9 kHz path (10 ms), read as peak equivalent | −39.7 dBFS | Gain smoothed in dB: 1 ms attack, 75 ms release, two poles |
| High 2 | Peak of the stage-1-reduced 9 kHz path, scaled by `ah^0.64` | −27.3 dB | 0.75/0.15 ms peak release; gain smoothed 16 ms / 25 ms |

High uses two cascaded stages. The second stage sees the band after the first
stage's reduction, scaled with the control amount, so stronger settings
compress harder and a short dip follows sudden loud material before the gain
settles. Gain smoothing acts on the logarithm of the gain after the threshold,
so reduction starts within the first millisecond, and recovery follows a slow
S-shaped curve over a few hundred milliseconds. High's first stage averages
power, so its response to noise does not depend on sample rate.

The constants were tuned by comparing renders with measured material across
44.1–192 kHz and then rounded. Rounding changed the spectral match by at most
0.02 dB.

## Realtime contract

One audio thread owns each processor. Fixed arrays hold all state; processing
allocates no memory, takes no locks, calls no host API and performs no I/O.
Float and double entry points accept any block length, including zero, and
work in place. Nonfinite controls are sanitized, nonfinite input becomes zero
and extreme finite input is bounded. Filter, detector and smoothing states flush
or snap tiny values, so silence never leaves subnormal state. Output never
depends on how a host splits blocks.

Mid, High and Output Trim ramp linearly over 512 samples at 48 kHz (10.7 ms;
trim in decibels). Bypass crossfades over 21.3 ms. Ramp times are fixed in
time, independent of the host buffer size; a new target restarts the ramp from
the current value. Values set before the first processed audio after
`prepare()` or `reset()` apply immediately. Latency is zero. There is no
limiter: output may exceed 0 dBFS, and Output Trim compensates.

`Meters` reports, per processed block, the strongest reduction of each path and
the input/output peak. `ResponseCurve` (`src/dsp/Response.h`) evaluates the
steady-state magnitude response for given controls and band gains; tests check
it against processed tones, and the editor draws it.

## State contract

Stable parameter IDs: 0 Mid Air, 1 High Air, 2 Link, 3 Output Trim, 4 Bypass.
Mid and High are 0–100%, Trim −12–0 dB. Link is an editor gesture: changing
either knob moves the other by the same normalized amount until a range limit;
automation lanes stay independent. Host bypass shares the Bypass crossfade but
is not stored.

The state payload is 24 bytes: `TEND`, little-endian version 1, three IEEE
float32 values, two boolean flags and reserved zeros. Loading validates length,
version, ranges, finite values and flags before changing any parameter.
