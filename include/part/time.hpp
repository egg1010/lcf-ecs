#pragma once


#include <chrono>
#include <cstdint>
#include "force_inline.hpp"


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
// timer: 统一计时器 (墙钟 + CPU 周期, 精度优先)
// 构造时双 now(): 先 rdtscp (序列化, 干净起点), 后 steady_clock (单调, 绝对时间)
// 成员声明顺序决定初始化顺序: cstart_ 先, wstart_ 后
// 调度器 (timer::scheduler + 时间字面量) 已分离至 timer_scheduler.hpp
// =============================================================================

class timer
{
    using clock = std::chrono::steady_clock;
    uint64_t cstart_;            // 先声明: rdtscp 序列化, 确保起点干净
    clock::time_point wstart_;   // 后声明: steady_clock 单调, 绝对时间

public:
    // 终点快照 (一次性返回墙钟 + 周期, 避免多次 now() 调用引入额外开销)
    struct snapshot
    {
        double nanoseconds;
        uint64_t cycles;
    };

    timer() noexcept
        : cstart_(rdtscp()), wstart_(clock::now()) {}

    void reset() noexcept
    {
        cstart_ = rdtscp();
        wstart_ = clock::now();
    }

    // 墙钟已逝时间 (绝对时间, 单调递增, 适合日志/阈值判断)
    [[nodiscard]] double elapsed_nanoseconds() const noexcept
    {
        return std::chrono::duration<double, std::nano>(clock::now() - wstart_).count();
    }

    [[nodiscard]] double elapsed_microseconds() const noexcept
    {
        return std::chrono::duration<double, std::micro>(clock::now() - wstart_).count();
    }

    [[nodiscard]] double elapsed_milliseconds() const noexcept
    {
        return std::chrono::duration<double, std::milli>(clock::now() - wstart_).count();
    }

    [[nodiscard]] double elapsed_seconds() const noexcept
    {
        return std::chrono::duration<double>(clock::now() - wstart_).count();
    }

    // CPU 周期 (相对比较, 精度最高, 非 x86 返回 0)
    [[nodiscard]] uint64_t elapsed_cycles() const noexcept
    {
        return rdtscp() - cstart_;
    }

    // 一次性快照 (精度优先): 终点先 rdtscp (精确), 后 clock::now
    // cycles 不含 clock::now 开销; nanoseconds 含 rdtscp 开销 (固定, 相对比较可抵消)
    [[nodiscard]] snapshot take_snapshot() const noexcept
    {
        uint64_t cend = rdtscp();
        double ns_val = std::chrono::duration<double, std::nano>(clock::now() - wstart_).count();
        return {ns_val, cend - cstart_};
    }

    // 当前 CPU 周期计数 (绝对值, 非 x86 返回 0)
    [[nodiscard]] static uint64_t now_cycles() noexcept
    {
        return rdtscp();
    }

    // 当前墙钟纳秒 (绝对值, 单调递增, 自 epoch 起)
    [[nodiscard]] static double now_nanoseconds() noexcept
    {
        return std::chrono::duration<double, std::nano>(
            clock::now().time_since_epoch()).count();
    }

    // 配对计时: 返回 fn 执行的墙钟纳秒
    template<typename F>
    [[nodiscard]] static double measure_nanoseconds(F&& fn) noexcept
    {
        auto t0 = clock::now();
        fn();
        auto t1 = clock::now();
        return std::chrono::duration<double, std::nano>(t1 - t0).count();
    }

    // 配对计时: 返回 fn 执行的 CPU 周期数 (非 x86 返回 0)
    template<typename F>
    [[nodiscard]] static uint64_t measure_cycles(F&& fn) noexcept
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
};
