#pragma once

#include "Variant.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace silica {

// Called for every `@name` (or bare name) the evaluator meets. Widgets resolve
// it against the DataStore first and their own state second.
using ValueResolver = std::function<Variant(std::string_view)>;

// A parsed .sil expression such as `@health > 0 ? "存活" : "死亡"`.
//
// The source is parsed once; Evaluate is cheap enough to run every frame for
// bindings that also read widget state. Supported grammar:
//
//   ternary    := equality ('?' ternary ':' ternary)?
//   equality   := comparison (('==' | '!=') comparison)*
//   comparison := additive (('>' | '<' | '>=' | '<=') additive)*
//   additive   := multiplicative (('+' | '-') multiplicative)*
//   multiplicative := unary (('*' | '/' | '%') unary)*
//   unary      := ('-' | '!') unary | primary
//   primary    := number | string | true | false | null | @name | name
//               | '@item.field' | '$local' | event.field | '(' ternary ')'
class Expression {
public:
    Expression() = default;

    // Returns nullopt and fills `error` when the text does not parse.
    static std::optional<Expression> Parse(std::string_view source, std::string& error);

    [[nodiscard]] bool        Empty() const { return !m_root; }
    [[nodiscard]] Variant     Evaluate(ValueResolver const& resolve) const;
    [[nodiscard]] std::string const& Source() const { return m_source; }

    // Root data keys referenced by the expression, without the leading '@' and
    // without any `.field` path (`@item.name` yields "item"). Used to subscribe
    // the owning widget to the store. Script locals ('$x') and event fields
    // ('event.value') are not data keys and are omitted.
    [[nodiscard]] std::vector<std::string> const& Identifiers() const { return m_identifiers; }

    // The single data key the expression reads, or nullopt for anything more
    // complex (including field paths such as `@item.name`). Two-way bindings
    // (`checked: @opt`) write user edits back only through this form.
    [[nodiscard]] std::optional<std::string> PlainIdentifier() const;

    struct Node; // opaque AST node, defined by the implementation

private:
    std::shared_ptr<Node const> m_root;
    std::vector<std::string>    m_identifiers;
    std::string                 m_source;
};

} // namespace silica
