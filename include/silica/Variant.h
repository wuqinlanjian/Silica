#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace silica {

class Variant;

// Containers used by the reactive store: a list for dynamic data (ForEach) and
// a string-keyed record for structured elements (`@item.name`).
using VariantArray  = std::vector<Variant>;
using VariantObject = std::map<std::string, Variant, std::less<>>;

// A dynamically typed value carried by the DataStore and produced by the
// expression evaluator: null, bool, number, string, array and object. Colors
// travel as their string form (#RRGGBB) and are parsed by the widget property
// that receives them.
//
// Arrays and objects live behind a shared_ptr and are copied by handle. There
// is no mutation API: rebuild and Set a new container instead, which keeps the
// store's change detection (deep equality) meaningful.
class Variant {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Variant() = default;
    Variant(bool value) : m_type(Type::Bool), m_bool(value) {}
    Variant(int value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Variant(long long value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Variant(float value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Variant(double value) : m_type(Type::Number), m_number(value) {}
    Variant(std::string value) : m_type(Type::String), m_string(std::move(value)) {}
    Variant(char const* value) : m_type(Type::String), m_string(value ? value : "") {}

    static Variant Array(VariantArray items) {
        Variant value;
        value.m_type  = Type::Array;
        value.m_array = std::make_shared<VariantArray>(std::move(items));
        return value;
    }

    static Variant Object(VariantObject fields) {
        Variant value;
        value.m_type   = Type::Object;
        value.m_object = std::make_shared<VariantObject>(std::move(fields));
        return value;
    }

    [[nodiscard]] Type type() const { return m_type; }
    [[nodiscard]] bool IsNull() const { return m_type == Type::Null; }
    [[nodiscard]] bool IsBool() const { return m_type == Type::Bool; }
    [[nodiscard]] bool IsNumber() const { return m_type == Type::Number; }
    [[nodiscard]] bool IsString() const { return m_type == Type::String; }
    [[nodiscard]] bool IsArray() const { return m_type == Type::Array; }
    [[nodiscard]] bool IsObject() const { return m_type == Type::Object; }

    // Empty views for values of another type, so `@items.length` on a missing
    // key never throws.
    [[nodiscard]] VariantArray const& AsArray() const {
        static VariantArray const empty;
        return IsArray() && m_array ? *m_array : empty;
    }

    [[nodiscard]] VariantObject const& AsObject() const {
        static VariantObject const empty;
        return IsObject() && m_object ? *m_object : empty;
    }

    // Array length, object field count, string length in bytes, else 0.
    [[nodiscard]] std::size_t Count() const {
        switch (m_type) {
        case Type::Array:
            return IsArray() && m_array ? m_array->size() : 0;
        case Type::Object:
            return IsObject() && m_object ? m_object->size() : 0;
        case Type::String:
            return m_string.size();
        default:
            return 0;
        }
    }

    // Field of an object (`@item.name`), null when absent or not an object.
    [[nodiscard]] Variant Field(std::string_view name) const {
        if (!IsObject() || !m_object) return Variant{};
        auto const found = m_object->find(name);
        return found == m_object->end() ? Variant{} : found->second;
    }

    // Element of an array (`@item.tags.0`), null when out of range.
    [[nodiscard]] Variant Index(std::size_t index) const {
        if (!IsArray() || !m_array || index >= m_array->size()) return Variant{};
        return (*m_array)[index];
    }

    // Truthiness used by conditions: nonzero numbers, non-empty strings other
    // than "false"/"0", true booleans, and non-empty containers are true; null
    // is false.
    [[nodiscard]] bool Truthy() const {
        switch (m_type) {
        case Type::Bool:
            return m_bool;
        case Type::Number:
            return m_number != 0.0 && !std::isnan(m_number);
        case Type::String:
            return !m_string.empty() && m_string != "false" && m_string != "0";
        case Type::Array:
            return Count() > 0;
        case Type::Object:
            return Count() > 0;
        case Type::Null:
        default:
            return false;
        }
    }

    [[nodiscard]] double ToNumber() const {
        switch (m_type) {
        case Type::Bool:
            return m_bool ? 1.0 : 0.0;
        case Type::Number:
            return m_number;
        case Type::String: {
            char*        end   = nullptr;
            double const value = std::strtod(m_string.c_str(), &end);
            return end != m_string.c_str() ? value : 0.0;
        }
        case Type::Null:
        default:
            return 0.0;
        }
    }

    [[nodiscard]] std::string ToString() const {
        switch (m_type) {
        case Type::Bool:
            return m_bool ? "true" : "false";
        case Type::Number:
            return FormatNumber(m_number);
        case Type::String:
            return m_string;
        case Type::Array: {
            std::string out = "[";
            bool        first = true;
            for (auto const& item : AsArray()) {
                if (!first) out += ", ";
                first = false;
                out += item.ToString();
            }
            out += ']';
            return out;
        }
        case Type::Object: {
            std::string out = "{";
            bool        first = true;
            for (auto const& [key, value] : AsObject()) {
                if (!first) out += ", ";
                first = false;
                out += key;
                out += ": ";
                out += value.ToString();
            }
            out += '}';
            return out;
        }
        case Type::Null:
        default:
            return {};
        }
    }

    // Form fed back into ApplyProperty. Null becomes an empty string so a
    // binding whose data is not set yet leaves the widget on its default
    // instead of writing a literal "null".
    [[nodiscard]] std::string ToPropertyString() const { return ToString(); }

    bool operator==(Variant const& other) const {
        if (m_type == other.m_type) {
            switch (m_type) {
            case Type::Null:
                return true;
            case Type::Bool:
                return m_bool == other.m_bool;
            case Type::Number:
                return m_number == other.m_number;
            case Type::String:
                return m_string == other.m_string;
            case Type::Array: {
                auto const& a = AsArray();
                auto const& b = other.AsArray();
                if (a.size() != b.size()) return false;
                for (std::size_t i = 0; i < a.size(); ++i) {
                    if (!(a[i] == b[i])) return false;
                }
                return true;
            }
            case Type::Object:
                return AsObject() == other.AsObject();
            }
        }
        if (m_type == Type::Null || other.m_type == Type::Null) return false;
        // Containers only equal containers of the same shape; never fall back
        // to the numeric/string coercions below.
        if (IsArray() || IsObject() || other.IsArray() || other.IsObject()) return false;
        if (m_type == Type::Number || other.m_type == Type::Number) return ToNumber() == other.ToNumber();
        if (m_type == Type::Bool || other.m_type == Type::Bool) return Truthy() == other.Truthy();
        return ToString() == other.ToString();
    }

    bool operator!=(Variant const& other) const { return !(*this == other); }

private:
    static std::string FormatNumber(double value) {
        if (!std::isfinite(value)) return "0";
        char       buffer[64];
        auto const result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{}) return std::to_string(value);
        return std::string(buffer, result.ptr);
    }

    Type        m_type   = Type::Null;
    bool        m_bool   = false;
    double      m_number = 0.0;
    std::string m_string;

    std::shared_ptr<VariantArray>  m_array;
    std::shared_ptr<VariantObject> m_object;
};

} // namespace silica
