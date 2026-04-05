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
#include <span>

template <typename T>
class class_pool
{
private:
    T* data_ptr_{nullptr};
    size_t maximum_quantity_{0};
    size_t usage_{0};

    static constexpr size_t DEFAULT_CAPACITY = 8;
    static constexpr size_t SMALL_CAPACITY_THRESHOLD = 1024;
    static constexpr size_t MEDIUM_CAPACITY_THRESHOLD = 65536;

    [[nodiscard]] static constexpr size_t round_up_to_default(size_t n) noexcept
    {
        return n == 0 ? DEFAULT_CAPACITY : n;
    }

    [[nodiscard]] static constexpr size_t calculate_new_capacity(size_t current) noexcept
    {
        if (current == 0) return DEFAULT_CAPACITY;
        if (current < SMALL_CAPACITY_THRESHOLD) return current * 2;
        if (current < MEDIUM_CAPACITY_THRESHOLD) return current + (current >> 1);
        return current + (current >> 2);
    }

    [[nodiscard]] static constexpr size_t calculate_growth_for_reserve(size_t required) noexcept
    {
        if (required == 0) return DEFAULT_CAPACITY;
        size_t capacity = DEFAULT_CAPACITY;
        while (capacity < required)
        {
            capacity = calculate_new_capacity(capacity);
        }
        return capacity;
    }

    void destroy_range(T* first, T* last) noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            for (; first != last; ++first)
            {
                first->~T();
            }
        }
    }

    void uninitialized_copy(const T* src_first, const T* src_last, T* dst) noexcept
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(dst, src_first, static_cast<size_t>(src_last - src_first) * sizeof(T));
        }
        else
        {
            for (; src_first != src_last; ++src_first, ++dst)
            {
                new (dst) T(*src_first);
            }
        }
    }

    void uninitialized_move(T* src_first, T* src_last, T* dst) noexcept
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            std::memcpy(dst, src_first, static_cast<size_t>(src_last - src_first) * sizeof(T));
        }
        else
        {
            std::uninitialized_move(src_first, src_last, dst);
            std::destroy(src_first, src_last);
        }
    }

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

    constexpr class_pool() noexcept = default;

    explicit class_pool(size_t capacity) noexcept
        : data_ptr_(nullptr)
        , maximum_quantity_(capacity)
        , usage_(0)
    {
        if (capacity > 0) [[likely]]
        {
            size_t total_bytes = capacity * sizeof(T);
            data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
        }
    }

    class_pool(size_t count, const T& value) noexcept
        : data_ptr_(nullptr)
        , maximum_quantity_(round_up_to_default(count))
        , usage_(0)
    {
        if (count > 0) [[likely]]
        {
            size_t total_bytes = maximum_quantity_ * sizeof(T);
            data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
            for (size_t i = 0; i < count; ++i)
            {
                new (&data_ptr_[i]) T(value);
            }
            usage_ = count;
        }
    }

    template <typename InputIt>
    class_pool(InputIt first, InputIt last) noexcept
        : data_ptr_(nullptr)
        , maximum_quantity_(0)
        , usage_(0)
    {
        size_t count = static_cast<size_t>(std::distance(first, last));
        maximum_quantity_ = round_up_to_default(count);
        
        if (count > 0) [[likely]]
        {
            size_t total_bytes = maximum_quantity_ * sizeof(T);
            data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
            size_t i = 0;
            for (auto it = first; it != last; ++it, ++i)
            {
                new (&data_ptr_[i]) T(*it);
            }
            usage_ = count;
        }
    }

    class_pool(const class_pool& other) noexcept
        : data_ptr_(nullptr)
        , maximum_quantity_(other.maximum_quantity_)
        , usage_(other.usage_)
    {
        if (maximum_quantity_ > 0) [[likely]]
        {
            size_t total_bytes = maximum_quantity_ * sizeof(T);
            data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
            uninitialized_copy(other.data_ptr_, other.data_ptr_ + other.usage_, data_ptr_);
        }
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

    class_pool& operator=(const class_pool& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (data_ptr_ != nullptr) [[likely]]
            {
                destroy_range(data_ptr_, data_ptr_ + usage_);
                size_t total_bytes = maximum_quantity_ * sizeof(T);
                ::operator delete(data_ptr_, total_bytes, std::align_val_t{alignof(T)});
            }
            
            maximum_quantity_ = other.maximum_quantity_;
            usage_ = other.usage_;
            
            if (maximum_quantity_ > 0) [[likely]]
            {
                size_t total_bytes = maximum_quantity_ * sizeof(T);
                data_ptr_ = static_cast<T*>(::operator new(total_bytes, std::align_val_t{alignof(T)}));
                uninitialized_copy(other.data_ptr_, other.data_ptr_ + other.usage_, data_ptr_);
            }
            else
            {
                data_ptr_ = nullptr;
            }
        }
        return *this;
    }

    class_pool& operator=(class_pool&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (data_ptr_ != nullptr) [[likely]]
            {
                destroy_range(data_ptr_, data_ptr_ + usage_);
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

    ~class_pool() noexcept
    {
        if (data_ptr_ != nullptr) [[likely]]
        {
            destroy_range(data_ptr_, data_ptr_ + usage_);
            size_t total_bytes = maximum_quantity_ * sizeof(T);
            ::operator delete(data_ptr_, total_bytes, std::align_val_t{alignof(T)});
        }
    }

    template <typename... Args>
    inline void emplace_back(Args&&... args) noexcept
    {
        static_assert(std::is_constructible_v<T, Args...>, 
                     "T must be constructible from the provided arguments");
        
        if (usage_ >= maximum_quantity_) [[unlikely]]
        {
            size_t new_capacity = calculate_new_capacity(maximum_quantity_);
            resize(new_capacity);
        }
        new (&data_ptr_[usage_]) T(std::forward<Args>(args)...);
        ++usage_;
    }

    inline void clear() noexcept
    {
        destroy_range(data_ptr_, data_ptr_ + usage_);
        usage_ = 0;
    }

    [[nodiscard]] constexpr T* get(size_t index) noexcept 
    { 
        return &data_ptr_[index];
    }

    [[nodiscard]] constexpr const T* get(size_t index) const noexcept 
    { 
        return &data_ptr_[index];
    }

    [[nodiscard]] constexpr size_type capacity() const noexcept { return maximum_quantity_; }
    [[nodiscard]] constexpr size_type sparse_capacity() const noexcept { return maximum_quantity_; }
    [[nodiscard]] constexpr size_type size() const noexcept { return usage_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return usage_ == 0; }
    [[nodiscard]] constexpr pointer data() noexcept { return data_ptr_; }
    [[nodiscard]] constexpr const_pointer data() const noexcept { return data_ptr_; }

    inline void reserve(size_t new_capacity) noexcept
    {
        if (new_capacity > maximum_quantity_) [[unlikely]]
        {
            size_t optimal_capacity = calculate_growth_for_reserve(new_capacity);
            resize(optimal_capacity);
        }
    }

    inline void shrink_to_fit() noexcept
    {
        if (usage_ == 0 && data_ptr_ != nullptr) [[unlikely]]
        {
            size_t old_total_bytes = maximum_quantity_ * sizeof(T);
            ::operator delete(data_ptr_, old_total_bytes, std::align_val_t{alignof(T)});
            data_ptr_ = nullptr;
            maximum_quantity_ = 0;
            return;
        }
        
        if (usage_ < maximum_quantity_ && usage_ > 0) [[likely]]
        {
            T* new_ptr = nullptr;
            size_t new_total_bytes = usage_ * sizeof(T);
            new_ptr = static_cast<T*>(::operator new(new_total_bytes, std::align_val_t{alignof(T)}));
            
            uninitialized_move(data_ptr_, data_ptr_ + usage_, new_ptr);
            
            size_t old_total_bytes = maximum_quantity_ * sizeof(T);
            ::operator delete(data_ptr_, old_total_bytes, std::align_val_t{alignof(T)});
            
            data_ptr_ = new_ptr;
            maximum_quantity_ = usage_;
        }
    }

    [[nodiscard]] T& at(size_t index)
    {
        if (index >= usage_) [[unlikely]]
        {
            throw std::out_of_range("class_pool::at: index out of range");
        }
        return data_ptr_[index];
    }

    [[nodiscard]] const T& at(size_t index) const
    {
        if (index >= usage_) [[unlikely]]
        {
            throw std::out_of_range("class_pool::at: index out of range");
        }
        return data_ptr_[index];
    }

    [[nodiscard]] constexpr T& front() noexcept
    {
        return data_ptr_[0];
    }

    [[nodiscard]] constexpr const T& front() const noexcept 
    {   
        return data_ptr_[0];
    }

    [[nodiscard]] constexpr T& back() noexcept
    {
        return data_ptr_[usage_ - 1];
    }

    [[nodiscard]] constexpr const T& back() const noexcept 
    {   
        return data_ptr_[usage_ - 1];
    }

    inline void resize(size_t new_capacity) noexcept
    {
        if (new_capacity <= maximum_quantity_) [[likely]]
        {
            return;
        }

        T* new_ptr = nullptr;
        size_t new_total_bytes = new_capacity * sizeof(T);
        new_ptr = static_cast<T*>(::operator new(new_total_bytes, std::align_val_t{alignof(T)}));
        
        if (data_ptr_ != nullptr) [[likely]]
        {
            uninitialized_move(data_ptr_, data_ptr_ + usage_, new_ptr);
            size_t old_total_bytes = maximum_quantity_ * sizeof(T);
            ::operator delete(data_ptr_, old_total_bytes, std::align_val_t{alignof(T)});
        }

        data_ptr_ = new_ptr;
        maximum_quantity_ = new_capacity;
    }

    inline T* insert(T* pos, const T& value) noexcept
    {
        size_t index = static_cast<size_t>(pos - data_ptr_);
        if (usage_ >= maximum_quantity_) [[unlikely]]
        {
            size_t new_capacity = calculate_new_capacity(maximum_quantity_);
            resize(new_capacity);
        }
        
        if (index < usage_) [[likely]]
        {
            std::move_backward(data_ptr_ + index, data_ptr_ + usage_, data_ptr_ + usage_ + 1);
            (data_ptr_ + index)->~T(); 
        }
        
        new (data_ptr_ + index) T(value);
        ++usage_;
        return data_ptr_ + index;
    }

    inline T* insert(T* pos, T&& value) noexcept
    {
        size_t index = static_cast<size_t>(pos - data_ptr_);
        if (usage_ >= maximum_quantity_) [[unlikely]]
        {
            size_t new_capacity = calculate_new_capacity(maximum_quantity_);
            resize(new_capacity);
        }
        
        if (index < usage_) [[likely]]
        {
            std::move_backward(data_ptr_ + index, data_ptr_ + usage_, data_ptr_ + usage_ + 1);
            (data_ptr_ + index)->~T(); 
        }
        
        new (data_ptr_ + index) T(std::move(value));
        ++usage_;
        return data_ptr_ + index;
    }

    template <typename... Args>
    inline T* emplace(T* pos, Args&&... args) noexcept
    {
        size_t index = static_cast<size_t>(pos - data_ptr_);
        if (usage_ >= maximum_quantity_) [[unlikely]]
        {
            size_t new_capacity = calculate_new_capacity(maximum_quantity_);
            resize(new_capacity);
        }
        
        if (index < usage_) [[likely]]
        {
            std::move_backward(data_ptr_ + index, data_ptr_ + usage_, data_ptr_ + usage_ + 1);
            (data_ptr_ + index)->~T(); 
        }
        
        new (data_ptr_ + index) T(std::forward<Args>(args)...);
        ++usage_;
        return data_ptr_ + index;
    }

    inline T* erase(T* pos) noexcept
    {
        size_t index = static_cast<size_t>(pos - data_ptr_);
        if (index < usage_) [[likely]]
        {
            pos->~T();
            if (usage_ > 1 && index < usage_ - 1) [[likely]]
            {
                std::move(data_ptr_ + index + 1, data_ptr_ + usage_, data_ptr_ + index);
                (data_ptr_ + usage_ - 1)->~T();
            }
            --usage_;
            return data_ptr_ + index;
        }
        return data_ptr_ + usage_;
    }

    inline T* erase(T* first, T* last) noexcept
    {
        size_t start_index = static_cast<size_t>(first - data_ptr_);
        size_t end_index = static_cast<size_t>(last - data_ptr_);
        
        if (start_index >= usage_) [[unlikely]]
        {
            return data_ptr_ + usage_;
        }
        
        if (end_index > usage_) [[unlikely]]
        {
            end_index = usage_;
        }
        
        if (start_index >= end_index) [[unlikely]]
        {
            return data_ptr_ + start_index;
        }
        
        destroy_range(first, last);
        
        if (end_index < usage_) [[likely]]
        {
            std::move(data_ptr_ + end_index, data_ptr_ + usage_, data_ptr_ + start_index);
            size_t new_usage = usage_ - (end_index - start_index);
            destroy_range(data_ptr_ + new_usage, data_ptr_ + usage_);
            usage_ = new_usage;
        }
        else
        {
            usage_ -= (end_index - start_index);
        }
        return data_ptr_ + start_index;
    }

    inline void assign(size_t count, const T& value) noexcept
    {
        clear();
        reserve(count);
        for (size_t i = 0; i < count; ++i) 
        {
            emplace_back(value);
        }
    }

    template <typename InputIt>
    inline void assign(InputIt first, InputIt last) noexcept
    {
        clear();
        size_t count = static_cast<size_t>(std::distance(first, last));
        reserve(count);
        for (auto it = first; it != last; ++it) 
        {
            emplace_back(*it);
        }
    }

    inline void swap(class_pool& other) noexcept
    {
        std::swap(data_ptr_, other.data_ptr_);
        std::swap(maximum_quantity_, other.maximum_quantity_);
        std::swap(usage_, other.usage_);
    }

    inline void pop_back() noexcept
    {
        if (usage_ > 0) [[likely]]
        {
            data_ptr_[usage_ - 1].~T();
            --usage_;
        }
    }

    [[nodiscard]] constexpr bool valid() const noexcept { return data_ptr_ != nullptr; }
    [[nodiscard]] constexpr size_type size_bytes() const noexcept { return usage_ * sizeof(T); }
    [[nodiscard]] constexpr size_type capacity_bytes() const noexcept { return maximum_quantity_ * sizeof(T); }

    [[nodiscard]] constexpr std::span<T> span() noexcept { return std::span<T>(data_ptr_, usage_); }
    [[nodiscard]] constexpr std::span<const T> span() const noexcept { return std::span<const T>(data_ptr_, usage_); }

    [[nodiscard]] constexpr T& operator[](size_t index) noexcept
    {
        return data_ptr_[index];
    }

    [[nodiscard]] constexpr const T& operator[](size_t index) const noexcept 
    {   
        return data_ptr_[index];
    }

    constexpr iterator begin() noexcept { return data_ptr_; }
    constexpr iterator end() noexcept { return data_ptr_ + usage_; }
    constexpr const_iterator begin() const noexcept { return data_ptr_; }
    constexpr const_iterator end() const noexcept { return data_ptr_ + usage_; }
    constexpr const_iterator cbegin() const noexcept { return data_ptr_; }
    constexpr const_iterator cend() const noexcept { return data_ptr_ + usage_; }

    constexpr reverse_iterator rbegin() noexcept 
    { 
        return reverse_iterator(data_ptr_ + usage_); 
    }

    constexpr reverse_iterator rend() noexcept 
    { 
        return reverse_iterator(data_ptr_); 
    }

    constexpr const_reverse_iterator rbegin() const noexcept 
    { 
        return const_reverse_iterator(data_ptr_ + usage_); 
    }

    constexpr const_reverse_iterator rend() const noexcept 
    { 
        return const_reverse_iterator(data_ptr_); 
    }

    constexpr const_reverse_iterator crbegin() const noexcept 
    { 
        return const_reverse_iterator(data_ptr_ + usage_); 
    }

    constexpr const_reverse_iterator crend() const noexcept 
    { 
        return const_reverse_iterator(data_ptr_); 
    }

    template <typename... Args>
    inline T& emplace_at(size_t index, Args&&... args) noexcept
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
            usage_ = index + 1;
        } 
        else 
        {
            data_ptr_[index].~T();
            new (&data_ptr_[index]) T(std::forward<Args>(args)...);
        }
        return data_ptr_[index];
    }

    template <typename... Args>
    inline T& sparse_emplace_at(size_t index, Args&&... args) noexcept
    {
        static_assert(std::is_constructible_v<T, Args...>,
            "T must be constructible from the provided arguments");
        
        if (index >= maximum_quantity_) [[unlikely]]
        {
            resize(index + 1);
        }
        
        new (&data_ptr_[index]) T(std::forward<Args>(args)...);
        return data_ptr_[index];
    }

    [[nodiscard]] constexpr bool is_constructed_at(size_t index) const noexcept
    {
        return index < usage_;
    }
};

template <typename T>
void swap(class_pool<T>& a, class_pool<T>& b) noexcept
{
    a.swap(b);
}
