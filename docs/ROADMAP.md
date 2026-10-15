# ToneForge AI — Implementation Roadmap

Approach: design → implement → compile → test → repeat, one milestone at a time.
Nothing below is claimed as done unless it actually compiled and its tests actually
ran (see the milestone log at the bottom for what has been verified so far, and in
which environment).

## Environment reality check

This roadmap is being executed inside a sandboxed Linux container: g++ 11.4 (C++20
capable), CMake 4.4 (installed via pip, not apt — no root in this sandbox),
Python 3.10, network access, but **no JUCE, no VST3 SDK, no audio host, no GUI, no
Windows/macOS toolchain**. That means:

- `core/` (pure C++20 DSP) can be fully built and unit-tested here.
- `ai-backend/` (Python/FastAPI) can be fully built and unit-tested here.
- `plugin/` (JUCE VST3 + Standalone) can be **structured and written** here, but
  cannot be compiled or run here — JUCE (~500MB+ via FetchContent) and a real
  desktop/DAW environment are required. It must be built and verified on your actual
  Windows/macOS machine. `docs/SETUP_WINDOWS.md` covers that build.

This isn't a corner being cut — it's the honest boundary of what this sandbox can
verify, and it's exactly why `core` was designed with zero JUCE dependency: the DSP
correctness doesn't have to wait on a JUCE build to be proven.

## Milestones

**M1 — Foundations (this session).** Repo scaffold, `core` DSP interfaces
(`IAudioModule`, `ParameterStore`, `SignalChain`), two fully-implemented and unit
tested modules (NoiseGate, Drive) proving the interface holds up, preset
serialization skeleton, `ai-backend` skeleton with `MockProvider` + `/generate`
+ `/health` + song reference lookup, pytest suite green. Architecture and roadmap
docs (this file and ARCHITECTURE.md).

**M2 — Full DSP module set.** Implement Compressor, Amp (waveshaper + tone stack,
no neural model yet), CabIR (offline convolution against a small bundled IR set),
EQ (parametric, multi-band), Chorus, Phaser, Flanger, Delay, Reverb (algorithmic,
e.g. Freeverb-style to start), Limiter, Input/Output gain-trim modules. Each ships
with its own unit test file. Risk: getting Amp/Cab to sound competitive is a genuine
DSP research effort, not a checklist item — budget real iteration time here, and
expect the first pass to sound "acceptable," not "Neural DSP quality."

**M3 — JUCE plugin shell.** `PluginProcessor`/`PluginEditor`, `ParameterBridge`
wiring `core::ParameterStore` to `AudioProcessorValueTreeState`, VST3 + Standalone
CMake targets via JUCE FetchContent, signal chain UI (add/remove/reorder modules),
per-module editor panels, meters. This is the first milestone that requires your
local machine to build and run. Risk: JUCE VST3 build size/time, and VST3 SDK
licensing terms (Steinberg VST3 SDK is GPLv3-or-commercial — needs a licensing
decision before any commercial distribution).

**M4 — Preset system end-to-end.** Factory preset pack (curated, hand-tuned, maybe
15-25 presets spanning clean/crunch/lead/ambient archetypes), user preset save/load/
tag/favorite, JSON import/export, preset browser UI, undo/redo across preset
changes.

**M5 — AI panel wired into the plugin.** `AiClient` background HTTP calls from plugin
to local `ai-backend`, "Generate Tone" / "Modify Tone" / "Explain Tone" wired to real
UI actions, graceful degradation when the backend or Ollama isn't running (clear "AI
backend offline" state, never a crash or silent no-op). **Done** - see the milestone
log entry below for what actually shipped and how it was verified.

**M6 — Ollama integration quality pass.** Prompt engineering for reliable
structured JSON output, retry/repair loop, model selection UI (which local Ollama
model to use), latency budgeting and progress indication in the UI (AI calls are
seconds, not milliseconds — the UI must communicate this, not freeze).
**Partially done** — see the milestone log below. Shipped: retry/repair loop
(already existed, extended to a new modify-patch prompt), real per-prompt
reverb-character/modulation-type/distortion-style selection (prompt engineering
that gives the model actual expressive range, not just seven abstract sliders),
a real Ollama-backed "Modify Tone" (previously always silently used the mock
heuristic regardless of whether Ollama was running), and busy/status-line
progress indication in `AiPanel` (from the AI-panel-wiring milestone). Still
open: model selection is a `TONEFORGE_OLLAMA_MODEL` environment variable, not
an in-app UI picker — a real, deliberate scope cut, not an oversight.

**M7 — Song/artist reference system.** Expand the curated song database, tune the
fuzzy-match thresholds, build the inferred-match fallback prompt, add the
"Inspired by" disclaimer UI treatment, add a feedback mechanism (was this a good
match?) to inform future curation. **Started** - see the M6 log below: curated
database grew to 7 entries (real, sourced values, not guessed), `Reference Song`
is wired into the plugin UI, and the inferred-match prompt was given a first
real pass (reasoning-from-genre instruction). Still open and the actual hard
part of this milestone: web-search-grounded inference for songs outside both
the curated database and the model's own training knowledge - the only lever
that actually fixes "obscure" rather than just making the miss more graceful;
needs a search-API decision. Also still open: fuzzy-match threshold tuning,
UI disclaimer treatment beyond the AI panel's output area, and any feedback
mechanism.

**M8 — Reference audio analysis (real implementations).** Replace
`MockAudioAnalyzer` with real spectral/dynamics/transient/harmonic analysis (FFT via
a DSP library, envelope followers, onset detection), wire "Analyze Recording" in the
AI panel to actually use it for tone matching suggestions.

**M9 — Hardening.** Thread-safety stress tests (TSan build), fuzz preset
JSON parsing, preset schema migration tests across versions, CI pipeline (build +
test on push), crash reporting, telemetry opt-in.

**M10+ — Long-term roadmap** (explicitly out of MVP scope, tracked here so nothing
gets forgotten): neural amp capture (NAM-style, ONNX Runtime inference — real-time
budget for neural inference on the audio thread is a hard constraint and likely
needs a dedicated inference thread + lookahead buffering, not naive in-callback
inference), pedal capture, cloud tone sharing + community presets, automatic input
gain / guitar calibration, live tone adaptation, DAW automation improvements, AU,
AAX, Linux plugin builds.

## Cross-cutting risks

**Audio thread safety is the highest-consequence risk.** A single lock or
allocation slipping into `process()` causes glitches/dropouts that are hard to
reproduce and devastating to user trust. Mitigation: the `IAudioModule` interface
makes `process()` `noexcept`, code review checklist explicitly checks for
allocation/locking, and M9 adds ThreadSanitizer + allocation-tracking test builds
that fail CI if the audio thread allocates.

**`std::atomic<std::shared_ptr<T>>` lock-free guarantee is platform-dependent.**
Called out in ARCHITECTURE.md §3.2 — needs `std::atomic<T>::is_always_lock_free`
checked on every target platform before relying on it; fallback is a custom
lock-free double-buffer.

**VST3 SDK licensing.** GPLv3 or a paid Steinberg commercial license — this is a
business decision, not an engineering one, and blocks commercial distribution if
unresolved. Flagging now so it isn't discovered at ship time.

**AI output reliability.** Local LLMs (via Ollama) are not guaranteed to emit valid
structured JSON on the first try. The `MockProvider` fallback and bounded-retry
repair loop (M6) exist specifically so a bad model response degrades gracefully
instead of breaking the "Generate Tone" button.

**Getting the Amp/Cab DSP to sound genuinely good** is a real audio-engineering
research problem, not a template-fill task. The roadmap treats M2 as the milestone
most likely to need extra iteration cycles, and this should be communicated
honestly rather than papered over with a "done" checkbox.

**JUCE + VST3 build cannot be verified in this sandbox.** Every plugin-layer change
from M3 onward needs to actually be built and smoke-tested on a real machine/DAW
before being considered done — "it should compile" is not the same as "it compiled."

## Milestone log (what has actually been verified, and where)

- M1: `core` built and tested with g++ 11.4 / CMake 4.4 in the original Linux
  sandbox (27/27 tests, plus clean under ASan+UBSan). `ai-backend` tested with
  pytest in the same sandbox (25/25). `plugin/` scaffolded but not compiled
  anywhere at that point.
- M1 (Windows follow-up): `core` built and tested for real on Windows with
  MSVC (Visual Studio Build Tools 2026, `cl` 19.5x / toolset 14.51) via the
  CMake Visual Studio generator - 27/27 tests passing. This caught a real
  portability bug the Linux sandbox couldn't see: `PresetSerializer.cpp` used
  `gmtime_r`, `strptime`, and `timegm`, which are POSIX/glibc extensions with
  no MSVC equivalent (`strptime` doesn't exist on MSVC at all). Fixed with
  `gmtime_s`/`gmtime_r` and `_mkgmtime`/`timegm` behind `_WIN32` guards, and
  `sscanf`-based parsing instead of `strptime`. `plugin/` still not built -
  next up.
- M3 (first build): `plugin/` compiled successfully on Windows for the first
  time - JUCE 7.0.12 fetched via CMake FetchContent, built against MSVC
  Build Tools 2026 (`cl` 19.51.36252.0, Windows SDK 10.0.26100.0), producing
  both `ToneForge AI.vst3` and the `ToneForge AI.exe` Standalone target with
  zero build errors on the first attempt. The Standalone target
  (`ToneForge AI.exe`) was launched and confirmed to open its window and
  run (JUCE's audio device init + `prepareToPlay`/`processBlock` all live)
  - first real runtime proof, not just a successful compile. Not yet tested
  as a VST3 inside an actual DAW. The UI is still just the placeholder
  editor from the M1 scaffold (dark background, status label) - no real
  signal-chain/module-editor/preset-browser/AI-panel UI yet, that work is
  still ahead in M3.
- M2 (started): user plugged a real guitar into the Standalone build after
  the M3 first-run and correctly diagnosed "audio passes through but there
  is no tone" as a real gap, not a bug report. Root cause: the default
  chain was only NoiseGate->Drive, with no Amp/Cab stage to give the signal
  amp-like character, and no UI/ParameterBridge to adjust anything anyway.
  Response: implemented `Biquad` (RBJ cookbook filter utility) plus three
  new modules - `Amp` (amp.v1), `Cab` (cab.v1, EQ-approximation not true IR
  convolution - see docs/DSP.md), `Eq` (eq.v1) - each unit tested in the
  Linux sandbox first (47/47 tests total, clean under ASan+UBSan) before
  touching Windows again, exactly like the M1 modules. Wired into the
  default chain (`NoiseGate -> Drive -> Amp -> Cab -> Eq`) and into
  `PluginProcessor::createModule()`. Not yet rebuilt/re-tested on Windows
  at the time of this log entry - that's the immediate next step.
  `ParameterBridge`/real UI controls still don't exist, so the new tonal
  character comes from each module's constructor defaults, not anything
  adjustable yet - flagged as the next real gap once Amp/Cab are confirmed
  to sound right on hardware.
- M2 (Windows verified): the Amp/Cab/Eq/Biquad work above rebuilt and
  passed 47/47 tests on Windows/MSVC, matching the Linux sandbox exactly -
  no portability surprises this time.
- M3 (real controls): `ParameterBridge` implemented (bridges
  `tfcore::ParameterStore` to `juce::AudioProcessorValueTreeState`, RT-safe
  once-per-block sync in `processBlock()`) and `PluginEditor` rewritten
  from a placeholder label into five titled columns (one per module) with
  a rotary slider per parameter and a bypass toggle, built dynamically from
  `ParameterBridge::describeModules()`. `getStateInformation`/
  `setStateInformation` now do real ValueTree-based session save/recall
  (not yet the full JSON Preset system - still M4). First build attempt hit
  two real MSVC errors, both fixed and logged as concrete lessons: (1)
  `copyXmlToBinary`/`getXmlFromBinary` are not free functions in this JUCE
  version's `juce` namespace - switched to `ValueTree::writeToStream`/
  `readFromData`, a more standard pattern anyway; (2) a classic C++ "most
  vexing parse" in `ParameterBridge::sanitizeId` (`juce::String
  result(std::string(x));` parsed as a function declaration, not a variable
  definition) - fixed with assignment-style initialization. A subsequent
  `LNK1163` linker error turned out to be stale incremental-build state
  (common after several source-file changes in one session), resolved by
  clearing `build/plugin` and rebuilding clean, not a code issue. User
  confirmed on real hardware: all sliders/bypass toggles work and audibly
  change the tone live while playing. `applyPreset()` still has the
  documented dangling-pointer bug (see its comment in PluginProcessor.cpp)
  and must not be wired up before that's fixed.
- Known outstanding gaps going into M4/M5 (superseded by the pedalboard
  entry below - kept here for history): no real signal-chain/preset-
  browser/AI-panel UI yet; no Preset JSON save/load wired into the
  plugin; `applyPreset()`'s ParameterBridge rebinding bug; `ai-backend`
  has not yet been run end-to-end against the plugin.
- M3 (pedalboard redesign): user correctly diagnosed the five-column
  fixed-chain UI as "very rudimentary controls for an amp" and asked how
  to add pedals - explicitly chose the flexible pedalboard redesign over
  the smaller "add more fixed-position pedal types" option. Replaced the
  fixed `NoiseGate -> Drive -> Amp -> Cab -> Eq` chain with 8 slots, each
  independently choosing its module type (`ModuleRegistry`, new), with a
  fixed pool of 8 generic relabelable knobs per slot bound via runtime-
  swappable `tfcore::ParameterRange::normalize()/denormalize()` (new -
  core addition, 4 new unit tests, verified 51/51 clean under g++ 11.4
  and ASan+UBSan in the sandbox before touching Windows). Required a real
  core ownership change: `tfcore::ChainState` now holds
  `shared_ptr<IAudioModule>` instead of `unique_ptr<IAudioModule>` so a
  slot's module instance can outlive any single `ChainState` snapshot -
  existing `SignalChain` tests updated (`make_unique` -> `make_shared`)
  and re-verified, no behavior change to anything audio-thread-facing.
  `ParameterBridge.h/.cpp` rewritten in place into `SlotManager` (kept
  the filename - see that file's header comment for why, given files in
  the synced project folder can't be renamed from here) implementing a
  real RT-safety mechanism, not just a bigger version of the old one:
  JUCE can deliver a slot's type-parameter-changed callback on the AUDIO
  thread (host automation), so the callback only sets a lock-free dirty
  flag; a 20 Hz message-thread timer does the actual module construction/
  rebinding and publishes an atomically-swapped snapshot pair (a
  `ChainState` for audio processing, a parallel "slot bindings" vector for
  parameter syncing) - full design writeup in
  `docs/ARCHITECTURE.md` §4.1 and the sequence diagram in
  `docs/THREAD_MODEL.md`. `PluginEditor` rewritten from 5 fixed columns to
  8 slot panels (type dropdown, bypass, 8 knobs that relabel/hide via a
  separate 10 Hz UI-only polling timer watching for type changes).
  `applyPreset()` rewritten to drive slot APVTS parameters through this
  same mechanism instead of constructing modules directly - this closes
  the previously-documented dangling-pointer bug rather than carrying it
  forward into the new system. **Not yet built or run on Windows** - the
  `core` change (shared_ptr) is sandbox-verified; every JUCE-layer file
  (`SlotManager`, `ModuleRegistry`, `PluginProcessor`, `PluginEditor`) is
  written to the same integration pattern already proven twice this
  project, but cannot be compiled in this sandbox (no JUCE) - the next
  real step is a Windows rebuild and hardware test, same as every prior
  plugin-layer milestone.
- M2 (Reverb): user tried the pedalboard, confirmed slots work, and asked
  for "a simple reverb like spring, hall, etc" as a real example pedal.
  Implemented `Reverb` (`reverb.v1`) - classic Schroeder/Moorer comb+
  allpass topology (8 combs + 4 allpasses per channel), same honesty
  boundary as Cab (algorithmic, not IR convolution). Added a `character`
  parameter (Hall/Plate/Spring/Room) selecting between four hand-tuned
  delay-length/feedback/damping presets, with Spring additionally
  LFO-modulating one comb's delay for a "flutter" character - `size`,
  `damping`, `predelay_ms`, `mix` round out 5 parameters, well inside the
  8-knob-per-slot budget. New reusable `CombFilter`/`AllpassFilter`/
  `SimpleDelayLine` primitives in `core/include/tfcore/dsp/
  ReverbNetwork.h`. 6 new tests (`test_reverb.cpp`) - tail-energy-after-
  impulse, mix=0 passthrough, bypass, Hall-vs-Room character
  differentiation, a feedback-stability stress test (max size, zero
  damping, continuous broadband-ish input, asserts every output sample
  stays finite and bounded), and state round-trip. First test run caught
  a real bug, not a tuning issue: `SimpleDelayLine` mishandled
  `delay == 0` (predelay off) by reading a not-yet-written buffer slot,
  trapping the test impulse for one full buffer-length (~4400 samples)
  instead of passing it straight through - fixed by special-casing
  `delaySamples_ <= 0` to true zero-latency pass-through. 57/57 tests
  passing after the fix, clean under ASan+UBSan, all verified in the
  Linux sandbox before touching Windows. Wired into `ModuleRegistry`
  (`types()` + `create()`) so it's immediately selectable in any
  pedalboard slot - no plugin-layer changes needed beyond that one
  registration, which is exactly the payoff the `ModuleRegistry`
  indirection (added during the pedalboard redesign) was for. Not yet
  built/run on Windows - same boundary as every plugin-facing change.
- M3 (discrete-choice knobs): user tried Reverb and pointed out there was
  no button/switch to pick Hall/Plate/Spring/Room - just a knob stepping
  through quarters, exactly the polish gap flagged (but left unfixed) in
  the previous log entry. Closed it properly rather than bolting on a
  Reverb-specific UI hack: `tfcore::Parameter` gained an optional
  `valueLabels` list (`core/include/tfcore/dsp/ParameterStore.h`) - when
  present, it names each consecutive integer value in the parameter's
  range, and the constructor throws if the label count doesn't match the
  range's span (a real programmer-error guard, 3 new unit tests, 60/60
  passing, clean under ASan+UBSan). Reverb's `character` parameter now
  declares `{"Hall","Plate","Spring","Room"}`. `SlotPanel`
  (`plugin/Source/PluginEditor.cpp`) checks
  `tfcore::Parameter::isDiscreteChoice()` when relabeling a slot's knobs
  and swaps in a labeled `juce::ComboBox` in place of the rotary slider
  for any knob whose currently-bound parameter is discrete - both widgets
  bind to the exact same generic `[0,1]` knob parameter via JUCE's
  attachment machinery, so this needed no APVTS layout change at all, and
  works for any future module's discrete parameters for free, not just
  Reverb's. Not yet built/run on Windows - same boundary as every
  plugin-facing change.
- M2 (main pedal set): user provided a curated real-pedalboard reference
  list (Tuner, Compressor, Overdrive, Distortion, Multi-Modulation,
  Delay & Reverb) and asked for "the main ones," explicitly agreeing a
  Tuner isn't needed. Scoped it to 4 new modules (Reverb already existed):
  `Compressor` (compressor.v1 - feedforward, threshold/ratio/attack/
  release/makeup, gain-reduction-amount smoothed rather than the raw
  envelope, same idea NoiseGate's gain smoothing already uses),
  `Distortion` (distortion.v1 - high-gain counterpart to Drive, a
  symmetric earlier-clipping curve cascaded twice plus a presence shelf,
  differentiated from Drive by clip character and range, not just a
  bigger gain knob), `Delay` (delay.v1 - feedback delay reusing
  `SimpleDelayLine` from Reverb's ReverbNetwork.h, feedback-path-only
  lowpass for authentic progressively-darker tape-delay repeats), and a
  single multi-mode `Modulation` (modulation.v1 - Chorus/Flanger/Phaser/
  Tremolo behind one `type` dropdown, covering the "Strymon Mobius" role
  without four separate modules - reused the discrete-choice-dropdown UI
  mechanism built for Reverb's `character` for exactly this). Modulation
  needed one real new primitive set (`ModulationNetwork.h`:
  `InterpolatingDelayLine` for fractional-sample delay, `FirstOrderAllpass`
  for the phaser cascade) and one documented departure from the
  established "recompute coefficients once per block" convention - its
  LFO advances every sample, since modulation has to sound continuous,
  not stair-stepped. 24 new tests across `test_compressor.cpp`,
  `test_distortion.cpp`, `test_delay.cpp`, `test_modulation.cpp` (84/84
  total core tests passing, clean under ASan+UBSan, verified in the
  Linux sandbox before touching Windows, same discipline as every prior
  module). Drive's pedalboard display label changed from "Drive" to
  "Overdrive" (moduleId unchanged) now that Distortion exists alongside
  it as a genuinely different pedal. All four wired into `ModuleRegistry`
  - zero other plugin-layer changes needed, confirming the `ModuleRegistry`
  indirection built during the pedalboard redesign is paying off exactly
  as intended. Not yet built/run on Windows - same boundary as every
  plugin-facing change.
- Known outstanding gaps going into M4/M5: no drag-to-reorder for
  pedalboard slots (processing order is fixed to slot order - see
  ARCHITECTURE.md §4.1); no real preset-browser/AI-panel UI yet; no
  Preset JSON save/load wired into the plugin; `ai-backend` has not yet
  been run end-to-end against the plugin; Limiter is the one module from
  the original DSP.md list still not implemented (deliberately deferred,
  not forgotten); Modulation's `rate_hz`/`depth` controls are shared
  across all four of its types rather than per-type-idiomatic ranges, a
  documented simplification.
- M5 (AI panel wired up): user asked for the AI integration this milestone
  had flagged as "never yet run end-to-end." Before writing any plugin
  code, actually ran `ai-backend` for real in the Linux sandbox (`uvicorn
  app.main:app --host 127.0.0.1 --port 8765`, mock provider), called
  `/generate` via `curl`, and wrote a standalone C++ verification program
  linked against the real compiled `tfcore` sources to parse the response
  through the actual `PresetSerializer::fromJson()` and diff every
  parameter name against each module's real `ParameterStore` - this is a
  genuinely different (and stronger) check than the existing `pytest`
  suite, which only validates the Pydantic schema shape, not agreement
  with `core`'s real parameter names. It found three real mismatches in
  `app/core/mapping.py`: `modulation.v1` was missing `type`/`feedback`,
  `delay.v1` was missing `tone`, and `reverb.v1` emitted a nonexistent
  `decay_s` field while missing `character`/`damping`/`predelay_ms` - all
  fixed, re-verified (pytest 25/25 still passing, live curl output
  correct, and the verification harness reporting zero `[MISMATCH]`
  lines against every implemented module). See ARCHITECTURE.md §5.4 for
  the full writeup. Then built the plugin-side integration: `AiClient`
  (`plugin/Source/ai/AiClient.h/.cpp`) - a `PluginProcessor`-owned HTTP
  client using one short-lived `juce::Thread` per request rather than a
  persistent worker/queue, reusing `tfcore::json::Value`/
  `PresetSerializer` directly instead of a second JSON layer -
  `PluginProcessor::captureCurrentPreset()` (the inverse of
  `applyPreset()`, snapshotting the live pedalboard state for Modify/
  Explain), and `AiPanel` (`plugin/Source/AiPanel.h/.cpp`) - prompt box,
  Generate/Modify/Explain buttons, status line, output display, every
  async callback guarded with `juce::Component::SafePointer<AiPanel>`
  against the editor closing mid-request, a periodic 15s health check so
  "AI backend offline" is a visible state rather than a silent no-op, and
  the AI-provenance disclaimer always surfaced on a successful Generate/
  Modify rather than only carried in data. Wired `AiPanel` into
  `PluginEditor` as a fixed bottom strip below the 8 pedalboard slots
  (editor grew from 1360x520 to 1360x700 to fit it) and added the two new
  source files to `plugin/CMakeLists.txt`. Full design writeup in
  ARCHITECTURE.md §4.2, sequence diagram in THREAD_MODEL.md. Not yet
  built/run on Windows - same boundary as every plugin-facing change;
  requires `ai-backend` to actually be running for the panel to do
  anything besides show "AI backend offline."
- M6 (Ollama quality pass): user asked to work through M6, specifically
  concerned that the AI couldn't produce a genuinely good tone yet. Before
  writing any prompt-engineering code, re-ran the M5 verification approach
  (real `ai-backend` + a standalone C++ harness parsing responses through
  the actual `tfcore::PresetSerializer`/`ParameterStore`) against a battery
  of 10 varied prompts instead of just the default profile, and it found
  the real root cause: `build_default_chain()` unconditionally emitted 10
  modules (including `limiter.v1`, which `ModuleRegistry` doesn't even
  implement) while the pedalboard only has 8 slots, and
  `PluginProcessor::applyPreset()` maps `preset.chain[i]` onto slot `i` by
  raw position - so `reverb.v1` and `limiter.v1` (always the last two
  entries) were silently dropped from **every single AI-generated preset**,
  regardless of how much reverb the prompt asked for. This had never been
  caught because the M5 verification only checked parameter *names* against
  one default-profile response, never chain *length* against the plugin's
  actual slot budget, and neither side's own test suite could see the
  other's constraint.

  Fixed with a real redesign, not a truncation patch:
  `app/core/mapping.py::build_default_chain()` now builds a fixed 5-6-slot
  tonal spine (Noise Gate, optional Compressor, Overdrive-or-Distortion,
  Amp, Cab, Eq) and lets Modulation/Delay/Reverb compete for whatever slots
  are left, keeping the highest-scoring ones (by how strongly the prompt
  asked for each) and dropping the rest - the same trade-off a real 8-slot
  hardware pedalboard has, not a workaround. `limiter.v1` is no longer
  emitted at all (it always resolved to an empty slot anyway).

  Separately, and directly toward "produce a genuinely good tone": the AI
  mapping previously had no way to pick *which* Reverb character
  (Hall/Plate/Spring/Room), *which* Modulation type
  (Chorus/Flanger/Phaser/Tremolo), or Overdrive-vs-Distortion - every prompt
  got the same module with only its continuous knobs tuned, so "spring
  reverb" and "hall reverb" (or "tremolo" and "phaser") produced an
  identical signal chain. `ToneDescriptor` gained three categorical fields
  (`reverb_character`, `modulation_type`, `distortion_style`), driven by new
  keyword rules in `tone_descriptor.py` (MockProvider) and a matching
  addition to `OllamaProvider`'s system prompt (requesting them as short
  strings like `"spring"` rather than raw integers - far more reliable for
  a local model to produce correctly than remembering index 2 means Spring).
  A "heavy distorted metal tone" now gets `distortion.v1` (symmetric,
  earlier-clipping, cascaded twice) instead of `drive.v1` with a bigger gain
  number - a real DSP-level difference, not a bigger knob.

  `OllamaProvider.modify()` used to unconditionally delegate to
  `MockProvider().modify()` regardless of whether Ollama was even reachable
  - "Modify Tone" never actually benefited from a real local model, even
  when one was running correctly. Replaced with a real diff-based modify:
  the model is sent only the currently-active modules and their real
  current parameter values, and returns a JSON patch (module id -> changed
  params, plus an optional `bypassed` toggle) that's applied with
  parameter-id validation (an unrecognized parameter name in the patch is
  dropped, never written) and a `MockProvider` fallback if the patch is
  malformed, empty, or Ollama is unreachable. Deliberately scoped to
  "adjust what's already on the board," not "add a module that isn't
  there" - inserting a new module mid-modify would mean re-deciding its
  place within the 8-slot budget, which is what Generate Tone is for.

  Verification: added `tests/test_ollama_provider.py` (11 new tests) -
  `OllamaProvider` had no dedicated test coverage at all before this pass,
  despite owning the retry loop, JSON-extraction, and (new) patch-validation
  logic; every test monkeypatches `_chat` except one, which relies on
  nothing actually listening on the configured port (true in this sandbox
  and CI) to exercise the real "offline degrades gracefully" path with no
  mocking. Extended `tests/test_mock_provider.py` with 4 new tests
  (drive-vs-distortion selection, reverb-character/modulation-type
  selection, the 8-slot budget never being exceeded across varied prompts,
  and reverb specifically no longer being starved out) and updated one
  existing test whose premise the redesign intentionally changed (it
  asserted a heavy/metal prompt used `drive.v1`, which is now
  `distortion.v1` by design). 40/40 backend tests passing. Rebuilt the C++
  verification harness (added `distortion.v1` to its known-module list, and
  a new `[MISMATCH]`-worthy check for chain length against the pedalboard's
  8-slot budget) and re-ran it against 10 varied real `/generate` responses
  (ambient/shoegaze, heavy metal, clean jazz, funk, spring reverb, tremolo,
  bluesy overdrive, phaser+cathedral, neo-soul, hall reverb) - every one
  parsed cleanly, every chain fit within 8 slots (two profiles hit exactly
  8), zero `[MISMATCH]` lines, and spot-checks confirmed the categorical
  fields land correctly (e.g. "surfy spring reverb tone" -> `character =
  2.0`, "slow pulsing tremolo tone" -> `type = 3.0`). Also spot-verified
  that a genuinely oversubscribed prompt ("phaser lead with long delay and
  cathedral reverb" - all three of modulation/delay/reverb wanted, only 2
  back-of-chain slots free) correctly dropped the lowest-scoring effect
  (modulation, 0.3) and kept the two the prompt leaned on harder (reverb
  0.5, delay 0.35) - the budget/priority logic working as designed, not
  just fitting by luck.

  Not yet possible to verify: an actual Ollama instance with a real model
  pulled - this sandbox has no Ollama installed and no path to install one,
  same boundary as every JUCE-layer change. All of the above was verified
  against the mock provider (which now exercises the identical
  `build_default_chain()`/categorical-field path `OllamaProvider.generate()`
  merges its JSON onto) and against `OllamaProvider`'s own logic via
  monkeypatched `_chat` calls - real end-to-end Ollama testing (does a real
  local model reliably emit the requested JSON shape, is the modify-patch
  prompt followed well in practice) is the next real verification step, and
  needs to happen on a machine with Ollama actually running.
- Known outstanding gaps going into M7+: model-selection UI (still an env
  var); Modulation's `rate_hz`/`depth` shared-across-types simplification
  (noted in the M2 log entry) still stands; `OllamaProvider.modify()`'s
  "can't add a module that isn't already on the board" scope limit is
  real and not yet solved (surfaced to the user via a description note
  rather than hidden); the six curated `song_reference/db.py` entries don't
  yet set the three new categorical descriptor fields (they get sensible
  defaults, but hand-tuning e.g. a spring-reverb character for a
  surf-adjacent entry is a real, cheap follow-up not done here);
  `is_available()`/`/health` still can't distinguish "Ollama running, no
  model pulled yet" from "Ollama running, model pulled" - both currently
  read as available until a real `/generate` call is attempted.
- M6 (real end-to-end verification + song-reference fix): user has Ollama
  running in a Docker container with `gemma3:4b` pulled - the first time
  any of M5/M6's work was verified against a real model rather than the
  mock provider or monkeypatched tests. Set `app/config.py`'s and
  `OllamaProvider`'s default model to `gemma3:4b` to match, and expanded
  `docs/SETUP_WINDOWS.md` with a Docker-specific Ollama section. Live
  results confirmed the whole M6 rework working end to end: `/generate`
  for "warm bluesy overdrive with a bit of spring reverb" correctly
  produced all 8 spine+back-of-chain modules including `reverb.v1` with
  `character: 2` (Spring) and a modest, non-maxed mix - not just parameter
  names matching, the actual categorical reasoning landing correctly from
  a real 4B local model. `/modify` with "make it darker and add more
  delay" came back `provider: ollama:gemma3:4b` (not a mock fallback),
  confirming the patch-based modify prompt parses and applies correctly in
  practice, though the response didn't populate the `unmet` field for the
  unfulfillable "add more delay" part (delay.v1 wasn't already on that
  board) - a real small-model reliability gap in following the secondary
  "explain what you couldn't do" instruction, not a code defect.

  Testing against a real song surfaced a second real gap:
  `/song-reference`'s "inferred" path (used for any song not in the small
  curated list) was hardcoded to always call the *mock* provider for its
  AI lookup, regardless of `TONEFORGE_PRIMARY_PROVIDER` - meaning asking
  about an obscure song never actually consulted Ollama's knowledge of it
  at all. Fixed to use the same primary-with-mock-fallback pattern
  `/generate` and `/modify` already use (`app/api/routes_song_reference.py`,
  `app/song_reference/lookup.py`'s `lookup_song()` parameter renamed
  `fallback_provider` -> `provider` for clarity since it's no longer
  specifically "the fallback"). That alone wasn't sufficient, though: the
  inferred path's `_apply_generated_hints()` only ever extracted a single
  gain number from the provider's generated preset and discarded
  everything else (brightness, compression, reverb character, modulation
  type, distortion style), falling back to a keyword match on the song
  title/artist string for every other field - which usually matches
  nothing, since song titles rarely contain tone-descriptor words. Fixed
  properly, not patched: added `mapping.descriptor_from_preset()`, a real
  inverse of `build_default_chain()` that reconstructs a full
  `ToneDescriptor` from every module actually present in a generated
  preset (module absence read back as "not wanted," matching the M6
  slot-budget semantics). `lookup_song()`'s inferred branch now uses this
  instead of the narrow gain-only hint. New `tests/test_mapping.py` (7
  tests - `mapping.py` had no direct test coverage at all before this,
  despite being arguably the most important file in the backend) checks
  the round trip recovers gain/brightness/distortion-style/reverb-
  character/modulation-type within a small tolerance, correctly zeroes out
  effects absent from the chain, and doesn't error on a hand-built preset
  missing a gain-stage module entirely. 47/47 backend tests passing.
  Re-ran the C++ verification harness against a live `/song-reference`
  response too - chain fit within budget, zero `[MISMATCH]` lines.
  Live-verified on the user's machine: the same "Sweet Child O' Mine"
  query, re-run after this fix, came back with a materially different
  (and more plausible) result - moderate `reverb.v1` mix (0.128, not
  washy) and `drive.v1` (not `distortion.v1`), consistent with a
  hot-rodded-Marshall overdrive tone rather than generic defaults with one
  number changed.
- M6 (Reference Song UI + a real "obscure song" gap found and partially
  addressed): user asked to actually test "Cliffs of Dover" and other
  songs through the plugin itself, not just curl. Surfaced that the AI
  panel had no way to reach `/song-reference` at all - only
  Generate/Modify/Explain existed. Added `AiClient::songReference()`
  (identical shape to `generate()`/`modify()`, since the backend response
  is a `PresetDTO` either way) and a fourth "Reference Song" button in
  `AiPanel` reusing the same prompt box as a song/artist query. Two real
  MSVC build errors caught on the first Windows attempt with these plus
  the earlier AiClient/AiPanel work, both fixed: `AiClient.h` only
  included `<juce_core/juce_core.h>`, but `juce::MessageManager::
  callAsync` (used in every async completion path) lives in the
  `juce_events` module - `juce_core.h` doesn't declare it (the module was
  already being linked transitively via `juce_audio_utils`'s own
  dependencies, so this was a missing #include, not a missing link
  dependency - MSVC C3083/C2039/C3861 on `MessageManager::callAsync`);
  `AiPanel.cpp` called `.isEmpty()` on `result.preset.description`, which
  is a plain `std::string` (`tfcore` has zero JUCE dependency by design) -
  should have been `.empty()`. Separately, `ctest --test-dir build`
  reported "No tests were found!!!" on a clean Windows build despite
  `tfcore_tests.exe` building fine - reproduced in the Linux sandbox and
  root-caused: `enable_testing()` was never called anywhere in the
  project, so CMake never generated the top-level `CTestTestfile.cmake`
  relay into `core/`'s `add_test()` registration (`ctest --test-dir
  build/core` would have worked; `ctest --test-dir build` - what
  `docs/SETUP_WINDOWS.md` actually recommends - could not). Fixed by
  adding `enable_testing()` to the top-level `CMakeLists.txt`, verified
  by reproducing and fixing the exact failure in the sandbox before
  touching Windows, then confirmed fixed on the user's machine too.

  Testing "Cliffs of Dover" for real (through the rebuilt plugin) got an
  honest reaction: "sounds not bad, but definitely a rough
  approximation" - expected, and traced to real documented DSP scope
  limits (`Cab` is an EQ-shaped approximation, not real IR convolution;
  `Amp` is a simple waveshaper + tone stack, not a modeled/profiled amp -
  both flagged in `docs/DSP.md`/`docs/ROADMAP.md` as needing real
  audio-engineering iteration, not a quick fix). Testing a song NOT in
  the curated database ("Freaking Out the Neighborhood" by Mac DeMarco)
  surfaced something more fixable: the inferred result had no
  `modulation.v1` at all despite chorus being THE defining element of
  DeMarco's actual tone. Researched his real gear via web search (Boss
  CE-3/CE-5 chorus, clean small tube amps, EHX Holy Grail reverb, slapback
  delay - sources in the chat transcript) and added a seventh curated
  entry to `song_reference/db.py` with sourced values - the first curated
  entry to actually set `modulation_amount`/`modulation_type` (every prior
  entry left them at ToneDescriptor's defaults). New regression test
  (`test_mac_demarco_curated_entry_is_chorus_forward_and_clean`) checks
  low gain, high modulation amount, and Chorus type specifically. Live
  round-trip through `/song-reference` and the C++ verification harness
  confirmed `matchType: song_curated`, `confidence: 1.0`, and
  `modulation.v1` present with `type: 0` (Chorus) and a real (not
  near-zero) mix.

  This led to the deeper question: what about songs that are ALSO outside
  the model's own training knowledge, not just outside the curated
  database? Laid out three honest options rather than picking one
  unilaterally: (1) web-search-grounded inference (retrieve real
  information at request time instead of relying on frozen training-time
  memory - the only lever that actually breaks the "obscure = bad"
  ceiling, but needs the user to choose/obtain a search API and accepts
  real added latency - not built); (2) a bigger Ollama model (marginal
  help, same fundamental ceiling - a bigger model just knows *more*
  things, doesn't fix zero-signal cases); (3) fixing the failure mode
  itself so "I don't know" degrades to a plausible genre-appropriate
  guess instead of a confidently generic one - cheap, no new
  infrastructure, implemented now. Added `AIProvider.infer_song_tone()` as
  a method distinct from `generate()` (both providers implement it -
  `MockProvider`'s is a thin wrapper around `generate()`, since the
  deterministic keyword mapper has no "knowledge" to reason honestly
  about either way). `OllamaProvider.infer_song_tone()` uses a new,
  separate `_SONG_INFERENCE_SYSTEM_PROMPT` that explicitly instructs the
  model to reason from genre/era/production conventions and avoid
  inventing specific-sounding details it isn't actually confident about,
  rather than reusing `generate()`'s prompt (refactored
  `_request_descriptor_json()` to take a `system_prompt` parameter so both
  prompts share the same retry/JSON-extraction logic).
  `song_reference/lookup.py`'s inferred branch now calls
  `provider.infer_song_tone(query)` instead of `provider.generate(f"tone
  reminiscent of the song {{query}}")`. This does NOT make an obscure
  song's result accurate - there is still no retrieval/grounding, it's
  purely a prompting change - it only changes what "the model doesn't
  really know" looks like. 6 new tests (`test_infer_song_tone_*` in
  `tests/test_ollama_provider.py`, one in `test_mock_provider.py`) - one
  specifically asserts `infer_song_tone()`'s system prompt differs from
  `generate()`'s and contains the "do not invent" instruction while
  `generate()`'s doesn't. 52/52 backend tests passing. Re-verified via the
  live server + C++ harness (mock provider, since option (1)/a bigger
  model are both still open - Ollama's actual behavior under the new
  prompt has not yet been tested on the user's machine).
- M6/M7 (decision: fine-tune a local model instead of web-search/Spotify
  grounding): after testing "Freaking Out the Neighborhood" live
  (`matchType: song_inferred`, `confidence: 0.35`, no `modulation.v1` in
  the result) user confirmed the general problem isn't any one song, it's
  that obscure/less-mainstream material outside both the curated database
  and the model's own training knowledge gets a "confidently generic"
  guess. Web-search grounding (Tavily/Brave/SerpAPI) and the Spotify Web
  API were both researched and presented with real trade-offs, then
  explicitly rejected by the user as impractical - and, on reflection,
  both add an external cloud dependency that cuts against the project's
  actual goal, restated directly by the user: a personal, local-first tool
  they can eventually open-source, not a product competing with
  cloud-backed platforms like Positive Grid's BIAS X. User proposed
  fine-tuning a small model specifically on guitar-tone JSON generation
  instead - confirmed feasible on their hardware (RTX 3070, 8GB VRAM -
  comfortable headroom for QLoRA on a 3-4B model like `gemma3:4b`/
  `llama3.2:3b` via Unsloth, ~1-2 hours training time, both native-Windows
  and WSL2 install paths now supported). Given an explicit choice via
  AskUserQuestion, user chose to keep improving AI/tone quality along this
  path over open-source prep or a DSP fidelity pass.

  First concrete step, buildable and verifiable without a GPU:
  `ai-backend/scripts/generate_training_data.py`, a synthetic training
  data generator producing chat-format JSONL
  (`ai-backend/training_data/tone_training_data.jsonl`) in the exact
  system/user/assistant shape `OllamaProvider` uses at real inference time,
  so the eventual fine-tune learns to answer the actual production
  contract rather than some other schema needing a second translation
  layer. Two example families: (1) 600 "generate"-style examples - natural
  language tone descriptions built from varied sentence templates wrapped
  around `tone_descriptor.py`'s own `_KEYWORD_RULES` vocabulary, with
  ground-truth JSON computed via the same deterministic
  `descriptor_from_text()` every provider is already measured against, so
  every target is internally consistent with its prompt; (2) 42 song
  inference examples (7 curated songs x 6 query phrasings each) using
  `_SONG_INFERENCE_SYSTEM_PROMPT`, with ground truth taken directly from
  the hand-curated, ear-informed `SongReference.descriptor` values in
  `song_reference/db.py` - not the keyword baseline - since the entire
  point is baking that curated knowledge (and the reasoning pattern behind
  it) into the model's own weights so similar-but-unseen songs benefit
  too. Fixed one real gap while building this: curated `SongReference`
  entries don't populate `descriptor.summary_tags` (that field is only
  filled in by keyword matching), which would have trained the model to
  answer every song query with the useless placeholder `["general"]` -
  the generator now passes `SongReference.tags` through instead. 642 total
  examples, verified structurally (every line valid JSON, exactly 3
  messages in system/user/assistant order, assistant content itself valid
  JSON matching the full `_SYSTEM_PROMPT` schema, zero duplicate
  system-family/user pairs) and spot-checked by hand (e.g. "Cliffs of
  Dover by Eric Johnson" -> the real curated descriptor with tags
  `["lead", "bright", "compressed", "instrumental"]`, not a keyword-baseline
  guess). Pure Python, no GPU/network required, so this ran and was
  verified fully in the sandbox; the actual QLoRA fine-tuning script and
  Windows/Unsloth setup instructions that consume this file are the next
  step, to be run on the user's own RTX 3070 and reported back.
- M6/M7 (Unsloth QLoRA fine-tuning script + native-Windows setup guide):
  user confirmed native Windows (not WSL) as the install path. Researched
  Unsloth's current (as of this writing) native-Windows support directly
  from their docs - a real Conda-based method now exists with no WSL
  requirement. Wrote `ai-backend/scripts/finetune_tone_model.py`, which
  trains `unsloth/gemma-3-4b-it-unsloth-bnb-4bit` (Unsloth's dynamic-4bit
  quant of the same gemma3:4b family `app/config.py` already defaults to)
  via QLoRA (r=16, effective batch size 8, 3 epochs) on
  `tone_training_data.jsonl`, then exports straight to GGUF (Q4_K_M) with
  an auto-generated Ollama `Modelfile`. Two decisions worth recording: (1)
  the dataset's system/user/assistant rows are flattened into a 2-turn
  user/assistant conversation before training, because Gemma 3's own chat
  template has a known gap in dedicated system-role handling - folding the
  system prompt into the user turn (same pattern most Gemma-based apps
  use) sidesteps that ambiguity entirely rather than risk it silently
  going wrong; (2) `train_on_responses_only` masks the loss to the
  assistant's JSON response only, so the model isn't wasting capacity
  learning to predict the (long, mostly fixed) system+user boilerplate.
  The script ends with a quick in-process inference smoke test (2 real
  prompts, printed before export) so there's an immediate sanity check
  before the slower GGUF export step runs. Companion doc
  `docs/FINE_TUNING_WINDOWS.md` walks through Miniconda setup, matching
  the PyTorch CUDA build to the actual driver via `nvidia-smi`, installing
  Unsloth, running the script, `ollama create`-ing the export, and
  re-running the same live comparisons already done against base
  gemma3:4b (Sweet Child O' Mine, Mac DeMarco, Cliffs of Dover) to check
  whether the fine-tune measurably helps - plus a troubleshooting section
  sourced from Unsloth's own docs (CUDA/torch mismatch, forced
  reinstalls, xformers/bitsandbytes checks, OOM mitigation via batch
  size/grad accum). None of this - the training run itself, the GGUF
  export, or the Ollama registration - can be executed or verified in the
  sandbox this project is developed in (no GPU); the script was only
  syntax-checked (`py_compile`) here. This is a genuine trial run on the
  user's own machine - training loss and the smoke-test output should be
  watched closely and reported back before treating the exported model as
  ready for real use.
- M6/M7 (decision: fine-tuning shelved): after the Windows/Unsloth setup
  steps (Miniconda + Conda env + matching PyTorch to CUDA), user decided
  against continuing - the setup/training time investment wasn't worth it
  for an uncertain payoff. `scripts/finetune_tone_model.py` and
  `docs/FINE_TUNING_WINDOWS.md` are left in the repo (untouched, fully
  written) in case this is revisited later, but are not part of the active
  path. Reverting to the pre-fine-tuning state: base `gemma3:4b` via
  Ollama, `infer_song_tone()`'s honesty-oriented prompt for the inferred
  path, and the curated `song_reference/db.py` table as the main lever for
  fixing any specific song that comes back noticeably wrong - a 5-minute,
  no-training, guaranteed-correct edit, same as the Mac DeMarco entry
  added earlier this milestone.
- M6/M7 (few-shot prompting): given an explicit choice via AskUserQuestion
  between few-shot prompting, expanding the curated song database, a
  DSP fidelity pass on Amp/Cab, and self-consistency sampling, user picked
  few-shot prompting - free, no training, no new infrastructure. Added a
  4-example block to `_SYSTEM_PROMPT` (clean jazz, heavy distorted metal
  w/ tight gate, ambient shoegaze wall-of-sound, warm bluesy overdrive w/
  spring reverb - chosen to span the parameter space rather than cluster
  around one style) and a 2-example block to
  `_SONG_INFERENCE_SYSTEM_PROMPT` (Cliffs of Dover, Freaking Out the
  Neighborhood) in `app/providers/ollama_provider.py`. Every example's
  target JSON was generated by actually running the prompt through
  `descriptor_from_text()` (or reading the curated descriptor for the two
  song examples) in the sandbox, not hand-typed, so every example is
  guaranteed internally consistent - the same discipline
  `scripts/generate_training_data.py` used for the shelved fine-tuning
  dataset. New regression test
  (`test_system_prompt_includes_few_shot_examples_derived_from_descriptor_from_text`)
  asserts the embedded example values still match `descriptor_from_text()`'s
  real output, so if the keyword-mapping table changes later, a now-stale
  example won't go unnoticed. 53/53 backend tests passing. Not yet
  verified against the user's real Ollama instance - the next real test is
  re-running the same live comparisons (Sweet Child O' Mine, Mac DeMarco,
  Cliffs of Dover) to see whether the few-shot examples measurably help
  versus the zero-shot prompt they replace.
