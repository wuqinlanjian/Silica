#include "silica/UICompiler.h"

#include "silica/DataStore.h"
#include "silica/Expression.h"
#include "silica/ForEach.h"
#include "silica/Property.h"
#include "silica/Runtime.h"
#include "silica/SilParser.h"
#include "silica/WidgetFactory.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace silica {
namespace {

constexpr int    kMaxDepth            = 64;
constexpr int    kMaxVarDepth         = 16;
constexpr size_t kMaxComponentNesting = 32;
constexpr size_t kMaxStyleChain       = 32;

void report(
    std::vector<SilDiagnostic>& diagnostics,
    SilDiagnostic::Severity     severity,
    int                         line,
    int                         column,
    std::string                 message
) {
    diagnostics.push_back({severity, line, column, std::move(message)});
}

bool IsNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// '.' is deliberately excluded: `$x` and `@item.name` stop at the dot so a
// field path keeps working after substitution.
bool IsNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

bool IsQuoted(std::string_view text) {
    return text.size() >= 2 && text.front() == '"' && text.back() == '"';
}

std::string Unquote(std::string_view text) {
    if (!IsQuoted(text)) return std::string(text);
    std::string out;
    out.reserve(text.size());
    for (size_t i = 1; i + 1 < text.size(); ++i) {
        char const c = text[i];
        if (c == '\\' && i + 2 < text.size()) {
            switch (text[i + 1]) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            default: out += text[i + 1]; break;
            }
            ++i;
            continue;
        }
        out += c;
    }
    return out;
}

std::string Quote(std::string_view text) {
    std::string out = "\"";
    for (char const c : text) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    out += '"';
    return out;
}

// `margin-left` and `marginLeft` name the same property; the compiler compares
// compacted keys so a style value yields to an inline one either way.
std::string NormalizedKey(std::string_view key) {
    std::string normalized = property::ToLower(property::Trim(key));
    normalized.erase(std::remove(normalized.begin(), normalized.end(), '-'), normalized.end());
    return normalized;
}

void EraseNormalized(std::map<std::string, std::string>& values, std::string const& normalized) {
    for (auto it = values.begin(); it != values.end();) {
        if (NormalizedKey(it->first) == normalized) it = values.erase(it);
        else ++it;
    }
}

struct PropertyLayer {
    std::map<std::string, std::string> props;
    std::map<std::string, std::string> bindings;
    std::map<std::string, std::string> templates;
};

// Overlays `layer` on `target`. A key set in any category replaces the same
// key in all three, so an inline binding overrides a styled literal.
void ApplyLayer(PropertyLayer& target, PropertyLayer const& layer) {
    auto const apply = [&target](
                           std::map<std::string, std::string>&       destination,
                           std::map<std::string, std::string> const& source
                       ) {
        for (auto const& [key, value] : source) {
            std::string const normalized = NormalizedKey(key);
            EraseNormalized(target.props, normalized);
            EraseNormalized(target.bindings, normalized);
            EraseNormalized(target.templates, normalized);
            destination[key] = value;
        }
    };
    apply(target.props, layer.props);
    apply(target.bindings, layer.bindings);
    apply(target.templates, layer.templates);
}

struct CompileContext {
    std::shared_ptr<SilDocument const> document;
    std::vector<SilDiagnostic>*        diagnostics = nullptr;
    std::vector<std::string>           componentStack; // lowercase definition names
};

// Component arguments in their two textual forms: expression-safe text for
// bindings/scripts/parameter lists and plain text for literal properties.
struct ParamText {
    std::map<std::string, std::string, std::less<>> expressions;
    std::map<std::string, std::string, std::less<>> plain;
};

struct SlotBinding {
    std::shared_ptr<std::vector<SilNode> const> children;
    std::shared_ptr<SilDataContext const>       scope;
    ParamText                                   params;
};

bool IsSlotNode(SilNode const& node) {
    return property::ToLower(property::Trim(node.type)) == "slot";
}

bool BodyHasSlot(std::vector<SilNode> const& nodes) {
    for (auto const& node : nodes) {
        if (IsSlotNode(node)) return true;
        if (BodyHasSlot(node.children)) return true;
    }
    return false;
}

// `DemoRowBox`, `demo-row-box` and `demo_row_box` name the same style: class
// tokens are compared without case or word separators, like property keys.
std::string StyleKey(std::string_view name) {
    std::string key = property::ToLower(property::Trim(name));
    key.erase(std::remove_if(key.begin(), key.end(), [](char c) { return c == '-' || c == '_'; }), key.end());
    return key;
}

SilStyle const* FindStyle(SilDocument const& document, std::string const& name) {
    std::string const wanted = StyleKey(name);
    for (auto const& [key, style] : document.styles) {
        if (StyleKey(key) == wanted) return &style;
    }
    return nullptr;
}

SilComponentDef const* FindComponent(SilDocument const& document, std::string_view type) {
    std::string const wanted = property::ToLower(property::Trim(type));
    if (wanted.empty()) return nullptr;
    for (auto const& [name, definition] : document.components) {
        if (property::ToLower(name) == wanted) return &definition;
    }
    return nullptr;
}

// Recursively replaces `$name` with the variable text. Unknown names are left
// alone (a script-local or ordinary text), and a cycle stops with a diagnostic.
std::string SubstituteVars(
    CompileContext&          ctx,
    std::string const&       text,
    std::vector<std::string>& visiting,
    int                      line,
    int                      column
) {
    std::string out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '$' || i + 1 >= text.size() || !IsNameStart(text[i + 1])) {
            out += text[i];
            ++i;
            continue;
        }
        size_t end = i + 1;
        while (end < text.size() && IsNameChar(text[end])) ++end;
        std::string const name = text.substr(i + 1, end - i - 1);

        auto const found = ctx.document->vars.find(name);
        if (found == ctx.document->vars.end()) {
            out.append(text, i, end - i);
            i = end;
            continue;
        }
        if (std::find(visiting.begin(), visiting.end(), name) != visiting.end()) {
            report(*ctx.diagnostics, SilDiagnostic::Severity::Error, line, column,
                   "variable '$" + name + "' refers to itself");
            out.append(text, i, end - i);
            i = end;
            continue;
        }
        if (visiting.size() >= kMaxVarDepth) {
            report(*ctx.diagnostics, SilDiagnostic::Severity::Error, line, column,
                   "variables nest too deeply ('$" + name + "')");
            out.append(text, i, end - i);
            i = end;
            continue;
        }
        visiting.push_back(name);
        out += SubstituteVars(ctx, found->second, visiting, line, column);
        visiting.pop_back();
        i = end;
    }
    return out;
}

// Replaces `@param` with the argument text. Expression mode skips quoted
// strings so a script string that happens to contain '@name' stays literal.
std::string SubstituteParams(
    std::string const&                                text,
    std::map<std::string, std::string, std::less<>> const& table,
    bool                                              expressionMode
) {
    if (table.empty() || text.find('@') == std::string::npos) return text;

    std::string out;
    out.reserve(text.size() + 16);
    bool inString = false;
    for (size_t i = 0; i < text.size();) {
        char const c = text[i];
        if (expressionMode) {
            if (inString) {
                out += c;
                if (c == '\\' && i + 1 < text.size()) {
                    out += text[i + 1];
                    i += 2;
                    continue;
                }
                if (c == '"') inString = false;
                ++i;
                continue;
            }
            if (c == '"') {
                inString = true;
                out += c;
                ++i;
                continue;
            }
        }
        if (c == '@' && i + 1 < text.size() && IsNameStart(text[i + 1])) {
            size_t end = i + 1;
            while (end < text.size() && IsNameChar(text[end])) ++end;
            std::string const name = text.substr(i + 1, end - i - 1);
            auto const        found = table.find(name);
            if (found != table.end()) {
                out += found->second;
                i = end;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return out;
}

// Template literals substitute inside `{{ ... }}` only; the surrounding text is
// plain and only literal arguments expand there.
std::string SubstituteTemplateParams(std::string const& text, ParamText const& params) {
    if (text.find('@') == std::string::npos) return text;
    std::string out;
    out.reserve(text.size() + 16);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t const open = text.find("{{", pos);
        if (open == std::string::npos) {
            out += SubstituteParams(text.substr(pos), params.plain, false);
            break;
        }
        out += SubstituteParams(text.substr(pos, open - pos), params.plain, false);
        size_t const close = text.find("}}", open + 2);
        if (close == std::string::npos) {
            out += SubstituteParams(text.substr(open), params.expressions, true);
            break;
        }
        out += SubstituteParams(text.substr(open, close + 2 - open), params.expressions, true);
        pos = close + 2;
    }
    return out;
}

struct Argument {
    Variant     value;
    std::string expression; // valid in expression position
    std::string plain;      // valid as literal text
};

// `@key` evaluates against the caller scope and store; everything else is a
// literal (becomes quoted text when used in an expression).
Argument EvaluateArgument(
    CompileContext&                        ctx,
    std::string const&                     source,
    std::shared_ptr<SilDataContext const> const& scope,
    int                                    line,
    int                                    column
) {
    Argument    argument;
    std::string const text(property::Trim(source));
    if (text.empty()) return argument;

    if (text.front() == '@') {
        std::string error;
        auto        parsed = Expression::Parse(text, error);
        if (parsed) {
            argument.value = parsed->Evaluate([&scope](std::string_view name) {
                if (scope && scope->Has(name)) return scope->Get(name);
                return DataStore::Has(name) ? DataStore::Get(name) : Variant{};
            });
        } else {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Warning,
                line,
                column,
                "cannot evaluate the argument '" + text + "': " + error
            );
        }
        argument.expression = text;
        argument.plain      = text;
        return argument;
    }

    if (IsQuoted(text)) {
        argument.plain      = Unquote(text);
        argument.expression = text;
        argument.value      = Variant{argument.plain};
        return argument;
    }

    std::string const lower = property::ToLower(text);
    if (lower == "true" || lower == "false") {
        argument.value      = Variant{lower == "true"};
        argument.plain      = lower;
        argument.expression = lower;
        return argument;
    }
    if (lower == "null" || lower == "none") {
        argument.expression = "null";
        return argument;
    }
    if (auto number = property::ParseNumber(text)) {
        argument.value      = Variant{*number};
        argument.plain      = text;
        argument.expression = text;
        return argument;
    }

    argument.plain      = text;
    argument.expression = Quote(text);
    argument.value      = Variant{text};
    return argument;
}

std::vector<std::string> SplitStyleNames(std::string const& text) {
    std::vector<std::string> names;
    std::string              current;
    for (char const c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == ',') {
            if (!current.empty()) {
                names.push_back(current);
                current.clear();
            }
            continue;
        }
        current += c;
    }
    if (!current.empty()) names.push_back(current);
    return names;
}

bool CollectStyleChain(
    CompileContext&                ctx,
    std::string const&             name,
    int                            line,
    int                            column,
    std::vector<SilStyle const*>&  chain,
    std::vector<std::string>&      visiting
) {
    if (chain.size() >= kMaxStyleChain) {
        report(*ctx.diagnostics, SilDiagnostic::Severity::Error, line, column, "styles nest too deeply");
        return false;
    }

    SilStyle const* style = FindStyle(*ctx.document, name);
    if (!style) {
        report(*ctx.diagnostics, SilDiagnostic::Severity::Error, line, column, "unknown style '" + name + "'");
        return false;
    }

    std::string const key = StyleKey(style->name);
    if (std::find(visiting.begin(), visiting.end(), key) != visiting.end()) {
        report(
            *ctx.diagnostics,
            SilDiagnostic::Severity::Error,
            style->line,
            style->column,
            "style '" + style->name + "' extends itself"
        );
        return false;
    }

    visiting.push_back(key);
    bool ok = true;
    if (!style->base.empty()) ok = CollectStyleChain(ctx, style->base, line, column, chain, visiting);
    visiting.pop_back();

    chain.push_back(style);
    return ok;
}

// Applies `$variables`, the style chain from the node's `class`/`style`
// directives, and then the enclosing component's arguments. Inline declarations
// stay on top of styled ones.
SilNode ExpandNode(CompileContext& ctx, SilNode const& raw, ParamText const* params) {
    SilNode node = raw;

    auto const substituteVars = [&ctx, &raw](std::string& text) {
        if (text.find('$') == std::string::npos) return;
        std::vector<std::string> visiting;
        text = SubstituteVars(ctx, text, visiting, raw.line, raw.column);
    };
    for (auto& [key, value] : node.props) substituteVars(value);
    for (auto& [key, value] : node.bindings) substituteVars(value);
    for (auto& [key, value] : node.templates) substituteVars(value);
    for (auto& [key, value] : node.params) substituteVars(value);
    // Scripts keep their text: `$name` is a script-local there.

    std::vector<std::string> styleNames;
    auto const takeDirective = [&node](char const* wanted) {
        std::string    value;
        std::vector<std::string> keys;
        for (auto const& [key, ignored] : node.props) {
            if (NormalizedKey(key) == wanted) keys.push_back(key);
        }
        for (auto const& key : keys) {
            value = node.props[key];
            node.props.erase(key);
        }
        return value;
    };
    for (auto& name : SplitStyleNames(takeDirective("style"))) styleNames.push_back(std::move(name));
    for (auto& name : SplitStyleNames(takeDirective("class"))) styleNames.push_back(std::move(name));

    if (!styleNames.empty()) {
        PropertyLayer merged;
        for (auto const& name : styleNames) {
            std::vector<SilStyle const*> chain;
            std::vector<std::string>     visiting;
            if (!CollectStyleChain(ctx, name, raw.line, raw.column, chain, visiting)) continue;
            for (auto const* style : chain) {
                // Style blocks may themselves reference $variables; expand them
                // before the layer lands on the node.
                PropertyLayer layer{style->values.props, style->values.bindings, style->values.templates};
                for (auto& [key, value] : layer.props) substituteVars(value);
                for (auto& [key, value] : layer.bindings) substituteVars(value);
                for (auto& [key, value] : layer.templates) substituteVars(value);
                ApplyLayer(merged, layer);
            }
        }
        ApplyLayer(merged, PropertyLayer{node.props, node.bindings, node.templates});
        node.props     = std::move(merged.props);
        node.bindings  = std::move(merged.bindings);
        node.templates = std::move(merged.templates);
    }

    if (params) {
        for (auto& [key, value] : node.props) value = SubstituteParams(value, params->plain, false);
        for (auto& [key, value] : node.bindings) value = SubstituteParams(value, params->expressions, true);
        for (auto& [key, value] : node.params) value = SubstituteParams(value, params->expressions, true);
        for (auto& [key, value] : node.scripts) value = SubstituteParams(value, params->expressions, true);
        for (auto& [key, value] : node.templates) value = SubstituteTemplateParams(value, *params);
    }

    return node;
}

void ApplyNodeToWidget(CompileContext& ctx, SilNode const& node, SilicaWidget& widget) {
    for (auto const& [key, value] : node.props) {
        if (!widget.ApplyProperty(key, value)) {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Warning,
                node.line,
                node.column,
                "unknown property '" + key + "' on '" + node.type + "'"
            );
        }
    }
    for (auto const& [key, expression] : node.bindings) {
        std::string error;
        if (!widget.BindProperty(key, expression, error)) {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Warning,
                node.line,
                node.column,
                "cannot bind property '" + key + "' on '" + node.type + "': " + error
            );
        }
    }
    for (auto const& [key, templateText] : node.templates) {
        std::string error;
        if (!widget.BindTemplate(key, templateText, error)) {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Warning,
                node.line,
                node.column,
                "cannot bind property '" + key + "' on '" + node.type + "': " + error
            );
        }
    }
    for (auto const& [key, source] : node.scripts) {
        std::string error;
        if (!widget.SetScript(key, source, error)) {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Warning,
                node.line,
                node.column,
                "cannot attach the script on '" + key + "' of '" + node.type + "': " + error
            );
        }
    }
}

WidgetPtr BuildNode(
    CompileContext&                        ctx,
    SilNode const&                         raw,
    int                                    depth,
    std::shared_ptr<SilDataContext const> const& scope,
    ParamText const*                       params,
    SlotBinding const*                     slot
);

void AppendSlotChildren(CompileContext& ctx, SlotBinding const& slot, int depth, std::vector<WidgetPtr>& out) {
    if (!slot.children) return;
    for (auto const& provided : *slot.children) {
        auto built = BuildNode(ctx, provided, depth + 1, slot.scope, &slot.params, nullptr);
        if (built) out.push_back(std::move(built));
    }
}

WidgetPtr BuildComponent(
    CompileContext&                        ctx,
    SilNode const&                         inst,
    SilComponentDef const&                 definition,
    int                                    depth,
    std::shared_ptr<SilDataContext const> const& scope,
    ParamText const*                       callerParams
) {
    std::string const key = property::ToLower(definition.name);
    if (std::find(ctx.componentStack.begin(), ctx.componentStack.end(), key) != ctx.componentStack.end()) {
        report(
            *ctx.diagnostics,
            SilDiagnostic::Severity::Error,
            inst.line,
            inst.column,
            "component '" + definition.name + "' instantiates itself recursively"
        );
        return nullptr;
    }
    if (ctx.componentStack.size() >= kMaxComponentNesting) {
        report(
            *ctx.diagnostics,
            SilDiagnostic::Severity::Error,
            inst.line,
            inst.column,
            "components nest too deeply (limit " + std::to_string(kMaxComponentNesting) + ")"
        );
        return nullptr;
    }

    auto     componentScope = std::make_shared<SilDataContext>(scope);
    ParamText bindings;

    for (auto const& parameter : definition.params) {
        auto const found = inst.params.find(parameter.name);
        std::string rawValue;
        if (found != inst.params.end()) {
            rawValue = found->second;
        } else if (!parameter.defaultValue.empty()) {
            rawValue = parameter.defaultValue;
        } else {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Warning,
                inst.line,
                inst.column,
                "component '" + definition.name + "' is missing the argument '" + parameter.name + "'"
            );
            continue;
        }

        // Defaults and arguments may name $variables; expand them before the
        // argument is classified as literal or expression.
        {
            std::vector<std::string> visiting;
            rawValue = SubstituteVars(ctx, rawValue, visiting, inst.line, inst.column);
        }

        Argument argument = EvaluateArgument(ctx, rawValue, scope, inst.line, inst.column);
        componentScope->Set(parameter.name, argument.value);
        bindings.expressions[parameter.name] = std::move(argument.expression);
        bindings.plain[parameter.name]       = std::move(argument.plain);    }

    for (auto const& [name, value] : inst.params) {
        bool const known = std::any_of(
            definition.params.begin(),
            definition.params.end(),
            [&name](SilComponentDef::Param const& parameter) { return parameter.name == name; }
        );
        if (!known) {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Warning,
                inst.line,
                inst.column,
                "unknown argument '" + name + "' on component '" + definition.name + "'"
            );
        }
    }

    if (!inst.children.empty() && !BodyHasSlot(definition.body)) {
        report(
            *ctx.diagnostics,
            SilDiagnostic::Severity::Warning,
            inst.line,
            inst.column,
            "component '" + definition.name + "' has no Slot; its children are ignored"
        );
    }

    SlotBinding slot;
    slot.children = std::make_shared<std::vector<SilNode> const>(inst.children);
    slot.scope    = scope;
    if (callerParams) slot.params = *callerParams;

    ctx.componentStack.push_back(key);
    std::vector<WidgetPtr> built;
    for (auto const& bodyNode : definition.body) {
        if (IsSlotNode(bodyNode)) {
            AppendSlotChildren(ctx, slot, depth, built);
            continue;
        }
        if (auto widget = BuildNode(ctx, bodyNode, depth + 1, componentScope, &bindings, &slot)) {
            built.push_back(std::move(widget));
        }
    }
    ctx.componentStack.pop_back();

    if (built.empty()) return nullptr;

    WidgetPtr result;
    if (built.size() == 1) {
        result = std::move(built.front());
    } else {
        result = WidgetFactory::Create("Container");
        if (!result) {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Error,
                inst.line,
                inst.column,
                "multiple component roots need a 'Container' component"
            );
            return nullptr;
        }
        result->ApplyProperty("layout", "vertical");
        for (auto& widget : built) result->AddChild(std::move(widget));
    }

    // Properties on the instantiation (`name`, geometry, `class`, bindings)
    // override the definition's root, which makes a component adaptable without
    // editing its body.
    ApplyNodeToWidget(ctx, inst, *result);
    return result;
}

WidgetPtr BuildNode(
    CompileContext&                        ctx,
    SilNode const&                         raw,
    int                                    depth,
    std::shared_ptr<SilDataContext const> const& scope,
    ParamText const*                       params,
    SlotBinding const*                     slot
) {
    if (depth > kMaxDepth) {
        report(
            *ctx.diagnostics,
            SilDiagnostic::Severity::Error,
            raw.line,
            raw.column,
            "components nest too deeply (limit " + std::to_string(kMaxDepth) + ")"
        );
        return nullptr;
    }

    SilNode node = ExpandNode(ctx, raw, params);

    WidgetPtr widget = WidgetFactory::Create(node.type);
    if (!widget) {
        if (auto const* definition = FindComponent(*ctx.document, node.type)) {
            return BuildComponent(ctx, node, *definition, depth, scope, params);
        }
        report(
            *ctx.diagnostics,
            SilDiagnostic::Severity::Error,
            node.line,
            node.column,
            "unknown component '" + node.type + "' (register it with WidgetFactory or define a Component)"
        );
        return nullptr;
    }

    // The scope must exist before bindings evaluate: a ForEach row binds
    // `@item.name` immediately when the row is built.
    if (scope) widget->SetDataContext(scope);

    ApplyNodeToWidget(ctx, node, *widget);

    // ForEach is a repeater, not a container of literal children: its body is a
    // template rebuilt once per array element.
    if (auto forEach = std::dynamic_pointer_cast<SilicaForEach>(widget)) {
        auto const param = [&node](char const* name) -> std::string {
            for (auto const& [key, value] : node.params) {
                if (property::ToLower(property::Trim(key)) == name) return value;
            }
            return {};
        };

        std::string const data = param("data");
        if (data.empty()) {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Error,
                node.line,
                node.column,
                "'ForEach' needs a data parameter: ForEach (data: @rows, item: @row) { ... }"
            );
            return widget;
        }

        // The template keeps its own copy of the body and of the component
        // context, so the document may be released after compilation; rows are
        // rebuilt later from the shared definition map.
        auto const templateBody = node.children;
        auto const rowParams    = params ? *params : ParamText{};
        auto const rowSlot      = slot ? *slot : SlotBinding{};
        auto const document     = ctx.document;
        auto const baseStack    = ctx.componentStack;
        forEach->SetTemplate(
            [templateBody, depth, rowParams, rowSlot, document, baseStack](
                std::shared_ptr<SilDataContext const> const& rowScope,
                std::vector<SilDiagnostic>&                  rowDiagnostics
            ) -> std::vector<WidgetPtr> {
                CompileContext rowContext{document, &rowDiagnostics, baseStack};
                std::vector<WidgetPtr> rows;
                rows.reserve(templateBody.size());
                for (auto const& child : templateBody) {
                    if (IsSlotNode(child)) {
                        AppendSlotChildren(rowContext, rowSlot, depth, rows);
                        continue;
                    }
                    auto built = BuildNode(rowContext, child, depth + 1, rowScope, &rowParams, &rowSlot);
                    if (built) rows.push_back(std::move(built));
                }
                return rows;
            }
        );

        std::string error;
        if (!forEach->Configure(data, param("item"), param("index"), error, ctx.diagnostics)) {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Error,
                node.line,
                node.column,
                "cannot configure 'ForEach': " + error
            );
        }
        return widget;
    }

    for (auto const& child : node.children) {
        if (IsSlotNode(child)) {
            if (!slot) {
                report(
                    *ctx.diagnostics,
                    SilDiagnostic::Severity::Error,
                    child.line,
                    child.column,
                    "'Slot' can only be used inside a Component definition"
                );
                continue;
            }
            if (!slot->children) continue;
            for (auto const& provided : *slot->children) {
                auto built = BuildNode(ctx, provided, depth + 1, slot->scope, &slot->params, nullptr);
                if (!built) continue;
                if (!widget->AddChild(std::move(built))) {
                    report(
                        *ctx.diagnostics,
                        SilDiagnostic::Severity::Error,
                        provided.line,
                        provided.column,
                        "'" + node.type + "' cannot contain '" + provided.type + "'"
                    );
                }
            }
            continue;
        }

        WidgetPtr built = BuildNode(ctx, child, depth + 1, scope, params, slot);
        if (!built) continue;
        if (!widget->AddChild(std::move(built))) {
            report(
                *ctx.diagnostics,
                SilDiagnostic::Severity::Error,
                child.line,
                child.column,
                "'" + node.type + "' cannot contain '" + child.type + "'"
            );
        }
    }
    return widget;
}

} // namespace

std::string SilCompileResult::Report() const {
    std::string out;
    for (auto const& diagnostic : diagnostics) {
        if (!out.empty()) out += '\n';
        out += FormatDiagnostic(diagnostic, origin);
    }
    return out;
}

WidgetPtr UICompiler::Compile(std::string_view silSource) { return CompileDetailed(silSource).root; }

SilCompileResult UICompiler::CompileDetailed(std::string_view silSource, std::string origin) {
    SilCompileResult result;
    result.origin = origin;
    WidgetFactory::RegisterBuiltins();

    SilParser   parser(silSource);
    SilDocument parsed = parser.Parse();
    result.diagnostics = std::move(parsed.diagnostics);
    RegisterKeyframes(std::move(parsed.keyframes));

    auto document = std::make_shared<SilDocument const>(std::move(parsed));

    for (auto const& [name, definition] : document->components) {
        if (WidgetFactory::Has(name)) {
            report(
                result.diagnostics,
                SilDiagnostic::Severity::Warning,
                definition.line,
                definition.column,
                "component '" + name + "' has the same name as a builtin widget; the builtin is used"
            );
        }
    }

    if (document->roots.empty()) {
        if (!HasErrors(result.diagnostics)) {
            report(
                result.diagnostics,
                SilDiagnostic::Severity::Error,
                1,
                1,
                "the document does not define any component"
            );
        }
        return result;
    }

    CompileContext          ctx{document, &result.diagnostics, {}};
    std::vector<WidgetPtr> roots;
    roots.reserve(document->roots.size());
    for (auto const& node : document->roots) {
        if (auto widget = BuildNode(ctx, node, 0, nullptr, nullptr, nullptr)) roots.push_back(std::move(widget));
    }
    if (roots.empty()) return result;

    if (roots.size() == 1) {
        result.root = std::move(roots.front());
        return result;
    }

    // Several top-level components are stacked so nothing overlaps at (0, 0).
    // The wrapper also comes from the factory, so a custom "Container" stays in
    // charge of layout.
    WidgetPtr wrapper = WidgetFactory::Create("Container");
    if (!wrapper) {
        report(
            result.diagnostics,
            SilDiagnostic::Severity::Error,
            1,
            1,
            "multiple roots need a 'Container' component, but none is registered"
        );
        return result;
    }
    wrapper->ApplyProperty("layout", "vertical");
    for (auto& root : roots) wrapper->AddChild(std::move(root));
    result.root = std::move(wrapper);
    return result;
}

SilCompileResult UICompiler::CompileFile(std::string const& path) {
    std::string const resolved = runtime::ResolvePath(path);
    std::ifstream     file(resolved, std::ios::binary);
    if (!file) {
        SilCompileResult result;
        result.origin = resolved;
        report(result.diagnostics, SilDiagnostic::Severity::Error, 1, 1, "cannot open '" + resolved + "'");
        return result;
    }

    std::string source(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
    return CompileDetailed(source, resolved);
}

UIWidgetPtr UICompiler::CompileFile(std::string const& filepath, std::string const& fallbackSource) {
    try {
        SilCompileResult fromFile = CompileFile(filepath);
        if (fromFile.ok()) return fromFile.root;

        for (auto const& diagnostic : fromFile.diagnostics) {
            runtime::Log(FormatDiagnostic(diagnostic, fromFile.origin));
        }
        runtime::Log("silica: '" + filepath + "' is unavailable, compiling the fallback source");
    } catch (std::exception const& error) {
        runtime::Log("silica: exception loading '" + filepath + "': " + error.what());
    } catch (...) {
        runtime::Log("silica: unknown exception loading '" + filepath + "'");
    }

    SilCompileResult fallback = CompileDetailed(fallbackSource, filepath + " (fallback)");
    for (auto const& diagnostic : fallback.diagnostics) {
        runtime::Log(FormatDiagnostic(diagnostic, fallback.origin));
    }
    return fallback.root;
}

} // namespace silica
