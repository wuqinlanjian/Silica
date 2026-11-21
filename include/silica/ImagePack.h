#pragma once

#include "Widget.h"

#include <string>
#include <string_view>

namespace silica::imagepack {

// Maps `src` to a texture path the game renderer understands. Plain texture
// names ("textures/ui/icon") pass through untouched. A source that names an
// image file ("file:ui/logo.png", "D:/art/cat.png", "ui/logo.png") is copied
// into <modDir>/resource_packs/silica_images, which LeviLamina registers as a
// resource pack source for every mod, and is mapped to its packaged
// "textures/silica/<hash>" name.
// Relative paths resolve against silica::runtime::RootPath(). Returns false
// while the file is missing; callers should simply retry on a later frame.
bool Resolve(std::string_view src, std::string& texturePath);

// Imports every image file referenced by the tree (Image widgets and Part
// skins) so the resources are on disk before the engine builds its pack stack.
// Call it right after compiling a document; rendering also resolves lazily.
void Warmup(WidgetPtr const& root);

} // namespace silica::imagepack
