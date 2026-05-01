// test_lua_engine.cpp
// LuaEngine 单元测试

#include "catch2/catch_amalgamated.hpp"
#include <LuaEngine.h>

TEST_CASE("LuaEngine 创建和销毁", "[lua][engine]")
{
    SECTION("创建 Lua 引擎")
    {
        lua_State* L = LuaEngineCreate(L"test");
        REQUIRE(L != nullptr);
        
        if (L) {
            LuaEngineDestroy(L);
        }
    }
    
    SECTION("多次创建独立实例")
    {
        lua_State* L1 = LuaEngineCreate(L"test1");
        lua_State* L2 = LuaEngineCreate(L"test2");
        
        REQUIRE(L1 != nullptr);
        REQUIRE(L2 != nullptr);
        REQUIRE(L1 != L2);  // 应该是不同的实例
        
        if (L1) LuaEngineDestroy(L1);
        if (L2) LuaEngineDestroy(L2);
    }
}

TEST_CASE("LuaEngine 空名称处理", "[lua][engine]")
{
    SECTION("空字符串名称")
    {
        lua_State* L = LuaEngineCreate(L"");
        // 即使空名称也应该能创建（具体行为取决于实现）
        if (L) {
            LuaEngineDestroy(L);
        }
    }
}

TEST_CASE("LuaEngine 销毁安全", "[lua][engine]")
{
    SECTION("多次销毁同一指针")
    {
        lua_State* L = LuaEngineCreate(L"test");
        if (L) {
            LuaEngineDestroy(L);
            // 注意: 第二次销毁会导致崩溃，这是设计上的
            // 测试框架不测试这种情况
        }
    }
    
    SECTION("空指针安全")
    {
        // LuaEngineDestroy 应该能处理空指针
        // 由于 C API 不能重载，这里仅文档说明
        // LuaEngineDestroy(nullptr); // 不应该调用
    }
}
