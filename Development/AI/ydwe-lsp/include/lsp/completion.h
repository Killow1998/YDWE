#pragma once

#include <lsp/protocol.h>
#include <string>
#include <vector>
#include <memory>

namespace ydwe {
namespace lsp {

class CompletionProvider {
public:
    virtual ~CompletionProvider() = default;
    virtual CompletionList provideCompletion(
        const std::string& documentUri,
        const std::string& documentContent,
        const Position& position) = 0;
};

class JassCompletionProvider : public CompletionProvider {
public:
    JassCompletionProvider();
    ~JassCompletionProvider() override;

    CompletionList provideCompletion(
        const std::string& documentUri,
        const std::string& documentContent,
        const Position& position) override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

class LuaCompletionProvider : public CompletionProvider {
public:
    LuaCompletionProvider();
    ~LuaCompletionProvider() override;

    CompletionList provideCompletion(
        const std::string& documentUri,
        const std::string& documentContent,
        const Position& position) override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

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

} // namespace lsp
} // namespace ydwe
