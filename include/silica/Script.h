#pragma once

#include "Expression.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace silica {

// Payload of the widget event a script is handling. Scripts read it as
// `event.value`, `event.checked`, `event.text`, `event.index` and `event.name`.
struct ScriptEvent {
    std::string name;
    Variant     value;
    Variant     text;
    Variant     checked;
    Variant     index;

    [[nodiscard]] Variant Field(std::string_view field) const;
};

// A parsed `{ ... }` event script:
//
//   Button { onClick: { @health += 10; if (@health > 120) { @health = 120; } } }
//   Checkbox { checked: @opt; onChange: { @theme = event.checked ? "on" : "off"; } }
//
// Statements are assignments (`@key`, plain names, or `$local`) and if/else
// blocks. Store writes go through DataStore::Set, so bindings refresh
// synchronously while the event is handled. `$locals` live for one run only.
class Script {
public:
    // Returns nullptr and fills `error` when the body does not parse.
    static std::unique_ptr<Script> Parse(std::string_view source, std::string& error);

    [[nodiscard]] bool Empty() const { return m_statements.empty(); }

    // Runs every statement. The resolver is the same one used by bindings, so
    // `@health` reads the store, `@checked` reads widget state and `event.*`
    // reads the payload of the event that triggered the script.
    bool Run(ValueResolver const& resolve) const;

private:
    friend class ScriptParser;

    Script() = default;

    struct Statement {
        enum class Kind { Assign, If };
        enum class Op { Set, Add, Sub, Mul, Div };

        Kind kind = Kind::Assign;
        Op   op   = Op::Set;

        bool        local = false; // `$name`, scoped to this run
        std::string target;        // key without '@'

        Expression             expression; // right-hand side or `if` condition
        std::vector<Statement> body;       // `if` then-branch
        std::vector<Statement> alternate;  // `if` else-branch
    };

    static bool Execute(
        std::vector<Statement> const&           statements,
        ValueResolver const&                     resolve,
        std::unordered_map<std::string, Variant>& locals,
        int                                      depth
    );

    std::vector<Statement> m_statements;
};

} // namespace silica
