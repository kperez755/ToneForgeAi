#include "TestFramework.h"
#include "tfcore/preset/PresetSerializer.h"

using namespace tfcore;

namespace {

Preset makeSamplePreset() {
    Preset preset;
    preset.id = "preset-001";
    preset.name = "Cliffs of Dover Lead";
    preset.description = "Bright, compressed lead tone inspired by Eric Johnson's playing style.";
    preset.author = "ToneForge AI";
    preset.tags = {"lead", "bright", "compressed"};
    preset.favorite = true;

    ModuleState gate;
    gate.moduleId = "noise_gate.v1";
    gate.bypassed = false;
    gate.parameters["threshold_db"] = -45.0f;
    gate.parameters["attack_ms"] = 2.0f;
    preset.chain.push_back(gate);

    ModuleState drive;
    drive.moduleId = "drive.v1";
    drive.bypassed = false;
    drive.parameters["gain_db"] = 18.0f;
    drive.parameters["tone"] = 0.75f;
    drive.resourceRef = "amp_model_clean_boost";
    preset.chain.push_back(drive);

    AiProvenance provenance;
    provenance.provider = "mock";
    provenance.prompt = "Give me a Cliffs of Dover style lead tone.";
    provenance.matchType = "song_curated";
    provenance.confidence = 0.72;
    provenance.disclaimer = "Inspired by - approximation, not an exact recreation.";
    preset.aiProvenance = provenance;

    return preset;
}

} // namespace

TEST_CASE("PresetSerializer round-trips a full preset through JSON") {
    Preset original = makeSamplePreset();
    std::string json = PresetSerializer::toJson(original);
    Preset restored = PresetSerializer::fromJson(json);

    REQUIRE(restored.id == original.id);
    REQUIRE(restored.name == original.name);
    REQUIRE(restored.description == original.description);
    REQUIRE(restored.author == original.author);
    REQUIRE(restored.favorite == original.favorite);
    REQUIRE(restored.tags.size() == 3);
    REQUIRE(restored.chain.size() == 2);
    REQUIRE(restored.chain[0].moduleId == "noise_gate.v1");
    REQUIRE_APPROX(restored.chain[0].parameters.at("threshold_db"), -45.0f);
    REQUIRE(restored.chain[1].moduleId == "drive.v1");
    REQUIRE(restored.chain[1].resourceRef.has_value());
    REQUIRE(*restored.chain[1].resourceRef == "amp_model_clean_boost");
}

TEST_CASE("PresetSerializer round-trips AI provenance including the disclaimer") {
    Preset original = makeSamplePreset();
    std::string json = PresetSerializer::toJson(original);
    Preset restored = PresetSerializer::fromJson(json);

    REQUIRE(restored.aiProvenance.has_value());
    REQUIRE(restored.aiProvenance->matchType == "song_curated");
    REQUIRE(restored.aiProvenance->disclaimer == "Inspired by - approximation, not an exact recreation.");
    REQUIRE(!restored.aiProvenance->disclaimer.empty());
}

TEST_CASE("PresetSerializer defaults schemaVersion to current when absent and rejects future versions") {
    Preset original = makeSamplePreset();
    std::string json = PresetSerializer::toJson(original);
    Preset restored = PresetSerializer::fromJson(json);
    REQUIRE(restored.schemaVersion == kCurrentPresetSchemaVersion);

    std::string futureJson = R"({"id":"x","name":"y","schemaVersion":999,"chain":[]})";
    bool threw = false;
    try {
        PresetSerializer::fromJson(futureJson);
    } catch (const std::exception&) {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("PresetSerializer handles a preset with no AI provenance") {
    Preset preset;
    preset.id = "manual-001";
    preset.name = "Hand Tuned Clean";

    std::string json = PresetSerializer::toJson(preset);
    Preset restored = PresetSerializer::fromJson(json);
    REQUIRE_FALSE(restored.aiProvenance.has_value());
}
