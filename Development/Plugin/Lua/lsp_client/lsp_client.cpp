#include "lsp_client.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <memory>

namespace ydwe {
namespace lsp {
namespace client {

class LspClient::Impl {
public:
    ClientConfig config;
    MessageCallback messageCallback;
    LogCallback logCallback;

    HANDLE hChildStdInRead = NULL;
    HANDLE hChildStdInWrite = NULL;
    HANDLE hChildStdOutRead = NULL;
    HANDLE hChildStdOutWrite = NULL;
    HANDLE hProcess = NULL;
    DWORD processId = 0;

    std::atomic<bool> running{false};
    std::thread readThread;
    std::mutex sendMutex;

    std::queue<std::string> incomingMessages;
    std::mutex queueMutex;

    void log(int level, const std::string& msg) {
        if (logCallback && level <= config.logLevel) {
            logCallback(level, msg);
        }
    }

    bool createPipes() {
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        if (!CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &sa, 0)) {
            log(1, "Failed to create stdout pipe");
            return false;
        }
        SetHandleInformation(hChildStdOutRead, HANDLE_FLAG_INHERIT, 0);

        if (!CreatePipe(&hChildStdInRead, &hChildStdInWrite, &sa, 0)) {
            log(1, "Failed to create stdin pipe");
            return false;
        }
        SetHandleInformation(hChildStdInWrite, HANDLE_FLAG_INHERIT, 0);

        return true;
    }

    bool startProcess() {
        if (config.serverPath.empty()) {
            log(1, "Server path not set");
            return false;
        }

        PROCESS_INFORMATION pi;
        STARTUPINFOA si;
        ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
        ZeroMemory(&si, sizeof(STARTUPINFOA));
        si.cb = sizeof(STARTUPINFOA);
        si.hStdInput = hChildStdInRead;
        si.hStdOutput = hChildStdOutWrite;
        si.hStdError = hChildStdOutWrite;
        si.dwFlags |= STARTF_USESTDHANDLES;

        BOOL success = CreateProcessA(
            config.serverPath.c_str(),
            NULL,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            config.workspacePath.empty() ? NULL : config.workspacePath.c_str(),
            &si,
            &pi
        );

        if (!success) {
            log(1, "Failed to start LSP server process");
            return false;
        }

        hProcess = pi.hProcess;
        processId = pi.dwProcessId;
        CloseHandle(pi.hThread);

        CloseHandle(hChildStdOutWrite);
        hChildStdOutWrite = NULL;
        CloseHandle(hChildStdInRead);
        hChildStdInRead = NULL;

        log(3, "LSP server started successfully");
        return true;
    }

    void readLoop() {
        const int BUFFER_SIZE = 4096;
        char buffer[BUFFER_SIZE];
        std::string messageBuffer;

        while (running) {
            DWORD bytesRead = 0;
            BOOL success = ReadFile(hChildStdOutRead, buffer, BUFFER_SIZE - 1, &bytesRead, NULL);

            if (!success || bytesRead == 0) {
                if (running) {
                    log(2, "LSP server disconnected");
                }
                break;
            }

            buffer[bytesRead] = '\0';
            messageBuffer += buffer;

            size_t pos = 0;
            while ((pos = messageBuffer.find("\r\n\r\n")) != std::string::npos) {
                std::string headers = messageBuffer.substr(0, pos);

                size_t contentLength = 0;
                size_t clPos = headers.find("Content-Length: ");
                if (clPos != std::string::npos) {
                    size_t clEnd = headers.find("\r\n", clPos);
                    std::string clStr = headers.substr(clPos + 16, clEnd - clPos - 16);
                    contentLength = std::stoul(clStr);
                }

                size_t totalLength = pos + 4 + contentLength;
                if (messageBuffer.length() >= totalLength) {
                    std::string message = messageBuffer.substr(pos + 4, contentLength);
                    messageBuffer = messageBuffer.substr(totalLength);

                    {
                        std::lock_guard<std::mutex> lock(queueMutex);
                        incomingMessages.push(message);
                    }
                } else {
                    break;
                }
            }
        }
    }
};

LspClient::LspClient() : pImpl(new Impl()) {}

LspClient::~LspClient() {
    stopServer();
}

void LspClient::setConfig(const ClientConfig& config) {
    pImpl->config = config;
}

void LspClient::setMessageCallback(MessageCallback callback) {
    pImpl->messageCallback = callback;
}

void LspClient::setLogCallback(LogCallback callback) {
    pImpl->logCallback = callback;
}

bool LspClient::startServer() {
    if (pImpl->running) {
        pImpl->log(2, "Server already running");
        return false;
    }

    if (!pImpl->createPipes()) {
        return false;
    }

    if (!pImpl->startProcess()) {
        return false;
    }

    pImpl->running = true;
    pImpl->readThread = std::thread(&Impl::readLoop, pImpl.get());

    std::string initRequest = R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}})";
    sendMessage(initRequest);

    return true;
}

void LspClient::stopServer() {
    if (!pImpl->running) {
        return;
    }

    pImpl->running = false;

    std::string shutdownRequest = R"({"jsonrpc":"2.0","id":999,"method":"shutdown"})";
    sendMessage(shutdownRequest);

    if (pImpl->readThread.joinable()) {
        pImpl->readThread.join();
    }

    if (pImpl->hChildStdInWrite) {
        CloseHandle(pImpl->hChildStdInWrite);
        pImpl->hChildStdInWrite = NULL;
    }
    if (pImpl->hChildStdOutRead) {
        CloseHandle(pImpl->hChildStdOutRead);
        pImpl->hChildStdOutRead = NULL;
    }
    if (pImpl->hProcess) {
        TerminateProcess(pImpl->hProcess, 0);
        CloseHandle(pImpl->hProcess);
        pImpl->hProcess = NULL;
    }

    pImpl->log(3, "LSP server stopped");
}

bool LspClient::isRunning() const {
    return pImpl->running;
}

bool LspClient::sendMessage(const std::string& jsonMessage) {
    if (!pImpl->running || !pImpl->hChildStdInWrite) {
        return false;
    }

    std::lock_guard<std::mutex> lock(pImpl->sendMutex);

    std::string header = "Content-Length: " + std::to_string(jsonMessage.length()) + "\r\n\r\n";
    std::string fullMessage = header + jsonMessage;

    DWORD bytesWritten = 0;
    BOOL success = WriteFile(pImpl->hChildStdInWrite, fullMessage.c_str(), static_cast<DWORD>(fullMessage.length()), &bytesWritten, NULL);

    if (!success || bytesWritten != fullMessage.length()) {
        pImpl->log(1, "Failed to send message");
        return false;
    }

    FlushFileBuffers(pImpl->hChildStdInWrite);
    return true;
}

void LspClient::pollMessages() {
    std::queue<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(pImpl->queueMutex);
        messages = std::move(pImpl->incomingMessages);
    }

    while (!messages.empty()) {
        if (pImpl->messageCallback) {
            pImpl->messageCallback(messages.front());
        }
        messages.pop();
    }
}

static std::string escapeJson(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

void LspClient::notifyDocumentOpen(const std::string& uri, const std::string& languageId, const std::string& text) {
    std::ostringstream oss;
    oss << R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")"
        << uri << R"(","languageId":")" << languageId << R"(","version":1,"text":")"
        << escapeJson(text) << R"("}}})";
    sendMessage(oss.str());
}

void LspClient::notifyDocumentChange(const std::string& uri, int startLine, int startChar, int endLine, int endChar, const std::string& newText) {
    std::ostringstream oss;
    oss << R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")"
        << uri << R"(","version":2},"contentChanges":[{"range":{"start":{"line":)"
        << startLine << R"(,"character":)" << startChar
        << R"(},"end":{"line":)" << endLine << R"(,"character":)" << endChar
        << R"(},"text":")" << escapeJson(newText) << R"("}]}})";
    sendMessage(oss.str());
}

void LspClient::notifyDocumentClose(const std::string& uri) {
    std::string msg = R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":")" + uri + R"("}}})";
    sendMessage(msg);
}

void LspClient::notifyDocumentSave(const std::string& uri) {
    std::string msg = R"({"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":")" + uri + R"("}}})";
    sendMessage(msg);
}

void LspClient::requestCompletion(const std::string& uri, int line, int character, int requestId) {
    std::ostringstream oss;
    oss << R"({"jsonrpc":"2.0","id":)" << requestId
        << R"(,"method":"textDocument/completion","params":{"textDocument":{"uri":")"
        << uri << R"("},"position":{"line":)" << line << R"(,"character":)" << character << R"(}}})";
    sendMessage(oss.str());
}

void LspClient::requestDiagnostics(const std::string& uri) {
    std::string msg = R"({"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":")" + uri + R"("}}})";
    sendMessage(msg);
}

static LspClient* g_client = nullptr;

LspClient* getGlobalClient() {
    if (!g_client) {
        g_client = new LspClient();
    }
    return g_client;
}

void shutdownGlobalClient() {
    delete g_client;
    g_client = nullptr;
}

} // namespace client
} // namespace lsp
} // namespace ydwe
