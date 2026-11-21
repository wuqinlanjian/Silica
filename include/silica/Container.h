#pragma once

#include "Widget.h"

#include <vector>

namespace silica {

enum class LayoutMode {
    Absolute,
    Vertical,
    Horizontal,
};

// Lays its children out in absolute coordinates. In Vertical/Horizontal mode a
// child with zero width (resp. height) is stretched across the container, the
// padding is kept on every side, and the container size is auto-computed when
// it is left at zero. Hidden widgets take no space.
//
// Render and hit-test walk children in z-order (stable, insertion order breaks
// ties), so a later/larger-z sibling is drawn last and is hit first.
class SilicaContainer : public SilicaWidget {
public:
    LayoutMode layout = LayoutMode::Absolute;

    // Appends a child and returns it, so a tree can be built in one line.
    WidgetPtr Add(WidgetPtr child);
    bool      AddChild(WidgetPtr child) override;
    virtual void Clear();

    [[nodiscard]] virtual size_t                        Count() const { return children.size(); }
    [[nodiscard]] virtual std::vector<WidgetPtr> const& Children() const { return children; }

    // Children sorted by z-order. The result lives in a scratch buffer reused
    // across calls, so the per-frame paint walk does not allocate.
    [[nodiscard]] std::vector<WidgetPtr> const& PaintOrder() const;

    void Update() override;
    void Render(MinecraftUIRenderContext& context) override;
    bool ApplyProperty(std::string_view key, std::string_view value) override;

protected:
    std::vector<WidgetPtr> children;

private:
    mutable std::vector<WidgetPtr> m_paintOrder;
};

} // namespace silica
