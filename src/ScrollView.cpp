#include "silica/ScrollView.h"

#include "silica/Property.h"
#include "silica/Renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace silica {
namespace {

constexpr float kScrollbarMinLen = 16.f;

double secondsNow() {
    using clock             = std::chrono::steady_clock;
    static clock::time_point const start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

} // namespace

SilicaScrollView::SilicaScrollView() {
    m_content          = std::make_shared<SilicaContainer>();
    m_content->layout  = LayoutMode::Vertical;
    m_content->padding = 0.f;
    m_content->parent  = this;
    children.push_back(m_content);
}

bool SilicaScrollView::AddChild(WidgetPtr child) {
    if (!m_content) return false;
    if (!m_content->AddChild(std::move(child))) return false;
    Update();
    return true;
}

void SilicaScrollView::Clear() {
    if (m_content) m_content->Clear();
    scrollOffsetY   = 0.f;
    scrollTargetY   = 0.f;
    m_contentHeight = 0.f;
    m_draggingBar   = false;
}

size_t SilicaScrollView::Count() const { return m_content ? m_content->Count() : 0; }

std::vector<WidgetPtr> const& SilicaScrollView::Children() const {
    return m_content ? m_content->Children() : m_noChildren;
}

float SilicaScrollView::MaxOffset() const {
    float const inset = std::max(0.f, padding);
    float const viewH = height > 0.f ? std::max(0.f, height - 2.f * inset) : 0.f;
    if (viewH <= 0.f) return 0.f;
    return std::max(0.f, m_contentHeight - viewH);
}

SilicaScrollView::BarGeometry SilicaScrollView::Bar() const {
    BarGeometry bar;
    if (!showScrollbar) return bar;

    float const barWidth = std::max(.5f, scrollbarWidth);
    float const barInset = std::max(0.f, scrollbarInset);
    if (width <= 2.f * barInset + barWidth) return bar;

    float const inset  = std::max(0.f, padding);
    float const viewH  = height > 0.f ? std::max(0.f, height - 2.f * inset) : 0.f;
    float const trackH = height > 0.f ? std::max(0.f, height - 2.f * barInset) : 0.f;
    if (viewH <= 0.f || trackH <= 0.f || m_contentHeight <= 0.f) return bar;

    float const maxOffset = MaxOffset();
    if (maxOffset <= 0.f) return bar;

    float const minLen = std::min(kScrollbarMinLen, trackH);
    float const thumbH = std::clamp(trackH * viewH / m_contentHeight, minLen, trackH);
    float const ratio  = std::clamp(scrollOffsetY / maxOffset, 0.f, 1.f);

    bar.trackX = x + width - barInset - barWidth;
    bar.trackY = y + barInset;
    bar.trackH = trackH;
    bar.thumbH = thumbH;
    bar.thumbY = bar.trackY + (trackH - thumbH) * ratio;
    bar.visible = true;
    return bar;
}

bool SilicaScrollView::InBarStrip(float px, float py) const {
    BarGeometry const bar = Bar();
    if (!bar.visible) return false;
    // The visual bar is thin; widen the grab strip to the left so it is easy to
    // hit without stealing clicks from content further away.
    float const slop = std::max(6.f, std::max(.5f, scrollbarWidth) * 1.5f);
    return py >= bar.trackY && py <= bar.trackY + bar.trackH && px >= bar.trackX - slop
        && px <= x + width;
}

void SilicaScrollView::DragTo(float py) {
    BarGeometry const bar = Bar();
    if (!bar.visible) return;

    float const travel = bar.trackH - bar.thumbH;
    float const thumbY = travel > 0.f ? std::clamp(py - m_dragGrab, bar.trackY, bar.trackY + travel)
                                      : bar.trackY;
    float const ratio  = travel > 0.f ? (thumbY - bar.trackY) / travel : 0.f;

    scrollTargetY = ratio * MaxOffset();
    if (!smoothScroll) {
        scrollOffsetY = scrollTargetY;
        Update();
    }
}

void SilicaScrollView::PlaceContent() {
    if (!m_content) return;

    float const inset = std::max(0.f, padding);
    float const viewW = std::max(0.f, width - 2.f * inset);

    m_content->x      = x + inset;
    m_content->width  = viewW > 0.f ? viewW : std::max(0.f, width);
    m_content->height = 0.f; // auto-size to the stacked children
    m_content->y      = y + inset - scrollOffsetY;
    m_content->Update();
    m_contentHeight = m_content->height;

    float const maxOffset = MaxOffset();
    float const clamped   = std::clamp(scrollOffsetY, 0.f, maxOffset);
    scrollTargetY         = std::clamp(scrollTargetY, 0.f, maxOffset);
    if (clamped != scrollOffsetY) {
        scrollOffsetY = clamped;
        m_content->y  = y + inset - scrollOffsetY;
        m_content->Update();
    }
}

void SilicaScrollView::Update() {
    if (!visible) return;

    float const maxOffset = MaxOffset();
    scrollTargetY         = std::clamp(scrollTargetY, 0.f, maxOffset);

    double const now = secondsNow();
    float const  dt  = m_lastTick >= 0.0
        ? static_cast<float>(std::clamp(now - m_lastTick, 1.0 / 240.0, .1))
        : 1.f / 60.f;
    m_lastTick = now;

    if (!smoothScroll || scrollSmoothing <= 0.f) {
        scrollOffsetY = scrollTargetY;
    } else if (scrollOffsetY != scrollTargetY) {
        float const easing = 1.f - std::exp(-scrollSmoothing * dt);
        scrollOffsetY += (scrollTargetY - scrollOffsetY) * easing;
        if (std::fabs(scrollTargetY - scrollOffsetY) < .25f) scrollOffsetY = scrollTargetY;
    }
    scrollOffsetY = std::clamp(scrollOffsetY, 0.f, maxOffset);

    PlaceContent();
}

void SilicaScrollView::Render(MinecraftUIRenderContext& context) {
    if (!visible) return;
    Update();

    PaintSelf(context);

    float const inset = std::max(0.f, padding);
    PushClip(
        context,
        x + inset,
        y + inset,
        std::max(0.f, width - 2.f * inset),
        std::max(0.f, height - 2.f * inset)
    );
    if (m_content && m_content->visible) m_content->Render(context);
    PopClip(context);

    if (showScrollbar) DrawScrollbar(context);
}

bool SilicaScrollView::OnMouseDown(float px, float py) {
    if (!visible || !InBarStrip(px, py)) return false;

    BarGeometry const bar = Bar();
    if (!bar.visible) return false;

    if (py >= bar.thumbY && py <= bar.thumbY + bar.thumbH) {
        m_dragGrab = py - bar.thumbY;
    } else {
        // Clicking the track pages to the cursor: center the thumb under it.
        m_dragGrab = bar.thumbH * .5f;
        DragTo(py);
    }
    m_draggingBar = true;
    return true;
}

bool SilicaScrollView::OnMouseMove(float px, float py) {
    (void)px;
    if (!m_draggingBar) return false;
    DragTo(py);
    return true;
}

bool SilicaScrollView::OnMouseUp(float px, float py, bool inside) {
    (void)px;
    (void)py;
    (void)inside;
    bool const wasDragging = m_draggingBar;
    m_draggingBar          = false;
    return wasDragging;
}

bool SilicaScrollView::OnMouseWheel(float x, float y, float delta) {
    (void)x;
    (void)y;
    if (!visible || delta == 0.f) return false;
    if (MaxOffset() <= 0.f) return false;

    scrollTargetY = std::clamp(scrollTargetY - delta * scrollStep, 0.f, MaxOffset());
    if (!smoothScroll || scrollSmoothing <= 0.f) scrollOffsetY = scrollTargetY;
    Update();
    return true;
}

void SilicaScrollView::DrawScrollbar(MinecraftUIRenderContext& context) {
    BarGeometry const bar = Bar();
    if (!bar.visible) return;

    float const barWidth = std::max(.5f, scrollbarWidth);

    auto const drawBar = [&](float py, float ph, Color color) {
        if (scrollbarRadius > 0.f) {
            FillRoundRect(context, bar.trackX, py, barWidth, ph, scrollbarRadius, color);
        } else {
            FillRect(context, bar.trackX, py, barWidth, ph, color);
        }
    };

    Color thumb = scrollbarThumbColor;
    if (m_draggingBar) thumb.a = std::min(1.f, thumb.a * 1.35f);

    drawBar(bar.trackY, bar.trackH, scrollbarTrackColor.ScaleAlpha(PaintAlpha()));
    drawBar(bar.thumbY, bar.thumbH, thumb.ScaleAlpha(PaintAlpha()));
}

bool SilicaScrollView::ApplyProperty(std::string_view key, std::string_view value) {
    std::string const k = property::ToLower(property::Trim(key));
    if (k == "scroll-step" || k == "scroll-step-y" || k == "wheel-step") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        scrollStep = *number;
        return true;
    }
    if (k == "smooth-scroll" || k == "scroll-smooth") {
        auto smooth = property::ParseBool(value);
        if (!smooth) return false;
        smoothScroll = *smooth;
        if (!smoothScroll) scrollOffsetY = scrollTargetY;
        return true;
    }
    if (k == "scroll-smoothing" || k == "scroll-easing") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        scrollSmoothing = *number;
        return true;
    }
    if (k == "scrollbar" || k == "show-scrollbar") {
        auto shown = property::ParseBool(value);
        if (!shown) return false;
        showScrollbar = *shown;
        return true;
    }
    if (k == "scrollbar-width") {
        auto number = property::ParseNumber(value);
        if (!number || *number <= 0.f) return false;
        scrollbarWidth = *number;
        return true;
    }
    if (k == "scrollbar-inset") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        scrollbarInset = *number;
        return true;
    }
    if (k == "scrollbar-radius" || k == "scrollbar-thumb-radius") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        scrollbarRadius = *number;
        return true;
    }
    if (k == "scrollbar-color" || k == "scrollbar-track-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        scrollbarTrackColor = *color;
        return true;
    }
    if (k == "scrollbar-thumb-color") {
        auto color = property::ParseColor(value);
        if (!color) return false;
        scrollbarThumbColor = *color;
        return true;
    }
    if (k == "offset" || k == "scroll-offset" || k == "scroll-y") {
        auto number = property::ParseNumber(value);
        if (!number || *number < 0.f) return false;
        scrollOffsetY = *number;
        scrollTargetY = scrollOffsetY;
        return true;
    }
    // Layout of the viewport itself is meaningless (content stacks vertically);
    // accept and ignore it so a shared style block does not fail to compile.
    if (k == "layout" || k == "direction") return true;
    return SilicaWidget::ApplyProperty(key, value);
}

} // namespace silica
