#pragma once

#include <string>

#include "tfcore/preset/Preset.h"

namespace tfcore {

// Converts Preset <-> JSON text. Off-audio-thread only (allocates freely,
// may throw on malformed input via fromJson).
//
// Schema is versioned via Preset::schemaVersion. toJson() always writes
// kCurrentPresetSchemaVersion. fromJson() dispatches on the version field
// read from the document so older presets can be migrated forward; today
// there is only one version, so migration is a no-op, but the seam exists
// so this isn't a breaking change later (see docs/ROADMAP.md M4).
class PresetSerializer {
public:
    [[nodiscard]] static std::string toJson(const Preset& preset, bool pretty = true);

    // Throws std::runtime_error if `text` is not valid JSON or does not
    // match the expected Preset shape.
    [[nodiscard]] static Preset fromJson(const std::string& text);
};

} // namespace tfcore
