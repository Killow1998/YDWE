#include <jass/lexer.h>
#include <cctype>
#include <unordered_set>

namespace ydwe {
namespace lsp {
namespace jass {

// ========== Keywords ==========

static const std::unordered_set<std::string> keywords = {
    "function", "endfunction",
    "takes", "returns", "nothing",
    "globals", "endglobals",
    "local", "set", "call", "return",
    "if", "then", "else", "elseif", "endif",
    "loop", "exitwhen", "endloop",
    "true", "false", "null",
    "constant", "native", "type", "extends",
    "array"
};

// ========== Builtin types ==========

static const std::unordered_set<std::string> builtinTypes = {
    "nothing", "null", "boolean", "integer", "real", "string",
    "handle", "code",
    // Common handle types
    "agent", "event", "player", "widget", "unit", "destructable",
    "item", "ability", "buff", "force", "group", "trigger",
    "triggercondition", "triggeraction", "timer", "location", "region",
    "rect", "sound", "effect", "lightning", "image", "ubersplat",
    "quest", "questitem", "defeatcondition", "timerdialog",
    "leaderboard", "multiboard", "multiboarditem",
    "dialog", "button", "texttag", "fogmodifier", "fogstate",
    "hashtable", "gamecache", "file",
    "framehandle", "originframetype", "frametype",
    "attacktype", "damagetype", "weapontype", "soundtype",
    "pathingtype", "terraindeformation", "blendmode",
    "raritycontrol", "playercolor", "placement",
    "startlocprio", "mapdensity", "gametype", "mapflag",
    "mapcontrol", "playerslotstate", "volist",
    "camerafield", "camerasetup",
    "textaligntype", "frameeventtype", "oskeytype"
};

// ========== Token ==========

Token::Token(Type t, std::string v, Position s, Position e)
    : type(t), value(std::move(v)), start(s), end(e) {}

bool Token::isKeyword() const {
    return keywords.count(value) > 0;
}

bool Token::isType() const {
    return builtinTypes.count(value) > 0;
}

// ========== Lexer ==========

class Lexer::Impl {
public:
    std::string source;
    size_t pos = 0;
    int line = 0;
    int column = 0;

    explicit Impl(std::string src) : source(std::move(src)) {}

    char peek() const {
        if (pos >= source.length()) return '\0';
        return source[pos];
    }

    char advance() {
        if (pos >= source.length()) return '\0';
        char c = source[pos++];
        if (c == '\n') {
            line++;
            column = 0;
        } else {
            column++;
        }
        return c;
    }

    void skipWhitespace() {
        while (pos < source.length()) {
            char c = source[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                advance();
            } else if (c == '/') {
                // Check for comments
                if (pos + 1 < source.length() && source[pos + 1] == '/') {
                    // Single-line comment
                    while (peek() != '\n' && peek() != '\0') {
                        advance();
                    }
                } else if (pos + 1 < source.length() && source[pos + 1] == '*') {
                    // Multi-line comment
                    advance(); // /
                    advance(); // *
                    while (!(peek() == '*' && pos + 1 < source.length() && source[pos + 1] == '/')
                           && peek() != '\0') {
                        advance();
                    }
                    if (peek() == '*') {
                        advance(); // *
                        advance(); // /
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }
    }

    Token readIdentifier() {
        Position startPos{line, column};
        std::string value;

        while (std::isalnum(peek()) || peek() == '_') {
            value += advance();
        }

        Position endPos{line, column};

        // Check if keyword or type
        if (keywords.count(value)) {
            return Token(Token::KEYWORD, value, startPos, endPos);
        } else if (builtinTypes.count(value)) {
            return Token(Token::TYPE, value, startPos, endPos);
        }

        return Token(Token::IDENTIFIER, value, startPos, endPos);
    }

    Token readNumber() {
        Position startPos{line, column};
        std::string value;
        bool isReal = false;

        while (std::isdigit(peek()) || peek() == '.') {
            if (peek() == '.') {
                if (isReal) break; // Second dot
                isReal = true;
            }
            value += advance();
        }

        Position endPos{line, column};
        return Token(isReal ? Token::REAL_LITERAL : Token::INTEGER_LITERAL, value, startPos, endPos);
    }

    Token readString() {
        Position startPos{line, column};
        std::string value;

        char quote = advance(); // Opening quote

        while (peek() != quote && peek() != '\0') {
            if (peek() == '\\') {
                advance();
                char escaped = advance();
                switch (escaped) {
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    case '\\': value += '\\'; break;
                    case '\'': value += '\''; break;
                    case '"': value += '"'; break;
                    default: value += escaped; break;
                }
            } else {
                value += advance();
            }
        }

        if (peek() == quote) {
            advance(); // Closing quote
        }

        Position endPos{line, column};
        return Token(Token::STRING_LITERAL, value, startPos, endPos);
    }

    Token readOperator() {
        Position startPos{line, column};
        char c = advance();
        std::string value(1, c);

        // Check for two-char operators
        char next = peek();
        if ((c == '=' && next == '=') ||
            (c == '!' && next == '=') ||
            (c == '<' && next == '=') ||
            (c == '>' && next == '=') ||
            (c == '&' && next == '&') ||
            (c == '|' && next == '|')) {
            value += advance();
        }

        Position endPos{line, column};
        return Token(Token::OPERATOR, value, startPos, endPos);
    }
};

// ========== Public interface ==========

Lexer::Lexer(std::string source) : pImpl(std::make_unique<Impl>(std::move(source))) {}

Lexer::~Lexer() = default;

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        pImpl->skipWhitespace();

        Position startPos{pImpl->line, pImpl->column};
        char c = pImpl->peek();

        if (c == '\0') {
            break;
        }

        Token token;

        if (std::isalpha(c) || c == '_') {
            token = pImpl->readIdentifier();
        } else if (std::isdigit(c)) {
            token = pImpl->readNumber();
        } else if (c == '"' || c == '\'') {
            token = pImpl->readString();
        } else if (c == '+' || c == '-' || c == '*' || c == '/' ||
                   c == '=' || c == '<' || c == '>' || c == '!' ||
                   c == '&' || c == '|') {
            token = pImpl->readOperator();
        } else if (c == '(' || c == ')' || c == '[' || c == ']' ||
                   c == '{' || c == '}' || c == ',' || c == '.' ||
                   c == ':' || c == ';') {
            pImpl->advance();
            Position endPos{pImpl->line, pImpl->column};
            token = Token(Token::PUNCTUATION, std::string(1, c), startPos, endPos);
        } else {
            // Unknown character
            pImpl->advance();
            Position endPos{pImpl->line, pImpl->column};
            token = Token(Token::UNKNOWN, std::string(1, c), startPos, endPos);
        }

        tokens.push_back(std::move(token));
    }

    // Add EOF marker
    Position eofPos{pImpl->line, pImpl->column};
    tokens.emplace_back(Token::END_OF_FILE, "", eofPos, eofPos);

    return tokens;
}

Position Lexer::getPosition() const {
    return {pImpl->line, pImpl->column};
}

// ========== Helper functions ==========

bool isKeyword(const std::string& word) {
    return keywords.count(word) > 0;
}

bool isType(const std::string& word) {
    return builtinTypes.count(word) > 0;
}

} // namespace jass
} // namespace lsp
} // namespace ydwe
