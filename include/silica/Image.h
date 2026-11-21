#pragma once

#include "Widget.h"

#include <string>

namespace silica {

// Draws a texture. `src` takes either the path form the game uses internally
// ("textures/items/diamond") or a reference to an image file on disk:
// "file:ui/logo.png", "D:/art/logo.png" or "ui/logo.png" (any .png/.tga path is
// treated as a file, relative ones resolve against the mod directory). Silica
// copies such files into a generated resource pack and draws them from there,
// so the game's pack stack is never hand-edited. The whole texture is stretched
// into the box, so item icons and simple GUI art both work. A missing texture
// paints a dim placeholder and is reported once, so a typo is visible instead
// of silent.
class SilicaImage : public SilicaWidget {
public:
    SilicaImage();

    std::string src;

    void Update() override;
    void Render(MinecraftUIRenderContext& context) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;

private:
    bool m_missingLogged = false;
};

} // namespace silica
