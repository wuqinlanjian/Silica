#pragma once

#include "Container.h"

namespace silica {

// A clipped, vertically scrollable viewport. Children added to it go into an
// internal content container (vertical layout, auto height) that is shifted by
// scrollOffsetY; painting and hit testing are both limited to the viewport, so
// content scrolled out of view is neither drawn nor clickable.
//
// The wheel and the scrollbar only move scrollTargetY; every Update eases
// scrollOffsetY toward it, so both the content and the thumb glide instead of
// jumping. The scrollbar thumb can be grabbed and dragged directly.
class SilicaScrollView : public SilicaContainer {
public:
    SilicaScrollView();

    float scrollOffsetY = 0.f; // rendered offset, animated toward scrollTargetY
    float scrollTargetY = 0.f; // requested offset (wheel / scrollbar drag)
    float scrollStep    = 24.f;
    bool  showScrollbar = true;

    // Exponential easing toward the target; 0 or false snaps instantly.
    bool  smoothScroll    = true;
    float scrollSmoothing = 22.f; // approach rate per second

    // Scrollbar metrics exposed to the declarative layer.
    float scrollbarWidth      = 3.f;
    float scrollbarInset      = 3.f;
    float scrollbarRadius     = 0.f;
    Color scrollbarTrackColor = Color{1.f, 1.f, 1.f, .10f};
    Color scrollbarThumbColor = Color{1.f, 1.f, 1.f, .38f};

    bool AddChild(WidgetPtr child) override;
    void Clear() override;

    [[nodiscard]] size_t                        Count() const override;
    [[nodiscard]] std::vector<WidgetPtr> const& Children() const override;

    [[nodiscard]] std::shared_ptr<SilicaContainer> const& Content() const { return m_content; }
    [[nodiscard]] float                                   ContentHeight() const { return m_contentHeight; }
    [[nodiscard]] bool DraggingScrollbar() const { return m_draggingBar; }

    void Update() override;
    void Render(MinecraftUIRenderContext& context) override;
    bool OnMouseDown(float x, float y) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseUp(float x, float y, bool inside) override;
    bool OnMouseWheel(float x, float y, float delta) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;

private:
    struct BarGeometry {
        float trackX = 0.f, trackY = 0.f, trackH = 0.f;
        float thumbY = 0.f, thumbH = 0.f;
        bool  visible = false;
    };

    [[nodiscard]] float       MaxOffset() const;
    [[nodiscard]] BarGeometry Bar() const;
    [[nodiscard]] bool        InBarStrip(float px, float py) const;
    void                      DragTo(float py);
    void                      PlaceContent();
    void                      DrawScrollbar(MinecraftUIRenderContext& context);

    std::shared_ptr<SilicaContainer> m_content; // a SilicaContainer, also the only child
    float                            m_contentHeight = 0.f;
    std::vector<WidgetPtr>           m_noChildren;

    bool   m_draggingBar = false;
    float  m_dragGrab    = 0.f;
    double m_lastTick    = -1.0;
};

} // namespace silica
