// YDWE LSP Server - Core implementation
#include <lsp/server.h>
#include <lsp/completion.h>
#include <jass/lexer.h>
#include <jass/parser.h>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <cstring>

namespace ydwe {
namespace lsp {

// ========== Server::Impl ==========

class Server::Impl {
public:
    bool initialized = false;
    bool shutdown = false;
    std::unordered_map<std::string, TextDocument> documents;
    jass::Parser parser;

    // Request handlers
    std::unordered_map<std::string, std::function<json(const json&)>> requestHandlers;
    // Notification handlers
    std::unordered_map<std::string, std::function<void(const json&)>> notificationHandlers;

    Impl() {
        registerBuiltinHandlers();
    }

    void registerBuiltinHandlers() {
        requestHandlers["initialize"] = [this](const json& params) -> json {
            return handleInitialize(params);
        };
        requestHandlers["shutdown"] = [this](const json& params) -> json {
            return handleShutdown(params);
        };
        requestHandlers["textDocument/completion"] = [this](const json& params) -> json {
            return handleCompletion(params);
        };
        requestHandlers["textDocument/hover"] = [this](const json& params) -> json {
            return handleHover(params);
        };
        requestHandlers["textDocument/definition"] = [this](const json& params) -> json {
            return handleDefinition(params);
        };
        requestHandlers["textDocument/documentSymbol"] = [this](const json& params) -> json {
            return handleDocumentSymbol(params);
        };

        notificationHandlers["initialized"] = [this](const json& params) {
        };
        notificationHandlers["textDocument/didOpen"] = [this](const json& params) {
            handleDidOpen(params);
        };
        notificationHandlers["textDocument/didChange"] = [this](const json& params) {
            handleDidChange(params);
        };
        notificationHandlers["textDocument/didClose"] = [this](const json& params) {
            handleDidClose(params);
        };
        notificationHandlers["textDocument/didSave"] = [this](const json& params) {
            handleDidSave(params);
        };
        notificationHandlers["exit"] = [this](const json& params) {
            shutdown = true;
        };
    }

    // ---- Initialize ----
    json handleInitialize(const json& params) {
        initialized = true;

        json result;
        result["capabilities"] = json::object();
        result["capabilities"]["textDocumentSync"] = 1;
        result["capabilities"]["completionProvider"] = json::object();
        result["capabilities"]["completionProvider"]["triggerCharacters"] = json::array({".", ":"});
        result["capabilities"]["hoverProvider"] = true;
        result["capabilities"]["definitionProvider"] = true;
        result["capabilities"]["documentSymbolProvider"] = true;
        result["capabilities"]["publishDiagnostics"] = json::object();
        result["capabilities"]["publishDiagnostics"]["relatedInformation"] = false;

        return result;
    }

    // ---- Shutdown ----
    json handleShutdown(const json& params) {
        shutdown = true;
        return nullptr;
    }

    // ---- Parse and publish diagnostics ----
    void parseAndPublishDiagnostics(const std::string& uri) {
        auto it = documents.find(uri);
        if (it == documents.end()) return;

        TextDocument& doc = it->second;

        if (doc.languageId == "jass") {
            // Parse the document
            auto jassDoc = std::make_shared<jass::JassDocument>(parser.parse(doc.content));
            doc.parsed = jassDoc;

            // Collect diagnostics from parser
            auto parseDiags = parser.getDiagnostics();

            // Collect diagnostics from analyzer
            auto analyzeDiags = jass::Analyzer::analyze(*jassDoc, doc.content);

            // Merge diagnostics
            doc.diagnostics.clear();
            doc.diagnostics.insert(doc.diagnostics.end(), parseDiags.begin(), parseDiags.end());
            doc.diagnostics.insert(doc.diagnostics.end(), analyzeDiags.begin(), analyzeDiags.end());

            // Publish diagnostics notification
            json diagParams;
            diagParams["uri"] = uri;
            diagParams["diagnostics"] = json::array();

            for (const auto& d : doc.diagnostics) {
                json jd;
                jd["range"]["start"]["line"] = d.range.start.line;
                jd["range"]["start"]["character"] = d.range.start.character;
                jd["range"]["end"]["line"] = d.range.end.line;
                jd["range"]["end"]["character"] = d.range.end.character;
                if (d.severity.has_value()) {
                    jd["severity"] = static_cast<int>(d.severity.value());
                }
                jd["message"] = d.message;
                if (d.source.has_value()) {
                    jd["source"] = d.source.value();
                }
                diagParams["diagnostics"].push_back(jd);
            }

            sendNotification("textDocument/publishDiagnostics", diagParams);
        }
    }

    // ---- Completion ----
    json handleCompletion(const json& params) {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        int line = params["position"]["line"].get<int>();
        int character = params["position"]["character"].get<int>();

        auto it = documents.find(uri);
        if (it == documents.end()) {
            json result;
            result["isIncomplete"] = false;
            result["items"] = json::array();
            return result;
        }

        const TextDocument& doc = it->second;
        std::string langId = doc.languageId;

        json result;
        result["isIncomplete"] = false;
        result["items"] = json::array();

        Position pos{line, character};
        std::string prefix = utils::getWordAtPosition(doc.content, pos);

        if (langId == "jass" || langId == "lua") {
            CompletionProvider* provider = nullptr;
            JassCompletionProvider jassProvider;
            LuaCompletionProvider luaProvider;

            if (langId == "jass") {
                provider = &jassProvider;
            } else {
                provider = &luaProvider;
            }

            CompletionList list = provider->provideCompletion(uri, doc.content, pos);

            result["isIncomplete"] = list.isIncomplete;
            for (const auto& item : list.items) {
                json jItem;
                jItem["label"] = item.label;
                if (item.kind.has_value()) {
                    jItem["kind"] = static_cast<int>(item.kind.value());
                }
                if (item.detail.has_value()) {
                    jItem["detail"] = item.detail.value();
                }
                if (item.documentation.has_value()) {
                    jItem["documentation"] = item.documentation.value();
                }
                if (item.insertText.has_value()) {
                    jItem["insertText"] = item.insertText.value();
                }
                result["items"].push_back(jItem);
            }

            // Add user-defined symbols from parsed document
            if (doc.parsed && langId == "jass") {
                auto& jassDoc = *doc.parsed;

                // User-defined functions
                for (const auto& f : jassDoc.functions) {
                    if (!prefix.empty() && f.name.find(prefix) != 0 &&
                        std::string(f.name.size(), '\0') != prefix) {
                        // Case-insensitive prefix check
                        std::string lowerName = f.name;
                        std::string lowerPrefix = prefix;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                        std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);
                        if (lowerName.find(lowerPrefix) != 0) continue;
                    }

                    std::ostringstream detail;
                    detail << "function " << f.name << " takes ";
                    for (size_t i = 0; i < f.parameters.size(); i++) {
                        if (i > 0) detail << ", ";
                        detail << f.parameters[i].type.name << " " << f.parameters[i].name;
                    }
                    detail << " returns " << f.returnType.name;

                    json jItem;
                    jItem["label"] = f.name;
                    jItem["kind"] = static_cast<int>(CompletionItemKind::Function);
                    jItem["detail"] = detail.str();
                    jItem["insertText"] = f.name + "(";
                    result["items"].push_back(jItem);
                }

                // User-defined global variables
                for (const auto& g : jassDoc.globals) {
                    if (!prefix.empty()) {
                        std::string lowerName = g.name;
                        std::string lowerPrefix = prefix;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                        std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);
                        if (lowerName.find(lowerPrefix) != 0) continue;
                    }

                    json jItem;
                    jItem["label"] = g.name;
                    jItem["kind"] = g.isConstant ? static_cast<int>(CompletionItemKind::Constant)
                                                 : static_cast<int>(CompletionItemKind::Variable);
                    jItem["detail"] = g.type.name + (g.isArray ? "[]" : "") + " (global)";
                    jItem["insertText"] = g.name;
                    result["items"].push_back(jItem);
                }

                // User-defined types
                for (const auto& t : jassDoc.types) {
                    if (!prefix.empty()) {
                        std::string lowerName = t.name;
                        std::string lowerPrefix = prefix;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                        std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);
                        if (lowerName.find(lowerPrefix) != 0) continue;
                    }

                    json jItem;
                    jItem["label"] = t.name;
                    jItem["kind"] = static_cast<int>(CompletionItemKind::Class);
                    jItem["detail"] = "type " + t.name;
                    if (t.extends.has_value()) {
                        jItem["detail"] = jItem["detail"].get<std::string>() + " extends " + t.extends.value();
                    }
                    jItem["insertText"] = t.name;
                    result["items"].push_back(jItem);
                }

                // Local variables and parameters in current function
                for (const auto& f : jassDoc.functions) {
                    bool inFunction = f.bodyRange.has_value() &&
                        pos.line >= f.range.start.line &&
                        pos.line <= f.range.end.line;
                    if (!inFunction) continue;

                    // Parameters
                    for (const auto& p : f.parameters) {
                        if (!prefix.empty()) {
                            std::string lowerName = p.name;
                            std::string lowerPrefix = prefix;
                            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                            std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);
                            if (lowerName.find(lowerPrefix) != 0) continue;
                        }

                        json jItem;
                        jItem["label"] = p.name;
                        jItem["kind"] = static_cast<int>(CompletionItemKind::Variable);
                        jItem["detail"] = p.type.name + " (parameter)";
                        jItem["insertText"] = p.name;
                        result["items"].push_back(jItem);
                    }

                    // Locals
                    auto locs = jassDoc.findLocals(f.name);
                    for (const auto& lv : locs) {
                        if (!prefix.empty()) {
                            std::string lowerName = lv.name;
                            std::string lowerPrefix = prefix;
                            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                            std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);
                            if (lowerName.find(lowerPrefix) != 0) continue;
                        }

                        json jItem;
                        jItem["label"] = lv.name;
                        jItem["kind"] = static_cast<int>(CompletionItemKind::Variable);
                        jItem["detail"] = lv.type.name + " (local)";
                        jItem["insertText"] = lv.name;
                        result["items"].push_back(jItem);
                    }

                    break;
                }
            }
        }

        return result;
    }

    // ---- Hover ----
    json handleHover(const json& params) {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        int line = params["position"]["line"].get<int>();
        int character = params["position"]["character"].get<int>();

        auto it = documents.find(uri);
        if (it == documents.end()) {
            return nullptr;
        }

        const TextDocument& doc = it->second;
        Position pos{line, character};
        std::string word = utils::getWordAtPosition(doc.content, pos);

        if (word.empty()) {
            return nullptr;
        }

        std::string hoverText;

        // Look up in parsed document symbols
        if (doc.parsed && doc.languageId == "jass") {
            auto& jassDoc = *doc.parsed;

            // Check functions
            auto func = jassDoc.findFunction(word);
            if (func.has_value()) {
                std::ostringstream oss;
                oss << "function " << func->name << " takes ";
                for (size_t i = 0; i < func->parameters.size(); i++) {
                    if (i > 0) oss << ", ";
                    oss << func->parameters[i].type.name << " " << func->parameters[i].name;
                }
                oss << " returns " << func->returnType.name;
                if (func->isNative) oss << " [native]";
                hoverText = oss.str();
            }

            // Check globals
            if (hoverText.empty()) {
                auto global = jassDoc.findGlobal(word);
                if (global.has_value()) {
                    hoverText = "global " + global->type.name + " " + global->name;
                    if (global->isArray) hoverText += " (array)";
                    if (global->isConstant) hoverText += " (constant)";
                }
            }

            // Check locals and parameters in current function
            if (hoverText.empty()) {
                for (const auto& f : jassDoc.functions) {
                    bool inFunction = f.bodyRange.has_value() &&
                        pos.line >= f.range.start.line &&
                        pos.line <= f.range.end.line;
                    if (!inFunction) continue;

                    // Check parameters
                    for (const auto& p : f.parameters) {
                        if (p.name == word) {
                            hoverText = "parameter " + p.type.name + " " + p.name;
                            break;
                        }
                    }
                    // Check locals
                    if (hoverText.empty()) {
                        auto locals = jassDoc.findLocals(f.name);
                        for (const auto& lv : locals) {
                            if (lv.name == word) {
                                hoverText = "local " + lv.type.name + " " + lv.name;
                                break;
                            }
                        }
                    }
                    break;
                }
            }

            // Check types
            if (hoverText.empty()) {
                for (const auto& t : jassDoc.types) {
                    if (t.name == word) {
                        hoverText = "type " + t.name;
                        if (t.extends.has_value()) {
                            hoverText += " extends " + t.extends.value();
                        }
                        break;
                    }
                }
            }
        }

        // Fallback to keyword/type check
        if (hoverText.empty()) {
            if (utils::isJassKeyword(word)) {
                hoverText = "keyword: " + word;
            } else if (utils::isJassType(word)) {
                hoverText = "type: " + word;
            }
        }

        if (hoverText.empty()) {
            return nullptr;
        }

        json result;
        result["contents"] = json::object();
        result["contents"]["kind"] = "plaintext";
        result["contents"]["value"] = hoverText;

        // Calculate range for the word
        auto lines = utils::splitLines(doc.content);
        if (line >= 0 && line < static_cast<int>(lines.size())) {
            const std::string& ln = lines[line];
            int start = character;
            while (start > 0 && (std::isalnum(ln[start - 1]) || ln[start - 1] == '_')) start--;
            int end = character;
            while (end < static_cast<int>(ln.size()) && (std::isalnum(ln[end]) || ln[end] == '_')) end++;

            result["range"] = json::object();
            result["range"]["start"] = {{"line", line}, {"character", start}};
            result["range"]["end"] = {{"line", line}, {"character", end}};
        }

        return result;
    }

    // ---- Definition ----
    json handleDefinition(const json& params) {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        int line = params["position"]["line"].get<int>();
        int character = params["position"]["character"].get<int>();

        auto it = documents.find(uri);
        if (it == documents.end()) {
            return nullptr;
        }

        const TextDocument& doc = it->second;
        if (!doc.parsed || doc.languageId != "jass") {
            return nullptr;
        }

        Position pos{line, character};
        std::string word = utils::getWordAtPosition(doc.content, pos);
        if (word.empty()) return nullptr;

        auto& jassDoc = *doc.parsed;

        // Find function definition
        auto func = jassDoc.findFunction(word);
        if (func.has_value()) {
            json loc;
            loc["uri"] = uri;
            loc["range"]["start"]["line"] = func->range.start.line;
            loc["range"]["start"]["character"] = func->range.start.character;
            loc["range"]["end"]["line"] = func->range.end.line;
            loc["range"]["end"]["character"] = func->range.end.character;
            return json::array({loc});
        }

        // Find global definition
        auto global = jassDoc.findGlobal(word);
        if (global.has_value()) {
            json loc;
            loc["uri"] = uri;
            loc["range"]["start"]["line"] = global->range.start.line;
            loc["range"]["start"]["character"] = global->range.start.character;
            loc["range"]["end"]["line"] = global->range.end.line;
            loc["range"]["end"]["character"] = global->range.end.character;
            return json::array({loc});
        }

        // Find local variable or parameter definition in current function
        for (const auto& f : jassDoc.functions) {
            if (f.bodyRange.has_value() &&
                pos.line >= f.range.start.line &&
                pos.line <= f.range.end.line) {
                // Check parameters
                for (const auto& p : f.parameters) {
                    if (p.name == word) {
                        json loc;
                        loc["uri"] = uri;
                        loc["range"]["start"]["line"] = p.position.line;
                        loc["range"]["start"]["character"] = p.position.character;
                        loc["range"]["end"]["line"] = p.position.line;
                        loc["range"]["end"]["character"] = p.position.character + word.length();
                        return json::array({loc});
                    }
                }
                // Check locals
                auto locs = jassDoc.findLocals(f.name);
                for (const auto& lv : locs) {
                    if (lv.name == word) {
                        json loc;
                        loc["uri"] = uri;
                        loc["range"]["start"]["line"] = lv.position.line;
                        loc["range"]["start"]["character"] = lv.position.character;
                        loc["range"]["end"]["line"] = lv.position.line;
                        loc["range"]["end"]["character"] = lv.position.character + word.length();
                        return json::array({loc});
                    }
                }
                break;
            }
        }

        return nullptr;
    }

    // ---- Document Symbol ----
    json handleDocumentSymbol(const json& params) {
        std::string uri = params["textDocument"]["uri"].get<std::string>();

        auto it = documents.find(uri);
        if (it == documents.end()) {
            return json::array();
        }

        const TextDocument& doc = it->second;
        if (!doc.parsed || doc.languageId != "jass") {
            return json::array();
        }

        auto& jassDoc = *doc.parsed;
        json symbols = json::array();

        // Functions
        for (const auto& f : jassDoc.functions) {
            json sym;
            sym["name"] = f.name;
            sym["kind"] = 12; // Function = 12
            sym["range"]["start"]["line"] = f.range.start.line;
            sym["range"]["start"]["character"] = f.range.start.character;
            sym["range"]["end"]["line"] = f.range.end.line;
            sym["range"]["end"]["character"] = f.range.end.character;
            if (f.bodyRange.has_value()) {
                sym["selectionRange"]["start"]["line"] = f.bodyRange->start.line;
                sym["selectionRange"]["start"]["character"] = f.bodyRange->start.character;
                sym["selectionRange"]["end"]["line"] = f.bodyRange->start.line;
                sym["selectionRange"]["end"]["character"] = f.bodyRange->start.character + f.name.length();
            }
            std::string detail = f.returnType.name;
            sym["detail"] = detail;
            symbols.push_back(sym);
        }

        // Globals
        for (const auto& g : jassDoc.globals) {
            json sym;
            sym["name"] = g.name;
            sym["kind"] = g.isConstant ? 14 : 13; // Constant = 14, Variable = 13
            sym["range"]["start"]["line"] = g.range.start.line;
            sym["range"]["start"]["character"] = g.range.start.character;
            sym["range"]["end"]["line"] = g.range.end.line;
            sym["range"]["end"]["character"] = g.range.end.character;
            sym["detail"] = g.type.name + (g.isArray ? "[]" : "");
            symbols.push_back(sym);
        }

        return symbols;
    }

    // ---- didOpen ----
    void handleDidOpen(const json& params) {
        TextDocument doc;
        doc.uri = params["textDocument"]["uri"].get<std::string>();
        doc.languageId = params["textDocument"]["languageId"].get<std::string>();
        doc.version = params["textDocument"]["version"].get<int>();
        doc.content = params["textDocument"]["text"].get<std::string>();

        documents[doc.uri] = std::move(doc);
        parseAndPublishDiagnostics(params["textDocument"]["uri"].get<std::string>());
    }

    // ---- didChange ----
    void handleDidChange(const json& params) {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        auto it = documents.find(uri);
        if (it == documents.end()) return;

        const auto& changes = params["contentChanges"];
        if (changes.is_array() && !changes.empty()) {
            it->second.content = changes.back()["text"].get<std::string>();
            if (params["textDocument"].contains("version")) {
                it->second.version = params["textDocument"]["version"].get<int>();
            }
        }

        parseAndPublishDiagnostics(uri);
    }

    // ---- didClose ----
    void handleDidClose(const json& params) {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        documents.erase(uri);

        // Clear diagnostics for closed document
        json diagParams;
        diagParams["uri"] = uri;
        diagParams["diagnostics"] = json::array();
        sendNotification("textDocument/publishDiagnostics", diagParams);
    }

    // ---- didSave ----
    void handleDidSave(const json& params) {
        std::string uri = params["textDocument"]["uri"].get<std::string>();
        parseAndPublishDiagnostics(uri);
    }

    // ---- Message I/O ----

    std::string readMessage() {
        std::string headerLine;
        size_t contentLength = 0;

        while (std::getline(std::cin, headerLine)) {
            if (!headerLine.empty() && headerLine.back() == '\r') {
                headerLine.pop_back();
            }
            if (headerLine.empty()) {
                break;
            }
            if (headerLine.find("Content-Length:") == 0) {
                contentLength = std::stoul(headerLine.substr(16));
            }
        }

        if (contentLength == 0) {
            return "";
        }

        std::string body(contentLength, '\0');
        std::cin.read(body.data(), static_cast<std::streamsize>(contentLength));

        if (std::cin.gcount() != static_cast<std::streamsize>(contentLength)) {
            return "";
        }

        return body;
    }

    void sendMessage(const json& message) {
        std::string body = message.dump();
        std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
        std::cout << header << body << std::flush;
    }

    void sendNotification(const std::string& method, const json& params) {
        json notification;
        notification["jsonrpc"] = "2.0";
        notification["method"] = method;
        notification["params"] = params;
        sendMessage(notification);
    }

    json processMessage(const std::string& body) {
        json msg = json::parse(body, nullptr, false);
        if (msg.is_discarded()) {
            return json();
        }

        std::string method;
        if (msg.contains("method")) {
            method = msg["method"].get<std::string>();
        }

        json params;
        if (msg.contains("params")) {
            params = msg["params"];
        }

        bool isRequest = msg.contains("id");

        if (isRequest) {
            json response;
            response["jsonrpc"] = "2.0";
            response["id"] = msg["id"];

            auto it = requestHandlers.find(method);
            if (it != requestHandlers.end()) {
                try {
                    json result = it->second(params);
                    response["result"] = result;
                } catch (const std::exception& e) {
                    response["error"] = json::object();
                    response["error"]["code"] = -32603;
                    response["error"]["message"] = std::string("Internal error: ") + e.what();
                }
            } else {
                response["error"] = json::object();
                response["error"]["code"] = -32601;
                response["error"]["message"] = "Method not found: " + method;
            }

            return response;
        } else {
            auto it = notificationHandlers.find(method);
            if (it != notificationHandlers.end()) {
                try {
                    it->second(params);
                } catch (...) {
                }
            }
            return json();
        }
    }
};

// ========== Server public interface ==========

Server::Server() : pImpl(new Impl()) {}

Server::~Server() = default;

void Server::initialize(const json& params) {
    pImpl->initialized = true;
}

bool Server::isInitialized() const {
    return pImpl->initialized;
}

void Server::run() {
    while (!pImpl->shutdown) {
        std::string body = pImpl->readMessage();
        if (body.empty()) {
            break;
        }

        json response = pImpl->processMessage(body);
        if (!response.is_null()) {
            pImpl->sendMessage(response);
        }
    }
}

json Server::handleMessage(const std::string& message) {
    return pImpl->processMessage(message);
}

void Server::onNotification(const std::string& method, MessageHandler handler) {
    pImpl->notificationHandlers[method] = handler;
}

void Server::onRequest(const std::string& method, std::function<json(const json&)> handler) {
    pImpl->requestHandlers[method] = handler;
}

void Server::sendNotification(const std::string& method, const json& params) {
    pImpl->sendNotification(method, params);
}

void Server::openDocument(const TextDocumentItem& item) {
    TextDocument doc;
    doc.uri = item.uri;
    doc.languageId = item.languageId;
    doc.version = item.version;
    doc.content = item.text;
    pImpl->documents[doc.uri] = std::move(doc);
}

void Server::changeDocument(const VersionedTextDocumentIdentifier& doc, const json& contentChanges) {
    auto it = pImpl->documents.find(doc.uri);
    if (it == pImpl->documents.end()) return;
    it->second.version = doc.version;
    if (contentChanges.is_array() && !contentChanges.empty()) {
        it->second.content = contentChanges.back()["text"].get<std::string>();
    }
}

void Server::closeDocument(const TextDocumentIdentifier& doc) {
    pImpl->documents.erase(doc.uri);
}

std::optional<TextDocument> Server::getDocument(const std::string& uri) const {
    auto it = pImpl->documents.find(uri);
    if (it != pImpl->documents.end()) {
        return it->second;
    }
    return std::nullopt;
}

CompletionList Server::onCompletion(const TextDocumentPositionParams& params) {
    return CompletionList{};
}

json Server::onHover(const TextDocumentPositionParams& params) {
    return nullptr;
}

json Server::onDefinition(const TextDocumentPositionParams& params) {
    return nullptr;
}

std::vector<Diagnostic> Server::onValidate(const std::string& uri) {
    auto it = pImpl->documents.find(uri);
    if (it == pImpl->documents.end()) return {};
    return it->second.diagnostics;
}

InitializeResult Server::getCapabilities() const {
    InitializeResult result;
    result.capabilities.textDocumentSync = true;
    result.capabilities.completionProvider = true;
    result.capabilities.hoverProvider = true;
    result.capabilities.definitionProvider = true;
    result.capabilities.diagnosticProvider = true;
    return result;
}

void Server::log(const std::string& message) {
    std::cerr << "[YDWE LSP] " << message << std::endl;
}

void Server::logError(const std::string& message) {
    std::cerr << "[YDWE LSP ERROR] " << message << std::endl;
}

// ========== Utils ==========

namespace utils {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string getLine(const std::string& text, int line) {
    auto lines = splitLines(text);
    if (line >= 0 && line < static_cast<int>(lines.size())) {
        return lines[line];
    }
    return "";
}

std::string getWordAtPosition(const std::string& text, const Position& pos) {
    auto lines = splitLines(text);
    if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return "";

    const std::string& line = lines[pos.line];
    if (pos.character < 0 || pos.character > static_cast<int>(line.length())) return "";

    int start = pos.character;
    while (start > 0 && (std::isalnum(line[start - 1]) || line[start - 1] == '_')) {
        start--;
    }

    int end = pos.character;
    while (end < static_cast<int>(line.length()) &&
           (std::isalnum(line[end]) || line[end] == '_')) {
        end++;
    }

    return line.substr(start, end - start);
}

bool isJassKeyword(const std::string& word) {
    static const std::vector<std::string> keywords = {
        "function", "endfunction", "takes", "returns", "nothing",
        "globals", "endglobals", "local", "set", "call", "return",
        "if", "then", "else", "elseif", "endif",
        "loop", "exitwhen", "endloop",
        "true", "false", "null",
        "constant", "native", "type", "extends", "array"
    };
    return std::find(keywords.begin(), keywords.end(), word) != keywords.end();
}

bool isJassType(const std::string& word) {
    static const std::vector<std::string> types = {
        "nothing", "null", "boolean", "integer", "real", "string",
        "handle", "code"
    };
    return std::find(types.begin(), types.end(), word) != types.end();
}

std::vector<std::string> getJassKeywords() {
    return {
        "function", "endfunction", "takes", "returns", "nothing",
        "globals", "endglobals", "local", "set", "call", "return",
        "if", "then", "else", "elseif", "endif",
        "loop", "exitwhen", "endloop",
        "true", "false", "null",
        "constant", "native", "type", "extends", "array"
    };
}

std::vector<std::string> getJassTypes() {
    return {
        "nothing", "null", "boolean", "integer", "real", "string",
        "handle", "code"
    };
}

} // namespace utils

} // namespace lsp
} // namespace ydwe
