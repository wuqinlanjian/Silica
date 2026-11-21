#include "silica/Button.h"

#include "silica/Handlers.h"
#include "silica/Property.h"
#include "silica/Renderer.h"

namespace silica {

SilicaButton::SilicaButton() {
    width  = 120.f;
    height = 20.f;
    style  = UIStyle::PrimaryButton();
}

void SilicaButton::Click() {
    if (!visible) return;
    // A `{ ... }` script from .sil wins over the C++/handler-name hook.
    if (RunClickScript()) return;
    if (onClick) onClick();
}

bool SilicaButton::OnMouseDown(float x, float y) {
    (void)x;
    (void)y;
    return visible;
}

bool SilicaButton::OnMouseUp(float x, float y, bool inside) {
    (void)x;
    (void)y;
    if (inside) Click();
    return true;
}

bool SilicaButton::OnActivate() {
    Click();
    return true;
}

bool SilicaButton::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));
    if (k == "text" || k == "value") {
        text = std::string(value);
        return true;
    }
    if (k == "font-size" || k == "font") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        fontSize = *number;
        return true;
    }
    if (k == "onclick" || k == "on-click" || k == "action") {
        std::string handler = std::string(property::Trim(value));
        if (handler.empty()) {
            onClick = {};
        } else {
            onClick = [name = std::move(handler)] { SilicaHandlers::Invoke(name); };
        }
        return true;
    }
    return SilicaPanel::ApplyProperty(key, value);
}

void SilicaButton::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    SilicaPanel::Render(context);
    auto const overlay = [&](Color color) {
        if (style.radius > 0.f) FillRoundRect(context, x, y, width, height, style.radius, color);
        else FillRect(context, x, y, width, height, color);
    };
    if (hovered && !style.hoverBackground) {
        overlay(Color{1.f, 1.f, 1.f, .10f * PaintAlpha()});
    }
    if (pressed && !style.pressedBackground) {
        overlay(Color{0.f, 0.f, 0.f, .18f * PaintAlpha()});
    }
    if (text.empty()) return;
    // The 14-unit line box is centered in the button; DrawText centers the text
    // inside that box.
    float const lineY = y + (height - 14.f * fontSize) * .5f;
    DrawText(
        context,
        x,
        lineY,
        width,
        text,
        style.textColor.ScaleAlpha(PaintAlpha()),
        fontSize,
        style.Align(::ui::TextAlignment::Center),
        style.fontFamily
    );
    FlushText(context);
}

} // namespace silica
