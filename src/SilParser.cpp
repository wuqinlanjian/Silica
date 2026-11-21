#include "silica/SilParser.h"

#include "silica/Property.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace silica {
namespace {

constexpr int    kMaxDepth       = 64;
constexpr size_t kMaxDiagnostics = 200;

bool isIdentStart(unsigned char c) { return std::isalpha(c) != 0 || c == '_'; }

bool isIdentChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.';
}

bool isHexDigit(unsigned char c) { return std::isxdigit(c) != 0; }

std::string Quoted(std::string const& text) { return "'" + text + "'"; }

} // namespace

std::string FormatDiagnostic(SilDiagnostic const& diagnostic, std::string_view origin) {
    std::string out(origin);
    out += ':';
    out += std::to_string(diagnostic.line);
    out += ':';
    out += std::to_string(diagnostic.column);
    out += diagnostic.severity == SilDiagnostic::Severity::Error ? ": error: " : ": warning: ";
    out += diagnostic.message;
    return out;
}

SilParser::SilParser(std::string_view source) : source_(source) {}

void SilParser::Tokenize() {
    int    line   = 1;
    int    column = 1;
    size_t i      = 0;

    auto advance = [&](size_t count = 1) {
        for (size_t n = 0; n < count && i < source_.size(); ++n, ++i) {
            if (source_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
    };
    auto peekChar = [&](size_t ahead = 0) -> char {
        return i + ahead < source_.size() ? source_[i + ahead] : '\0';
    };

    while (i < source_.size()) {
        unsigned char const c = static_cast<unsigned char>(source_[i]);

        if (std::isspace(c) != 0) {
            advance();
            continue;
        }
        if (c == '/' && peekChar(1) == '/') {
            while (i < source_.size() && source_[i] != '\n') advance();
            continue;
        }
        if (c == '/' && peekChar(1) == '*') {
            int const startLine   = line;
            int const startColumn = column;
            advance(2);
            bool closed = false;
            while (i < source_.size()) {
                if (source_[i] == '*' && peekChar(1) == '/') {
                    advance(2);
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) Error(startLine, startColumn, "unterminated block comment");
            continue;
        }

        int const    tokenLine   = line;
        int const    tokenColumn = column;
        size_t const tokenBegin  = i;

        if (c == '{' || c == '}' || c == ':' || c == ';' || c == '(' || c == ')' || c == ','
            || c == '?' || c == '=' || c == '+' || c == '*' || c == '/' || c == '%' || c == '<'
            || c == '>' || c == '!') {
            TokenKind kind = TokenKind::End;
            switch (c) {
            case '{': kind = TokenKind::LBrace; break;
            case '}': kind = TokenKind::RBrace; break;
            case '(': kind = TokenKind::LParen; break;
            case ')': kind = TokenKind::RParen; break;
            case ',': kind = TokenKind::Comma; break;
            case '?': kind = TokenKind::Question; break;
            case ':': kind = TokenKind::Colon; break;
            case '=': kind = TokenKind::Assign; break;
            case '+': kind = TokenKind::Plus; break;
            case '*': kind = TokenKind::Star; break;
            case '/': kind = TokenKind::Slash; break;
            case '%': kind = TokenKind::Percent; break;
            case '<': kind = TokenKind::Less; break;
            case '>': kind = TokenKind::Greater; break;
            case '!': kind = TokenKind::Bang; break;
            default: kind = TokenKind::Semicolon; break;
            }
            tokens_.push_back(
                {kind, std::string(1, static_cast<char>(c)), tokenLine, tokenColumn, tokenBegin, tokenBegin + 1}
            );
            advance();
            continue;
        }

        if (c == '"') {
            advance();
            std::string text;
            bool        closed = false;
            while (i < source_.size()) {
                char const d = source_[i];
                if (d == '"') {
                    advance();
                    closed = true;
                    break;
                }
                if (d == '\n') break;
                if (d == '\\' && i + 1 < source_.size()) {
                    char const escaped = source_[i + 1];
                    switch (escaped) {
                    case 'n': text += '\n'; break;
                    case 't': text += '\t'; break;
                    case 'r': text += '\r'; break;
                    case '"': text += '"'; break;
                    case '\\': text += '\\'; break;
                    default: text += escaped; break;
                    }
                    advance(2);
                    continue;
                }
                text += d;
                advance();
            }
            if (!closed) Error(tokenLine, tokenColumn, "unterminated string");
            tokens_.push_back({TokenKind::String, std::move(text), tokenLine, tokenColumn, tokenBegin, i});
            continue;
        }

        if (c == '#') {
            advance();
            std::string text = "#";
            while (i < source_.size() && isHexDigit(static_cast<unsigned char>(source_[i]))) {
                text += source_[i];
                advance();
            }
            if (text.size() == 1) Error(tokenLine, tokenColumn, "expected hex digits after '#'");
            tokens_.push_back({TokenKind::Color, std::move(text), tokenLine, tokenColumn, tokenBegin, i});
            continue;
        }

        bool const startsNumber =
            std::isdigit(c) != 0
            || ((c == '-' || c == '.') && std::isdigit(static_cast<unsigned char>(peekChar(1))) != 0);
        if (startsNumber) {
            std::string text;
            if (c == '-' || c == '.') {
                text += static_cast<char>(c);
                advance();
            }
            while (i < source_.size() && std::isdigit(static_cast<unsigned char>(source_[i])) != 0) {
                text += source_[i];
                advance();
            }
            if (i < source_.size() && source_[i] == '.'
                && std::isdigit(static_cast<unsigned char>(peekChar(1))) != 0) {
                text += '.';
                advance();
                while (i < source_.size() && std::isdigit(static_cast<unsigned char>(source_[i])) != 0) {
                    text += source_[i];
                    advance();
                }
            }
            while (i < source_.size()
                   && (std::isalpha(static_cast<unsigned char>(source_[i])) != 0 || source_[i] == '%')) {
                text += source_[i];
                advance();
            }
            tokens_.push_back({TokenKind::Number, std::move(text), tokenLine, tokenColumn, tokenBegin, i});
            continue;
        }

        // A '-' that does not start a number is the subtraction operator; '-' in
        // an identifier (`margin-left`) is consumed by the identifier branch.
        if (c == '-') {
            tokens_.push_back({TokenKind::Minus, "-", tokenLine, tokenColumn, tokenBegin, tokenBegin + 1});
            advance();
            continue;
        }

        if (isIdentStart(c) || c == '@' || c == '$') {
            std::string text;
            if (c == '@' || c == '$') {
                text += static_cast<char>(c);
                advance();
            }
            while (i < source_.size() && isIdentChar(static_cast<unsigned char>(source_[i]))) {
                text += source_[i];
                advance();
            }
            if (text == "@") {
                Error(tokenLine, tokenColumn, "expected a name after '@'");
                continue;
            }
            if (text == "$") {
                Error(tokenLine, tokenColumn, "expected a name after '$'");
                continue;
            }
            tokens_.push_back({TokenKind::Identifier, std::move(text), tokenLine, tokenColumn, tokenBegin, i});
            continue;
        }

        Error(
            tokenLine,
            tokenColumn,
            std::string("unexpected character '") + static_cast<char>(c) + "'"
        );
        advance();
    }

    tokens_.push_back({TokenKind::End, {}, line, column, i, i});
}

SilDocument SilParser::Parse() {
    document_ = {};
    tokens_.clear();
    index_ = 0;
    Tokenize();
    ParseRoots();
    return std::move(document_);
}

void SilParser::ParseRoots() {
    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::RBrace)) {
            ErrorHere("unexpected '}' at the top level");
            Advance();
            continue;
        }
        if (Check(TokenKind::Identifier)) {
            std::string const keyword = property::ToLower(Peek().text);
            if (keyword == "@keyframes") {
                ParseKeyframes();
                continue;
            }
            // Definitions are told apart from widgets by their shape, so a
            // widget named "Var" or "Style" still parses as a widget.
            if (keyword == "var" && Peek(1).kind == TokenKind::Identifier && Peek(2).kind == TokenKind::Colon) {
                ParseVar();
                continue;
            }
            if (keyword == "style" && Peek(1).kind == TokenKind::Identifier
                && (Peek(2).kind == TokenKind::LBrace
                    || (Peek(2).kind == TokenKind::Identifier
                        && property::ToLower(Peek(2).text) == "extends"))) {
                ParseStyle();
                continue;
            }
            if (keyword == "component" && Peek(1).kind == TokenKind::Identifier
                && (Peek(2).kind == TokenKind::LBrace || Peek(2).kind == TokenKind::LParen)) {
                ParseComponent();
                continue;
            }
            auto node = ParseNode(0);
            if (node) document_.roots.push_back(std::move(*node));
            continue;
        }
        ErrorHere("expected a component type name");
        Advance();
    }
}

// `Var gutter: 6;` — a compile-time text constant referenced as `$gutter`.
void SilParser::ParseVar() {
    Advance(); // 'Var'
    Token const name = Advance();
    Advance(); // ':'

    std::vector<Token> values;
    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::Semicolon)) {
            Advance();
            break;
        }
        if (Check(TokenKind::RBrace)) break;
        if (Check(TokenKind::LBrace)) {
            Error(name.line, name.column, "variable " + Quoted(name.text) + " expects a value, not a block");
            SkipBlock();
            break;
        }
        values.push_back(Advance());
    }

    if (values.empty()) {
        Error(name.line, name.column, "variable " + Quoted(name.text) + " is missing a value");
        return;
    }
    if (document_.vars.contains(name.text)) {
        Warning(name.line, name.column, "duplicate variable " + Quoted(name.text) + "; the last value wins");
    }
    document_.vars[name.text] = JoinTokens(values);
}

// `Style Card extends Surface { ... }` — property sets without children or
// event scripts; UICompiler expands them where a node names the style.
void SilParser::ParseStyle() {
    Advance(); // 'Style'
    Token const name = Advance();

    SilStyle style;
    style.name   = name.text;
    style.line   = name.line;
    style.column = name.column;

    if (Check(TokenKind::Identifier) && property::ToLower(Peek().text) == "extends") {
        Advance();
        if (!Check(TokenKind::Identifier)) {
            ErrorHere("expected a style name after 'extends'");
            return;
        }
        style.base = Advance().text;
    }

    if (!Check(TokenKind::LBrace)) {
        Error(name.line, name.column, "expected '{' after Style " + Quoted(style.name));
        SkipStatement();
        return;
    }
    Advance();

    SilNode holder;
    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::RBrace)) {
            Advance();
            break;
        }
        if (Check(TokenKind::Semicolon)) {
            Advance();
            continue;
        }
        if (Check(TokenKind::Identifier) && Peek(1).kind == TokenKind::Colon) {
            ParseProperty(holder, false);
            continue;
        }
        if (Check(TokenKind::Identifier) && (Peek(1).kind == TokenKind::LBrace || Peek(1).kind == TokenKind::LParen)) {
            ErrorHere("styles cannot contain child components");
            auto child = ParseNode(0);
            (void)child;
            continue;
        }
        ErrorHere("expected a property inside Style " + Quoted(style.name));
        Advance();
        SkipStatement();
    }

    style.values.props     = std::move(holder.props);
    style.values.bindings  = std::move(holder.bindings);
    style.values.templates = std::move(holder.templates);

    if (document_.styles.contains(style.name)) {
        Warning(name.line, name.column, "duplicate style " + Quoted(style.name) + "; the last one wins");
    }
    document_.styles[style.name] = std::move(style);
}

// `Component Stat(label, value: 0) { ... }` — a reusable subtree. Parameters
// may carry a raw default value; the body is stored as nodes and built once
// per instantiation by UICompiler.
void SilParser::ParseComponent() {
    Advance(); // 'Component'
    Token const name = Advance();

    SilComponentDef definition;
    definition.name   = name.text;
    definition.line   = name.line;
    definition.column = name.column;

    if (Check(TokenKind::LParen)) {
        Advance();
        while (!Check(TokenKind::End)) {
            if (Check(TokenKind::RParen)) {
                Advance();
                break;
            }
            if (Check(TokenKind::Comma) || Check(TokenKind::Semicolon)) {
                Advance();
                continue;
            }
            if (!Check(TokenKind::Identifier)) {
                ErrorHere("expected a parameter name");
                Advance();
                while (!Check(TokenKind::End) && !Check(TokenKind::Comma) && !Check(TokenKind::RParen)) {
                    Advance();
                }
                continue;
            }

            Token const parameter = Advance();
            SilComponentDef::Param entry;
            entry.name = parameter.text;

            if (Check(TokenKind::Colon)) {
                Advance();
                std::vector<Token> values;
                while (!Check(TokenKind::End) && !Check(TokenKind::Comma) && !Check(TokenKind::RParen)) {
                    values.push_back(Advance());
                }
                if (values.empty()) {
                    Error(parameter.line, parameter.column, "parameter " + Quoted(entry.name) + " is missing a default value");
                } else {
                    entry.defaultValue = JoinTokens(values);
                }
            }
            definition.params.push_back(std::move(entry));

            if (Check(TokenKind::Comma)) {
                Advance();
                continue;
            }
            if (Check(TokenKind::RParen)) {
                Advance();
                break;
            }
            ErrorHere("expected ',' or ')' after parameter");
            SkipStatement();
            break;
        }
    }

    if (!Check(TokenKind::LBrace)) {
        Error(name.line, name.column, "expected '{' after Component " + Quoted(definition.name));
        SkipStatement();
        return;
    }
    Advance();

    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::RBrace)) {
            Advance();
            break;
        }
        if (Check(TokenKind::Semicolon)) {
            Advance();
            continue;
        }
        if (Check(TokenKind::Identifier) && Peek(1).kind == TokenKind::Colon) {
            ErrorHere("a component body starts with a component; properties belong on the nodes inside");
            SkipStatement();
            continue;
        }
        auto child = ParseNode(1);
        if (child) definition.body.push_back(std::move(*child));
    }

    if (document_.components.contains(definition.name)) {
        Warning(name.line, name.column, "duplicate component " + Quoted(definition.name) + "; the last one wins");
    }
    document_.components[definition.name] = std::move(definition);
}

// @keyframes pulse {
//     0% { opacity: 1; transform: scale(1); }
//     50% { opacity: .6; transform: scale(.95); }
// }
void SilParser::ParseKeyframes() {
    Token const keyword = Advance();
    if (!Check(TokenKind::Identifier)) {
        ErrorHere("expected a name after @keyframes");
        return;
    }
    Token const name = Advance();

    if (!Check(TokenKind::LBrace)) {
        Error(keyword.line, keyword.column, "expected '{' after @keyframes " + Quoted(name.text));
        return;
    }
    Advance();

    KeyframeSet set;
    set.name = name.text;

    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::RBrace)) {
            Advance();
            break;
        }
        if (Check(TokenKind::Semicolon)) {
            Advance();
            continue;
        }
        if (!Check(TokenKind::Number)) {
            ErrorHere("expected a keyframe percentage such as 0%");
            Advance();
            SkipStatement();
            continue;
        }

        Token       percent = Advance();
        std::string digits  = percent.text;
        if (!digits.empty() && digits.back() == '%') digits.pop_back();
        auto const offset = property::ParseNumber(digits);
        if (!offset) Error(percent.line, percent.column, "expected a percentage such as 50%");

        if (!Check(TokenKind::LBrace)) {
            ErrorHere("expected '{' after keyframe " + Quoted(percent.text));
            SkipStatement();
            continue;
        }
        Advance();

        SilNode holder;
        while (!Check(TokenKind::End)) {
            if (Check(TokenKind::RBrace)) {
                Advance();
                break;
            }
            if (Check(TokenKind::Semicolon)) {
                Advance();
                continue;
            }
            if (Check(TokenKind::Identifier) && Peek(1).kind == TokenKind::Colon) {
                ParseProperty(holder);
                continue;
            }
            ErrorHere("expected a property inside the keyframe");
            Advance();
            SkipStatement();
        }

        KeyframeStep step;
        step.offset = offset ? std::clamp(*offset / 100.f, 0.f, 1.f) : 0.f;
        step.props  = std::move(holder.props);
        set.steps.push_back(std::move(step));
    }

    std::stable_sort(set.steps.begin(), set.steps.end(), [](KeyframeStep const& a, KeyframeStep const& b) {
        return a.offset < b.offset;
    });
    if (!set.steps.empty()) document_.keyframes[set.name] = std::move(set);
}

std::optional<SilNode> SilParser::ParseNode(int depth) {
    if (depth > kMaxDepth) {
        ErrorHere("components nest too deeply (limit " + std::to_string(kMaxDepth) + ")");
        Advance();
        SkipBlock();
        return std::nullopt;
    }

    Token const typeToken = Advance();
    SilNode     node;
    node.type   = typeToken.text;
    node.line   = typeToken.line;
    node.column = typeToken.column;

    // Arguments precede the body: `ForEach (data: @rows, item: @row) { ... }`.
    if (Check(TokenKind::LParen)) ParseParams(node);

    if (!Check(TokenKind::LBrace)) {
        ErrorHere("expected '{' after component " + Quoted(node.type));
        return std::nullopt;
    }
    Advance();
    ParseBody(node, depth);
    return node;
}

// `(data: @rows, item: @row, index: @i)` — every value keeps its source slice
// (minus surrounding whitespace), so the widget receives the same text the
// expression evaluator expects.
void SilParser::ParseParams(SilNode& node) {
    Advance(); // '('

    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::RParen)) {
            Advance();
            return;
        }
        if (Check(TokenKind::Comma) || Check(TokenKind::Semicolon)) {
            Advance();
            continue;
        }
        if (!Check(TokenKind::Identifier)) {
            ErrorHere("expected a parameter name");
            Advance();
            while (!Check(TokenKind::End) && !Check(TokenKind::Comma) && !Check(TokenKind::RParen)) Advance();
            continue;
        }

        Token const key = Advance();
        if (!Check(TokenKind::Colon)) {
            Error(key.line, key.column, "expected ':' after parameter " + Quoted(key.text));
            while (!Check(TokenKind::End) && !Check(TokenKind::Comma) && !Check(TokenKind::RParen)) Advance();
            continue;
        }
        Advance(); // ':'

        std::vector<Token> values;
        while (!Check(TokenKind::End) && !Check(TokenKind::Comma) && !Check(TokenKind::RParen)) {
            values.push_back(Advance());
        }
        if (values.empty()) {
            Error(key.line, key.column, "parameter " + Quoted(key.text) + " is missing a value");
        } else {
            size_t const begin = values.front().begin;
            size_t const end   = values.back().end;
            node.params[key.text] = std::string(property::Trim(source_.substr(begin, end - begin)));
        }

        if (Check(TokenKind::Comma)) {
            Advance();
            continue;
        }
        if (Check(TokenKind::RParen)) {
            Advance();
            return;
        }
        Error(key.line, key.column, "expected ',' or ')' after parameter " + Quoted(key.text));
        return;
    }

    ErrorHere("expected ')' closing the parameter list");
}

void SilParser::ParseBody(SilNode& node, int depth) {
    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::RBrace)) {
            Advance();
            return;
        }
        if (Check(TokenKind::Semicolon)) {
            Advance();
            continue;
        }
        if (Check(TokenKind::Identifier)) {
            TokenKind const next = Peek(1).kind;
            if (next == TokenKind::Colon) {
                ParseProperty(node);
                continue;
            }
            if (next == TokenKind::LBrace || next == TokenKind::LParen) {
                auto child = ParseNode(depth + 1);
                if (child) node.children.push_back(std::move(*child));
                continue;
            }
            ErrorHere("expected ':' or '{' after " + Quoted(Peek().text));
            Advance();
            SkipStatement();
            continue;
        }

        std::string const label = Peek().text.empty() ? "end of file" : Quoted(Peek().text);
        ErrorHere("unexpected " + label + " inside " + Quoted(node.type));
        Advance();
    }

    ErrorHere("missing '}' for " + Quoted(node.type) + " opened at line " + std::to_string(node.line));
}

void SilParser::ParseProperty(SilNode& node, bool allowScripts) {
    Token const key = Advance();
    Advance(); // ':'

    if (Check(TokenKind::LBrace)) {
        if (!allowScripts) {
            Error(key.line, key.column, "styles cannot run scripts; " + Quoted(key.text) + " is only valid on a component");
            SkipBlock();
            return;
        }
        ParseScriptProperty(node, key);
        return;
    }

    std::vector<Token> values;
    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::Semicolon)) {
            Advance();
            break;
        }
        if (Check(TokenKind::RBrace)) break;
        if (Check(TokenKind::Identifier) && Peek(1).kind == TokenKind::Colon) {
            ErrorHere("missing ';' after property " + Quoted(key.text));
            break;
        }
        if (Check(TokenKind::LBrace)) {
            Error(key.line, key.column, "property " + Quoted(key.text) + " expects a value, not a block");
            SkipBlock();
            break;
        }
        values.push_back(Advance());
    }

    if (values.empty()) {
        Error(key.line, key.column, "property " + Quoted(key.text) + " is missing a value");
        return;
    }

    // A leading '@' turns the whole value into a reactive binding. The original
    // source slice is kept so strings keep their quotes and operators stay
    // exactly as the evaluator expects.
    if (values.front().kind == TokenKind::Identifier && !values.front().text.empty()
        && values.front().text.front() == '@') {
        size_t const begin = values.front().begin;
        size_t const end   = values.back().end;
        std::string  expression(property::Trim(source_.substr(begin, end - begin)));
        if (node.props.contains(key.text) || node.bindings.contains(key.text)) {
            Warning(key.line, key.column, "duplicate property " + Quoted(key.text) + "; the last value wins");
        }
        node.bindings[key.text] = std::move(expression);
        return;
    }

    std::string value = JoinTokens(values);

    // `text: "{{@item.name}} - {{@item.count}}"` interpolates at evaluation
    // time; the whole literal is kept so the surrounding text survives.
    if (value.find("{{") != std::string::npos) {
        if (node.props.contains(key.text) || node.bindings.contains(key.text)
            || node.templates.contains(key.text)) {
            Warning(key.line, key.column, "duplicate property " + Quoted(key.text) + "; the last value wins");
        }
        node.templates[key.text] = std::move(value);
        return;
    }

    if (node.props.contains(key.text) || node.bindings.contains(key.text)) {
        Warning(key.line, key.column, "duplicate property " + Quoted(key.text) + "; the last value wins");
    }
    node.props[key.text] = std::move(value);
}

// `onClick: { @health = @health + 10; }` — the body is stored verbatim (minus
// the braces); Script parses the statements when the widget is built.
void SilParser::ParseScriptProperty(SilNode& node, Token const& key) {
    Advance(); // the opening '{'
    size_t const bodyBegin = Peek().begin;

    int depth = 1;
    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::LBrace)) {
            ++depth;
            Advance();
            continue;
        }
        if (Check(TokenKind::RBrace)) {
            --depth;
            if (depth == 0) break;
            Advance();
            continue;
        }
        Advance();
    }

    if (!Check(TokenKind::RBrace)) {
        Error(key.line, key.column, "missing '}' after the script for " + Quoted(key.text));
        return;
    }

    size_t const bodyEnd = Peek().begin;
    Advance(); // the closing '}'

    if (node.props.contains(key.text) || node.scripts.contains(key.text)) {
        Warning(key.line, key.column, "duplicate property " + Quoted(key.text) + "; the last value wins");
    }
    node.scripts[key.text] =
        std::string(property::Trim(source_.substr(bodyBegin, bodyEnd - bodyBegin)));
}

// Rebuilds a property value from its tokens: string tokens keep their decoded
// text, and a space is inserted between words but not around commas or inside
// a call (`rgb(1, 2, 3)`).
std::string SilParser::JoinTokens(std::vector<Token> const& tokens) const {
    std::string value;
    for (size_t i = 0; i < tokens.size(); ++i) {
        Token const& token = tokens[i];
        bool const tightBefore = token.kind == TokenKind::Comma || token.kind == TokenKind::RParen;
        bool const tightAfterParen = i > 0 && tokens[i - 1].kind == TokenKind::LParen;
        if (!value.empty() && !tightBefore && !tightAfterParen) value += ' ';
        value += token.text;
    }
    return value;
}

void SilParser::SkipStatement() {
    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::Semicolon)) {
            Advance();
            return;
        }
        if (Check(TokenKind::RBrace)) return;
        if (Check(TokenKind::LBrace)) {
            SkipBlock();
            continue;
        }
        Advance();
    }
}

void SilParser::SkipBlock() {
    if (!Check(TokenKind::LBrace)) {
        Advance();
        return;
    }
    int depth = 0;
    while (!Check(TokenKind::End)) {
        if (Check(TokenKind::LBrace)) {
            ++depth;
        } else if (Check(TokenKind::RBrace)) {
            --depth;
            Advance();
            if (depth <= 0) return;
            continue;
        }
        Advance();
    }
}

SilParser::Token const& SilParser::Peek(size_t ahead) const {
    size_t const at = index_ + ahead;
    return at < tokens_.size() ? tokens_[at] : tokens_.back();
}

SilParser::Token SilParser::Advance() {
    Token const token = Peek();
    if (index_ + 1 < tokens_.size()) ++index_;
    return token;
}

bool SilParser::Check(TokenKind kind, size_t ahead) const { return Peek(ahead).kind == kind; }

void SilParser::Error(int line, int column, std::string message) {
    if (document_.diagnostics.size() >= kMaxDiagnostics) return;
    document_.diagnostics.push_back(
        {SilDiagnostic::Severity::Error, line, column, std::move(message)}
    );
}

void SilParser::Warning(int line, int column, std::string message) {
    if (document_.diagnostics.size() >= kMaxDiagnostics) return;
    document_.diagnostics.push_back(
        {SilDiagnostic::Severity::Warning, line, column, std::move(message)}
    );
}

void SilParser::ErrorHere(std::string message) { Error(Peek().line, Peek().column, std::move(message)); }

} // namespace silica
