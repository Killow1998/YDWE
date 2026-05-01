// test_noncopyable.cpp
// noncopyable 单元测试
#include "catch2/catch_amalgamated.hpp"
#include <base/util/noncopyable.h>

class TestNonCopyable : private base::noncopyable {
public:
    int value = 0;
};

TEST_CASE("noncopyable — 拷贝被禁止", "[util][noncopyable]") {
    SECTION("拷贝构造被删除") {
        STATIC_REQUIRE(!std::is_copy_constructible_v<TestNonCopyable>);
    }
    SECTION("拷贝赋值被删除") {
        STATIC_REQUIRE(!std::is_copy_assignable_v<TestNonCopyable>);
    }
    SECTION("拷贝赋值被删除2") {
        // noncopyable 同时禁用了拷贝和移动
        STATIC_REQUIRE(!std::is_move_constructible_v<TestNonCopyable>);
    }
}

TEST_CASE("noncopyable — 正常使用", "[util][noncopyable]") {
    SECTION("默认构造") {
        TestNonCopyable obj;
        REQUIRE(obj.value == 0);
    }
    SECTION("直接修改") {
        TestNonCopyable obj;
        obj.value = 42;
        REQUIRE(obj.value == 42);
    }
}
