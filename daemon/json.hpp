// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Tiny JSON value type with parser and pretty printer. Only what the two config files
// need; keeps the daemon free of extra dependencies. Object member order is preserved so
// that the written files stay stable and diff-friendly.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace priomix::json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

class Value {
public:
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : data_(b) {}
    Value(int n) : data_(static_cast<double>(n)) {}
    Value(int64_t n) : data_(static_cast<double>(n)) {}
    Value(uint32_t n) : data_(static_cast<double>(n)) {}
    Value(double n) : data_(n) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Object o) : data_(std::move(o)) {}

    bool IsNull() const { return std::holds_alternative<std::monostate>(data_); }
    bool IsBool() const { return std::holds_alternative<bool>(data_); }
    bool IsNumber() const { return std::holds_alternative<double>(data_); }
    bool IsString() const { return std::holds_alternative<std::string>(data_); }
    bool IsArray() const { return std::holds_alternative<Array>(data_); }
    bool IsObject() const { return std::holds_alternative<Object>(data_); }

    bool AsBool(bool fallback = false) const;
    double AsNumber(double fallback = 0) const;
    std::string AsString(std::string fallback = {}) const;
    const Array& AsArray() const;
    const Object& AsObject() const;

    // Object member lookup, nullptr if this is not an object or the key is missing.
    const Value* Find(std::string_view key) const;

    std::string Serialize(int indent = 2) const;

private:
    void SerializeTo(std::string& out, int indent, int depth) const;

    std::variant<std::monostate, bool, double, std::string, Array, Object> data_;
};

// Returns std::nullopt on malformed input; error receives a short description.
std::optional<Value> Parse(std::string_view text, std::string* error = nullptr);

}  // namespace priomix::json
