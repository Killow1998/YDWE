#pragma once

#include <lsp/protocol.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace ydwe {
namespace lsp {
namespace jass {

// ========== Jass type system ==========

enum class JassType {
    Nothing,
    Null,
    Boolean,
    Integer,
    Real,
    String,
    Handle,
    Code,
    Unit,
    Item,
    Destructable,
    Trigger,
    Timer,
    Group,
    Location,
    Rect,
    Boolexpr,
    Sound,
    Effect,
    UserDefined
};

struct TypeInfo {
    std::string name;
    JassType kind;
    std::optional<std::string> extends;
};

// ========== Jass syntax elements ==========

struct Parameter {
    std::string name;
    TypeInfo type;
    Position position;
};

struct FunctionInfo {
    std::string name;
    std::vector<Parameter> parameters;
    TypeInfo returnType;
    Range range;
    std::optional<Range> bodyRange;
    bool isNative = false;
    bool isConstant = false;
};

struct GlobalVariable {
    std::string name;
    TypeInfo type;
    bool isConstant = false;
    bool isArray = false;
    std::optional<std::string> initialValue;
    Range range;
};

struct LocalVariable {
    std::string name;
    TypeInfo type;
    std::optional<std::string> initialValue;
    Position position;
    std::shared_ptr<FunctionInfo> parentFunction;
};

// ========== Syntax tree ==========

struct JassDocument {
    std::vector<TypeInfo> types;
    std::vector<GlobalVariable> globals;
    std::vector<FunctionInfo> functions;
    std::unordered_map<std::string, std::vector<LocalVariable>> locals;

    std::optional<FunctionInfo> findFunction(const std::string& name) const;
    std::optional<GlobalVariable> findGlobal(const std::string& name) const;
    std::vector<LocalVariable> findLocals(const std::string& functionName) const;
};

// ========== Parser ==========

class Parser {
public:
    Parser();
    ~Parser();

    JassDocument parse(const std::string& source);
    std::vector<Diagnostic> getDiagnostics() const;
    void update(const std::string& uri, const std::string& newContent,
                const std::vector<Range>& changes);
    std::optional<FunctionInfo> getFunctionAtPosition(const Position& pos) const;

    static std::vector<FunctionInfo> getBuiltinFunctions();
    static std::vector<TypeInfo> getBuiltinTypes();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// ========== Code analysis ==========

class Analyzer {
public:
    static std::vector<Diagnostic> analyze(const JassDocument& doc, const std::string& source);
    static std::optional<std::string> checkTypes(const JassDocument& doc);
    static std::vector<Diagnostic> findUnusedVariables(const JassDocument& doc);
};

} // namespace jass
} // namespace lsp
} // namespace ydwe
