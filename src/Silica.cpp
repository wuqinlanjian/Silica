#include "silica/Silica.h"

#include <map>
#include <mutex>
#include <utility>

namespace silica {
namespace {

struct LoadedUI {
    std::string fallbackSource;
    UIWidgetPtr root;
};

std::mutex& RegistryMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, LoadedUI>& Registry() {
    static std::map<std::string, LoadedUI> registry;
    return registry;
}

} // namespace

void Silica::SetRootPath(std::string path) { runtime::SetRootPath(std::move(path)); }

std::string Silica::RootPath() { return runtime::RootPath(); }

std::string Silica::ResolvePath(std::string_view path) { return runtime::ResolvePath(path); }

void Silica::SetLogSink(runtime::LogSink sink) { runtime::SetLogSink(std::move(sink)); }

UIWidgetPtr Silica::CompileFile(std::string const& filepath, std::string const& fallbackSource) {
    UIWidgetPtr root = UICompiler::CompileFile(filepath, fallbackSource);

    std::lock_guard lock(RegistryMutex());
    LoadedUI&       entry = Registry()[runtime::ResolvePath(filepath)];
    entry.fallbackSource  = fallbackSource;
    entry.root            = root;
    return root;
}

UIWidgetPtr Silica::ReloadUI(std::string const& filepath) {
    std::string const key = runtime::ResolvePath(filepath);

    {
        std::lock_guard lock(RegistryMutex());
        if (Registry().find(key) == Registry().end()) {
            runtime::Log("silica: ReloadUI('" + filepath + "') before CompileFile; nothing to replace");
            return nullptr;
        }
    }

    SilCompileResult fresh = UICompiler::CompileFile(filepath);
    UIWidgetPtr      replacement;
    if (fresh.ok()) {
        replacement = std::move(fresh.root);
        runtime::Log("silica: reloaded '" + key + "'");
    } else {
        for (auto const& diagnostic : fresh.diagnostics) {
            runtime::Log(FormatDiagnostic(diagnostic, fresh.origin));
        }
        runtime::Log("silica: reload of '" + key + "' failed; keeping the previous tree");
    }

    std::lock_guard lock(RegistryMutex());
    LoadedUI&       entry = Registry()[key];
    if (replacement) entry.root = std::move(replacement);
    return entry.root;
}

UIWidgetPtr Silica::CurrentUI(std::string const& filepath) {
    std::string const key = runtime::ResolvePath(filepath);
    std::lock_guard   lock(RegistryMutex());
    auto const        it = Registry().find(key);
    return it == Registry().end() ? nullptr : it->second.root;
}

} // namespace silica
