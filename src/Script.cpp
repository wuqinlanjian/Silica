#include "silica/Script.h"

#include "silica/DataStore.h"
#include "silica/Property.h"

#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace silica {

Variant ScriptEvent::Field(std::string_view field) const {
    if (field == "name") return Variant{name};
    if (field == "value") return value;
    if (field == "text") return text;
    if (field == "checked") return checked;
    if (field == "index") return index;
    return Variant{};
}

namespace {

bool IsNameStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool IsNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

// Skips whitespace and both comment forms; returns false when a block comment
// was left open.
bool SkipTrivia(std::string_view source, size_t& pos) {
    while (pos < source.size()) {
        char const c = source[pos];
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            ++pos;
            continue;
        }
        if (c == '/' && pos + 1 < source.size() && source[pos + 1] == '/') {
            while (pos < source.size() && source[pos] != '\n') ++pos;
            continue;
        }
        if (c == '/' && pos + 1 < source.size() && source[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < source.size() && !(source[pos] == '*' && source[pos + 1] == '/')) ++pos;
            if (pos + 1 >= source.size()) return false;
            pos += 2;
            continue;
        }
        break;
    }
    return true;
}

// Matches a keyword when it is not glued to a longer name (`iffy` stays a name).
bool MatchWord(std::string_view source, size_t& pos, std::string_view word) {
    if (source.substr(pos, word.size()) != word) return false;
    size_t const end = pos + word.size();
    if (end < source.size() && IsNameChar(source[end])) return false;
    pos = end;
    return true;
}

// `pos` sits on the opening quote. An unterminated string consumes the rest of
// the source so the expression parser reports it instead of looping here.
bool SkipString(std::string_view source, size_t& pos) {
    char const quote = source[pos++];
    while (pos < source.size()) {
        char const c = source[pos];
        if (c == '\\' && pos + 1 < source.size()) {
            pos += 2;
            continue;
        }
        ++pos;
        if (c == quote) return true;
    }
    return false;
}

// Consumes a string or a comment when one starts at `pos`; false leaves `pos`
// unchanged so callers can inspect the character themselves.
bool SkipStringOrComment(std::string_view source, size_t& pos) {
    char const c = source[pos];
    if (c == '"' || c == '\'') {
        (void)SkipString(source, pos);
        return true;
    }
    if (c == '/' && pos + 1 < source.size() && source[pos + 1] == '/') {
        while (pos < source.size() && source[pos] != '\n') ++pos;
        return true;
    }
    if (c == '/' && pos + 1 < source.size() && source[pos + 1] == '*') {
        pos += 2;
        while (pos + 1 < source.size() && !(source[pos] == '*' && source[pos + 1] == '/')) ++pos;
        pos = pos + 1 < source.size() ? pos + 2 : source.size();
        return true;
    }
    return false;
}

} // namespace

class ScriptParser {
public:
    ScriptParser(std::string_view source, std::string& error) : m_source(source), m_error(error) {}

    bool ParseDocument(Script& script) { return ParseStatements(script.m_statements, false); }

    [[nodiscard]] bool failed() const { return m_failed; }

private:
    using Statement = Script::Statement;
    using Op        = Script::Statement::Op;

    void Fail(std::string message) {
        if (m_failed) return;
        m_failed = true;
        m_error  = std::move(message);
    }

    bool ParseStatements(std::vector<Statement>& out, bool braced) {
        while (true) {
            if (!SkipTrivia(m_source, m_pos)) {
                Fail("unterminated block comment");
                return false;
            }
            if (m_pos >= m_source.size()) {
                if (braced) {
                    Fail("expected '}'");
                    return false;
                }
                return true;
            }
            if (braced && m_source[m_pos] == '}') {
                ++m_pos;
                return true;
            }
            Statement statement;
            if (!ParseStatement(statement)) return false;
            out.push_back(std::move(statement));
        }
    }

    bool ParseStatement(Statement& statement) {
        if (MatchWord(m_source, m_pos, "if")) return ParseIf(statement);
        return ParseAssignment(statement);
    }

    bool ParseIf(Statement& statement) {
        statement.kind = Statement::Kind::If;
        if (!SkipTrivia(m_source, m_pos) || m_pos >= m_source.size() || m_source[m_pos] != '(') {
            Fail("expected '(' after 'if'");
            return false;
        }
        ++m_pos;

        std::string condition;
        if (!ScanGroup(condition)) return false;
        std::string error;
        auto        parsed = Expression::Parse(condition, error);
        if (!parsed) {
            Fail("in 'if (...)': " + error);
            return false;
        }
        statement.expression = std::move(*parsed);

        if (!SkipTrivia(m_source, m_pos) || m_pos >= m_source.size() || m_source[m_pos] != '{') {
            Fail("expected '{' after the 'if' condition");
            return false;
        }
        ++m_pos;
        if (!ParseStatements(statement.body, true)) return false;

        size_t const afterThen = m_pos;
        if (!SkipTrivia(m_source, m_pos)) {
            Fail("unterminated block comment");
            return false;
        }
        if (!MatchWord(m_source, m_pos, "else")) {
            m_pos = afterThen;
            return true;
        }
        if (!SkipTrivia(m_source, m_pos)) {
            Fail("unterminated block comment");
            return false;
        }
        if (m_pos < m_source.size() && m_source[m_pos] == '{') {
            ++m_pos;
            return ParseStatements(statement.alternate, true);
        }
        if (MatchWord(m_source, m_pos, "if")) {
            Statement nested;
            if (!ParseIf(nested)) return false;
            statement.alternate.push_back(std::move(nested));
            return true;
        }
        Fail("expected '{' or 'if' after 'else'");
        return false;
    }

    bool ParseAssignment(Statement& statement) {
        statement.kind = Statement::Kind::Assign;
        if (m_pos < m_source.size() && m_source[m_pos] == '@') {
            ++m_pos;
        } else if (m_pos < m_source.size() && m_source[m_pos] == '$') {
            statement.local = true;
            ++m_pos;
        }
        if (m_pos >= m_source.size() || !IsNameStart(m_source[m_pos])) {
            Fail("expected a data name or 'if' at the start of a statement");
            return false;
        }
        size_t const nameBegin = m_pos++;
        while (m_pos < m_source.size() && IsNameChar(m_source[m_pos])) ++m_pos;
        statement.target = std::string(m_source.substr(nameBegin, m_pos - nameBegin));

        if (!SkipTrivia(m_source, m_pos)) {
            Fail("unterminated block comment");
            return false;
        }
        if (m_pos >= m_source.size()) {
            Fail("expected an assignment after '" + statement.target + "'");
            return false;
        }

        char const c = m_source[m_pos];
        if (c == '=') {
            statement.op = Op::Set;
            ++m_pos;
        } else if ((c == '+' || c == '-' || c == '*' || c == '/') && m_pos + 1 < m_source.size()
                   && m_source[m_pos + 1] == '=') {
            statement.op = c == '+' ? Op::Add : c == '-' ? Op::Sub : c == '*' ? Op::Mul : Op::Div;
            m_pos += 2;
        } else {
            Fail("expected '=', '+=', '-=', '*=' or '/=' after '" + statement.target + "'");
            return false;
        }

        std::string expressionSource;
        if (!SkipTrivia(m_source, m_pos) || !ScanExpression(expressionSource)) return false;
        std::string error;
        auto        parsed = Expression::Parse(expressionSource, error);
        if (!parsed) {
            Fail("in '" + statement.target + " ...': " + error);
            return false;
        }
        statement.expression = std::move(*parsed);
        return true;
    }

    // `pos` sits just after '('. Captures the balanced text up to the closing
    // ')' and consumes it.
    bool ScanGroup(std::string& out) {
        size_t const begin = m_pos;
        int          depth = 1;
        while (m_pos < m_source.size()) {
            char const c = m_source[m_pos];
            if (SkipStringOrComment(m_source, m_pos)) continue;
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                if (--depth == 0) {
                    out = std::string(property::Trim(m_source.substr(begin, m_pos - begin)));
                    ++m_pos;
                    if (out.empty()) {
                        Fail("expected a condition inside 'if (...)'");
                        return false;
                    }
                    return true;
                }
            }
            ++m_pos;
        }
        Fail("expected ')' in the 'if' condition");
        return false;
    }

    // Runs to the next ';' (consumed) or the end of the enclosing block
    // (left for ParseStatements), skipping parentheses, strings and comments.
    bool ScanExpression(std::string& out) {
        size_t const begin = m_pos;
        int          depth = 0;
        while (m_pos < m_source.size()) {
            char const c = m_source[m_pos];
            if (SkipStringOrComment(m_source, m_pos)) continue;
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                if (depth > 0) --depth;
            } else if (depth == 0 && (c == ';' || c == '}')) {
                out = std::string(property::Trim(m_source.substr(begin, m_pos - begin)));
                if (c == ';') ++m_pos;
                if (out.empty()) {
                    Fail("expected an expression");
                    return false;
                }
                return true;
            }
            ++m_pos;
        }
        out = std::string(property::Trim(m_source.substr(begin)));
        if (out.empty()) {
            Fail("expected an expression");
            return false;
        }
        return true;
    }

    std::string_view m_source;
    std::string&     m_error;
    size_t           m_pos   = 0;
    bool             m_failed = false;
};

std::unique_ptr<Script> Script::Parse(std::string_view source, std::string& error) {
    error.clear();
    auto         script = std::unique_ptr<Script>(new Script());
    ScriptParser parser(source, error);
    if (!parser.ParseDocument(*script) || parser.failed()) {
        if (error.empty()) error = "invalid script";
        return nullptr;
    }
    if (script->m_statements.empty()) {
        error = "the script body is empty";
        return nullptr;
    }
    return script;
}

bool Script::Run(ValueResolver const& resolve) const {
    std::unordered_map<std::string, Variant> locals;
    ValueResolver scoped = [&](std::string_view name) -> Variant {
        if (!name.empty() && name.front() == '$') {
            auto const it = locals.find(std::string(name));
            return it == locals.end() ? Variant{} : it->second;
        }
        return resolve ? resolve(name) : Variant{};
    };
    return Execute(m_statements, scoped, locals, 0);
}

bool Script::Execute(
    std::vector<Statement> const&             statements,
    ValueResolver const&                      resolve,
    std::unordered_map<std::string, Variant>& locals,
    int                                       depth
) {
    if (depth > 16) return false;

    using Op   = Statement::Op;
    using Kind = Statement::Kind;

    auto const combine = [](Op op, Variant const& current, Variant const& rhs) {
        auto const finite = [](double value) { return std::isfinite(value) ? value : 0.0; };
        switch (op) {
        case Op::Add:
            if (current.IsString() || rhs.IsString()) {
                return Variant{current.ToString() + rhs.ToString()};
            }
            return Variant{finite(current.ToNumber() + rhs.ToNumber())};
        case Op::Sub: return Variant{finite(current.ToNumber() - rhs.ToNumber())};
        case Op::Mul: return Variant{finite(current.ToNumber() * rhs.ToNumber())};
        case Op::Div: {
            double const divisor = rhs.ToNumber();
            return Variant{divisor == 0.0 ? 0.0 : finite(current.ToNumber() / divisor)};
        }
        case Op::Set:
            break;
        }
        return rhs;
    };

    // The store feeds layout and drawing, so a non-finite number would surface
    // as a poisoned coordinate; fold those back to zero before storing.
    auto const sanitize = [](Variant value) {
        if (value.IsNumber() && !std::isfinite(value.ToNumber())) return Variant{0.0};
        return value;
    };

    for (auto const& statement : statements) {
        if (statement.kind == Statement::Kind::If) {
            Variant const condition = statement.expression.Evaluate(resolve);
            auto const&   branch    = condition.Truthy() ? statement.body : statement.alternate;
            if (!Execute(branch, resolve, locals, depth + 1)) return false;
            continue;
        }

        Variant value = statement.expression.Evaluate(resolve);
        if (statement.op != Op::Set) {
            Variant current;
            if (statement.local) {
                auto const it = locals.find("$" + statement.target);
                if (it != locals.end()) current = it->second;
            } else {
                current = DataStore::Get(statement.target);
            }
            value = combine(statement.op, current, value);
        }

        if (statement.local) {
            locals["$" + statement.target] = sanitize(std::move(value));
        } else {
            DataStore::Set(statement.target, sanitize(std::move(value)));
        }
    }
    return true;
}

} // namespace silica
