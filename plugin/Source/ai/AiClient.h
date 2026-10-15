#pragma once

// NOT YET COMPILED ANYWHERE - see docs/SETUP_WINDOWS.md.
//
// Talks to the local ai-backend (docs/ARCHITECTURE.md section 5) over
// plain HTTP on localhost (default http://127.0.0.1:8765). The plugin
// never embeds a language model or calls Python in-process - see
// ai-backend/app/main.py's own header comment, which already named this
// class before it existed.

#include <functional>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>
// juce::MessageManager::callAsync (used throughout AiClient.cpp to always
// deliver results back to the message thread) lives in the juce_events
// module, not juce_core - juce_core.h alone doesn't declare it. The module
// itself was already being compiled/linked transitively (via
// juce_audio_utils's own dependencies, declared in plugin/CMakeLists.txt),
// so this was a missing #include, not a missing link dependency - caught by
// a real first Windows build attempt (MSVC C3083/C2039/C3861 on
// MessageManager::callAsync), not guessed in advance.
#include <juce_events/juce_events.h>

#include "tfcore/preset/Preset.h"

namespace toneforge {

struct GenerateResult {
    bool success = false;
    tfcore::Preset preset;
    juce::String errorMessage; // meaningful only when success == false
};

struct ExplainResult {
    bool success = false;
    juce::String text; // the explanation on success, an error message otherwise
};

struct HealthResult {
    bool reachable = false;
    juce::String statusSummary; // short, human-readable, safe to put straight in a Label
};

// Every public method here spawns a background thread to perform the
// blocking HTTP call + JSON (de)serialization, then delivers its result
// back via juce::MessageManager::callAsync - callbacks always land on the
// message thread, never the thread that did the networking, so callers
// (AiPanel) never need to reason about cross-thread safety themselves.
// Nothing in this class ever touches the audio thread, and it only ever
// produces/consumes plain tfcore::Preset value objects (no live
// IAudioModule/Parameter pointers - those stay owned by SlotManager, see
// state/ParameterBridge.h); the caller applies a returned preset via
// ToneForgeAudioProcessor::applyPreset() (message-thread-only) itself.
//
// Owned by ToneForgeAudioProcessor (long-lived, one per plugin instance),
// not by the editor - a request can legitimately still be in flight (a
// local LLM can take many seconds) when the user closes and reopens the
// plugin window, and the processor outlives that. Completion callbacks
// passed in from AiPanel should capture a juce::Component::SafePointer to
// themselves, not a raw `this`, for exactly this reason - see AiPanel.cpp.
class AiClient {
public:
    explicit AiClient(juce::String baseUrl = "http://127.0.0.1:8765");
    ~AiClient();

    void generate(juce::String prompt, std::function<void(GenerateResult)> onComplete);
    // Song/artist reference lookup (POST /song-reference): matches against
    // a small curated database of hand-tuned tones first (deterministic,
    // no model involved), falling back to asking the primary AI provider
    // to infer a plausible tone from its general knowledge of the song on
    // a miss - see ai-backend/app/song_reference/lookup.py. Same
    // GenerateResult shape as generate()/modify() since the backend
    // response is a PresetDTO either way.
    void songReference(juce::String query, std::function<void(GenerateResult)> onComplete);
    void modify(tfcore::Preset current, juce::String instruction, std::function<void(GenerateResult)> onComplete);
    void explain(tfcore::Preset current, std::function<void(ExplainResult)> onComplete);
    void checkHealth(std::function<void(HealthResult)> onComplete);

private:
    void runInBackground(std::function<void()> task);
    void pruneFinishedThreads();

    juce::String baseUrl_;
    // Owns every in-flight/recently-finished request thread. Only ever
    // touched from the message thread (all public methods above, plus
    // the destructor, are message-thread-only) so no extra locking is
    // needed around this vector itself.
    std::vector<std::unique_ptr<juce::Thread>> threads_;
};

} // namespace toneforge
