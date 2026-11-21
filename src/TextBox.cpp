#include "silica/TextBox.h"

#include <windows.h>
#undef DrawText // windows.h maps the name onto DrawTextW

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#include "silica/Handlers.h"
#include "silica/Input.h"
#include "silica/Property.h"
#include "silica/Renderer.h"
#include "silica/Script.h"

namespace silica {
namespace {

std::optional<char> keyToChar(int keyCode) {
    if (keyCode >= 'A' && keyCode <= 'Z') return static_cast<char>(keyCode - 'A' + 'a');
    if (keyCode >= '0' && keyCode <= '9') return static_cast<char>(keyCode);
    switch (keyCode) {
    case VK_SPACE: return ' ';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_MINUS: return '-';
    case VK_OEM_PLUS: return '=';
    case VK_OEM_1: return ';';
    case VK_OEM_2: return '/';
    case VK_OEM_3: return '`';
    case VK_OEM_4: return '[';
    case VK_OEM_5: return '\\';
    case VK_OEM_6: return ']';
    case VK_OEM_7: return '\'';
    default: return std::nullopt;
    }
}

// Keeps the byte limit from splitting a UTF-8 sequence.
size_t clampToLimit(std::string_view text, int limit) {
    if (limit <= 0 || text.size() <= static_cast<size_t>(limit)) return text.size();
    size_t end = static_cast<size_t>(limit);
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    return end;
}

} // namespace

SilicaTextBox::SilicaTextBox() {
    height            = 22.f;
    style.background  = Color{.10f, .11f, .13f, .92f};
    style.borderColor = Color{.42f, .44f, .47f, 1.f};
    style.borderWidth = 1.f;
    style.textColor   = Color{.95f, .955f, .965f, 1.f};
}

void SilicaTextBox::Update() {
    if (height <= 0.f) height = 22.f;
}

void SilicaTextBox::Tick(float deltaTime) {
    SilicaWidget::Tick(deltaTime);
    if (focused) m_caretTime += std::clamp(deltaTime, 0.f, .25f);
}

void SilicaTextBox::SetText(std::string next, bool notifyChange) {
    text = std::move(next);
    if (maxLength > 0) text.resize(clampToLimit(text, maxLength));
    if (notifyChange) notify();
}

void SilicaTextBox::notify() {
    WriteBackBinding("text", Variant{text});

    ScriptEvent event;
    event.name  = "change";
    event.value = Variant{text};
    event.text  = Variant{text};
    if (FireScriptEvent("change", event)) return;

    if (onChange) onChange(text);
    if (!m_handler.empty()) SilicaHandlers::Invoke(m_handler, text);
}

void SilicaTextBox::eraseLastCodepoint() {
    if (text.empty()) return;
    size_t end = text.size() - 1;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    text.erase(end);
}

bool SilicaTextBox::insertChar(char c) {
    if (maxLength > 0 && static_cast<int>(text.size()) >= maxLength) return false;
    text.push_back(c);
    notify();
    return true;
}

bool SilicaTextBox::OnMouseDown(float, float) { return visible; }

bool SilicaTextBox::HasState(std::string_view name) const {
    if (name == "empty" || name == "blank") return text.empty();
    if (name == "filled" || name == "has-text") return !text.empty();
    if (name == "editing") return focused;
    return SilicaWidget::HasState(name);
}

bool SilicaTextBox::OnMouseUp(float, float, bool) { return true; }

void SilicaTextBox::OnFocusChanged(bool isFocused) {
    m_caretTime = 0.f;
    if (isFocused) {
        m_textAtFocus = text;
        return;
    }
    // Losing focus saves: report the final text once if it changed since the
    // field was entered.
    if (text != m_textAtFocus) notify();
}

bool SilicaTextBox::OnKeyDown(int keyCode) {
    if (!focused) return false;

    switch (keyCode) {
    case VK_BACK:
        if (!text.empty()) {
            eraseLastCodepoint();
            notify();
        }
        return true;
    case VK_ESCAPE:
        FocusManager::Get().Clear();
        return true;
    case VK_RETURN: {
        ScriptEvent event;
        event.name  = "submit";
        event.value = Variant{text};
        event.text  = Variant{text};
        if (FireScriptEvent("submit", event)) return true;
        if (onSubmit) onSubmit(text);
        if (!m_submitHandler.empty()) SilicaHandlers::Invoke(m_submitHandler, text);
        return true;
    }
    default:
        break;
    }

    // A mapped character belongs to the field even when max_length rejects it,
    // so it never falls through to the host's shortcuts.
    if (auto c = keyToChar(keyCode)) {
        insertChar(*c);
        return true;
    }
    return false;
}

void SilicaTextBox::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    Update();
    PaintSelf(context);

    float const padding = std::max(0.f, textPadding);
    float const innerW  = std::max(0.f, width - 2.f * padding);
    if (innerW <= 0.f || height <= 0.f) return;

    float const font   = std::max(.5f, fontSize);
    float const lineY  = y + (height - 14.f * font) * .5f;
    float const textW  = TextWidth(context, text, font, style.fontFamily);
    float const shift  = std::max(0.f, textW - innerW);
    float const startX = x + padding - shift;

    PushClip(context, x + padding, y + 1.f, innerW, std::max(0.f, height - 2.f));
    if (text.empty() && !placeholder.empty()) {
        DrawText(
            context,
            startX,
            lineY,
            innerW,
            placeholder,
            placeholderColor.ScaleAlpha(PaintAlpha()),
            font,
            ::ui::TextAlignment::Left,
            style.fontFamily
        );
    } else {
        DrawText(
            context,
            startX,
            lineY,
            textW + 2.f,
            text,
            style.textColor.ScaleAlpha(PaintAlpha()),
            font,
            ::ui::TextAlignment::Left,
            style.fontFamily
        );
    }
    float const blinkPeriod = 1.06f;
    bool const  caretShown  = std::fmod(m_caretTime, blinkPeriod) < blinkPeriod * .5f;
    if (focused && caretShown) {
        float const caretX = startX + textW;
        FillRect(
            context,
            caretX,
            y + 4.f,
            std::max(.5f, caretWidth),
            std::max(0.f, height - 8.f),
            caretColor.ScaleAlpha(PaintAlpha())
        );
    }
    FlushText(context);
    PopClip(context);
}

bool SilicaTextBox::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));
    if (k == "text" || k == "value") {
        text = std::string(value);
        if (maxLength > 0) text.resize(clampToLimit(text, maxLength));
        return true;
    }
    if (k == "placeholder" || k == "hint") {
        placeholder = std::string(value);
        return true;
    }
    if (k == "max-length" || k == "max_length" || k == "maxlength") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        maxLength = static_cast<int>(*number);
        return true;
    }
    if (k == "font-size" || k == "font") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        fontSize = *number;
        return true;
    }
    if (k == "text-padding" || k == "padding-x" || k == "inset") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        textPadding = *number;
        return true;
    }
    if (k == "caret-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        caretColor = *color;
        return true;
    }
    if (k == "caret-width" || k == "caret-size") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        caretWidth = *number;
        return true;
    }
    if (k == "placeholder-color" || k == "hint-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        placeholderColor = *color;
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
    if (k == "onsubmit" || k == "on-submit" || k == "onenter") {
        std::string handler = std::string(property::Trim(value));
        if (handler.empty()) {
            onSubmit = {};
            m_submitHandler.clear();
        } else {
            m_submitHandler = std::move(handler);
        }
        return true;
    }
    return SilicaWidget::ApplyProperty(key, value);
}

} // namespace silica
