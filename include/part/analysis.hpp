#pragma once

// analysis.hpp - 微架构分析 (缓存测量 / 内存屏障 / 地址生成)
// analyzer 类: 统一所有原语与测量接口
// x86/x64 全套支持; 其他平台屏障为编译器屏障, 缓存测量回退到周期计数

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include "force_inline.hpp"
#include "dense.hpp"
#include "tiered_sort.hpp"
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
// analyzer: 微架构分析器 (屏障 / 缓存 / 周期 / 地址 / 测量)
// 静态方法=无状态原语; 实例方法=持有 config 的一站式测量
// =============================================================================

class analyzer
{
public:
    // ===== 配置: 缓存层级阈值 (1/2/3/4 级可配置) =====
    struct config
    {
        double l1_max = 4.0;       // L1 命中: <= 4 周期
        double l2_max = 12.0;      // L2 命中: <= 12 周期
        double l3_max = 40.0;      // L3 命中: <= 40 周期
        double l4_max = 150.0;     // L4/DRAM 命中: <= 150 周期 (服务器/嵌入式可选)
        int    cache_levels = 3;   // 实际层级数 (1/2/3/4)
    };

    // ===== 报告结构 =====
    struct cache_report
    {
        size_t total = 0;          // 总访问次数
        size_t l1_hits = 0;
        size_t l2_hits = 0;
        size_t l3_hits = 0;
        size_t l4_hits = 0;        // 第 4 级命中 (cache_levels >= 4 时有效)
        size_t misses = 0;         // 主存访问
        double l1_hit_rate = 0;
        double l2_hit_rate = 0;
        double l3_hit_rate = 0;
        double l4_hit_rate = 0;
        double miss_rate = 0;
        double avg_cycles = 0;
        double p50_cycles = 0;
        double p95_cycles = 0;
        double p99_cycles = 0;
        int active_levels = 3;
    };

    struct batch_result
    {
        double total_cycles = 0;
        double avg_cycles_per_access = 0;
        double baseline_cycles = 0;
        double net_cycles_per_access = 0;
        size_t repeats = 0;
    };

    // 地址视图: 零分配 POD
    struct address_view
    {
        const void* const* addrs;
        size_t count;

        [[nodiscard]] const void* operator[](size_t i) const noexcept { return addrs[i]; }
        [[nodiscard]] size_t size() const noexcept { return count; }
        [[nodiscard]] bool empty() const noexcept { return count == 0; }
    };

    // ===== 构造 =====
    analyzer() noexcept = default;
    explicit analyzer(const config& c) noexcept : config_(c) {}

    [[nodiscard]] const config& get_config() const noexcept { return config_; }
    void set_config(const config& c) noexcept { config_ = c; }

    // ===== L1: 内存屏障原语 (x86/x64 全套; 其他平台编译器屏障) =====

    static FORCE_INLINE void mfence() noexcept
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

    static FORCE_INLINE void lfence() noexcept
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

    static FORCE_INLINE void sfence() noexcept
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

    // ===== L2: 缓存控制 (x86/x64 clflush; 其他平台空操作) =====

    static FORCE_INLINE void cache_flush(const void* p) noexcept
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
    static inline void cache_flush_range(const void* p, size_t bytes) noexcept
    {
#if ANALYSIS_HAS_X86
        const char* ptr = static_cast<const char*>(p);
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

    // ===== L3: 序列化周期测量 (lfence; rdtsc; lfence, Intel 推荐顺序) =====

    // 全屏障周期测量 (适合精确测量短代码段)
    [[nodiscard]] static FORCE_INLINE uint64_t now_cycles_fenced() noexcept
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

    // 全屏障配对计时 (lfence; rdtsc 包裹, 排除乱序干扰)
    template<typename F>
    [[nodiscard]] static uint64_t measure_cycles_fenced(F&& fn) noexcept
    {
#if ANALYSIS_HAS_X86
        uint64_t c0 = now_cycles_fenced();
        fn();
        uint64_t c1 = now_cycles_fenced();
        return c1 - c0;
#else
        fn();
        return 0;
#endif
    }

    // 单次循环周期测量 (rdtscp 包裹, 开销更低, 适合循环总开销测量)
    template<typename F>
    [[nodiscard]] static uint64_t measure_loop_cycles(F&& fn) noexcept
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

    // ===== L4: 地址生成 (顺序 / 随机, 确定性可复现) =====

    [[nodiscard]] static inline dense<const void*> make_sequential_addresses(
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

    // 随机地址序列 (xorshift64 PRNG, 确定性, 无 std 依赖)
    [[nodiscard]] static inline dense<const void*> make_random_addresses(
        const void* base, size_t count, size_t stride, uint64_t seed) noexcept
    {
        dense<const void*> result;
        result.reserve_exact(count);
        const char* ptr = static_cast<const char*>(base);
        uint64_t state = seed ? seed : 0x9E3779B97F4A7C15ULL;
        for (size_t i = 0; i < count; ++i)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            size_t idx = static_cast<size_t>(state % count);
            result.push_back(static_cast<const void*>(ptr + idx * stride));
        }
        return result;
    }

    // ===== L5: 缓存命中测量 (实例方法, 使用 config_ 阈值) =====

    // 便捷重载: 内部生成顺序地址 + 测量 + 分类
    [[nodiscard]] inline cache_report measure_hits(
        const void* base, size_t count, size_t stride) const noexcept
    {
        auto addrs = make_sequential_addresses(base, count, stride);
        address_view av{addrs.data(), addrs.size()};
        return measure_hits(av);
    }

    // address_view 重载 (复用预生成地址)
    [[nodiscard]] inline cache_report measure_hits(const address_view& addrs) const noexcept
    {
        cache_report r;
        r.total = addrs.count;
        r.active_levels = config_.cache_levels;
        if (addrs.count == 0)
        {
            return r;
        }

        // 基线 (rdtscp 配对开销, 多次取最小)
        uint64_t baseline = static_cast<uint64_t>(-1);
        for (size_t i = 0; i < 100; ++i)
        {
            uint64_t b = measure_baseline_();
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
            uint64_t raw = measure_one_access_(addrs[i]);
            uint64_t cyc = (raw > baseline) ? raw - baseline : 0;
            double c = static_cast<double>(cyc);
            samples.push_back(c);
            sum += c;

            switch (classify_latency_(cyc))
            {
                case 0: ++r.l1_hits; break;
                case 1: ++r.l2_hits; break;
                case 2: ++r.l3_hits; break;
                case 3: ++r.l4_hits; break;
                default: ++r.misses; break;
            }
        }

        double n = static_cast<double>(addrs.count);
        r.avg_cycles = sum / n;
        r.l1_hit_rate = static_cast<double>(r.l1_hits) / n;
        r.l2_hit_rate = static_cast<double>(r.l2_hits) / n;
        r.l3_hit_rate = static_cast<double>(r.l3_hits) / n;
        r.l4_hit_rate = static_cast<double>(r.l4_hits) / n;
        r.miss_rate = static_cast<double>(r.misses) / n;

        double* p = samples.data();
        sort(p, samples.size());
        compute_pct_(p, samples.size(), r);
        return r;
    }

    // ===== L6: 批量测量 (多次取最优, 扣除基线) =====

    [[nodiscard]] inline batch_result measure_batch(
        const address_view& addrs, size_t repeats = 10) const noexcept
    {
        batch_result r;
        r.repeats = repeats;
        if (addrs.count == 0 || repeats == 0)
        {
            return r;
        }

#if ANALYSIS_HAS_X86
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
                total += measure_one_access_(addrs[i]);
            }
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

        double sum_best = 0;
        for (size_t i = 0; i < BEST_COUNT; ++i)
        {
            sum_best += static_cast<double>(best_times[i]);
        }
        double avg_total = sum_best / static_cast<double>(BEST_COUNT);
        r.total_cycles = avg_total;
        r.avg_cycles_per_access = avg_total / static_cast<double>(addrs.count);

        uint64_t best_baseline = static_cast<uint64_t>(-1);
        constexpr size_t BASELINE_ITERS = 1000;
        for (size_t i = 0; i < BASELINE_ITERS; ++i)
        {
            uint64_t b = measure_baseline_();
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
        (void)repeats;
#endif
        return r;
    }

    // ===== L7: 自适应缓存层级检测 (1KB → 16MB 步进扫描) =====
    // 返回检测到的 config, cache_levels 表示实际层级数 (1/2/3/4)
    [[nodiscard]] static inline config detect_config() noexcept
    {
        config th;

#if ANALYSIS_HAS_X86
        constexpr size_t SCAN_SIZES[] = {
            1024, 2048, 4096, 8192, 16384, 32768, 65536,
            131072, 262144, 524288, 1048576, 2097152,
            4194304, 8388608, 16777216
        };
        constexpr size_t SCAN_COUNT = sizeof(SCAN_SIZES) / sizeof(SCAN_SIZES[0]);

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
            size_t count = sz / 64;
            if (count == 0)
            {
                count = 1;
            }

            auto addrs = make_sequential_addresses(scan_buf, count, 64);
            address_view av{addrs.data(), addrs.size()};

            constexpr size_t WARM_REPEATS = 3;
            double best_avg = 1e18;
            for (size_t rep = 0; rep < WARM_REPEATS; ++rep)
            {
                for (size_t j = 0; j < av.size(); ++j)
                {
                    volatile uint8_t v = *static_cast<const volatile uint8_t*>(av[j]);
                    (void)v;
                }
                double sum = 0;
                for (size_t j = 0; j < av.size(); ++j)
                {
                    uint64_t raw = measure_one_access_(av[j]);
                    sum += static_cast<double>(raw);
                }
                double avg = sum / static_cast<double>(av.size());
                if (avg < best_avg)
                {
                    best_avg = avg;
                }
            }
            uint64_t baseline = static_cast<uint64_t>(-1);
            for (size_t b = 0; b < 100; ++b)
            {
                uint64_t v = measure_baseline_();
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

        // 分析延迟跳变点 (跳变判定: 相对增量 > 50% 且绝对增量 > 3 周期)
        if (results.size() >= 3)
        {
            double l1_threshold = results[0].avg_cycles;
            double l2_threshold = l1_threshold;
            double l3_threshold = l2_threshold;
            double l4_threshold = l3_threshold;
            int levels = 1;

            for (size_t i = 1; i < results.size(); ++i)
            {
                double prev = results[i - 1].avg_cycles;
                double cur = results[i].avg_cycles;
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
                    else if (levels == 3)
                    {
                        l4_threshold = cur;
                        l3_threshold = prev;
                        levels = 4;
                    }
                }
            }

            th.l1_max = l1_threshold + 1.0;
            th.l2_max = (levels >= 2) ? l2_threshold + 2.0 : th.l2_max;
            th.l3_max = (levels >= 3) ? l3_threshold + 5.0 : th.l3_max;
            th.l4_max = (levels >= 4) ? l4_threshold + 10.0 : th.l4_max;
            th.cache_levels = levels;
        }
#else
        // 非 x86: 返回默认阈值, 无法检测
#endif

        return th;
    }

    // ===== L8: 报告打印 (格式化输出到 stdout) =====

    static inline void print_report(const char* label, const cache_report& r) noexcept
    {
        std::printf("  %-28s | L1:%5.1f%%  L2:%5.1f%%  L3:%5.1f%%  Miss:%5.1f%%"
                    " | avg=%5.1f  p50=%5.1f  p95=%5.1f  p99=%5.1f  cyc  levels=%d\n",
                    label,
                    r.l1_hit_rate * 100, r.l2_hit_rate * 100,
                    r.l3_hit_rate * 100, r.miss_rate * 100,
                    r.avg_cycles, r.p50_cycles, r.p95_cycles, r.p99_cycles,
                    r.active_levels);
    }

    static inline void print_batch(const char* label, const batch_result& r) noexcept
    {
        std::printf("  %-28s | total=%8.1f  avg=%5.1f  baseline=%4.1f  net=%5.1f  cyc/access"
                    "  (repeats=%zu)\n",
                    label, r.total_cycles, r.avg_cycles_per_access,
                    r.baseline_cycles, r.net_cycles_per_access, r.repeats);
    }

    static inline void print_config(const char* label, const config& c) noexcept
    {
        std::printf("  %-28s | levels=%d  L1<=%.1f  L2<=%.1f  L3<=%.1f  L4<=%.1f  cyc\n",
                    label, c.cache_levels, c.l1_max, c.l2_max, c.l3_max, c.l4_max);
    }

private:
    config config_;

    // 单次访问延迟测量 (rdtscp 配对, lfence 序列化)
    [[nodiscard]] static FORCE_INLINE uint64_t measure_one_access_(const void* p) noexcept
    {
#if ANALYSIS_HAS_X86
        uint64_t c0, c1;
        #if defined(_MSC_VER)
            _mm_lfence();
            c0 = __rdtscp(nullptr);
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
    [[nodiscard]] static FORCE_INLINE uint64_t measure_baseline_() noexcept
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

    // 根据周期数和 config_ 判定层级 (0=L1, 1=L2, 2=L3, 3=L4, 4=Miss)
    [[nodiscard]] inline int classify_latency_(uint64_t cycles) const noexcept
    {
        double c = static_cast<double>(cycles);
        if (c <= config_.l1_max)
        {
            return 0;
        }
        if (config_.cache_levels >= 2 && c <= config_.l2_max)
        {
            return 1;
        }
        if (config_.cache_levels >= 3 && c <= config_.l3_max)
        {
            return 2;
        }
        if (config_.cache_levels >= 4 && c <= config_.l4_max)
        {
            return 3;
        }
        return 4;
    }

    // 已排序裸指针的百分位计算
    static inline void compute_pct_(double* sorted, size_t n, cache_report& r) noexcept
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
};
