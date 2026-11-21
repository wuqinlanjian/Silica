#include "silica/Tree.h"

#include "silica/Container.h"

namespace silica {

WidgetPtr FindByName(WidgetPtr const& root, std::string_view name) {
    if (!root) return nullptr;
    if (root->name == name) return root;
    if (auto container = std::dynamic_pointer_cast<SilicaContainer>(root)) {
        for (auto const& child : container->Children()) {
            if (auto found = FindByName(child, name)) return found;
        }
    }
    return nullptr;
}

WidgetPtr HitTest(WidgetPtr const& root, float x, float y) {
    if (!root || !root->visible) return nullptr;
    // Bounds check first: it is also the logical clip, so content scrolled out
    // of a ScrollView (or positioned outside any container) is never hit.
    if (!root->Contains(x, y)) return nullptr;
    if (auto container = std::dynamic_pointer_cast<SilicaContainer>(root)) {
        auto const& order = container->PaintOrder();
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            if (auto hit = HitTest(*it, x, y)) return hit;
        }
    }
    return root;
}

} // namespace silica
