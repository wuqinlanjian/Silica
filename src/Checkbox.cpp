#include "silica/Checkbox.h"

#include <windows.h>
#undef DrawText // windows.h maps the name onto DrawTextW

#include "silica/Handlers.h"
#include "silica/Property.h"
#include "silica/Renderer.h"
#include "silica/Script.h"

#include <algorithm>

namespace silica {

SilicaCheckbox::SilicaCheckbox() {
    height             = 18.f;
    style.background   = Color{.16f, .17f, .19f, .90f};
    style.borderColor  = Color{.45f, .47f, .50f, 1.f};
    style.borderWidth  = 1.f;
    style.textColor    = Color{.95f, .955f, .965f, 1.f};
}

float SilicaCheckbox::boxSize() const {
    if (boxSizeOverride > 0.f) return std::max(4.f, boxSizeOverride);
    return std::max(14.f, 14.f * fontSize);
}

void SilicaCheckbox::Update() {
    if (height <= 0.f) height = boxSize() + 4.f;
}

void SilicaCheckbox::SetChecked(bool value, bool notifyChange) {
    checked = value;
    if (notifyChange) notify();
}

void SilicaCheckbox::Toggle() { SetChecked(!checked); }

void SilicaCheckbox::notify() {
    WriteBackBinding("checked", Variant{checked});

    ScriptEvent event;
    event.name    = "change";
    event.value   = Variant{checked};
    event.text    = Variant{text};
    event.checked = Variant{checked};
    if (FireScriptEvent("change", event)) return;

    if (onChange) onChange(checked);
    if (!m_handler.empty()) SilicaHandlers::Invoke(m_handler, checked ? "true" : "false");
}

bool SilicaCheckbox::OnMouseDown(float, float) { return visible; }

bool SilicaCheckbox::OnMouseUp(float, float, bool inside) {
    if (inside) Toggle();
    return true;
}

bool SilicaCheckbox::OnActivate() {
    Toggle();
    return true;
}

bool SilicaCheckbox::OnKeyDown(int keyCode) {
    if (keyCode == VK_SPACE) {
        Toggle();
        return true;
    }
    return false;
}

bool SilicaCheckbox::HasState(std::string_view name) const {
    if (name == "checked" || name == "on" || name == "selected") return checked;
    if (name == "unchecked" || name == "off") return !checked;
    return SilicaWidget::HasState(name);
}

void SilicaCheckbox::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    Update();

    PaintParts(context, true);

    float const box = boxSize();
    float const top = y + (height - box) * .5f;

    if (showBox) {
        bool const hasStateFill = (pressed && style.pressedBackground) || (hovered && style.hoverBackground);
        if (style.gradient && !hasStateFill) {
            FillRoundGradientRect(
                context,
                x,
                top,
                box,
                box,
                style.radius,
                style.gradient->from.ScaleAlpha(PaintAlpha()),
                style.gradient->to.ScaleAlpha(PaintAlpha()),
                style.gradient->horizontal
            );
        } else if (style.radius > 0.f) {
            FillRoundRect(context, x, top, box, box, style.radius, StateFill().ScaleAlpha(PaintAlpha()));
        } else {
            FillRect(context, x, top, box, box, StateFill().ScaleAlpha(PaintAlpha()));
        }
        if (style.borderWidth > 0.f) {
            float const border = std::max(1.f, style.borderWidth);
            if (style.radius > 0.f) {
                DrawRoundFrame(context, x, top, box, box, style.radius, style.borderColor.ScaleAlpha(PaintAlpha()), border);
            } else {
                DrawFrame(context, x, top, box, box, style.borderColor.ScaleAlpha(PaintAlpha()), border);
            }
        }
        if (checked) {
            float const inset = checkInset >= 0.f ? std::min(checkInset, box * .5f) : std::max(3.f, box * .22f);
            float const mark  = std::max(0.f, box - 2.f * inset);
            if (style.radius > 0.f) {
                FillRoundRect(
                    context,
                    x + inset,
                    top + inset,
                    mark,
                    mark,
                    std::max(0.f, style.radius - inset),
                    accent.ScaleAlpha(PaintAlpha())
                );
            } else {
                FillRect(context, x + inset, top + inset, mark, mark, accent.ScaleAlpha(PaintAlpha()));
            }
        }
        if (hovered && !style.hoverBackground) {
            Color const glow = Color{1.f, 1.f, 1.f, .10f * PaintAlpha()};
            if (style.radius > 0.f) FillRoundRect(context, x, top, box, box, style.radius, glow);
            else FillRect(context, x, top, box, box, glow);
        }
    }

    if (!text.empty()) {
        float const font   = std::max(.5f, fontSize);
        float const lineY  = y + (height - 14.f * font) * .5f;
        float const textX  = x + box + textGap;
        float const textW  = std::max(0.f, width - box - textGap);
        DrawText(
            context,
            textX,
            lineY,
            textW,
            text,
            style.textColor.ScaleAlpha(PaintAlpha()),
            font,
            style.Align(::ui::TextAlignment::Left),
            style.fontFamily
        );
        FlushText(context);
    }

    PaintParts(context, false);
}

bool SilicaCheckbox::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));
    if (k == "text" || k == "label") {
        text = std::string(value);
        return true;
    }
    if (k == "checked" || k == "value") {
        auto state = property::ParseBool(value);
        if (!state) return false;
        checked = *state;
        return true;
    }
    if (k == "font-size" || k == "font") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        fontSize = *number;
        return true;
    }
    if (k == "accent" || k == "check-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        accent = *color;
        return true;
    }
    if (k == "box-size" || k == "box" || k == "check-size") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        boxSizeOverride = *number;
        return true;
    }
    if (k == "text-gap" || k == "gap" || k == "label-gap") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        textGap = *number;
        return true;
    }
    if (k == "check-inset" || k == "mark-inset") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        checkInset = *number;
        return true;
    }
    if (k == "show-box" || k == "box-visible") {
        auto shown = property::ParseBool(value);
        if (!shown) return false;
        showBox = *shown;
        return true;
    }
    if (k == "onchange" || k == "on-change") {
        std::string handler = std::string(property::Trim(value));
        if (handler.empty()) {
            onChange = {};
            m_handler.clear();
        } else {
            m_handler = std::move(handler);
        }
        return true;
    }
    return SilicaWidget::ApplyProperty(key, value);
}

} // namespace silica
