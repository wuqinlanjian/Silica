#pragma once

#include "Widget.h"

#include <string>

namespace silica {

class SilicaLabel : public SilicaWidget {
public:
    SilicaLabel();

    std::string text;
    float       fontSize = 1.f;
    // Opt-in multi-line text. Without it a label stays a single truncated line,
    // which keeps existing layouts and heights unchanged.
    bool        wrap     = false;

    void Update() override;
    void Render(MinecraftUIRenderContext& context) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;
};

} // namespace silica
