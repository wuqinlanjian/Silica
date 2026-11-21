#include "silica/Property.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace silica::property {

std::string_view Trim(std::string_view text) {
    size_t begin = 0;
    size_t end   = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string ToLower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::optional<Color> ParseColor(std::string_view text) {
    text = Trim(text);
    if (text.empty()) return std::nullopt;

    if (text.front() == '#') {
        std::string_view const hex = text.substr(1);
        if (hex.size() != 3 && hex.size() != 4 && hex.size() != 6 && hex.size() != 8) return std::nullopt;

        int value[8] = {};
        for (size_t i = 0; i < hex.size(); ++i) {
            char const c = hex[i];
            int        digit;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return std::nullopt;
            value[i] = digit;
        }

        auto channel = [](int v) { return static_cast<float>(v) / 255.f; };
        switch (hex.size()) {
        case 3:
            return Color{channel(value[0] * 17), channel(value[1] * 17), channel(value[2] * 17), 1.f};
        case 4:
            return Color{
                channel(value[0] * 17),
                channel(value[1] * 17),
                channel(value[2] * 17),
                channel(value[3] * 17)};
        case 6:
            return Color{
                channel(value[0] * 16 + value[1]),
                channel(value[2] * 16 + value[3]),
                channel(value[4] * 16 + value[5]),
                1.f};
        default:
            return Color{
                channel(value[0] * 16 + value[1]),
                channel(value[2] * 16 + value[3]),
                channel(value[4] * 16 + value[5]),
                channel(value[6] * 16 + value[7])};
        }
    }

    std::string const name = ToLower(text);
    if (name == "transparent" || name == "none") return kTransparent;
    if (name == "white") return Color{1.f, 1.f, 1.f, 1.f};
    if (name == "black") return Color{0.f, 0.f, 0.f, 1.f};
    if (name == "red") return Color{1.f, .23f, .19f, 1.f};
    if (name == "green") return Color{.3f, .78f, .35f, 1.f};
    if (name == "blue") return Color{.25f, .48f, 1.f, 1.f};
    if (name == "yellow") return Color{1.f, .9f, .25f, 1.f};
    if (name == "orange") return Color{1.f, .6f, .2f, 1.f};
    if (name == "purple") return Color{.6f, .35f, .9f, 1.f};
    if (name == "pink") return Color{1.f, .55f, .75f, 1.f};
    if (name == "cyan") return Color{.2f, .85f, .9f, 1.f};
    if (name == "magenta") return Color{.9f, .3f, .85f, 1.f};
    if (name == "gray" || name == "grey") return Color{.5f, .5f, .5f, 1.f};
    return std::nullopt;
}

std::optional<float> ParseNumber(std::string_view text) {
    text = Trim(text);
    if (text.empty()) return std::nullopt;

    for (std::string_view const suffix : {"px", "em", "pt", "dp"}) {
        if (text.size() > suffix.size() && text.ends_with(suffix)) {
            text.remove_suffix(suffix.size());
            break;
        }
    }
    text = Trim(text);
    if (text.empty()) return std::nullopt;

    std::string const copy(text);
    char*             end = nullptr;
    float const       value = std::strtof(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<bool> ParseBool(std::string_view text) {
    std::string const value = ToLower(Trim(text));
    if (value == "true" || value == "1" || value == "yes" || value == "on") return true;
    if (value == "false" || value == "0" || value == "no" || value == "off") return false;
    return std::nullopt;
}

std::vector<std::string> Split(std::string_view text) {
    std::vector<std::string> tokens;
    size_t                   i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        size_t const begin = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i > begin) tokens.emplace_back(text.substr(begin, i - begin));
    }
    return tokens;
}

std::optional<BorderSpec> ParseBorder(std::string_view text) {
    std::vector<std::string> const tokens = Split(text);
    if (tokens.empty()) return std::nullopt;

    if (tokens.size() == 1) {
        std::string const value = ToLower(tokens[0]);
        if (value == "none" || value == "0") return BorderSpec{0.f, kTransparent};
    }

    BorderSpec spec;
    for (auto const& token : tokens) {
        std::string const value = ToLower(token);
        if (value == "solid" || value == "dashed" || value == "dotted" || value == "double") continue;
        if (value == "none") {
            spec.width = 0.f;
            continue;
        }
        if (auto width = ParseNumber(token)) {
            spec.width = *width;
            continue;
        }
        if (auto color = ParseColor(token)) {
            spec.color = *color;
            continue;
        }
        return std::nullopt;
    }
    if (!spec.width && !spec.color) return std::nullopt;
    return spec;
}

} // namespace silica::property
