#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace silica {

// One component instance in the declarative document. Properties keep their
// textual form: only the widget knows what "font-size" or "layout" means.
struct SilNode {
    std::string                        type;
    std::map<std::string, std::string> props;
    std::vector<SilNode>               children;
    int                                line   = 1;
    int                                column = 1;

    // Component arguments (`ForEach (data: @hits, item: @item, index: @idx)`),
    // keyed by parameter name. Values keep their source form, so `@hits` keeps
    // its '@' and UICompiler strips it when configuring the widget.
    std::map<std::string, std::string> params;

    // Reactive values (`text: @user_name`, `visible: @health > 0`), keyed by
    // property. The raw expression text is kept intact: quotes and operators
    // are exactly what the evaluator expects.
    std::map<std::string, std::string> bindings;

    // Literal values that interpolate expressions (`text: "{{@item.name}}"`).
    // The whole value is kept, including any surrounding text.
    std::map<std::string, std::string> templates;

    // Event scripts (`onClick: { @health = @health + 10; }`), keyed by
    // property. The body excludes the outer braces.
    std::map<std::string, std::string> scripts;
};

// Values a Style block contributes, split the same way a component body splits
// them, so an inline declaration can override a styled one key by key.
struct SilPropertySet {
    std::map<std::string, std::string> props;
    std::map<std::string, std::string> bindings;
    std::map<std::string, std::string> templates;
};

// Top-level `Style Name [extends Base] { ... }`. A node opts in with
// `class: "a b"` or `style: name`; UICompiler expands the chain (base first)
// and lets inline properties win.
struct SilStyle {
    std::string    name;
    std::string    base; // empty when the style does not extend another
    SilPropertySet values;
    int            line   = 1;
    int            column = 1;
};

// Top-level `Component Name(p1, p2: default) { body }`. An instantiation
// `Name(p1: value) { children }` builds the body once, exposing the arguments
// as `@p1` and replacing `Slot` with the caller's children.
struct SilComponentDef {
    struct Param {
        std::string name;
        std::string defaultValue; // raw source text; empty means required
    };

    std::string          name;
    std::vector<Param>   params;
    std::vector<SilNode> body;
    int                  line   = 1;
    int                  column = 1;
};

struct SilDiagnostic {
    enum class Severity { Error, Warning };

    Severity    severity = Severity::Error;
    int         line     = 1;
    int         column   = 1;
    std::string message;
};

inline bool HasErrors(std::vector<SilDiagnostic> const& diagnostics) {
    for (auto const& diagnostic : diagnostics) {
        if (diagnostic.severity == SilDiagnostic::Severity::Error) return true;
    }
    return false;
}

// "path.sil:12:5: error: expected ';'"
std::string FormatDiagnostic(SilDiagnostic const& diagnostic, std::string_view origin);

} // namespace silica
