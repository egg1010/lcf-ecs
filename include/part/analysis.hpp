#pragma once

// analysis.hpp - 微架构分析工具 (缓存测量 / 内存屏障 / 地址生成)
// 无命名空间, 通用工具. 设计原则: 精度第一, 性能第二.
// x86/x64 提供完整支持 (rdtscp/clflush/mfence/lfence/sfence);
// 其他平台屏障为编译器屏障, 缓存测量回退到周期计数 (rdtscp 返回 0 时仅统计访问次数).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include "force_inline.hpp"
#include "dense.hpp"
#include "time.hpp"  // rdtscp / rdtsc

// =============================================================================
// 平台检测
// =============================================================================

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    #define ANALYSIS_HAS_X86 1
    #if defined(_MSC_VER)
        #include <intrin.h>
    #endif
#else
    #define ANALYSIS_HAS_X86 0
#endif

// =============================================================================
// L0: 内存屏障原语 (x86/x64 全套; 其他平台编译器屏障)
// =============================================================================

// 全屏障 (Store + Load 序列化)
FORCE_INLINE void mfence() noexcept
{
#if ANALYSIS_HAS_X86
    #if defined(_MSC_VER)
        _mm_mfence();
    #else
        __asm__ __volatile__("mfence" ::: "memory");
    #endif
#else
    asm volatile("" ::: "memory");
#endif
}

// Load 屏障 (后续 Load 不重排到前面)
FORCE_INLINE void lfence() noexcept
{
#if ANALYSIS_HAS_X86
    #if defined(_MSC_VER)
        _mm_lfence();
    #else
        __asm__ __volatile__("lfence" ::: "memory");
    #endif
#else
    asm volatile("" ::: "memory");
#endif
}

// Store 屏障 (后续 Store 不重排到前面)
FORCE_INLINE void sfence() noexcept
{
#if ANALYSIS_HAS_X86
    #if defined(_MSC_VER)
        _mm_sfence();
    #else
        __asm__ __volatile__("sfence" ::: "memory");
    #endif
#else
    asm volatile("" ::: "memory");
#endif
}

// =============================================================================
// L1: 缓存控制 (x86/x64 clflush; 其他平台空操作)
// =============================================================================

// 刷新单条缓存行 (64 字节对齐的行)
FORCE_INLINE void cache_flush(const void* p) noexcept
{
#if ANALYSIS_HAS_X86
    #if defined(_MSC_VER)
        _mm_clflush(p);
    #else
        __asm__ __volatile__("clflush %0" : : "m"(*static_cast<const char*>(p)));
    #endif
#else
    (void)p;
#endif
}

// 刷新字节范围 (逐缓存行刷新, 尾部 mfence 确保全局可见)
inline void cache_flush_range(const void* p, size_t bytes) noexcept
{
#if ANALYSIS_HAS_X86
    const char* ptr = static_cast<const char*>(p);
    // 对齐到缓存行边界 (64 字节)
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned = addr & ~static_cast<uintptr_t>(63);
    const char* end = ptr + bytes;
    const char* aligned_ptr = reinterpret_cast<const char*>(aligned);
    while (aligned_ptr < end)
    {
        cache_flush(aligned_ptr);
        aligned_ptr += 64;
    }
    mfence();
#else
    (void)p;
    (void)bytes;
#endif
}

// 预取提示 (复用 force_inline.hpp 的 LCF_PREFETCH_R / LCF_PREFETCH_NTA)
// LCF_PREFETCH_R(ptr)   - 预取到 L1 (高局部性)
// LCF_PREFETCH_NTA(ptr) - 预取非临时 (不污染缓存)

// =============================================================================
// L2: 序列化周期测量 (lfence; rdtsc; lfence 全屏障, Intel 推荐顺序)
// =============================================================================

// 全屏障周期测量: lfence; rdtsc; lfence (避免乱序导致的计数偏差)
// 适合精确测量短代码段 (相比 rdtscp 更严格, 两端都有 lfence)
[[nodiscard]] FORCE_INLINE uint64_t rdtsc_fenced() noexcept
{
#if ANALYSIS_HAS_X86
    #if defined(_MSC_VER)
        _mm_lfence();
        uint64_t t = __rdtsc();
        _mm_lfence();
        return t;
    #else
        uint32_t lo, hi;
        __asm__ __volatile__(
            "lfence\n\t"
            "rdtsc\n\t"
            "lfence"
            : "=a"(lo), "=d"(hi)
            :: "memory");
        return (static_cast<uint64_t>(hi) << 32) | lo;
    #endif
#else
    return 0;
#endif
}

// 全屏障配对计时: 返回 fn 执行的周期数 (lfence; rdtsc 包裹)
// 适合短代码段精确测量 (相比 measure_cycles 更严格, 排除乱序干扰)
template<typename F>
[[nodiscard]] uint64_t measure_cycles_fenced(F&& fn) noexcept
{
#if ANALYSIS_HAS_X86
    uint64_t c0 = rdtsc_fenced();
    fn();
    uint64_t c1 = rdtsc_fenced();
    return c1 - c0;
#else
    fn();
    return 0;
#endif
}

// =============================================================================
// L3: 地址生成 (顺序 / 随机, 确定性可复现)
// =============================================================================

// 地址视图: 零分配 POD, 持有 {指针, 数量}
struct address_view
{
    const void* const* addrs;
    size_t count;

    [[nodiscard]] const void* operator[](size_t i) const noexcept { return addrs[i]; }
    [[nodiscard]] size_t size() const noexcept { return count; }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
};

// 顺序地址序列 (缓存友好: 逐元素步进)
// 返回 dense<const void*>, 调用方负责生命周期
inline dense<const void*> make_sequential_addresses(
    const void* base, size_t count, size_t stride) noexcept
{
    dense<const void*> result;
    result.reserve_exact(count);
    const char* ptr = static_cast<const char*>(base);
    for (size_t i = 0; i < count; ++i)
    {
        result.push_back(static_cast<const void*>(ptr));
        ptr += stride;
    }
    return result;
}

// 随机地址序列 (缓存不友好, 确定性可复现)
// 使用 xorshift64 PRNG (无依赖, 周期 2^64-1, 无 std 容器)
inline dense<const void*> make_random_addresses(
    const void* base, size_t count, size_t stride, uint64_t seed) noexcept
{
    dense<const void*> result;
    result.reserve_exact(count);
    const char* ptr = static_cast<const char*>(base);

    // xorshift64 (Marsaglia): 确定性, 快速, 无状态依赖
    uint64_t state = seed ? seed : 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < count; ++i)
    {
        // 生成随机索引 [0, count)
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        size_t idx = static_cast<size_t>(state % count);
        result.push_back(static_cast<const void*>(ptr + idx * stride));
    }
    return result;
}

// =============================================================================
// L4: 缓存层级阈值 (可手动设置或自适应检测)
// =============================================================================

// 缓存层级阈值 (周期数): 访问延迟 <= 阈值视为命中该层
struct latency_thresholds
{
    double l1_max = 4.0;       // L1 命中: <= 4 周期 (典型 ~4)
    double l2_max = 12.0;      // L2 命中: <= 12 周期 (典型 ~10-14)
    double l3_max = 40.0;      // L3 命中: <= 40 周期 (典型 ~30-50)
    int cache_levels = 3;      // 缓存层级数 (1/2/3, 嵌入式可能 1-2)
};

// 缓存命中报告
struct cache_report
{
    size_t total = 0;          // 总访问次数
    size_t l1_hits = 0;        // L1 命中数
    size_t l2_hits = 0;        // L2 命中数
    size_t l3_hits = 0;        // L3 命中数
    size_t misses = 0;         // 未命中数 (主存访问)
    double l1_hit_rate = 0;    // L1 命中率
    double l2_hit_rate = 0;    // L2 命中率
    double l3_hit_rate = 0;    // L3 命中率
    double miss_rate = 0;      // 未命中率
    double avg_cycles = 0;     // 平均访问周期
    double p50_cycles = 0;     // 中位数周期
    double p95_cycles = 0;     // 95 百分位周期
    double p99_cycles = 0;     // 99 百分位周期
    int active_levels = 3;     // 实际参与分类的层级 (受 cache_levels 限制)
};

// 批量测量结果 (扣除基线, 适合精确 L1/L2 延迟测量)
struct batch_cache_result
{
    double total_cycles = 0;           // 总周期数 (含基线)
    double avg_cycles_per_access = 0;  // 平均每次访问周期 (含基线)
    double baseline_cycles = 0;        // 基线周期 (空指针访问)
    double net_cycles_per_access = 0;  // 净每次访问周期 (扣除基线)
    size_t repeats = 0;                // 重复次数
};

// =============================================================================
// L5: 缓存命中测量 (逐次 rdtscp 计时 + 阈值分类)
// =============================================================================

namespace detail
{
    // 单次访问延迟测量 (rdtscp 配对, 含 lfence 序列化)
    [[nodiscard]] FORCE_INLINE uint64_t measure_one_access(const void* p) noexcept
    {
#if ANALYSIS_HAS_X86
        uint64_t c0, c1;
        #if defined(_MSC_VER)
            _mm_lfence();
            c0 = __rdtscp(nullptr);
            // 强制读取, 防止优化器消除
            volatile uint8_t v = *static_cast<const volatile uint8_t*>(p);
            (void)v;
            _mm_lfence();
            c1 = __rdtscp(nullptr);
        #else
            __asm__ __volatile__("lfence" ::: "memory");
            uint32_t lo, hi;
            __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx", "memory");
            c0 = (static_cast<uint64_t>(hi) << 32) | lo;
            volatile uint8_t v = *static_cast<const volatile uint8_t*>(p);
            (void)v;
            __asm__ __volatile__("lfence" ::: "memory");
            __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx", "memory");
            c1 = (static_cast<uint64_t>(hi) << 32) | lo;
        #endif
        return c1 - c0;
#else
        (void)p;
        return 0;
#endif
    }

    // 基线测量 (空指针访问, 测量计时开销本身)
    [[nodiscard]] FORCE_INLINE uint64_t measure_baseline() noexcept
    {
#if ANALYSIS_HAS_X86
        uint64_t c0, c1;
        #if defined(_MSC_VER)
            _mm_lfence();
            c0 = __rdtscp(nullptr);
            _mm_lfence();
            c1 = __rdtscp(nullptr);
        #else
            __asm__ __volatile__("lfence" ::: "memory");
            uint32_t lo, hi;
            __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx", "memory");
            c0 = (static_cast<uint64_t>(hi) << 32) | lo;
            __asm__ __volatile__("lfence" ::: "memory");
            __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx", "memory");
            c1 = (static_cast<uint64_t>(hi) << 32) | lo;
        #endif
        return c1 - c0;
#else
        return 0;
#endif
    }

    // 内部分类: 根据周期数和阈值判定层级
    [[nodiscard]] inline int classify_latency(
        uint64_t cycles, const latency_thresholds& th) noexcept
    {
        double c = static_cast<double>(cycles);
        if (c <= th.l1_max)
        {
            return 0;  // L1
        }
        if (th.cache_levels >= 2 && c <= th.l2_max)
        {
            return 1;  // L2
        }
        if (th.cache_levels >= 3 && c <= th.l3_max)
        {
            return 2;  // L3
        }
        return 3;  // Miss
    }

    // 内部统计计算 (已排序裸指针)
    inline void compute_pct(double* sorted, size_t n, cache_report& r) noexcept
    {
        if (n == 0)
        {
            return;
        }
        auto pct = [&](double q) noexcept -> double
        {
            size_t idx = static_cast<size_t>(q * static_cast<double>(n - 1));
            return sorted[idx];
        };
        r.p50_cycles = pct(0.50);
        r.p95_cycles = pct(0.95);
        r.p99_cycles = pct(0.99);
    }
}  // namespace detail

// =============================================================================
// L6: 缓存命中测量 (逐次计时, 阈值分类 + 百分位统计)
// =============================================================================

// 测量一组地址访问的缓存命中情况
// 默认三级阈值 (l1_max=4, l2_max=12, l3_max=40), 可传入自定义阈值
// 注: 逐次计时含 rdtscp 配对开销 (~50 周期), 分类基于净延迟 (扣除基线)
[[nodiscard]] inline cache_report measure_cache_hits(
    const address_view& addrs,
    const latency_thresholds& th = latency_thresholds{}) noexcept
{
    cache_report r;
    r.total = addrs.count;
    r.active_levels = th.cache_levels;
    if (addrs.count == 0)
    {
        return r;
    }

    // 测量基线 (rdtscp 配对开销, 多次取最小)
    uint64_t baseline = static_cast<uint64_t>(-1);
    for (size_t i = 0; i < 100; ++i)
    {
        uint64_t b = detail::measure_baseline();
        if (b < baseline)
        {
            baseline = b;
        }
    }

    dense<double> samples;
    samples.reserve_exact(addrs.count);

    double sum = 0;
    for (size_t i = 0; i < addrs.count; ++i)
    {
        uint64_t raw = detail::measure_one_access(addrs[i]);
        // 净延迟: 扣除基线 (rdtscp 配对开销), 反映真实访问延迟
        uint64_t cyc = (raw > baseline) ? raw - baseline : 0;
        double c = static_cast<double>(cyc);
        samples.push_back(c);
        sum += c;

        int level = detail::classify_latency(cyc, th);
        switch (level)
        {
            case 0: ++r.l1_hits; break;
            case 1: ++r.l2_hits; break;
            case 2: ++r.l3_hits; break;
            default: ++r.misses; break;
        }
    }

    r.avg_cycles = sum / static_cast<double>(addrs.count);
    r.l1_hit_rate = static_cast<double>(r.l1_hits) / static_cast<double>(addrs.count);
    r.l2_hit_rate = static_cast<double>(r.l2_hits) / static_cast<double>(addrs.count);
    r.l3_hit_rate = static_cast<double>(r.l3_hits) / static_cast<double>(addrs.count);
    r.miss_rate = static_cast<double>(r.misses) / static_cast<double>(addrs.count);

    // 排序样本计算百分位
    double* p = samples.data();
    sort(p, samples.size());
    detail::compute_pct(p, samples.size(), r);

    return r;
}

// =============================================================================
// L7: 批量缓存测量 (多次重复取最优, 扣除基线)
// =============================================================================

// 批量测量: 重复 repeats 次, 每次遍历全部地址, 取 3 次最优值平均
// 扣除基线 (空访问计时开销), 适合精确 L1/L2 延迟测量
[[nodiscard]] inline batch_cache_result measure_cache_batch(
    const address_view& addrs, size_t repeats = 10) noexcept
{
    batch_cache_result r;
    r.repeats = repeats;
    if (addrs.count == 0 || repeats == 0)
    {
        return r;
    }

#if ANALYSIS_HAS_X86
    // 取 3 次最优值 (最小值) 平均, 减少调度噪声
    constexpr size_t BEST_COUNT = 3;
    uint64_t best_times[BEST_COUNT] = {};
    for (size_t i = 0; i < BEST_COUNT; ++i)
    {
        best_times[i] = static_cast<uint64_t>(-1);
    }

    for (size_t rep = 0; rep < repeats; ++rep)
    {
        uint64_t total = 0;
        for (size_t i = 0; i < addrs.count; ++i)
        {
            total += detail::measure_one_access(addrs[i]);
        }
        // 插入到最优值数组 (保持升序)
        for (size_t i = 0; i < BEST_COUNT; ++i)
        {
            if (total < best_times[i])
            {
                for (size_t j = BEST_COUNT - 1; j > i; --j)
                {
                    best_times[j] = best_times[j - 1];
                }
                best_times[i] = total;
                break;
            }
        }
    }

    // 3 次最优值平均
    double sum_best = 0;
    for (size_t i = 0; i < BEST_COUNT; ++i)
    {
        sum_best += static_cast<double>(best_times[i]);
    }
    double avg_total = sum_best / static_cast<double>(BEST_COUNT);
    r.total_cycles = avg_total;
    r.avg_cycles_per_access = avg_total / static_cast<double>(addrs.count);

    // 基线: 测量空访问开销 (多次取最小)
    uint64_t best_baseline = static_cast<uint64_t>(-1);
    constexpr size_t BASELINE_ITERS = 1000;
    for (size_t i = 0; i < BASELINE_ITERS; ++i)
    {
        uint64_t b = detail::measure_baseline();
        if (b < best_baseline)
        {
            best_baseline = b;
        }
    }
    r.baseline_cycles = static_cast<double>(best_baseline);
    r.net_cycles_per_access =
        (r.avg_cycles_per_access > r.baseline_cycles)
            ? r.avg_cycles_per_access - r.baseline_cycles
            : 0;
#else
    // 非 x86: 无周期计数, 返回零
    (void)repeats;
#endif
    return r;
}

// =============================================================================
// L8: 单次循环周期测量 (rdtscp 包裹整个循环)
// =============================================================================

// 测量 fn 执行的总周期数 (单次 rdtscp 包裹, 不含 lfence)
// 适合测量循环体内的总开销 (相比 measure_cycles_fenced 开销更低)
template<typename F>
[[nodiscard]] uint64_t measure_loop_cycles(F&& fn) noexcept
{
#if ANALYSIS_HAS_X86
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
// L9: 自适应缓存层级检测 (1KB → 16MB 步进扫描)
// =============================================================================

// 自适应检测缓存层级和阈值
// 原理: 分配不同大小的工作集, 顺序访问测量平均周期
//   - 工作集 <= L1: 低延迟 (L1 命中)
//   - L1 < 工作集 <= L2: 延迟上升 (L2 命中)
//   - L2 < 工作集 <= L3: 延迟再上升 (L3 命中)
//   - 工作集 > L3: 延迟最高 (主存访问)
// 返回检测到的阈值, cache_levels 表示实际层级数 (1/2/3)
[[nodiscard]] inline latency_thresholds detect_cache_latency_thresholds() noexcept
{
    latency_thresholds th;

#if ANALYSIS_HAS_X86
    // 工作集大小序列 (字节): 1KB → 16MB, 2 倍步进
    // 覆盖 L1 (32-64KB) / L2 (256KB-1MB) / L3 (4-16MB)
    constexpr size_t SCAN_SIZES[] = {
        1024,       // 1KB
        2048,       // 2KB
        4096,       // 4KB
        8192,       // 8KB
        16384,      // 16KB
        32768,      // 32KB
        65536,      // 64KB
        131072,     // 128KB
        262144,     // 256KB
        524288,     // 512KB
        1048576,    // 1MB
        2097152,    // 2MB
        4194304,    // 4MB
        8388608,    // 8MB
        16777216    // 16MB
    };
    constexpr size_t SCAN_COUNT = sizeof(SCAN_SIZES) / sizeof(SCAN_SIZES[0]);

    // 为每个工作集大小测量平均访问周期
    // 缓冲区: 64 字节对齐, 避免缓存行跨界
    constexpr size_t MAX_BUF = 16 * 1024 * 1024 + 64;
    alignas(64) static char scan_buf[MAX_BUF];

    struct size_result
    {
        size_t size;
        double avg_cycles;
    };
    dense<size_result> results;
    results.reserve_exact(SCAN_COUNT);

    for (size_t i = 0; i < SCAN_COUNT; ++i)
    {
        size_t sz = SCAN_SIZES[i];
        size_t count = sz / 64;  // 每缓存行一个访问点
        if (count == 0)
        {
            count = 1;
        }

        // 生成顺序地址 (64 字节步进, 每次访问不同缓存行)
        auto addrs = make_sequential_addresses(scan_buf, count, 64);
        address_view av{addrs.data(), addrs.size()};

        // 热缓存测量: 先预热一遍 (填充缓存), 再测量第二遍延迟
        // 工作集 <= L1: 第二遍全 L1 命中 (~4 周期)
        // L1 < 工作集 <= L2: 第二遍 L2 命中 (~10-14 周期, L1 容纳不下)
        // L2 < 工作集 <= L3: 第二遍 L3 命中 (~30-50 周期)
        // 工作集 > L3: 第二遍主存访问 (~100-300 周期)
        constexpr size_t WARM_REPEATS = 3;
        double best_avg = 1e18;
        for (size_t rep = 0; rep < WARM_REPEATS; ++rep)
        {
            // 预热: 完整访问一遍, 填充缓存
            for (size_t j = 0; j < av.size(); ++j)
            {
                volatile uint8_t v = *static_cast<const volatile uint8_t*>(av[j]);
                (void)v;
            }
            // 测量: 第二遍访问, 数据已在缓存中
            double sum = 0;
            for (size_t j = 0; j < av.size(); ++j)
            {
                uint64_t raw = detail::measure_one_access(av[j]);
                sum += static_cast<double>(raw);
            }
            double avg = sum / static_cast<double>(av.size());
            if (avg < best_avg)
            {
                best_avg = avg;
            }
        }
        // 净延迟: 扣除基线 (rdtscp 配对开销)
        uint64_t baseline = static_cast<uint64_t>(-1);
        for (size_t b = 0; b < 100; ++b)
        {
            uint64_t v = detail::measure_baseline();
            if (v < baseline)
            {
                baseline = v;
            }
        }
        double net = (best_avg > static_cast<double>(baseline))
                         ? best_avg - static_cast<double>(baseline)
                         : 0;
        results.push_back({sz, net});
    }

    // 分析延迟跳变点, 识别层级
    // L1→L2: 第一个显著跳变 (延迟显著上升)
    // L2→L3: 第二个显著跳变
    if (results.size() >= 3)
    {
        double l1_threshold = results[0].avg_cycles;
        double l2_threshold = l1_threshold;
        double l3_threshold = l2_threshold;
        int levels = 1;

        for (size_t i = 1; i < results.size(); ++i)
        {
            double prev = results[i - 1].avg_cycles;
            double cur = results[i].avg_cycles;
            // 跳变判定: 延迟相对增量 > 50% 且绝对增量 > 3 周期
            if (prev > 0 && cur > prev * 1.5 && (cur - prev) > 3.0)
            {
                if (levels == 1)
                {
                    l2_threshold = cur;
                    l1_threshold = prev;
                    levels = 2;
                }
                else if (levels == 2)
                {
                    l3_threshold = cur;
                    l2_threshold = prev;
                    levels = 3;
                }
            }
        }

        th.l1_max = l1_threshold + 1.0;   // L1 阈值: 跳变前延迟 + 余量
        th.l2_max = (levels >= 2) ? l2_threshold + 2.0 : th.l2_max;
        th.l3_max = (levels >= 3) ? l3_threshold + 5.0 : th.l3_max;
        th.cache_levels = levels;
    }
#else
    // 非 x86: 返回默认阈值, 无法检测
#endif

    return th;
}

// =============================================================================
// L10: 报告打印 (格式化输出到 stdout)
// =============================================================================

// 打印 cache_report 到 stdout
inline void print_cache_report(const char* label, const cache_report& r) noexcept
{
    std::printf("  %-28s | L1:%5.1f%%  L2:%5.1f%%  L3:%5.1f%%  Miss:%5.1f%%"
                " | avg=%5.1f  p50=%5.1f  p95=%5.1f  p99=%5.1f  cyc  levels=%d\n",
                label,
                r.l1_hit_rate * 100, r.l2_hit_rate * 100,
                r.l3_hit_rate * 100, r.miss_rate * 100,
                r.avg_cycles, r.p50_cycles, r.p95_cycles, r.p99_cycles,
                r.active_levels);
}

// 打印 batch_cache_result 到 stdout
inline void print_cache_batch(const char* label, const batch_cache_result& r) noexcept
{
    std::printf("  %-28s | total=%8.1f  avg=%5.1f  baseline=%4.1f  net=%5.1f  cyc/access"
                "  (repeats=%zu)\n",
                label, r.total_cycles, r.avg_cycles_per_access,
                r.baseline_cycles, r.net_cycles_per_access, r.repeats);
}

// 打印 latency_thresholds 到 stdout
inline void print_thresholds(const char* label, const latency_thresholds& th) noexcept
{
    std::printf("  %-28s | levels=%d  L1<=%.1f  L2<=%.1f  L3<=%.1f  cyc\n",
                label, th.cache_levels, th.l1_max, th.l2_max, th.l3_max);
}
