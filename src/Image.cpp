#include "silica/Image.h"

#include "silica/Property.h"
#include "silica/Renderer.h"
#include "silica/Runtime.h"

namespace silica {
namespace {

constexpr float kDefaultSize = 16.f;

} // namespace

SilicaImage::SilicaImage() {
    width  = kDefaultSize;
    height = kDefaultSize;
}

void SilicaImage::Update() {
    if (width <= 0.f) width = kDefaultSize;
    if (height <= 0.f) height = kDefaultSize;
}

void SilicaImage::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    Update();
    PaintSelf(context);

    if (!src.empty()) {
        if (DrawTexture(context, src, x, y, width, height, style.textColor, PaintAlpha())) return;
        if (!m_missingLogged) {
            m_missingLogged = true;
            runtime::Log("silica: image texture not available: " + src);
        }
    }

    // Placeholder: a dim box with an outline, sized like the requested image.
    float const alpha = PaintAlpha() * .55f;
    FillRect(context, x, y, width, height, Color{.16f, .17f, .19f, .45f * alpha});
    DrawFrame(context, x, y, width, height, Color{.55f, .57f, .60f, .8f * alpha});
}

bool SilicaImage::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));
    if (k == "src" || k == "source" || k == "texture" || k == "image") {
        src = std::string(property::Trim(value));
        return true;
    }
    return SilicaWidget::ApplyProperty(key, value);
}

} // namespace silica
