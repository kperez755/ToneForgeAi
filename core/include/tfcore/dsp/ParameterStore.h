#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tfcore {

// Describes the legal range and response curve of a parameter. Pure metadata,
// consumed by the UI layer to build sliders/knobs; never touched by process().
struct ParameterRange {
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float step = 0.0f;   // 0 = continuous
    float skew = 1.0f;   // 1 = linear, <1 biases toward min, >1 biases toward max

    [[nodiscard]] float clamp(float value) const noexcept {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    // Linear map from a host-facing normalized [0,1] float to this
    // parameter's real [minValue, maxValue] range, and back. Added for the
    // pedalboard slot system (docs/ARCHITECTURE.md "Pedalboard slots"):
    // each slot exposes a fixed pool of generic 0..1 knob parameters to
    // JUCE (since APVTS parameters can't change range after construction),
    // and these convert between "what the host/UI knob says" and "what the
    // currently-bound module's real parameter needs". Deliberately linear
    // only - `skew` is UI-curve metadata that, consistent with the rest of
    // this codebase today, is not yet applied anywhere; a skewed
    // normalize/denormalize pair would be a follow-up if a module ever
    // needs non-linear knob feel.
    [[nodiscard]] float denormalize(float normalized) const noexcept {
        const float clamped01 = normalized < 0.0f ? 0.0f : (normalized > 1.0f ? 1.0f : normalized);
        return minValue + (maxValue - minValue) * clamped01;
    }

    [[nodiscard]] float normalize(float value) const noexcept {
        if (maxValue <= minValue) return 0.0f;
        return (clamp(value) - minValue) / (maxValue - minValue);
    }
};

// One RT-safe parameter. UI/background threads call set(); the audio thread
// calls get(). std::atomic<float> is lock-free on every mainstream target
// (verified via is_always_lock_free in the unit tests) which is what makes
// this safe to read from process() without a mutex.
class Parameter {
public:
    // valueLabels, when non-empty, names each consecutive integer value in
    // [range.minValue, range.maxValue] - e.g. Reverb's "character"
    // parameter (range 0..3) passes {"Hall","Plate","Spring","Room"} so
    // value 0 means "Hall", 1 means "Plate", and so on. This is what lets
    // the UI layer render a labeled dropdown/segmented switch instead of a
    // bare numeric knob for parameters that are really a discrete choice
    // dressed up as a float (see docs/ARCHITECTURE.md "Pedalboard slots" -
    // every pedalboard knob is a continuous [0,1] host parameter under the
    // hood regardless of what it means to the currently-bound module, so
    // "discrete" here is a UI-presentation concern layered on top, not a
    // different underlying parameter type). Throws if the label count
    // doesn't match the range's integer span, since a mismatch here is a
    // programmer error in the module's constructor, not bad runtime data.
    Parameter(std::string id, std::string displayName, ParameterRange range, float defaultValue,
              std::string units = {}, std::vector<std::string> valueLabels = {})
        : id_(std::move(id)),
          displayName_(std::move(displayName)),
          range_(range),
          units_(std::move(units)),
          value_(range.clamp(defaultValue)),
          defaultValue_(range.clamp(defaultValue)),
          valueLabels_(std::move(valueLabels)) {
        if (!valueLabels_.empty()) {
            const auto expected = static_cast<std::size_t>(range_.maxValue - range_.minValue) + 1;
            if (valueLabels_.size() != expected) {
                throw std::invalid_argument("Parameter '" + id_ + "': valueLabels size (" +
                                             std::to_string(valueLabels_.size()) +
                                             ") does not match range's integer span (" +
                                             std::to_string(expected) + ")");
            }
        }
    }

    [[nodiscard]] float get() const noexcept { return value_.load(std::memory_order_relaxed); }

    void set(float newValue) noexcept { value_.store(range_.clamp(newValue), std::memory_order_relaxed); }

    void resetToDefault() noexcept { value_.store(defaultValue_, std::memory_order_relaxed); }

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& displayName() const noexcept { return displayName_; }
    [[nodiscard]] const std::string& units() const noexcept { return units_; }
    [[nodiscard]] const ParameterRange& range() const noexcept { return range_; }
    [[nodiscard]] float defaultValue() const noexcept { return defaultValue_; }
    [[nodiscard]] const std::vector<std::string>& valueLabels() const noexcept { return valueLabels_; }
    [[nodiscard]] bool isDiscreteChoice() const noexcept { return !valueLabels_.empty(); }

private:
    std::string id_;
    std::string displayName_;
    ParameterRange range_;
    std::string units_;
    std::atomic<float> value_;
    float defaultValue_;
    std::vector<std::string> valueLabels_;
};

// Fixed-cardinality set of parameters owned by a module. The set of
// parameters is established during construction/prepare() and never mutated
// afterward, which is what makes concurrent lookup-by-name safe without a
// lock: the underlying vector never reallocates once "sealed".
class ParameterStore {
public:
    Parameter& add(std::string id, std::string displayName, ParameterRange range, float defaultValue,
                   std::string units = {}, std::vector<std::string> valueLabels = {}) {
        if (sealed_) {
            throw std::logic_error("ParameterStore: cannot add parameters after seal()");
        }
        parameters_.push_back(std::make_unique<Parameter>(std::move(id), std::move(displayName), range, defaultValue,
                                                            std::move(units), std::move(valueLabels)));
        return *parameters_.back();
    }

    // Called once, after all add() calls, typically at the end of the owning
    // module's constructor. After this, the parameter list is immutable.
    void seal() noexcept { sealed_ = true; }

    [[nodiscard]] Parameter* find(std::string_view id) noexcept {
        for (auto& p : parameters_)
            if (p->id() == id) return p.get();
        return nullptr;
    }

    [[nodiscard]] const Parameter* find(std::string_view id) const noexcept {
        for (auto& p : parameters_)
            if (p->id() == id) return p.get();
        return nullptr;
    }

    void forEach(const std::function<void(Parameter&)>& fn) {
        for (auto& p : parameters_) fn(*p);
    }

    void forEach(const std::function<void(const Parameter&)>& fn) const {
        for (auto& p : parameters_) fn(*p);
    }

    [[nodiscard]] std::size_t size() const noexcept { return parameters_.size(); }

private:
    std::vector<std::unique_ptr<Parameter>> parameters_;
    bool sealed_ = false;
};

} // namespace tfcore
