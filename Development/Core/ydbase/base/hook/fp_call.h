#pragma once

#include <type_traits>
#include <concepts>

// C++20 现代实现 - 使用 concepts 优化模板约束

#include <base/util/horrible_cast.h>

namespace base {

// C++20 concepts 约束
namespace detail {
    template <typename T>
    concept TriviallyCopyable = std::is_trivially_copyable_v<T>;
    
    template <typename T>
    concept FunctionPointer = std::is_function_v<std::remove_reference_t<T>>;
    
    template <typename From, typename To>
    concept SameSize = sizeof(From) == sizeof(To);
}

namespace call_ {
    // 使用 constexpr inline 替代 static const (C++17 起)
    template <class OutputClass, class InputClass>
    struct same_size {
        static constexpr bool value =
            (!std::is_reference_v<InputClass> && sizeof(OutputClass) == sizeof(InputClass))
            || (std::is_reference_v<InputClass> && sizeof(OutputClass) == sizeof(std::add_pointer_t<InputClass>));
    };

    template <class Argument>
    [[nodiscard]] inline uintptr_t cast(Argument input)
    {
        if constexpr (std::is_function_v<std::remove_reference_t<Argument>>)
        {
            static_assert(std::is_trivially_copyable_v<Argument>, "Argument is not trivially copyable.");
            static_assert(sizeof(Argument) == sizeof(uintptr_t), "Argument and uintptr_t are not the same size.");
            return horrible_cast<uintptr_t>(input);
        }
        else if constexpr (same_size<uintptr_t, Argument>::value)
        {
            static_assert(std::is_trivially_copyable_v<Argument>, "Argument is not trivially copyable.");
            return horrible_cast<uintptr_t>(input);
        }
        else
        {
            static_assert(std::is_trivially_copyable_v<Argument>, "Argument is not trivially copyable.");
            static_assert(sizeof(Argument) <= sizeof(uintptr_t), "Argument can not be converted to uintptr_t.");
            return static_cast<uintptr_t>(input);
        }
    }

    template <typename Arg>
    struct cast_type {
        typedef uintptr_t type;
    };
}

template <typename R, typename F, typename ... Args>
inline R std_call(F f, Args ... args)
{
    return (reinterpret_cast<R(__stdcall *)(typename call_::cast_type<Args>::type ... args)>(f))(call_::cast(args)...);
}

template <typename R, typename F, typename ... Args>
inline R fast_call(F f, Args ... args)
{
    return (reinterpret_cast<R(__fastcall *)(typename call_::cast_type<Args>::type ... args)>(f))(call_::cast(args)...);
}

template <typename R, typename F, typename ... Args>
inline R c_call(F f, Args ... args)
{
    return (reinterpret_cast<R(__cdecl *)(typename call_::cast_type<Args>::type ... args)>(f))(call_::cast(args)...);
}

template <typename R, typename F, typename This, typename ... Args>
inline R this_call(F f, This t, Args ... args)
{
    return (reinterpret_cast<R(__fastcall *)(typename call_::cast_type<This>::type, void*, typename call_::cast_type<Args>::type ... args)>(f))(call_::cast(t), 0, call_::cast(args)...);
}
}

