// perf_common.hpp - 独立性能测试共享工具
// 跨编译器: GCC/Clang/MSVC
#pragma once

#include "include/part/time.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <numeric>
#include <algorithm>
#include <random>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// 控制台 UTF-8 输出 (解决中文乱码)
namespace lcf_perf_detail {
    struct console_utf8_init {
        console_utf8_init() noexcept { SetConsoleOutputCP(CP_UTF8); }
    };
    inline console_utf8_init g_console_utf8_inst;
}
#endif

// 编译器屏障宏
#if defined(_MSC_VER)
    #include <intrin.h>
    #define PERF_READ_WRITE_BARRIER() _ReadWriteBarrier()
#else
    #define PERF_READ_WRITE_BARRIER() asm volatile("" : : : "memory")
#endif

// opaque: 使值对编译器不透明, 阻止常量折叠和死代码消除
#if defined(_MSC_VER)
    // MSVC x64 无 inline asm, 用 volatile + 屏障
    template <typename T>
    inline T opaque(T v) noexcept
    {
        volatile T sink = v;
        _ReadWriteBarrier();
        T result = sink;
        return result;
    }
#else
    // GCC/Clang: asm "+r" 寄存器往返, 零开销
    template <typename T>
    inline T opaque(T v) noexcept
    {
        asm volatile("" : "+r"(v) : :);
        return v;
    }
#endif

// touch_ptr: 强制读取指针首字节, 防止返回值被消除
inline void touch_ptr(const void* p) noexcept
{
    if (p) [[likely]]
    {
        volatile uint8_t v = *static_cast<const volatile uint8_t*>(p);
        (void)v;
    }
}

// compiler_barrier: 阻止跨屏障重排
inline void compiler_barrier() noexcept
{
    PERF_READ_WRITE_BARRIER();
}

// 多次重复取最小值 (减少单次测量的调度噪声)
template <typename F>
double best_ns(int repeat, F&& fn) noexcept
{
    double best = 1e18;
    for (int r = 0; r < repeat; ++r)
    {
        timer t;
        fn();
        double ns = t.elapsed_ns();
        if (ns < best) best = ns;
    }
    return best;
}

// 单次测量的周期数 (用于 sub-ns 级测量)
// 使用 cycle_timer 直接测一次, 不走 benchmark_cycles 的多次平均
inline double measure_cycles_once() noexcept
{
    cycle_timer ct;
    ct.reset();
    return static_cast<double>(ct.elapsed_cycles());
}

// 多次重复取最小值 (周期版本, 用 cycle_timer 的单次迭代)
template <typename F>
double best_cycles(int repeat, F&& fn) noexcept
{
    double best = 1e18;
    for (int r = 0; r < repeat; ++r)
    {
        cycle_timer ct;
        ct.reset();
        fn();
        double c = static_cast<double>(ct.elapsed_cycles());
        if (c < best) best = c;
    }
    return best;
}

// === 格式化辅助 ===
inline void print_header(const char* title) noexcept
{
    std::cout << "\n┌─ " << title << "\n";
}

inline void print_footer() noexcept
{
    std::cout << "└──────────────────────────────────────────────\n";
}

// 单次操作纳秒 + 吞吐量 (亿/s)
inline void print_ns(const char* label, size_t n, double ns) noexcept
{
    double throughput = (ns > 0 && n > 0) ? static_cast<double>(n) / ns : 0;
    std::cout << "  " << std::left << std::setw(36) << label
              << " | " << std::right << std::setw(10) << n << " 次"
              << " | " << std::fixed << std::setprecision(3) << std::setw(8) << ns << " ns"
              << " | " << std::setprecision(2) << std::setw(8) << throughput << " 亿/s\n";
}

// 统计分布输出 (min/p50/p95/p99/max + mean)
inline void print_dist(const char* label, const stats& s, const char* unit = "ns") noexcept
{
    std::cout << "  " << std::left << std::setw(36) << label
              << " | n=" << std::right << std::setw(8) << s.count
              << " | min=" << std::fixed << std::setprecision(3) << std::setw(7) << s.min
              << " p50=" << std::setw(7) << s.p50
              << " p95=" << std::setw(7) << s.p95
              << " p99=" << std::setw(7) << s.p99
              << " max=" << std::setw(7) << s.max
              << " mean=" << std::setw(7) << s.mean << " " << unit << "\n";
}

// 测试组件 (覆盖 4B/12B/32B 三个量级, 全部 trivially copyable)
struct POD4  { uint32_t v; };
struct POD12 { float x, y, z; };
struct POD32 { float a[8]; };

// 模块汇总输出
inline void print_module_summary(const char* module_name, size_t op_count,
                                  double min_ns, double max_ns, double sum_ns) noexcept
{
    double avg = (op_count > 0) ? sum_ns / static_cast<double>(op_count) : 0;
    std::cout << "\n└─ [" << module_name << "] 模块汇总: " << op_count << " 项"
              << " | 延迟范围 " << std::fixed << std::setprecision(3)
              << min_ns << "~" << max_ns << " ns"
              << " | 均值 " << avg << " ns\n";
}
