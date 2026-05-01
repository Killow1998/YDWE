// test_fp_call.cpp
// fp_call 单元测试 - 函数指针调用约定测试

#include "catch2/catch_amalgamated.hpp"
#include <base/hook/fp_call.h>

// stdcall 测试函数
int __stdcall test_stdcall(int a, int b)
{
    return a + b;
}

// cdecl 测试函数  
int __cdecl test_cdecl(int a, int b, int c)
{
    return a + b + c;
}

// fastcall 测试函数
int __fastcall test_fastcall(int a, int b)
{
    return a * b;
}

TEST_CASE("fp_call std_call 调用约定", "[hook][fp_call]")
{
    SECTION("基本整数加法")
    {
        auto result = base::std_call<int>(test_stdcall, 10, 20);
        REQUIRE(result == 30);
    }
    
    SECTION("负数运算")
    {
        auto result = base::std_call<int>(test_stdcall, -5, 5);
        REQUIRE(result == 0);
    }
    
    SECTION("零值")
    {
        auto result = base::std_call<int>(test_stdcall, 0, 0);
        REQUIRE(result == 0);
    }
}

TEST_CASE("fp_call c_call 调用约定", "[hook][fp_call]")
{
    SECTION("三参数加法")
    {
        auto result = base::c_call<int>(test_cdecl, 1, 2, 3);
        REQUIRE(result == 6);
    }
    
    SECTION("包含零")
    {
        auto result = base::c_call<int>(test_cdecl, 0, 100, 50);
        REQUIRE(result == 150);
    }
}

TEST_CASE("fp_call fast_call 调用约定", "[hook][fp_call]")
{
    SECTION("基本乘法")
    {
        auto result = base::fast_call<int>(test_fastcall, 6, 7);
        REQUIRE(result == 42);
    }
    
    SECTION("乘以零")
    {
        auto result = base::fast_call<int>(test_fastcall, 100, 0);
        REQUIRE(result == 0);
    }
    
    SECTION("负数乘法")
    {
        auto result = base::fast_call<int>(test_fastcall, -5, 4);
        REQUIRE(result == -20);
    }
}

// 注意: fp_call 设计用于普通函数指针
// this_call 需要特殊处理成员函数指针
