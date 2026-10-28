#pragma once

// signal_queue.hpp - 信号/变更日志统一队列

#include "ring_buffer.hpp"
#include "dense.hpp"
#include <cstddef>
#include <utility>

template <typename Event, size_t Capacity>
class signal_queue
{
public:
    signal_queue() noexcept = default;
    signal_queue(signal_queue&&) noexcept = default;
    signal_queue& operator=(signal_queue&&) noexcept = default;
    signal_queue(const signal_queue&) = delete;
    signal_queue& operator=(const signal_queue&) = delete;

    // 入队不丢事件: 主环满则落溢出缓冲, 保持先进先出顺序
    //   溢出未排空时先进溢出, 防止新事件越过积压乱序
    void push(const Event& event) noexcept
    {
        if (!enabled_) [[unlikely]] return;
        if (overflow_read_ < overflow_.size() || !buffer_.try_push(event)) [[unlikely]]
        {
            overflow_.emplace_back(event);
        }
    }

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void set_enabled(bool value) noexcept { enabled_ = value; }

    [[nodiscard]] bool has_pending() const noexcept
    {
        return !buffer_.empty() || overflow_read_ < overflow_.size();
    }

    [[nodiscard]] size_t pending_count() const noexcept
    {
        return buffer_.size() + (overflow_.size() - overflow_read_);
    }

    // 预算 = 主环容量 × 4; 先主环后溢出共享预算, 重入或禁用期间无操作
    //   溢出用读偏移消费, 排空后整体清空, 全程零搬移
    template <typename Handler>
    void flush(Handler&& handler) noexcept
    {
        if (flushing_) [[unlikely]] return;
        flushing_ = true;
        size_t budget = Capacity * 4;
        budget -= buffer_.drain_with_budget(budget, [&](Event& ev) noexcept { handler(ev); });
        while (budget > 0 && overflow_read_ < overflow_.size())
        {
            handler(overflow_[overflow_read_]);
            ++overflow_read_;
            --budget;
        }
        if (overflow_read_ == overflow_.size()) [[unlikely]]
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
    bool enabled_{true};
    bool flushing_{false};
};
