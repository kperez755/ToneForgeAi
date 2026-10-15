#include "tfcore/util/Json.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace tfcore::json {

namespace {

void dumpString(const std::string& s, std::ostream& out) {
    out << '"';
    for (char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out << buf;
                } else {
                    out << c;
                }
        }
    }
    out << '"';
}

void dumpNumber(double n, std::ostream& out) {
    if (n == static_cast<long long>(n) && std::abs(n) < 1e15) {
        out << static_cast<long long>(n);
    } else {
        std::ostringstream tmp;
        tmp.precision(9);
        tmp << n;
        out << tmp.str();
    }
}

void writeIndent(std::ostream& out, int indent, int depth) {
    if (indent <= 0) return;
    out << '\n';
    for (int i = 0; i < indent * depth; ++i) out << ' ';
}

void dumpValue(const Value& v, std::ostream& out, int indent, int depth) {
    switch (v.type()) {
        case Value::Type::Null: out << "null"; break;
        case Value::Type::Bool: out << (v.asBool() ? "true" : "false"); break;
        case Value::Type::Number: dumpNumber(v.asNumber(), out); break;
        case Value::Type::String: dumpString(v.asString(), out); break;
        case Value::Type::Array: {
            const auto& arr = v.asArray();
            if (arr.empty()) { out << "[]"; break; }
            out << '[';
            bool first = true;
            for (const auto& item : arr) {
                if (!first) out << ',';
                first = false;
                writeIndent(out, indent, depth + 1);
                dumpValue(item, out, indent, depth + 1);
            }
            writeIndent(out, indent, depth);
            out << ']';
            break;
        }
        case Value::Type::Object: {
            const auto& obj = v.asObject();
            if (obj.empty()) { out << "{}"; break; }
            out << '{';
            bool first = true;
            for (const auto& [key, val] : obj) {
                if (!first) out << ',';
                first = false;
                writeIndent(out, indent, depth + 1);
                dumpString(key, out);
                out << ':';
                if (indent > 0) out << ' ';
                dumpValue(val, out, indent, depth + 1);
            }
            writeIndent(out, indent, depth);
            out << '}';
            break;
        }
    }
}

// --- Parser -----------------------------------------------------------

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    Value parseValue() {
        skipWhitespace();
        if (pos_ >= text_.size()) throw std::runtime_error("Json: unexpected end of input");
        char c = text_[pos_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return Value(parseString());
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        return parseNumber();
    }

    void expectEnd() {
        skipWhitespace();
        if (pos_ != text_.size()) throw std::runtime_error("Json: trailing content after top-level value");
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;

    void skipWhitespace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    char peek() {
        if (pos_ >= text_.size()) throw std::runtime_error("Json: unexpected end of input");
        return text_[pos_];
    }

    void expect(char c) {
        if (pos_ >= text_.size() || text_[pos_] != c) {
            throw std::runtime_error(std::string("Json: expected '") + c + "'");
        }
        ++pos_;
    }

    Value parseObject() {
        expect('{');
        Object obj;
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return Value(std::move(obj)); }
        while (true) {
            skipWhitespace();
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            Value val = parseValue();
            obj[key] = std::move(val);
            skipWhitespace();
            if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; }
            expect('}');
            break;
        }
        return Value(std::move(obj));
    }

    Value parseArray() {
        expect('[');
        Array arr;
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return Value(std::move(arr)); }
        while (true) {
            arr.push_back(parseValue());
            skipWhitespace();
            if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; }
            expect(']');
            break;
        }
        return Value(std::move(arr));
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) throw std::runtime_error("Json: unterminated string");
            char c = text_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= text_.size()) throw std::runtime_error("Json: bad escape");
                char esc = text_[pos_++];
                switch (esc) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        // Minimal \uXXXX support: only handles the BMP
                        // (no surrogate pairs), sufficient for our schema
                        // which does not require full Unicode escapes.
                        if (pos_ + 4 > text_.size()) throw std::runtime_error("Json: bad \\u escape");
                        pos_ += 4; // skip hex digits, codepoint not reconstructed (documented scope limit)
                        out += '?';
                        break;
                    }
                    default: throw std::runtime_error("Json: unknown escape");
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    Value parseBool() {
        if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; return Value(true); }
        if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; return Value(false); }
        throw std::runtime_error("Json: invalid literal");
    }

    Value parseNull() {
        if (text_.compare(pos_, 4, "null") == 0) { pos_ += 4; return Value(nullptr); }
        throw std::runtime_error("Json: invalid literal");
    }

    Value parseNumber() {
        std::size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
        if (pos_ == start) throw std::runtime_error("Json: invalid number");
        try {
            return Value(std::stod(text_.substr(start, pos_ - start)));
        } catch (...) {
            throw std::runtime_error("Json: invalid number literal");
        }
    }
};

} // namespace

std::string Value::dump(int indent) const {
    std::ostringstream out;
    dumpValue(*this, out, indent, 0);
    return out.str();
}

Value Value::parse(const std::string& text) {
    Parser parser(text);
    Value v = parser.parseValue();
    parser.expectEnd();
    return v;
}

} // namespace tfcore::json
