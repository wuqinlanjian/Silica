#pragma once

#include "Color.h"

#include <optional>
#include <string>
#include <vector>

#include "mc/client/gui/TextAlignment.h"

namespace silica {

struct GradientStop {
    float position = 0.f; // 0..1 along the gradient axis
    Color color{kTransparent};
};

// Linear gradient along one axis. `horizontal` runs left to right; the default
// runs top to bottom. `stops` carries the full CSS-style stop list; from/to
// stay filled for code written against the original two-stop shape.
struct GradientSpec {
    Color                     from{kTransparent};
    Color                     to{kTransparent};
    bool                      horizontal = false;
    std::vector<GradientStop> stops;

    // Every stop (and from/to) multiplied by alpha; used for style opacity.
    [[nodiscard]] GradientSpec WithAlpha(float alpha) const;
};

// "linear-gradient(to right, #498FC3, #6BA9D9)" or the legacy "from to" pair.
std::optional<GradientSpec> ParseGradient(std::string_view text);

// Drop shadow behind the whole box. blur/spread expand the shape layer by
// layer, so no shader or texture is involved.
struct ShadowSpec {
    float dx     = 0.f;
    float dy     = 2.f;
    float blur   = 4.f;
    float spread = 0.f;
    Color color  = Color{0.f, 0.f, 0.f, .35f};
};

// Border image drawn as nine slices. Slice sizes are in texture pixels, like
// CSS border-image-slice; the corners keep their size and the edges stretch.
struct NineSliceSpec {
    std::string src;
    float       left   = 0.f;
    float       top    = 0.f;
    float       right  = 0.f;
    float       bottom = 0.f;

    [[nodiscard]] bool empty() const { return src.empty(); }
};

// One style record per widget, the way a QSS rule bundles the properties a
// selector sets. Colors are straight alpha RGBA in 0..1; opacity multiplies
// the alpha of everything the widget paints.
struct UIStyle {
    Color background{kTransparent};
    Color borderColor{kTransparent};
    float borderWidth = 0.f;
    Color textColor{kWhite};
    float opacity = 1.f;

    // Corner rounding applied to the background and the border; 0 keeps sharp
    // corners. Clamped to half the smaller side at draw time.
    float radius = 0.f;

    // Optional state colors. When a color is present the widget paints it in
    // that state (disabled wins over pressed, pressed wins over hovered); when
    // absent, controls keep their legacy dimming/overlay behavior.
    std::optional<Color> hoverBackground;
    std::optional<Color> pressedBackground;
    std::optional<Color> disabledBackground;

    // Alpha multiplier applied to everything a disabled widget paints. 1 turns
    // the automatic dim off, letting a document style disabled widgets purely
    // through `@disabled` rules.
    float disabledOpacity = .45f;

    // Fill alternatives and decoration painted with the background.
    std::optional<GradientSpec>   gradient;
    std::optional<ShadowSpec>     shadow;
    std::optional<NineSliceSpec>  borderImage;

    // Empty means the engine's default UI font. Accepts a font name from the
    // game's font table ("Mojangles", ...) or the aliases default/smooth/
    // unicode/rune/ui.
    std::string fontFamily;

    // -1 keeps the widget's own default; 0 left, 1 center, 2 right.
    int textAlign = -1;

    // Text alignment for a widget that has its own default.
    [[nodiscard]] ::ui::TextAlignment Align(::ui::TextAlignment fallback) const {
        switch (textAlign) {
        case 0:
            return ::ui::TextAlignment::Left;
        case 1:
            return ::ui::TextAlignment::Center;
        case 2:
            return ::ui::TextAlignment::Right;
        default:
            return fallback;
        }
    }

    UIStyle WithOpacity(float value) const;

    static UIStyle DarkPanel();
    static UIStyle PrimaryButton();

    // The same panel without fill, for a border-only frame.
    static UIStyle Outline();
};

} // namespace silica
