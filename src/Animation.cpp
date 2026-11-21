#include "silica/Animation.h"

#include "silica/Container.h"
#include "silica/Property.h"
#include "silica/Widget.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace silica {
namespace {

std::mutex& KeyframeMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, KeyframeSet>& KeyframeTable() {
    static std::map<std::string, KeyframeSet> table;
    return table;
}

std::string_view StripQuotes(std::string_view text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text.remove_prefix(1);
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

std::optional<float> ParseSeconds(std::string_view text) {
    text = property::Trim(text);
    bool milliseconds = false;
    if (text.size() > 2 && text.ends_with("ms")) {
        text.remove_suffix(2);
        milliseconds = true;
    } else if (!text.empty() && text.back() == 's') {
        text.remove_suffix(1);
    }
    text = property::Trim(text);
    if (text.empty()) return std::nullopt;

    std::string const copy(text);
    char*             end   = nullptr;
    float const       value = std::strtof(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value) || value < 0.f) return std::nullopt;
    return milliseconds ? value / 1000.f : value;
}

float Ease(Easing easing, float t) {
    t = std::clamp(t, 0.f, 1.f);
    switch (easing) {
    case Easing::EaseOutQuad: {
        float const inverse = 1.f - t;
        return 1.f - inverse * inverse;
    }
    case Easing::EaseInOutCubic:
        return t < .5f ? 4.f * t * t * t : 1.f - std::pow(2.f - 2.f * t, 3.f) * .5f;
    case Easing::EaseInOutSine:
        return .5f - .5f * std::cos(3.14159265f * t);
    default:
        return t;
    }
}

bool ParseEasing(std::string_view name, Easing& easing) {
    std::string const key = property::ToLower(property::Trim(name));
    if (key == "linear") {
        easing = Easing::Linear;
        return true;
    }
    if (key == "ease-out" || key == "easeout" || key == "ease-out-quad" || key == "easeoutquad"
        || key == "out") {
        easing = Easing::EaseOutQuad;
        return true;
    }
    if (key == "ease-in-out" || key == "easeinout" || key == "ease-in-out-cubic"
        || key == "easeinoutcubic" || key == "ease" || key == "cubic") {
        easing = Easing::EaseInOutCubic;
        return true;
    }
    if (key == "ease-in-out-sine" || key == "sine") {
        easing = Easing::EaseInOutSine;
        return true;
    }
    return false;
}

Color LerpColor(Color const& from, Color const& to, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return Color{
        from.r + (to.r - from.r) * t,
        from.g + (to.g - from.g) * t,
        from.b + (to.b - from.b) * t,
        from.a + (to.a - from.a) * t,
    };
}

void RegisterKeyframes(std::map<std::string, KeyframeSet> sets) {
    std::lock_guard lock(KeyframeMutex());
    KeyframeTable() = std::move(sets);
}

std::optional<KeyframeSet> FindKeyframes(std::string_view name) {
    std::lock_guard lock(KeyframeMutex());
    auto const     it = KeyframeTable().find(std::string(name));
    if (it == KeyframeTable().end()) return std::nullopt;
    return it->second;
}

bool ParseTransitionSpec(std::string_view text, std::string& prop, TransitionSpec& spec) {
    auto const tokens = property::Split(text);
    if (tokens.empty()) return false;

    prop = property::ToLower(tokens.front());
    bool sawValue = false;
    for (size_t i = 1; i < tokens.size(); ++i) {
        if (auto seconds = ParseSeconds(tokens[i])) {
            spec.duration = *seconds;
            sawValue      = true;
            continue;
        }
        if (Easing easing; ParseEasing(tokens[i], easing)) {
            spec.easing = easing;
            sawValue    = true;
        }
    }
    return sawValue;
}

bool ParseConditional(std::string_view text, ConditionalValue& out) {
    text = property::Trim(text);
    int depth = 0;
    size_t question = std::string_view::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        char const c = text[i];
        if (c == '(') ++depth;
        else if (c == ')') depth = std::max(0, depth - 1);
        else if (c == '?' && depth == 0) {
            question = i;
            break;
        }
    }
    if (question == std::string_view::npos) return false;

    size_t colon = std::string_view::npos;
    depth        = 0;
    for (size_t i = question + 1; i < text.size(); ++i) {
        char const c = text[i];
        if (c == '(') ++depth;
        else if (c == ')') depth = std::max(0, depth - 1);
        else if (c == ':' && depth == 0) colon = i;
    }
    if (colon == std::string_view::npos) return false;

    out.condition = std::string(property::Trim(text.substr(0, question)));
    out.whenTrue  = std::string(property::Trim(StripQuotes(property::Trim(text.substr(
        question + 1,
        colon - question - 1
    )))));
    out.whenFalse = std::string(property::Trim(StripQuotes(property::Trim(text.substr(colon + 1)))));
    return !out.condition.empty() && !out.whenTrue.empty() && !out.whenFalse.empty();
}

void TickTree(SilicaWidget& root, float deltaTime) {
    root.Tick(deltaTime);
    if (auto* container = dynamic_cast<SilicaContainer*>(&root)) {
        for (auto const& child : container->Children()) {
            if (child) TickTree(*child, deltaTime);
        }
    }
}

} // namespace silica
