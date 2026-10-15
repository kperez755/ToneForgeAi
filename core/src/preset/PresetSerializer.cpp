#include "tfcore/preset/PresetSerializer.h"

#include <cstdio>
#include <ctime>
#include <stdexcept>

#include "tfcore/util/Json.h"

namespace tfcore {

using json::Value;

namespace {

Value moduleStateToJson(const ModuleState& state) {
    Value v = Value::makeObject();
    v.set("moduleId", state.moduleId);
    v.set("bypassed", state.bypassed);
    Value params = Value::makeObject();
    for (const auto& [key, val] : state.parameters) params.set(key, val);
    v.set("parameters", std::move(params));
    if (state.resourceRef) v.set("resourceRef", *state.resourceRef);
    return v;
}

ModuleState moduleStateFromJson(const Value& v) {
    ModuleState state;
    if (const auto* id = v.find("moduleId")) state.moduleId = id->asString();
    if (const auto* bypassed = v.find("bypassed")) state.bypassed = bypassed->asBool();
    if (const auto* params = v.find("parameters")) {
        for (const auto& [key, val] : params->asObject()) {
            state.parameters[key] = static_cast<float>(val.asNumber());
        }
    }
    if (const auto* ref = v.find("resourceRef")) state.resourceRef = ref->asString();
    return state;
}

Value provenanceToJson(const AiProvenance& p) {
    Value v = Value::makeObject();
    v.set("provider", p.provider);
    v.set("prompt", p.prompt);
    v.set("matchType", p.matchType);
    v.set("confidence", p.confidence);
    v.set("disclaimer", p.disclaimer);
    return v;
}

AiProvenance provenanceFromJson(const Value& v) {
    AiProvenance p;
    if (const auto* x = v.find("provider")) p.provider = x->asString();
    if (const auto* x = v.find("prompt")) p.prompt = x->asString();
    if (const auto* x = v.find("matchType")) p.matchType = x->asString();
    if (const auto* x = v.find("confidence")) p.confidence = x->asNumber();
    if (const auto* x = v.find("disclaimer")) p.disclaimer = x->asString();
    return p;
}

// gmtime_r/timegm are POSIX-only and strptime doesn't exist on MSVC at all,
// so time (de)serialization is implemented portably here rather than
// relying on any of the three: gmtime_s (Windows) / gmtime_r (POSIX) behind
// a platform guard for UTC decomposition, sscanf for parsing the fixed
// "%Y-%m-%dT%H:%M:%SZ" layout instead of strptime, and _mkgmtime (Windows) /
// timegm (POSIX) behind a platform guard for the inverse. This was only
// caught by an actual Windows/MSVC build - the Linux sandbox this project
// was originally built and tested in has gmtime_r/strptime/timegm as
// standard glibc extensions, so the bug was invisible there. Logged as a
// concrete example of why docs/ROADMAP.md flags "core has only been
// verified on the platform it happened to be built on" as a real risk.
std::tm gmTimeUtc(std::time_t t) {
    std::tm result{};
#if defined(_WIN32)
    gmtime_s(&result, &t);
#else
    gmtime_r(&t, &result);
#endif
    return result;
}

std::time_t timegmPortable(std::tm& tmVal) {
#if defined(_WIN32)
    return _mkgmtime(&tmVal);
#else
    return timegm(&tmVal);
#endif
}

std::string formatIso8601(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmVal = gmTimeUtc(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmVal);
    return std::string(buf);
}

std::chrono::system_clock::time_point parseIso8601(const std::string& text) {
    std::tm tmVal{};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    const int fieldsParsed = std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%dZ", &year, &month, &day, &hour, &minute, &second);
    if (fieldsParsed != 6) {
        return std::chrono::system_clock::time_point{};
    }
    tmVal.tm_year = year - 1900;
    tmVal.tm_mon = month - 1;
    tmVal.tm_mday = day;
    tmVal.tm_hour = hour;
    tmVal.tm_min = minute;
    tmVal.tm_sec = second;
    tmVal.tm_isdst = 0;
    std::time_t t = timegmPortable(tmVal);
    return std::chrono::system_clock::from_time_t(t);
}

} // namespace

std::string PresetSerializer::toJson(const Preset& preset, bool pretty) {
    Value root = Value::makeObject();
    root.set("id", preset.id);
    root.set("name", preset.name);
    root.set("description", preset.description);
    root.set("author", preset.author);
    root.set("schemaVersion", preset.schemaVersion);
    root.set("favorite", preset.favorite);
    root.set("createdAt", formatIso8601(preset.createdAt));

    Value tags = Value::makeArray();
    for (const auto& tag : preset.tags) tags.push(Value(tag));
    root.set("tags", std::move(tags));

    Value chain = Value::makeArray();
    for (const auto& module : preset.chain) chain.push(moduleStateToJson(module));
    root.set("chain", std::move(chain));

    if (preset.aiProvenance) {
        root.set("aiProvenance", provenanceToJson(*preset.aiProvenance));
    }

    return root.dump(pretty ? 2 : 0);
}

Preset PresetSerializer::fromJson(const std::string& text) {
    Value root = Value::parse(text);
    if (!root.isObject()) throw std::runtime_error("PresetSerializer: root is not an object");

    Preset preset;
    if (const auto* v = root.find("id")) preset.id = v->asString();
    if (const auto* v = root.find("name")) preset.name = v->asString();
    if (const auto* v = root.find("description")) preset.description = v->asString();
    if (const auto* v = root.find("author")) preset.author = v->asString();
    if (const auto* v = root.find("schemaVersion")) preset.schemaVersion = static_cast<int>(v->asNumber(kCurrentPresetSchemaVersion));
    if (const auto* v = root.find("favorite")) preset.favorite = v->asBool();
    if (const auto* v = root.find("createdAt")) preset.createdAt = parseIso8601(v->asString());

    if (const auto* v = root.find("tags")) {
        for (const auto& tag : v->asArray()) preset.tags.push_back(tag.asString());
    }

    if (const auto* v = root.find("chain")) {
        for (const auto& moduleJson : v->asArray()) preset.chain.push_back(moduleStateFromJson(moduleJson));
    }

    if (const auto* v = root.find("aiProvenance")) {
        preset.aiProvenance = provenanceFromJson(*v);
    }

    // Schema migration seam: today there is exactly one schema version, so
    // there is nothing to migrate. When schemaVersion 2 is introduced, add
    // `if (preset.schemaVersion == 1) { migrateV1ToV2(preset); }` here
    // rather than changing the fields read above in place.
    if (preset.schemaVersion > kCurrentPresetSchemaVersion) {
        throw std::runtime_error("PresetSerializer: preset schema version is newer than this build supports");
    }

    return preset;
}

} // namespace tfcore
