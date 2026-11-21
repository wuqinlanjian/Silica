#pragma once

#include "Style.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace silica {

class SilicaWidget;

// CSS-style timing curves; t is the raw progress in 0..1.
enum class Easing {
    Linear,
    EaseOutQuad,
    EaseInOutCubic,
    EaseInOutSine,
};

float Ease(Easing easing, float t);
bool  ParseEasing(std::string_view name, Easing& easing);

// "0.3s", "300ms" or a bare number of seconds.
std::optional<float> ParseSeconds(std::string_view text);

Color LerpColor(Color const& from, Color const& to, float t);

// One `50% { opacity: .6; transform: scale(.95); }` block.
struct KeyframeStep {
    float                              offset = 0.f; // 0..1
    std::map<std::string, std::string> props;
};

struct KeyframeSet {
    std::string               name;
    std::vector<KeyframeStep> steps;

    [[nodiscard]] bool empty() const { return steps.empty(); }
};

// The compiler publishes a document's @keyframes before the widgets are built;
// an `animation: name duration infinite` property resolves against this table.
void                       RegisterKeyframes(std::map<std::string, KeyframeSet> sets);
std::optional<KeyframeSet> FindKeyframes(std::string_view name);

// "background 0.3s ease-out"
struct TransitionSpec {
    float  duration = .3f; // seconds
    Easing easing   = Easing::Linear;
};
bool ParseTransitionSpec(std::string_view text, std::string& prop, TransitionSpec& spec);

// "@checked ? \"#6CC349\" : \"#33363C\""
struct ConditionalValue {
    std::string condition;
    std::string whenTrue;
    std::string whenFalse;
};
bool ParseConditional(std::string_view text, ConditionalValue& out);

// Per-property animation state kept on the widget.
struct ColorTrack {
    Color  from{kTransparent};
    Color  target{kTransparent};
    Color  current{kTransparent};
    float  elapsed  = 0.f;
    float  duration = 0.f;
    Easing easing   = Easing::Linear;
    bool   used     = false;
    bool   running  = false;
};

struct FloatTrack {
    float  from     = 0.f;
    float  target   = 0.f;
    float  current  = 0.f;
    float  elapsed  = 0.f;
    float  duration = 0.f;
    Easing easing   = Easing::Linear;
    bool   used     = false;
    bool   running  = false;
};

struct DynamicRule {
    std::string prop;
    std::string condition;
    std::string whenTrue;
    std::string whenFalse;
};

// Walks the tree depth-first and calls Tick(deltaTime) on every widget,
// including the children of a ScrollView.
void TickTree(SilicaWidget& root, float deltaTime);

} // namespace silica
