#include "TestFramework.h"
#include "tfcore/dsp/ParameterStore.h"

using namespace tfcore;

TEST_CASE("Parameter clamps values to its range") {
    Parameter p("gain_db", "Gain", ParameterRange{0.0f, 40.0f}, 12.0f, "dB");
    p.set(1000.0f);
    REQUIRE_APPROX(p.get(), 40.0f);
    p.set(-1000.0f);
    REQUIRE_APPROX(p.get(), 0.0f);
}

TEST_CASE("Parameter starts at its clamped default value") {
    Parameter p("tone", "Tone", ParameterRange{0.0f, 1.0f}, 5.0f);
    REQUIRE_APPROX(p.get(), 1.0f); // 5.0 clamped into [0,1]
}

TEST_CASE("Parameter resetToDefault restores the original default") {
    Parameter p("level_db", "Level", ParameterRange{-24.0f, 12.0f}, -6.0f, "dB");
    p.set(10.0f);
    REQUIRE_APPROX(p.get(), 10.0f);
    p.resetToDefault();
    REQUIRE_APPROX(p.get(), -6.0f);
}

TEST_CASE("std::atomic<float> is lock-free on this platform") {
    // This is the property SignalChain's per-parameter concurrency model
    // depends on (docs/ARCHITECTURE.md 3.2): audio-thread reads must never
    // block on a UI-thread write. If this ever fails on a target platform,
    // Parameter must switch to a different RT-safe mechanism.
    REQUIRE(std::atomic<float>::is_always_lock_free);
}

TEST_CASE("ParameterStore find() locates parameters by id") {
    ParameterStore store;
    store.add("threshold_db", "Threshold", ParameterRange{-80.0f, 0.0f}, -50.0f, "dB");
    store.add("attack_ms", "Attack", ParameterRange{0.1f, 50.0f}, 2.0f, "ms");
    store.seal();

    REQUIRE(store.size() == 2);
    auto* threshold = store.find("threshold_db");
    REQUIRE(threshold != nullptr);
    REQUIRE_APPROX(threshold->get(), -50.0f);
    REQUIRE(store.find("nonexistent") == nullptr);
}

TEST_CASE("ParameterRange denormalize maps [0,1] onto [min,max]") {
    ParameterRange range{-24.0f, 12.0f};
    REQUIRE_APPROX(range.denormalize(0.0f), -24.0f);
    REQUIRE_APPROX(range.denormalize(1.0f), 12.0f);
    REQUIRE_APPROX(range.denormalize(0.5f), -6.0f);
}

TEST_CASE("ParameterRange denormalize clamps out-of-range normalized input") {
    ParameterRange range{0.0f, 40.0f};
    REQUIRE_APPROX(range.denormalize(-5.0f), 0.0f);
    REQUIRE_APPROX(range.denormalize(5.0f), 40.0f);
}

TEST_CASE("ParameterRange normalize is the inverse of denormalize") {
    ParameterRange range{-80.0f, 0.0f};
    REQUIRE_APPROX(range.normalize(-80.0f), 0.0f);
    REQUIRE_APPROX(range.normalize(0.0f), 1.0f);
    REQUIRE_APPROX(range.normalize(-40.0f), 0.5f);
    // Round trip through both directions.
    REQUIRE_APPROX(range.denormalize(range.normalize(-20.0f)), -20.0f);
}

TEST_CASE("ParameterRange normalize clamps out-of-range input and never divides by zero") {
    ParameterRange degenerate{5.0f, 5.0f}; // min == max
    REQUIRE_APPROX(degenerate.normalize(5.0f), 0.0f);
    REQUIRE_APPROX(degenerate.normalize(100.0f), 0.0f);

    ParameterRange range{0.0f, 1.0f};
    REQUIRE_APPROX(range.normalize(-10.0f), 0.0f);
    REQUIRE_APPROX(range.normalize(10.0f), 1.0f);
}

TEST_CASE("Parameter with no valueLabels is not a discrete choice") {
    Parameter p("mix", "Mix", ParameterRange{0.0f, 1.0f}, 0.5f);
    REQUIRE_FALSE(p.isDiscreteChoice());
    REQUIRE(p.valueLabels().empty());
}

TEST_CASE("Parameter with valueLabels reports itself as a discrete choice") {
    Parameter p("character", "Character", ParameterRange{0.0f, 3.0f, 1.0f, 1.0f}, 0.0f, "",
                std::vector<std::string>{"Hall", "Plate", "Spring", "Room"});
    REQUIRE(p.isDiscreteChoice());
    REQUIRE(p.valueLabels().size() == 4);
    REQUIRE(p.valueLabels()[2] == "Spring");
}

TEST_CASE("Parameter throws if valueLabels count doesn't match the range's integer span") {
    bool threw = false;
    try {
        Parameter p("character", "Character", ParameterRange{0.0f, 3.0f, 1.0f, 1.0f}, 0.0f, "",
                    std::vector<std::string>{"Hall", "Plate"}); // range spans 4 values, only 2 labels given
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("ParameterStore forEach visits every parameter") {
    ParameterStore store;
    store.add("a", "A", ParameterRange{0.0f, 1.0f}, 0.0f);
    store.add("b", "B", ParameterRange{0.0f, 1.0f}, 0.0f);
    store.add("c", "C", ParameterRange{0.0f, 1.0f}, 0.0f);
    store.seal();

    int count = 0;
    store.forEach([&count](Parameter& p) {
        p.set(1.0f);
        ++count;
    });
    REQUIRE(count == 3);
    REQUIRE_APPROX(store.find("a")->get(), 1.0f);
    REQUIRE_APPROX(store.find("b")->get(), 1.0f);
    REQUIRE_APPROX(store.find("c")->get(), 1.0f);
}
