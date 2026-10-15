#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "tfcore/preset/ModuleState.h"

namespace tfcore {

// Provenance metadata attached to AI-generated or AI-modified presets.
// `disclaimer` is required and non-empty by construction (see
// PresetSerializer) specifically so the UI can never accidentally present an
// AI-inspired preset as an "exact" recreation - see docs/ARCHITECTURE.md
// section 5.2 on the song reference system for why this matters.
struct AiProvenance {
    std::string provider;         // "mock" | "ollama:<model>" | "cloud:<provider>"
    std::string prompt;           // original user prompt / instruction
    std::string matchType;        // "generated" | "modified" | "song_curated" | "song_inferred"
    double confidence = 1.0;      // 0..1, meaningful mainly for song-reference matches
    std::string disclaimer;       // e.g. "Inspired by - approximation, not an exact recreation."
};

// The current preset schema version. Bump this and add a migration step in
// PresetSerializer whenever a breaking change is made to the JSON shape.
inline constexpr int kCurrentPresetSchemaVersion = 1;

struct Preset {
    std::string id;               // stable unique id (UUID), assigned at creation
    std::string name;
    std::string description;
    std::string author;
    int schemaVersion = kCurrentPresetSchemaVersion;
    std::vector<std::string> tags;
    bool favorite = false;
    std::chrono::system_clock::time_point createdAt{};
    std::vector<ModuleState> chain; // ordered: chain[0] is first in the signal path
    std::optional<AiProvenance> aiProvenance;
};

} // namespace tfcore
