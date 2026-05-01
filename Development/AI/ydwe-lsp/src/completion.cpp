#include <lsp/completion.h>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace ydwe {
namespace lsp {

// ========== Forward declarations for utils ==========

namespace completion_utils {
    std::string getWordPrefix(const std::string& line, int character);
    std::vector<CompletionItem> filterCompletions(
        const std::vector<CompletionItem>& candidates,
        const std::string& prefix);
    CompletionItem createKeywordItem(const std::string& keyword);
    CompletionItem createFunctionItem(
        const std::string& name,
        const std::string& detail,
        const std::string& documentation);
    CompletionItem createTypeItem(const std::string& typeName);
}

// ========== Jass keywords and types ==========

static const std::vector<std::string> jassKeywords = {
    "function", "endfunction", "takes", "returns", "nothing",
    "globals", "endglobals", "local", "set", "call", "return",
    "if", "then", "else", "elseif", "endif",
    "loop", "exitwhen", "endloop",
    "true", "false", "null",
    "constant", "native", "type", "extends", "array"
};

static const std::vector<std::string> jassTypes = {
    "nothing", "null", "boolean", "integer", "real", "string",
    "handle", "code", "unit", "item", "destructable", "trigger",
    "timer", "group", "location", "rect", "boolexpr", "sound",
    "effect", "quest", "questitem", "timerdialog", "leaderboard",
    "multiboard", "force", "region", "fogstate", "dialog",
    "button", "texttag", "lightning", "image", "ubersplat",
    "hashtable", "framehandle"
};

static const std::vector<std::pair<std::string, std::string>> jassCommonFunctions = {
    {"BJDebugMsg", "BJDebugMsg takes string msg returns nothing"},
    {"DisplayTimedTextToPlayer", "DisplayTimedTextToPlayer takes player toPlayer, real x, real y, real duration, string message returns nothing"},
    {"GetLocalPlayer", "GetLocalPlayer takes nothing returns player"},
    {"GetPlayerName", "GetPlayerName takes player whichPlayer returns string"},
    {"GetPlayerId", "GetPlayerId takes player whichPlayer returns integer"},
    {"CreateUnit", "CreateUnit takes player id, integer unitid, real x, real y, real face returns unit"},
    {"KillUnit", "KillUnit takes unit whichUnit returns nothing"},
    {"RemoveUnit", "RemoveUnit takes unit whichUnit returns nothing"},
    {"TriggerRegisterTimerEvent", "TriggerRegisterTimerEvent takes trigger whichTrigger, real timeout, boolean periodic returns event"},
    {"TriggerRegisterPlayerEvent", "TriggerRegisterPlayerEvent takes trigger whichTrigger, player whichPlayer, playerevent whichPlayerEvent returns event"},
    {"TriggerAddAction", "TriggerAddAction takes trigger whichTrigger, code actionFunc returns nothing"},
    {"EnableTrigger", "EnableTrigger takes trigger whichTrigger returns nothing"},
    {"DisableTrigger", "DisableTrigger takes trigger whichTrigger returns nothing"},
    {"DestroyTrigger", "DestroyTrigger takes trigger whichTrigger returns nothing"},
    {"TimerStart", "TimerStart takes timer whichTimer, real timeout, boolean periodic, code handlerFunc returns nothing"},
    {"DestroyTimer", "DestroyTimer takes timer whichTimer returns nothing"},
    {"GetUnitX", "GetUnitX takes unit whichUnit returns real"},
    {"GetUnitY", "GetUnitY takes unit whichUnit returns real"},
    {"SetUnitPosition", "SetUnitPosition takes unit whichUnit, real newX, real newY returns nothing"},
    {"UnitAddAbility", "UnitAddAbility takes unit whichUnit, integer abilityId returns boolean"},
    {"UnitRemoveAbility", "UnitRemoveAbility takes unit whichUnit, integer abilityId returns boolean"},
};

// ========== JassCompletionProvider ==========

class JassCompletionProvider::Impl {
public:
    std::vector<CompletionItem> allCompletions;

    Impl() {
        for (const auto& kw : jassKeywords) {
            allCompletions.push_back(completion_utils::createKeywordItem(kw));
        }
        for (const auto& type : jassTypes) {
            allCompletions.push_back(completion_utils::createTypeItem(type));
        }
        for (const auto& func : jassCommonFunctions) {
            allCompletions.push_back(completion_utils::createFunctionItem(
                func.first, func.second, "BJ Common Function"
            ));
        }
    }
};

JassCompletionProvider::JassCompletionProvider()
    : pImpl(std::make_unique<Impl>()) {}

JassCompletionProvider::~JassCompletionProvider() = default;

CompletionList JassCompletionProvider::provideCompletion(
    const std::string& documentUri,
    const std::string& documentContent,
    const Position& position
) {
    CompletionList result;
    result.isIncomplete = false;

    std::string currentLine;
    std::istringstream stream(documentContent);
    int lineNum = 0;
    while (std::getline(stream, currentLine)) {
        if (lineNum == position.line) break;
        lineNum++;
    }

    std::string prefix = completion_utils::getWordPrefix(currentLine, position.character);

    if (prefix.empty()) {
        result.items = pImpl->allCompletions;
    } else {
        result.items = completion_utils::filterCompletions(pImpl->allCompletions, prefix);
    }

    if (result.items.size() > 100) {
        result.items.resize(100);
        result.isIncomplete = true;
    }

    return result;
}

// ========== LuaCompletionProvider ==========

static const std::vector<std::string> luaKeywords = {
    "and", "break", "do", "else", "elseif", "end", "false",
    "for", "function", "if", "in", "local", "nil", "not",
    "or", "repeat", "return", "then", "true", "until", "while"
};

static const std::vector<std::string> luaCommonFunctions = {
    "print", "pairs", "ipairs", "next", "tonumber", "tostring",
    "type", "assert", "error", "pcall", "xpcall", "require",
    "load", "loadfile", "dofile"
};

class LuaCompletionProvider::Impl {
public:
    std::vector<CompletionItem> allCompletions;

    Impl() {
        for (const auto& kw : luaKeywords) {
            allCompletions.push_back(completion_utils::createKeywordItem(kw));
        }
        for (const auto& func : luaCommonFunctions) {
            CompletionItem item;
            item.label = func;
            item.kind = CompletionItemKind::Function;
            item.insertText = func + "()";
            allCompletions.push_back(item);
        }
    }
};

LuaCompletionProvider::LuaCompletionProvider()
    : pImpl(std::make_unique<Impl>()) {}

LuaCompletionProvider::~LuaCompletionProvider() = default;

CompletionList LuaCompletionProvider::provideCompletion(
    const std::string& documentUri,
    const std::string& documentContent,
    const Position& position
) {
    CompletionList result;
    result.isIncomplete = false;

    std::string currentLine;
    std::istringstream stream(documentContent);
    int lineNum = 0;
    while (std::getline(stream, currentLine)) {
        if (lineNum == position.line) break;
        lineNum++;
    }

    std::string prefix = completion_utils::getWordPrefix(currentLine, position.character);

    if (prefix.empty()) {
        result.items = pImpl->allCompletions;
    } else {
        result.items = completion_utils::filterCompletions(pImpl->allCompletions, prefix);
    }

    return result;
}

// ========== Utility functions ==========

namespace completion_utils {

std::string getWordPrefix(const std::string& line, int character) {
    if (character <= 0 || character > static_cast<int>(line.length())) {
        return "";
    }

    int start = character - 1;
    while (start >= 0 && (std::isalnum(line[start]) || line[start] == '_')) {
        start--;
    }

    return line.substr(start + 1, character - start - 1);
}

std::vector<CompletionItem> filterCompletions(
    const std::vector<CompletionItem>& candidates,
    const std::string& prefix
) {
    std::vector<CompletionItem> result;
    std::string lowerPrefix = prefix;
    std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);

    for (const auto& item : candidates) {
        std::string lowerLabel = item.label;
        std::transform(lowerLabel.begin(), lowerLabel.end(), lowerLabel.begin(), ::tolower);

        if (lowerLabel.find(lowerPrefix) == 0) {
            result.push_back(item);
        }
    }

    return result;
}

CompletionItem createKeywordItem(const std::string& keyword) {
    CompletionItem item;
    item.label = keyword;
    item.kind = CompletionItemKind::Keyword;
    item.insertText = keyword;
    return item;
}

CompletionItem createFunctionItem(
    const std::string& name,
    const std::string& detail,
    const std::string& documentation
) {
    CompletionItem item;
    item.label = name;
    item.kind = CompletionItemKind::Function;
    item.detail = detail;
    item.documentation = documentation;
    item.insertText = name + "(";
    return item;
}

CompletionItem createTypeItem(const std::string& typeName) {
    CompletionItem item;
    item.label = typeName;
    item.kind = CompletionItemKind::TypeParameter;
    item.insertText = typeName;
    return item;
}

} // namespace completion_utils

} // namespace lsp
} // namespace ydwe
