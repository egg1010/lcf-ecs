// timer_scheduler.hpp - 定时调度器 (timer::scheduler + 时间字面量)
// 从 time.hpp 分离: 调度器依赖 dense/t_fun, 计时器本体零容器依赖
#pragma once

#include <chrono>
#include <cstdint>
#include <cstddef>
#include "dense.hpp"
#include "t_fun.hpp"
#include "time.hpp"

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
