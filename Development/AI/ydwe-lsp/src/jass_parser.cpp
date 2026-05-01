// YDWE Jass Parser Implementation
// Parses Jass source into JassDocument with types, globals, functions, locals

#include <jass/parser.h>
#include <jass/lexer.h>
#include <algorithm>
#include <sstream>

namespace ydwe {
namespace lsp {
namespace jass {

// ========== Helpers ==========

static JassType nameToJassType(const std::string& name) {
    static const std::unordered_map<std::string, JassType> map = {
        {"nothing", JassType::Nothing}, {"null", JassType::Null},
        {"boolean", JassType::Boolean}, {"integer", JassType::Integer},
        {"real", JassType::Real}, {"string", JassType::String},
        {"handle", JassType::Handle}, {"code", JassType::Code},
        {"unit", JassType::Unit}, {"item", JassType::Item},
        {"destructable", JassType::Destructable}, {"trigger", JassType::Trigger},
        {"timer", JassType::Timer}, {"group", JassType::Group},
        {"location", JassType::Location}, {"rect", JassType::Rect},
        {"boolexpr", JassType::Boolexpr}, {"sound", JassType::Sound},
        {"effect", JassType::Effect},
    };
    auto it = map.find(name);
    if (it != map.end()) return it->second;
    return JassType::UserDefined;
}

static TypeInfo makeTypeInfo(const std::string& name) {
    TypeInfo info;
    info.name = name;
    info.kind = nameToJassType(name);
    return info;
}

// ========== Parser::Impl ==========

class Parser::Impl {
public:
    std::vector<Token> tokens;
    size_t pos = 0;
    std::vector<Diagnostic> diagnostics;

    void reset() {
        tokens.clear();
        pos = 0;
        diagnostics.clear();
    }

    const Token& current() const {
        static const Token eof{Token::END_OF_FILE, "", {0, 0}, {0, 0}};
        if (pos >= tokens.size()) return eof;
        return tokens[pos];
    }

    const Token& peek(size_t offset = 0) const {
        static const Token eof{Token::END_OF_FILE, "", {0, 0}, {0, 0}};
        size_t idx = pos + offset;
        if (idx >= tokens.size()) return eof;
        return tokens[idx];
    }

    Token advance() {
        Token t = current();
        if (pos < tokens.size()) pos++;
        return t;
    }

    bool match(Token::Type type) {
        if (current().type == type) {
            advance();
            return true;
        }
        return false;
    }

    bool matchValue(const std::string& value) {
        if (current().value == value) {
            advance();
            return true;
        }
        return false;
    }

    bool checkValue(const std::string& value) const {
        return current().value == value;
    }

    void addError(const std::string& msg, const Position& pos) {
        Diagnostic d;
        d.range.start = pos;
        d.range.end = pos;
        d.severity = DiagnosticSeverity::Error;
        d.message = msg;
        diagnostics.push_back(d);
    }

    // ---- Parse entry point ----

    JassDocument parseDocument(const std::string& source) {
        reset();

        Lexer lexer(source);
        tokens = lexer.tokenize();

        JassDocument doc;

        while (current().type != Token::END_OF_FILE) {
            if (checkValue("type")) {
                parseTypeDecl(doc);
            } else if (checkValue("globals")) {
                parseGlobalsBlock(doc);
            } else if (checkValue("native")) {
                parseNativeDecl(doc);
            } else if (checkValue("function")) {
                parseFunctionDecl(doc);
            } else if (checkValue("constant")) {
                parseConstantDecl(doc);
            } else {
                advance(); // skip unknown top-level token
            }
        }

        return doc;
    }

    // ---- type X extends Y ----

    void parseTypeDecl(JassDocument& doc) {
        Position startPos = current().start;
        advance(); // consume "type"

        if (current().type != Token::IDENTIFIER && current().type != Token::TYPE) {
            addError("expected type name after 'type'", current().start);
            return;
        }
        std::string typeName = current().value;
        advance();

        TypeInfo info = makeTypeInfo(typeName);

        if (checkValue("extends")) {
            advance();
            if (current().type != Token::IDENTIFIER && current().type != Token::TYPE) {
                addError("expected parent type after 'extends'", current().start);
                return;
            }
            info.extends = current().value;
            advance();
        }

        doc.types.push_back(info);
    }

    // ---- globals ... endglobals ----

    void parseGlobalsBlock(JassDocument& doc) {
        advance(); // consume "globals"

        while (current().type != Token::END_OF_FILE && !checkValue("endglobals")) {
            parseGlobalVariable(doc);
        }

        if (checkValue("endglobals")) {
            advance();
        }
    }

    void parseGlobalVariable(JassDocument& doc) {
        bool isConst = false;
        if (checkValue("constant")) {
            isConst = true;
            advance();
        }

        // Parse type
        if (current().type != Token::IDENTIFIER && current().type != Token::TYPE && current().type != Token::KEYWORD) {
            advance();
            return;
        }
        std::string typeName = current().value;
        advance();

        bool isArray = false;
        if (checkValue("array")) {
            isArray = true;
            advance();
        }

        // Parse name
        if (current().type != Token::IDENTIFIER) {
            addError("expected variable name", current().start);
            return;
        }
        std::string varName = current().value;
        Position varPos = current().start;
        advance();

        GlobalVariable gv;
        gv.name = varName;
        gv.type = makeTypeInfo(typeName);
        gv.isConstant = isConst;
        gv.isArray = isArray;
        gv.range.start = varPos;
        gv.range.end = varPos;

        // Optional initialization
        if (current().value == "=") {
            advance();
            // Skip the initializer expression
            skipExpression();
        }

        doc.globals.push_back(gv);
    }

    // ---- native Name takes ... returns ... ----

    void parseNativeDecl(JassDocument& doc) {
        advance(); // consume "native"
        parseFunctionSignature(doc, true);
    }

    // ---- constant native/function ... ----

    void parseConstantDecl(JassDocument& doc) {
        advance(); // consume "constant"

        if (checkValue("native")) {
            advance();
            parseFunctionSignature(doc, true, true);
        } else if (checkValue("function")) {
            advance();
            parseFunctionSignature(doc, false, true);
        } else {
            // constant global variable - handled in globals block
            addError("unexpected token after 'constant'", current().start);
        }
    }

    // ---- function Name takes ... returns ... ----

    void parseFunctionDecl(JassDocument& doc) {
        advance(); // consume "function"
        parseFunctionSignature(doc, false);
    }

    void parseFunctionSignature(JassDocument& doc, bool isNative, bool isConstant = false) {
        if (current().type != Token::IDENTIFIER) {
            addError("expected function name", current().start);
            return;
        }

        FunctionInfo func;
        func.name = current().value;
        func.range.start = current().start;
        func.isNative = isNative;
        func.isConstant = isConstant;
        advance();

        // Parse parameters: takes [Type name [, Type name]*] returns Type
        if (!checkValue("takes")) {
            addError("expected 'takes' after function name", current().start);
            return;
        }
        advance(); // consume "takes"

        if (checkValue("nothing")) {
            advance(); // no parameters
        } else {
            // Parse parameter list
            while (true) {
                if (current().type != Token::IDENTIFIER && current().type != Token::TYPE) {
                    addError("expected parameter type", current().start);
                    break;
                }
                std::string paramType = current().value;
                advance();

                if (current().type != Token::IDENTIFIER) {
                    addError("expected parameter name", current().start);
                    break;
                }
                std::string paramName = current().value;
                Position paramPos = current().start;
                advance();

                Parameter p;
                p.name = paramName;
                p.type = makeTypeInfo(paramType);
                p.position = paramPos;
                func.parameters.push_back(p);

                if (current().value == ",") {
                    advance();
                } else {
                    break;
                }
            }
        }

        // returns
        if (!checkValue("returns")) {
            addError("expected 'returns'", current().start);
            return;
        }
        advance();

        if (checkValue("nothing")) {
            func.returnType = makeTypeInfo("nothing");
            advance();
        } else if (current().type == Token::IDENTIFIER || current().type == Token::TYPE) {
            func.returnType = makeTypeInfo(current().value);
            advance();
        }

        func.range.end = current().start;

        if (isNative) {
            doc.functions.push_back(func);
            return;
        }

        // Parse function body for local variables
        func.bodyRange = Range{current().start, current().start};
        parseFunctionBody(func, doc);

        doc.functions.push_back(func);
    }

    void parseFunctionBody(FunctionInfo& func, JassDocument& doc) {
        // Parse until "endfunction", collecting local declarations
        int depth = 1; // nesting level for if/loop

        while (current().type != Token::END_OF_FILE) {
            if (checkValue("endfunction")) {
                if (func.bodyRange.has_value()) {
                    func.bodyRange->end = current().end;
                }
                func.range.end = current().end;
                advance();
                return;
            }

            if (checkValue("local")) {
                advance();
                parseLocalVariable(func, doc);
            } else {
                // Skip other statements
                if (checkValue("if") || checkValue("loop")) {
                    depth++;
                } else if (checkValue("endif") || checkValue("endloop")) {
                    depth--;
                }
                advance();
            }
        }
    }

    void parseLocalVariable(FunctionInfo& func, JassDocument& doc) {
        // local Type name [= expr]
        if (current().type != Token::IDENTIFIER && current().type != Token::TYPE) {
            advance();
            return;
        }
        std::string typeName = current().value;
        advance();

        bool isArray = false;
        if (checkValue("array")) {
            isArray = true;
            advance();
        }

        if (current().type != Token::IDENTIFIER) {
            return;
        }
        std::string varName = current().value;
        Position varPos = current().start;
        advance();

        LocalVariable lv;
        lv.name = varName;
        lv.type = makeTypeInfo(typeName);
        lv.position = varPos;

        // Optional initialization
        if (current().value == "=") {
            advance();
            // Skip the initializer
            skipExpression();
        }

        doc.locals[func.name].push_back(lv);
    }

    // Skip over an expression (simplified - handles basic cases)
    void skipExpression() {
        int parenDepth = 0;
        while (current().type != Token::END_OF_FILE) {
            const auto& t = current();
            if (t.value == "(") {
                parenDepth++;
                advance();
            } else if (t.value == ")") {
                if (parenDepth == 0) break;
                parenDepth--;
                advance();
            } else if (t.value == "," && parenDepth == 0) {
                break;
            } else if (t.value == "\n" || t.type == Token::PUNCTUATION) {
                // Check for statement terminators
                if (t.value == ";") {
                    advance();
                    break;
                }
                advance();
            } else if (t.type == Token::KEYWORD &&
                       (t.value == "call" || t.value == "set" || t.value == "local" ||
                        t.value == "if" || t.value == "elseif" || t.value == "else" ||
                        t.value == "endif" || t.value == "loop" || t.value == "endloop" ||
                        t.value == "exitwhen" || t.value == "return" || t.value == "endfunction")) {
                break;
            } else {
                advance();
            }
        }
    }

    // ---- Builtin data ----

    static std::vector<FunctionInfo> getBuiltinFunctions() {
        std::vector<FunctionInfo> funcs;

        // Common BJ functions
        auto addFunc = [&](const char* name, const char* retType,
                          std::vector<std::pair<std::string, std::string>> params) {
            FunctionInfo f;
            f.name = name;
            f.returnType = makeTypeInfo(retType);
            f.isNative = true;
            for (auto& [t, n] : params) {
                Parameter p;
                p.name = n;
                p.type = makeTypeInfo(t);
                f.parameters.push_back(p);
            }
            funcs.push_back(f);
        };

        addFunc("GetPlayerId", "integer", {{"player", "whichPlayer"}});
        addFunc("GetPlayerName", "string", {{"player", "whichPlayer"}});
        addFunc("CreateUnit", "unit", {{"player", "id"}, {"integer", "unitid"}, {"real", "x"}, {"real", "y"}, {"real", "face"}});
        addFunc("KillUnit", "nothing", {{"unit", "whichUnit"}});
        addFunc("RemoveUnit", "nothing", {{"unit", "whichUnit"}});
        addFunc("GetUnitX", "real", {{"unit", "whichUnit"}});
        addFunc("GetUnitY", "real", {{"unit", "whichUnit"}});
        addFunc("SetUnitPosition", "nothing", {{"unit", "whichUnit"}, {"real", "newX"}, {"real", "newY"}});
        addFunc("IssueImmediateOrder", "boolean", {{"unit", "whichUnit"}, {"string", "order"}});
        addFunc("CreateTrigger", "trigger", {});
        addFunc("TriggerRegisterTimerEvent", "event", {{"trigger", "whichTrigger"}, {"real", "timeout"}, {"boolean", "periodic"}});
        addFunc("TriggerRegisterPlayerEvent", "event", {{"trigger", "whichTrigger"}, {"player", "whichPlayer"}, {"playerevent", "whichPlayerEvent"}});
        addFunc("TriggerAddAction", "nothing", {{"trigger", "whichTrigger"}, {"code", "actionFunc"}});
        addFunc("EnableTrigger", "nothing", {{"trigger", "whichTrigger"}});
        addFunc("DisableTrigger", "nothing", {{"trigger", "whichTrigger"}});
        addFunc("DestroyTrigger", "nothing", {{"trigger", "whichTrigger"}});
        addFunc("CreateTimer", "timer", {});
        addFunc("TimerStart", "nothing", {{"timer", "whichTimer"}, {"real", "timeout"}, {"boolean", "periodic"}, {"code", "handlerFunc"}});
        addFunc("DestroyTimer", "nothing", {{"timer", "whichTimer"}});
        addFunc("GetExpiredTimer", "timer", {});
        addFunc("GetTriggerUnit", "unit", {});
        addFunc("GetTriggerPlayer", "player", {});
        addFunc("GetLocalPlayer", "player", {});
        addFunc("DisplayTextToPlayer", "nothing", {{"player", "toPlayer"}, {"real", "x"}, {"real", "y"}, {"string", "message"}});
        addFunc("DisplayTimedTextToPlayer", "nothing", {{"player", "toPlayer"}, {"real", "x"}, {"real", "y"}, {"real", "duration"}, {"string", "message"}});
        addFunc("CreateGroup", "group", {});
        addFunc("DestroyGroup", "nothing", {{"group", "whichGroup"}});
        addFunc("GroupAddUnit", "boolean", {{"group", "whichGroup"}, {"unit", "whichUnit"}});
        addFunc("GroupRemoveUnit", "boolean", {{"group", "whichGroup"}, {"unit", "whichUnit"}});
        addFunc("FirstOfGroup", "unit", {{"group", "whichGroup"}});
        addFunc("ForGroup", "nothing", {{"group", "whichGroup"}, {"code", "callback"}});
        addFunc("GetEnumUnit", "unit", {});
        addFunc("CreateLocation", "location", {});
        addFunc("MoveLocation", "nothing", {{"location", "whichLocation"}, {"real", "x"}, {"real", "y"}});
        addFunc("GetLocationX", "real", {{"location", "whichLocation"}});
        addFunc("GetLocationY", "real", {{"location", "whichLocation"}});
        addFunc("RemoveLocation", "nothing", {{"location", "whichLocation"}});
        addFunc("I2S", "string", {{"integer", "i"}});
        addFunc("R2S", "string", {{"real", "r"}});
        addFunc("S2I", "integer", {{"string", "s"}});
        addFunc("S2R", "real", {{"string", "s"}});
        addFunc("I2R", "real", {{"integer", "i"}});
        addFunc("R2I", "integer", {{"real", "r"}});
        addFunc("StringConcat", "string", {{"string", "s1"}, {"string", "s2"}});
        addFunc("StringLength", "integer", {{"string", "s"}});
        addFunc("SubString", "string", {{"string", "s"}, {"integer", "start"}, {"integer", "end"}});
        addFunc("Condition", "boolexpr", {{"code", "func"}});
        addFunc("GetBooleanAnd", "boolean", {{"boolean", "a"}, {"boolean", "b"}});
        addFunc("GetBooleanOr", "boolean", {{"boolean", "a"}, {"boolean", "b"}});
        addFunc("Not", "boolean", {{"boolean", "b"}});
        addFunc("ModuloInteger", "integer", {{"integer", "a"}, {"integer", "b"}});
        addFunc("ModuloReal", "real", {{"real", "a"}, {"real", "b"}});
        addFunc("SquareRoot", "real", {{"real", "x"}});
        addFunc("Pow", "real", {{"real", "x"}, {"real", "y"}});
        addFunc("Sin", "real", {{"real", "x"}});
        addFunc("Cos", "real", {{"real", "x"}});
        addFunc("Atan2", "real", {{"real", "y"}, {"real", "x"}});

        return funcs;
    }

    static std::vector<TypeInfo> getBuiltinTypes() {
        return {
            makeTypeInfo("nothing"), makeTypeInfo("null"),
            makeTypeInfo("boolean"), makeTypeInfo("integer"),
            makeTypeInfo("real"), makeTypeInfo("string"),
            makeTypeInfo("handle"), makeTypeInfo("code"),
            makeTypeInfo("unit"), makeTypeInfo("item"),
            makeTypeInfo("destructable"), makeTypeInfo("trigger"),
            makeTypeInfo("timer"), makeTypeInfo("group"),
            makeTypeInfo("location"), makeTypeInfo("rect"),
            makeTypeInfo("boolexpr"), makeTypeInfo("sound"),
            makeTypeInfo("effect"), makeTypeInfo("quest"),
            makeTypeInfo("questitem"), makeTypeInfo("timerdialog"),
            makeTypeInfo("leaderboard"), makeTypeInfo("multiboard"),
            makeTypeInfo("force"), makeTypeInfo("region"),
            makeTypeInfo("fogstate"), makeTypeInfo("dialog"),
            makeTypeInfo("button"), makeTypeInfo("texttag"),
            makeTypeInfo("lightning"), makeTypeInfo("image"),
            makeTypeInfo("ubersplat"), makeTypeInfo("hashtable"),
            makeTypeInfo("framehandle"),
        };
    }
};

// ========== JassDocument lookups ==========

std::optional<FunctionInfo> JassDocument::findFunction(const std::string& name) const {
    for (const auto& f : functions) {
        if (f.name == name) return f;
    }
    return std::nullopt;
}

std::optional<GlobalVariable> JassDocument::findGlobal(const std::string& name) const {
    for (const auto& g : globals) {
        if (g.name == name) return g;
    }
    return std::nullopt;
}

std::vector<LocalVariable> JassDocument::findLocals(const std::string& functionName) const {
    auto it = locals.find(functionName);
    if (it != locals.end()) return it->second;
    return {};
}

// ========== Parser public interface ==========

Parser::Parser() : pImpl(new Impl()) {}

Parser::~Parser() = default;

JassDocument Parser::parse(const std::string& source) {
    return pImpl->parseDocument(source);
}

std::vector<Diagnostic> Parser::getDiagnostics() const {
    return pImpl->diagnostics;
}

void Parser::update(const std::string& uri, const std::string& newContent, const std::vector<Range>& changes) {
    // Full reparse for now
    pImpl->parseDocument(newContent);
}

std::optional<FunctionInfo> Parser::getFunctionAtPosition(const Position& pos) const {
    return std::nullopt;
}

std::vector<FunctionInfo> Parser::getBuiltinFunctions() {
    return Impl::getBuiltinFunctions();
}

std::vector<TypeInfo> Parser::getBuiltinTypes() {
    return Impl::getBuiltinTypes();
}

// ========== Analyzer ==========

std::vector<Diagnostic> Analyzer::analyze(const JassDocument& doc, const std::string& source) {
    std::vector<Diagnostic> diags;

    // Check for duplicate function names
    std::unordered_map<std::string, int> funcCount;
    for (const auto& f : doc.functions) {
        funcCount[f.name]++;
        if (funcCount[f.name] > 1) {
            Diagnostic d;
            d.range = f.range;
            d.severity = DiagnosticSeverity::Warning;
            d.message = "duplicate function definition: " + f.name;
            diags.push_back(d);
        }
    }

    // Check for duplicate global names
    std::unordered_map<std::string, int> globalCount;
    for (const auto& g : doc.globals) {
        globalCount[g.name]++;
        if (globalCount[g.name] > 1) {
            Diagnostic d;
            d.range = g.range;
            d.severity = DiagnosticSeverity::Warning;
            d.message = "duplicate global variable: " + g.name;
            diags.push_back(d);
        }
    }

    return diags;
}

std::optional<std::string> Analyzer::checkTypes(const JassDocument& doc) {
    // Placeholder for type checking
    return std::nullopt;
}

std::vector<Diagnostic> Analyzer::findUnusedVariables(const JassDocument& doc) {
    // Placeholder for unused variable detection
    return {};
}

} // namespace jass
} // namespace lsp
} // namespace ydwe
