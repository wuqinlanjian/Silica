#include "silica/Input.h"

#include "silica/Container.h"

#include <functional>

namespace silica {
namespace {

SilicaWidget* nearestFocusable(SilicaWidget* widget) {
    for (SilicaWidget* current = widget; current; current = current->parent) {
        // A disabled ancestor makes the whole subtree inert, so focus must not
        // jump above it.
        if (current->disabled) return nullptr;
        if (current->Focusable()) return current;
    }
    return nullptr;
}

void setHovered(SilicaWidget*& current, SilicaWidget* next) {
    if (current == next) return;
    if (current) current->hovered = false;
    current = next;
    if (next) next->hovered = true;
}

} // namespace

FocusManager& FocusManager::Get() {
    static FocusManager instance;
    return instance;
}

void FocusManager::Set(SilicaWidget* widget) {
    if (m_focused == widget) {
        if (m_focused) m_focused->focused = true;
        return;
    }
    SilicaWidget* previous = m_focused;
    m_focused              = widget;
    if (previous) {
        previous->focused = false;
        previous->OnFocusChanged(false);
    }
    if (m_focused) {
        m_focused->focused = true;
        m_focused->OnFocusChanged(true);
    }
}

void FocusManager::Clear() { Set(nullptr); }

void PointerRouter::Update(WidgetPtr const& root, PointerState const& pointer) {
    // Disabling a widget mid-drag (a script or a bound flag) must end the drag
    // instead of feeding moves to an inert control.
    if (m_captured && m_captured->IsDisabled()) {
        SilicaWidget* captured = m_captured;
        m_captured             = nullptr;
        captured->pressed      = false;
        captured->OnMouseUp(pointer.x, pointer.y, false);
    }

    SilicaWidget* hit = pointer.inside ? HitTest(root, pointer.x, pointer.y).get() : nullptr;
    // Any disabled ancestor disables the hit widget as well.
    bool const interactive = hit && !hit->IsDisabled();

    // A captured widget keeps the pointer even when it leaves the cursor, so
    // drags stay stable; hover still follows the geometry.
    setHovered(m_hovered, interactive ? hit : nullptr);

    if (pointer.pressed) {
        if (m_captured) {
            m_captured->pressed = false;
            m_captured          = nullptr;
        }

        SilicaWidget* handler = nullptr;
        if (interactive) {
            for (SilicaWidget* current = hit; current; current = current->parent) {
                if (current->OnMouseDown(pointer.x, pointer.y)) {
                    handler = current;
                    break;
                }
            }
        }
        if (handler) {
            handler->pressed = true;
            m_captured       = handler;
        }
        FocusManager::Get().Set(nearestFocusable(hit));
    }

    if (m_captured) {
        if (pointer.released) {
            SilicaWidget* captured = m_captured;
            m_captured             = nullptr;
            captured->pressed      = false;
            captured->OnMouseUp(pointer.x, pointer.y, captured == hit);
        } else if (pointer.down && !pointer.pressed) {
            m_captured->OnMouseMove(pointer.x, pointer.y);
        }
    }
}

bool PointerRouter::Wheel(WidgetPtr const& root, PointerState const& pointer, float delta) {
    if (delta == 0.f) return false;
    SilicaWidget* hit = pointer.inside ? HitTest(root, pointer.x, pointer.y).get() : nullptr;
    for (SilicaWidget* current = hit; current; current = current->parent) {
        // Scrolling over a disabled row still belongs to the enclosing list.
        if (current->disabled) continue;
        if (current->OnMouseWheel(pointer.x, pointer.y, delta)) return true;
    }
    return false;
}

void PointerRouter::Reset() {
    if (m_hovered) m_hovered->hovered = false;
    if (m_captured) m_captured->pressed = false;
    m_hovered  = nullptr;
    m_captured = nullptr;
}

void FocusRing::Collect(WidgetPtr const& root) {
    m_widgets.clear();
    m_focus = 0;

    std::function<void(WidgetPtr const&)> walk = [&](WidgetPtr const& widget) {
        if (!widget) return;
        if (widget->Focusable() && !widget->IsDisabled()) m_widgets.push_back(widget);
        if (auto container = std::dynamic_pointer_cast<SilicaContainer>(widget)) {
            for (auto const& child : container->Children()) walk(child);
        }
    };
    walk(root);

    if (!m_widgets.empty()) {
        FocusManager::Get().Set(m_widgets.front().get());
    } else {
        FocusManager::Get().Clear();
    }
}

void FocusRing::Move(int delta) {
    int const count = static_cast<int>(m_widgets.size());
    if (count == 0) return;
    int const step = delta < 0 ? -1 : 1;

    // A click may have moved the focus; continue the walk from there.
    int index = m_focus;
    if (auto* focused = FocusManager::Get().Current()) {
        for (int i = 0; i < count; ++i) {
            if (m_widgets[static_cast<size_t>(i)].get() == focused) {
                index = i;
                break;
            }
        }
    }

    for (int attempt = 0; attempt < count; ++attempt) {
        index = (index + step + count) % count;
        SilicaWidget* candidate = m_widgets[static_cast<size_t>(index)].get();
        if (candidate->visible && !candidate->IsDisabled()) break;
    }
    m_focus = index;
    FocusManager::Get().Set(m_widgets[static_cast<size_t>(m_focus)].get());
}

void FocusRing::Focus(SilicaWidget* widget) {
    if (!widget || widget->IsDisabled()) return;
    for (size_t i = 0; i < m_widgets.size(); ++i) {
        if (m_widgets[i].get() == widget) {
            m_focus = static_cast<int>(i);
            FocusManager::Get().Set(widget);
            return;
        }
    }
}

void FocusRing::Activate() {
    if (auto* widget = Current()) widget->OnActivate();
}

SilicaWidget* FocusRing::Current() const {
    if (m_widgets.empty()) return nullptr;
    int const     index = m_focus < 0 || m_focus >= static_cast<int>(m_widgets.size()) ? 0 : m_focus;
    SilicaWidget* current = m_widgets[static_cast<size_t>(index)].get();
    return current->IsDisabled() ? nullptr : current;
}

} // namespace silica
