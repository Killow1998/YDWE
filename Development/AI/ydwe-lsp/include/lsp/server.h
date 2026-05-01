#pragma once

#include <lsp/protocol.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <memory>
#include <sstream>

namespace ydwe {
namespace lsp {

namespace jass {
    struct JassDocument;
}

// ========== Document management ==========

struct TextDocument {
    std::string uri;
    std::string languageId;
    int version = 0;
    std::string content;
    std::shared_ptr<jass::JassDocument> parsed;
    std::vector<Diagnostic> diagnostics;
};

// ========== Server ==========

class Server {
public:
    using MessageHandler = std::function<void(const json&)>;

    Server();
    ~Server();

    void initialize(const json& params);
    bool isInitialized() const;

    // Main loop - read from stdin, write to stdout
    void run();

    // Process a single message (for testing)
    json handleMessage(const std::string& message);

    // Register custom handlers
    void onNotification(const std::string& method, MessageHandler handler);
    void onRequest(const std::string& method,
                   std::function<json(const json&)> handler);

    // Send notification to client
    void sendNotification(const std::string& method, const json& params);

    // Document management
    void openDocument(const TextDocumentItem& item);
    void changeDocument(const VersionedTextDocumentIdentifier& doc,
                       const json& contentChanges);
    void closeDocument(const TextDocumentIdentifier& doc);
    std::optional<TextDocument> getDocument(const std::string& uri) const;

    // LSP features
    CompletionList onCompletion(const TextDocumentPositionParams& params);
    json onHover(const TextDocumentPositionParams& params);
    json onDefinition(const TextDocumentPositionParams& params);
    std::vector<Diagnostic> onValidate(const std::string& uri);

    // Server info
    InitializeResult getCapabilities() const;

    // Logging
    void log(const std::string& message);
    void logError(const std::string& message);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// ========== Utility functions ==========

namespace utils {
    std::string readFile(const std::string& uri);
    std::vector<std::string> splitLines(const std::string& text);
    std::string getLine(const std::string& text, int line);
    Position positionFromOffset(const std::string& text, size_t offset);
    size_t offsetFromPosition(const std::string& text, const Position& pos);
    std::string getWordAtPosition(const std::string& text, const Position& pos);
    bool isJassKeyword(const std::string& word);
    bool isJassType(const std::string& word);
    std::vector<std::string> getJassKeywords();
    std::vector<std::string> getJassTypes();
}

} // namespace lsp
} // namespace ydwe
