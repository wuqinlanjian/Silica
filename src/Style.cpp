#include "silica/Style.h"

#include "silica/Property.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace silica {
namespace {

struct Direction {
    bool horizontal = false;
    bool reverse    = false;
};

std::optional<Direction> ParseDirection(std::string const& text) {
    if (text == "to right" || text == "right" || text == "to-right" || text == "row") {
        return Direction{true, false};
    }
    if (text == "to left" || text == "left" || text == "to-left") return Direction{true, true};
    if (text == "to bottom" || text == "bottom" || text == "down" || text == "to-bottom"
        || text == "column" || text == "vertical") {
        return Direction{false, false};
    }
    if (text == "to top" || text == "top" || text == "up" || text == "to-top") {
        return Direction{false, true};
    }

    std::string const number = text.ends_with("deg") ? text.substr(0, text.size() - 3) : text;
    auto const        angle  = property::ParseNumber(number);
    if (!angle) return std::nullopt;

    float const degrees = std::fmod(std::fmod(*angle, 360.f) + 360.f, 360.f);
    float const radians = degrees * 3.14159265f / 180.f;
    float const x       = std::sin(radians);  // 0deg points up, clockwise
    float const y       = -std::cos(radians);
    Direction   direction;
    if (std::fabs(x) >= std::fabs(y)) {
        direction.horizontal = true;
        direction.reverse    = x < 0.f;
    } else {
        direction.horizontal = false;
        direction.reverse    = y < 0.f;
    }
    return direction;
}

std::vector<std::string> SplitCommas(std::string const& text) {
    std::vector<std::string> parts;
    size_t                   start = 0;
    int                      depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char const c = text[i];
        if (c == '(') ++depth;
        else if (c == ')') depth = std::max(0, depth - 1);
        else if (c == ',' && depth == 0) {
            parts.push_back(std::string(property::Trim(std::string_view(text).substr(start, i - start))));
            start = i + 1;
        }
    }
    parts.push_back(std::string(property::Trim(std::string_view(text).substr(start))));
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](std::string const& p) { return p.empty(); }), parts.end());
    return parts;
}

std::optional<float> ParsePosition(std::string_view text) {
    text = property::Trim(text);
    bool percent = false;
    if (!text.empty() && text.back() == '%') {
        text.remove_suffix(1);
        percent = true;
    }
    auto number = property::ParseNumber(text);
    if (!number) return std::nullopt;
    return percent ? *number / 100.f : *number;
}

void FillMissingPositions(std::vector<std::optional<float>>& positions) {
    if (positions.empty()) return;
    if (!positions.front()) positions.front() = 0.f;
    if (!positions.back()) positions.back() = 1.f;

    size_t i = 0;
    while (i < positions.size()) {
        if (positions[i]) {
            ++i;
            continue;
        }
        size_t const begin = i;
        while (i < positions.size() && !positions[i]) ++i;
        size_t const end    = i - 1; // last missing index
        float const  before = *positions[begin - 1];
        float const  after  = *positions[i];
        float const  count  = static_cast<float>(end - begin + 2);
        for (size_t at = begin; at <= end; ++at) {
            float const step = static_cast<float>(at - begin + 1) / count;
            positions[at]    = before + (after - before) * step;
        }
    }

    for (auto& position : positions) position = std::clamp(*position, 0.f, 1.f);
}

} // namespace

GradientSpec GradientSpec::WithAlpha(float alpha) const {
    GradientSpec copy = *this;
    copy.from         = from.ScaleAlpha(alpha);
    copy.to           = to.ScaleAlpha(alpha);
    for (auto& stop : copy.stops) stop.color = stop.color.ScaleAlpha(alpha);
    return copy;
}

std::optional<GradientSpec> ParseGradient(std::string_view text) {
    std::string trimmed(text);
    std::string const lowered = property::ToLower(property::Trim(text));

    if (lowered.rfind("linear-gradient", 0) == 0) {
        size_t const open  = trimmed.find('(');
        size_t const close = trimmed.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open) return std::nullopt;

        std::vector<std::string> parts =
            SplitCommas(trimmed.substr(open + 1, close - open - 1));
        if (parts.size() < 2) return std::nullopt;

        Direction direction;
        std::string const first = property::ToLower(property::Trim(parts.front()));
        if (!property::ParseColor(parts.front())) {
            auto parsed = ParseDirection(first);
            if (!parsed) return std::nullopt;
            direction = *parsed;
            parts.erase(parts.begin());
        }
        if (parts.empty()) return std::nullopt;

        std::vector<std::optional<float>> positions;
        std::vector<Color>                colors;
        for (auto const& part : parts) {
            auto const tokens = property::Split(part);
            if (tokens.empty()) continue;
            auto color = property::ParseColor(tokens[0]);
            if (!color) return std::nullopt;
            colors.push_back(*color);
            positions.push_back(tokens.size() > 1 ? ParsePosition(tokens[1]) : std::nullopt);
        }
        if (colors.empty()) return std::nullopt;
        FillMissingPositions(positions);

        GradientSpec spec;
        spec.horizontal = direction.horizontal;
        for (size_t i = 0; i < colors.size(); ++i) {
            float const position = direction.reverse ? 1.f - *positions[i] : *positions[i];
            spec.stops.push_back({position, colors[i]});
        }
        std::stable_sort(spec.stops.begin(), spec.stops.end(), [](GradientStop const& a, GradientStop const& b) {
            return a.position < b.position;
        });
        spec.from = spec.stops.front().color;
        spec.to   = spec.stops.back().color;
        return spec;
    }

    // Legacy "from to" pair with optional horizontal/vertical hint.
    auto const tokens = property::Split(text);
    if (tokens.size() == 1 && property::ToLower(tokens[0]) == "none") return std::nullopt;

    GradientSpec               spec;
    std::vector<Color>         colors;
    for (auto const& token : tokens) {
        std::string const word = property::ToLower(token);
        if (word == "horizontal" || word == "row" || word == "h" || word == "to-right") {
            spec.horizontal = true;
            continue;
        }
        if (word == "vertical" || word == "column" || word == "v" || word == "to-bottom") {
            spec.horizontal = false;
            continue;
        }
        auto color = property::ParseColor(token);
        if (!color) return std::nullopt;
        colors.push_back(*color);
    }
    if (colors.empty()) return std::nullopt;
    // A single color stays a solid fill; only two or more tokens form the
    // legacy gradient form.
    if (colors.size() < 2) return std::nullopt;
    spec.from = colors.front();
    spec.to   = colors.back();
    for (size_t i = 0; i < colors.size(); ++i) {
        float const position = colors.size() == 1
            ? 0.f
            : static_cast<float>(i) / static_cast<float>(colors.size() - 1);
        spec.stops.push_back({position, colors[i]});
    }
    return spec;
}

UIStyle UIStyle::WithOpacity(float value) const {
    UIStyle copy = *this;
    copy.opacity = value;
    return copy;
}

UIStyle UIStyle::DarkPanel() {
    UIStyle style;
    style.background  = Color{.055f, .059f, .066f, .92f};
    style.borderColor = Color{.353f, .357f, .361f, .90f};
    style.borderWidth = 1.f;
    style.textColor   = Color{.95f, .955f, .965f, 1.f};
    return style;
}

UIStyle UIStyle::PrimaryButton() {
    UIStyle style;
    style.background  = Color{.424f, .765f, .286f, 1.f};
    style.borderColor = Color{.55f, .85f, .42f, 1.f};
    style.borderWidth = 1.f;
    style.textColor   = Color{1.f, 1.f, 1.f, 1.f};
    return style;
}

UIStyle UIStyle::Outline() {
    UIStyle style;
    style.background  = kTransparent;
    style.borderColor = Color{.353f, .357f, .361f, .90f};
    style.borderWidth = 1.f;
    style.textColor   = Color{.95f, .955f, .965f, 1.f};
    return style;
}

} // namespace silica
