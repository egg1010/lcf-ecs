#pragma once
#include <new>
#include <cstddef> 
#include <cstring> 
#include <algorithm>
#include <type_traits>  
#include <memory>
#include <iterator>
#include <limits>
#include <stdexcept>



template <typename T>
class class_pool
{
private:
    T* data_ptr_{nullptr};
    size_t maximum_quantity_{0}; // 分配的总容量（元素个数）
    size_t usage_{0};     // 实际使用的大小（元素个数）

public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    class_pool() noexcept
    : maximum_quantity_(8)
    , usage_(0)
    {
        size_t total_bytes = maximum_quantity_ * sizeof(T);
        data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
    }

    class_pool(size_t count, const T& value) noexcept
        : maximum_quantity_(count > 0 ? count : 8)
    {
        if (maximum_quantity_ > 0) 
        {
            size_t total_bytes = maximum_quantity_ * sizeof(T);
            data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
            if (count > 0) 
            {
                for (size_t i = 0; i < count; ++i) 
                {
                    new (&data_ptr_[i]) T(value);
                }
                usage_ = count;
            }
        }
    }

    template <typename InputIt>
    class_pool(InputIt first, InputIt last) noexcept
    : usage_(0)
    {
        size_t count = std::distance(first, last);
        maximum_quantity_ = count > 0 ? count : 8;
        
        if (maximum_quantity_ > 0) 
        {
            size_t total_bytes = maximum_quantity_ * sizeof(T);
            data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
            if (count > 0) 
            {
                size_t i = 0;
                for (auto it = first; it != last; ++it, ++i) 
                {
                    new (&data_ptr_[i]) T(*it);
                }
                usage_ = count;
            }
        }
    }

    explicit class_pool(size_t capacity) noexcept
        : maximum_quantity_(capacity)
        , usage_(0)
    {
        if (capacity > 0) 
        {
            size_t total_bytes = capacity * sizeof(T);
            data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
        }
    }

    class_pool(const class_pool& other) noexcept
        : maximum_quantity_(other.maximum_quantity_)
        , usage_(other.usage_)
    {
        if (maximum_quantity_ > 0) 
        {
            size_t total_bytes = maximum_quantity_ * sizeof(T);
            data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
            
            if constexpr (std::is_trivially_copyable_v<T>) 
            {
                std::memcpy(data_ptr_, other.data_ptr_, usage_ * sizeof(T));
            } 
            else 
            {
                for (size_t i = 0; i < usage_; ++i) 
                {
                    new (&data_ptr_[i]) T(other.data_ptr_[i]);
                }
            }
        }
    }

    class_pool& operator=(const class_pool& other) noexcept
    {
        if (this != &other) 
        {
            if (data_ptr_ != nullptr) 
            {
                if constexpr (!std::is_trivially_destructible_v<T>) 
                {
                    for (size_t i = 0; i < usage_; ++i) 
                    {
                        data_ptr_[i].~T();
                    }
                }
                size_t total_bytes = maximum_quantity_ * sizeof(T);
                ::operator delete(data_ptr_, total_bytes, std::align_val_t{alignof(T)});
            }
            
            maximum_quantity_ = other.maximum_quantity_;
            usage_ = other.usage_;
            
            if (maximum_quantity_ > 0) 
            {
                size_t total_bytes = maximum_quantity_ * sizeof(T);
                data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
                
                if constexpr (std::is_trivially_copyable_v<T>) 
                {
                    std::memcpy(data_ptr_, other.data_ptr_, usage_ * sizeof(T));
                } 
                else 
                {
                    for (size_t i = 0; i < usage_; ++i) 
                    {
                        new (&data_ptr_[i]) T(other.data_ptr_[i]);
                    }
                }
            }
            else 
            {
                data_ptr_ = nullptr;
            }
        }
        return *this;
    }

    class_pool(class_pool&& other) noexcept
        : data_ptr_(other.data_ptr_)
        , maximum_quantity_(other.maximum_quantity_)
        , usage_(other.usage_)
    {
        other.data_ptr_ = nullptr;
        other.maximum_quantity_ = 0;
        other.usage_ = 0;
    }

    class_pool& operator=(class_pool&& other) noexcept
    {
        if (this != &other) 
        {
            if (data_ptr_ != nullptr) 
            {
                if constexpr (!std::is_trivially_destructible_v<T>) 
                {
                    for (size_t i = 0; i < usage_; ++i) 
                    {
                        data_ptr_[i].~T();
                    }
                }
                size_t total_bytes = maximum_quantity_ * sizeof(T);
                ::operator delete(data_ptr_, total_bytes, std::align_val_t{alignof(T)});
            }
            
            data_ptr_ = other.data_ptr_;
            maximum_quantity_ = other.maximum_quantity_;
            usage_ = other.usage_;
            
            other.data_ptr_ = nullptr;
            other.maximum_quantity_ = 0;
            other.usage_ = 0;
        }
        return *this;
    }
    
    template <typename... Args>
    void emplace_back(Args&&... args) noexcept
    {
        static_assert(std::is_constructible_v<T, Args...>, 
                     "T must be constructible from the provided arguments");
        
        if (usage_ >= maximum_quantity_)
        {
            size_t new_capacity = maximum_quantity_ * 4;
            if (new_capacity == maximum_quantity_) 
            { 
                new_capacity = maximum_quantity_ + 1;
            }
            resize(new_capacity);
        }
        new (&data_ptr_[usage_]) T(std::forward<Args>(args)...);
        ++usage_;
    }

    ~class_pool() noexcept
    {
        if (data_ptr_ != nullptr) 
        {
            if constexpr (!std::is_trivially_destructible_v<T>) 
            {
                for (size_t i = 0; i < usage_; ++i) 
                {
                    data_ptr_[i].~T();
                }
            }
            size_t total_bytes = maximum_quantity_ * sizeof(T);
            ::operator delete(data_ptr_, total_bytes, std::align_val_t{alignof(T)});
        }
    }

    void clear() noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<T>) 
        {
            for (size_t i = 0; i < usage_; ++i) 
            {
                data_ptr_[i].~T();
            }
        }
        usage_ = 0;
    }
    

    T* get(size_t index) noexcept 
    { 
        return &data_ptr_[index];
    }
    
    size_t capacity() const noexcept { return maximum_quantity_; }

    size_t max_size() const noexcept 
    { 
        return std::numeric_limits<size_t>::max() / sizeof(T); 
    }
    
    size_t size() const noexcept { return usage_; }

    bool empty() const noexcept { return usage_ == 0; }

    T* data() noexcept { return data_ptr_; }
    const T* data() const noexcept { return data_ptr_; }

    void reserve(size_t new_capacity) noexcept
    {
        if (new_capacity > maximum_quantity_)
        {
            resize(new_capacity);
        }
    }

    void shrink_to_fit() noexcept
    {
        if (usage_ < maximum_quantity_ && usage_ > 0) 
        {
            T* new_ptr = nullptr;
            size_t new_total_bytes = usage_ * sizeof(T);
            new_ptr = static_cast<T*>(::operator new(new_total_bytes, std::align_val_t{alignof(T)}));
            
            if constexpr (std::is_trivially_copyable_v<T>) 
            {
                std::memcpy(new_ptr, data_ptr_, usage_ * sizeof(T));
            } 
            else 
            {
                std::uninitialized_move(data_ptr_, data_ptr_ + usage_, new_ptr);
                std::destroy(data_ptr_, data_ptr_ + usage_);
            }
            
            size_t old_total_bytes = maximum_quantity_ * sizeof(T);
            ::operator delete(data_ptr_, old_total_bytes, std::align_val_t{alignof(T)});
            
            data_ptr_ = new_ptr;
            maximum_quantity_ = usage_;
        }
    }

    T& at(size_t index)
    {
        if (index >= usage_) 
        {
            throw std::out_of_range("class_pool::at: index out of range");
        }
        return data_ptr_[index];
    }
    
    const T& at(size_t index) const
    {
        if (index >= usage_) 
        {
            throw std::out_of_range("class_pool::at: index out of range");
        }
        return data_ptr_[index];
    }

    T& front() noexcept
    {
        return data_ptr_[0];
    }
    
    const T& front() const noexcept 
    {   
        return data_ptr_[0];
    }
    
    T& back() noexcept
    {
        return data_ptr_[usage_ - 1];
    }
    
    const T& back() const noexcept 
    {   
        return data_ptr_[usage_ - 1];
    }

    void resize(size_t new_capacity) noexcept
    {
        if (new_capacity <= maximum_quantity_) 
        {
            return;
        }

        T* new_ptr = nullptr;
        size_t new_total_bytes = new_capacity * sizeof(T);
        new_ptr = static_cast<T*>(::operator new(new_total_bytes, std::align_val_t{alignof(T)}));
        
        if (data_ptr_ != nullptr) 
        {
            if constexpr (std::is_trivially_copyable_v<T>) 
            {
                std::memcpy(new_ptr, data_ptr_, usage_ * sizeof(T));
            } 
            else 
            {
                std::uninitialized_move(data_ptr_, data_ptr_ + usage_, new_ptr);
                std::destroy(data_ptr_, data_ptr_ + usage_);
            }
            
            size_t old_total_bytes = maximum_quantity_ * sizeof(T);
            ::operator delete(data_ptr_, old_total_bytes, std::align_val_t{alignof(T)});
        }

        data_ptr_ = new_ptr;
        maximum_quantity_ = new_capacity;
    }

    T* insert(T* pos, const T& value) noexcept
    {
        size_t index = static_cast<size_t>(pos - data_ptr_);
        if (usage_ >= maximum_quantity_) 
        {
            size_t new_capacity = maximum_quantity_ * 4;
            if (new_capacity == maximum_quantity_) 
            {
                new_capacity = maximum_quantity_ + 1;
            }
            resize(new_capacity);
        }
        
        if (index < usage_) 
        {
            std::move_backward(data_ptr_ + index, data_ptr_ + usage_, data_ptr_ + usage_ + 1);
            (data_ptr_ + index)->~T(); 
        }
        
        new (data_ptr_ + index) T(value);
        ++usage_;
        return data_ptr_ + index;
    }
    
    T* insert(T* pos, T&& value) noexcept
    {
        size_t index = static_cast<size_t>(pos - data_ptr_);
        if (usage_ >= maximum_quantity_) 
        {
            size_t new_capacity = maximum_quantity_ * 4;
            if (new_capacity == maximum_quantity_) 
            {
                new_capacity = maximum_quantity_ + 1;
            }
            resize(new_capacity);
        }
        
        if (index < usage_) 
        {
            std::move_backward(data_ptr_ + index, data_ptr_ + usage_, data_ptr_ + usage_ + 1);
            (data_ptr_ + index)->~T();
        }
        
        new (data_ptr_ + index) T(std::move(value));
        ++usage_;
        return data_ptr_ + index;
    }
    
    template <typename... Args>
    T* emplace(T* pos, Args&&... args) noexcept
    {
        size_t index = static_cast<size_t>(pos - data_ptr_);
        if (usage_ >= maximum_quantity_) 
        {
            size_t new_capacity = maximum_quantity_ * 4;
            if (new_capacity == maximum_quantity_) 
            {
                new_capacity = maximum_quantity_ + 1;
            }
            resize(new_capacity);
        }
        
        if (index < usage_) 
        {
            std::move_backward(data_ptr_ + index, data_ptr_ + usage_, data_ptr_ + usage_ + 1);
            (data_ptr_ + index)->~T();
        }
        
        new (data_ptr_ + index) T(std::forward<Args>(args)...);
        ++usage_;
        return data_ptr_ + index;
    }
    
    T* erase(T* pos) noexcept
    {
        size_t index = static_cast<size_t>(pos - data_ptr_);
        if (index < usage_) 
        {
            pos->~T();
            if (usage_ > 1 && index < usage_ - 1) 
            {
                std::move(data_ptr_ + index + 1, data_ptr_ + usage_, data_ptr_ + index);
                (data_ptr_ + usage_ - 1)->~T();
            }
            --usage_;
            return data_ptr_ + index;
        }
        return data_ptr_ + usage_;
    }
    
    T* erase(T* first, T* last) noexcept
    {
        size_t start_index = static_cast<size_t>(first - data_ptr_);
        size_t end_index = static_cast<size_t>(last - data_ptr_);
        
        if (start_index >= usage_) 
        {
            return data_ptr_ + usage_;
        }
        
        if (end_index > usage_) 
        {
            end_index = usage_;
        }
        
        if (start_index >= end_index) 
        {
            return data_ptr_ + start_index;
        }
        
        for (size_t i = start_index; i < end_index; ++i) 
        {
            (data_ptr_ + i)->~T();
        }
        
        if (end_index < usage_) 
        {
            std::move(data_ptr_ + end_index, data_ptr_ + usage_, data_ptr_ + start_index);
            size_t new_usage = usage_ - (end_index - start_index);
            for (size_t i = new_usage; i < usage_; ++i) 
            {
                (data_ptr_ + i)->~T();
            }
            usage_ = new_usage;
        }
        else
        {
            usage_ -= (end_index - start_index);
        }
        return data_ptr_ + start_index;
    }

    void assign(size_t count, const T& value) noexcept
    {
        clear();
        reserve(count);
        for (size_t i = 0; i < count; ++i) 
        {
            emplace_back(value);
        }
    }
    
    template <typename InputIt>
    void assign(InputIt first, InputIt last) noexcept
    {
        clear();
        size_t count = std::distance(first, last);
        reserve(count);
        for (auto it = first; it != last; ++it) 
        {
            emplace_back(*it);
        }
    }

    void swap(class_pool& other) noexcept
    {
        std::swap(data_ptr_, other.data_ptr_);
        std::swap(maximum_quantity_, other.maximum_quantity_);
        std::swap(usage_, other.usage_);
    }
    
    void pop_back() noexcept
    {
        if (usage_ > 0)
        {
            data_ptr_[usage_ - 1].~T();
            --usage_;
        }
    }

    bool valid() const noexcept { return data_ptr_ != nullptr; }
    
    T& operator[](size_t index) noexcept
    {
        return data_ptr_[index];
    }
    
    const T& operator[](size_t index) const noexcept 
    {   
        return data_ptr_[index];
    }
    
    iterator begin() noexcept { return data_ptr_; }
    iterator end() noexcept { return data_ptr_ + usage_; }
    
    const_iterator begin() const noexcept { return data_ptr_; }
    const_iterator end() const noexcept { return data_ptr_ + usage_; }
    
    const_iterator cbegin() const noexcept { return data_ptr_; }
    const_iterator cend() const noexcept { return data_ptr_ + usage_; }
    
    reverse_iterator rbegin() noexcept 
    { 
        return reverse_iterator(data_ptr_ + usage_); 
    }
    
    reverse_iterator rend() noexcept 
    { 
        return reverse_iterator(data_ptr_); 
    }
    
    const_reverse_iterator rbegin() const noexcept 
    { 
        return const_reverse_iterator(data_ptr_ + usage_); 
    }
    
    const_reverse_iterator rend() const noexcept 
    { 
        return const_reverse_iterator(data_ptr_); 
    }
    
    const_reverse_iterator crbegin() const noexcept 
    { 
        return const_reverse_iterator(data_ptr_ + usage_); 
    }
    
    const_reverse_iterator crend() const noexcept 
    { 
        return const_reverse_iterator(data_ptr_); 
    }
    

    template <typename... Args>
    T& emplace_at(size_t index, Args&&... args) noexcept
    {
        static_assert(std::is_constructible_v<T, Args...>,
            "T must be constructible from the provided arguments");
        
        if (index >= usage_) [[unlikely]] 
        {

            if (index >= maximum_quantity_) 
            {
                resize(index + 1);
            }

            new (&data_ptr_[index]) T(std::forward<Args>(args)...);
            if (index >= usage_) {
                usage_ = index + 1;
            }
        } 
        else 
        {
            data_ptr_[index].~T();
            new (&data_ptr_[index]) T(std::forward<Args>(args)...);
        }
        return data_ptr_[index];
    }
};

template <typename T>
void swap(class_pool<T>& a, class_pool<T>& b) noexcept
{
    a.swap(b);
}