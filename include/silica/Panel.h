#pragma once

#include "Widget.h"

namespace silica {

// Fill and border only; use a SilicaContainer subclass once the panel needs
// children.
class SilicaPanel : public SilicaWidget {
public:
    void Render(MinecraftUIRenderContext& context) override;
};

} // namespace silica
