# Testing

Results from 2026-10-19 on macOS 27.0.1, Apple M4, Apple Clang 21, Release
C++20 builds. Machine-readable results: [validation.json](../measurements/validation.json),
[adapter-equivalence.json](../measurements/adapter-equivalence.json) and
[benchmark.csv](../measurements/benchmark.csv).

| Check | Result |
| --- | --- |
| DSP/state tests (`ctest`) | Passed |
| AddressSanitizer + UndefinedBehaviorSanitizer | Passed |
| AU `auval` | Passed |
| Official VST3 SDK validator | 47 passed, 0 failed |
| CLAP validator 0.4.1 | 44 run: 36 passed, 0 failed, 8 skipped (unsupported optional features) |
| pluginval 1.0.4, strictness 5, AU and VST3 | Passed, including editor, editor-while-processing, state and automation |
| AU and CLAP versus DSP core, 48 kHz, block 127, 35/50/−3 dB | Sample-identical |
| AU host bypass versus the Bypass parameter | Sample-identical; exact pass-through 1023 frames after the change |
| Release bundles | arm64, macOS 11 deployment target |

Releases target Apple Silicon only. The same checks run in GitHub Actions on every
push (`.github/workflows/ci.yml`); the release workflow repeats the build and DSP
tests before packaging.

## DSP and state tests

`tests/dsp/tests.cpp` covers:

- 44.1–192 kHz plus 8, 22.05 and 384 kHz and invalid-rate fallback;
- bit-identical output for block lengths 1–1031, including parameter changes at
  arbitrary frames;
- mono and stereo, identical channels equal to mono, swapped channels giving
  swapped outputs, no channel crosstalk;
- float/double equivalence, in-place processing and zero-length buffers;
- silence, impulses, full scale, 1e12, tiny and genuinely subnormal input, and
  nonfinite sanitization;
- neutral transparency, trim, bypass continuity, reset and sample-rate changes;
- linear trim ramps in dB settling at exactly 512 samples, bypass exact after
  1024 samples, and values set after reset but before audio applying immediately;
- the display response curve against processed quiet and loud tones (within
  0.01 dB and 0.1 dB), and the reported meters;
- zero heap allocations while processing or automating;
- state round trip and rejection of truncated, unknown-version, nonfinite and
  invalid-flag payloads.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build-sanitize --target topend-dsp-tests
ctest --test-dir build-sanitize --output-on-failure
```

## Plugin validation

Install the bundles first; `auval` and the AU pluginval run use the registered
component. `tools/validate.py` runs every validator without opening a DAW;
pluginval briefly opens test editors.

```sh
python3 tools/fetch_validators.py
cmake -S tools/validators -B build-validator-local -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-validator-local --target validator
python3 tools/install.py --build build-release/out
python3 tools/validate.py --build build-release
.venv/bin/python tools/check_adapters.py
```

`check_adapters.py` renders generated stereo input, with signal from the first
frame, through the DSP core, the CLAP plugin and the AU plugin, and toggles the
AU host bypass property mid-render. The AU harness reserves event ID
4294967295 for that property.

## Real-host checks

An earlier REAPER session (script `tests/integration/reaper_smoke.lua`) passed
discovery, two instances per format, parameter changes, project save/reload and
ten editor open/close cycles for AU, VST3 and CLAP. Audio playback, offline
bounces, transport, recorded automation lanes and the current editor have not
yet been checked in a DAW. Logic, Ableton Live, Bitwig and FL Studio are
unverified.

Suggested acceptance pass in a disposable session: play vocals, drums,
acoustic recordings and mixes at several settings; record automation for every
parameter and compare playback with an offline bounce; save and reload multiple
instances; switch rates and buffer sizes; exercise Link, Details, numeric
entry, the mouse wheel and resizing.

## Performance

`benchmark.csv` holds five-run medians for two seconds of stereo processing at
44.1/48/96 kHz, blocks 32–1024 and 1/8/32 instances, with controls at 50/50/−3
dB on a 0.2-peak sine. Values are a share of one core's real-time budget,
excluding host and editor work. At block 128, one instance uses about 0.22%,
0.23% and 0.46% at the three rates; 32 instances about 7.0%, 7.6% and 14.6%.

```sh
build/topend-benchmark > measurements/local/benchmark.csv
```
