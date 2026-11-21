#include "silica/Dropdown.h"

#include <windows.h>
#undef DrawText // windows.h maps the name onto DrawTextW

#include <algorithm>

#include "silica/Handlers.h"
#include "silica/Property.h"
#include "silica/Renderer.h"
#include "silica/Script.h"

namespace silica {
namespace {

std::string_view trimCopy(std::string_view text) { return property::Trim(text); }

} // namespace

SilicaDropdown::SilicaDropdown() {
    width             = 160.f;
    height            = 24.f;
    zOrder            = 100; // the open list must cover later siblings
    style.background  = Color{.10f, .11f, .13f, .92f};
    style.borderColor = Color{.42f, .44f, .47f, 1.f};
    style.borderWidth = 1.f;
    style.textColor   = Color{.95f, .955f, .965f, 1.f};
}

float SilicaDropdown::listHeight() const {
    if (options.empty()) return 0.f;
    return static_cast<float>(options.size()) * itemHeight + 2.f;
}

int SilicaDropdown::indexAt(float py) const {
    if (options.empty() || py < y + height) return -1;
    float const row = (py - (y + height) - 1.f) / std::max(1.f, itemHeight);
    int const   idx = static_cast<int>(row);
    if (idx < 0 || idx >= static_cast<int>(options.size())) return -1;
    return idx;
}

std::string const& SilicaDropdown::SelectedText() const {
    if (selected < 0 || selected >= static_cast<int>(options.size())) return m_empty;
    return options[static_cast<size_t>(selected)];
}

void SilicaDropdown::SetOpen(bool open) { m_open = open && visible; }

void SilicaDropdown::Select(int index, bool notifyChange) {
    if (options.empty()) {
        selected = 0;
        return;
    }
    int const next = std::clamp(index, 0, static_cast<int>(options.size()) - 1);
    bool const changed = next != selected;
    selected           = next;
    if (notifyChange && changed) notify();
}

void SilicaDropdown::notify() {
    Variant const index = Variant{static_cast<double>(selected)};
    WriteBackBinding("selected", index);
    WriteBackBinding("index", index);
    WriteBackBinding("value", index);

    ScriptEvent event;
    event.name  = "change";
    event.value = index;
    event.text  = Variant{SelectedText()};
    event.index = index;
    if (FireScriptEvent("change", event)) return;

    if (onChange) onChange(selected, SelectedText());
    if (!m_handler.empty()) SilicaHandlers::Invoke(m_handler, std::to_string(selected));
}

bool SilicaDropdown::Contains(float px, float py) const {
    if (SilicaWidget::Contains(px, py)) return true;
    if (!m_open || options.empty()) return false;
    float const top = y + height;
    return px >= x && px < Right() && py >= top && py < top + listHeight();
}

bool SilicaDropdown::OnMouseDown(float px, float py) {
    if (!visible) return false;

    if (!m_open) {
        SetOpen(true);
        return true;
    }

    int const index = indexAt(py);
    if (index >= 0) Select(index);
    SetOpen(false);
    return true;
}

bool SilicaDropdown::OnMouseUp(float, float, bool) { return true; }

bool SilicaDropdown::OnActivate() {
    SetOpen(!m_open);
    return true;
}

void SilicaDropdown::OnFocusChanged(bool isFocused) {
    if (!isFocused) SetOpen(false);
}

bool SilicaDropdown::OnKeyDown(int keyCode) {
    if (keyCode == VK_ESCAPE) {
        if (!m_open) return false;
        SetOpen(false);
        return true;
    }
    if (keyCode == VK_RETURN || keyCode == VK_SPACE) {
        SetOpen(!m_open);
        return true;
    }
    if (keyCode == VK_UP || keyCode == VK_DOWN) {
        if (options.empty()) return true;
        Select(selected + (keyCode == VK_UP ? -1 : 1));
        return true;
    }
    return false;
}

void SilicaDropdown::Update() {
    if (height <= 0.f) height = 24.f;
    itemHeight = std::max(12.f, itemHeight);
    if (!options.empty()) selected = std::clamp(selected, 0, static_cast<int>(options.size()) - 1);
}

bool SilicaDropdown::HasState(std::string_view name) const {
    if (name == "expanded" || name == "open" || name == "opened") return m_open;
    if (name == "collapsed" || name == "closed") return !m_open;
    return SilicaWidget::HasState(name);
}

void SilicaDropdown::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    Update();
    PaintSelf(context);

    float const font  = std::max(.5f, fontSize);
    float const lineY = y + (height - 14.f * font) * .5f;
    float const pad   = std::max(0.f, itemPadding);
    std::string const& shown = SelectedText();
    if (!shown.empty()) {
        DrawText(
            context,
            x + pad,
            lineY,
            width - pad - 18.f,
            shown,
            style.textColor.ScaleAlpha(PaintAlpha()),
            font,
            style.Align(::ui::TextAlignment::Left),
            style.fontFamily
        );
    }
    std::string const& arrow = m_open ? arrowOpen : arrowClosed;
    if (!arrow.empty()) {
        DrawText(
            context,
            x + width - 18.f,
            lineY,
            12.f,
            arrow,
            arrowColor.ScaleAlpha(PaintAlpha()),
            font,
            ::ui::TextAlignment::Center,
            style.fontFamily
        );
    }
    FlushText(context);

    if (m_open && !options.empty()) {
        float const top = y + height;
        float const h   = listHeight();
        Color const listBg    = listBackground.ScaleAlpha(PaintAlpha());
        Color const listFrame = style.borderColor.ScaleAlpha(PaintAlpha());
        if (style.radius > 0.f) {
            FillRoundRect(context, x, top, width, h, style.radius, listBg);
            DrawRoundFrame(context, x, top, width, h, style.radius, listFrame);
        } else {
            FillRect(context, x, top, width, h, listBg);
            DrawFrame(context, x, top, width, h, listFrame);
        }

        for (size_t i = 0; i < options.size(); ++i) {
            float const itemY = top + 1.f + static_cast<float>(i) * itemHeight;
            if (static_cast<int>(i) == selected) {
                FillRect(
                    context,
                    x + 1.f,
                    itemY,
                    width - 2.f,
                    itemHeight,
                    selectionColor.ScaleAlpha(PaintAlpha())
                );
            }
            float const itemLine = itemY + (itemHeight - 14.f * font) * .5f;
            DrawText(
                context,
                x + pad,
                itemLine,
                std::max(0.f, width - 2.f * pad),
                options[i],
                style.textColor.ScaleAlpha(PaintAlpha()),
                font,
                style.Align(::ui::TextAlignment::Left),
                style.fontFamily
            );
            FlushText(context);
        }
    }

}

bool SilicaDropdown::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));
    if (k == "options" || k == "items" || k == "choices") {
        parseOptions(value);
        return true;
    }
    if (k == "selected" || k == "index" || k == "value") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        Select(static_cast<int>(*number), false);
        return true;
    }
    if (k == "item-height" || k == "row-height") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        itemHeight = *number;
        return true;
    }
    if (k == "font-size" || k == "font") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        fontSize = *number;
        return true;
    }
    if (k == "accent") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        accent         = *color;
        selectionColor = Color{color->r, color->g, color->b, .30f * color->a};
        return true;
    }
    if (k == "selection-color" || k == "selected-background" || k == "selected-bg" || k == "selected-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        selectionColor = *color;
        return true;
    }
    if (k == "arrow-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        arrowColor = *color;
        return true;
    }
    // One token sets both states; two tokens are closed then open. An empty
    // value (arrow: "") hides the arrow.
    if (k == "arrow") {
        auto const tokens = property::Split(value);
        if (tokens.empty()) {
            arrowClosed.clear();
            arrowOpen.clear();
        } else {
            arrowClosed = tokens.front();
            arrowOpen   = tokens.size() > 1 ? tokens[1] : tokens.front();
        }
        return true;
    }
    if (k == "arrow-closed" || k == "arrow-collapsed") {
        auto const tokens = property::Split(value);
        arrowClosed = tokens.empty() ? std::string() : tokens.front();
        return true;
    }
    if (k == "arrow-open" || k == "arrow-expanded") {
        auto const tokens = property::Split(value);
        arrowOpen = tokens.empty() ? std::string() : tokens.front();
        return true;
    }
    if (k == "list-background" || k == "list-bg") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        listBackground = *color;
        return true;
    }
    if (k == "item-padding" || k == "item-inset") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        itemPadding = *number;
        return true;
    }
    if (k == "open" || k == "expanded") {
        auto state = property::ParseBool(value);
        if (!state) return false;
        SetOpen(*state);
        return true;
    }
    if (k == "onchange" || k == "on-change") {
        std::string handler = std::string(property::Trim(value));
        if (handler.empty()) {
            onChange = {};
            m_handler.clear();
        } else {
            m_handler = std::move(handler);
        }
        return true;
    }
    return SilicaWidget::ApplyProperty(key, value);
}

void SilicaDropdown::parseOptions(std::string_view value) {
    options.clear();
    size_t start = 0;
    while (start <= value.size()) {
        size_t comma = value.find(',', start);
        if (comma == std::string_view::npos) comma = value.size();
        auto const token = property::Trim(value.substr(start, comma - start));
        if (!token.empty()) options.emplace_back(token);
        if (comma == value.size()) break;
        start = comma + 1;
    }
    if (options.empty()) {
        selected = 0;
        return;
    }
    selected = std::clamp(selected, 0, static_cast<int>(options.size()) - 1);
}

} // namespace silica
