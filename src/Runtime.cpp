#include "silica/Runtime.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace silica::runtime {
namespace {

std::mutex  g_mutex;
std::string g_root;
LogSink     g_sink;

bool isAbsolute(std::string_view path) {
    if (path.empty()) return false;
    if (path.front() == '/' || path.front() == '\\') return true;
    if (path.size() < 2 || path[1] != ':') return false;
    char const drive = path[0];
    return (drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z');
}

std::string join(std::string_view root, std::string_view path) {
    std::string out(root);
    while (!out.empty() && (out.back() == '/' || out.back() == '\\')) out.pop_back();
    if (!out.empty()) out += '/';
    size_t start = 0;
    while (start < path.size() && (path[start] == '/' || path[start] == '\\')) ++start;
    out.append(path.substr(start));
    return out;
}

void defaultSink(std::string_view message) {
    std::string line(message);
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
#ifdef _WIN32
    OutputDebugStringA(line.c_str());
#endif
}

} // namespace

std::string RootPath() {
    std::lock_guard lock(g_mutex);
    return g_root;
}

void SetRootPath(std::string path) {
    // One separator keeps resolved paths (and registry keys) stable no matter
    // how the caller spelled the mod directory.
    std::replace(path.begin(), path.end(), '\\', '/');
    std::lock_guard lock(g_mutex);
    g_root = std::move(path);
}

std::string ResolvePath(std::string_view path) {
    if (isAbsolute(path)) return std::string(path);
    std::lock_guard lock(g_mutex);
    if (g_root.empty()) return std::string(path);
    return join(g_root, path);
}

void SetLogSink(LogSink sink) {
    std::lock_guard lock(g_mutex);
    g_sink = std::move(sink);
}

void Log(std::string_view message) {
    LogSink sink;
    {
        std::lock_guard lock(g_mutex);
        sink = g_sink;
    }
    // Invoked outside the lock: a sink is allowed to log or compile again.
    if (sink) {
        sink(message);
        return;
    }
    defaultSink(message);
}

} // namespace silica::runtime
