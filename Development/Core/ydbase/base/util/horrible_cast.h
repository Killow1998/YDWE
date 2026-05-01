#pragma once

#include <bit>
#include <type_traits>

namespace base {
// 使用 C++20 std::bit_cast 替代 union 类型双关（避免未定义行为）
template <class To, class From>
[[nodiscard]] constexpr To horrible_cast(const From& from) noexcept {
    static_assert(sizeof(To) == sizeof(From), "To and From must be the same size");
    static_assert(std::is_trivially_copyable_v<From>, "From must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<To>, "To must be trivially copyable");
    return std::bit_cast<To>(from);
}
} // namespace base
