#pragma once
#include "class_pool.hpp"
#include <stack>

template <typename T=size_t>
class id_allocation
{
private:
    T next_id_{0};
    std::stack<T, class_pool<T>> recycled_ids_;
public:
    [[nodiscard]] T get_id() noexcept
    {
        if (!recycled_ids_.empty()) [[likely]]
        {
            T id = recycled_ids_.top();
            recycled_ids_.pop();
            return id;
        }
        return ++next_id_;
    }
    
    void free_id(T id) noexcept
    {
        recycled_ids_.emplace(id);
    }
    
    [[nodiscard]] T total_number_of_ids() const noexcept
    { 
        return recycled_ids_.size(); 
    }
    
    [[nodiscard]] T maximum_id() const noexcept
    { 
        return next_id_; 
    }
};