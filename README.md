# lcf-ecs

A lightweight, header-only Entity-Component-System library for C++20.
Supports GCC / Clang / MSVC with AVX2/SSE2 SIMD paths and LTO.

轻量级 C++20 ECS 库, header-only 设计, 支持 GCC / Clang / MSVC 三编译器,
内置 AVX2/SSE2 SIMD 路径与 LTO 链接时优化.

---

## Compiler Support / 编译器支持

| Compiler / 编译器 | Min Version / 最低版本 | Tested / 已实测 | ABI | SIMD | LTO |
|---|---|---|---|---|---|
| GCC (Linux / macOS) | 11+ | — | SysV | AVX2 + FMA + BMI2 | `-flto=N` |
| MinGW GCC (Windows) | 11+ | 16.1.0 | MinGW x64 | SSE2 + BMI2 (1) | `-flto=N` |
| Clang (Linux / macOS) | 13+ | — | SysV | AVX2 + FMA + BMI2 | `-flto=thin` |
| Clang (Windows, MinGW target) | 13+ | — | MinGW x64 | AVX2 + FMA + BMI2 | `-flto=thin` |
| Clang-cl (Windows, MSVC ABI) | 13+ | 22.1.8 | MSVC | AVX2 + FMA + BMI2 | `/GL + /LTCG` |
| MSVC `cl.exe` | 19.30+ (VS 2022) | 19.50 (VS 2026) | MSVC | AVX2 (2) | `/GL + /LTCG` |

(1) MinGW GCC 启用 `-mavx2` 会生成 `vmovdqa` (32 字节栈对齐), 但 MinGW x64 ABI 遵循
    Windows x64 ABI 规范, 仅保证 16 字节栈对齐 (设计规范, 非 bug) → 随机崩溃
    (0xC0000005, ASLR 相关). GCC 默认不为 32 字节对齐插入动态栈调整 prologue.
    故 MinGW GCC 退化为 SSE2/BMI2; 代码内 AVX2 路径均有 `#ifdef __AVX2__` 标量回退.
    MSVC `/arch:AVX2` 会自动插入动态栈对齐; SysV ABI 为 AVX 函数维护 32 字节栈对齐.

(2) MSVC `/arch:AVX2` 自动插入动态栈对齐, 无崩溃风险.

---

## Build / 构建

Requires CMake 3.20+ and a C++20 compiler.
需要 CMake 3.20+ 和 C++20 编译器.

### Windows (MinGW GCC)

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows (MSVC / clang-cl)

```bash
# VS 2022
cmake -B build -G "Visual Studio 17 2022" -A x64
# VS 2026
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

If Visual Studio is installed in a non-default path, add `-DCMAKE_GENERATOR_INSTANCE="D:\vs"`.
若 VS 安装在非默认路径, 需加 `-DCMAKE_GENERATOR_INSTANCE="D:\vs"`.

### Windows (Clang, MinGW target)

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Linux / macOS (GCC or Clang)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

---

## Build Options / 构建选项

| Option | Default | Description |
|---|---|---|
| `BUILD_USAGEC` | ON | usagec 示例程序 |
| `BUILD_FUNCTIONAL_TESTS` | ON | 功能测试 (test_functional) |
| `BUILD_PERF_TESTS` | ON | 性能测试 (test_perf, 308 项基准) |
| `BUILD_SCS_PERF_TESTS` | ON | single_class_set 性能测试 |
| `BUILD_MODULE_PERF_TESTS` | ON | 各模块独立性能测试 (16 个) |
| `LCF_ENABLE_LTO` | ON | 链接时优化 (LTO/IPO) |
| `LCF_MINIMAL_STACK` | 0 | 嵌入式/RTOS: 关闭栈分配, 改用堆 |

Disable an option by passing `-DBUILD_PERF_TESTS=OFF` to cmake.
通过 `-DBUILD_PERF_TESTS=OFF` 等参数禁用对应构建项.

---

## Usage / 用法

Copy the `include/` folder into your project, then `#include "include/component.hpp"`.
Detailed examples: [usage.md](usage.md) and [usagec.cpp](usagec.cpp).

复制 `include/` 文件夹到项目中, 然后 `#include "include/component.hpp"`.
详细示例见 [usage.md](usage.md) 与 [usagec.cpp](usagec.cpp).

---

## License

MIT License. See [LICENSE](LICENSE).
