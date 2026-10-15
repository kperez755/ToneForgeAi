# ToneForge AI — Architecture

Status: MVP foundation (Milestone 1). This document is the source of truth for how the
system is decomposed and why. Update it whenever a subsystem's responsibilities change.

## 1. Guiding constraints

The audio callback is sacred. It must never allocate, lock, touch the filesystem, touch
the network, call into Python, parse JSON, or call an LLM. Every architectural decision
below exists to protect that constraint while still letting the rest of the app be
dynamic, AI-driven, and user-editable.

The second constraint is that the DSP must be testable and buildable without a DAW, a
GUI, or JUCE. If DSP correctness can only be verified by ear inside a plugin host, it
isn't tested. So the DSP is built as a standalone C++20 static library
(`ToneForgeCore`) with zero dependency on JUCE, and the JUCE plugin is a thin wrapper
around it.

## 2. Top-level decomposition

```
ToneForgeAI/
  core/         ToneForgeCore — pure C++20 DSP + signal chain + preset model. No JUCE.
  plugin/       JUCE VST3 / Standalone wrapper. Depends on core. UI lives here.
  ai-backend/   Python FastAPI service. Turns language into PresetDTO JSON.
  presets/      Factory preset JSON + the JSON schema they validate against.
  docs/         This file, plus focused docs per subsystem.
  scripts/      Build / dev-loop helpers.
```

Four independently buildable, independently testable units. `core` has no knowledge
`plugin` exists. `plugin` has no knowledge `ai-backend` exists beyond "there is an HTTP
service at a configurable URL that returns preset JSON." `ai-backend` has no knowledge
of JUCE, VST3, or real-time audio at all. This is deliberate: it means a DSP engineer
can work in `core/` all day without JUCE installed, and an ML engineer can iterate on
`ai-backend/` without touching C++ or owning a DAW license.

## 3. Core layer (`core/`) — DSP and signal chain

### 3.1 `IAudioModule`

Every effect (noise gate, compressor, drive, amp, cab IR, EQ, chorus, phaser, flanger,
delay, reverb, limiter) implements one interface:

```cpp
class IAudioModule {
public:
    virtual ~IAudioModule() = default;

    // Called on the message/background thread before playback starts or on
    // sample-rate/block-size change. May allocate.
    virtual void prepare(const ProcessSpec& spec) = 0;

    // Called on the audio thread, every block. Must be RT-safe: no allocation,
    // no locks, no syscalls.
    virtual void process(AudioBuffer<float>& buffer) noexcept = 0;

    // Clears internal state (filter memory, delay lines) without reallocating.
    virtual void reset() noexcept = 0;

    virtual bool isBypassed() const noexcept = 0;
    virtual void setBypassed(bool shouldBypass) noexcept = 0;

    // Fixed-cardinality parameter set, safe to read from the audio thread.
    virtual ParameterStore& parameters() noexcept = 0;

    // Off-thread only. Used for preset save/restore.
    virtual ModuleState getState() const = 0;
    virtual void setState(const ModuleState& state) = 0;

    virtual std::string_view moduleId() const noexcept = 0; // stable id, e.g. "drive.v1"
};
```

Rationale: `process()` is `noexcept` and takes no owning references — it cannot
allocate or throw without violating the contract, which makes RT-safety reviewable at
the type level, not just by convention. `getState`/`setState` are the only
serialization-aware methods, and they are explicitly documented as off-thread, so
nobody is tempted to serialize inside `process()`.

Every module is independently unit-testable: construct it, `prepare()` it with a fake
`ProcessSpec`, feed it a buffer, assert on the output. No plugin host required.

### 3.2 Parameters: `ParameterStore`

Each module owns a fixed set of parameters, created once in its constructor (no
runtime map mutation — that would make the "audio thread reads a map" pattern
thread-unsafe). Each parameter is a `std::atomic<float>` holding a normalized 0..1 or
denormalized engineering value, plus static metadata (range, curve, default, units,
display name) used only by the UI:

```cpp
class ParameterStore {
public:
    Parameter& add(std::string id, ParameterRange range, float defaultValue);
    Parameter* find(std::string_view id) noexcept;
    void forEach(const std::function<void(Parameter&)>&) const;
private:
    std::vector<std::unique_ptr<Parameter>> parameters_; // fixed after prepare()
};
```

`Parameter::get()`/`set()` are lock-free atomic loads/stores. The UI thread writes,
the audio thread reads — single-writer-many-reader per parameter, which is the one
pattern `std::atomic<float>` gives you for free without a mutex.

**Known risk, documented rather than hidden:** simple per-parameter atomics are fine
for continuous knobs (gain, mix, frequency) but wrong for changes that must apply
atomically across many parameters at once — e.g. switching an amp model or loading a
new cab IR, where a torn read (half old state, half new) would glitch or crash. For
those, the module holds a `std::shared_ptr<const Resource>` swapped via
`std::atomic_exchange` from a background thread and `std::atomic_load` on the audio
thread (RCU-style). This is called out explicitly in `docs/DSP.md` because
`std::atomic<std::shared_ptr<T>>` is lock-free only on some standard library
implementations — this needs a measured check on the target platform (libstdc++ vs.
libc++) before it ships, and a lock-free custom double-buffer is the fallback if it
isn't.

### 3.3 `SignalChain`

Owns an ordered `std::vector<std::shared_ptr<IAudioModule>>` and calls `process()` on
each in order on the audio thread. `shared_ptr`, not `unique_ptr`: the plugin layer's
pedalboard slot system (§4.1) needs a module instance to outlive any single
`ChainState` — a slot's module keeps its internal state (filter memory, envelope
followers) alive across chain rebuilds that only change *other* slots, while also
being handed to a fresh `ChainState` every time the active module set changes.
Reordering, inserting, or removing modules (e.g. user drags a pedal to a new slot, or
an AI-generated preset changes which modules are even present) never happens in place
on the audio-thread vector. Instead:

1. Background/UI thread builds a **new** `ChainState` (new vector, new module
   instances, fully prepared).
2. A single `std::atomic<ChainState*>` (or `shared_ptr`, per the same caveat as above)
   is swapped.
3. Audio thread does one atomic load per block to get the chain it processes this
   block; the old `ChainState` is destroyed later on the background thread once no
   longer referenced.

This gives us glitch-free structural changes (new amp, new full preset, AI-generated
chain) without ever taking a lock on the audio thread.

### 3.4 Presets

```cpp
struct ModuleSnapshot { std::string moduleId; bool bypassed; std::map<std::string,float> params; std::optional<std::string> resourceRef; };
struct Preset {
    std::string id, name, description, author;
    int schemaVersion;
    std::vector<std::string> tags;
    std::chrono::system_clock::time_point createdAt;
    std::vector<ModuleSnapshot> chain;
    std::optional<AiProvenance> aiProvenance; // prompt, provider, "inspired by X" note
};
```

`PresetSerializer` converts `Preset <-> JSON` and `Preset -> ChainState` (building
actual module instances). Serialization is schema-versioned from day one
(`schemaVersion`) because presets will outlive this MVP and factory/user presets must
remain loadable across releases — a versioned migration function is cheaper to write
now than a "sorry, your presets broke" support thread later.

### 3.5 Analysis interfaces (mocked for MVP)

```cpp
class IAudioAnalyzer {
public:
    virtual ~IAudioAnalyzer() = default;
    virtual SpectralProfile analyzeSpectrum(const AudioBuffer<float>&) = 0;
    virtual DynamicsProfile analyzeDynamics(const AudioBuffer<float>&) = 0;
    virtual TransientProfile analyzeTransients(const AudioBuffer<float>&) = 0;
    virtual HarmonicProfile analyzeHarmonics(const AudioBuffer<float>&) = 0;
};
```

`MockAudioAnalyzer` returns plausible fixed/heuristic values so the rest of the app
(UI, preset generation from "analyze this recording") can be built and tested against
a stable contract today. The real implementation (FFT-based spectral analysis,
envelope-follower dynamics, onset detection) is a Milestone 3+ item and swaps in
behind the same interface — nothing above this layer needs to change.

## 4. Plugin layer (`plugin/`) — JUCE wrapper

`plugin/` depends on `core` and JUCE. Responsibilities:

- `PluginProcessor`: owns a `SignalChain`, implements `processBlock()` as "sync slot
  parameters into the currently-live modules, load the current `ChainState` pointer,
  call `process()` on each module" — nothing else. Implements
  `getStateInformation`/`setStateInformation` by delegating to APVTS's own
  serialization (full JSON `PresetSerializer` round-trip is still M4).
- `SlotManager` (`state/ParameterBridge.h/.cpp` — see §4.1): maps the pedalboard's
  fixed slot parameters to `core::ParameterStore` entries of whichever module
  currently occupies each slot, and keeps the two in sync. This is the one place
  JUCE's parameter model and our RT-safe atomic model meet.
- UI (`Source/PluginEditor.h/.cpp`): a title bar, the 8 `SlotPanel`s (§4.1), and an
  `AiPanel` docked in a fixed strip along the bottom of the editor. Undo/redo and a
  dedicated preset-browser panel are still open M4 items (not built yet).
- `AiClient` (`Source/ai/AiClient.h/.cpp`, described fully in §4.2): the actual class
  that talks to `ai-backend` — this superseded an earlier conceptual design in this
  document that called it `AiWorker`; the implemented shape differs in a few ways
  described below, so `AiWorker` should be read as historical/aspirational, not what
  ships.

### 4.2 AI panel wiring (`AiPanel` + `AiClient`)

`AiPanel` (`Source/AiPanel.h/.cpp`) is the UI: a single-line prompt box, four buttons
(Generate Tone / Modify Tone / Explain Tone / Reference Song), a status line, and a
read-only output area. It's owned directly by `ToneForgeAudioProcessorEditor` (a plain
member, not a `unique_ptr` — nothing about it needs to outlive the editor). Reference
Song reuses the same prompt box as a song/artist query and hits `POST /song-reference`
via `AiClient::songReference()` — same `GenerateResult` shape as Generate/Modify, since
the backend response is a `PresetDTO` either way, so it needed no new plumbing beyond
one more method + button.

`AiClient` (`Source/ai/AiClient.h/.cpp`) is the HTTP client, and it is **not** owned by
the editor — it's a long-lived member of `ToneForgeAudioProcessor` itself
(`getAiClient()`), because a plugin's processor instance outlives any number of editor
open/close cycles a host may do, and an in-flight request shouldn't be tied to whether
a window happens to be open. Design points that differ from the earlier `AiWorker`
sketch:

- No persistent queue/worker thread. Each `generate()`/`modify()`/`explain()`/
  `checkHealth()` call spins up its own short-lived `juce::Thread` subclass
  (`RequestThread`, internal to `AiClient.cpp`) that runs one blocking HTTP POST via
  `juce::URL` and then terminates. `AiClient` keeps these in a
  `std::vector<std::unique_ptr<juce::Thread>>`, pruning finished ones opportunistically
  on the next call rather than on a timer. This trades a small amount of thread-spawn
  overhead (irrelevant next to LLM response latency) for not needing a queue,
  worker-loop shutdown protocol, or task-cancellation logic at all.
- Every request's JSON body is built with the *same* hand-rolled `tfcore::json::Value`
  library and `tfcore::PresetSerializer` the rest of the codebase already uses — no
  second JSON library, no separate `PresetDTO` C++ type. The plugin sends/receives
  `tfcore::Preset` directly; only the Python side has a Pydantic `PresetDTO` mirror
  (§5), and it's built to serialize field-for-field compatibly (camelCase aliases:
  `moduleId`, `matchType`, `schemaVersion`, `resourceRef`, `aiProvenance`).
- Every completion callback finishes with `juce::MessageManager::callAsync`, so results
  always land back on the message thread regardless of which background thread ran the
  HTTP call — `AiPanel` never has to reason about which thread invoked its callback.
- `AiPanel` guards every callback with a `juce::Component::SafePointer<AiPanel>`
  captured at request-start and checked for null before touching any member state —
  the request may still be in flight when the editor window (and therefore `AiPanel`)
  is destroyed, and the callback must degrade to a silent no-op rather than touch freed
  memory. `AiClient` itself has no such problem: it's owned by the processor, which
  outlives any editor.
- "Modify Tone" and "Explain Tone" both operate on `PluginProcessor::
  captureCurrentPreset()` — a fresh read of whatever's actually dialed into the 8
  pedalboard slots right now (not a saved/named preset) — so "make it darker" acts on
  the tone as currently set, matching what the user hears.
- A successful Generate/Modify result is applied via the *existing*
  `PluginProcessor::applyPreset()` path (§4.1's last bullet) — there is no separate
  "construct modules from an AI response" code path to keep in sync.
- `AiPanel` always surfaces `preset.aiProvenance.disclaimer` in its output area on a
  successful Generate/Modify — never silently carries it in data only. See §5.2's
  "Inspired by — approximation" requirement; this is where that requirement is actually
  enforced on the UI side.
- A 15-second periodic `checkHealth()` keeps the status line honest about whether
  `ai-backend` is even reachable, so "the button doesn't do anything" reads as
  "AI backend offline" rather than as a silent failure.

### 4.1 Pedalboard slots

The plugin exposes a flexible "any pedal, any slot" pedalboard, not a fixed chain —
but `juce::AudioProcessorValueTreeState` needs its full parameter layout fixed at
*construction* time, before any module has even been chosen. Real multi-effects
plugins reconcile this with a fixed-slot pattern, and that's what ToneForge AI does:

- `kNumSlots` (8) fixed slots, processed left-to-right in slot order. "Any pedal in
  any position" today means choosing module types in the slot order you want them
  processed in — drag-to-reorder is a deliberately deferred UI polish item (the
  underlying data model already supports it; only the gesture doesn't exist yet).
- Each slot exposes a **fixed** set of always-present APVTS parameters: a
  `slot{N}_type` choice (`ModuleRegistry::types()` — None, Noise Gate, Drive, Amp,
  Cab, Eq, ...), a `slot{N}_bypass` bool, and `kKnobsPerSlot` (8) generic
  `slot{N}_knob{K}` floats normalized to `[0,1]`.
- The knobs are host-facing *normalized* floats, not the module's real engineering
  units. When slot 3 holds Amp, knob 0 might mean "gain"; switch that slot to Eq and
  the same host parameter (same automation lane, same identity) instead means
  "low_gain_db", denormalized into Eq's real `[-12,12]` range via
  `tfcore::ParameterRange::denormalize()`. The UI relabels the knob to match; the host
  never needs to know the meaning changed. `kKnobsPerSlot` is sized with headroom
  over today's largest module (Amp, 5 parameters) so new module types can land
  without another layout break.
- Some parameters are really a small discrete choice wearing a continuous knob's
  clothes - Reverb's `character` (Hall/Plate/Spring/Room) is the first example.
  `tfcore::Parameter` carries an optional `valueLabels` list for exactly this (see
  `core/include/tfcore/dsp/ParameterStore.h`); when a knob's currently-bound
  parameter has one, `SlotPanel` (`plugin/Source/PluginEditor.cpp`) swaps in a
  labeled `juce::ComboBox` instead of a rotary slider. Both widgets bind to the
  *same* underlying `[0,1]` float parameter via JUCE's generic attachment
  machinery (a combo box's item index N maps to normalized position
  `N/(numItems-1)`, same mechanism `AudioParameterChoice` uses) - no APVTS layout
  change needed to support this, and only one widget's attachment is ever live at
  a time so there's no risk of two listeners fighting over one parameter.
- **Changing a slot's type at runtime without breaking real-time safety** is the
  interesting part: `AudioProcessorValueTreeState::Listener::parameterChanged()` can
  fire on the *audio* thread (hosts may deliver automation for a choice parameter
  from inside `processBlock()`), but constructing a new module and rebinding its
  parameters allocates. So the listener callback does the bare minimum — sets a
  lock-free `atomic<bool>` dirty flag — and a `juce::Timer` polling at 20 Hz does the
  actual rebuild on the message thread, then republishes the result to the audio
  thread via the same atomic-shared_ptr-swap pattern `SignalChain` already uses
  (§3.3), publishing both a new `ChainState` (what the audio thread processes) and a
  parallel, equally-immutable "slot bindings" snapshot (what `syncToCore()` reads to
  know which JUCE knob maps to which live module's `tfcore::Parameter*`) together.
  Neither structure is ever mutated after publishing — a slot-type change always
  builds an entirely new snapshot rather than editing the live one in place, which is
  what makes it safe for the audio thread to be mid-read on the old one while the
  message thread builds the new one concurrently. Full reasoning lives in the header
  comment of `plugin/Source/state/ParameterBridge.h` (filename predates the
  redesign — see that file's top comment for why it wasn't renamed).
- This same mechanism is what `PluginProcessor::applyPreset()` uses to load a preset:
  it drives the slot APVTS parameters exactly like the UI does, rather than
  constructing modules directly and hoping some separate binding stays in sync — that
  separate path is what caused an earlier documented dangling-pointer bug (see the M3
  milestone log in `docs/ROADMAP.md`), and routing everything through one mechanism
  is what closes it for good rather than just patching that one call site.

## 5. AI backend (`ai-backend/`) — Python / FastAPI

The plugin never embeds a language model and never calls Python in-process. It talks
to a local HTTP service over `localhost` (default `http://127.0.0.1:8765`). This keeps
the audio process small, keeps the AI stack swappable/upgradable independent of the
plugin binary, and means the AI backend can be tested with `pytest` and `curl` with no
audio host involved at all.

### 5.1 Provider abstraction

```python
class AIProvider(Protocol):
    async def generate(self, prompt: str) -> PresetDTO: ...
    async def modify(self, preset: PresetDTO, instruction: str) -> PresetDTO: ...
    async def explain(self, preset: PresetDTO) -> str: ...
```

- `MockProvider`: deterministic keyword/heuristic mapping from prompt to
  `PresetDTO`. No network, no model, always available. This is the default provider
  in tests and CI, and the fallback when Ollama is unreachable, so the app is never
  in a state where "AI panel" means "nothing happens."
- `OllamaProvider`: calls a local Ollama instance (`http://localhost:11434`) with a
  system prompt constraining output to a small JSON tone-descriptor shape (not the full
  `PresetDTO` — the model picks tone *qualities*, `app/core/mapping.py`'s
  `build_default_chain()` is still the one place that knows the actual signal-chain
  topology and real per-module parameter ranges), validated and retried with a
  corrective prompt on failure (bounded retry count, then falls back to `MockProvider`
  output so the user still gets *something* rather than an error toast). Requests a mix
  of continuous 0–1 amounts (gain, brightness, compression, ...) and short categorical
  strings (`reverb_character: "hall"|"plate"|"spring"|"room"`, `modulation_type`,
  `distortion_style`) — strings rather than raw integer indices, since a local model is
  far more reliably able to emit `"spring"` than to remember that spring is index 2 of
  4 (see docs/ROADMAP.md's M6 log). `modify()` uses a second, different prompt: given
  the preset's currently-active modules and real parameter values plus the user's
  instruction, the model returns a JSON patch touching only the modules/parameters that
  should change (an unrecognized module id or parameter name in the patch is dropped,
  never applied) — a real diff, not "regenerate everything and hope nothing else
  drifted."
- Future: `CloudProvider` implementations (Anthropic/OpenAI-backed) behind the same
  `Protocol`, selected by config — no call site changes required.

### 5.2 Song reference system

`song_reference/db.py` is a small curated dataset (7 entries as of the M6 pass, each with
real sourced values — see the M6 log in `docs/ROADMAP.md` for how the Mac DeMarco entry
was researched, not guessed): song, artist, tags, and a full `ToneDescriptor` including
the categorical fields (`reverb_character`/`modulation_type`/`distortion_style`), not
just the continuous amounts. `lookup.py` does fuzzy title/artist matching (rapidfuzz)
against that dataset first; on a miss it calls `AIProvider.infer_song_tone()` — a method
distinct from `generate()`, with its own system prompt (`OllamaProvider`'s
`_SONG_INFERENCE_SYSTEM_PROMPT`) that explicitly instructs the model to reason from
genre/era/production conventions and avoid inventing specific-sounding details it isn't
actually confident about, rather than reusing `generate()`'s prompt. This does not make
an obscure song's result accurate — there is no retrieval/grounding, it is still working
purely from the model's training-time knowledge — it only changes what "the model doesn't
really know" looks like, from a confidently generic result to an honest genre-appropriate
one. A real fix for genuinely obscure songs would need to retrieve real information at
request time (web search before the Ollama call), which is a real, larger feature this
project does not build yet (needs a search-API decision — see `docs/ROADMAP.md`'s M7
entry). Every result the system returns is tagged:

```json
{ "match_type": "curated" | "inferred", "confidence": 0.0-1.0, "disclaimer": "Inspired by — approximation, not an exact recreation." }
```

The API and UI copy never say "recreation" or "exact clone" — this is enforced by
having `disclaimer` be a required, non-empty field on the response schema, not just a
UI convention.

### 5.3 Endpoints

`POST /generate` (prompt -> PresetDTO), `POST /modify` (preset + instruction ->
PresetDTO), `POST /explain` (preset -> text), `POST /song-reference` (song/artist ->
PresetDTO + provenance), `GET /health` (provider status, model availability). All
request/response bodies are Pydantic models shared between routes and providers, so a
malformed AI response is caught at the schema boundary, not deep in application logic.

### 5.4 Module parameter fidelity (`app/core/mapping.py`)

`mapping.py` is where a heuristic/mock provider's internal tone knobs get turned into
actual `moduleId` + parameter-name/value pairs on a `PresetDTO`. This is the one place
in the backend that has to stay in lockstep with `core`'s real per-module parameter
sets (`core/include/tfcore/dsp/modules/*.h`) — a typo or stale field name here produces
a `PresetDTO` that looks fine in isolation but silently fails to apply a parameter once
it reaches `PluginProcessor::applyPreset()` (which looks up each parameter by exact
name and simply skips ones it doesn't recognize — see §4.1). This had never been
checked against the real module parameter sets end-to-end before the AI panel was
wired up; doing so for the first time (running `ai-backend` for real, parsing its
`/generate` response through the actual `tfcore::PresetSerializer`/`ParameterStore`,
and diffing parameter names module-by-module) found three real mismatches, since fixed:
`modulation.v1` was missing `type`/`feedback`, `delay.v1` was missing `tone`, and
`reverb.v1` emitted a nonexistent `decay_s` field while missing `character`/`damping`/
`predelay_ms`. Covered going forward by the same verification approach, not just the
existing `pytest` suite (which only checks the Pydantic schema shape, not agreement
with `core`'s actual parameter names).

**M6 follow-up: chain length vs. slot budget.** The same verification approach, run
against a *variety* of prompts instead of just the default one, found a second,
higher-impact class of bug this per-parameter check couldn't see: `build_default_chain()`
used to unconditionally emit 10 modules (including `limiter.v1`, which
`ModuleRegistry::create()` can't even instantiate yet) while the pedalboard only has 8
slots, and `PluginProcessor::applyPreset()` maps `preset.chain[i]` onto slot `i` by raw
position with no bounds-aware merging — so `reverb.v1` and `limiter.v1` (always the last
two entries in the old fixed-order list) were silently dropped from *every*
AI-generated preset, independent of prompt. Every individual parameter name was
correct; the chain was just too long to ever fully reach the plugin. Fixed by making
`build_default_chain()` itself slot-budget-aware: a fixed tonal spine (Noise Gate,
optional Compressor, Overdrive-or-Distortion, Amp, Cab, Eq) plus a scored competition
for whatever slots are left among Modulation/Delay/Reverb — the losing effect(s) are
left off the chain entirely rather than emitted-and-silently-dropped. See
`docs/ROADMAP.md`'s M6 log entry for the full write-up and how it was re-verified (10
varied prompts, a rebuilt C++ harness that now also flags chain length against the
8-slot budget, zero mismatches).

## 6. Thread model

| Thread | Owns | May do | Must never do |
|---|---|---|---|
| Audio thread | `processBlock` | atomic loads of params/ChainState, DSP math | allocate, lock, file/network IO, call Python |
| UI/message thread | JUCE components, `UndoManager` | build new ChainState/Preset objects, write params atomically, schedule background work | block on network, block on file IO |
| Background worker pool | preset load/save, IR loading, (future) audio analysis | file IO, heavy compute | touch audio-thread data structures directly |
| AI request threads | one short-lived `juce::Thread` per `AiClient` call (§4.2's `RequestThread`), pruned opportunistically | network IO (HTTP POST to `ai-backend`), JSON parse/validate | touch audio-thread data structures directly, block the UI thread, outlive their own single request |

Cross-thread handoff is either (a) a single atomic pointer swap (ChainState, as in
§3.3), (b) per-parameter atomics, or (c) `juce::MessageManager::callAsync` posting a
fully-formed, immutable object to the UI thread. No shared mutable state is touched
from two threads without one of those three mechanisms — this is the rule the code
review checklist enforces. See `docs/THREAD_MODEL.md` for the full write-up including
sequence diagrams (to be added as the plugin layer is implemented).

## 7. Why this structure holds up over 5 years

- **`core` has no JUCE dependency**, so DSP work, unit tests, and even a future
  non-JUCE port (AU on macOS via a different wrapper, or a Linux-native build) never
  require touching the DSP code itself.
- **Provider abstraction** means swapping Ollama for a fine-tuned local model, or
  adding a cloud fallback, is a new class, not a rewrite.
- **Schema-versioned presets** mean factory presets shipped in v1.0 still load in
  v3.0.
- **The atomic-swap ChainState pattern** means "AI generates an entirely new 7-module
  chain" and "user drags one slider" go through the same safe mechanism, so there is
  no special-cased unsafe path for the AI feature specifically.
- **Mocked analysis/AI interfaces from day one** mean the UI and preset pipeline can
  be built, tested, and demoed before the real spectral-analysis or fine-tuned-model
  work exists, and swapping the mock for the real thing is additive, not a rewrite.
