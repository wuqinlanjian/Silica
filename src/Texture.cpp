#include "silica/Renderer.h"

#include "silica/ImagePack.h"
#include "silica/Runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>

#include "ll/api/memory/Symbol.h"

#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/renderer/hal/TextureDescription.h"

// The local LeviLamina import library does not export the texture plumbing
// (ResourceLocation construction/destruction and TexturePtr destruction). The
// linker redirects those imports onto these pointers, which are filled by name
// at first use. The call sites themselves stay ordinary SDK calls, so the
// compiler keeps generating the correct MSVC ABI for them.
extern "C" void* silicaResourceLocationCtor = nullptr;
extern "C" void* silicaResourceLocationDtor = nullptr;
extern "C" void* silicaTexturePtrDtor       = nullptr;

#pragma comment(linker, "/alternatename:__imp_??0ResourceLocation@@QEAA@VPathView@Core@@@Z=silicaResourceLocationCtor")
#pragma comment(linker, "/alternatename:__imp_??1ResourceLocation@@QEAA@XZ=silicaResourceLocationDtor")
#pragma comment(linker, "/alternatename:__imp_??1TexturePtr@mce@@QEAA@XZ=silicaTexturePtrDtor")

namespace silica {
namespace {

constexpr char kResourceLocationCtor[] = "??0ResourceLocation@@QEAA@VPathView@Core@@@Z";
constexpr char kResourceLocationDtor[] = "??1ResourceLocation@@QEAA@XZ";
constexpr char kTexturePtrDtor[]       = "??1TexturePtr@mce@@QEAA@XZ";

constexpr char kMaterial[] = "ui_textured_and_glcolor";

std::once_flag g_textureOnce;
bool           g_textureReady = false;

void resolveTextureApi() {
    silicaResourceLocationCtor = ll::memory::SymbolView(kResourceLocationCtor).resolve(true);
    silicaResourceLocationDtor = ll::memory::SymbolView(kResourceLocationDtor).resolve(true);
    silicaTexturePtrDtor       = ll::memory::SymbolView(kTexturePtrDtor).resolve(true);
    g_textureReady             = silicaResourceLocationCtor && silicaResourceLocationDtor && silicaTexturePtrDtor;
    if (!g_textureReady) {
        runtime::Log("silica: texture api unavailable (ResourceLocation/TexturePtr symbols not resolved)");
    }
}

struct TextureHandle {
    // Holding the texture data shared_ptr keeps the client texture alive for
    // as long as the caller draws from it.
    std::shared_ptr<BedrockTextureData const> data;

    [[nodiscard]] bool valid() const {
        return data && data->mIsMissingTexture != IsMissingTexture::Yes;
    }
};

bool AcquireTexture(MinecraftUIRenderContext& context, std::string_view src, TextureHandle& out) {
    if (src.empty()) return false;
    std::call_once(g_textureOnce, resolveTextureApi);
    if (!g_textureReady) return false;

    // External image files are repackaged as a generated resource pack; the
    // returned name is what the game can look up. Fails while the pack is still
    // being registered, and the widget retries on its next frame.
    std::string resolved;
    if (!imagepack::Resolve(src, resolved)) return false;

    ResourceLocation const location{Core::PathView{resolved}};
    mce::TexturePtr        texture = context.getTexture(location, false);
    out.data                       = texture.mClientTexture;
    // An unknown path resolves to the checkerboard placeholder; treat it as a
    // miss so the widget can draw its own fallback instead.
    return out.valid();
}

} // namespace

bool DrawTexture(
    MinecraftUIRenderContext& context,
    std::string_view          src,
    float                     x,
    float                     y,
    float                     w,
    float                     h,
    Color                     tint,
    float                     alpha
) {
    if (src.empty() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)
        || w <= 0.f || h <= 0.f || !std::isfinite(alpha) || alpha <= 0.f || ClipIsEmpty()) {
        return false;
    }

    TextureHandle texture;
    if (!AcquireTexture(context, src, texture)) return false;

    // Pending fills belong below this image and must not ride along with the
    // textured flush below.
    FlushDraws(context);

    context.drawImage(
        texture.data->mClientTexture,
        glm::vec2{x, y},
        glm::vec2{w, h},
        glm::vec2{0.f, 0.f},
        glm::vec2{1.f, 1.f},
        false
    );
    context.flushImages(
        mce::Color{tint.r, tint.g, tint.b, 1.f},
        std::clamp(alpha * tint.a, 0.f, 1.f),
        HashedString{kMaterial}
    );
    return true;
}

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
) {
    if (src.empty() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)
        || w <= 0.f || h <= 0.f || !std::isfinite(alpha) || alpha <= 0.f || ClipIsEmpty()) {
        return false;
    }

    TextureHandle texture;
    if (!AcquireTexture(context, src, texture)) return false;

    FlushDraws(context);

    auto const& description =
        static_cast<mce::TextureDescription const&>(texture.data->mTextureDescription);
    float const texW = static_cast<float>(description.mWidth);
    float const texH = static_cast<float>(description.mHeight);
    if (texW <= 0.f || texH <= 0.f) return false;

    float const srcLeft   = std::clamp(slices.left, 0.f, texW);
    float const srcTop    = std::clamp(slices.top, 0.f, texH);
    float const srcRight  = std::clamp(slices.right, 0.f, texW);
    float const srcBottom = std::clamp(slices.bottom, 0.f, texH);

    // Destination slices shrink when the box is smaller than the two borders.
    float scaleX = 1.f;
    float scaleY = 1.f;
    if (srcLeft + srcRight > 0.f && srcLeft + srcRight > w) {
        scaleX = w / (srcLeft + srcRight);
    }
    if (srcTop + srcBottom > 0.f && srcTop + srcBottom > h) {
        scaleY = h / (srcTop + srcBottom);
    }
    float const left   = srcLeft * scaleX;
    float const right  = srcRight * scaleX;
    float const top    = srcTop * scaleY;
    float const bottom = srcBottom * scaleY;
    float const midW   = std::max(0.f, w - left - right);
    float const midH   = std::max(0.f, h - top - bottom);

    float const uLeft   = std::min(1.f, srcLeft / texW);
    float const uRight  = std::min(1.f, srcRight / texW);
    float const vTop    = std::min(1.f, srcTop / texH);
    float const vBottom = std::min(1.f, srcBottom / texH);
    float const uMid    = std::max(0.f, 1.f - uLeft - uRight);
    float const vMid    = std::max(0.f, 1.f - vTop - vBottom);

    auto const& textureRef = texture.data->mClientTexture;
    auto const  draw       = [&](float px, float py, float pw, float ph, float u, float v, float uw, float vh) {
        if (pw <= 0.f || ph <= 0.f || uw <= 0.f || vh <= 0.f) return;
        context.drawImage(
            textureRef,
            glm::vec2{px, py},
            glm::vec2{pw, ph},
            glm::vec2{u, v},
            glm::vec2{uw, vh},
            false
        );
    };

    // Corners keep their size, edges stretch along one axis, the middle both.
    draw(x, y, left, top, 0.f, 0.f, uLeft, vTop);
    draw(x + w - right, y, right, top, 1.f - uRight, 0.f, uRight, vTop);
    draw(x, y + h - bottom, left, bottom, 0.f, 1.f - vBottom, uLeft, vBottom);
    draw(x + w - right, y + h - bottom, right, bottom, 1.f - uRight, 1.f - vBottom, uRight, vBottom);
    draw(x + left, y, midW, top, uLeft, 0.f, uMid, vTop);
    draw(x + left, y + h - bottom, midW, bottom, uLeft, 1.f - vBottom, uMid, vBottom);
    draw(x, y + top, left, midH, 0.f, vTop, uLeft, vMid);
    draw(x + w - right, y + top, right, midH, 1.f - uRight, vTop, uRight, vMid);
    draw(x + left, y + top, midW, midH, uLeft, vTop, uMid, vMid);

    context.flushImages(
        mce::Color{tint.r, tint.g, tint.b, 1.f},
        std::clamp(alpha * tint.a, 0.f, 1.f),
        HashedString{kMaterial}
    );
    return true;
}

} // namespace silica
