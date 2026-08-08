#pragma once


#include <chrono>
#include <cstdint>
#include "force_inline.hpp"
#include "dense.hpp"
#include "t_fun.hpp"


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
// 时间字面量 (返回纳秒值, 配合 timer::scheduler 使用)
// 用法: sched.schedule_after(100_ms, task);  sched.schedule_every(16_ms, task);
// =============================================================================

inline constexpr double operator""_ns(unsigned long long n) noexcept
{
    return static_cast<double>(n);
}

inline constexpr double operator""_us(unsigned long long n) noexcept
{
    return static_cast<double>(n) * 1'000.0;
}

inline constexpr double operator""_ms(unsigned long long n) noexcept
{
    return static_cast<double>(n) * 1'000'000.0;
}

inline constexpr double operator""_sec(unsigned long long n) noexcept
{
    return static_cast<double>(n) * 1'000'000'000.0;
}

// =============================================================================
// timer: 统一计时器 (墙钟 + CPU 周期, 精度优先)
// 构造时双 now(): 先 rdtscp (序列化, 干净起点), 后 steady_clock (单调, 绝对时间)
// 成员声明顺序决定初始化顺序: cstart_ 先, wstart_ 后
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

    // ===== 静态原语 =====

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

    // ===== 定时调度器 =====
    class scheduler;
};

// =============================================================================
// timer::scheduler: 定时触发器 (复用 t_fun, 线性扫描, 惰性删除)
// 适合游戏帧调度 / 科学计算检查点 / 延迟回调
// tick() 单次 now() + 线性扫描, 定时器数量少时缓存友好
// =============================================================================

class timer::scheduler
{
public:
    using timer_id = size_t;
    static constexpr timer_id invalid_id = static_cast<timer_id>(-1);

private:
    struct timer_entry
    {
        double next_trigger_ns;    // 下次触发的绝对纳秒戳
        double period_ns;          // 周期 (0 = 一次性)
        t_fun<void()> task;        // 回调 (CTAD: 用户传 void(*)() 自动构造)
        bool active;               // 惰性删除标记

        timer_entry(double trigger, double period, t_fun<void()> t, bool act) noexcept
            : next_trigger_ns(trigger), period_ns(period), task(t), active(act) {}
    };

    dense<timer_entry> timers_;

public:
    scheduler() noexcept = default;

    // 一次性: delay_ns 纳秒后触发一次 (可用字面量 100_ms / 1_sec 等)
    // task 可传 void(*)() 函数指针, CTAD 自动推导为 t_fun<void()>
    timer_id schedule_after(double delay_ns, t_fun<void()> task) noexcept
    {
        double now = timer::now_nanoseconds();
        timers_.increase_capacity(timers_.size() + 1);
        timers_.push_back(timer_entry(now + delay_ns, 0.0, task, true));
        return timers_.size() - 1;
    }

    // 周期性: 每 period_ns 纳秒触发 (游戏帧调度 / 检查点, 可用字面量 16_ms 等)
    // task 可传 void(*)() 函数指针, CTAD 自动推导为 t_fun<void()>
    timer_id schedule_every(double period_ns, t_fun<void()> task) noexcept
    {
        double now = timer::now_nanoseconds();
        timers_.increase_capacity(timers_.size() + 1);
        timers_.push_back(timer_entry(now + period_ns, period_ns, task, true));
        return timers_.size() - 1;
    }

    // 取消定时器 (O(1) 惰性标记, timer_id 不失效)
    void cancel(timer_id id) noexcept
    {
        if (id < timers_.size())
        {
            timers_[id].active = false;
        }
    }

    // 推进时间, 触发所有到期回调 (主循环每帧/每步调用)
    // 单次 now() + 线性扫描, 周期定时器用累加避免漂移
    void tick() noexcept
    {
        double now = timer::now_nanoseconds();
        for (size_t i = 0; i < timers_.size(); ++i)
        {
            if (!timers_[i].active)
            {
                continue;
            }
            if (now >= timers_[i].next_trigger_ns)
            {
                timers_[i].task();
                if (timers_[i].period_ns > 0.0)
                {
                    // 周期性: 累加下次触发 (避免漂移)
                    timers_[i].next_trigger_ns += timers_[i].period_ns;
                }
                else
                {
                    // 一次性: 惰性标记
                    timers_[i].active = false;
                }
            }
        }
    }

    // 整理碎片 (清理已取消的惰性槽位)
    // 注意: 调用后 timer_id 可能失效, 需重新获取
    void compact() noexcept
    {
        size_t write = 0;
        for (size_t read = 0; read < timers_.size(); ++read)
        {
            if (timers_[read].active)
            {
                if (write != read)
                {
                    timers_[write] = std::move(timers_[read]);
                }
                ++write;
            }
        }
        while (timers_.size() > write)
        {
            timers_.pop_back();
        }
    }

    // 挂起定时器数量 (含惰性未清理的)
    [[nodiscard]] size_t pending_count() const noexcept
    {
        return timers_.size();
    }

    // 清空所有定时器
    void clear() noexcept
    {
        timers_.clear();
    }
};
