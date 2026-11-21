#pragma once

#include "Widget.h"

#include <string_view>

namespace silica {

// Depth-first lookup by the `name:` property; empty when nothing matches.
WidgetPtr FindByName(WidgetPtr const& root, std::string_view name);

// Deepest visible widget whose rectangle contains the point. Later siblings win
// over earlier ones; nullptr when the point misses the tree.
WidgetPtr HitTest(WidgetPtr const& root, float x, float y);

} // namespace silica
