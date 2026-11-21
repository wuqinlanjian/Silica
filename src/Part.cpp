#include "silica/Part.h"

#include "silica/Property.h"
#include "silica/Renderer.h"

#include <algorithm>

namespace silica {
namespace {

SilicaPart::Anchor parseAnchor(std::string_view value) {
    std::string const name = property::ToLower(property::Trim(value));
    if (name == "fill" || name == "stretch" || name == "all") return SilicaPart::Anchor::Fill;
    if (name == "top") return SilicaPart::Anchor::Top;
    if (name == "bottom") return SilicaPart::Anchor::Bottom;
    if (name == "left") return SilicaPart::Anchor::Left;
    if (name == "right") return SilicaPart::Anchor::Right;
    if (name == "center" || name == "middle") return SilicaPart::Anchor::Center;
    if (name == "top-left" || name == "topleft") return SilicaPart::Anchor::TopLeft;
    if (name == "top-right" || name == "topright") return SilicaPart::Anchor::TopRight;
    if (name == "bottom-left" || name == "bottomleft") return SilicaPart::Anchor::BottomLeft;
    if (name == "bottom-right" || name == "bottomright") return SilicaPart::Anchor::BottomRight;
    return SilicaPart::Anchor::Fill;
}

void readInsets(SilicaPart& part, std::string_view value) {
    std::vector<float> numbers;
    for (auto const& token : property::Split(value)) {
        if (auto number = property::ParseNumber(token)) numbers.push_back(*number);
    }
    switch (numbers.size()) {
    case 1:
        part.insetTop = part.insetRight = part.insetBottom = part.insetLeft = numbers[0];
        break;
    case 2:
        part.insetTop = part.insetBottom = numbers[0];
        part.insetRight = part.insetLeft = numbers[1];
        break;
    case 3:
        part.insetTop    = numbers[0];
        part.insetRight  = numbers[1];
        part.insetLeft   = numbers[1];
        part.insetBottom = numbers[2];
        break;
    case 4:
        part.insetTop    = numbers[0];
        part.insetRight  = numbers[1];
        part.insetBottom = numbers[2];
        part.insetLeft   = numbers[3];
        break;
    default:
        break;
    }
}

} // namespace

bool SilicaPart::Place(SilicaWidget const& owner) {
    visible = false;
    if (!enabled) return false;

    if (!when.empty()) {
        for (auto const& raw : property::Split(when)) {
            std::string_view token = property::Trim(raw);
            bool const       negate = !token.empty() && token.front() == '!';
            if (negate) token.remove_prefix(1);
            std::string const word = property::ToLower(property::Trim(token));
            if (word.empty()) continue;
            bool const state = owner.HasState(word);
            if (negate ? state : !state) return false;
        }
    }

    float const innerW = std::max(0.f, owner.width - insetLeft - insetRight);
    float const innerH = std::max(0.f, owner.height - insetTop - insetBottom);
    float const partW  = widthSet ? width : innerW;
    float const partH  = heightSet ? height : innerH;

    float px = owner.x + insetLeft;
    float py = owner.y + insetTop;

    switch (anchor) {
    case Anchor::Fill:
        width  = innerW;
        height = innerH;
        break;
    case Anchor::Top:
        width  = partW;
        height = partH;
        break;
    case Anchor::Bottom:
        width  = partW;
        height = partH;
        py     = owner.y + owner.height - insetBottom - partH;
        break;
    case Anchor::Left:
        width  = partW;
        height = partH;
        py     = owner.y + insetTop + (innerH - partH) * .5f;
        break;
    case Anchor::Right:
        width  = partW;
        height = partH;
        px     = owner.x + owner.width - insetRight - partW;
        py     = owner.y + insetTop + (innerH - partH) * .5f;
        break;
    case Anchor::Center:
        width  = partW;
        height = partH;
        px     = owner.x + insetLeft + (innerW - partW) * .5f;
        py     = owner.y + insetTop + (innerH - partH) * .5f;
        break;
    case Anchor::TopLeft:
        width  = partW;
        height = partH;
        break;
    case Anchor::TopRight:
        width  = partW;
        height = partH;
        px     = owner.x + owner.width - insetRight - partW;
        break;
    case Anchor::BottomLeft:
        width  = partW;
        height = partH;
        py     = owner.y + owner.height - insetBottom - partH;
        break;
    case Anchor::BottomRight:
        width  = partW;
        height = partH;
        px     = owner.x + owner.width - insetRight - partW;
        py     = owner.y + owner.height - insetBottom - partH;
        break;
    }

    x = px + offsetX;
    y = py + offsetY;
    visible = true;
    return true;
}

void SilicaPart::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    PaintSelf(context);
    if (!src.empty()) {
        DrawTexture(context, src, x, y, width, height, tint, PaintAlpha());
    }
    if (!text.empty()) {
        float const font  = std::max(.5f, fontSize);
        float const lineY = y + (height - 14.f * font) * .5f;
        DrawText(
            context,
            x,
            lineY,
            width,
            text,
            style.textColor.ScaleAlpha(PaintAlpha()),
            font,
            style.Align(::ui::TextAlignment::Center),
            style.fontFamily
        );
        FlushText(context);
    }
}

bool SilicaPart::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));
    if (k == "anchor" || k == "attach" || k == "align-to") {
        anchor = parseAnchor(value);
        return true;
    }
    if (k == "inset" || k == "insets") {
        readInsets(*this, value);
        return true;
    }
    if (k == "inset-x") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        insetLeft = insetRight = *number;
        return true;
    }
    if (k == "inset-y") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        insetTop = insetBottom = *number;
        return true;
    }
    if (k == "inset-top") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        insetTop = *number;
        return true;
    }
    if (k == "inset-right") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        insetRight = *number;
        return true;
    }
    if (k == "inset-bottom") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        insetBottom = *number;
        return true;
    }
    if (k == "inset-left") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        insetLeft = *number;
        return true;
    }
    if (k == "offset-x" || k == "dx") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        offsetX = *number;
        return true;
    }
    if (k == "offset-y" || k == "dy") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        offsetY = *number;
        return true;
    }
    if (k == "layer" || k == "paint-layer") {
        std::string const name = property::ToLower(property::Trim(value));
        if (name == "under" || name == "below" || name == "back" || name == "backdrop") {
            under = true;
            return true;
        }
        if (name == "over" || name == "above" || name == "front") {
            under = false;
            return true;
        }
        return false;
    }
    if (k == "when" || k == "if" || k == "state") {
        when = std::string(property::Trim(value));
        return true;
    }
    if (k == "src" || k == "texture" || k == "image") {
        src = std::string(property::Trim(value));
        return true;
    }
    if (k == "tint" || k == "texture-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        tint = *color;
        return true;
    }
    if (k == "text" || k == "label") {
        text = std::string(value);
        return true;
    }
    if (k == "font-size") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        fontSize = *number;
        return true;
    }
    if (k == "visible" || k == "show" || k == "enabled") {
        auto shown = property::ParseBool(value);
        if (!shown) return false;
        enabled = *shown;
        return true;
    }
    if (k == "width" || k == "height") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        if (k == "width") {
            width    = *number;
            widthSet = *number > 0.f;
        } else {
            height    = *number;
            heightSet = *number > 0.f;
        }
        return true;
    }
    if (k == "x" || k == "y") {
        // Parts are positioned by their anchor; x/y only make sense as offsets.
        auto number = property::ParseNumber(value);
        if (!number) return false;
        if (k == "x") offsetX = *number;
        else offsetY = *number;
        return true;
    }
    return SilicaWidget::ApplyProperty(key, value);
}

} // namespace silica
