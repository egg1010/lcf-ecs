#pragma once
#include "dense.hpp"
#include "force_inline.hpp"

template <typename T = size_t>
class id_allocation
{
private:
    T next_id_{0};
    dense<T> recycled_ids_;
public:
    id_allocation() noexcept
    {
        recycled_ids_.increase_capacity(256);
    }

    [[nodiscard]] FORCE_INLINE
    T get_id() noexcept
    {
        if (!recycled_ids_.empty()) [[likely]]
        {
            T id = recycled_ids_.back();
            recycled_ids_.pop_back();
            return id;
        }
        return ++next_id_;
    }

    FORCE_INLINE
    void free_id(T id) noexcept
    {
        recycled_ids_.emplace_back(id);
    }

    [[nodiscard]] FORCE_INLINE
    T total_number_of_ids() const noexcept
    {
        return static_cast<T>(recycled_ids_.size());
    }

    [[nodiscard]] FORCE_INLINE
    T maximum_id() const noexcept
    {
        return next_id_;
    }
};