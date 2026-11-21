#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "TestOverlay.h"
#include "ll/api/mod/Mod.h"
#include "silica/Silica.h"

namespace {

std::string pathToUtf8(std::filesystem::path const& path) {
    auto utf8 = path.u8string();
    return std::string(reinterpret_cast<char const*>(utf8.c_str()), utf8.size());
}

} // namespace

extern "C" {
__declspec(dllexport) bool ll_mod_load(ll::mod::Mod& self) {
    std::filesystem::path const dir = self.getModDir();
    silica_test::openLog(dir / L"SilicaTest.log");
    silica_test::logLine("[mod] loaded from %s", pathToUtf8(dir).c_str());

    // Diagnostics from the .sil compiler land in the mod log; relative
    // documents are resolved under the mod directory.
    silica::Silica::SetLogSink([](std::string_view line) {
        silica_test::logLine("%.*s", static_cast<int>(line.size()), line.data());
    });
    silica::Silica::SetRootPath(pathToUtf8(dir));
    // Package file-backed images now: mod load runs before the engine builds
    // the resource pack stack, so the pack is live from the first frame.
    silica_test::warmDocument();
    return true;
}

__declspec(dllexport) bool ll_mod_enable(ll::mod::Mod& self) {
    (void)self;
    return silica_test::startOverlay();
}

__declspec(dllexport) bool ll_mod_disable(ll::mod::Mod& self) {
    (void)self;
    silica_test::stopOverlay();
    return true;
}
}
