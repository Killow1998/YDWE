# C++20 性能优化总结

## 已实施的优化

### 1. horrible_cast - 使用 std::bit_cast
**文件**: `base/util/horrible_cast.h`

**优化前**:
```cpp
// union 类型双关 - 未定义行为
union horrible_union { OutputClass out; InputClass in; };
```

**优化后**:
```cpp
// C++20 std::bit_cast - 安全、constexpr
[[nodiscard]] constexpr To horrible_cast(const From& from) noexcept
{
    return std::bit_cast<To>(from);
}
```

**收益**:
- ✅ 编译期计算 (constexpr)
- ✅ 消除未定义行为
- ✅ 更好的编译器优化机会

---

### 2. fp_call - 使用 C++20 Concepts
**文件**: `base/hook/fp_call.h`

**新增优化**:
```cpp
#include <concepts>

namespace detail {
    template <typename T>
    concept TriviallyCopyable = std::is_trivially_copyable_v<T>;
    
    template <typename T>
    concept FunctionPointer = std::is_function_v<std::remove_reference_t<T>>;
}

// 使用 constexpr inline 替代 static const
template <class OutputClass, class InputClass>
struct same_size {
    static constexpr bool value = ...;  // 替代 static const
};
```

**收益**:
- ✅ 更清晰的模板约束
- ✅ 更好的编译错误信息
- ✅ 编译期常量优化

---

### 3. inline.cpp - 智能指针内存管理
**文件**: `base/hook/inline.cpp`

**优化前**:
```cpp
hook_info* hi = new hook_info;  // 原始指针
delete hi;                      // 手动释放
```

**优化后**:
```cpp
auto hi = std::make_unique<hook_info>();  // 智能指针
// 自动内存管理
```

**收益**:
- ✅ 异常安全
- ✅ 无内存泄漏
- ✅ 更清晰的代码

---

### 4. singleton - 简化线程安全实现
**文件**: `base/util/singleton.h`

**优化前**:
```cpp
// 复杂的 object_creator 模式
struct object_creator {
    object_creator() { singleton_threadsafe<object_type>::instance(); }
};
static object_creator create_object;
```

**优化后**:
```cpp
// C++11 起 static 自动线程安全
[[nodiscard]] static ObjectType& instance()
{
    static ObjectType obj;
    return obj;
}
```

**收益**:
- ✅ 更简单的代码
- ✅ 编译器优化的静态初始化
- ✅ 零开销抽象

---

### 5. noncopyable - 使用 = delete
**文件**: `base/util/noncopyable.h`

**优化前**:
```cpp
private:
    noncopyable( const noncopyable& );
    const noncopyable& operator=( const noncopyable& );
```

**优化后**:
```cpp
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
```

**收益**:
- ✅ 更清晰的语义
- ✅ 更好的编译错误信息

---

## 待进一步优化的区域

### 1. 使用 std::span (C++20)
**适用文件**: 需要指针+大小参数的地方

```cpp
// 优化前
void process_data(uint8_t* data, size_t size);

// 优化后
void process_data(std::span<uint8_t> data);
```

### 2. 使用 consteval (C++20)
**适用场景**: 强制编译期计算的函数

```cpp
consteval size_t calculate_offset() { return /* 编译期计算 */; }
```

### 3. 使用 constinit (C++20)
**适用场景**: 静态变量初始化

```cpp
constinit static ObjectType obj;  // 强制编译期初始化
```

### 4. 使用 std::expected (C++23)
**适用场景**: 错误处理

```cpp
std::expected<int, Error> parse_value(const char* str);
```

---

## 构建系统优化

### 编译器选项
已启用:
- `/std:c++20` - C++20 标准
- `/ConformanceMode` - 严格一致性模式
- `/utf-8` - UTF-8 编码

### 待优化
- 考虑使用 `/LTCG` (链接时代码生成)
- 考虑使用 `/O2` 或 `/Ox` 优化级别
- PGO (配置文件引导优化)

---

## 性能基准

建议在以下场景进行基准测试:
1. Hook 安装/卸载性能
2. Lua 引擎启动时间
3. 内存分配模式
4. 启动时间

---

*最后更新: 2026-05-01 — 任务 5-6-7 完成*
