# lcf-ecs

> 轻量级 C++20 header-only ECS 库，内置 AVX2/SSE2 SIMD 路径与 LTO。

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://isocpp.org/)
[![Version](https://img.shields.io/badge/version-1.0.8-brightgreen.svg)](VERSION.md)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()
[![Header Only](https://img.shields.io/badge/build-header--only-orange.svg)]()

---

## 目录

- [特性](#特性)
- [编译器支持](#编译器支持)
- [构建](#构建)
- [快速上手](#快速上手)
- [文档](#文档)
- [License](#license)

---

## 特性

- **Header-Only**：仅需 `#include`，无链接库依赖
- **跨平台编译器**：GCC / Clang / MinGW / Clang-cl / MSVC 全覆盖
- **SIMD 加速**：x86/x64 启用 AVX2 + FMA + BMI2，MinGW 自动回退 SSE2/BMI2，ARM64 标量回退
- **链接时优化 (LTO)**：GCC `-flto=N`、Clang `-flto=thin`、MSVC `/GL + /LTCG`
- **自研容器**：`class_pool`（稀疏）/ `dense<T>`（密集）替代 `std::vector`
- **多视图查询**：`single_view` / `multi_view` / `runtime_view` / `group` / `reorder_group`
- **反射系统**：聚合体反射、字段查询、成员偏移计算
- **序列化框架**：JSON / Binary / Protobuf / FlatBuffer 四格式 + 编解码注册表
- **UTF-8 字符串**：`utf8pp` 拥有串 + `utf8_view` 零拷贝视图，完整 Unicode 支持
- **零异常设计**：禁用异常，错误经返回值/状态码传递

## 编译器支持

| 编译器 | 最低版本 | SIMD | LTO |
|---|---|---|---|
| GCC (Linux/macOS) | 11+ | AVX2 + FMA + BMI2 | `-flto=N` |
| MinGW GCC (Windows) | 11+ | SSE2 + BMI2 | `-flto=N` |
| Clang (Linux/macOS) | 13+ | AVX2 + FMA + BMI2 | `-flto=thin` |
| Clang-cl (Windows) | 13+ | AVX2 + FMA + BMI2 | `/GL + /LTCG` |
| MSVC `cl.exe` | 19.30+ (VS 2022) | AVX2 | `/GL + /LTCG` |

> MinGW GCC 退化为 SSE2/BMI2：MinGW x64 ABI 仅保证 16 字节栈对齐，启用 AVX2 会随机崩溃。

## 构建

需要 CMake 3.20+ 和 C++20 编译器。

```bash
# Windows (MinGW GCC)
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Windows (MSVC)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Linux / macOS
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## 快速上手

复制 `include/` 文件夹到项目中，然后 `#include "include/component.hpp"`。

```cpp
#include "include/component.hpp"

struct Position { float x, y, z; };
struct Velocity { float x, y, z; };
struct Health   { int cur, max; };

int main()
{
    ecs::manager mgr;
    mgr.append_preallocated_entities(10);

    // 创建实体并挂载组件
    auto e1 = mgr.create_entity();
    auto e2 = mgr.create_entity();
    mgr.add(e1, Position{1, 2, 3});
    mgr.add(e1, Velocity{4, 5, 6});
    mgr.addc(e2, Health{80, 100});

    // 单组件视图遍历
    mgr.view<Position>().for_each([](Position& p) {
        p.x += 1.0f;
    });

    // 多组件视图（带过滤）
    mgr.view<Position, Velocity>().for_each([](entity e, Position& p, Velocity& v) {
        p.x += v.x;
    });

    // 条件视图：拥有 Position 但缺少 Velocity
    mgr.view<Position>(ecs::without<Velocity>);

    // 拥有 Position 且同时具备 Health
    mgr.view<Position>(ecs::with<Health>);

    mgr.delete_entity(e1);
    return 0;
}
```

## 文档

详细接口文档见 [usage/index.html](usage/index.html)（浏览器打开即可，离线可用）。

## License

MIT License. See [LICENSE](LICENSE).
