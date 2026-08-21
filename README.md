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
- [构建](#构建)
- [快速上手](#快速上手)
- [文档](#文档)
- [License](#license)

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
