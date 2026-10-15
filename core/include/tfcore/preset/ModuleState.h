#pragma once

#include <map>
#include <optional>
#include <string>

namespace tfcore {

// Off-thread-only snapshot of a single module's state, used for
// preset save/restore. Deliberately dependency-free (no JSON library here —
// PresetSerializer in the preset/ directory owns JSON (de)serialization so
// this header stays cheap to include everywhere).
struct ModuleState {
    std::string moduleId;               // e.g. "drive.v1" - stable, versioned
    bool bypassed = false;
    std::map<std::string, float> parameters;
    std::optional<std::string> resourceRef; // e.g. cab IR file id, amp model id
};

} // namespace tfcore
