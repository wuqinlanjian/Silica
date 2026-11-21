#pragma once

#include "Animation.h"
#include "SilNode.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace silica {

struct SilDocument {
    std::vector<SilNode>                 roots;
    std::vector<SilDiagnostic>           diagnostics;
    std::map<std::string, KeyframeSet>   keyframes;
    std::map<std::string, std::string>   vars;       // `Var name: value;`
    std::map<std::string, SilStyle>      styles;     // `Style Name [extends Base] { ... }`
    std::map<std::string, SilComponentDef> components;

    bool ok() const { return !roots.empty() && !HasErrors(diagnostics); }
};

// Recursive-descent parser for the .sil UI language:
//
//   Container {
//       layout: vertical;
//       padding: 10;
//       Label { text: "Silica"; color: #FFFFFF; font-size: 1.5; margin-bottom: 10; }
//       Button { text: "OK"; onClick: "handle_button_click"; }
//   }
//
// A component body holds properties (`key: value;`) and child components. Both
// `//` and `/* */` comments are skipped. Property values may be several tokens
// ("border: 1 solid #FFF"), and a property without a trailing ';' is accepted
// when the block ends or the next key starts (the latter is reported).
//
// Reactive values keep their source text untouched:
//
//   text: @user_name;
//   visible: @health > 0;
//   text: @health > 0 ? "存活" : "死亡";
//   onClick: { @health = @health + 10; }
//
// A value that starts with '@' becomes a binding; a `{` in value position is a
// script block. Both are stored on SilNode and compiled by UICompiler.
//
// Three top-level definitions are hoisted out of the widget tree:
//
//   Var gutter: 6;                       // compile-time text constant, used as $gutter
//   Style Card [extends Base] { ... }    // reusable property set, used as class: "card"
//   Component Stat(label, value: 0) {   // reusable subtree with arguments and Slot
//       Label { text: @label; }
//   }
//
// Definitions may appear before or after their use; UICompiler resolves them.
class SilParser {
public:
    explicit SilParser(std::string_view source);

    // Always returns a document. Errors are reported through diagnostics and
    // recovery keeps the parse going, so a broken file never throws.
    SilDocument Parse();

private:
    enum class TokenKind {
        Identifier,
        String,
        Number,
        Color,
        LBrace,
        RBrace,
        LParen,
        RParen,
        Comma,
        Question,
        Colon,
        Semicolon,
        // Operators exist so expressions can be tokenized; bindings keep the
        // original source slice instead of the joined token text.
        Assign,
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        Bang,
        Less,
        Greater,
        End,
    };

    struct Token {
        TokenKind   kind = TokenKind::End;
        std::string text;
        int         line   = 1;
        int         column = 1;
        size_t      begin  = 0; // byte offset of the first character
        size_t      end    = 0; // one past the last character
    };

    void Tokenize();
    void ParseRoots();
    void ParseKeyframes();
    void ParseVar();
    void ParseStyle();
    void ParseComponent();
    std::optional<SilNode> ParseNode(int depth);
    void                   ParseParams(SilNode& node);
    void                   ParseBody(SilNode& node, int depth);
    void                   ParseProperty(SilNode& node, bool allowScripts = true);
    void                   ParseScriptProperty(SilNode& node, Token const& key);
    void                   SkipStatement();
    void                   SkipBlock();
    std::string            JoinTokens(std::vector<Token> const& tokens) const;

    Token const& Peek(size_t ahead = 0) const;
    Token        Advance();
    bool         Check(TokenKind kind, size_t ahead = 0) const;

    void Error(int line, int column, std::string message);
    void Warning(int line, int column, std::string message);
    void ErrorHere(std::string message);

    std::string              source_;
    std::vector<Token>       tokens_;
    size_t                   index_ = 0;
    SilDocument              document_;
};

} // namespace silica
