#pragma once

#include "Panel.h"

#include <functional>
#include <string>

namespace silica {

class SilicaButton : public SilicaPanel {
public:
    SilicaButton();

    std::string           text;
    float                 fontSize = 1.f;
    std::function<void()> onClick;

    // The input layer calls this on a completed click; the widget itself never
    // reaches into the game's input.
    void Click();

    bool Focusable() const override { return true; }
    bool OnMouseDown(float x, float y) override;
    bool OnMouseUp(float x, float y, bool inside) override;
    bool OnActivate() override;

    void Render(MinecraftUIRenderContext& context) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;
};

} // namespace silica
