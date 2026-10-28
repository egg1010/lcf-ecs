#pragma once

// force_inline.hpp - 跨平台编译器属性宏集中定义
// 所有平台分支 (#if defined(__GNUC__)/__clang__/_MSC_VER) 集中在此,
// 各模块只 #include 本头文件使用宏, 不再内联 #if 条件编译块.
// 支持: GCC / Clang / MSVC (x86/x64/ARM64)

// 内联控制
#ifndef FORCE_INLINE
#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE inline __attribute__((always_inline))
#else
#define FORCE_INLINE inline
#endif
#endif

#ifndef NOINLINE
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif
#endif

// 函数属性
// pure: 无副作用, 结果仅依赖参数 (MSVC 无等价, 空)
#if defined(__GNUC__) || defined(__clang__)
#define LCF_PURE [[gnu::pure]]
#else
#define LCF_PURE
#endif

// flatten: 内联被调用函数 (MSVC 无等价, 空)
#if defined(__GNUC__) || defined(__clang__)
#define LCF_FLATTEN [[gnu::flatten]]
#else
#define LCF_FLATTEN
#endif

// restrict 限定符: __restrict 在 MSVC/GCC/Clang 均被识别 (C++ 扩展)
#define LCF_RESTRICT __restrict

// 不可达路径: MSVC 用 __assume(0), GCC/Clang 用 __builtin_unreachable()
#if defined(_MSC_VER) && !defined(__clang__)
#define LCF_UNREACHABLE() __assume(0)
#else
#define LCF_UNREACHABLE() __builtin_unreachable()
#endif

// 确定性终止: 防御性检查用; LCF_UNREACHABLE 仅优化提示, 不产生实际终止代码
#if defined(_MSC_VER) && !defined(__clang__)
#define LCF_TRAP() __debugbreak()
#else
#define LCF_TRAP() __builtin_trap()
#endif

// 函数级 AVX2 定向: GCC/Clang 用 gnu::target 激活定向函数;
// MSVC 忽略 (intrinsics 不受 /arch 限制, 由运行时检测守卫调用)
#if defined(_MSC_VER) && !defined(__clang__)
#define LCF_TARGET_AVX2
#else
#define LCF_TARGET_AVX2 [[gnu::target("avx2")]]
#endif

// 预取指令
#if defined(__GNUC__) || defined(__clang__)
#define LCF_PREFETCH_R(ptr) __builtin_prefetch(ptr, 0, 3)   // 读, L1 (高局部性)
#define LCF_PREFETCH_NTA(ptr) __builtin_prefetch(ptr, 0, 0)  // 读, 非临时 (不污染缓存)
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define LCF_PREFETCH_R(ptr) _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0)
#define LCF_PREFETCH_NTA(ptr) _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_NTA)
#elif defined(_MSC_VER)
#include <intrin.h>
#define LCF_PREFETCH_R(ptr) __prefetch(ptr)
#define LCF_PREFETCH_NTA(ptr) __prefetch(ptr)
#else
#define LCF_PREFETCH_R(ptr) ((void)0)
#define LCF_PREFETCH_NTA(ptr) ((void)0)
#endif

// 历史别名: 各模块原用名 (PREFETCH_R / DENSE_*) 统一映射到 LCF_ 前缀,
// 避免大量使用点改动. 新代码应直接用 LCF_ 前缀宏.
#ifndef PREFETCH_R
#define PREFETCH_R(ptr) LCF_PREFETCH_R(ptr)
#endif
#ifndef PREFETCH_NTA
#define PREFETCH_NTA(ptr) LCF_PREFETCH_NTA(ptr)
#endif
#ifndef DENSE_PREFETCH_R
#define DENSE_PREFETCH_R(ptr) LCF_PREFETCH_R(ptr)
#endif
#ifndef DENSE_ALWAYS_INLINE
#define DENSE_ALWAYS_INLINE FORCE_INLINE
#endif
#ifndef DENSE_FLATTEN
#define DENSE_FLATTEN LCF_FLATTEN
#endif
#ifndef DENSE_RESTRICT
#define DENSE_RESTRICT LCF_RESTRICT
#endif

// 编译器假设: 告知优化器条件恒真, 用于边界检查之后消去冗余分支
#if defined(_MSC_VER) && !defined(__clang__)
#define LCF_ASSUME(cond) __assume(cond)
#elif defined(__clang__)
#define LCF_ASSUME(cond) __builtin_assume(cond)
#elif defined(__GNUC__)
#define LCF_ASSUME(cond) do { if (!(cond)) { __builtin_unreachable(); } } while (false)
#else
#define LCF_ASSUME(cond) ((void)0)
#endif

// 空成员优化: MSVC 需显式命名空间属性, 标准写法会被静默忽略
#if defined(_MSC_VER) && !defined(__clang__)
#define LCF_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define LCF_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

// 内存栅栏: MSVC 用 intrin, GCC/Clang 用内联汇编
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define LCF_COMPILER_FENCE() _ReadWriteBarrier()
#define LCF_MEMORY_FENCE() _mm_mfence()
#define LCF_LOAD_FENCE() _mm_lfence()
#define LCF_STORE_FENCE() _mm_sfence()
#elif defined(__GNUC__) || defined(__clang__)
#define LCF_COMPILER_FENCE() __asm__ __volatile__("" ::: "memory")
#define LCF_MEMORY_FENCE() __asm__ __volatile__("mfence" ::: "memory")
#define LCF_LOAD_FENCE() __asm__ __volatile__("lfence" ::: "memory")
#define LCF_STORE_FENCE() __asm__ __volatile__("sfence" ::: "memory")
#else
#define LCF_COMPILER_FENCE() ((void)0)
#define LCF_MEMORY_FENCE() ((void)0)
#define LCF_LOAD_FENCE() ((void)0)
#define LCF_STORE_FENCE() ((void)0)
#endif

// 运行时 AVX2 检测: MSVC 走 CPUID + XGETBV, GCC/Clang 用 __builtin_cpu_supports
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
[[nodiscard]] inline bool lcf_cpu_has_avx2() noexcept
{
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 1);
    const bool osxsave = (info[2] & (1 << 27)) != 0;
    const bool avx = (info[2] & (1 << 28)) != 0;
    if (!osxsave || !avx)
    {
        return false;
    }
    if ((_xgetbv(0) & 0x6) != 0x6)
    {
        return false;
    }
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
}
#define LCF_CPU_HAS_AVX2() (::lcf_cpu_has_avx2())
#elif defined(__GNUC__) || defined(__clang__)
#define LCF_CPU_HAS_AVX2() (__builtin_cpu_supports("avx2") != 0)
#else
#define LCF_CPU_HAS_AVX2() false
#endif
