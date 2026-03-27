#pragma once
#include <vector>
#include <stack>

template <typename T=size_t>
class id_allocation
{
private:
    T next_id_{0};
    std::stack<T, std::vector<T>> recycled_ids_;
public:
    T get_id()
    {
        if (!recycled_ids_.empty()) 
        {
            T id = recycled_ids_.top();
            recycled_ids_.pop();
            return id;
        }
        
        return next_id_++;
    }
    
    void free_id(T id)
    {
        recycled_ids_.push(id);
    }
    
    T total_number_of_ids() const 
    { 
        return recycled_ids_.size(); 
    }
    
    T maximum_id() const 
    { 
        return next_id_; 
    }
};