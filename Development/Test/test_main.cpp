// test_main.cpp
// Catch2 测试主入口

#include "catch2/catch_amalgamated.hpp"

int main(int argc, char* argv[])
{
    Catch::Session session;
    return session.run(argc, argv);
}
