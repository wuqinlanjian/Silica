#pragma once

#include "Button.h"
#include "Color.h"
#include "Container.h"
#include "Handlers.h"
#include "Input.h"
#include "Label.h"
#include "Panel.h"
#include "Property.h"
#include "Renderer.h"
#include "Runtime.h"
#include "SilNode.h"
#include "SilParser.h"
#include "Style.h"
#include "Tree.h"
#include "UICompiler.h"
#include "Widget.h"
#include "WidgetFactory.h"

#include <string>
#include <string_view>

namespace silica {

// Facade over the parts a mod touches: where its .sil files live, where the
// compiler writes diagnostics, and which tree is mounted for each document.
class Silica {
public:
    // The mod passes its own directory once at init; every relative file path
    // is then resolved under it.
    static void        SetRootPath(std::string path);
    static std::string RootPath();
    static std::string ResolvePath(std::string_view path);

    // Compiler diagnostics and fallback notices go here; by default they reach
    // stderr and OutputDebugString.
    static void SetLogSink(runtime::LogSink sink);

    // Compiles filepath with an embedded fallback and records the result, so
    // ReloadUI can refresh it later.
    static UIWidgetPtr CompileFile(std::string const& filepath, std::string const& fallbackSource);

    // Re-reads filepath and replaces the tree recorded by CompileFile. Returns
    // the new tree; when the file is broken the previous tree is kept.
    static UIWidgetPtr ReloadUI(std::string const& filepath);

    // The tree currently recorded for filepath (empty when never loaded).
    static UIWidgetPtr CurrentUI(std::string const& filepath);
};

} // namespace silica
