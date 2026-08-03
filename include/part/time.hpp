#pragma once

// 时间测量工具 (仅时间相关操作)
// 墙钟 + CPU 周期 / 配对计时 / RAII 作用域计时 / 统计 / 基准测试
// 设计原则: 精度第一, 性能第二; 不提供 CPU 频率/缓存/屏障/异常检测

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <new>
#include "tiered_sort.hpp"
#include "force_inline.hpp"
#include "dense.hpp"

// =============================================================================
// L0: CPU 周期读取原语 (x86/x64 rdtsc)
// =============================================================================

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    #define TIME_HAS_RDTSC 1
    #if defined(_MSC_VER)
        #include <intrin.h>
        FORCE_INLINE uint64_t rdtsc() noexcept
        {
            return __rdtsc();
        }
        FORCE_INLINE uint64_t rdtscp() noexcept
        {
            unsigned aux;
            return __rdtscp(&aux);
        }
    #elif defined(__GNUC__) || defined(__clang__)
        FORCE_INLINE uint64_t rdtsc() noexcept
        {
            uint32_t lo, hi;
            __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
            return (static_cast<uint64_t>(hi) << 32) | lo;
        }
        FORCE_INLINE uint64_t rdtscp() noexcept
        {
            uint32_t lo, hi, aux;
            __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
            return (static_cast<uint64_t>(hi) << 32) | lo;
        }
    #endif
#else
    #define TIME_HAS_RDTSC 0
    FORCE_INLINE uint64_t rdtsc() noexcept { return 0; }
    FORCE_INLINE uint64_t rdtscp() noexcept { return 0; }
#endif

// =============================================================================
// L1: 统一计时器 stopwatch (同时记录墙钟 + 周期, 精度优先)
// 构造时双 now(): 先 rdtscp (快, 序列化指令流, 干净起点), 后 steady_clock (慢, 单调)
// 成员声明顺序决定初始化顺序: cstart_ 先, wstart_ 后
// =============================================================================

// 终点快照 (一次性返回墙钟 + 周期, 避免多次 now() 调用引入额外开销)
struct time_snapshot
{
    double ns_val;
    uint64_t cycles;
};

class stopwatch
{
    using clock = std::chrono::steady_clock;
    uint64_t cstart_;            // 先声明: rdtscp 序列化, 确保起点干净
    clock::time_point wstart_;   // 后声明: steady_clock 单调, 绝对时间

public:
    stopwatch() noexcept
        : cstart_(rdtscp()), wstart_(clock::now()) {}

    void reset() noexcept
    {
        cstart_ = rdtscp();
        wstart_ = clock::now();
    }

    // 墙钟 (绝对时间, 单调递增, 适合日志/阈值判断)
    [[nodiscard]] double ns() const noexcept
    {
        return std::chrono::duration<double, std::nano>(clock::now() - wstart_).count();
    }

    [[nodiscard]] double us() const noexcept
    {
        return std::chrono::duration<double, std::micro>(clock::now() - wstart_).count();
    }

    [[nodiscard]] double ms() const noexcept
    {
        return std::chrono::duration<double, std::milli>(clock::now() - wstart_).count();
    }

    [[nodiscard]] double s() const noexcept
    {
        return std::chrono::duration<double>(clock::now() - wstart_).count();
    }

    // CPU 周期 (相对比较, 精度最高, 适合基准测试)
    [[nodiscard]] uint64_t cycles() const noexcept
    {
        return rdtscp() - cstart_;
    }

    // 一次性快照 (精度优先): 终点先 rdtscp (精确), 后 clock::now
    // cycles 不含 clock::now 开销; ns 含 rdtscp 开销 (固定, 相对比较可抵消)
    [[nodiscard]] time_snapshot snapshot() const noexcept
    {
        uint64_t cend = rdtscp();
        double ns_val = std::chrono::duration<double, std::nano>(clock::now() - wstart_).count();
        return {ns_val, cend - cstart_};
    }
};

// =============================================================================
// L2: 配对计时 (零样板, 单次调用返回时长)
// =============================================================================

// 墙钟配对计时: 返回纳秒
template<typename F>
[[nodiscard]] double measure_ns(F&& fn) noexcept
{
    auto t0 = std::chrono::steady_clock::now();
    fn();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count();
}

// 周期配对计时: 返回周期数 (非 x86 返回 0)
template<typename F>
[[nodiscard]] uint64_t measure_cycles(F&& fn) noexcept
{
#if TIME_HAS_RDTSC
    uint64_t c0 = rdtscp();
    fn();
    uint64_t c1 = rdtscp();
    return c1 - c0;
#else
    fn();
    return 0;
#endif
}

// =============================================================================
// L3: 耗时自动格式化 (自动选单位: ns/us/ms/s)
// =============================================================================

// 将纳秒值格式化为可读字符串, 保留 2-3 位有效数字
// 例: 123.4ns / 1.23us / 12.34ms / 1.50s
inline void format_duration(double ns_val, char* buf, size_t cap) noexcept
{
    if (ns_val < 1000.0)
    {
        std::snprintf(buf, cap, "%.1fns", ns_val);
    }
    else if (ns_val < 1000000.0)
    {
        std::snprintf(buf, cap, "%.2fus", ns_val / 1000.0);
    }
    else if (ns_val < 1000000000.0)
    {
        std::snprintf(buf, cap, "%.2fms", ns_val / 1000000.0);
    }
    else
    {
        std::snprintf(buf, cap, "%.2fs", ns_val / 1000000000.0);
    }
}

// =============================================================================
// L4: RAII 作用域计时 (默认写入全局记录, 配合 scope_report 查看)
// =============================================================================

struct scope_record_entry
{
    const char* name;
    double ns_val;
    uint64_t cycles;
};

// 全局作用域计时记录表 (非线程安全, 项目排除多线程)
[[nodiscard]] inline dense<scope_record_entry>& scope_records() noexcept
{
    static dense<scope_record_entry> r;
    return r;
}

inline void scope_clear() noexcept
{
    scope_records().clear();
}

// RAII 作用域计时器: 析构时记录 name + 耗时到全局表
struct scope_time
{
    const char* name_;
    stopwatch sw_;

    explicit scope_time(const char* name) noexcept
        : name_(name), sw_() {}

    ~scope_time() noexcept
    {
        time_snapshot snap = sw_.snapshot();
        dense<scope_record_entry>& r = scope_records();
        r.increase_capacity(r.size() + 1);
        r.push_back({name_, snap.ns_val, snap.cycles});
    }

    scope_time(const scope_time&) = delete;
    scope_time& operator=(const scope_time&) = delete;
};

// 打印所有作用域计时记录到 stdout (格式化对齐)
inline void scope_report() noexcept
{
    const dense<scope_record_entry>& r = scope_records();
    for (size_t i = 0; i < r.size(); ++i)
    {
        char buf[32];
        format_duration(r[i].ns_val, buf, sizeof(buf));
#if TIME_HAS_RDTSC
        std::printf("  %-24s %10s  %llu cycles\n",
                    r[i].name, buf,
                    static_cast<unsigned long long>(r[i].cycles));
#else
        std::printf("  %-24s %10s\n", r[i].name, buf);
#endif
    }
}

// =============================================================================
// L5: 统计分析
// =============================================================================

struct stats
{
    double min = 0;
    double max = 0;
    double mean = 0;
    double median = 0;
    double p50 = 0;
    double p90 = 0;
    double p95 = 0;
    double p99 = 0;
    double stddev = 0;
    size_t count = 0;
};

namespace detail
{
    // 内部统计计算核心 (已排序的裸指针样本)
    inline stats compute_stats_sorted(double* p, size_t n) noexcept
    {
        stats s;
        s.count = n;
        if (n == 0)
        {
            return s;
        }
        s.min = p[0];
        s.max = p[n - 1];
        double sum = 0;
        for (size_t i = 0; i < n; ++i)
        {
            sum += p[i];
        }
        s.mean = sum / static_cast<double>(n);
        auto pct = [&](double q) noexcept -> double
        {
            if (n == 1)
            {
                return p[0];
            }
            size_t idx = static_cast<size_t>(q * static_cast<double>(n - 1));
            return p[idx];
        };
        s.median = s.p50 = pct(0.50);
        s.p90 = pct(0.90);
        s.p95 = pct(0.95);
        s.p99 = pct(0.99);
        double sq_sum = 0;
        for (size_t i = 0; i < n; ++i)
        {
            double v = p[i];
            sq_sum += (v - s.mean) * (v - s.mean);
        }
        s.stddev = std::sqrt(sq_sum / static_cast<double>(n));
        return s;
    }
}  // namespace detail

// const 引用重载: 内部拷贝排序, 不修改原样本
[[nodiscard]] inline stats compute_stats(const dense<double>& samples) noexcept
{
    size_t n = samples.size();
    if (n == 0)
    {
        return {};
    }
    dense<double> tmp;
    tmp.reserve_exact(n);
    for (size_t i = 0; i < n; ++i)
    {
        tmp.push_back(samples[i]);
    }
    double* p = tmp.data();
    sort(p, n);
    return detail::compute_stats_sorted(p, n);
}

// 移动重载: 直接排序原样本, 避免拷贝
[[nodiscard]] inline stats compute_stats(dense<double>&& samples) noexcept
{
    size_t n = samples.size();
    if (n == 0)
    {
        return {};
    }
    double* p = samples.data();
    sort(p, n);
    return detail::compute_stats_sorted(p, n);
}

// 通用类型版 (支持 uint64_t 周期数 / int 计数等)
template<typename T>
[[nodiscard]] stats compute_stats_t(const dense<T>& samples) noexcept
{
    size_t n = samples.size();
    if (n == 0)
    {
        return {};
    }
    dense<double> tmp;
    tmp.reserve_exact(n);
    for (size_t i = 0; i < n; ++i)
    {
        tmp.push_back(static_cast<double>(samples[i]));
    }
    double* p = tmp.data();
    sort(p, n);
    return detail::compute_stats_sorted(p, n);
}

// =============================================================================
// L6: P² 在线分位数估计器 (O(1) 空间, 流式监控)
// =============================================================================

class p2_quantile
{
    double q_[5] = {};
    size_t n_[5] = {};
    double ns_[5] = {};
    size_t count_ = 0;
    double p_;

public:
    explicit p2_quantile(double quantile) noexcept : p_(quantile) {}

    void add(double x) noexcept
    {
        ++count_;
        if (count_ <= 5)
        {
            q_[count_ - 1] = x;
            if (count_ == 5)
            {
                // 前 5 个值就地插入排序
                for (size_t i = 1; i < 5; ++i)
                {
                    double key = q_[i];
                    size_t j = i;
                    while (j > 0 && key < q_[j - 1])
                    {
                        q_[j] = q_[j - 1];
                        --j;
                    }
                    q_[j] = key;
                }
                n_[0] = 1; n_[1] = 2; n_[2] = 3; n_[3] = 4; n_[4] = 5;
                ns_[0] = 1; ns_[4] = 5;
                ns_[1] = 1 + 2 * p_;
                ns_[2] = 1 + 4 * p_;
                ns_[3] = 3 + 2 * p_;
            }
            return;
        }

        // 找区间 k
        int k = -1;
        if (x < q_[0]) [[unlikely]] { k = 0; }
        else if (x >= q_[4]) [[unlikely]] { k = 3; }
        else
        {
            for (int i = 0; i < 4; ++i)
            {
                if (q_[i] <= x && x < q_[i + 1])
                {
                    k = i;
                    break;
                }
            }
        }

        // 更新标记位置
        for (int i = k + 1; i < 5; ++i) { ++n_[i]; }
        ns_[0] = 1;
        ns_[1] = 1 + static_cast<double>(count_ - 1) * p_ / 2;
        ns_[2] = 1 + static_cast<double>(count_ - 1) * p_;
        ns_[3] = 1 + static_cast<double>(count_ - 1) * (1 + p_) / 2;
        ns_[4] = static_cast<double>(count_);

        // 调整中间标记 1/2/3
        for (int i = 1; i <= 3; ++i)
        {
            double d = ns_[i] - static_cast<double>(n_[i]);
            if ((d >= 1.0 && n_[i + 1] - n_[i] > 1) ||
                (d <= -1.0 && n_[i] - n_[i - 1] > 1))
            {
                int ds = (d > 0) ? 1 : -1;
                // 抛物线插值
                double qs = q_[i] + static_cast<double>(ds) /
                    static_cast<double>(n_[i + 1] - n_[i - 1]) *
                    ((static_cast<double>(n_[i] - n_[i - 1] + ds) * (q_[i + 1] - q_[i]) /
                      static_cast<double>(n_[i + 1] - n_[i])) +
                     (static_cast<double>(n_[i + 1] - n_[i] - ds) * (q_[i] - q_[i - 1]) /
                      static_cast<double>(n_[i] - n_[i - 1])));
                if (q_[i - 1] < qs && qs < q_[i + 1])
                {
                    q_[i] = qs;
                }
                else
                {
                    // 线性回退 (有符号分母处理负向移动)
                    int64_t denom = static_cast<int64_t>(n_[i + ds]) - static_cast<int64_t>(n_[i]);
                    q_[i] = q_[i] + static_cast<double>(ds) *
                        (q_[i + ds] - q_[i]) / static_cast<double>(denom);
                }
                n_[i] = static_cast<size_t>(static_cast<int64_t>(n_[i]) + ds);
            }
        }
    }

    [[nodiscard]] double estimate() const noexcept
    {
        if (count_ == 0) [[unlikely]] return 0;
        if (count_ <= 5) [[unlikely]] return q_[count_ - 1];
        return q_[2];
    }

    [[nodiscard]] size_t count() const noexcept { return count_; }

    void reset() noexcept
    {
        count_ = 0;
        for (size_t i = 0; i < 5; ++i) { q_[i] = 0; n_[i] = 0; ns_[i] = 0; }
    }
};

// =============================================================================
// L7: 基准测试 (自动模式选择 + 三级精度)
// =============================================================================

// P² 流式基准结果
struct p2_benchmark_result
{
    double p50 = 0;
    double p90 = 0;
    double p95 = 0;
    double p99 = 0;
    size_t count = 0;
};

// 统一基准结果 (墙钟 + 周期)
struct benchmark_result
{
    double ns_mean = 0;
    double ns_p50 = 0;
    double ns_p99 = 0;
    uint64_t cycles_mean = 0;
    size_t iterations = 0;
};

// 预热: 直到连续 3 次每百次迭代周期数偏差 <5% (精度优先)
template<typename F>
void warmup_until_stable(F&& fn, size_t max_iter = 10000) noexcept
{
#if TIME_HAS_RDTSC
    uint64_t prev = 0;
    size_t stable_count = 0;
    for (size_t i = 0; i < max_iter; i += 100)
    {
        uint64_t c0 = rdtscp();
        for (size_t j = 0; j < 100; ++j) { fn(); }
        uint64_t c1 = rdtscp();
        uint64_t cur = (c1 - c0) / 100;
        if (prev != 0 && cur >= prev * 0.95 && cur <= prev * 1.05)
        {
            if (++stable_count >= 3) { return; }
        }
        else
        {
            stable_count = 0;
        }
        prev = cur;
    }
#else
    for (size_t i = 0; i < max_iter; ++i) { fn(); }
#endif
}

// L1 batch 模式: 单次计时包裹整个循环 (最低开销, 高频小函数)
// 返回 mean/min/max, 无分位数 (仅 1 个样本, 精度由大 iterations 补偿)
template<typename F>
[[nodiscard]] stats benchmark_batch(size_t iterations, size_t warmup, F&& fn) noexcept
{
    for (size_t i = 0; i < warmup; ++i) { fn(); }
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iterations; ++i) { fn(); }
    auto t1 = std::chrono::steady_clock::now();
    double total_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double per_op = total_ns / static_cast<double>(iterations);
    stats s;
    s.count = iterations;
    s.min = s.max = s.mean = s.median = s.p50 = s.p90 = s.p95 = s.p99 = per_op;
    s.stddev = 0;
    return s;
}

// L2 chunked 模式: 每 chunk_size 次迭代计时一次 (P² 分位数, 中频函数)
template<typename F>
[[nodiscard]] p2_benchmark_result benchmark_chunked(
    size_t iterations, size_t chunk_size, size_t warmup, F&& fn) noexcept
{
    for (size_t i = 0; i < warmup; ++i) { fn(); }
    p2_quantile p50(0.50), p90(0.90), p95(0.95), p99(0.99);
    for (size_t i = 0; i < iterations; i += chunk_size)
    {
        size_t n = (chunk_size < iterations - i) ? chunk_size : (iterations - i);
        auto t0 = std::chrono::steady_clock::now();
        for (size_t j = 0; j < n; ++j) { fn(); }
        auto t1 = std::chrono::steady_clock::now();
        double per_op = std::chrono::duration<double, std::nano>(t1 - t0).count() /
                        static_cast<double>(n);
        p50.add(per_op); p90.add(per_op); p95.add(per_op); p99.add(per_op);
    }
    return {p50.estimate(), p90.estimate(), p95.estimate(), p99.estimate(), iterations};
}

// L3 precise 模式: 每次迭代单独计时 (全样本, 低频大函数, 精度最高)
template<typename F>
[[nodiscard]] stats benchmark_precise(size_t iterations, size_t warmup, F&& fn) noexcept
{
    for (size_t i = 0; i < warmup; ++i) { fn(); }
    dense<double> samples;
    samples.reserve_exact(iterations);
    for (size_t i = 0; i < iterations; ++i)
    {
        auto t0 = std::chrono::steady_clock::now();
        fn();
        auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return compute_stats(std::move(samples));
}

// L3 precise 周期版: 周期级精度 (仅 x86, 精度最高)
template<typename F>
[[nodiscard]] stats benchmark_precise_cycles(size_t iterations, size_t warmup, F&& fn) noexcept
{
#if TIME_HAS_RDTSC
    for (size_t i = 0; i < warmup; ++i) { fn(); }
    dense<double> samples;
    samples.reserve_exact(iterations);
    for (size_t i = 0; i < iterations; ++i)
    {
        uint64_t c0 = rdtscp();
        fn();
        uint64_t c1 = rdtscp();
        samples.push_back(static_cast<double>(c1 - c0));
    }
    return compute_stats(std::move(samples));
#else
    return benchmark_precise(iterations, warmup, std::forward<F>(fn));
#endif
}

// P² 流式基准 (墙钟, 适合超大样本/内存受限)
template<typename F>
[[nodiscard]] p2_benchmark_result benchmark_p2(size_t iterations, size_t warmup, F&& fn) noexcept
{
    for (size_t i = 0; i < warmup; ++i) { fn(); }
    p2_quantile p50(0.50), p90(0.90), p95(0.95), p99(0.99);
    for (size_t i = 0; i < iterations; ++i)
    {
        auto t0 = std::chrono::steady_clock::now();
        fn();
        auto t1 = std::chrono::steady_clock::now();
        double ns_val = std::chrono::duration<double, std::nano>(t1 - t0).count();
        p50.add(ns_val); p90.add(ns_val); p95.add(ns_val); p99.add(ns_val);
    }
    return {p50.estimate(), p90.estimate(), p95.estimate(), p99.estimate(), iterations};
}

// 自动模式基准测试: 基于单次耗时自动选择 batch/chunked/precise (精度优先)
//   < 100ns   → batch (单次计时开销占比高, 用大循环摊薄)
//   100ns~10us → chunked (P² 分位数, 平衡精度与开销)
//   > 10us    → precise (全样本, 精度最高)
template<typename F>
[[nodiscard]] benchmark_result benchmark(F&& fn, size_t iterations = 10000) noexcept
{
    // 预热 + 探测单次耗时
    warmup_until_stable(fn);
    double probe1 = measure_ns(fn);
    double probe2 = measure_ns(fn);
    double single = (probe1 < probe2) ? probe1 : probe2;

    benchmark_result result;
    result.iterations = iterations;

    if (single < 100.0)
    {
        // 高频小函数: batch 模式
        stats s = benchmark_batch(iterations, 0, fn);
        result.ns_mean = s.mean;
        result.ns_p50 = s.p50;
        result.ns_p99 = s.p99;
    }
    else if (single < 10000.0)
    {
        // 中频函数: chunked 模式
        p2_benchmark_result r = benchmark_chunked(iterations, 100, 0, fn);
        result.ns_mean = r.p50;
        result.ns_p50 = r.p50;
        result.ns_p99 = r.p99;
    }
    else
    {
        // 低频大函数: precise 模式 (限制迭代次数)
        size_t n = (iterations > 1000) ? 1000 : iterations;
        stats s = benchmark_precise(n, 0, fn);
        result.ns_mean = s.mean;
        result.ns_p50 = s.p50;
        result.ns_p99 = s.p99;
        result.iterations = n;
    }

    // 周期均值 (x86)
#if TIME_HAS_RDTSC
    uint64_t c0 = rdtscp();
    size_t cyc_iter = (single < 10000.0) ? 1000 : 100;
    for (size_t i = 0; i < cyc_iter; ++i) { fn(); }
    uint64_t c1 = rdtscp();
    result.cycles_mean = (c1 - c0) / cyc_iter;
#endif

    return result;
}

// =============================================================================
// L8: 基准测试运行器 (多场景对比, 一键报告)
// =============================================================================

class benchmark_runner
{
    struct entry
    {
        const char* name;
        benchmark_result result;
    };
    dense<entry> results_;

public:
    template<typename F>
    void run(const char* name, F&& fn, size_t iterations = 10000) noexcept
    {
        results_.increase_capacity(results_.size() + 1);
        results_.push_back({name, benchmark(std::forward<F>(fn), iterations)});
    }

    [[nodiscard]] size_t size() const noexcept { return results_.size(); }

    void clear() noexcept { results_.clear(); }

    // 打印对比表格到 stdout (对齐格式)
    void report() const noexcept
    {
        std::printf("%-20s %12s %12s %12s %12s\n",
                    "场景", "mean", "p50", "p99", "cycles");
        std::printf("%-20s %12s %12s %12s %12s\n",
                    "----", "----", "---", "---", "------");
        for (size_t i = 0; i < results_.size(); ++i)
        {
            char mean_buf[32], p50_buf[32], p99_buf[32];
            format_duration(results_[i].result.ns_mean, mean_buf, sizeof(mean_buf));
            format_duration(results_[i].result.ns_p50, p50_buf, sizeof(p50_buf));
            format_duration(results_[i].result.ns_p99, p99_buf, sizeof(p99_buf));
#if TIME_HAS_RDTSC
            std::printf("%-20s %12s %12s %12s %12llu\n",
                        results_[i].name, mean_buf, p50_buf, p99_buf,
                        static_cast<unsigned long long>(results_[i].result.cycles_mean));
#else
            std::printf("%-20s %12s %12s %12s\n",
                        results_[i].name, mean_buf, p50_buf, p99_buf);
#endif
        }
    }

    // 可指定 sink 输出 (sink 接收格式化后的行)
    template<typename Sink>
    void report_to(Sink&& sink) const noexcept
    {
        for (size_t i = 0; i < results_.size(); ++i)
        {
            char mean_buf[32], p50_buf[32], p99_buf[32];
            format_duration(results_[i].result.ns_mean, mean_buf, sizeof(mean_buf));
            format_duration(results_[i].result.ns_p50, p50_buf, sizeof(p50_buf));
            format_duration(results_[i].result.ns_p99, p99_buf, sizeof(p99_buf));
            sink(results_[i].name, mean_buf, p50_buf, p99_buf,
                 results_[i].result.cycles_mean, results_[i].result.iterations);
        }
    }
};
