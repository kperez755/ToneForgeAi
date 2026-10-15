// Standalone verification tool: loads every JSON file in presets/factory/
// through tfcore::PresetSerializer::fromJson and reports success/failure.
// Not part of the normal test suite (it does file IO, which core/tests
// deliberately avoids) - this is a dev-time / CI-time sanity check that the
// ai-backend's PresetDTO JSON output is actually consumable by the C++
// side, run manually or from a CI step, not from process().
//
// Usage: verify_factory_presets <path-to-presets-directory>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "tfcore/preset/PresetSerializer.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: verify_factory_presets <presets-dir>\n";
        return 2;
    }

    int failures = 0;
    int checked = 0;

    for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
        if (entry.path().extension() != ".json") continue;
        ++checked;

        std::ifstream file(entry.path());
        std::stringstream buffer;
        buffer << file.rdbuf();

        try {
            tfcore::Preset preset = tfcore::PresetSerializer::fromJson(buffer.str());

            if (preset.id.empty()) throw std::runtime_error("empty id");
            if (preset.name.empty()) throw std::runtime_error("empty name");
            if (preset.chain.empty()) throw std::runtime_error("empty chain");
            if (preset.aiProvenance && preset.aiProvenance->disclaimer.empty()) {
                throw std::runtime_error("aiProvenance present but disclaimer is empty");
            }

            // Round-trip: re-serialize and re-parse, confirm stability.
            std::string reserialized = tfcore::PresetSerializer::toJson(preset);
            tfcore::Preset reparsed = tfcore::PresetSerializer::fromJson(reserialized);
            if (reparsed.chain.size() != preset.chain.size()) {
                throw std::runtime_error("chain size changed across round-trip");
            }

            std::cout << "[ OK ] " << entry.path().filename().string()
                       << " (" << preset.name << ", " << preset.chain.size() << " modules"
                       << (preset.aiProvenance ? ", AI-provenanced" : "") << ")\n";
        } catch (const std::exception& ex) {
            std::cout << "[FAIL] " << entry.path().filename().string() << ": " << ex.what() << "\n";
            ++failures;
        }
    }

    std::cout << "\n" << checked << " preset file(s) checked, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
