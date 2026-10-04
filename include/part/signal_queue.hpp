#pragma once

// signal_queue.hpp - 信号/变更日志统一队列

#include "ring_buffer.hpp"
#include <cstddef>
#include <utility>

template <typename Event, size_t Capacity>
class signal_queue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "signal_queue capacity must be power of 2");

public:
    signal_queue() noexcept = default;
    signal_queue(signal_queue&&) noexcept = default;
    signal_queue& operator=(signal_queue&&) noexcept = default;
    signal_queue(const signal_queue&) = delete;
    signal_queue& operator=(const signal_queue&) = delete;

    // 入队不丢事件 (底层 ring_buffer 无界扩展), Capacity 为 chunk 尺寸非上限
    void push(const Event& event) noexcept
    {
        if (!enabled_) [[unlikely]] return;
        (void)buffer_.push(event);
    }

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void set_enabled(bool value) noexcept { enabled_ = value; }

    [[nodiscard]] bool has_pending() const noexcept { return buffer_.has_pending(); }

    [[nodiscard]] size_t pending_count() const noexcept { return buffer_.pending_count(); }

    // 预算 = 环形容量 × 4; 重入或禁用期间无操作
    //   单轮处理量受限, 需多轮 flush 才能排空
    template <typename Handler>
    void flush(Handler&& handler) noexcept
    {
        if (flushing_) [[unlikely]] return;
        flushing_ = true;
        buffer_.drain_with_budget(Capacity * 4, [&](const Event& ev) noexcept { handler(ev); });
        flushing_ = false;
    }

private:
    ring_buffer<Event, Capacity> buffer_;
    bool enabled_{true};
    bool flushing_{false};
};
