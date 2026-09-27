// SPDX-License-Identifier: GPL-3.0-only
#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace priomix::json {

namespace {

const Array kEmptyArray;
const Object kEmptyObject;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::optional<Value> ParseDocument(std::string* error) {
        auto value = ParseValue(0);
        SkipWhitespace();
        if (value && pos_ != text_.size()) {
            Fail("trailing characters");
            value.reset();
        }
        if (!value && error != nullptr) {
            *error = error_ + " at offset " + std::to_string(pos_);
        }
        return value;
    }

private:
    static constexpr int kMaxDepth = 64;

    void Fail(const char* message) {
        if (error_.empty()) error_ = message;
    }

    void SkipWhitespace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool Consume(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) == literal) {
            pos_ += literal.size();
            return true;
        }
        return false;
    }

    std::optional<Value> ParseValue(int depth) {
        if (depth > kMaxDepth) {
            Fail("nesting too deep");
            return std::nullopt;
        }
        SkipWhitespace();
        if (pos_ >= text_.size()) {
            Fail("unexpected end of input");
            return std::nullopt;
        }
        char c = text_[pos_];
        if (c == '{') return ParseObject(depth);
        if (c == '[') return ParseArray(depth);
        if (c == '"') {
            auto s = ParseString();
            if (!s) return std::nullopt;
            return Value(std::move(*s));
        }
        if (Consume("true")) return Value(true);
        if (Consume("false")) return Value(false);
        if (Consume("null")) return Value(nullptr);
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
        Fail("unexpected character");
        return std::nullopt;
    }

    std::optional<Value> ParseNumber() {
        const std::string number(text_.substr(pos_, 64));
        char* end = nullptr;
        double value = std::strtod(number.c_str(), &end);
        if (end == number.c_str()) {
            Fail("invalid number");
            return std::nullopt;
        }
        pos_ += static_cast<size_t>(end - number.c_str());
        return Value(value);
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::optional<uint32_t> ParseHex4() {
        if (pos_ + 4 > text_.size()) return std::nullopt;
        uint32_t cp = 0;
        for (int i = 0; i < 4; ++i) {
            char h = text_[pos_++];
            cp <<= 4;
            if (h >= '0' && h <= '9')
                cp |= static_cast<uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f')
                cp |= static_cast<uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
                cp |= static_cast<uint32_t>(h - 'A' + 10);
            else
                return std::nullopt;
        }
        return cp;
    }

    std::optional<std::string> ParseString() {
        ++pos_;  // opening quote
        std::string out;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) break;
            char e = text_[pos_++];
            switch (e) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    auto cp = ParseHex4();
                    if (!cp) {
                        Fail("invalid unicode escape");
                        return std::nullopt;
                    }
                    // Surrogate pair
                    if (*cp >= 0xD800 && *cp <= 0xDBFF && Consume("\\u")) {
                        auto low = ParseHex4();
                        if (!low || *low < 0xDC00 || *low > 0xDFFF) {
                            Fail("invalid surrogate pair");
                            return std::nullopt;
                        }
                        *cp = 0x10000 + ((*cp - 0xD800) << 10) + (*low - 0xDC00);
                    }
                    AppendUtf8(out, *cp);
                    break;
                }
                default:
                    Fail("invalid escape");
                    return std::nullopt;
            }
        }
        Fail("unterminated string");
        return std::nullopt;
    }

    std::optional<Value> ParseArray(int depth) {
        ++pos_;
        Array array;
        SkipWhitespace();
        if (Consume("]")) return Value(std::move(array));
        while (true) {
            auto item = ParseValue(depth + 1);
            if (!item) return std::nullopt;
            array.push_back(std::move(*item));
            SkipWhitespace();
            if (Consume(",")) continue;
            if (Consume("]")) return Value(std::move(array));
            Fail("expected ',' or ']'");
            return std::nullopt;
        }
    }

    std::optional<Value> ParseObject(int depth) {
        ++pos_;
        Object object;
        SkipWhitespace();
        if (Consume("}")) return Value(std::move(object));
        while (true) {
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                Fail("expected member name");
                return std::nullopt;
            }
            auto key = ParseString();
            if (!key) return std::nullopt;
            SkipWhitespace();
            if (!Consume(":")) {
                Fail("expected ':'");
                return std::nullopt;
            }
            auto value = ParseValue(depth + 1);
            if (!value) return std::nullopt;
            object.emplace_back(std::move(*key), std::move(*value));
            SkipWhitespace();
            if (Consume(",")) continue;
            if (Consume("}")) return Value(std::move(object));
            Fail("expected ',' or '}'");
            return std::nullopt;
        }
    }

    std::string_view text_;
    size_t pos_ = 0;
    std::string error_;
};

void AppendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

}  // namespace

bool Value::AsBool(bool fallback) const {
    return IsBool() ? std::get<bool>(data_) : fallback;
}

double Value::AsNumber(double fallback) const {
    return IsNumber() ? std::get<double>(data_) : fallback;
}

std::string Value::AsString(std::string fallback) const {
    return IsString() ? std::get<std::string>(data_) : fallback;
}

const Array& Value::AsArray() const {
    return IsArray() ? std::get<Array>(data_) : kEmptyArray;
}

const Object& Value::AsObject() const {
    return IsObject() ? std::get<Object>(data_) : kEmptyObject;
}

const Value* Value::Find(std::string_view key) const {
    for (const auto& [name, value] : AsObject()) {
        if (name == key) return &value;
    }
    return nullptr;
}

std::string Value::Serialize(int indent) const {
    std::string out;
    SerializeTo(out, indent, 0);
    out += '\n';
    return out;
}

void Value::SerializeTo(std::string& out, int indent, int depth) const {
    auto newline = [&](int d) {
        out += '\n';
        out.append(static_cast<size_t>(indent * d), ' ');
    };

    if (IsNull()) {
        out += "null";
    } else if (IsBool()) {
        out += std::get<bool>(data_) ? "true" : "false";
    } else if (IsNumber()) {
        double n = std::get<double>(data_);
        char buf[32];
        if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 1e15) {
            std::snprintf(buf, sizeof(buf), "%.0f", n);
        } else if (std::isfinite(n)) {
            std::snprintf(buf, sizeof(buf), "%.17g", n);
        } else {
            std::snprintf(buf, sizeof(buf), "null");
        }
        out += buf;
    } else if (IsString()) {
        AppendEscaped(out, std::get<std::string>(data_));
    } else if (IsArray()) {
        const auto& array = std::get<Array>(data_);
        if (array.empty()) {
            out += "[]";
            return;
        }
        // Arrays of scalars stay on one line, arrays of containers get one item per line.
        bool nested = false;
        for (const auto& item : array)
            nested = nested || item.IsArray() || item.IsObject();
        out += '[';
        for (size_t i = 0; i < array.size(); ++i) {
            if (i > 0) out += nested ? "," : ", ";
            if (nested) newline(depth + 1);
            array[i].SerializeTo(out, indent, depth + 1);
        }
        if (nested) newline(depth);
        out += ']';
    } else {
        const auto& object = std::get<Object>(data_);
        if (object.empty()) {
            out += "{}";
            return;
        }
        // Small flat nested objects (priority entries) stay on one line.
        bool flat = depth > 0 && object.size() <= 2;
        for (const auto& [key, value] : object)
            flat = flat && !value.IsArray() && !value.IsObject();
        out += flat ? "{ " : "{";
        for (size_t i = 0; i < object.size(); ++i) {
            if (i > 0) out += flat ? ", " : ",";
            if (!flat) newline(depth + 1);
            AppendEscaped(out, object[i].first);
            out += ": ";
            object[i].second.SerializeTo(out, indent, depth + 1);
        }
        if (flat) {
            out += " }";
        } else {
            newline(depth);
            out += '}';
        }
    }
}

std::optional<Value> Parse(std::string_view text, std::string* error) {
    return Parser(text).ParseDocument(error);
}

}  // namespace priomix::json
