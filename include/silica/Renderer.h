#pragma once

#include "Color.h"
#include "Style.h"

#include <string>
#include <string_view>

#include "mc/client/gui/TextAlignment.h"

class Font;
class MinecraftUIRenderContext;

namespace silica {

// Resolves a font family against the game's font table. An empty family (or
// an unknown name) yields the default UI font; aliases default/smooth/
// unicode/rune/ui map onto the built-in font slots.
Font* ResolveFont(MinecraftUIRenderContext& context, std::string_view family = {});

float TextWidth(
    MinecraftUIRenderContext& context,
    std::string_view          text,
    float                     size   = 1.f,
    std::string_view          family = {}
);

// Fills are batched: consecutive rects of the same color share one flush, so a
// rounded box (body + corner bands) or a frame costs a single flush instead of
// dozens. A different color, a text draw, a texture or a clip change submits
// the pending run first, which keeps the paint order intact. Call FlushDraws
// once after the last widget of a frame.
void FillRect(MinecraftUIRenderContext& context, float x, float y, float w, float h, Color color);

// Border drawn inside the rect, so a framed widget keeps its own size.
void DrawFrame(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    Color                     color,
    float                     thickness = 1.f
);

// Rounded variants: the radius is clamped to half the smaller side, and 0
// falls back to the plain rect/frame. Corners are rasterized as horizontal
// strips in half-unit bands, so they do not need a texture or a custom
// pipeline.
void FillRoundRect(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    Color                     color
);
void DrawRoundFrame(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    Color                     color,
    float                     thickness = 1.f
);

// Linear gradient between two stops, either top to bottom or left to right.
// The rounded variant clips the stops to the same corner arcs as FillRoundRect.
void FillGradientRect(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    Color                     from,
    Color                     to,
    bool                      horizontal = false
);
void FillRoundGradientRect(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    Color                     from,
    Color                     to,
    bool                      horizontal = false
);

// Pixel-level linear gradient: the box is cut into one-GUI-unit bands along
// the gradient axis (two or more on very large areas, since every band is a
// draw call) and each band samples the stop list with a linear interpolation.
void FillLinearGradient(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    GradientSpec const&       spec
);
void FillRoundLinearGradient(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    GradientSpec const&       spec
);

// Soft drop shadow assembled from expanding rounded layers; drawn before the
// fill, so the box covers the solid center.
void DrawShadow(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    ShadowSpec const&         shadow,
    float                     opacity = 1.f
);

// Text is accepted as one line and truncated with an ellipsis when it does not
// fit the width; there is no clipping in this renderer. Draws are staged and
// submitted by FlushText, so several labels can share one flush.
void DrawText(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     width,
    std::string_view          text,
    Color                     color,
    float                     size   = 1.f,
    ::ui::TextAlignment       align  = ::ui::TextAlignment::Left,
    std::string_view          family = {}
);

void FlushText(MinecraftUIRenderContext& context);

// Submits every pending fill and text draw. The host calls this once after the
// last widget of a frame; fills and text are otherwise submitted lazily at the
// next ordering boundary.
void FlushDraws(MinecraftUIRenderContext& context);

// Restricts everything drawn until the matching PopClip to the rect, using the
// render context's own clipping stack so nested clips compose.
void PushClip(MinecraftUIRenderContext& context, float x, float y, float w, float h);
void PopClip(MinecraftUIRenderContext& context);

// True while any part of the rect is inside the active clip (always true with
// no clip). Containers use it to skip widgets scrolled out of view entirely.
bool RectVisibleInClip(float x, float y, float w, float h);

// True when the active clip has no area at all, i.e. the whole subtree is
// scrolled out. Draw functions check it so nothing is queued for a region the
// game may render unclipped (some builds drop zero-area clipping rectangles).
bool ClipIsEmpty();

// Draws a whole texture (textures/items/diamond style path) stretched into the
// rect. Returns false when the texture is not available (unknown path, loading
// not finished, or the game-side texture API could not be resolved).
bool DrawTexture(
    MinecraftUIRenderContext& context,
    std::string_view          src,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    Color                     tint,
    float                     alpha
);

// Draws `src` with its corners kept at the slice sizes and its edges/center
// stretched (nine-slice). Returns false when the texture is unavailable.
bool DrawNineSlice(
    MinecraftUIRenderContext& context,
    std::string_view          src,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    NineSliceSpec const&      slices,
    Color                     tint,
    float                     alpha
);

} // namespace silica
