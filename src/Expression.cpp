#include "silica/Expression.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace silica {

struct Expression::Node {
    enum class Kind { Literal, Identifier, Unary, Binary, Ternary };
    enum class Op {
        Add,
        Sub,
        Mul,
        Div,
        Modulo,
        Equal,
        NotEqual,
        Less,
        Greater,
        LessEqual,
        GreaterEqual,
        Negate,
        Not,
    };

    Kind  kind = Kind::Literal;
    Op    op   = Op::Add;
    Variant literal;
    std::string name;

    std::shared_ptr<Node const> a;
    std::shared_ptr<Node const> b;
    std::shared_ptr<Node const> c;
};

namespace {

using NodePtr = std::shared_ptr<Expression::Node const>;

enum class Kind {
    End,
    Number,
    String,
    Identifier,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Question,
    Colon,
    LParen,
    RParen,
    Equal,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
};

struct Token {
    Kind        kind = Kind::End;
    std::string text;
    double      number = 0.0;
    size_t      at     = 0;
};

class Lexer {
public:
    Lexer(std::string_view source, std::string& error) : m_source(source), m_error(error) {}

    Token Next() {
        SkipTrivia();
        Token token;
        token.at = m_pos;
        if (m_pos >= m_source.size()) return token;

        char const c = m_source[m_pos];
        if (std::isdigit(static_cast<unsigned char>(c)) != 0
            || (c == '.' && std::isdigit(Peek(1)) != 0)) {
            return Number();
        }
        if (c == '"' || c == '\'') return String(c);
        if (c == '@' || c == '$' || std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            return Identifier();
        }

        ++m_pos;
        switch (c) {
        case '+': token.kind = Kind::Plus; break;
        case '-': token.kind = Kind::Minus; break;
        case '*': token.kind = Kind::Star; break;
        case '/': token.kind = Kind::Slash; break;
        case '%': token.kind = Kind::Percent; break;
        case '!':
            if (Match('=')) {
                token.kind = Kind::NotEqual;
            } else {
                token.kind = Kind::Bang;
            }
            break;
        case '?': token.kind = Kind::Question; break;
        case ':': token.kind = Kind::Colon; break;
        case '(': token.kind = Kind::LParen; break;
        case ')': token.kind = Kind::RParen; break;
        case '=':
            if (Match('=')) {
                token.kind = Kind::Equal;
            } else {
                Fail("use '==' to compare; '=' only assigns inside a script");
                token.kind = Kind::Equal;
            }
            break;
        case '<':
            token.kind = Match('=') ? Kind::LessEqual : Kind::Less;
            break;
        case '>':
            token.kind = Match('=') ? Kind::GreaterEqual : Kind::Greater;
            break;
        default:
            Fail(std::string("unexpected character '") + c + "'");
            token.kind = Kind::End;
            break;
        }
        return token;
    }

    [[nodiscard]] bool failed() const { return m_failed; }

private:
    void Fail(std::string message) {
        if (m_failed) return;
        m_failed = true;
        m_error  = std::move(message);
    }

    char Peek(size_t ahead) const {
        return m_pos + ahead < m_source.size() ? m_source[m_pos + ahead] : '\0';
    }

    bool Match(char expected) {
        if (m_pos < m_source.size() && m_source[m_pos] == expected) {
            ++m_pos;
            return true;
        }
        return false;
    }

    void SkipTrivia() {
        while (m_pos < m_source.size()) {
            char const c = m_source[m_pos];
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++m_pos;
                continue;
            }
            if (c == '/' && Peek(1) == '/') {
                while (m_pos < m_source.size() && m_source[m_pos] != '\n') ++m_pos;
                continue;
            }
            if (c == '/' && Peek(1) == '*') {
                size_t const start = m_pos;
                m_pos += 2;
                while (m_pos + 1 < m_source.size()
                       && !(m_source[m_pos] == '*' && m_source[m_pos + 1] == '/')) {
                    ++m_pos;
                }
                if (m_pos + 1 >= m_source.size()) {
                    Fail("unterminated block comment at " + std::to_string(start));
                    m_pos = m_source.size();
                } else {
                    m_pos += 2;
                }
                continue;
            }
            break;
        }
    }

    Token Number() {
        Token        token;
        token.kind   = Kind::Number;
        size_t const begin = m_pos;
        while (std::isdigit(static_cast<unsigned char>(Peek(0))) != 0) ++m_pos;
        if (Peek(0) == '.' && std::isdigit(static_cast<unsigned char>(Peek(1))) != 0) {
            ++m_pos;
            while (std::isdigit(static_cast<unsigned char>(Peek(0))) != 0) ++m_pos;
        }
        token.text   = std::string(m_source.substr(begin, m_pos - begin));
        token.number = std::strtod(token.text.c_str(), nullptr);
        token.at     = begin;
        return token;
    }

    Token String(char quote) {
        Token        token;
        token.kind   = Kind::String;
        token.at     = m_pos;
        ++m_pos;
        std::string text;
        bool        closed = false;
        while (m_pos < m_source.size()) {
            char const c = m_source[m_pos];
            if (c == quote) {
                ++m_pos;
                closed = true;
                break;
            }
            if (c == '\\' && m_pos + 1 < m_source.size()) {
                char const escaped = m_source[m_pos + 1];
                switch (escaped) {
                case 'n': text += '\n'; break;
                case 't': text += '\t'; break;
                case 'r': text += '\r'; break;
                default: text += escaped; break;
                }
                m_pos += 2;
                continue;
            }
            text += c;
            ++m_pos;
        }
        if (!closed) Fail("unterminated string at " + std::to_string(token.at));
        token.text = std::move(text);
        return token;
    }

    Token Identifier() {
        Token        token;
        token.kind   = Kind::Identifier;
        size_t const begin = m_pos;
        if (m_source[m_pos] == '@') ++m_pos;
        while (m_pos < m_source.size()) {
            char const c = m_source[m_pos];
            if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.'
                || c == '$') {
                ++m_pos;
                continue;
            }
            break;
        }
        token.text = std::string(m_source.substr(begin, m_pos - begin));
        token.at   = begin;
        return token;
    }

    std::string_view m_source;
    std::string&     m_error;
    size_t           m_pos  = 0;
    bool             m_failed = false;
};

class Parser {
public:
    Parser(std::string_view source, std::string& error) : m_lexer(source, error), m_error(error) {
        Advance();
    }

    NodePtr ParseDocument(std::vector<std::string>& identifiers) {
        m_identifiers = &identifiers;
        NodePtr node = ParseTernary();
        if (node && Current().kind != Kind::End) Fail("unexpected trailing input");
        return node;
    }

    [[nodiscard]] bool failed() const { return m_failed || m_lexer.failed(); }

private:
    Token const& Current() const { return m_current; }

    void Advance() {
        m_current = m_lexer.Next();
        if (m_lexer.failed() && !m_failed) {
            m_failed = true;
            if (m_error.empty()) m_error = "invalid expression";
        }
    }

    bool Match(Kind kind) {
        if (m_current.kind != kind) return false;
        Advance();
        return true;
    }

    void Fail(std::string message) {
        if (m_failed) return;
        m_failed = true;
        m_error  = std::move(message);
    }

    std::shared_ptr<Expression::Node> Make(Expression::Node::Kind kind) {
        auto node  = std::make_shared<Expression::Node>();
        node->kind = kind;
        return node;
    }

    NodePtr ParseTernary() {
        NodePtr condition = ParseEquality();
        if (!condition || m_current.kind != Kind::Question) return condition;
        Advance();
        NodePtr whenTrue = ParseTernary();
        if (!whenTrue) return nullptr;
        if (!Match(Kind::Colon)) {
            Fail("expected ':' in a conditional expression");
            return nullptr;
        }
        NodePtr whenFalse = ParseTernary();
        if (!whenFalse) return nullptr;

        auto node  = Make(Expression::Node::Kind::Ternary);
        node->a    = condition;
        node->b    = whenTrue;
        node->c    = whenFalse;
        return node;
    }

    NodePtr ParseEquality() {
        NodePtr left = ParseComparison();
        while (left) {
            Expression::Node::Op op;
            if (m_current.kind == Kind::Equal) op = Expression::Node::Op::Equal;
            else if (m_current.kind == Kind::NotEqual) op = Expression::Node::Op::NotEqual;
            else break;
            Advance();
            NodePtr right = ParseComparison();
            if (!right) return nullptr;
            left = Binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    NodePtr ParseComparison() {
        NodePtr left = ParseAdditive();
        while (left) {
            Expression::Node::Op op;
            switch (m_current.kind) {
            case Kind::Less: op = Expression::Node::Op::Less; break;
            case Kind::Greater: op = Expression::Node::Op::Greater; break;
            case Kind::LessEqual: op = Expression::Node::Op::LessEqual; break;
            case Kind::GreaterEqual: op = Expression::Node::Op::GreaterEqual; break;
            default: return left;
            }
            Advance();
            NodePtr right = ParseAdditive();
            if (!right) return nullptr;
            left = Binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    NodePtr ParseAdditive() {
        NodePtr left = ParseMultiplicative();
        while (left) {
            Expression::Node::Op op;
            if (m_current.kind == Kind::Plus) op = Expression::Node::Op::Add;
            else if (m_current.kind == Kind::Minus) op = Expression::Node::Op::Sub;
            else break;
            Advance();
            NodePtr right = ParseMultiplicative();
            if (!right) return nullptr;
            left = Binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    NodePtr ParseMultiplicative() {
        NodePtr left = ParseUnary();
        while (left) {
            Expression::Node::Op op;
            if (m_current.kind == Kind::Star) op = Expression::Node::Op::Mul;
            else if (m_current.kind == Kind::Slash) op = Expression::Node::Op::Div;
            else if (m_current.kind == Kind::Percent) op = Expression::Node::Op::Modulo;
            else break;
            Advance();
            NodePtr right = ParseUnary();
            if (!right) return nullptr;
            left = Binary(op, std::move(left), std::move(right));
        }
        return left;
    }

    NodePtr ParseUnary() {
        Expression::Node::Op op;
        if (m_current.kind == Kind::Minus) {
            op = Expression::Node::Op::Negate;
        } else if (m_current.kind == Kind::Bang) {
            op = Expression::Node::Op::Not;
        } else {
            return ParsePrimary();
        }
        Advance();
        NodePtr operand = ParseUnary();
        if (!operand) return nullptr;

        auto node = Make(Expression::Node::Kind::Unary);
        node->op  = op;
        node->a   = std::move(operand);
        return node;
    }

    NodePtr ParsePrimary() {
        Token const token = m_current;
        switch (token.kind) {
        case Kind::Number: {
            auto node     = Make(Expression::Node::Kind::Literal);
            node->literal = Variant{token.number};
            Advance();
            return node;
        }
        case Kind::String: {
            auto node     = Make(Expression::Node::Kind::Literal);
            node->literal = Variant{token.text};
            Advance();
            return node;
        }
        case Kind::Identifier: {
            Advance();
            std::string name = token.text;
            if (!name.empty() && name.front() == '@') name.erase(name.begin());
            if (name == "true" || name == "false") {
                auto node     = Make(Expression::Node::Kind::Literal);
                node->literal = Variant{name == "true"};
                return node;
            }
            if (name == "null") {
                auto node = Make(Expression::Node::Kind::Literal);
                return node;
            }
            // Script locals ('$x') and event payload fields are not data keys,
            // so they neither subscribe nor trigger polling. Dotted names
            // subscribe to their root key (`@item.name` -> "item").
            std::string const root = name.substr(0, name.find('.'));
            bool const isDynamicScope =
                root.empty() || root.front() == '$' || root == "event";
            if (m_identifiers && !isDynamicScope
                && std::find(m_identifiers->begin(), m_identifiers->end(), root)
                    == m_identifiers->end()) {
                m_identifiers->push_back(root);
            }
            auto node = Make(Expression::Node::Kind::Identifier);
            node->name = std::move(name);
            return node;
        }
        case Kind::LParen: {
            Advance();
            NodePtr inner = ParseTernary();
            if (!inner) return nullptr;
            if (!Match(Kind::RParen)) {
                Fail("expected ')'");
                return nullptr;
            }
            return inner;
        }
        case Kind::End:
            Fail("unexpected end of expression");
            return nullptr;
        default:
            Fail("expected a value");
            return nullptr;
        }
    }

    static NodePtr Binary(Expression::Node::Op op, NodePtr left, NodePtr right) {
        auto node = std::make_shared<Expression::Node>();
        node->kind = Expression::Node::Kind::Binary;
        node->op   = op;
        node->a    = std::move(left);
        node->b    = std::move(right);
        return node;
    }

    Lexer                      m_lexer;
    std::string&               m_error;
    Token                      m_current;
    std::vector<std::string>*  m_identifiers = nullptr;
    bool                       m_failed      = false;
};

Variant EvaluateNode(Expression::Node const& node, ValueResolver const& resolve) {
    using Kind = Expression::Node::Kind;
    using Op   = Expression::Node::Op;

    // A NaN/infinity result would leak into layout or drawing as a poisoned
    // coordinate, so arithmetic saturates such results back to zero.
    auto finite = [](double value) { return std::isfinite(value) ? value : 0.0; };

    switch (node.kind) {
    case Kind::Literal:
        return node.literal;
    case Kind::Identifier:
        return resolve ? resolve(node.name) : Variant{};
    case Kind::Unary: {
        Variant const value = EvaluateNode(*node.a, resolve);
        if (node.op == Op::Negate) return Variant{-finite(value.ToNumber())};
        return Variant{!value.Truthy()};
    }
    case Kind::Ternary:
        return EvaluateNode(*node.a, resolve).Truthy() ? EvaluateNode(*node.b, resolve)
                                                       : EvaluateNode(*node.c, resolve);
    case Kind::Binary:
        break;
    }

    Variant const left  = EvaluateNode(*node.a, resolve);
    Variant const right = EvaluateNode(*node.b, resolve);
    switch (node.op) {
    case Op::Add:
        if (left.IsString() || right.IsString()) return Variant{left.ToString() + right.ToString()};
        return Variant{finite(left.ToNumber() + right.ToNumber())};
    case Op::Sub:
        return Variant{finite(left.ToNumber() - right.ToNumber())};
    case Op::Mul:
        return Variant{finite(left.ToNumber() * right.ToNumber())};
    case Op::Div: {
        double const divisor = right.ToNumber();
        return Variant{divisor == 0.0 ? 0.0 : finite(left.ToNumber() / divisor)};
    }
    case Op::Modulo: {
        double const divisor = right.ToNumber();
        return Variant{divisor == 0.0 ? 0.0 : finite(std::fmod(left.ToNumber(), divisor))};
    }
    case Op::Equal:
        return Variant{left == right};
    case Op::NotEqual:
        return Variant{left != right};
    case Op::Less:
    case Op::Greater:
    case Op::LessEqual:
    case Op::GreaterEqual: {
        bool less = false;
        bool equal = false;
        if (left.IsString() && right.IsString()) {
            less  = left.ToString() < right.ToString();
            equal = left.ToString() == right.ToString();
        } else {
            double const a = left.ToNumber();
            double const b = right.ToNumber();
            less  = a < b;
            equal = a == b;
        }
        switch (node.op) {
        case Op::Less: return Variant{less};
        case Op::Greater: return Variant{!less && !equal};
        case Op::LessEqual: return Variant{less || equal};
        default: return Variant{!less};
        }
    }
    case Op::Negate:
    case Op::Not:
        break;
    }
    return Variant{};
}
} // namespace

std::optional<Expression> Expression::Parse(std::string_view source, std::string& error) {
    error.clear();
    Expression expression;
    expression.m_source = std::string(source);

    Parser parser(source, error);
    NodePtr root = parser.ParseDocument(expression.m_identifiers);
    if (!root || parser.failed() || !error.empty()) {
        if (error.empty()) error = "invalid expression";
        return std::nullopt;
    }
    expression.m_root = std::move(root);
    return expression;
}

Variant Expression::Evaluate(ValueResolver const& resolve) const {
    if (!m_root) return Variant{};
    return EvaluateNode(*m_root, resolve);
}

std::optional<std::string> Expression::PlainIdentifier() const {
    if (!m_root || m_root->kind != Node::Kind::Identifier) return std::nullopt;
    std::string const& name = m_root->name;
    if (name.empty() || name.front() == '$' || name.rfind("event.", 0) == 0
        || name.find('.') != std::string::npos) {
        return std::nullopt;
    }
    return name;
}

} // namespace silica
