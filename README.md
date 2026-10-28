# lcf-ecs

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://isocpp.org/)
[![Version](https://img.shields.io/badge/version-1.0.8-brightgreen.svg)](VERSION.md)

一个 C++20 的 header-only 库，包含 ECS、反射、序列化与线程池四个模块，以及它们共用的基础组件。代码以头文件形式提供，不依赖第三方库。

A C++20 header-only library offering four modules — ECS, reflection, serialization and a thread pool — plus the primitives they share. Everything ships as headers, with no third-party dependencies.

## 模块 / Modules

| 模块 / Module | 内容 / Contents | 入口 / Entry |
|---|---|---|
| ECS | 实体、组件存储、视图、分组 / entities, component storage, views, groups | `include/ecs/manager.hpp` |
| 反射 / Reflection | 类型与字段注册、查询视图 / type and field registration, query views | `include/reflection/manager.hpp` |
| 序列化 / Serialization | 对象与整存档读写，四种编码格式 / object and full-archive I/O in four formats | `include/serialization/manager.hpp` |
| 线程池 / Thread pool | Worker、lane、任务图与帧循环 / workers, lanes, task graph, frame driving | `include/thread/manager.hpp` |

## 使用 / Usage

```cpp
#include "include/ecs/manager.hpp"

struct position { float x, y, z; };
struct velocity { float x, y, z; };

int main()
{
    ecs::manager mgr;

    auto e = mgr.create_entity();
    mgr.add(e, position{1.0f, 2.0f, 3.0f});
    mgr.add(e, velocity{0.5f, 0.0f, 0.0f});

    mgr.view<position, velocity>().for_each([](position& p, velocity& v) {
        p.x += v.x;
    });

    mgr.delete_entity(e);
}
```

## 环境 / Requirements

- C++20 编译器（GCC 11+ / Clang 14+ / MSVC 19.30+）/ A C++20 compiler

## 范围 / Scope

- 三个主模块（ECS、反射、线程池）互不依赖；序列化依赖 ECS 与反射。
- 类型 id 与稳定类型名保存在进程级注册表中，跨动态库使用时需自行管理生命周期。
- `part/` 包含容器、内存分配器、编解码、算法与 Unicode 处理等基础组件，部分与 ECS 无直接关系，可按需使用。
- 文档只描述接口调用方式，不展开实现细节。

- The three core modules (ECS, reflection, thread pool) do not depend on each other; serialization depends on ECS and reflection.
- Type ids and stable type names live in process-wide registries; manage their lifetime yourself when crossing shared library boundaries.
- `part/` holds containers, allocators, codecs, algorithms and Unicode helpers. Some of it is unrelated to ECS and can be used on its own.
- The documentation describes how to call the interfaces, not how they are implemented.

## 文档 / Documentation

浏览器打开 [usage/index.html](usage/index.html)，离线可用。接口按模块分章。

Open [usage/index.html](usage/index.html) in a browser; it works offline and documents interfaces per module.

## License

MIT License. See [LICENSE](LICENSE).
