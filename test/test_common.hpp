// test_common.hpp - 测试公共头文件
// 功能测试与性能测试共享: 头文件/组件定义/Timer/格式化输出/统计汇总
#pragma once

#include "include/component.hpp"
#include "include/part/void_any.hpp"
#include "include/part/memory_pool.hpp"
#include "include/part/class_pool.hpp"
#include "include/part/arena_allocator.hpp"
#include "include/part/slab_allocator.hpp"
#include "include/part/layered_allocator.hpp"
#include "include/part/time.hpp"
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <functional>
#include <vector>
#include <array>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// 控制台 UTF-8 输出 (解决中文乱码)
namespace lcf_test_detail {
    struct console_utf8_init {
        console_utf8_init() noexcept { SetConsoleOutputCP(CP_UTF8); }
    };
    inline console_utf8_init g_console_utf8_inst;
}
#endif

using ecs::entity;
using ecs::entity_manager;
using ecs::sparse_entry;
using ecs::single_class_set;

// === 测试组件定义 ===
struct Position {
    float x, y, z;
    Position(float x = 0.0f, float y = 0.0f, float z = 0.0f) : x(x), y(y), z(z) {}
};
struct Velocity {
    float vx, vy, vz;
    Velocity(float vx = 0.0f, float vy = 0.0f, float vz = 0.0f) : vx(vx), vy(vy), vz(vz) {}
};
struct Health {
    int current, max;
    Health(int current = 100, int max = 100) : current(current), max(max) {}
};
struct Name {
    std::string value;
    Name(const std::string& name = "Entity") : value(name) {}
};
struct Damage {
    int amount;
    Damage(int amount = 0) : amount(amount) {}
};
struct Armor {
    int defense;
    Armor(int defense = 0) : defense(defense) {}
};
struct Speed {
    float value;
    Speed(float value = 0.0f) : value(value) {}
};
struct Rotation {
    float x, y, z, w;
    Rotation(float x = 0.0f, float y = 0.0f, float z = 0.0f, float w = 1.0f) : x(x), y(y), z(z), w(w) {}
};
struct Scale {
    float x, y, z;
    Scale(float x = 1.0f, float y = 1.0f, float z = 1.0f) : x(x), y(y), z(z) {}
};
struct Mass {
    float value;
    Mass(float value = 1.0f) : value(value) {}
};

// === 辅助工具 (timer 来自 time.hpp, 全局命名空间) ===

// 模块性能统计 (用于模块汇总与异常检查)
namespace test_stats {
    inline size_t g_pass_count = 0;
    inline size_t g_fail_count = 0;
    inline size_t g_perf_count = 0;

    // 当前模块统计
    struct module_stat
    {
        const char* name = "";
        size_t op_count = 0;       // 该模块测试项数
        double min_latency_ns = 1e18;  // 单次延迟最小值
        double max_latency_ns = 0;     // 单次延迟最大值
        double sum_latency_ns = 0;     // 单次延迟累加
        char fastest_op[64] = {};      // 最快操作名 (固定缓冲, 避免悬空指针)
        char slowest_op[64] = {};      // 最慢操作名
    };
    inline module_stat g_current_module;
    inline size_t g_module_count = 0;
}

static constexpr int COL1 = 42;  // 接口名列宽
static constexpr int COL2 = 12;  // 结果列宽

void print_section(int num, const char* title) {
    // 模块结束时输出上一模块汇总
    if (test_stats::g_module_count > 0 && test_stats::g_current_module.op_count > 0)
    {
        auto& m = test_stats::g_current_module;
        double avg = m.sum_latency_ns / static_cast<double>(m.op_count);
        std::cout << "  └─ 模块汇总: " << m.op_count << " 项"
                  << " | 延迟范围 " << std::fixed << std::setprecision(2)
                  << m.min_latency_ns << "~" << m.max_latency_ns << " ns"
                  << " | 均值 " << avg << " ns"
                  << " | 最快[" << m.fastest_op << "]"
                  << " | 最慢[" << m.slowest_op << "]\n";
    }
    test_stats::g_current_module = {};
    test_stats::g_current_module.name = title;
    ++test_stats::g_module_count;
    std::cout << "\n" << std::string(56, '=') << "\n"
              << "  " << num << ". " << title << "\n"
              << std::string(56, '=') << "\n";
}

void print_sub(const char* title) {
    std::cout << "\n  [ " << title << " ]\n";
}

void print_item(const char* name, const std::string& result) {
    std::cout << "  " << std::left << std::setw(COL1) << name
              << ": " << std::right << result << "\n";
}

void print_item(const char* name, bool pass) {
    test_stats::g_pass_count += pass ? 1 : 0;
    test_stats::g_fail_count += pass ? 0 : 1;
    std::cout << "  " << std::left << std::setw(COL1) << name
              << ": " << (pass ? "通过" : "失败") << "\n";
}

void print_item(const char* name, const char* result) {
    std::cout << "  " << std::left << std::setw(COL1) << name
              << ": " << result << "\n";
}

NOINLINE void print_perf(const std::string& op, size_t count, double ms) {
    ++test_stats::g_perf_count;

    // 单次延迟 (纳秒) 与吞吐量 (次/秒)
    double latency_ns = 0;
    double throughput = 0;
    if (ms > 0)
    {
        latency_ns = (ms * 1e6) / static_cast<double>(count);
        throughput = static_cast<double>(count) / (ms / 1000.0);
    }

    // 更新当前模块统计
    {
        auto& m = test_stats::g_current_module;
        ++m.op_count;
        m.sum_latency_ns += latency_ns;
        if (latency_ns < m.min_latency_ns)
        {
            m.min_latency_ns = latency_ns;
            std::strncpy(m.fastest_op, op.c_str(), 63);
            m.fastest_op[63] = '\0';
        }
        if (latency_ns > m.max_latency_ns)
        {
            m.max_latency_ns = latency_ns;
            std::strncpy(m.slowest_op, op.c_str(), 63);
            m.slowest_op[63] = '\0';
        }
    }

    // 吞吐量中文格式化 (万次/秒)
    auto fmt_throughput = [](double tps) -> std::string
    {
        if (tps >= 1e8) { return std::to_string(tps / 1e8).substr(0, 5) + "亿/s"; }
        if (tps >= 1e4) { return std::to_string(tps / 1e4).substr(0, 6) + "万/s"; }
        return std::to_string(static_cast<size_t>(tps)) + "/s";
    };

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  " << std::left << std::setw(34) << op
              << " | " << std::right << std::setw(10) << count << " 次"
              << " | 耗时 " << std::setw(9) << ms << " ms"
              << " | 单次 " << std::setw(9) << latency_ns << " ns"
              << " | " << std::setw(8) << fmt_throughput(throughput)
              << "\n";
}

void print_perf_sub(const char* title) {
    std::cout << "\n  ┌─ " << title << "\n";
}

void print_perf_sep() {
    std::cout << "  ├──────────────────────────────────────────\n";
}

// lcf_sink - 基准测试防优化 sink (替代 volatile 变量)
// 作用: 强制编译器认为 expr 的结果被使用, 不能优化掉被测代码.
// 实现: 通过易失引用读取到非 volatile 局部变量, 再丢弃:
//   - 无未使用变量警告 (函数调用, 参数被使用)
//   - 无副作用被优化 (volatile 读取是真实内存访问, 不可省略)
//   - 无 GCC 14+ "conversion to void will not access volatile object" 告警
//     (因为 (void) 作用于非 volatile 的 tmp, 而非 volatile 引用本身)
// 用法: lcf_sink(value); 替代 volatile T x = value;
template <typename T>
inline void lcf_sink(T&& v) noexcept {
    volatile auto& ref = v;
    auto tmp = ref;
    (void)tmp;
}

// 复合 sink: 同时 sink 多个值, 用于一次调用多个返回值
template <typename... Ts>
inline void lcf_sink_all(Ts&&... vs) noexcept {
    (lcf_sink(std::forward<Ts>(vs)), ...);
}

// === 缓存命中率详细输出 (基于 time.hpp) - 中文格式 ===
void print_cache_report(const char* name, const cache_report& r) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  " << std::left << std::setw(28) << name
              << " | 均值 " << std::setw(6) << r.avg_cycles << " 周期"
              << " | p50 " << std::setw(6) << r.p50_cycles
              << " | p95 " << std::setw(6) << r.p95_cycles
              << " | p99 " << std::setw(6) << r.p99_cycles
              << "\n    命中分布(" << r.active_levels << "级): L1 " << std::setw(5) << (r.l1_hit_rate * 100) << "%";
    if (r.active_levels >= 2)
    {
        std::cout << " | L2 " << std::setw(5) << (r.l2_hit_rate * 100) << "%";
    }
    if (r.active_levels >= 3)
    {
        std::cout << " | L3 " << std::setw(5) << (r.l3_hit_rate * 100) << "%";
    }
    std::cout << " | 未命中 " << std::setw(5) << (r.miss_rate * 100) << "%\n";
}

// 批量缓存测量输出 (精确平均延迟) - 中文格式
void print_cache_batch(const char* name, const batch_cache_result& r) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  " << std::left << std::setw(28) << name
              << " | 平均 " << std::setw(8) << r.avg_cycles_per_access << " 周期/次"
              << " | 净值 " << std::setw(8) << r.net_cycles_per_access << " 周期/次"
              << " | 基线 " << std::setw(8) << r.baseline_cycles << "\n";
}

// 统计分布输出 (min/mean/p50/p95/p99/max) - 中文格式
void print_stats(const char* name, const stats& s, const char* unit = "ns") {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  " << std::left << std::setw(28) << name
              << " | 最小 " << std::setw(8) << s.min
              << " | 均值 " << std::setw(8) << s.mean
              << " | p50 " << std::setw(8) << s.p50
              << " | p95 " << std::setw(8) << s.p95
              << " | p99 " << std::setw(8) << s.p99
              << " | 最大 " << std::setw(8) << s.max
              << " " << unit << " (n=" << s.count << ")\n";
}

// >64 组件类型双轨测试用
template<size_t N>
struct ExtraComp {
    int v{static_cast<int>(N)};
    ExtraComp(int v = 0) : v(v) {}
};

// === 测试结果汇总输出 (type: "功能测试" 或 "性能测试") ===
inline void print_summary(const char* type) {
    // 输出最后一个模块的汇总
    if (test_stats::g_module_count > 0 && test_stats::g_current_module.op_count > 0)
    {
        auto& m = test_stats::g_current_module;
        double avg = m.sum_latency_ns / static_cast<double>(m.op_count);
        std::cout << "  └─ 模块汇总: " << m.op_count << " 项"
                  << " | 延迟范围 " << std::fixed << std::setprecision(2)
                  << m.min_latency_ns << "~" << m.max_latency_ns << " ns"
                  << " | 均值 " << avg << " ns"
                  << " | 最快[" << m.fastest_op << "]"
                  << " | 最慢[" << m.slowest_op << "]\n";
    }

    std::cout << "\n" << std::string(56, '=') << "\n";
    std::cout << "  " << type << " 汇总\n";
    std::cout << std::string(56, '=') << "\n";
    if (test_stats::g_perf_count > 0)
    {
        std::cout << "  性能基准项: " << test_stats::g_perf_count << "\n";
        std::cout << "  模块总数:   " << test_stats::g_module_count << "\n";
    }
    if (test_stats::g_pass_count + test_stats::g_fail_count > 0)
    {
        size_t total = test_stats::g_pass_count + test_stats::g_fail_count;
        std::cout << "  功能测试项: 通过 " << test_stats::g_pass_count
                  << " / 失败 " << test_stats::g_fail_count
                  << " / 总计 " << total << "\n";
        std::cout << "  结果: " << (test_stats::g_fail_count == 0 ? "全部通过" : "存在失败") << "\n";
    }
    std::cout << std::string(56, '=') << "\n";
}
