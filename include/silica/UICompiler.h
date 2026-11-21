#pragma once

#include "SilNode.h"
#include "Widget.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace silica {

struct SilCompileResult {
    WidgetPtr                  root;
    std::vector<SilDiagnostic> diagnostics;
    std::string                origin = "<sil>";

    bool        ok() const { return root != nullptr && !HasErrors(diagnostics); }
    std::string Report() const; // one formatted diagnostic per line
};

// Bridge between the declarative document and the C++ widget tree: parse, create
// every node through WidgetFactory, apply all properties through ApplyProperty,
// then mount children with AddChild. Unknown components or properties become
// diagnostics instead of failures, so a typo never takes the game down.
class UICompiler {
public:
    // The requested one-liner. For diagnostics use CompileDetailed.
    static WidgetPtr Compile(std::string_view silSource);

    static SilCompileResult CompileDetailed(std::string_view silSource, std::string origin = "<sil>");

    // Detailed load from disk, resolved against the runtime root. No fallback:
    // a missing or broken file yields diagnostics and no root.
    static SilCompileResult CompileFile(std::string const& path);

    // Loads filepath and, when it cannot be read or does not compile, compiles
    // fallbackSource instead so a missing file never leaves the game without a
    // UI. Diagnostics of both attempts go to the runtime log. Never throws.
    static UIWidgetPtr CompileFile(std::string const& filepath, std::string const& fallbackSource);
};

} // namespace silica
