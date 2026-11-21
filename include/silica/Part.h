#pragma once

#include "Widget.h"

#include <string>
#include <string_view>

namespace silica {

// A decorative layer attached to another widget. Parts are the structure-level
// skin hook: any widget accepts them as children from .sil, they are laid out
// relative to the owner every frame instead of taking part in layout, and they
// can be shown conditionally from the owner's state.
//
//     Checkbox {
//         box-size: 40; show-box: false;
//         Part { anchor: left; width: 40; height: 20; radius: 10; background: #333; when: "!checked"; }
//         Part { anchor: left; width: 40; height: 20; radius: 10; background: #498FC3; when: "checked"; }
//     }
class SilicaPart : public SilicaWidget {
public:
    enum class Anchor {
        Fill,
        Top,
        Bottom,
        Left,
        Right,
        Center,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    Anchor anchor = Anchor::Fill;

    // Insets shrink the anchor box inside the owner; offsets shift the placed
    // part without changing its size.
    float insetTop    = 0.f;
    float insetRight  = 0.f;
    float insetBottom = 0.f;
    float insetLeft   = 0.f;
    float offsetX     = 0.f;
    float offsetY     = 0.f;

    // false paints the part before the owner's own paint (a glow or plate),
    // true paints it above everything the owner draws (the default).
    bool under = false;

    // Space/comma separated conditions evaluated against the owner; a leading
    // '!' negates. Example: "checked !hovered".
    std::string when;

    // Optional content: a stretched game texture and/or one line of text.
    std::string src;
    Color       tint{kWhite};
    std::string text;
    float       fontSize = 1.f;

    // `visible: false` in .sil disables the part without removing it.
    bool enabled = true;

    void Render(MinecraftUIRenderContext& context) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;

    // Positions the part against the owner and evaluates `when`. Returns false
    // (and hides the part) when a condition does not hold.
    bool Place(SilicaWidget const& owner);

private:
    bool widthSet  = false;
    bool heightSet = false;
};

} // namespace silica
