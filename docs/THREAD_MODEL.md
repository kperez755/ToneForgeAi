# ToneForge AI — Thread Model

See ARCHITECTURE.md section 6 for the summary table. This document goes one
level deeper into the sequences that matter most.

## Threads in the system

1. **Audio thread** — owned by the host (DAW) or, in Standalone mode, by
   JUCE's audio device callback. Calls `PluginProcessor::processBlock()`.
2. **UI/message thread** — owned by JUCE (`juce::MessageManager`). All
   `juce::Component` code, `AudioProcessorValueTreeState` listener
   callbacks, and anything triggered directly by user interaction runs here.
3. **Background worker pool** — a small `juce::ThreadPool` (or
   `std::jthread`s) used for preset load/save, IR file loading, and (from M8)
   audio analysis.
4. **AI request threads** — as implemented, not one dedicated persistent thread but
   one short-lived `juce::Thread` per request (`RequestThread`, internal to
   `plugin/Source/ai/AiClient.cpp`), spun up for the duration of a single HTTP call
   and then finished; `AiClient` (owned by `PluginProcessor`, not the editor) holds
   them in a `vector<unique_ptr<juce::Thread>>` and prunes finished ones
   opportunistically on the next call. See ARCHITECTURE.md §4.2 for why this shape was
   chosen over a persistent worker/queue.

No other threads touch shared state in this system. If a future feature
needs a new thread, it must be added to this table and its cross-thread
handoff mechanism documented before merging.

## Sequence: user drags a knob

1. UI thread: `juce::Slider` listener fires.
2. UI thread: `ParameterBridge` writes the new value into the corresponding
   `tfcore::Parameter` via `Parameter::set()` — a single relaxed atomic
   store.
3. Audio thread: next `processBlock()` call, the module's `process()` calls
   `Parameter::get()` — a single relaxed atomic load — and uses the new
   value immediately (parameters are read once per block per module in the
   MVP; per-sample smoothing for zipper-noise-free automation is an M2/M3
   item, tracked as technical debt below).

No lock, no allocation, on either side.

## Sequence: user loads a preset (or AI generates one)

1. UI thread (or AI worker thread, handing off via `callAsync` — see next
   section): has a `tfcore::Preset` in hand.
2. Background worker thread: for each `ModuleState` in `preset.chain`,
   construct the corresponding concrete `IAudioModule` (a small factory
   keyed on `moduleId`, e.g. `"drive.v1"` -> `new Drive()`), call
   `prepare()` on it with the current `ProcessSpec`, then `setState()` to
   apply the saved parameters. This builds a brand-new `ChainState`.
3. Background worker thread: calls `SignalChain::setChain(newChainState)` —
   one atomic pointer store (release semantics).
4. Audio thread: the very next `processBlock()` call does one atomic load
   (acquire semantics) of the current `ChainState` and processes it. The
   old `ChainState` is not explicitly freed here — it stays alive via
   `shared_ptr` refcounting until the audio thread's last reference to it is
   dropped, which happens naturally once it loads the new pointer on a
   subsequent block. No block is ever processed with a half-old,
   half-new chain.

## Sequence: user changes a pedalboard slot's module type

This is the pedalboard-specific variant of "user loads a preset" above, and the one
place a JUCE parameter-listener callback can legitimately fire on the *audio* thread
(host automation of a choice parameter can be delivered from inside `processBlock()`)
— see `docs/ARCHITECTURE.md` §4.1 for the full design rationale.

1. UI thread (usually) or audio thread (if a host automates the slot's type
   parameter): `SlotManager::parameterChanged()` fires. It does the absolute minimum
   possible — an 8-element allocation-free string comparison to find which slot, then
   one relaxed `atomic<bool>` store — and returns. No module construction, no vector
   mutation happens here, on either thread.
2. Message thread: `SlotManager`'s 20 Hz `juce::Timer` fires, sees the dirty flag,
   and calls `rebuildSlot()` — constructs the new `IAudioModule` (or `nullptr` for
   "None"), calls `prepare()` on it. This is message-thread-only bookkeeping state
   (`SlotManager::slots_`) the audio thread never reads directly.
3. Message thread: `publishChain()` walks every slot's current module and builds two
   brand-new immutable structures — a "slot bindings" vector (which JUCE knob maps to
   which live module's `tfcore::Parameter*`, for `syncToCore()`) and a
   `tfcore::ChainState` (for audio processing) — then publishes both via atomic
   pointer swap (release semantics), the same mechanism `SignalChain::setChain()`
   already uses.
4. Audio thread: the next `processBlock()` calls `SlotManager::syncToCore()`, which
   does one atomic load (acquire semantics) of the bindings snapshot, then plain
   atomic reads/writes to sync knob/bypass state into whichever modules are now live,
   followed by `SignalChain::process()`'s own atomic load of the new `ChainState`.
   Neither snapshot is ever mutated after publishing, so there is no window where the
   audio thread could observe a half-old, half-new slot binding.

`PluginProcessor::applyPreset()` reuses this exact path for every slot at once
(driving each slot's APVTS parameters, then calling `SlotManager::forceRebuildNow()`
— a synchronous, message-thread-only call to the same rebuild logic the timer would
otherwise reach on its next tick, so a preset load takes effect immediately instead
of within ~50ms) rather than constructing modules directly, which is what closed the
dangling-pointer bug an earlier version of this document flagged as outstanding.

## Sequence: "Generate Tone" from the AI panel (as implemented)

1. UI thread: user types a prompt in `AiPanel`, clicks "Generate Tone" (or presses
   Return). `AiPanel::onGenerateClicked()` bails out immediately if a request is
   already in flight or the prompt is empty; otherwise calls `setBusy(true)` (disables
   all three buttons) and shows a "Generating..." status — this call is *seconds*, not
   milliseconds, and the UI must never look frozen while it's in flight.
2. UI thread: captures `juce::Component::SafePointer<AiPanel> safeThis(this)` and calls
   `processor_.getAiClient().generate(prompt, callback)`, returning immediately.
3. `AiClient::generate()` (still on the UI thread, briefly): builds the JSON request
   body (`{"prompt": ...}`) via `tfcore::json::Value`, then calls `runInBackground()`,
   which prunes any finished request threads and spawns a new `RequestThread` to run
   the rest.
4. AI request thread (the new `RequestThread`, off both the audio thread and the UI
   thread): performs the blocking HTTP POST to `http://127.0.0.1:8765/generate` (15s
   connection timeout), and on success parses the JSON response body into a
   `tfcore::Preset` via `PresetSerializer::fromJson`. Both the HTTP call and the parse
   are wrapped in one `try/catch(const std::exception&)` — a malformed response or a
   dropped connection produces a `GenerateResult{success=false, errorMessage=...}`
   rather than a crash.
5. AI request thread: calls `juce::MessageManager::callAsync([onComplete, result] {
   onComplete(result); })`, handing the fully-formed, immutable `GenerateResult` to the
   UI thread. The `RequestThread` itself has nothing left to do after this and exits;
   it's reaped by a later `pruneFinishedThreads()` call, not joined here.
6. UI thread: the callback checks `safeThis == nullptr` first (the editor/`AiPanel` may
   have been closed while the request was in flight) and returns early if so — no
   further UI or processor calls happen in that case. Otherwise
   `AiPanel::handleGenerateResult()` runs: on success it calls
   `processor_.applyPreset(result.preset)` (proceeding exactly as in "user loads a
   preset" above), then always writes `result.preset.aiProvenance->disclaimer` into the
   output display alongside the preset name/description — never silently carried in
   data only. On failure it shows `result.errorMessage` in the status line instead and
   leaves the pedalboard untouched.

"Modify Tone" and "Explain Tone" follow the same shape, with two differences: the
outgoing request body also includes `processor_.captureCurrentPreset()` (a fresh
snapshot of the live pedalboard state, not a saved preset), and "Explain Tone"'s result
only ever populates the output display — it does not call `applyPreset()`, since
explaining a tone must never side-effect it.

## Known technical debt in the current thread model

- **Per-block, not per-sample, parameter reads.** Continuous knob moves are
  currently applied at block boundaries, which can produce audible zipper
  noise on smaller-than-ideal block sizes or slow automation ramps. M2/M3
  should add a smoothing stage (e.g. `juce::SmoothedValue`-equivalent inside
  `Parameter` or at the point of use in each module) — this is a DSP-quality
  issue, not a thread-safety one, but it's rooted in this same read path so
  it's noted here.
- **`std::atomic<shared_ptr<T>>` lock-free guarantee is unverified on all
  target platforms.** See ARCHITECTURE.md 3.2. Must be checked (and a
  fallback double-buffer implemented if needed) before M3 ships on a real
  target.
- **No explicit backpressure/cancellation across AI request threads.** `AiPanel`'s
  `requestInFlight_` flag disables all three buttons while a request is outstanding, so
  in practice the UI can't fire a second request until the first completes or fails —
  but nothing at the `AiClient` layer itself would stop a caller from doing so, and
  there's no cancellation if a request becomes stale (e.g. the user would rather bail
  out of a slow request than wait). Low risk today since `AiPanel` is the only caller
  and it self-serializes; worth a real cancellation token if a second caller is ever
  added (tracked for M6).
