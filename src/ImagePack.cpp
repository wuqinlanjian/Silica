#include "silica/ImagePack.h"

#include "silica/Container.h"
#include "silica/Image.h"
#include "silica/Part.h"
#include "silica/Runtime.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>

#include "ll/api/service/Bedrock.h"

#include "mc/resources/ResourcePackRepository.h"

namespace silica::imagepack {
namespace {

namespace fs = std::filesystem;

constexpr char kPackFolder[]  = "silica_images";
constexpr char kPackId[]      = "5a1b7c9e-64d3-4f18-9a02-3c8e7b2d5f41";
constexpr char kModuleId[]    = "c2d4e6f8-1a3b-4c5d-8e9f-0a1b2c3d4e5f";
constexpr char kTextureRoot[] = "textures/silica/";

// LeviLamina adds <modDir>/resource_packs as a pack source for every mod and
// stacks those packs itself, so a folder here needs no global pack list entry.
constexpr char kManifest[] = R"({
  "format_version": 2,
  "header": {
    "description": "Images packaged by the Silica UI toolkit",
    "name": "Silica Images",
    "uuid": "5a1b7c9e-64d3-4f18-9a02-3c8e7b2d5f41",
    "version": [1, 0, 0],
    "min_engine_version": [1, 21, 0]
  },
  "modules": [
    {
      "description": "Silica generated textures",
      "type": "resources",
      "uuid": "c2d4e6f8-1a3b-4c5d-8e9f-0a1b2c3d4e5f",
      "version": [1, 0, 0]
    }
  ]
}
)";

struct CacheEntry {
    std::uintmax_t     size = 0;
    fs::file_time_type written{};
    std::string        texture;
};

std::mutex                                  g_mutex;
std::unordered_map<std::string, CacheEntry> g_cache;
bool                                        g_rootMissingLogged = false;
bool                                        g_packDirty         = false;

fs::path toPath(std::string_view utf8) { return fs::u8path(std::string(utf8)); }

bool startsWithFilePrefix(std::string_view text, std::string_view& rest) {
    if (text.size() < 5) return false;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    if (lower(text[0]) != 'f' || lower(text[1]) != 'i' || lower(text[2]) != 'l' || lower(text[3]) != 'e'
        || text[4] != ':') {
        return false;
    }
    rest = text.substr(5);
    return true;
}

std::string lowerText(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// A document may name `file:../../anything`; anything that escapes the mod
// directory is refused, so a typo or a hostile document cannot read or copy
// files outside the pack it owns.
bool insideRoot(std::string const& root, fs::path const& target) {
    if (root.empty()) return true;
    std::error_code ec;
    fs::path const  base = fs::weakly_canonical(toPath(root), ec);
    if (ec) return false;
    fs::path const canonical = fs::weakly_canonical(target, ec);
    if (ec) return false;

    std::string const prefix = lowerText(base.generic_string());
    std::string const path   = lowerText(canonical.generic_string());
    return path == prefix || path.rfind(prefix + "/", 0) == 0;
}

bool g_outsideLogged = false;

std::string extensionLower(std::string_view path) {
    size_t const slash = path.find_last_of("/\\");
    size_t const dot   = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return {};
    std::string ext(path.substr(dot));
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool isImageExtension(std::string_view ext) { return ext == ".png" || ext == ".tga"; }

std::uint64_t hashFile(fs::path const& file) {
    std::ifstream in(file, std::ios::binary);
    std::uint64_t hash = 1469598103934665603ull;
    char          buffer[64 * 1024];
    while (in) {
        in.read(buffer, sizeof(buffer));
        std::streamsize const got = in.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

std::string hex16(std::uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string    out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<size_t>(i)] = digits[value & 0xF];
        value >>= 4;
    }
    return out;
}

bool writeText(fs::path const& file, std::string const& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// Re-scanning the repository picks up new files in the mod pack folder. The
// client instance is missing during early startup, so a failed attempt is
// retried from Resolve.
void refreshIfDirty() {
    if (!g_packDirty) return;
    auto repo = ll::service::bedrock::getResourcePackRepository(true);
    if (!repo) return;
    repo->refreshPacks();
    g_packDirty = false;
}

std::string importFile(fs::path const& file, std::uintmax_t size) {
    std::string const root = runtime::RootPath();
    if (root.empty()) {
        if (!g_rootMissingLogged) {
            g_rootMissingLogged = true;
            runtime::Log("silica: image import needs SetRootPath (the mod directory)");
        }
        return {};
    }

    fs::path const packDir = toPath(root) / "resource_packs" / kPackFolder;
    std::error_code ec;
    fs::create_directories(packDir / "textures" / "silica", ec);

    fs::path const manifest = packDir / "manifest.json";
    if (!fs::exists(manifest, ec)) {
        if (!writeText(manifest, kManifest)) {
            runtime::Log("silica: cannot write the image pack manifest in " + packDir.string());
            return {};
        }
        g_packDirty = true;
    }

    std::string const ext  = extensionLower(file.string());
    std::string const name = hex16(hashFile(file));
    fs::path const    dst  = packDir / "textures" / "silica" / (name + ext);
    bool const        copy = !fs::exists(dst, ec) || fs::file_size(dst, ec) != size;
    if (copy) {
        fs::copy_file(file, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            runtime::Log("silica: cannot copy image into the pack: " + ec.message());
            return {};
        }
        g_packDirty = true;
    }

    fs::path const icon = packDir / "pack_icon.png";
    if (!fs::exists(icon, ec) && ext == ".png") {
        fs::copy_file(file, icon, fs::copy_options::overwrite_existing, ec);
    }

    if (g_packDirty) refreshIfDirty();
    return std::string(kTextureRoot) + name;
}

void warmWidget(WidgetPtr const& widget) {
    if (!widget) return;
    std::string texture;
    if (auto image = std::dynamic_pointer_cast<SilicaImage>(widget); image && !image->src.empty()) {
        Resolve(image->src, texture);
    }
    for (auto const& part : widget->Parts()) {
        if (auto skin = std::dynamic_pointer_cast<SilicaPart>(part); skin && !skin->src.empty()) {
            Resolve(skin->src, texture);
        }
    }
    if (auto container = std::dynamic_pointer_cast<SilicaContainer>(widget)) {
        for (auto const& child : container->Children()) warmWidget(child);
    }
}

} // namespace

bool Resolve(std::string_view src, std::string& texturePath) {
    std::string_view file = src;
    bool const       explicitFile = startsWithFilePrefix(src, file);
    if (!explicitFile && !isImageExtension(extensionLower(src))) {
        texturePath.assign(src);
        return true;
    }

    std::string const absolute = runtime::ResolvePath(file);
    fs::path const    path     = toPath(absolute);
    if (!insideRoot(runtime::RootPath(), path)) {
        if (!g_outsideLogged) {
            g_outsideLogged = true;
            runtime::Log("silica: refusing image outside the mod directory: " + absolute);
        }
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;

    std::lock_guard lock(g_mutex);
    refreshIfDirty();

    std::uintmax_t const     size    = fs::file_size(path, ec);
    fs::file_time_type const written = fs::last_write_time(path, ec);
    auto                     it      = g_cache.find(absolute);
    if (it != g_cache.end() && it->second.size == size && it->second.written == written) {
        texturePath = it->second.texture;
        return true;
    }

    std::string texture = importFile(path, size);
    if (texture.empty()) return false;
    g_cache[absolute] = CacheEntry{size, written, texture};
    runtime::Log("silica: packed image '" + absolute + "' as " + texture);
    texturePath = std::move(texture);
    return true;
}

void Warmup(WidgetPtr const& root) { warmWidget(root); }

} // namespace silica::imagepack
