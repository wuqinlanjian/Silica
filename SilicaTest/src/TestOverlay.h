#pragma once

#include <filesystem>

namespace silica_test {

void openLog(std::filesystem::path const& path);
void logLine(char const* fmt, ...);

bool startOverlay();
void stopOverlay();

// Compiles the panel document and imports its file-backed images. Called from
// mod load, which runs inside the game's DllMain before the engine creates its
// resource pack repository, so the generated pack is part of the first stack.
void warmDocument();

} // namespace silica_test
