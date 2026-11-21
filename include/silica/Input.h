#pragma once

#include "Tree.h"

#include <memory>
#include <vector>

namespace silica {

// One frame of pointer input in GUI units; the caller derives the edges from
// whatever it polls (the game sends no usable cursor position for overlays).
struct PointerState {
    float x        = 0.f;
    float y        = 0.f;
    bool  down     = false;
    bool  pressed  = false; // down edge this frame
    bool  released = false; // up edge this frame
    bool  inside   = true;  // cursor is over the game window
};

// The single globally focused widget. Clicking a control focuses it, clicking
// empty space or pressing Esc releases it; widgets hear about the change
// through OnFocusChanged.
class FocusManager {
public:
    static FocusManager& Get();

    void Set(SilicaWidget* widget);
    void Clear();

    // nullptr while the focused widget became disabled, so key handlers never
    // feed an inert control (Tick also drops the focus itself).
    [[nodiscard]] SilicaWidget* Current() const {
        return m_focused && m_focused->IsDisabled() ? nullptr : m_focused;
    }

private:
    SilicaWidget* m_focused = nullptr;
};

// Routes pointer input into a widget tree. Press/release are delivered to the
// deepest widget under the cursor and bubble to ancestors until handled; the
// handler keeps receiving moves until the button is released (drag).
class PointerRouter {
public:
    // Call once per frame with hover position and any edges.
    void Update(WidgetPtr const& root, PointerState const& pointer);

    // Delivers one wheel notch; returns true when a widget consumed it.
    bool Wheel(WidgetPtr const& root, PointerState const& pointer, float delta);

    [[nodiscard]] SilicaWidget* Hovered() const { return m_hovered; }
    [[nodiscard]] SilicaWidget* Captured() const { return m_captured; }

    void Reset();

private:
    SilicaWidget* m_hovered  = nullptr;
    SilicaWidget* m_captured = nullptr;
};

// Keyboard focus order across every focusable widget in document order.
class FocusRing {
public:
    void Collect(WidgetPtr const& root);
    void Move(int delta);
    void Focus(SilicaWidget* widget);
    void Activate();

    [[nodiscard]] SilicaWidget* Current() const;
    [[nodiscard]] size_t        Size() const { return m_widgets.size(); }

private:
    std::vector<std::shared_ptr<SilicaWidget>> m_widgets;
    int                                        m_focus = 0;
};

} // namespace silica
