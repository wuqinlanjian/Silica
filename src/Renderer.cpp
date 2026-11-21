#include "silica/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "silica/Property.h"

#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/FontRepository.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/core/utility/NonOwnerPointer.h"
#include "mc/deps/input/RectangleArea.h"

namespace silica {
namespace {

// Quarter-unit corner bands: at gui scale 2-4 that lands under a physical pixel,
// so short radii read as clean arcs instead of visible stairs.
constexpr float kRoundStep = .25f;

// Every fill used to flush itself, which turned a rounded box into dozens of
// submits. Fills of one color can share the queue (the flush carries a single
// color), so a run is only submitted when the color changes or another kind of
// draw has to interleave. Text is staged separately by the game, so each
// transition between text and fills submits the other side first.
struct FillBatch {
    MinecraftUIRenderContext* context = nullptr;
    Color                     color{};
    int                       count = 0;
};

// Very large runs are split so one batch cannot swamp the UI vertex buffer.
constexpr int kFillBatchLimit = 1024;

thread_local FillBatch                  g_fill;
thread_local bool                       g_textStaged  = false;
thread_local MinecraftUIRenderContext*  g_textContext = nullptr;

bool sameColor(Color const& a, Color const& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// A NaN or infinity reaching the game's vertex data can poison a whole batch,
// so every primitive rejects non-finite geometry up front.
bool finiteRect(float x, float y, float w, float h) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(w) && std::isfinite(h);
}

void flushFill(MinecraftUIRenderContext& context) {
    if (g_fill.count <= 0) return;
    MinecraftUIRenderContext& target = g_fill.context ? *g_fill.context : context;
    target.flushImages(mce::Color{1.f, 1.f, 1.f, 1.f}, 1.f, HashedString{"ui_fillColor"});
    g_fill = FillBatch{};
}

void flushStagedText(MinecraftUIRenderContext& context) {
    if (!g_textStaged) return;
    MinecraftUIRenderContext& target = g_textContext ? *g_textContext : context;
    target.flushText(0.f, std::nullopt);
    g_textStaged  = false;
    g_textContext = nullptr;
}

mce::Color toMce(Color const& value) { return mce::Color{value.r, value.g, value.b, 1.f}; }

Color lerp(Color const& from, Color const& to, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return Color{
        from.r + (to.r - from.r) * t,
        from.g + (to.g - from.g) * t,
        from.b + (to.b - from.b) * t,
        from.a + (to.a - from.a) * t,
    };
}

bool fits(Font& font, std::string_view text, float width, float size) {
    return static_cast<float>(font.getLineLength(text, size, false)) <= width;
}

std::string truncate(Font& font, std::string_view text, float width, float size) {
    if (width <= 0.f || text.empty() || fits(font, text, width, size)) return std::string(text);
    constexpr std::string_view dots = "...";
    float const dotsWidth = static_cast<float>(font.getLineLength(dots, size, false));
    if (dotsWidth > width) return {};
    size_t end = text.size();
    while (end > 0) {
        --end;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
        if (fits(font, text.substr(0, end), width - dotsWidth, size)) {
            return std::string(text.substr(0, end)) + std::string(dots);
        }
    }
    return std::string(dots);
}

// Inset from the box edge of a quarter-circle corner at a distance from the
// edge (0 at the edge, radius at the end of the arc).
float cornerInset(float radius, float distance) {
    if (radius <= 0.f || distance >= radius) return 0.f;
    float const dy = radius - std::max(0.f, distance);
    return std::clamp(radius - std::sqrt(std::max(0.f, radius * radius - dy * dy)), 0.f, radius);
}

void fillRoundRectStep(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    Color                     color,
    float                     step
) {
    if (w <= 0.f || h <= 0.f || color.a <= 0.f) return;
    float const r = std::min({radius, w * .5f, h * .5f});
    if (r <= .5f) {
        FillRect(context, x, y, w, h, color);
        return;
    }
    step = std::max(.25f, step);

    FillRect(context, x, y + r, w, h - 2.f * r, color);
    for (float row = 0.f; row < r; row += step) {
        float const band   = std::min(step, r - row);
        float const center = row + band * .5f;
        float const inset  = cornerInset(r, center);
        FillRect(context, x + inset, y + row, w - 2.f * inset, band, color);
        FillRect(context, x + inset, y + h - row - band, w - 2.f * inset, band, color);
    }
}

void drawRoundFrameStep(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    Color                     color,
    float                     thickness,
    float                     step
) {
    if (w <= 0.f || h <= 0.f || color.a <= 0.f || thickness <= 0.f) return;
    float const r = std::min({radius, w * .5f, h * .5f});
    if (r <= .5f) {
        DrawFrame(context, x, y, w, h, color, thickness);
        return;
    }
    step = std::max(.25f, step);

    float const t  = std::min({thickness, w * .5f, h * .5f, r});
    float const ri = std::max(0.f, r - t);
    FillRect(context, x + r, y, w - 2.f * r, t, color);
    FillRect(context, x + r, y + h - t, w - 2.f * r, t, color);
    FillRect(context, x, y + r, t, h - 2.f * r, color);
    FillRect(context, x + w - t, y + r, t, h - 2.f * r, color);

    for (float row = 0.f; row < r; row += step) {
        float const band   = std::min(step, r - row);
        float const center = row + band * .5f;
        float const outer  = cornerInset(r, center);
        float const topY   = y + row;
        float const botY   = y + h - row - band;

        // Above the inner box the corner is solid; the straight edge covers
        // the middle, so only the two corner caps are painted here. Inside the
        // inner box the band runs between the outer and inner arcs, and below
        // the inner arc the side edge takes over.
        if (center < t) {
            if (r > outer) {
                FillRect(context, x + outer, topY, r - outer, band, color);
                FillRect(context, x + outer, botY, r - outer, band, color);
                FillRect(context, x + w - r, topY, r - outer, band, color);
                FillRect(context, x + w - r, botY, r - outer, band, color);
            }
            continue;
        }
        float const inner  = cornerInset(ri, center - t);
        float const leftTo = t + inner;
        if (leftTo > outer) {
            FillRect(context, x + outer, topY, leftTo - outer, band, color);
            FillRect(context, x + outer, botY, leftTo - outer, band, color);
        }
        float const rightFrom = w - t - inner;
        float const rightTo   = w - outer;
        if (rightFrom < rightTo) {
            FillRect(context, x + rightFrom, topY, rightTo - rightFrom, band, color);
            FillRect(context, x + rightFrom, botY, rightTo - rightFrom, band, color);
        }
    }
}

} // namespace

Font* ResolveFont(MinecraftUIRenderContext& context, std::string_view family) {
    // The debug text handle names the default UI font; resolving it through the
    // handle's repository avoids any game import for the font table.
    FontHandle& handle = context.mDebugTextFontHandle;
    if (!handle.mIsDummyHandle) {
        auto const& repository =
            static_cast<Bedrock::NonOwnerPointer<FontRepository> const&>(handle.mFontRepository);
        if (auto* fonts = repository.get()) {
            auto const& loaded =
                static_cast<std::vector<gsl::not_null<std::shared_ptr<Font>>> const&>(fonts->mLoadedFonts);

            std::uint64_t id    = std::numeric_limits<std::uint64_t>::max();
            std::string   asked = property::ToLower(property::Trim(family));
            if (!asked.empty() && asked != "default") {
                if (asked == "smooth" || asked == "smooth-latin") {
                    id = static_cast<std::uint64_t const&>(fonts->mSmoothFontLatinFontId);
                } else if (asked == "unicode") {
                    id = static_cast<std::uint64_t const&>(fonts->mUnicodeFontId);
                } else if (asked == "rune") {
                    id = static_cast<std::uint64_t const&>(fonts->mRuneFontId);
                } else if (asked == "ui") {
                    id = static_cast<std::uint64_t const&>(fonts->mUIFontId);
                } else {
                    auto const& names = static_cast<std::unordered_map<std::string, std::uint64_t> const&>(
                        fonts->mFontNameToIdentifier
                    );
                    std::string const exact(property::Trim(family));
                    auto              found = names.find(exact);
                    if (found != names.end()) {
                        id = found->second;
                    } else {
                        for (auto const& entry : names) {
                            if (property::ToLower(entry.first) == asked) {
                                id = entry.second;
                                break;
                            }
                        }
                    }
                }
            }
            if (id == std::numeric_limits<std::uint64_t>::max()) id = handle.mFontId;
            if (id < loaded.size()) return loaded[id].get().get();
        }
    }
    auto const& fallback = static_cast<std::shared_ptr<Font> const&>(handle.mDefaultFont);
    return fallback.get();
}

float TextWidth(MinecraftUIRenderContext& context, std::string_view text, float size, std::string_view family) {
    if (text.empty()) return 0.f;
    Font* font = ResolveFont(context, family);
    if (!font) return 0.f;
    return static_cast<float>(font->getLineLength(text, size, false));
}

void FillRect(MinecraftUIRenderContext& context, float x, float y, float w, float h, Color color) {
    if (!finiteRect(x, y, w, h) || w <= 0.f || h <= 0.f || color.a <= 0.f || ClipIsEmpty()) return;

    // Text staged earlier paints below this rect, so submit it before queueing.
    flushStagedText(context);
    if (g_fill.count > 0
        && (g_fill.context != &context || !sameColor(g_fill.color, color) || g_fill.count >= kFillBatchLimit)) {
        flushFill(context);
    }
    if (g_fill.count <= 0) {
        g_fill.context = &context;
        g_fill.color   = color;
    }
    ++g_fill.count;
    context.fillRectangle(RectangleArea{x, x + w, y, y + h}, toMce(color), color.a);
}

void DrawFrame(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    Color                     color,
    float                     thickness
) {
    if (w <= 0.f || h <= 0.f || color.a <= 0.f || thickness <= 0.f) return;
    float const t = std::min({thickness, w * .5f, h * .5f});
    FillRect(context, x, y, w, t, color);
    FillRect(context, x, y + h - t, w, t, color);
    FillRect(context, x, y + t, t, h - 2 * t, color);
    FillRect(context, x + w - t, y + t, t, h - 2 * t, color);
}

void FillRoundRect(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    Color                     color
) {
    fillRoundRectStep(context, x, y, w, h, radius, color, kRoundStep);
}

void DrawRoundFrame(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    Color                     color,
    float                     thickness
) {
    drawRoundFrameStep(context, x, y, w, h, radius, color, thickness, kRoundStep);
}

namespace {

std::vector<GradientStop> normalizedStops(GradientSpec const& spec) {
    if (spec.stops.empty()) return {{0.f, spec.from}, {1.f, spec.to}};

    std::vector<GradientStop> stops = spec.stops;
    std::stable_sort(stops.begin(), stops.end(), [](GradientStop const& a, GradientStop const& b) {
        return a.position < b.position;
    });
    if (stops.front().position > 0.f) stops.insert(stops.begin(), {0.f, stops.front().color});
    if (stops.back().position < 1.f) stops.push_back({1.f, stops.back().color});
    return stops;
}

Color sampleStops(std::vector<GradientStop> const& stops, float t) {
    t = std::clamp(t, 0.f, 1.f);
    if (t <= stops.front().position) return stops.front().color;
    for (size_t i = 1; i < stops.size(); ++i) {
        if (t > stops[i].position) continue;
        float const span = stops[i].position - stops[i - 1].position;
        float const u    = span > 0.f ? (t - stops[i - 1].position) / span : 0.f;
        return lerp(stops[i - 1].color, stops[i].color, u);
    }
    return stops.back().color;
}

// One GUI unit per band until the box gets large; every band is its own flush,
// so big areas step up to 2-5 units and cap the draw count near 220.
float gradientBandStep(float extent) {
    float const step = extent <= 192.f ? 1.f : std::ceil(extent / 224.f);
    return std::max(1.f, step);
}

} // namespace

void FillGradientRect(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    Color                     from,
    Color                     to,
    bool                      horizontal
) {
    GradientSpec spec;
    spec.from       = from;
    spec.to         = to;
    spec.horizontal = horizontal;
    spec.stops      = {{0.f, from}, {1.f, to}};
    FillLinearGradient(context, x, y, w, h, spec);
}

void FillRoundGradientRect(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    Color                     from,
    Color                     to,
    bool                      horizontal
) {
    GradientSpec spec;
    spec.from       = from;
    spec.to         = to;
    spec.horizontal = horizontal;
    spec.stops      = {{0.f, from}, {1.f, to}};
    FillRoundLinearGradient(context, x, y, w, h, radius, spec);
}

void FillLinearGradient(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    GradientSpec const&       spec
) {
    if (!finiteRect(x, y, w, h) || w <= 0.f || h <= 0.f) return;
    std::vector<GradientStop> const stops = normalizedStops(spec);
    if (stops.empty()) return;

    float const extent = spec.horizontal ? w : h;
    float const step   = gradientBandStep(extent);
    for (float at = 0.f; at < extent; at += step) {
        float const band  = std::min(step, extent - at);
        Color const color = sampleStops(stops, (at + band * .5f) / extent);
        if (color.a <= 0.f) continue;
        if (spec.horizontal) FillRect(context, x + at, y, band, h, color);
        else FillRect(context, x, y + at, w, band, color);
    }
}

void FillRoundLinearGradient(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    GradientSpec const&       spec
) {
    float const r = std::min({radius, w * .5f, h * .5f});
    if (r <= .5f) {
        FillLinearGradient(context, x, y, w, h, spec);
        return;
    }
    if (w <= 0.f || h <= 0.f) return;

    std::vector<GradientStop> const stops = normalizedStops(spec);
    if (stops.empty()) return;

    float const extent = spec.horizontal ? w : h;
    float const step   = gradientBandStep(extent);
    for (float at = 0.f; at < extent; at += step) {
        float const band   = std::min(step, extent - at);
        float const center = at + band * .5f;
        Color const color  = sampleStops(stops, center / extent);
        if (color.a <= 0.f) continue;
        if (spec.horizontal) {
            float const inset = cornerInset(r, std::min(center, w - center));
            FillRect(context, x + at, y + inset, band, h - 2.f * inset, color);
        } else {
            float const inset = cornerInset(r, std::min(center, h - center));
            FillRect(context, x + inset, y + at, w - 2.f * inset, band, color);
        }
    }
}

void DrawShadow(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    float                     radius,
    ShadowSpec const&         shadow,
    float                     opacity
) {
    Color const color = shadow.color.ScaleAlpha(opacity);
    if (!finiteRect(x, y, w, h) || !std::isfinite(radius) || color.a <= 0.f || w <= 0.f || h <= 0.f) return;

    float const blur   = std::max(0.f, shadow.blur);
    float const spread = std::max(0.f, shadow.spread);
    float const reach  = blur + spread;
    int const   steps  =
        reach > 0.f ? std::clamp(static_cast<int>(std::ceil(reach / 1.25f)), 2, 12) : 1;
    float const layerAlpha = color.a / static_cast<float>(steps);
    // Soft edges tolerate coarser bands, which keeps the extra layers cheap:
    // each layer is only a fill, and the falloff comes from stacking them.
    float const step = std::max(kRoundStep, (radius + reach) / 16.f);

    for (int i = steps; i >= 1; --i) {
        float const t      = static_cast<float>(i) / static_cast<float>(steps);
        float const expand = spread + blur * t;
        fillRoundRectStep(
            context,
            x - expand + shadow.dx,
            y - expand + shadow.dy,
            w + 2.f * expand,
            h + 2.f * expand,
            radius + expand,
            color.WithAlpha(layerAlpha),
            step
        );
    }
}

void DrawText(
    MinecraftUIRenderContext& context,
    float                     x,
    float                     y,
    float                     width,
    std::string_view          text,
    Color                     color,
    float                     size,
    ::ui::TextAlignment       align,
    std::string_view          family
) {
    if (!finiteRect(x, y, width, size) || text.empty() || width <= 0.f || color.a <= 0.f || size <= 0.f
        || ClipIsEmpty()) {
        return;
    }
    Font* font = ResolveFont(context, family);
    if (!font) return;

    std::string shown = truncate(*font, text, width, size);
    if (shown.empty()) return;

    // Fills queued earlier paint below this text; submit them first, then keep
    // the text staged so the next fill submits it before drawing above it.
    flushFill(context);

    TextMeasureData const measure{size, 0.f, false, false, false, align};
    CaretMeasureData const caret{-1, false};
    context.drawText(
        *font,
        RectangleArea{x, x + width, y, y + 14.f * size},
        std::move(shown),
        toMce(color),
        size,
        align,
        measure,
        caret
    );
    g_textStaged  = true;
    g_textContext = &context;
}

void FlushText(MinecraftUIRenderContext& context) { flushStagedText(context); }

void FlushDraws(MinecraftUIRenderContext& context) {
    flushFill(context);
    flushStagedText(context);
}

namespace {

// The game keeps a single clipping rectangle, so a nested PushClip would let
// the inner rect escape the outer one. Track the effective clip ourselves and
// feed the game the intersection; the game's saved state is only touched at
// the outermost level.
struct ClipRect {
    float x0 = 0.f, x1 = 0.f, y0 = 0.f, y1 = 0.f;
};

thread_local std::vector<ClipRect> g_clipStack;

bool isEmpty(ClipRect const& rect) { return rect.x1 <= rect.x0 || rect.y1 <= rect.y0; }

void applyClip(MinecraftUIRenderContext& context, ClipRect const& rect) {
    // The game treats a zero-area clipping rectangle as "no clipping", so a
    // widget scrolled wholly out of a viewport still painted its text (the
    // lingering placeholder over a list header). Keeping the last non-empty
    // rectangle is safe: an empty intersection means this subtree already lies
    // outside that rectangle.
    if (isEmpty(rect)) return;
    context.setClippingRectangle(RectangleArea{rect.x0, rect.x1, rect.y0, rect.y1});
}

} // namespace

void PushClip(MinecraftUIRenderContext& context, float x, float y, float w, float h) {
    // Everything queued so far belongs to the current clip; submit it before
    // the game starts honoring the new rectangle.
    FlushDraws(context);
    // A non-finite box has no meaningful intersection: clip it all away rather
    // than feeding the game NaN bounds.
    if (!finiteRect(x, y, w, h)) {
        x = 0.f;
        y = 0.f;
        w = 0.f;
        h = 0.f;
    }
    // A degenerate rect clips everything drawn in this range instead of leaving
    // the previous clip active.
    ClipRect rect{x, x + std::max(0.f, w), y, y + std::max(0.f, h)};
    if (g_clipStack.empty()) {
        context.saveCurrentClippingRectangle();
    } else {
        ClipRect const& parent = g_clipStack.back();
        rect.x0 = std::max(parent.x0, rect.x0);
        rect.x1 = std::max(parent.x0, std::min(parent.x1, rect.x1));
        rect.y0 = std::max(parent.y0, rect.y0);
        rect.y1 = std::max(parent.y0, std::min(parent.y1, rect.y1));
    }
    g_clipStack.push_back(rect);
    applyClip(context, rect);
}

void PopClip(MinecraftUIRenderContext& context) {
    // Clipped content must be submitted while its clip is still active.
    FlushDraws(context);
    if (g_clipStack.empty()) return;
    g_clipStack.pop_back();
    if (g_clipStack.empty()) {
        context.restoreSavedClippingRectangle();
    } else {
        applyClip(context, g_clipStack.back());
    }
}

bool RectVisibleInClip(float x, float y, float w, float h) {
    if (g_clipStack.empty()) return true;
    ClipRect const& clip = g_clipStack.back();
    return x + w > clip.x0 && x < clip.x1 && y + h > clip.y0 && y < clip.y1;
}

bool ClipIsEmpty() { return !g_clipStack.empty() && isEmpty(g_clipStack.back()); }

} // namespace silica
