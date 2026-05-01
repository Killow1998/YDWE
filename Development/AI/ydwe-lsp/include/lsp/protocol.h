#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <variant>
#include <vector>

namespace ydwe {
namespace lsp {

using json = nlohmann::json;

constexpr const char* LSP_VERSION = "3.17";

// ========== Basic types ==========

struct Position {
    int line = 0;
    int character = 0;
};

struct Range {
    Position start;
    Position end;
};

struct Location {
    std::string uri;
    Range range;
};

struct TextDocumentIdentifier {
    std::string uri;
};

struct VersionedTextDocumentIdentifier : TextDocumentIdentifier {
    int version = 0;
};

struct TextDocumentItem {
    std::string uri;
    std::string languageId;
    int version = 0;
    std::string text;
};

struct TextDocumentPositionParams {
    TextDocumentIdentifier textDocument;
    Position position;
};

// ========== Diagnostics ==========

enum class DiagnosticSeverity {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4
};

struct Diagnostic {
    Range range;
    std::optional<DiagnosticSeverity> severity;
    std::optional<std::string> code;
    std::optional<std::string> source;
    std::string message;
};

// ========== Completion ==========

enum class CompletionItemKind {
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25
};

struct CompletionItem {
    std::string label;
    std::optional<CompletionItemKind> kind;
    std::optional<std::string> detail;
    std::optional<std::string> documentation;
    std::optional<std::string> insertText;
};

struct CompletionList {
    bool isIncomplete = false;
    std::vector<CompletionItem> items;
};

// ========== Initialize ==========

struct ServerCapabilities {
    std::optional<bool> textDocumentSync = true;
    std::optional<bool> completionProvider = true;
    std::optional<bool> hoverProvider = true;
    std::optional<bool> definitionProvider = true;
    std::optional<bool> diagnosticProvider = true;
};

struct InitializeResult {
    ServerCapabilities capabilities;
};

// ========== Messages ==========

struct RequestMessage {
    std::string jsonrpc = "2.0";
    std::variant<int, std::string> id;
    std::string method;
    std::optional<json> params;
};

struct ResponseMessage {
    std::string jsonrpc = "2.0";
    std::variant<int, std::string, std::nullptr_t> id;
    std::optional<json> result;
    std::optional<json> error;
};

struct NotificationMessage {
    std::string jsonrpc = "2.0";
    std::string method;
    std::optional<json> params;
};

} // namespace lsp
} // namespace ydwe
