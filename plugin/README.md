# plugin/ — status

**Not yet built or run anywhere.** `Source/PluginProcessor.{h,cpp}` and
`Source/PluginEditor.{h,cpp}` are a first-pass skeleton establishing the
JUCE-to-`tfcore` integration pattern from `docs/ARCHITECTURE.md` section 4:
own a `tfcore::SignalChain`, adapt buffers at the boundary, apply presets by
building a new `ChainState` off-thread and swapping it in. See
`docs/SETUP_WINDOWS.md` for how to actually compile this, and the "NOT YET
COMPILED ANYWHERE" comment at the top of each file as a standing reminder
not to trust these files' correctness until they've been built once.

Planned subdirectories, not yet populated (`docs/ROADMAP.md` M3):

- `Source/ui/` — `SignalChainView`, `ModuleEditorPanel`, `PresetBrowserPanel`,
  `AiAssistantPanel`, `MeterComponent`.
- `Source/state/` — `ParameterBridge` (maps `tfcore::ParameterStore` to
  `juce::AudioProcessorValueTreeState`).
- `Source/ai/` — `AiWorker` (background thread that talks HTTP to
  `ai-backend`; see `docs/THREAD_MODEL.md` "Sequence: Generate Tone").

These were deliberately left unimplemented this session rather than
written speculatively with no way to compile or test them — see
`docs/ROADMAP.md` "Environment reality check" for why, and M3 for the plan.
