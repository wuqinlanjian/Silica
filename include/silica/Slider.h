#pragma once

#include "Widget.h"

#include <functional>
#include <string>

namespace silica {

// Horizontal value track. Pressing anywhere jumps the knob there, dragging
// keeps updating it; the value is clamped to [min, max] and optionally
// quantized to `step`. onChange fires on every change (live) and the named
// handler receives the formatted number.
class SilicaSlider : public SilicaWidget {
public:
    SilicaSlider();

    float                       min     = 0.f;
    float                       max     = 100.f;
    float                       value   = 50.f;
    float                       step    = 0.f; // 0 = continuous
    float                       knobSize = 12.f;
    Color                       accent{.42f, .76f, .29f, 1.f};
    std::function<void(float)>  onChange;

    // Internal metrics; style.background (when opaque) still wins for the
    // track so a shared panel background keeps working.
    float trackHeight    = 4.f;
    Color trackColor     = Color{.22f, .23f, .25f, 1.f};
    Color knobColor      = Color{.88f, .90f, .92f, 1.f};
    Color knobActiveColor = Color{.98f, .99f, 1.f, 1.f};

    // Optional value easing: the knob glides to every new value, including
    // pointer drags, instead of snapping.
    bool  smooth    = false;
    float smoothing = 20.f;

    void SetValue(float next, bool notify = true);
    [[nodiscard]] float DisplayValue() const { return m_displayValue; }

    [[nodiscard]] bool Focusable() const override { return true; }
    bool OnMouseDown(float x, float y) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseUp(float x, float y, bool inside) override;
    bool OnActivate() override;
    bool OnKeyDown(int keyCode) override;

    void Update() override;
    void Render(MinecraftUIRenderContext& context) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;

private:
    void                notify();
    void                setFromPointer(float x);
    [[nodiscard]] float fraction() const;

    std::string m_handler;
    float       m_displayValue = 50.f; // what Render paints; eases toward value
    double      m_lastTick     = -1.0;
};

} // namespace silica
