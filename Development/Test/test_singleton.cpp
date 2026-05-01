// test_singleton.cpp
// singleton 单元测试

#include "catch2/catch_amalgamated.hpp"
#include <base/util/singleton.h>

// 测试用的简单类
class TestService {
public:
    int value = 0;

    void increment() {
        ++value;
    }
    int get_value() const {
        return value;
    }
};

// 线程安全测试用的计数器
class ThreadSafeCounter {
public:
    std::atomic<int> count{0};

    void increment() {
        ++count;
    }
    int get() const {
        return count.load();
    }
};

TEST_CASE("singleton 基本功能", "[util][singleton]") {
    SECTION("单例唯一性") {
        auto& instance1 = base::singleton<TestService>::instance();
        auto& instance2 = base::singleton<TestService>::instance();

        // 验证是同一个实例
        REQUIRE(&instance1 == &instance2);
    }

    SECTION("单例状态保持") {
        auto& service = base::singleton<TestService>::instance();

        // 修改状态
        service.value = 100;
        service.increment();

        // 再次获取，状态应保持
        auto& service2 = base::singleton<TestService>::instance();
        REQUIRE(service2.get_value() == 101);
    }
}

TEST_CASE("singleton 向后兼容别名", "[util][singleton]") {
    SECTION("singleton_nonthreadsafe 别名") {
        auto& instance1 = base::singleton_nonthreadsafe<TestService>::instance();
        auto& instance2 = base::singleton_nonthreadsafe<TestService>::instance();

        REQUIRE(&instance1 == &instance2);
    }

    SECTION("singleton_threadsafe 别名") {
        auto& instance1 = base::singleton_threadsafe<TestService>::instance();
        auto& instance2 = base::singleton_threadsafe<TestService>::instance();

        REQUIRE(&instance1 == &instance2);
    }
}

TEST_CASE("singleton 禁用拷贝", "[util][singleton]") {
    SECTION("拷贝构造被删除") {
        static_assert(!std::is_copy_constructible_v<base::singleton<TestService>>,
                      "singleton should not be copy constructible");
    }

    SECTION("拷贝赋值被删除") {
        static_assert(!std::is_copy_assignable_v<base::singleton<TestService>>,
                      "singleton should not be copy assignable");
    }
}
