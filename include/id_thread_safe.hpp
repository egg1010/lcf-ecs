#pragma once
#include <cstdint>
#include <atomic>
#include <vector>
#include <stack>
#include <mutex>

template <typename T=size_t>
class id_allocation_t
{
private:
    std::atomic<T> next_id_{1};
    mutable std::mutex recycled_mutex_;
    std::stack<T, std::vector<T>> recycled_ids_;
public:
    T get_id()
    {
        {
            std::lock_guard<std::mutex> lock(recycled_mutex_);
            if (!recycled_ids_.empty()) 
            {
                T id = recycled_ids_.top();
                recycled_ids_.pop();
                return id;
            }
        }
        
        return next_id_.fetch_add(1);
    }
    
    void free_id(T id)
    {
        if (id != 0) 
        {
            std::lock_guard<std::mutex> lock(recycled_mutex_);
            recycled_ids_.push(id);
        }
    }
    
    T total_number_of_ids() const 
    { 
        std::lock_guard<std::mutex> lock(recycled_mutex_);
        return recycled_ids_.size(); 
    }
    
    T maximum_id() const 
    { 
        return next_id_.load() - 1; 
    }
};

