#include "silica/Label.h"

#include "silica/Property.h"
#include "silica/Renderer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace silica {
namespace {

// GUI units per text line at size 1 (the game's default font advance); the
// single-line height above stays at the 14 used by DrawText's bounds.
constexpr float kLineAdvance = 10.f;
constexpr int   kAutoLines   = 3;

size_t glyphLength(std::string_view text, size_t index) {
    unsigned char const lead = static_cast<unsigned char>(text[index]);
    size_t              size = 1;
    if ((lead >> 5) == 0x6) size = 2;
    else if ((lead >> 4) == 0xE) size = 3;
    else if ((lead >> 3) == 0x1E) size = 4;
    return std::min(size, text.size() - index);
}

std::vector<std::string> wrapText(
    MinecraftUIRenderContext& context,
    std::string_view          text,
    float                     width,
    float                     size,
    std::string_view          family
) {
    std::vector<std::string> lines;
    std::string              current;
    size_t                   i = 0;
    while (i < text.size()) {
        if (text[i] == '\n') {
            lines.push_back(current);
            current.clear();
            ++i;
            continue;
        }
        size_t const length = glyphLength(text, i);
        current.append(text.substr(i, length));
        i += length;
        if (TextWidth(context, current, size, family) <= width) continue;

        // Overflow: break at the last space, otherwise hard-break at the glyph
        // (CJK and long words have no spaces to rely on).
        size_t const space = current.find_last_of(' ');
        if (space != std::string::npos && space > 0) {
            std::string rest = current.substr(space + 1);
            current.resize(space);
            while (!current.empty() && current.back() == ' ') current.pop_back();
            lines.push_back(std::move(current));
            current = std::move(rest);
            while (!current.empty() && current.front() == ' ') current.erase(current.begin());
        } else {
            current.resize(current.size() - length);
            if (!current.empty()) {
                lines.push_back(std::move(current));
                current.clear();
            }
            current.assign(text.substr(i - length, length));
        }
    }
    lines.push_back(current);
    return lines;
}

} // namespace

SilicaLabel::SilicaLabel() { style.textColor = Color{.95f, .955f, .965f, 1.f}; }

void SilicaLabel::Update() {
    if (height <= 0.f) {
        height = wrap ? static_cast<float>(kAutoLines) * kLineAdvance * fontSize : 14.f * fontSize;
    }
}

bool SilicaLabel::ApplyProperty(std::string_view key, std::string_view value) {
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
    if (k == "wrap" || k == "text-wrap" || k == "word-wrap") {
        auto flag = property::ParseBool(value);
        if (!flag) return false;
        wrap = *flag;
        return true;
    }
    return SilicaWidget::ApplyProperty(key, value);
}

void SilicaLabel::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    PaintSelf(context);
    if (text.empty()) return;

    Color const color = style.textColor.ScaleAlpha(PaintAlpha());
    auto const  align = style.Align(::ui::TextAlignment::Left);

    if (!wrap) {
        DrawText(context, x, y, width, text, color, fontSize, align, style.fontFamily);
        FlushText(context);
        return;
    }

    float const lineHeight = kLineAdvance * fontSize;
    int         maxLines   = height > 0.f ? static_cast<int>(std::floor(height / lineHeight + 1e-3f)) : kAutoLines;
    maxLines               = std::max(1, maxLines);

    std::vector<std::string> const lines = wrapText(context, text, width, fontSize, style.fontFamily);
    size_t const                   count = std::min(lines.size(), static_cast<size_t>(maxLines));
    for (size_t i = 0; i < count; ++i) {
        DrawText(
            context,
            x,
            y + static_cast<float>(i) * lineHeight,
            width,
            lines[i],
            color,
            fontSize,
            align,
            style.fontFamily
        );
    }
    FlushText(context);
}

} // namespace silica
