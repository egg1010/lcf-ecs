#pragma once

// 计时与基准测量工具
// 墙钟计时 / CPU 周期计数 / 缓存屏障 / 统计分布 / 在线分位数 / 缓存延迟测量

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include "tiered_sort.hpp"
#include <cmath>
#include "force_inline.hpp"
#include "dense.hpp"

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

// CPU 周期计数 (x86/x64 rdtsc)
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

// x86 缓存行刷新 / 内存屏障 / 精确计时栅栏
#if TIME_HAS_RDTSC
    #if defined(_MSC_VER)
        FORCE_INLINE void cache_flush(const void* p) noexcept { _mm_clflush(p); }
        FORCE_INLINE void mfence() noexcept { _mm_mfence(); }
        FORCE_INLINE void lfence() noexcept { _mm_lfence(); }
        // Intel 推荐: lfence; rdtsc; lfence 全屏障周期测量
        FORCE_INLINE uint64_t rdtsc_fenced() noexcept
        {
            _mm_lfence();
            uint64_t t = __rdtsc();
            _mm_lfence();
            return t;
        }
    #else
        FORCE_INLINE void cache_flush(const void* p) noexcept
        {
            __asm__ __volatile__("clflush %0" : : "m"(*(const volatile char*)p));
        }
        FORCE_INLINE void mfence() noexcept
        {
            __asm__ __volatile__("mfence" ::: "memory");
        }
        FORCE_INLINE void lfence() noexcept
        {
            __asm__ __volatile__("lfence" ::: "memory");
        }
        FORCE_INLINE uint64_t rdtsc_fenced() noexcept
        {
            __asm__ __volatile__("lfence" ::: "memory");
            uint32_t lo, hi;
            __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
            __asm__ __volatile__("lfence" ::: "memory");
            return (static_cast<uint64_t>(hi) << 32) | lo;
        }
    #endif

    FORCE_INLINE void cache_flush_range(const void* p, size_t bytes) noexcept
    {
        const char* cp = static_cast<const char*>(p);
        const char* end = cp + bytes;
        while (cp < end)
        {
            cache_flush(cp);
            cp += 64;
        }
        mfence();
    }
#else
    FORCE_INLINE void cache_flush(const void*) noexcept {}
    FORCE_INLINE void cache_flush_range(const void*, size_t) noexcept {}
    FORCE_INLINE void mfence() noexcept {}
    FORCE_INLINE void lfence() noexcept {}
    FORCE_INLINE uint64_t rdtsc_fenced() noexcept { return 0; }
#endif

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

    [[nodiscard]] double elapsed_ns_estimated(double cpu_ghz) const noexcept
    {
        if (cpu_ghz <= 0)
        {
            return 0;
        }
        return static_cast<double>(elapsed_cycles()) / cpu_ghz;
    }
};

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
inline stats compute_stats(dense<double> samples) noexcept
{
    stats s;
    s.count = samples.size();
    if (s.count == 0)
    {
        return s;
    }
    // 用 data() 取连续裸指针排序 (emplace_back 填充保证密集)
    double* p = samples.data();
    sort(p, s.count);
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

// P² 在线分位数估计器 (Jain & Chlamtac, 1985)
// O(1) 空间, O(1) 每次观测, 无需存储全部样本
// 适用: 流式基准 / 大样本 / 实时监控
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

template <typename F>
stats benchmark_ns(size_t iterations, size_t warmup, F&& fn) noexcept
{
    for (size_t i = 0; i < warmup; ++i)
    {
        fn();
    }
    dense<double> samples;
    samples.increase_capacity(iterations);
    for (size_t i = 0; i < iterations; ++i)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
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
    dense<double> samples;
    samples.increase_capacity(iterations);
    for (size_t i = 0; i < iterations; ++i)
    {
        uint64_t c0 = rdtscp();
        fn();
        uint64_t c1 = rdtscp();
        samples.push_back(static_cast<double>(c1 - c0));
    }
    return compute_stats(std::move(samples));
#else
    return benchmark_ns(iterations, warmup, std::forward<F>(fn));
#endif
}

// 缓存延迟分级 (基于访问周期估算命中层级)
//   默认假设三级缓存: L1 ~4, L2 ~12, L3 ~40, DRAM ~200+
//   不同 CPU 缓存层级不同 (嵌入式可能仅 1-2 级), 可通过 cache_levels 配置
//   亦可调用 detect_cache_latency_thresholds() 自动检测
struct latency_thresholds
{
    double l1_max = 4.0;
    double l2_max = 15.0;   // cache_levels<2 时忽略
    double l3_max = 50.0;   // cache_levels<3 时忽略
    uint32_t cache_levels = 3;  // 1=仅L1, 2=L1+L2, 3=L1+L2+L3
    // >= 最后一级阈值视为 DRAM 未命中
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
    uint32_t active_levels = 3;  // 由 thresholds.cache_levels 决定
};

// 注: 单次 rdtscp 约 30 周期开销, 主要反映 L3 vs DRAM 差异
inline cache_report measure_cache_hits(const dense<const void*>& addresses,
                                       latency_thresholds th = {}) noexcept
{
    cache_report r;
    r.thresholds = th;
    r.active_levels = th.cache_levels;
    r.total_accesses = addresses.size();
    if (r.total_accesses == 0)
    {
        return r;
    }

#if TIME_HAS_RDTSC
    dense<double> cycles;
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
        cycles.push_back(cyc);
        sum += cyc;
        if (cyc < th.l1_max)
        {
            ++l1;
        }
        else if (th.cache_levels >= 2 && cyc < th.l2_max)
        {
            ++l2;
        }
        else if (th.cache_levels >= 3 && cyc < th.l3_max)
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
    sort(cp, r.total_accesses);
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

inline dense<const void*> make_sequential_addresses(const void* base, size_t count, size_t stride) noexcept
{
    dense<const void*> v;
    v.increase_capacity(count);
    const uint8_t* p = static_cast<const uint8_t*>(base);
    for (size_t i = 0; i < count; ++i)
    {
        v.push_back(p + i * stride);
    }
    return v;
}

// 随机访问地址序列 (缓存不友好, 确定性可复现)
inline dense<const void*> make_random_addresses(const void* base, size_t count, size_t stride, uint64_t seed = 12345) noexcept
{
    dense<size_t> indices;
    indices.increase_capacity(count);
    for (size_t i = 0; i < count; ++i)
    {
        indices.push_back(i);
    }
    // LCG 洗牌
    uint64_t x = seed;
    for (size_t i = count; i > 1; --i)
    {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        size_t j = static_cast<size_t>(x % i);
        std::swap(indices[i - 1], indices[j]);
    }
    dense<const void*> v;
    v.increase_capacity(count);
    const uint8_t* p = static_cast<const uint8_t*>(base);
    for (size_t i = 0; i < indices.size(); ++i)
    {
        v.push_back(p + indices[i] * stride);
    }
    return v;
}

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
inline batch_cache_result measure_cache_batch(const dense<const void*>& addresses, size_t repeats = 10) noexcept
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

// 通过步进扫描不同工作集大小 (1KB → 16MB), 检测延迟跳变推断缓存层级
// 返回值: cache_levels 为实际检测到的层级数 (1/2/3)
inline latency_thresholds detect_cache_latency_thresholds() noexcept
{
    latency_thresholds th;
#if TIME_HAS_RDTSC
    constexpr size_t buf_size = 16 * 1024 * 1024;  // 16MB 覆盖 L1/L2/L3
    constexpr size_t cache_line = 64;
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[buf_size]);
    if (!buf) [[unlikely]] return th;

    // 页故障预热
    for (size_t i = 0; i < buf_size; i += cache_line) { buf[i] = 0; }

    double prev_lat = 0;
    uint32_t levels = 0;

    for (size_t sz = 1024; sz <= buf_size; sz *= 2)
    {
        size_t count = sz / cache_line;
        auto addrs = make_sequential_addresses(buf.get(), count, cache_line);
        batch_cache_result bcr = measure_cache_batch(addrs, 3);
        double cur = bcr.net_cycles_per_access;

        if (levels == 0)
        {
            th.l1_max = cur * 1.5;
            levels = 1;
        }
        else if (levels < 3 && cur > prev_lat * 1.3)
        {
            // 延迟跳变 → 新缓存层级边界
            if (levels == 1)
            {
                th.l2_max = cur * 0.8;
                levels = 2;
            }
            else if (levels == 2)
            {
                th.l3_max = cur * 0.8;
                levels = 3;
            }
        }
        prev_lat = cur;
    }
    th.cache_levels = levels;
#endif
    return th;
}

// 注: 测量的是 invariant TSC 频率 (恒定), 而非核心频率 (受 Turbo Boost/DVFS 影响)
//     rdtsc 计数速率 = TSC 频率, 不随核心频率变化
//     cycle_timer::elapsed_ns_estimated() 基于 TSC 频率, 适合相对比较
//     若需精确墙钟时间, 优先使用 timer (high_resolution_clock)
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

// CPU 频率缓存 (首次调用校准, 后续零开销)
// 缓存的是 TSC 频率, 非核心频率 (参见 estimate_cpu_ghz 注释)
inline double cpu_ghz_cached() noexcept
{
    static double cached = estimate_cpu_ghz();
    return cached;
}

// 延迟异常检测器: 基于 P² 分位数动态检测超标延迟
// 用法: 持续 add() 建立基线, 然后 is_anomaly() 判断新样本是否异常
struct latency_anomaly_detector
{
    p2_quantile p50{0.50};
    p2_quantile p99{0.99};
    double multiplier = 3.0;  // 超过 p99 * multiplier 视为异常
    size_t warmup_count = 100;

    void add(double latency_ns) noexcept
    {
        p50.add(latency_ns);
        p99.add(latency_ns);
    }

    [[nodiscard]] bool is_anomaly(double latency_ns) const noexcept
    {
        if (p99.count() < warmup_count) [[unlikely]] return false;
        double threshold = p99.estimate() * multiplier;
        return latency_ns > threshold;
    }

    [[nodiscard]] double anomaly_threshold() const noexcept
    {
        return p99.estimate() * multiplier;
    }
};

// 流式基准测试: 使用 P² 在线估计, 无需存储全部样本
// 返回 p50/p90/p95/p99 估计值, 适合超大样本或内存受限场景
struct p2_benchmark_result
{
    double p50 = 0;
    double p90 = 0;
    double p95 = 0;
    double p99 = 0;
    size_t count = 0;
};

template <typename F>
p2_benchmark_result benchmark_p2(size_t iterations, size_t warmup, F&& fn) noexcept
{
    for (size_t i = 0; i < warmup; ++i)
    {
        fn();
    }
    p2_quantile est50(0.50), est90(0.90), est95(0.95), est99(0.99);
    for (size_t i = 0; i < iterations; ++i)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        est50.add(ns);
        est90.add(ns);
        est95.add(ns);
        est99.add(ns);
    }
    return {est50.estimate(), est90.estimate(), est95.estimate(), est99.estimate(), iterations};
}
