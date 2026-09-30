#pragma once

// signal_queue.hpp - 信号/变更日志统一队列

#include "ring_buffer.hpp"
#include "dense.hpp"
#include <cstddef>
#include <cstdint>
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

    // 环形缓冲优先, 满则追加到溢出链
    void push(const Event& event) noexcept
    {
        if (!enabled_) [[unlikely]] return;
        if (!buffer_.push(event)) [[unlikely]]
        {
            ++overflow_count_;
            overflow_.push_back(event);
        }
    }

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void set_enabled(bool value) noexcept { enabled_ = value; }

    [[nodiscard]] bool has_pending() const noexcept
    {
        return buffer_.has_pending() || overflow_read_ < overflow_.size();
    }

    [[nodiscard]] uint64_t overflow_count() const noexcept { return overflow_count_; }
    void reset_overflow_count() noexcept { overflow_count_ = 0; }

    void reserve_overflow(size_t count) noexcept { overflow_.increase_capacity(count); }

    // 预算 = 环形容量 × 4 + 溢出链长度; 重入或禁用期间无操作
    template <typename Handler>
    void flush(Handler&& handler) noexcept
    {
        if (flushing_) [[unlikely]] return;
        flushing_ = true;
        uint64_t budget = Capacity * 4 + overflow_.size();
        const size_t processed = buffer_.drain_with_budget(
            static_cast<size_t>(budget), [&](const Event& ev) noexcept { handler(ev); });
        budget -= processed;
        while (budget > 0 && overflow_read_ < overflow_.size())
        {
            handler(overflow_[overflow_read_]);
            ++overflow_read_;
            --budget;
        }
        if (overflow_read_ == overflow_.size() && overflow_.size() > 0)
        {
            overflow_.clear();
            overflow_read_ = 0;
        }
        flushing_ = false;
    }

private:
    ring_buffer<Event, Capacity> buffer_;
    dense<Event> overflow_;
    size_t overflow_read_{0};
    uint64_t overflow_count_{0};
    bool enabled_{true};
    bool flushing_{false};
};
