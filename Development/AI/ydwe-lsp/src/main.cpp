// YDWE LSP Server - Entry point

#include <lsp/server.h>
#include <iostream>
#include <string>

using namespace ydwe::lsp;

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--version" || arg == "-v") {
            std::cout << "YDWE LSP Server v0.1.0" << std::endl;
            std::cout << "LSP Protocol: 3.17" << std::endl;
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ydwe-lsp [options]" << std::endl;
            std::cout << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --version, -v    Show version" << std::endl;
            std::cout << "  --help, -h       Show help" << std::endl;
            std::cout << "  --stdio          Use stdio (default)" << std::endl;
            std::cout << std::endl;
            std::cout << "Reads LSP messages from stdin, writes to stdout." << std::endl;
            return 0;
        }
    }

    try {
        Server server;
        server.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
