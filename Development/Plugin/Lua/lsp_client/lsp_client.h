#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace ydwe {
namespace lsp {
namespace client {

struct ClientConfig {
    std::string serverPath;
    std::string workspacePath;
    int logLevel = 1;
};

using MessageCallback = std::function<void(const std::string& jsonMessage)>;
using LogCallback = std::function<void(int level, const std::string& message)>;

class LspClient {
public:
    LspClient();
    ~LspClient();

    void setConfig(const ClientConfig& config);
    void setMessageCallback(MessageCallback callback);
    void setLogCallback(LogCallback callback);

    bool startServer();
    void stopServer();
    bool isRunning() const;

    bool sendMessage(const std::string& jsonMessage);
    void pollMessages();

    void notifyDocumentOpen(const std::string& uri, const std::string& languageId, const std::string& text);
    void notifyDocumentChange(const std::string& uri, int startLine, int startChar, int endLine, int endChar, const std::string& newText);
    void notifyDocumentClose(const std::string& uri);
    void notifyDocumentSave(const std::string& uri);

    void requestCompletion(const std::string& uri, int line, int character, int requestId);
    void requestDiagnostics(const std::string& uri);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

LspClient* getGlobalClient();
void shutdownGlobalClient();

} // namespace client
} // namespace lsp
} // namespace ydwe
