#include "silica/Panel.h"

namespace silica {

void SilicaPanel::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    PaintSelf(context);
}

} // namespace silica
