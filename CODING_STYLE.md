# YDWE 代码风格指南

## 格式规范

### 缩进
- **使用 4 个空格**（禁用 Tab）
- 命名空间不增加缩进级别

### 换行
- Unix 风格换行符 (LF)
- 文件末尾保留一个空行

### 编码
- UTF-8 无 BOM（头文件和源文件）

## 命名规范

### 类型名
- `PascalCase`: `class HookManager`, `struct NodeInfo`
- 模板参数: `typename ObjectType`

### 变量名
- `snake_case`: `hook_count`, `target_address`
- 成员变量: 后缀 `_` 或前缀 `m_`（保持一致）
- 全局变量: 前缀 `g_`

### 函数名
- `snake_case`: `install_hook()`, `uninstall()`
- 布尔查询: 前缀 `is_` 或 `has_`: `is_valid()`, `has_hook()`

### 宏/常量
- `SCREAMING_SNAKE_CASE`: `MAX_HOOK_COUNT`
- 使用 `constexpr` 替代宏定义

### 命名空间
- 小写: `namespace base`, `namespace hook`
- 嵌套命名空间: `namespace base::hook`

## C++ 现代规范

### 类型特征
- 使用 `_v` 和 `_t` 后缀: `std::is_trivially_copyable_v<T>`
- 使用 `if constexpr` 替代模板特化

### 属性
- `[[nodiscard]]`: 返回值不应被忽略
- `[[maybe_unused]]`: 故意不使用

### 智能指针
- 优先使用 `std::unique_ptr`
- 工厂函数使用 `std::make_unique`/`std::make_shared`

### 函数
- 使用 `= default` 和 `= delete`
- 优先使用 `const&` 参数
- 右值引用使用 `&&`

## 头文件组织

### 包含顺序
1. 对应的 `.h` 文件（如果是 `.cpp`）
2. 项目内部头文件
3. 第三方库头文件
4. 标准库头文件
5. 系统头文件

### 示例
```cpp
// 如果是 foo.cpp
#include <base/util/foo.h>

// 项目内部
#include <base/util/bar.h>

// 第三方
#include <bee/utility/unicode.h>
#include <fmt/format.h>

// 标准库
#include <memory>
#include <vector>

// 系统
#include <windows.h>
```

### 头文件保护
```cpp
#pragma once
```

## 注释规范

### 文件头
```cpp
// filename.h
// 简要描述文件功能
// Copyright (c) 2024 YDWE Team
```

### 函数注释
```cpp
/**
 * 安装内联钩子
 * @param target 目标函数地址
 * @param detour 替换函数地址
 * @return 是否成功
 * @throws 无异常抛出
 */
bool install(uintptr_t target, uintptr_t detour);
```

## 现代 C++ 迁移清单

- [ ] 移除 `typedef`，使用 `using`
- [ ] 替换 `NULL`/`0` 为 `nullptr`
- [ ] 替换宏定义为 `constexpr`
- [ ] 使用 `auto` 简化类型声明
- [ ] 使用范围 for 循环
- [ ] 使用初始化列表
- [ ] 使用 `enum class`
