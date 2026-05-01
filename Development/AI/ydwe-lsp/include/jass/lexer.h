#pragma once

#include <lsp/protocol.h>
#include <string>
#include <vector>
#include <memory>

namespace ydwe {
namespace lsp {
namespace jass {

// ========== Token types ==========

struct Token {
    enum Type {
        END_OF_FILE,
        IDENTIFIER,
        KEYWORD,
        TYPE,
        INTEGER_LITERAL,
        REAL_LITERAL,
        STRING_LITERAL,
        BOOLEAN_LITERAL,
        OPERATOR,
        PUNCTUATION,
        UNKNOWN
    };

    Type type;
    std::string value;
    ::ydwe::lsp::Position start;
    ::ydwe::lsp::Position end;

    Token() = default;
    Token(Type t, std::string v, ::ydwe::lsp::Position s, ::ydwe::lsp::Position e);

    bool isKeyword() const;
    bool isType() const;
    bool isLiteral() const {
        return type == INTEGER_LITERAL || type == REAL_LITERAL ||
               type == STRING_LITERAL || type == BOOLEAN_LITERAL;
    }
};

// ========== Lexer ==========

class Lexer {
public:
    explicit Lexer(std::string source);
    ~Lexer();

    std::vector<Token> tokenize();
    ::ydwe::lsp::Position getPosition() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// ========== Helper functions ==========

bool isKeyword(const std::string& word);
bool isType(const std::string& word);

} // namespace jass
} // namespace lsp
} // namespace ydwe
