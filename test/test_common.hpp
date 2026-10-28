#pragma once

// test_common.hpp - 统一测试框架 (格式化输出 / 断言 / 计时 / 缓存报告)
//   3 类测试共用: 模块功能测试 / 高层封装测试 / ECS 性能测试
//   约定: 性能输出以百万次每秒 (Mops/s) 为单位

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include "include/part/time/timer.hpp"
#include "include/part/operating_message.hpp"
#include "include/part/analysis.hpp"

// Windows 控制台默认代码页 (GBK/936) 会把 UTF-8 中文输出显示为乱码,
// 启动时切到 UTF-8 (65001)。非 Windows 平台为空操作。
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

inline void t_console_utf8() noexcept
{
#if defined(_WIN32)
    static bool done = false;
    if (!done)
    {
        SetConsoleOutputCP(65001);
        SetConsoleCP(65001);
        done = true;
    }
#endif
}

// 不关心失败原因的调用点共用同一个错误对象；按线程隔离，避免并发测试互相覆盖
inline operating_message& scratch_error() noexcept
{
    static thread_local operating_message message;
    return message;
}

// ==== 全局统计 (单线程测试) ====
struct t_stats
{
    static inline size_t total = 0;
    static inline size_t pass = 0;
    static inline size_t fail = 0;
};

// ==== 显示宽度: ASCII 记 1 列, 其余 (含 CJK) 记 2 列 ====
inline size_t t_disp_width(const char* s) noexcept
{
    size_t w = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (*p)
    {
        unsigned char c = *p;
        if (c < 0x80)
        {
            ++w;
            ++p;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            w += 1;
            p += 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            w += 2;
            p += 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            w += 2;
            p += 4;
        }
        else
        {
            ++p;
        }
    }
    return w;
}

// 左对齐填充到 width 显示列
inline std::string t_pad(const char* s, size_t width) noexcept
{
    size_t w = t_disp_width(s);
    std::string out(s);
    if (w < width)
    {
        out.append(width - w, ' ');
    }
    return out;
}

inline void t_rule(char ch, size_t n) noexcept
{
    std::string line(n, ch);
    std::printf("%s\n", line.c_str());
}

// ==== 套件头 ====
inline void t_begin(const char* suite) noexcept
{
    // 关闭输出缓冲: 崩溃时保留已打印的分节信息, 便于定位挂在哪一节
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    t_console_utf8();
    std::printf("\n");
    t_rule('=', 64);
    std::printf("  %s\n", suite);
    t_rule('=', 64);
}

// ==== 节 / 子节 ====
inline void t_section(size_t idx, const char* title) noexcept
{
    std::printf("\n  [%zu] %s\n", idx, title);
}

inline void t_sub(const char* title) noexcept
{
    std::printf("\n    -- %s --\n", title);
}

// ==== 断言 ====
inline bool t_check(const char* name, bool cond) noexcept
{
    ++t_stats::total;
    if (cond)
    {
        ++t_stats::pass;
    }
    else
    {
        ++t_stats::fail;
    }
    std::string line = t_pad(name, 42);
    std::printf("    %s : %s\n", line.c_str(), cond ? "通过" : "失败");
    return cond;
}

template <typename A, typename B>
inline bool t_check_eq(const char* name, const A& a, const B& b) noexcept
{
    return t_check(name, a == b);
}

// ==== 计时 (多轮取最优) ====
template <typename F>
inline double t_bench_best_ms(F&& fn, size_t repeats = 5) noexcept
{
    double best = 1e18;
    for (size_t i = 0; i < repeats; ++i)
    {
        timer t;
        fn();
        double ms = t.elapsed_milliseconds();
        if (ms < best)
        {
            best = ms;
        }
    }
    return best;
}

// 每轮前执行 prep 重建初始状态, 计时只覆盖 run
template <typename Prep, typename Run>
    requires std::invocable<Prep&> && std::invocable<Run&>
inline double t_bench_best_ms(Prep&& prep, Run&& run, size_t repeats = 5) noexcept
{
    double best = 1e18;
    for (size_t i = 0; i < repeats; ++i)
    {
        prep();
        timer t;
        run();
        double ms = t.elapsed_milliseconds();
        if (ms < best)
        {
            best = ms;
        }
    }
    return best;
}

// 性能项: ops 为操作次数, ms 为耗时毫秒 (百万次/秒 + 单次纳秒)
inline void t_perf(const char* name, uint64_t ops, double ms) noexcept
{
    double mops = (ms > 0.0) ? static_cast<double>(ops) / ms / 1000.0 : 0.0;
    double ns = (ops > 0) ? ms * 1e6 / static_cast<double>(ops) : 0.0;
    std::string line = t_pad(name, 42);
    std::printf("    %s : %10.3f Mops/s  %8.3f ns/op\n", line.c_str(), mops, ns);
}

template <typename F>
inline void t_bench(const char* name, uint64_t ops, F&& fn, size_t repeats = 5) noexcept
{
    double ms = t_bench_best_ms(std::forward<F>(fn), repeats);
    t_perf(name, ops, ms);
}

// ==== 吞吐与内存 (MB / ns 基础单位) ====

// 吞吐项: 处理 bytes 字节数据、elems 个元素耗时 ms → MB/s + ns/元素
inline void t_throughput(const char* name, size_t bytes, uint64_t elems, double ms) noexcept
{
    double mbs = (ms > 0.0)
        ? (static_cast<double>(bytes) / (ms / 1000.0)) / (1024.0 * 1024.0)
        : 0.0;
    double ns = (elems > 0) ? ms * 1e6 / static_cast<double>(elems) : 0.0;
    std::string line = t_pad(name, 42);
    std::printf("    %s : %9.2f MB/s  %10.2f ns/unit\n", line.c_str(), mbs, ns);
}

template <typename F>
inline void t_throughput_bench(const char* name, size_t bytes, uint64_t elems,
                               F&& fn, size_t repeats = 5) noexcept
{
    double ms = t_bench_best_ms(std::forward<F>(fn), repeats);
    t_throughput(name, bytes, elems, ms);
}

// 内存占用项 (MB)
inline void t_mem(const char* name, size_t bytes) noexcept
{
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::string line = t_pad(name, 42);
    std::printf("    %s : %9.2f MB\n", line.c_str(), mb);
}

// 延迟项 (纯 ns, 用于无法按吞吐衡量的操作)
inline void t_latency(const char* name, uint64_t ops, double ms) noexcept
{
    double ns = (ops > 0) ? ms * 1e6 / static_cast<double>(ops) : 0.0;
    std::string line = t_pad(name, 42);
    std::printf("    %s : %10.2f ns/op\n", line.c_str(), ns);
}

template <typename F>
inline void t_latency_bench(const char* name, uint64_t ops, F&& fn, size_t repeats = 5) noexcept
{
    double ms = t_bench_best_ms(std::forward<F>(fn), repeats);
    t_latency(name, ops, ms);
}

// ==== 缓存命中报告 (软件侧时序分类) ====
inline analyzer& t_analyzer() noexcept
{
    static analyzer an;
    return an;
}

inline void t_cache(const char* name, const analyzer::cache_report& r) noexcept
{
    std::string line = t_pad(name, 42);
    std::printf("    %s : L1 %5.1f%%  L2 %5.1f%%  L3 %5.1f%%  Miss %5.1f%%  avg %6.2f cyc\n",
                line.c_str(),
                r.l1_hit_rate * 100.0, r.l2_hit_rate * 100.0,
                r.l3_hit_rate * 100.0, r.miss_rate * 100.0, r.avg_cycles);
}

// 顺序访问扫描 (base 起, count 个元素, 元素间隔 stride 字节)
inline void t_cache_scan(const char* name, const void* base,
                         size_t count, size_t stride) noexcept
{
    analyzer::cache_report r = t_analyzer().measure_hits(base, count, stride);
    t_cache(name, r);
}

// ==== 汇总 (返回值即进程退出码) ====
inline int t_end(const char* label) noexcept
{
    std::printf("\n");
    t_rule('-', 64);
    std::printf("  %s汇总: 通过 %zu / 失败 %zu / 总计 %zu  [%s]\n",
                label, t_stats::pass, t_stats::fail, t_stats::total,
                t_stats::fail == 0 ? "全部通过" : "存在失败");
    t_rule('-', 64);
    std::printf("\n");
    return static_cast<int>(t_stats::fail);
}
