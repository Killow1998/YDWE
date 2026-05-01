// test_horrible_cast.cpp
// horrible_cast 单元测试

#include "catch2/catch_amalgamated.hpp"
#include <base/util/horrible_cast.h>

TEST_CASE("horrible_cast 基本功能", "[util][horrible_cast]") {
    SECTION("float 到 int 的 bit_cast") {
        float f = 3.14159f;
        int i = base::horrible_cast<int>(f);

        // 验证转换可逆
        float f2 = base::horrible_cast<float>(i);
        REQUIRE(f == f2);
    }

    SECTION("指针到 uintptr_t 转换") {
        int x = 42;
        int* ptr = &x;
        uintptr_t addr = base::horrible_cast<uintptr_t>(ptr);
        int* ptr2 = base::horrible_cast<int*>(addr);

        REQUIRE(ptr == ptr2);
        REQUIRE(*ptr2 == 42);
    }

    SECTION("constexpr 编译期计算") {
        constexpr float f = 2.0f;
        constexpr int i = base::horrible_cast<int>(f);
        constexpr float f2 = base::horrible_cast<float>(i);
        static_assert(f == f2, "constexpr cast should be reversible");
    }
}

TEST_CASE("horrible_cast 边界情况", "[util][horrible_cast]") {
    SECTION("零值转换") {
        float f = 0.0f;
        int i = base::horrible_cast<int>(f);
        REQUIRE(i == 0);

        float f2 = base::horrible_cast<float>(i);
        REQUIRE(f2 == 0.0f);
    }

    SECTION("负零和无穷大") {
        float neg_zero = -0.0f;
        int i = base::horrible_cast<int>(neg_zero);
        float back = base::horrible_cast<float>(i);
        // 负零等于正零
        REQUIRE(back == 0.0f);
    }
}
