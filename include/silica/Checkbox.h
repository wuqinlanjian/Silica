#pragma once

#include "Widget.h"

#include <functional>
#include <string>

namespace silica {

// A box plus a caption. Clicking, Space (while focused) or Enter toggles it and
// reports the new state through onChange / the named .sil handler.
class SilicaCheckbox : public SilicaWidget {
public:
    SilicaCheckbox();

    std::string                 text;
    bool                        checked = false;
    float                       fontSize = .9f;
    Color                       accent{.42f, .76f, .29f, 1.f};
    std::function<void(bool)>   onChange;

    // Internal metrics. boxSizeOverride 0 keeps the size tied to the font, and
    // checkInset below 0 keeps the mark proportional to the box.
    float boxSizeOverride = 0.f;
    float textGap         = 8.f;
    float checkInset      = -1.f;

    // false hides the built-in box, mark and glow so Parts can draw a custom
    // look (a switch, for example); the box size still indents the caption.
    bool showBox = true;

    void Toggle();
    void SetChecked(bool value, bool notify = true);

    [[nodiscard]] bool Focusable() const override { return true; }
    [[nodiscard]] bool HasState(std::string_view name) const override;
    bool OnMouseDown(float x, float y) override;
    bool OnMouseUp(float x, float y, bool inside) override;
    bool OnActivate() override;
    bool OnKeyDown(int keyCode) override;

    void Update() override;
    void Render(MinecraftUIRenderContext& context) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;

private:
    void notify();

    std::string                            m_handler;
    [[nodiscard]] float                    boxSize() const;
};

} // namespace silica
