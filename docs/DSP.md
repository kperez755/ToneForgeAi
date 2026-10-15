# ToneForge AI — DSP

## What exists today

Eleven fully implemented, unit-tested modules in `core/include/tfcore/dsp/modules/`,
plus three shared DSP utilities: `Biquad.h` (standard RBJ "Audio EQ Cookbook"
coefficients, used by `Amp`/`Cab`/`Eq`/`Distortion`), `ReverbNetwork.h`
(comb/allpass/delay-line building blocks, used by `Reverb` and `Delay`), and
`ModulationNetwork.h` (fractional-delay and first-order-allpass building blocks,
used by `Modulation`):

- **NoiseGate** (`noise_gate.v1`) — one-pole envelope follower per channel,
  threshold/attack/hold/release. Straightforward, standard-topology gate;
  the main design decision was making the envelope follower and gain
  smoothing entirely branch-and-allocation-free so it's safe to call from
  `process()`.
- **Drive** (`drive.v1`, displayed as **"Overdrive"** in the pedalboard's type
  dropdown — moduleId unchanged, only the label, to match a transparent/dynamic
  drive pedal like a Blues Driver) — pre-gain, asymmetric `tanh`-based soft
  clipper, one-pole tone-tilt filter, makeup level. This is the module most
  likely to be revisited: the current clipper is cheap and alias-prone at high
  gain (no oversampling yet).
- **Distortion** (`distortion.v1`) — the high-gain counterpart to Overdrive
  (the "Friedman BE-OD" pedalboard slot): a symmetric clip that engages much
  earlier (threshold 0.35 vs Drive's asymmetric 0.6/0.8) cascaded twice for
  extra harmonic density, plus a `presence` control (post-clip `Biquad`
  high-shelf, 0-9 dB @ 3 kHz) for the fizz/brightness a single tone-tilt
  control doesn't capture. Still a single stompbox stage — `Amp` remains the
  module for "the whole amp."
- **Compressor** (`compressor.v1`) — standard feedforward dynamics compressor:
  peak envelope follower, dB-domain hard-knee gain computer, attack/release-
  smoothed gain reduction (smoothing the *gain reduction amount*, not the raw
  envelope — the standard way to avoid a click at the threshold crossing),
  makeup gain. No vari-mu/opto/FET circuit modeling (that's real analog
  circuit-modeling research) — same honesty boundary as Amp/Cab.
- **Delay** (`delay.v1`) — feedback delay built on `SimpleDelayLine`
  (`ReverbNetwork.h`, originally built for Reverb's predelay). A one-pole
  lowpass lives *only* in the feedback path, so the first echo is
  full-bandwidth and only the regenerating repeats get progressively
  darker — the classic tape-delay character, not just quieter repeats.
- **Amp** (`amp.v1`) — two cascaded symmetric soft-clip stages (rough
  approximation of multiple preamp tube stages) followed by a
  bass/mid/treble tone stack and a post-tone-stack presence shelf, all via
  `Biquad`. Explicitly not modeling any specific real amplifier - see the
  module's own header comment. This is the module most likely to need real
  iteration to sound genuinely competitive with commercial amp sims; that
  is real DSP research, not a template fill.
- **Cab** (`cab.v1`) — **not** impulse-response convolution. There is no
  licensed/recorded IR bundled, and a real convolution engine (partitioned/
  block convolution, needed to keep latency and CPU bounded regardless of
  IR length) doesn't exist yet - both are real future work. What exists
  instead is a fixed 3-filter chain (high-pass ~90 Hz, presence peak
  ~2.8 kHz, low-pass ~5.5 kHz) shaped like a typical closed-back 4x12's
  frequency response, with a `mix` parameter. This approximates "a cab" in
  the sense of removing raw amp harshness - it does not sound like, and
  should never be marketed as, any specific real cabinet.
- **Eq** (`eq.v1`) — general 3-band low-shelf/mid-peak/high-shelf trim,
  placed after Amp+Cab in the default chain.
- **Reverb** (`reverb.v1`) — algorithmic (Schroeder/Moorer topology: 8
  parallel feedback comb filters into 4 series allpass diffusers per
  channel, the "Freeverb" family), not convolution against a real
  impulse response - same honesty boundary as Cab. A `character`
  parameter (Hall/Plate/Spring/Room) selects between four hand-tuned
  presets that vary comb/allpass delay-length tables, feedback ceiling,
  and damping response; Spring additionally modulates one comb's delay
  length with a slow LFO for a "flutter"/boing character. `size`,
  `damping`, `predelay_ms`, and `mix` round out the parameter set. Added
  in response to a user request for a pedalboard-selectable reverb
  ("simple reverb like spring, hall, etc") - see the pedalboard milestone
  log entry in `docs/ROADMAP.md`. Building blocks (`CombFilter`,
  `AllpassFilter`, `SimpleDelayLine`) live in
  `core/include/tfcore/dsp/ReverbNetwork.h`, reused by `Delay`'s feedback
  line.
- **Modulation** (`modulation.v1`) — one "multi-modulation" pedal covering
  four related effects (the "Strymon Mobius" pedalboard slot) rather than
  four separate modules: a `type` parameter (Chorus/Flanger/Phaser/Tremolo,
  rendered as a dropdown via the same discrete-choice mechanism Reverb's
  `character` uses) selects which algorithm a shared LFO drives. Chorus and
  Flanger use `ModulationNetwork.h`'s `InterpolatingDelayLine` (linearly
  interpolated fractional-sample delay, needed because their delay time is
  what's being modulated continuously — `SimpleDelayLine`'s integer-only
  delay would "step" audibly); Phaser cascades 4 `FirstOrderAllpass` stages
  with a shared LFO-swept coefficient, summed with dry (the interference
  between dry and phase-shifted is what produces the moving notches, not
  the allpass alone); Tremolo is plain amplitude modulation. `rate_hz` and
  `depth` are shared across all four types rather than per-type-idiomatic
  ranges — one control surface covering all four, a documented
  simplification, not a missing feature. This module's LFO phase advances
  every *sample*, not once per block like the rest of this codebase's
  coefficient-recompute convention — the one place that convention doesn't
  hold, because modulation needs to sound continuous, not stair-stepped;
  see `ModulationNetwork.h`'s header comment.

All share `AudioModuleBase` for bypass handling and default
`getState()`/`setState()`, and all are covered by unit tests exercising:
normal operation, bypass, parameter boundary values, state round-trips, and
(for the filter-based modules) frequency-response sanity checks done via
steady-state sine measurement rather than a full FFT (see
`core/tests/test_biquad.cpp` for the technique) — `core/tests/test_noise_gate.cpp`,
`test_drive.cpp`, `test_amp.cpp`, `test_cab.cpp`, `test_eq.cpp`,
`test_reverb.cpp`, `test_compressor.cpp`, `test_distortion.cpp`,
`test_delay.cpp`, `test_modulation.cpp`. Reverb's tests additionally verify
the character presets are actually acoustically differentiated (Hall
sustains measurably more late-tail energy than Room at an identical `size`
setting) rather than being cosmetic labels on identical math, and — like
Delay's and Modulation's Flanger/Phaser tests — stress-test feedback loops
at their least-damped/highest-feedback configuration to confirm output
stays finite and bounded (a real risk in any feedback network — caught and
fixed a genuine bug during Reverb's development: `SimpleDelayLine`
mishandled `delay == 0`, trapping a test impulse in an unwritten buffer
slot for one full buffer length instead of passing it straight through).

The default chain wired into `plugin/Source/PluginProcessor.cpp` is
`NoiseGate -> Drive -> Amp -> Cab -> Eq`, matching this order for a reason:
Amp's tone stack and Cab's frequency shaping are what turn a raw distorted
signal into something that reads as "an amp tone" rather than "boosted
clean" - this was the direct fix for the "no tones" issue observed during
the first real hardware smoke test (see docs/ROADMAP.md M3 milestone log).

## What's still not implemented

Just **Limiter** at this point. The user's pedalboard "main pedals" list
(docs/ROADMAP.md) deliberately excluded a Tuner (not a signal-chain effect —
it detects pitch, it doesn't process/shape tone, so it doesn't fit the
`IAudioModule` "effect in the chain" contract; a tuner would be a separate
UI feature - a pitch-detection display - not a pedalboard slot module) and
scoped everything else down to one solid pedal per role rather than every
possible variation. The `ai-backend`'s preset mapping
(`ai-backend/app/core/mapping.py`) already emits a `limiter.v1` parameter
block so the AI generation path and preset schema won't need to change
shape when it lands — only an entry in `ModuleRegistry` (`types()` + the
`create()` switch, in `plugin/Source/state/ModuleRegistry.cpp`) is needed,
same pattern every other module already follows.

## Testing philosophy

Every module must be testable via `core/tests/*.cpp` alone — construct it,
`prepare()`, feed it buffers, assert on output — with no JUCE, no plugin
host, and no audio device involved. This is what let the M1 modules be
verified (including under AddressSanitizer + UndefinedBehaviorSanitizer,
see the M1 milestone log in ROADMAP.md) entirely inside a text-only Linux
sandbox with no GUI or DAW available. Every new module should ship with
tests in the same style: normal operation, bypass, boundary parameter
values, and state round-trip, at minimum.
