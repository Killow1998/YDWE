// Minimal test main
#include <jass/lexer.h>
#include <iostream>
#include <string>

int main() {
    std::string testCode = R"(
function Test takes nothing returns nothing
    local integer i = 0
    if i == 0 then
        call BJDebugMsg("Hello")
    endif
endfunction
)";

    ydwe::lsp::jass::Lexer lexer(testCode);
    auto tokens = lexer.tokenize();
    
    std::cout << "Tokenized " << tokens.size() << " tokens" << std::endl;
    
    for (const auto& token : tokens) {
        std::cout << "Token: " << token.value << " (type: " << static_cast<int>(token.type) << ")" << std::endl;
    }
    
    return 0;
}
