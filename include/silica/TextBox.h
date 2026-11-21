#pragma once

#include "Widget.h"

#include <functional>
#include <string>

namespace silica {

// Single-line text field. A click focuses it (FocusManager); while focused it
// takes printable keys and Backspace, shows a caret at the end, and pushes the
// text out through onChange / the named handler. Esc drops the focus, Enter
// submits, and losing focus saves the final text.
class SilicaTextBox : public SilicaWidget {
public:
    SilicaTextBox();

    std::string                             text;
    std::string                             placeholder;
    int                                     maxLength = 32;
    float                                   fontSize  = .9f;
    std::function<void(std::string const&)> onChange;
    std::function<void(std::string const&)> onSubmit;

    // Internal metrics exposed to the declarative layer.
    float textPadding      = 5.f;
    float caretWidth       = 1.5f;
    Color caretColor       = Color{.95f, .97f, 1.f, 1.f};
    Color placeholderColor = Color{.62f, .64f, .67f, 1.f};

    void SetText(std::string next, bool notifyChange = true);

    [[nodiscard]] bool Focusable() const override { return true; }
    [[nodiscard]] bool HasState(std::string_view name) const override;
    bool OnMouseDown(float x, float y) override;
    bool OnMouseUp(float x, float y, bool inside) override;
    void OnFocusChanged(bool isFocused) override;
    bool OnKeyDown(int keyCode) override;

    void Update() override;
    void Tick(float deltaTime) override;
    void Render(MinecraftUIRenderContext& context) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;

private:
    void notify();
    void eraseLastCodepoint();
    bool insertChar(char c);

    std::string m_handler;
    std::string m_submitHandler;
    std::string m_textAtFocus;
    float       m_caretTime = 0.f;
};

} // namespace silica
