#include "silica/Container.h"

#include "silica/Part.h"
#include "silica/Property.h"
#include "silica/Renderer.h"

#include <algorithm>
#include <cmath>

namespace silica {

WidgetPtr SilicaContainer::Add(WidgetPtr child) { return AddChild(child) ? child : nullptr; }

bool SilicaContainer::AddChild(WidgetPtr child) {
    if (!child) return false;
    // Parts are decor layers of this container, not layout children.
    if (dynamic_cast<SilicaPart*>(child.get())) return SilicaWidget::AddChild(std::move(child));
    child->parent = this;
    children.push_back(std::move(child));
    return true;
}

void SilicaContainer::Clear() { children.clear(); }

std::vector<WidgetPtr> const& SilicaContainer::PaintOrder() const {
    m_paintOrder.clear();
    bool sorted = true;
    int  last   = 0;
    bool first  = true;
    for (auto const& child : children) {
        if (!child) continue;
        if (!first && child->zOrder < last) sorted = false;
        last  = child->zOrder;
        first = false;
        m_paintOrder.push_back(child);
    }
    if (!sorted) {
        std::stable_sort(m_paintOrder.begin(), m_paintOrder.end(), [](WidgetPtr const& a, WidgetPtr const& b) {
            return a->zOrder < b->zOrder;
        });
    }
    return m_paintOrder;
}

void SilicaContainer::Update() {
    if (!visible) return;

    float const inset    = std::max(0.f, padding);
    float const contentX = x + inset;
    float const contentY = y + inset;
    float const contentW = width > 0.f ? std::max(0.f, width - 2.f * inset) : 0.f;
    float const contentH = height > 0.f ? std::max(0.f, height - 2.f * inset) : 0.f;

    if (layout == LayoutMode::Absolute) {
        for (auto& child : children) {
            if (child && child->visible) child->Update();
        }
        return;
    }

    bool const vertical = layout == LayoutMode::Vertical;
    float      cursor   = vertical ? contentY : contentX;
    float      extent   = 0.f; // used size along the layout axis, margins included

    for (auto& child : children) {
        if (!child || !child->visible) continue;

        float const lead  = vertical ? child->LeadTop() : child->LeadLeft();
        float const trail = vertical ? child->GapBottom() : child->GapRight();
        cursor += lead;
        extent += lead;

        // A widget that sizes itself (a label) must know its size before it can
        // be placed.
        if (child->width <= 0.f || child->height <= 0.f) child->Update();

        if (vertical) {
            child->x = contentX;
            if (child->width <= 0.f && contentW > 0.f) child->width = contentW;
            child->y = cursor;
            cursor += child->height;
            extent += child->height;
            if (trail > 0.f) {
                cursor += trail;
                extent += trail;
            }
        } else {
            child->y = contentY;
            if (child->height <= 0.f && contentH > 0.f) child->height = contentH;
            child->x = cursor;
            cursor += child->width;
            extent += child->width;
            if (trail > 0.f) {
                cursor += trail;
                extent += trail;
            }
        }
        child->Update();
    }

    if (vertical) {
        if (height <= 0.f) height = extent + 2.f * inset;
    } else if (width <= 0.f) {
        width = extent + 2.f * inset;
    }
}

bool SilicaContainer::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));
    if (k == "layout" || k == "direction") {
        std::string const layoutName = property::ToLower(property::Trim(value));
        if (layoutName == "vertical" || layoutName == "column" || layoutName == "v") {
            layout = LayoutMode::Vertical;
            return true;
        }
        if (layoutName == "horizontal" || layoutName == "row" || layoutName == "h") {
            layout = LayoutMode::Horizontal;
            return true;
        }
        if (layoutName == "absolute" || layoutName == "none") {
            layout = LayoutMode::Absolute;
            return true;
        }
        return false;
    }
    return SilicaWidget::ApplyProperty(key, value);
}

void SilicaContainer::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    // Layout runs once per frame through Update (the host walks the tree before
    // rendering); repeating it here laid the whole subtree out twice.
    PaintSelf(context);
    for (auto const& child : PaintOrder()) {
        if (!child->visible) continue;
        // Widgets scrolled or clipped out of view paint nothing, so skip them
        // and their subtree. The pad keeps shadows and offset parts that poke
        // out of the box alive.
        constexpr float cullPad = 128.f;
        if (!RectVisibleInClip(
                child->x - cullPad,
                child->y - cullPad,
                child->width + 2.f * cullPad,
                child->height + 2.f * cullPad
            )) {
            continue;
        }

        // Keyframe transforms are render-only: the box is scaled around its
        // center for this draw and restored afterwards, so layout and hit
        // testing keep using the authored geometry.
        float const scale  = child->AnimationScale();
        bool const  scaled = std::fabs(scale - 1.f) > .001f;
        float const sx     = child->x;
        float const sy     = child->y;
        float const sw     = child->width;
        float const sh     = child->height;
        if (scaled && scale > 0.f) {
            float const cx = sx + sw * .5f;
            float const cy = sy + sh * .5f;
            child->width   = sw * scale;
            child->height  = sh * scale;
            child->x       = cx - child->width * .5f;
            child->y       = cy - child->height * .5f;
        }
        child->Render(context);
        if (scaled) {
            child->x      = sx;
            child->y      = sy;
            child->width  = sw;
            child->height = sh;
        }
    }
}

} // namespace silica
