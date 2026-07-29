#pragma once
#include <atomic>
#include "dense.hpp"
#include "force_inline.hpp"

template <typename T = size_t>
class id_allocation
{
private:
    std::atomic<T> next_id_{0};
    dense<T> recycled_ids_;
public:
    id_allocation() noexcept
    {
        recycled_ids_.increase_capacity(256);
    }

    // 新分配路径线程安全 (fetch_add), 回收路径非线程安全
    // 语义等价于 ++next_id_ (返回自增后的新值, 从 1 开始, 0 保留给 "无效")
    [[nodiscard]] FORCE_INLINE
    T get_id() noexcept
    {
        if (!recycled_ids_.empty()) [[likely]]
        {
            T id = recycled_ids_.back();
            recycled_ids_.pop_back();
            return id;
        }
        return next_id_.fetch_add(1, std::memory_order_relaxed) + T{1};
    }

    FORCE_INLINE
    void free_id(T id) noexcept
    {
        recycled_ids_.push_back(id);
    }

    [[nodiscard]] FORCE_INLINE
    T total_number_of_ids() const noexcept
    {
        return static_cast<T>(recycled_ids_.size());
    }

    [[nodiscard]] FORCE_INLINE
    T maximum_id() const noexcept
    {
        return next_id_.load(std::memory_order_relaxed);
    }
};
