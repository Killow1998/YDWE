#pragma once

#include <base/util/noncopyable.h>

namespace base {
    // C++11 起，static 局部变量初始化是线程安全的
    template <typename ObjectType>
    class singleton : private noncopyable
    {
    private:
        singleton() = default;

    public:
        [[nodiscard]] static ObjectType& instance()
        {
            static ObjectType obj;
            return obj;
        }
    };

    // 为了保持向后兼容的别名
    template <typename ObjectType>
    using singleton_nonthreadsafe = singleton<ObjectType>;

    template <typename ObjectType>
    using singleton_threadsafe = singleton<ObjectType>;
}
