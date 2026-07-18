#pragma once

// 计时与基准测量工具
// 墙钟计时 / CPU 周期计数 / 统计分布 / 缓存延迟测量

#include <chrono>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "force_inline.hpp"
#include "class_pool.hpp"

// 墙钟计时器
class timer
{
    using clock = std::chrono::high_resolution_clock;
    clock::time_point start_;

public:
    timer() noexcept : start_(clock::now()) {}

    void reset() noexcept
    {
        start_ = clock::now();
    }

    [[nodiscard]] double elapsed_ns() const noexcept
    {
        return std::chrono::duration<double, std::nano>(clock::now() - start_).count();
    }

    [[nodiscard]] double elapsed_us() const noexcept
    {
        return std::chrono::duration<double, std::micro>(clock::now() - start_).count();
    }

    [[nodiscard]] double elapsed_ms() const noexcept
    {
        return std::chrono::duration<double, std::milli>(clock::now() - start_).count();
    }

    [[nodiscard]] double elapsed_s() const noexcept
    {
        return std::chrono::duration<double>(clock::now() - start_).count();
    }
};

// ============================================================
// CPU 周期计数 (x86/x64 rdtsc)
// ============================================================
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
    FORCE_INLINE uint64_t rdtsc() noexcept
    {
        return 0;
    }
    FORCE_INLINE uint64_t rdtscp() noexcept
    {
        return 0;
    }
#endif

// 周期计时器
class cycle_timer
{
    uint64_t start_;

public:
    cycle_timer() noexcept : start_(rdtscp()) {}

    void reset() noexcept
    {
        start_ = rdtscp();
    }

    [[nodiscard]] uint64_t elapsed_cycles() const noexcept
    {
        return rdtscp() - start_;
    }

    // 周期转纳秒, 需提供 CPU 频率 GHz
    [[nodiscard]] double elapsed_ns_estimated(double cpu_ghz) const noexcept
    {
        if (cpu_ghz <= 0)
        {
            return 0;
        }
        return static_cast<double>(elapsed_cycles()) / cpu_ghz;
    }
};

// 统计分布
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

// 从样本计算统计量 (会排序样本)
inline stats compute_stats(class_pool<double> samples) noexcept
{
    stats s;
    s.count = samples.size();
    if (s.count == 0)
    {
        return s;
    }
    // class_pool 迭代器为双向, 不支持 std::sort 的随机访问
    // 用 data() 取连续裸指针排序 (emplace_back 填充保证密集)
    double* p = samples.data();
    std::sort(p, p + s.count);
    s.min = p[0];
    s.max = p[s.count - 1];
    double sum = 0;
    for (size_t i = 0; i < s.count; ++i)
    {
        sum += p[i];
    }
    s.mean = sum / static_cast<double>(s.count);
    auto pct = [&](double q) noexcept -> double
    {
        if (s.count == 1)
        {
            return p[0];
        }
        size_t idx = static_cast<size_t>(q * static_cast<double>(s.count - 1));
        return p[idx];
    };
    s.median = s.p50 = pct(0.50);
    s.p90 = pct(0.90);
    s.p95 = pct(0.95);
    s.p99 = pct(0.99);
    double sq_sum = 0;
    for (size_t i = 0; i < s.count; ++i)
    {
        double v = p[i];
        sq_sum += (v - s.mean) * (v - s.mean);
    }
    s.stddev = std::sqrt(sq_sum / static_cast<double>(s.count));
    return s;
}

// 纳秒级基准: 运行 fn iterations 次
template <typename F>
stats benchmark_ns(size_t iterations, size_t warmup, F&& fn) noexcept
{
    for (size_t i = 0; i < warmup; ++i)
    {
        fn();
    }
    class_pool<double> samples;
    samples.increase_capacity(iterations);
    for (size_t i = 0; i < iterations; ++i)
    {
        timer t;
        fn();
        samples.emplace_back(t.elapsed_ns());
    }
    return compute_stats(std::move(samples));
}

// 周期级基准: 精度更高
template <typename F>
stats benchmark_cycles(size_t iterations, size_t warmup, F&& fn) noexcept
{
#if TIME_HAS_RDTSC
    for (size_t i = 0; i < warmup; ++i)
    {
        fn();
    }
    class_pool<double> samples;
    samples.increase_capacity(iterations);
    for (size_t i = 0; i < iterations; ++i)
    {
        uint64_t c0 = rdtscp();
        fn();
        uint64_t c1 = rdtscp();
        samples.emplace_back(static_cast<double>(c1 - c0));
    }
    return compute_stats(std::move(samples));
#else
    return benchmark_ns(iterations, warmup, std::forward<F>(fn));
#endif
}

// ============================================================
// 缓存延迟分级 (基于访问周期估算命中层级)
//   L1 ~4 周期, L2 ~12 周期, L3 ~40 周期, DRAM ~200+ 周期
// ============================================================
struct latency_thresholds
{
    double l1_max = 4.0;    // < 此值视为 L1 命中
    double l2_max = 15.0;   // < 此值视为 L2 命中
    double l3_max = 50.0;   // < 此值视为 L3 命中
    // >= l3_max 视为 DRAM 未命中
};

struct cache_report
{
    size_t total_accesses = 0;
    size_t l1_hits = 0;
    size_t l2_hits = 0;
    size_t l3_hits = 0;
    size_t misses = 0;
    double l1_hit_rate = 0;
    double l2_hit_rate = 0;
    double l3_hit_rate = 0;
    double miss_rate = 0;
    double avg_cycles = 0;
    double min_cycles = 0;
    double max_cycles = 0;
    double p50_cycles = 0;
    double p95_cycles = 0;
    double p99_cycles = 0;
    latency_thresholds thresholds;
};

// 测量一组地址访问的缓存命中情况
// 注: 单次 rdtscp 约 30 周期开销, 主要反映 L3 vs DRAM 差异
inline cache_report measure_cache_hits(const class_pool<const void*>& addresses,
                                       latency_thresholds th = {}) noexcept
{
    cache_report r;
    r.thresholds = th;
    r.total_accesses = addresses.size();
    if (r.total_accesses == 0)
    {
        return r;
    }

#if TIME_HAS_RDTSC
    class_pool<double> cycles;
    cycles.increase_capacity(r.total_accesses);
    size_t l1 = 0, l2 = 0, l3 = 0, miss = 0;
    double sum = 0;
    for (size_t i = 0; i < addresses.size(); ++i)
    {
        uint64_t c0 = rdtscp();
        volatile uint8_t v = *static_cast<const volatile uint8_t*>(addresses[i]);
        (void)v;
        uint64_t c1 = rdtscp();
        double cyc = static_cast<double>(c1 - c0);
        cycles.emplace_back(cyc);
        sum += cyc;
        if (cyc < th.l1_max)
        {
            ++l1;
        }
        else if (cyc < th.l2_max)
        {
            ++l2;
        }
        else if (cyc < th.l3_max)
        {
            ++l3;
        }
        else
        {
            ++miss;
        }
    }
    r.l1_hits = l1;
    r.l2_hits = l2;
    r.l3_hits = l3;
    r.misses = miss;
    r.avg_cycles = sum / static_cast<double>(r.total_accesses);
    double* cp = cycles.data();
    std::sort(cp, cp + r.total_accesses);
    r.min_cycles = cp[0];
    r.max_cycles = cp[r.total_accesses - 1];
    auto pct = [&](double q) noexcept -> double
    {
        return cp[static_cast<size_t>(q * static_cast<double>(r.total_accesses - 1))];
    };
    r.p50_cycles = pct(0.50);
    r.p95_cycles = pct(0.95);
    r.p99_cycles = pct(0.99);
#else
    r.l1_hits = r.total_accesses;
#endif
    double n = static_cast<double>(r.total_accesses);
    r.l1_hit_rate = static_cast<double>(r.l1_hits) / n;
    r.l2_hit_rate = static_cast<double>(r.l2_hits) / n;
    r.l3_hit_rate = static_cast<double>(r.l3_hits) / n;
    r.miss_rate = static_cast<double>(r.misses) / n;
    return r;
}

// 顺序访问地址序列 (缓存友好)
inline class_pool<const void*> make_sequential_addresses(const void* base, size_t count, size_t stride) noexcept
{
    class_pool<const void*> v;
    v.increase_capacity(count);
    const uint8_t* p = static_cast<const uint8_t*>(base);
    for (size_t i = 0; i < count; ++i)
    {
        v.emplace_back(p + i * stride);
    }
    return v;
}

// 随机访问地址序列 (缓存不友好, 确定性可复现)
inline class_pool<const void*> make_random_addresses(const void* base, size_t count, size_t stride, uint64_t seed = 12345) noexcept
{
    class_pool<size_t> indices;
    indices.increase_capacity(count);
    for (size_t i = 0; i < count; ++i)
    {
        indices.emplace_back(i);
    }
    // LCG 洗牌
    uint64_t x = seed;
    for (size_t i = count; i > 1; --i)
    {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        size_t j = static_cast<size_t>(x % i);
        std::swap(indices[i - 1], indices[j]);
    }
    class_pool<const void*> v;
    v.increase_capacity(count);
    const uint8_t* p = static_cast<const uint8_t*>(base);
    for (size_t i = 0; i < indices.size(); ++i)
    {
        v.emplace_back(p + indices[i] * stride);
    }
    return v;
}

// 批量缓存测量结果
struct batch_cache_result
{
    size_t total_accesses = 0;
    double total_cycles = 0;
    double avg_cycles_per_access = 0;
    double baseline_cycles = 0;
    double net_cycles_per_access = 0;
};

// 单次 rdtscp 包裹循环, 测量总周期
template <typename F>
double measure_loop_cycles(F&& access_fn) noexcept
{
#if TIME_HAS_RDTSC
    uint64_t c0 = rdtscp();
    access_fn();
    uint64_t c1 = rdtscp();
    return static_cast<double>(c1 - c0);
#else
    timer t;
    access_fn();
    return t.elapsed_ns();
#endif
}

// 批量测量: 返回平均每次访问周期 (扣除基线)
inline batch_cache_result measure_cache_batch(const class_pool<const void*>& addresses, size_t repeats = 10) noexcept
{
    batch_cache_result r;
    r.total_accesses = addresses.size() * repeats;
    if (addresses.empty())
    {
        return r;
    }

#if TIME_HAS_RDTSC
    // 基线: 空循环 (相同迭代次数, 不访问目标内存)
    double baseline = 0;
    volatile size_t sink = 0;
    for (size_t trial = 0; trial < 3; ++trial)
    {
        uint64_t c0 = rdtscp();
        for (size_t rep = 0; rep < repeats; ++rep)
        {
            for (size_t i = 0; i < addresses.size(); ++i)
            {
                sink = i;
            }
        }
        uint64_t c1 = rdtscp();
        baseline = std::max(baseline, static_cast<double>(c1 - c0));
    }
    (void)sink;
    r.baseline_cycles = baseline;

    // 实际访问测量 (取 3 次最小值)
    double best = 1e18;
    for (size_t trial = 0; trial < 3; ++trial)
    {
        uint64_t c0 = rdtscp();
        for (size_t rep = 0; rep < repeats; ++rep)
        {
            for (size_t i = 0; i < addresses.size(); ++i)
            {
                volatile uint8_t v = *static_cast<const volatile uint8_t*>(addresses[i]);
                (void)v;
            }
        }
        uint64_t c1 = rdtscp();
        best = std::min(best, static_cast<double>(c1 - c0));
    }
    r.total_cycles = best;
    r.avg_cycles_per_access = r.total_cycles / static_cast<double>(r.total_accesses);
    double net = r.total_cycles - r.baseline_cycles;
    if (net < 0)
    {
        net = 0;
    }
    r.net_cycles_per_access = net / static_cast<double>(r.total_accesses);
#endif
    return r;
}

// CPU 频率估算 (GHz)
inline double estimate_cpu_ghz(size_t calibration_ms = 100) noexcept
{
#if TIME_HAS_RDTSC
    timer t;
    uint64_t c0 = rdtscp();
    while (t.elapsed_ms() < static_cast<double>(calibration_ms))
    {
        // 忙等
    }
    uint64_t c1 = rdtscp();
    double elapsed_s = t.elapsed_ms() / 1000.0;
    if (elapsed_s <= 0)
    {
        return 0;
    }
    return static_cast<double>(c1 - c0) / elapsed_s / 1e9;
#else
    (void)calibration_ms;
    return 0;
#endif
}
