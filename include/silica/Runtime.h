#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace silica::runtime {

// Where the diagnostics produced by the compiler go. The default writes to
// stderr and the Win32 debug output; a mod can redirect both into its own log.
using LogSink = std::function<void(std::string_view)>;

// Directory that relative CompileFile paths are resolved against, normally the
// mod directory. Empty means "resolve against the working directory".
std::string RootPath();
void        SetRootPath(std::string path);

// Absolute paths (drive letter or leading slash) are returned unchanged;
// relative ones are joined under RootPath().
std::string ResolvePath(std::string_view path);

void SetLogSink(LogSink sink);
void Log(std::string_view message);

} // namespace silica::runtime
