#pragma once
#include "class_pool.hpp"
#include <stack>

#define FORCE_INLINE inline

template <typename T=size_t>
class id_allocation
{
private:
    T next_id_{0};
    std::stack<T, class_pool<T>> recycled_ids_;
public:
    [[nodiscard]] FORCE_INLINE
    T get_id() noexcept
    {
        if (!recycled_ids_.empty()) [[likely]]
        {
            T id = recycled_ids_.top();
            recycled_ids_.pop();
            return id;
        }
        return ++next_id_;
    }
    
    FORCE_INLINE
    void free_id(T id) noexcept
    {
        recycled_ids_.emplace(id);
    }
    
    [[nodiscard]] FORCE_INLINE
    T total_number_of_ids() const noexcept
    { 
        return recycled_ids_.size(); 
    }
    
    [[nodiscard]] FORCE_INLINE
    T maximum_id() const noexcept
    { 
        return next_id_; 
    }
};
