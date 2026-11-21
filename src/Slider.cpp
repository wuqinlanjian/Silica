#include "silica/Slider.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "silica/Handlers.h"
#include "silica/Property.h"
#include "silica/Renderer.h"
#include "silica/Script.h"

namespace silica {
namespace {

std::string formatValue(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    std::string text(buffer);
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text.empty() ? "0" : text;
}

double secondsNow() {
    using clock                         = std::chrono::steady_clock;
    static clock::time_point const start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

} // namespace

SilicaSlider::SilicaSlider() { height = 20.f; }

float SilicaSlider::fraction() const {
    if (max <= min) return 0.f;
    return std::clamp((m_displayValue - min) / (max - min), 0.f, 1.f);
}

void SilicaSlider::SetValue(float next, bool notifyChange) {
    float result = std::clamp(next, std::min(min, max), std::max(min, max));
    if (step > 0.f) {
        float const base = std::min(min, max);
        result           = base + std::round((result - base) / step) * step;
        result           = std::clamp(result, std::min(min, max), std::max(min, max));
    }
    if (result == value) return;
    value = result;
    if (!smooth) m_displayValue = value;
    if (notifyChange) notify();
}

void SilicaSlider::notify() {
    WriteBackBinding("value", Variant{value});

    ScriptEvent event;
    event.name  = "change";
    event.value = Variant{value};
    if (FireScriptEvent("change", event)) return;

    if (onChange) onChange(value);
    if (!m_handler.empty()) SilicaHandlers::Invoke(m_handler, formatValue(value));
}

void SilicaSlider::setFromPointer(float px) {
    float const pad   = knobSize * .5f;
    float const range = width - knobSize;
    if (range <= 0.f) {
        SetValue(px >= x + width * .5f ? max : min);
        return;
    }
    float const ratio = std::clamp((px - (x + pad)) / range, 0.f, 1.f);
    SetValue(min + ratio * (max - min));
}

bool SilicaSlider::OnMouseDown(float px, float) {
    if (!visible) return false;
    setFromPointer(px);
    return true;
}

bool SilicaSlider::OnMouseMove(float px, float) {
    if (!pressed) return false;
    setFromPointer(px);
    return true;
}

bool SilicaSlider::OnMouseUp(float px, float, bool inside) {
    if (inside) setFromPointer(px);
    return true;
}

bool SilicaSlider::OnActivate() {
    float const stepBy = step > 0.f ? step : std::max(1.f, (max - min) / 10.f);
    SetValue(value + stepBy);
    return true;
}

bool SilicaSlider::OnKeyDown(int keyCode) {
    float const stepBy = step > 0.f ? step : std::max(.01f, (max - min) / 100.f);
    switch (keyCode) {
    case VK_LEFT:
    case VK_DOWN:
        SetValue(value - stepBy);
        return true;
    case VK_RIGHT:
    case VK_UP:
        SetValue(value + stepBy);
        return true;
    case VK_HOME:
        SetValue(min);
        return true;
    case VK_END:
        SetValue(max);
        return true;
    default:
        return false;
    }
}

void SilicaSlider::Update() {
    if (height <= 0.f) height = 20.f;
    knobSize = std::clamp(knobSize, 6.f, std::max(6.f, height));

    double const now = secondsNow();
    float const  dt  = m_lastTick >= 0.0
        ? static_cast<float>(std::clamp(now - m_lastTick, 1.0 / 240.0, .1))
        : 1.f / 60.f;
    m_lastTick = now;

    if (!smooth || smoothing <= 0.f) {
        m_displayValue = value;
        return;
    }
    if (m_displayValue == value) return;

    float const easing = 1.f - std::exp(-smoothing * dt);
    m_displayValue += (value - m_displayValue) * easing;
    float const epsilon = std::max(1e-4f, std::fabs(max - min) * 5e-4f);
    if (std::fabs(value - m_displayValue) < epsilon) m_displayValue = value;
}

void SilicaSlider::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    Update();
    PaintParts(context, true);

    float const trackH = std::clamp(trackHeight, .5f, std::max(.5f, height));
    float const pad    = knobSize * .5f;
    float const trackX = x + pad;
    float const trackW = std::max(0.f, width - knobSize);
    float const trackY = y + (height - trackH) * .5f;
    Color const track  = style.background.a > 0.f ? style.background : trackColor;

    float const trackRadius = style.radius > 0.f ? std::min(style.radius, trackH * .5f) : 0.f;
    auto const drawBar = [&](float barW, Color color) {
        if (trackRadius > 0.f) {
            FillRoundRect(context, trackX, trackY, barW, trackH, trackRadius, color);
        } else {
            FillRect(context, trackX, trackY, barW, trackH, color);
        }
    };
    drawBar(trackW, track.ScaleAlpha(PaintAlpha()));
    drawBar(trackW * fraction(), accent.ScaleAlpha(PaintAlpha()));

    float const knobX = trackX + trackW * fraction() - pad;
    float const knobY = y + (height - knobSize) * .5f;
    Color const knobFill = (pressed ? knobActiveColor : knobColor).ScaleAlpha(PaintAlpha());
    if (style.radius > 0.f) FillRoundRect(context, knobX, knobY, knobSize, knobSize, style.radius, knobFill);
    else FillRect(context, knobX, knobY, knobSize, knobSize, knobFill);
    Color const knobFrame = Color{.35f, .37f, .40f, 1.f};
    if (style.radius > 0.f) {
        DrawRoundFrame(context, knobX, knobY, knobSize, knobSize, style.radius, knobFrame.ScaleAlpha(PaintAlpha()), 1.f);
    } else {
        DrawFrame(context, knobX, knobY, knobSize, knobSize, knobFrame.ScaleAlpha(PaintAlpha()), 1.f);
    }

    PaintParts(context, false);
}

bool SilicaSlider::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));
    if (k == "min") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        min = *number;
        SetValue(this->value, false);
        return true;
    }
    if (k == "max") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        max = *number;
        SetValue(this->value, false);
        return true;
    }
    if (k == "value") {
        auto number = property::ParseNumber(value);
        if (!number) return false;
        SetValue(*number, false);
        return true;
    }
    if (k == "step") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        step = *number;
        SetValue(this->value, false);
        return true;
    }
    if (k == "smooth" || k == "smooth-value" || k == "animate") {
        auto flag = property::ParseBool(value);
        if (!flag) return false;
        smooth = *flag;
        if (!smooth) m_displayValue = this->value;
        return true;
    }
    if (k == "smoothing" || k == "smooth-speed" || k == "smooth-easing") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        smoothing = *number;
        return true;
    }
    if (k == "knob-size" || k == "knob") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        knobSize = *number;
        return true;
    }
    if (k == "accent" || k == "fill-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        accent = *color;
        return true;
    }
    if (k == "track-height" || k == "track-size") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        trackHeight = *number;
        return true;
    }
    if (k == "track-color" || k == "groove-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        trackColor = *color;
        return true;
    }
    if (k == "knob-color" || k == "handle-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        knobColor = *color;
        return true;
    }
    if (k == "knob-active-color" || k == "knob-pressed-color" || k == "handle-active-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        knobActiveColor = *color;
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

} // namespace silica
