#include "silica/Widget.h"

#include "silica/Input.h"
#include "silica/Part.h"
#include "silica/Property.h"
#include "silica/Renderer.h"
#include "silica/Script.h"

#include <algorithm>
#include <cmath>

namespace silica {
namespace {

bool SameColor(Color const& a, Color const& b) {
    return std::fabs(a.r - b.r) < 1e-4f && std::fabs(a.g - b.g) < 1e-4f
        && std::fabs(a.b - b.b) < 1e-4f && std::fabs(a.a - b.a) < 1e-4f;
}

float KeyframeScale(KeyframeStep const& step) {
    if (auto const it = step.props.find("scale"); it != step.props.end()) {
        if (auto number = property::ParseNumber(it->second)) return std::max(0.f, *number);
    }
    auto const it = step.props.find("transform");
    if (it == step.props.end()) return 1.f;
    std::string const& text  = it->second;
    size_t const       open  = text.find('(');
    size_t const       close = text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) return 1.f;
    if (property::ToLower(property::Trim(std::string_view(text).substr(0, open))) != "scale") return 1.f;
    if (auto number = property::ParseNumber(std::string_view(text).substr(open + 1, close - open - 1))) {
        return std::max(0.f, *number);
    }
    return 1.f;
}

TransitionSpec const* FindTransition(
    std::map<std::string, TransitionSpec> const& transitions,
    std::string const&                          key
) {
    if (auto const it = transitions.find(key); it != transitions.end()) return &it->second;
    if (auto const it = transitions.find("all"); it != transitions.end()) return &it->second;
    return nullptr;
}

std::string NormalizeTransitionProp(std::string prop) {
    if (prop == "background-color" || prop == "bg") return "background";
    if (prop == "text-color" || prop == "foreground") return "color";
    return prop;
}

} // namespace

bool SilicaWidget::AddChild(std::shared_ptr<SilicaWidget> child) {
    auto part = std::dynamic_pointer_cast<SilicaPart>(child);
    if (!part) return false;
    part->parent = this;
    parts.push_back(std::move(child));
    return true;
}

bool SilicaWidget::HasState(std::string_view name) const {
    if (name == "hovered" || name == "hover") return hovered;
    if (name == "pressed" || name == "active") return pressed;
    if (name == "focused" || name == "focus") return focused;
    if (name == "visible") return visible;
    if (name == "disabled") return IsDisabled();
    return false;
}

bool SilicaWidget::IsDisabled() const {
    for (SilicaWidget const* current = this; current; current = current->parent) {
        if (current->disabled) return true;
    }
    return false;
}

void SilicaWidget::PaintParts(MinecraftUIRenderContext& context, bool under) {
    for (auto const& entry : parts) {
        auto part = std::dynamic_pointer_cast<SilicaPart>(entry);
        if (!part || part->under != under) continue;
        if (!part->Place(*this)) continue;
        // Parts are decor: they follow the owner's input state instead of
        // receiving pointer events of their own.
        part->hovered = hovered;
        part->pressed = pressed;
        part->focused = focused;
        part->Render(context);
    }
}

bool SilicaWidget::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));

    // State-driven values: `background: @checked ? "#6CC349" : "#33363C";`
    // The rule is re-evaluated every Tick, so the style follows the state even
    // when nothing re-applies the property.
    ConditionalValue conditional;
    if (ParseConditional(value, conditional) && !conditional.condition.empty()
        && (conditional.condition.front() == '@' || conditional.condition.front() == '!')) {
        DynamicRule rule{k, conditional.condition, conditional.whenTrue, conditional.whenFalse};
        auto const  it = std::find_if(m_dynamic.begin(), m_dynamic.end(), [&](DynamicRule const& entry) {
            return entry.prop == k;
        });
        if (it == m_dynamic.end()) m_dynamic.push_back(std::move(rule));
        else *it = std::move(rule);

        std::string const& picked = EvalCondition(conditional.condition) ? conditional.whenTrue
                                                                         : conditional.whenFalse;
        return ApplyProperty(k, picked);
    }

    auto const lengthInto = [&](float& target) {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        target = *number;
        return true;
    };

    if (k == "name" || k == "id") {
        name = std::string(property::Trim(value));
        return true;
    }
    if (k == "background" || k == "background-color" || k == "bg") {
        if (auto gradient = ParseGradient(value)) {
            style.gradient = std::move(*gradient);
            return true;
        }
        auto color = property::ParseColor(value);
        if (!color) return false;
        style.gradient.reset();
        if (m_inDynamicEval) AnimateColor("background", *color, style.background);
        else style.background = *color;
        return true;
    }
    if (k == "color" || k == "text-color" || k == "foreground") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        if (m_inDynamicEval) AnimateColor("color", *color, style.textColor);
        else style.textColor = *color;
        return true;
    }
    if (k == "border-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        if (m_inDynamicEval) AnimateColor("border-color", *color, style.borderColor);
        else style.borderColor = *color;
        return true;
    }
    if (k == "border-width") return lengthInto(style.borderWidth);
    if (k == "border") {
        auto border = property::ParseBorder(value);
        if (!border) return false;
        if (border->width) {
            style.borderWidth = std::max(0.f, *border->width);
            if (*border->width <= 0.f) style.borderColor = kTransparent;
        }
        if (border->color) style.borderColor = *border->color;
        return true;
    }
    if (k == "border-radius" || k == "radius" || k == "corner-radius") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        style.radius = *number;
        return true;
    }

    if (k == "font-family" || k == "font-name") {
        style.fontFamily = std::string(property::Trim(value));
        return true;
    }
    if (k == "text-align" || k == "align") {
        std::string const name = property::ToLower(property::Trim(value));
        if (name == "left" || name == "start" || name == "0") style.textAlign = 0;
        else if (name == "center" || name == "middle" || name == "1") style.textAlign = 1;
        else if (name == "right" || name == "end" || name == "2") style.textAlign = 2;
        else return false;
        return true;
    }

    // Gradient: "linear-gradient(to right, #a, #b)", "from to", optionally
    // prefixed/suffixed with horizontal|vertical.
    if (k == "gradient" || k == "background-gradient" || k == "bg-gradient") {
        if (property::ToLower(property::Trim(value)) == "none") {
            style.gradient.reset();
            return true;
        }
        if (auto gradient = ParseGradient(value)) {
            style.gradient = std::move(*gradient);
            return true;
        }
        if (auto color = property::ParseColor(value)) {
            GradientSpec spec;
            spec.from  = *color;
            spec.to    = *color;
            spec.stops = {{0.f, *color}, {1.f, *color}};
            style.gradient = std::move(spec);
            return true;
        }
        return false;
    }

    // Shadow: "dx dy blur [spread] #color"; 1 number means blur only.
    if (k == "shadow" || k == "box-shadow") {
        auto const tokens = property::Split(value);
        if (tokens.size() == 1 && property::ToLower(tokens[0]) == "none") {
            style.shadow.reset();
            return true;
        }
        ShadowSpec          spec;
        std::vector<float>  numbers;
        std::optional<Color> color;
        for (auto const& token : tokens) {
            if (auto number = property::ParseNumber(token)) {
                numbers.push_back(*number);
                continue;
            }
            auto parsed = property::ParseColor(token);
            if (!parsed) return false;
            color = *parsed;
        }
        if (numbers.empty() || numbers.size() > 4) return false;
        switch (numbers.size()) {
        case 1:
            spec.blur = numbers[0];
            break;
        case 2:
            spec.dx   = numbers[0];
            spec.dy   = numbers[1];
            break;
        case 3:
            spec.dx   = numbers[0];
            spec.dy   = numbers[1];
            spec.blur = numbers[2];
            break;
        default:
            spec.dx     = numbers[0];
            spec.dy     = numbers[1];
            spec.blur   = numbers[2];
            spec.spread = numbers[3];
            break;
        }
        if (color) spec.color = *color;
        style.shadow = spec;
        return true;
    }

    // Nine-slice border image. The first token is the texture path, the numbers
    // after it are CSS-style slices: 1 all sides, 2 vertical/horizontal,
    // 3 top/horizontal/bottom, 4 top/right/bottom/left (texture pixels).
    auto const readSlices = [](NineSliceSpec& spec, std::vector<std::string> const& tokens, size_t start) {
        std::vector<float> numbers;
        for (size_t i = start; i < tokens.size(); ++i) {
            auto number = property::ParseNumber(tokens[i]);
            if (!number || *number < 0.f) return false;
            numbers.push_back(*number);
        }
        if (numbers.empty() || numbers.size() > 4) return false;

        float top = 0.f, right = 0.f, bottom = 0.f, left = 0.f;
        switch (numbers.size()) {
        case 1:
            top = right = bottom = left = numbers[0];
            break;
        case 2:
            top = bottom = numbers[0];
            right = left = numbers[1];
            break;
        case 3:
            top    = numbers[0];
            right  = numbers[1];
            left   = numbers[1];
            bottom = numbers[2];
            break;
        default:
            top    = numbers[0];
            right  = numbers[1];
            bottom = numbers[2];
            left   = numbers[3];
            break;
        }
        spec.top    = top;
        spec.right  = right;
        spec.bottom = bottom;
        spec.left   = left;
        return true;
    };

    if (k == "border-image" || k == "nine-slice" || k == "border-image-source") {
        auto const tokens = property::Split(value);
        if (tokens.empty()) return false;
        if (tokens.size() == 1 && property::ToLower(tokens[0]) == "none") {
            style.borderImage.reset();
            return true;
        }
        NineSliceSpec spec = style.borderImage.value_or(NineSliceSpec{});
        size_t        start = 0;
        if (!property::ParseNumber(tokens[0])) {
            spec.src = tokens[0];
            start    = 1;
        }
        if (start < tokens.size() && !readSlices(spec, tokens, start)) return false;
        if (spec.src.empty()) return false;
        style.borderImage = spec;
        return true;
    }
    if (k == "border-image-slice" || k == "nine-slice-slice") {
        auto const     tokens = property::Split(value);
        NineSliceSpec  spec   = style.borderImage.value_or(NineSliceSpec{});
        if (!readSlices(spec, tokens, 0)) return false;
        style.borderImage = spec;
        return true;
    }

    // State colors: "none"/"transparent" clears an earlier value so a shared
    // style can be overridden back to the default behavior.
    auto const stateColorInto = [&](std::optional<Color>& target) {
        std::string const name = property::ToLower(property::Trim(value));
        if (name == "none") {
            target.reset();
            return true;
        }
        auto color = property::ParseColor(value);
        if (!color) return false;
        target = *color;
        return true;
    };
    if (k == "hover-background" || k == "hover-bg") return stateColorInto(style.hoverBackground);
    if (k == "pressed-background" || k == "press-background" || k == "active-background" || k == "pressed-bg") {
        return stateColorInto(style.pressedBackground);
    }
    if (k == "disabled-background" || k == "disabled-bg") return stateColorInto(style.disabledBackground);
    if (k == "disabled-opacity" || k == "disabled-alpha") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        style.disabledOpacity = std::clamp(*number, 0.f, 1.f);
        return true;
    }
    if (k == "opacity") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        float const target = std::clamp(*number, 0.f, 1.f);
        m_opacityManaged   = true;
        if (m_inDynamicEval) AnimateOpacity(target);
        else {
            m_baseOpacity = target;
            RefreshOpacity();
        }
        return true;
    }

    // transition: background 0.3s ease-out[, color 0.2s linear]
    if (k == "transition") {
        std::string const list(value);
        size_t            start = 0;
        bool              ok    = true;
        while (start <= list.size()) {
            size_t const comma = list.find(',', start);
            std::string const segment =
                list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            std::string    prop;
            TransitionSpec spec;
            if (ParseTransitionSpec(segment, prop, spec)) {
                m_transitions[NormalizeTransitionProp(std::move(prop))] = spec;
            } else if (!property::Trim(segment).empty()) {
                ok = false;
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return ok;
    }

    // animation: pulse 1.5s infinite|3
    if (k == "animation" || k == "animation-name") {
        auto const tokens = property::Split(value);
        if (tokens.empty()) return false;
        std::string const first = property::ToLower(tokens[0]);
        if (first == "none") {
            m_animationName.clear();
            return true;
        }
        m_animationName       = tokens[0];
        m_animationDuration   = 1.f;
        m_animationIterations = 1;
        m_animationTime       = 0.f;
        m_animationDone       = false;
        m_keyframes           = {};
        for (size_t i = 1; i < tokens.size(); ++i) {
            std::string const word = property::ToLower(tokens[i]);
            if (word == "infinite") {
                m_animationIterations = -1;
                continue;
            }
            if (auto seconds = ParseSeconds(tokens[i])) {
                m_animationDuration = std::max(.0001f, *seconds);
                continue;
            }
            if (auto count = property::ParseNumber(tokens[i])) {
                m_animationIterations = std::max(1, static_cast<int>(*count));
            }
        }
        if (auto found = FindKeyframes(m_animationName)) m_keyframes = std::move(*found);
        return true;
    }

    if (k == "z-order" || k == "z" || k == "layer" || k == "z-index") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        zOrder = static_cast<int>(*number);
        return true;
    }

    if (k == "width") return lengthInto(width);
    if (k == "height") return lengthInto(height);
    if (k == "margin") return lengthInto(margin);
    if (k == "margin-top") return lengthInto(marginTop);
    if (k == "margin-right") return lengthInto(marginRight);
    if (k == "margin-bottom") return lengthInto(marginBottom);
    if (k == "margin-left") return lengthInto(marginLeft);
    if (k == "padding") return lengthInto(padding);

    if (k == "x") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        x = *number;
        return true;
    }
    if (k == "y") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        y = *number;
        return true;
    }
    if (k == "visible") {
        auto shown = property::ParseBool(value);
        if (!shown) return false;
        visible = *shown;
        return true;
    }
    if (k == "disabled") {
        auto flag = property::ParseBool(value);
        if (!flag) return false;
        disabled = *flag;
        if (disabled) {
            hovered = false;
            pressed = false;
        }
        return true;
    }
    if (k == "enabled") {
        auto flag = property::ParseBool(value);
        if (!flag) return false;
        disabled = !*flag;
        if (disabled) {
            hovered = false;
            pressed = false;
        }
        return true;
    }
    return false;
}

Variant SilicaWidget::ResolveValue(std::string_view name) const {
    // The event payload is more specific than any data key of the same name.
    if (name.rfind("event.", 0) == 0) {
        return m_eventScope ? m_eventScope->Field(name.substr(6)) : Variant{};
    }

    // `@item.name` resolves `item` in the injected scope, then walks the path.
    // A name without a path keeps the original order: scope, store, state.
    size_t const      dot  = name.find('.');
    std::string_view  root = dot == std::string_view::npos ? name : name.substr(0, dot);
    Variant           value;
    if (ScopeHas(root)) {
        value = m_scope->Get(root);
    } else if (DataStore::Has(root)) {
        value = DataStore::Get(root);
    } else {
        return dot == std::string_view::npos ? Variant{HasState(name)} : Variant{};
    }
    if (dot == std::string_view::npos) return value;

    size_t start = dot + 1;
    while (start <= name.size()) {
        size_t const      next    = name.find('.', start);
        std::string_view  segment = name.substr(
            start,
            next == std::string_view::npos ? std::string_view::npos : next - start
        );
        if (segment.empty()) return Variant{};
        if (value.IsArray()) {
            std::size_t index   = 0;
            bool        numeric = true;
            for (char const c : segment) {
                if (c < '0' || c > '9') {
                    numeric = false;
                    break;
                }
                index = index * 10 + static_cast<std::size_t>(c - '0');
            }
            value = numeric ? value.Index(index) : Variant{};
        } else {
            value = value.Field(segment);
        }
        if (next == std::string_view::npos) break;
        start = next + 1;
    }
    return value;
}

void SilicaWidget::SetDataContext(std::shared_ptr<SilDataContext const> context) {
    m_scope = std::move(context);
}

bool SilicaWidget::ScopeHas(std::string_view name) const { return m_scope && m_scope->Has(name); }

namespace {

// `onClick`, `on-click` and `ONCLICK` all compact to "onclick".
std::string CompactKey(std::string_view key) {
    std::string normalized = property::ToLower(property::Trim(key));
    normalized.erase(std::remove(normalized.begin(), normalized.end(), '-'), normalized.end());
    return normalized;
}

// The event name inside an `on...` key; empty when the key is not an event.
std::string EventName(std::string_view compactKey) {
    if (compactKey == "action") return "click";
    if (compactKey.size() > 2 && compactKey.rfind("on", 0) == 0) {
        return std::string(compactKey.substr(2));
    }
    return {};
}

} // namespace

void SilicaWidget::WatchKeys(std::vector<std::string> const& keys) {
    std::shared_ptr<DataObserver> self;
    try {
        self = shared_from_this();
    } catch (...) {
        // A stack-allocated widget cannot outlive a subscription; bindings
        // still apply once, they just never receive notifications.
        return;
    }
    for (auto const& key : keys) {
        if (key.empty() || !m_watchedKeys.insert(key).second) continue;
        DataStore::Subscribe(key, self);
    }
}

bool SilicaWidget::BindProperty(std::string_view key, std::string_view expression, std::string& error) {
    auto parsed = Expression::Parse(expression, error);
    if (!parsed) return false;

    BoundProperty binding;
    binding.property   = property::ToLower(property::Trim(key));
    binding.expression = std::move(*parsed);
    binding.keys       = binding.expression.Identifiers();

    // Scope names are fixed for the lifetime of the widget (a ForEach rebuild
    // replaces the widget), so they need neither a subscription nor polling.
    // A key missing from the store may be widget state (`@checked`) or data
    // that arrives later; polling Tick covers both until a Set notifies us.
    std::vector<std::string> watched;
    for (auto const& name : binding.keys) {
        if (ScopeHas(name)) continue;
        watched.push_back(name);
        if (!DataStore::Has(name)) binding.poll = true;
    }

    ApplyBinding(binding, false);

    m_polledBindings |= binding.poll;
    m_bindings.push_back(std::move(binding));
    WatchKeys(watched);
    return true;
}

bool SilicaWidget::BindTemplate(std::string_view key, std::string_view source, std::string& error) {
    error.clear();

    BoundTemplate binding;
    binding.property = property::ToLower(property::Trim(key));

    auto const addKeys = [&binding](std::vector<std::string> const& keys) {
        for (auto const& name : keys) {
            if (std::find(binding.keys.begin(), binding.keys.end(), name) == binding.keys.end()) {
                binding.keys.push_back(name);
            }
        }
    };

    size_t pos = 0;
    while (pos < source.size()) {
        size_t const open = source.find("{{", pos);
        if (open == std::string_view::npos) {
            BoundTemplate::Segment literal;
            literal.literal = std::string(source.substr(pos));
            binding.segments.push_back(std::move(literal));
            break;
        }
        if (open > pos) {
            BoundTemplate::Segment literal;
            literal.literal = std::string(source.substr(pos, open - pos));
            binding.segments.push_back(std::move(literal));
        }

        size_t const close = source.find("}}", open + 2);
        if (close == std::string_view::npos) {
            error = "unterminated '{{' in a template";
            return false;
        }
        std::string_view const inner = property::Trim(source.substr(open + 2, close - open - 2));
        if (inner.empty()) {
            error = "empty expression inside '{{ }}'";
            return false;
        }

        std::string segmentError;
        auto        parsed = Expression::Parse(inner, segmentError);
        if (!parsed) {
            error = "in '{{ " + std::string(inner) + " }}': " + segmentError;
            return false;
        }
        addKeys(parsed->Identifiers());

        BoundTemplate::Segment segment;
        segment.expression = true;
        segment.value      = std::move(*parsed);
        binding.segments.push_back(std::move(segment));
        pos = close + 2;
    }

    if (binding.segments.empty()) {
        error = "the template is empty";
        return false;
    }

    std::vector<std::string> watched;
    for (auto const& name : binding.keys) {
        if (ScopeHas(name)) continue;
        watched.push_back(name);
        if (!DataStore::Has(name)) binding.poll = true;
    }

    ApplyTemplate(binding, false);

    m_polledBindings |= binding.poll;
    m_templates.push_back(std::move(binding));
    WatchKeys(watched);
    return true;
}

void SilicaWidget::ApplyTemplate(BoundTemplate const& binding, bool animate) {
    std::string text;
    bool        valueMissing = false;
    for (auto const& segment : binding.segments) {
        if (!segment.expression) {
            text += segment.literal;
            continue;
        }
        Variant const value =
            segment.value.Evaluate([this](std::string_view name) { return ResolveValue(name); });
        if (value.IsNull()) valueMissing = true;
        else text += value.ToString();
    }
    // A template that is only a missing key keeps the widget default, exactly
    // like a plain binding; mixed text still renders its literal parts.
    if (valueMissing && binding.segments.size() == 1) return;

    bool const previous = m_inDynamicEval;
    m_inDynamicEval     = animate;
    ApplyProperty(binding.property, text);
    m_inDynamicEval = previous;
}

void SilicaWidget::ApplyBinding(BoundProperty const& binding, bool animate) {
    Variant value =
        binding.expression.Evaluate([this](std::string_view name) { return ResolveValue(name); });
    if (value.IsNull()) return; // keep the current value until data arrives

    bool const previous = m_inDynamicEval;
    m_inDynamicEval     = animate;
    ApplyProperty(binding.property, value.ToPropertyString());
    m_inDynamicEval = previous;
}

void SilicaWidget::EvaluateBindings(std::string_view onlyKey, bool animate) {
    for (auto const& binding : m_bindings) {
        if (!onlyKey.empty()) {
            bool matches = false;
            for (auto const& key : binding.keys) {
                if (key == onlyKey) {
                    matches = true;
                    break;
                }
            }
            if (!matches) continue;
        }
        ApplyBinding(binding, animate);
    }
}

void SilicaWidget::EvaluateTemplates(std::string_view onlyKey, bool animate) {
    for (auto const& binding : m_templates) {
        if (!onlyKey.empty()) {
            bool matches = false;
            for (auto const& key : binding.keys) {
                if (key == onlyKey) {
                    matches = true;
                    break;
                }
            }
            if (!matches) continue;
        }
        ApplyTemplate(binding, animate);
    }
}

void SilicaWidget::EvaluatePolledBindings() {
    for (auto const& binding : m_bindings) {
        if (binding.poll) ApplyBinding(binding, true);
    }
    for (auto const& binding : m_templates) {
        if (binding.poll) ApplyTemplate(binding, true);
    }
}

void SilicaWidget::OnDataChanged(std::string_view key) {
    if (m_bindings.empty() && m_templates.empty()) return;
    EvaluateBindings(key, true);
    EvaluateTemplates(key, true);
}

bool SilicaWidget::SetScript(std::string_view key, std::string_view source, std::string& error) {
    std::string const event = EventName(CompactKey(key));
    if (event.empty()) {
        error = "scripts attach to an event property (onClick, onChange, onSubmit, ...)";
        return false;
    }

    auto script = Script::Parse(source, error);
    if (!script) return false;
    m_scripts[event] = std::move(script);
    return true;
}

bool SilicaWidget::HasScript(std::string_view event) const {
    std::string const compact = CompactKey(event);
    std::string const name    = EventName(compact);
    return m_scripts.find(name.empty() ? compact : name) != m_scripts.end();
}

bool SilicaWidget::FireScriptEvent(std::string_view event, ScriptEvent const& payload) {
    std::string const compact = CompactKey(event);
    std::string const name    = EventName(compact);
    auto const        it      = m_scripts.find(name.empty() ? compact : name);
    if (it == m_scripts.end()) return false;

    m_eventScope = &payload;
    it->second->Run([this](std::string_view key) { return ResolveValue(key); });
    m_eventScope = nullptr;
    return true;
}

bool SilicaWidget::RunClickScript() {
    ScriptEvent click;
    click.name = "click";
    return FireScriptEvent("click", click);
}

bool SilicaWidget::WriteBackBinding(std::string_view property, Variant const& value) {
    std::string const want = property::ToLower(property::Trim(property));
    for (auto const& binding : m_bindings) {
        if (binding.property != want) continue;
        auto key = binding.expression.PlainIdentifier();
        if (!key) return false;
        DataStore::Set(std::move(*key), value);
        return true;
    }
    return false;
}

Color SilicaWidget::StateFill() const {
    if (IsDisabled() && style.disabledBackground) return *style.disabledBackground;
    if (pressed && style.pressedBackground) return *style.pressedBackground;
    if (hovered && style.hoverBackground) return *style.hoverBackground;
    return style.background;
}

void SilicaWidget::Tick(float deltaTime) {
    deltaTime = std::clamp(deltaTime, 0.f, .25f);
    if (m_polledBindings) EvaluatePolledBindings();
    if (!m_dynamic.empty()) EvaluateDynamic();
    AdvanceTransitions(deltaTime);
    AdvanceKeyframes(deltaTime);

    // A widget can be disabled while it owns the keyboard (a bound flag or a
    // script); releasing the focus here stops keys from reaching it.
    if (focused && IsDisabled()) FocusManager::Get().Clear();
}

bool SilicaWidget::EvalCondition(std::string_view condition) const {
    std::string_view text = property::Trim(condition);
    bool             negate = false;
    if (!text.empty() && text.front() == '!') {
        negate = true;
        text.remove_prefix(1);
    }
    if (!text.empty() && text.front() == '@') text.remove_prefix(1);
    text = property::Trim(text);
    bool const result = HasState(text);
    return negate ? !result : result;
}

void SilicaWidget::EvaluateDynamic() {
    m_inDynamicEval = true;
    for (auto const& rule : m_dynamic) {
        ApplyProperty(rule.prop, EvalCondition(rule.condition) ? rule.whenTrue : rule.whenFalse);
    }
    m_inDynamicEval = false;
}

void SilicaWidget::AnimateColor(std::string const& key, Color const& target, Color& slot) {
    TransitionSpec const* spec = FindTransition(m_transitions, key);
    if (!spec || spec->duration <= 0.f) {
        m_colorTracks.erase(key);
        slot = target;
        return;
    }

    ColorTrack& track = m_colorTracks[key];
    if (!track.used) {
        track.used    = true;
        track.current = slot;
        track.from    = slot;
        track.target  = target;
        track.running = false;
        if (!SameColor(slot, target)) {
            track.elapsed  = 0.f;
            track.duration = spec->duration;
            track.easing   = spec->easing;
            track.running  = true;
        }
        return;
    }
    if (SameColor(track.target, target)) {
        slot = track.current;
        return;
    }

    track.from     = track.current;
    track.target   = target;
    track.elapsed  = 0.f;
    track.duration = spec->duration;
    track.easing   = spec->easing;
    track.running  = true;
}

void SilicaWidget::AnimateOpacity(float target) {
    TransitionSpec const* spec = FindTransition(m_transitions, "opacity");
    FloatTrack&           track = m_opacityTrack;

    if (!spec || spec->duration <= 0.f) {
        track         = {};
        m_baseOpacity = target;
        RefreshOpacity();
        return;
    }
    if (!track.used) {
        track.used    = true;
        track.current = target;
        track.from    = target;
        track.target  = target;
        track.running = false;
        m_baseOpacity = target;
        RefreshOpacity();
        return;
    }
    if (std::fabs(track.target - target) < 1e-5f) return;

    track.from     = track.current;
    track.target   = target;
    track.elapsed  = 0.f;
    track.duration = spec->duration;
    track.easing   = spec->easing;
    track.running  = true;
}

void SilicaWidget::RefreshOpacity() {
    if (!m_opacityManaged) return;
    style.opacity = std::clamp(m_baseOpacity * m_keyframeOpacity, 0.f, 1.f);
}

void SilicaWidget::AdvanceTransitions(float deltaTime) {
    for (auto& [key, track] : m_colorTracks) {
        if (!track.running) continue;
        track.elapsed += deltaTime;
        float t = track.duration > 0.f ? track.elapsed / track.duration : 1.f;
        if (t >= 1.f) {
            t             = 1.f;
            track.running = false;
        }
        track.current = LerpColor(track.from, track.target, Ease(track.easing, t));

        Color* slot = nullptr;
        if (key == "background") slot = &style.background;
        else if (key == "color") slot = &style.textColor;
        else if (key == "border-color") slot = &style.borderColor;
        if (slot) *slot = track.current;
    }

    if (m_opacityTrack.running) {
        FloatTrack& track = m_opacityTrack;
        track.elapsed += deltaTime;
        float t = track.duration > 0.f ? track.elapsed / track.duration : 1.f;
        if (t >= 1.f) {
            t             = 1.f;
            track.running = false;
        }
        track.current = track.from + (track.target - track.from) * Ease(track.easing, t);
        m_baseOpacity = track.current;
        RefreshOpacity();
    }
}

void SilicaWidget::AdvanceKeyframes(float deltaTime) {
    if (m_animationName.empty()) return;
    if (m_keyframes.empty()) {
        if (auto found = FindKeyframes(m_animationName)) m_keyframes = std::move(*found);
        if (m_keyframes.empty()) return;
    }

    m_opacityManaged = true;
    if (m_animationDone) return;

    float const duration = std::max(.0001f, m_animationDuration);
    m_animationTime += deltaTime;
    float local = m_animationTime;
    if (m_animationIterations < 0) {
        local = std::fmod(local, duration);
    } else {
        float const total = duration * static_cast<float>(m_animationIterations);
        if (local >= total) {
            m_animationTime = total;
            m_animationDone = true;
            local           = duration;
        } else {
            local = std::fmod(local, duration);
        }
    }

    SampleKeyframes(std::clamp(local / duration, 0.f, 1.f));
    RefreshOpacity();
}

void SilicaWidget::SampleKeyframes(float progress) {
    auto const& steps = m_keyframes.steps;
    if (steps.empty()) return;

    auto const opacityOf = [](KeyframeStep const& step) {
        auto const it = step.props.find("opacity");
        if (it == step.props.end()) return 1.f;
        auto const number = property::ParseNumber(it->second);
        return number ? std::clamp(*number, 0.f, 1.f) : 1.f;
    };

    float opacity = 1.f;
    float scale   = 1.f;

    size_t next = 0;
    while (next < steps.size() && steps[next].offset < progress) ++next;

    if (next == 0) {
        opacity = opacityOf(steps.front());
        scale   = KeyframeScale(steps.front());
    } else if (next >= steps.size()) {
        opacity = opacityOf(steps.back());
        scale   = KeyframeScale(steps.back());
    } else {
        KeyframeStep const& before = steps[next - 1];
        KeyframeStep const& after  = steps[next];
        float const         span   = after.offset - before.offset;
        float const         u      = span > 0.f ? std::clamp((progress - before.offset) / span, 0.f, 1.f) : 0.f;
        opacity                    = opacityOf(before) + (opacityOf(after) - opacityOf(before)) * u;
        scale                      = KeyframeScale(before) + (KeyframeScale(after) - KeyframeScale(before)) * u;
    }

    m_keyframeOpacity = opacity;
    m_keyframeScale   = scale;
}

void SilicaWidget::PaintSelf(MinecraftUIRenderContext& context) {
    PaintParts(context, true);

    float const alpha = PaintAlpha();
    if (style.shadow) {
        DrawShadow(context, x, y, width, height, style.radius, *style.shadow, alpha);
    }

    bool const hasStateFill = (IsDisabled() && style.disabledBackground)
                           || (pressed && style.pressedBackground) || (hovered && style.hoverBackground);
    if (style.gradient && !hasStateFill) {
        GradientSpec const spec = style.gradient->WithAlpha(alpha);
        FillRoundLinearGradient(context, x, y, width, height, style.radius, spec);
    } else if (style.radius > 0.f) {
        FillRoundRect(context, x, y, width, height, style.radius, StateFill().ScaleAlpha(alpha));
    } else {
        FillRect(context, x, y, width, height, StateFill().ScaleAlpha(alpha));
    }

    bool drewBorderImage = false;
    if (style.borderImage && !style.borderImage->src.empty()) {
        drewBorderImage = DrawNineSlice(
            context,
            style.borderImage->src,
            x,
            y,
            width,
            height,
            *style.borderImage,
            kWhite,
            alpha
        );
    }

    if (!drewBorderImage && style.borderWidth > 0.f) {
        Color const border = style.borderColor.ScaleAlpha(alpha);
        if (style.radius > 0.f) {
            DrawRoundFrame(context, x, y, width, height, style.radius, border, style.borderWidth);
        } else {
            DrawFrame(context, x, y, width, height, border, style.borderWidth);
        }
    }

    PaintParts(context, false);
}

} // namespace silica
