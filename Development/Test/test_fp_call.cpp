// test_fp_call.cpp
// fp_call 单元测试 - 函数指针调用约定测试
#include "catch2/catch_amalgamated.hpp"
#include <base/hook/fp_call.h>

// stdcall 测试函数
int __stdcall test_stdcall(int a, int b) {
    return a + b;
}
int __stdcall test_stdcall_large(int a, int b, int c, int d) {
    return a + b + c + d;
}

// cdecl 测试函数
int __cdecl test_cdecl(int a, int b, int c) {
    return a + b + c;
}
int __cdecl test_cdecl_ptr(const void* ptr, int offset) {
    return ptr != nullptr ? offset : -1;
}

// fastcall 测试函数
int __fastcall test_fastcall(int a, int b) {
    return a * b;
}
int __fastcall test_fastcall_bool(int a, int b) {
    return a != 0 && b != 0 ? 1 : 0;
}

TEST_CASE("fp_call std_call", "[hook][fp_call]") {
    SECTION("基本整数加法") {
        REQUIRE(base::std_call<int>(test_stdcall, 10, 20) == 30);
    }
    SECTION("负数") {
        REQUIRE(base::std_call<int>(test_stdcall, -5, 5) == 0);
    }
    SECTION("零值") {
        REQUIRE(base::std_call<int>(test_stdcall, 0, 0) == 0);
    }
    SECTION("大整数") {
        REQUIRE(base::std_call<int>(test_stdcall, 1000000, 999999) == 1999999);
    }
    SECTION("四参数") {
        REQUIRE(base::std_call<int>(test_stdcall_large, 1, 2, 3, 4) == 10);
    }
}

TEST_CASE("fp_call c_call", "[hook][fp_call]") {
    SECTION("三参数加法") {
        REQUIRE(base::c_call<int>(test_cdecl, 1, 2, 3) == 6);
    }
    SECTION("包含零") {
        REQUIRE(base::c_call<int>(test_cdecl, 0, 100, 50) == 150);
    }
    SECTION("指针参数非空") {
        int x = 42;
        REQUIRE(base::c_call<int>(test_cdecl_ptr, &x, 1) == 1);
    }
    SECTION("指针参数为空") {
        REQUIRE(base::c_call<int>(test_cdecl_ptr, nullptr, 0) == -1);
    }
}

TEST_CASE("fp_call fast_call", "[hook][fp_call]") {
    SECTION("基本乘法") {
        REQUIRE(base::fast_call<int>(test_fastcall, 6, 7) == 42);
    }
    SECTION("乘以零") {
        REQUIRE(base::fast_call<int>(test_fastcall, 100, 0) == 0);
    }
    SECTION("负数乘法") {
        REQUIRE(base::fast_call<int>(test_fastcall, -5, 4) == -20);
    }
    SECTION("布尔逻辑") {
        REQUIRE(base::fast_call<int>(test_fastcall_bool, 1, 1) == 1);
        REQUIRE(base::fast_call<int>(test_fastcall_bool, 0, 1) == 0);
    }
    SECTION("边界值 INT_MAX * 1") {
        REQUIRE(base::fast_call<int>(test_fastcall, 2147483647, 1) == 2147483647);
    }
}
