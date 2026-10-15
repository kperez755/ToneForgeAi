#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace tfcore::json {

// A deliberately small, dependency-free JSON value type. `core` has zero
// third-party dependencies by design (see docs/ARCHITECTURE.md) so preset
// (de)serialization - the one place `core` needs JSON - is self-contained
// rather than pulling in a third-party header. This is NOT a general-purpose
// JSON library: no comments, no streaming parser, no unicode surrogate
// pairs. It is scoped exactly to what Preset/ModuleState serialization
// needs, and is unit tested against that scope.
//
// This is entirely off the audio thread: only used by PresetSerializer,
// which is only ever called from the UI/background thread.
class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>; // ordered by key: deterministic output for tests/diffs

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() : type_(Type::Null) {}
    Value(std::nullptr_t) : type_(Type::Null) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(double n) : type_(Type::Number), number_(n) {}
    Value(int n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    Value(std::string s) : type_(Type::String), string_(std::move(s)) {}
    Value(const char* s) : type_(Type::String), string_(s) {}
    Value(Array a) : type_(Type::Array), array_(std::move(a)) {}
    Value(Object o) : type_(Type::Object), object_(std::move(o)) {}

    [[nodiscard]] static Value makeObject() { return Value(Object{}); }
    [[nodiscard]] static Value makeArray() { return Value(Array{}); }

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool isNull() const noexcept { return type_ == Type::Null; }
    [[nodiscard]] bool isObject() const noexcept { return type_ == Type::Object; }
    [[nodiscard]] bool isArray() const noexcept { return type_ == Type::Array; }
    [[nodiscard]] bool isString() const noexcept { return type_ == Type::String; }
    [[nodiscard]] bool isNumber() const noexcept { return type_ == Type::Number; }
    [[nodiscard]] bool isBool() const noexcept { return type_ == Type::Bool; }

    [[nodiscard]] bool asBool(bool fallback = false) const noexcept { return type_ == Type::Bool ? bool_ : fallback; }
    [[nodiscard]] double asNumber(double fallback = 0.0) const noexcept { return type_ == Type::Number ? number_ : fallback; }
    [[nodiscard]] const std::string& asString() const {
        static const std::string empty;
        return type_ == Type::String ? string_ : empty;
    }
    [[nodiscard]] const Array& asArray() const {
        static const Array empty;
        return type_ == Type::Array ? array_ : empty;
    }
    [[nodiscard]] const Object& asObject() const {
        static const Object empty;
        return type_ == Type::Object ? object_ : empty;
    }

    // Object helpers. set()/push() are only meaningful when the value is
    // (or was default-constructed as) the matching type.
    Value& set(const std::string& key, Value value) {
        if (type_ != Type::Object) { type_ = Type::Object; }
        object_[key] = std::move(value);
        return *this;
    }

    void push(Value value) {
        if (type_ != Type::Array) { type_ = Type::Array; }
        array_.push_back(std::move(value));
    }

    // Returns nullptr if key is absent or this is not an object.
    [[nodiscard]] const Value* find(const std::string& key) const {
        if (type_ != Type::Object) return nullptr;
        auto it = object_.find(key);
        return it == object_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::string dump(int indent = 0) const;

    // Throws std::runtime_error on malformed input.
    [[nodiscard]] static Value parse(const std::string& text);

private:
    Type type_;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

} // namespace tfcore::json
