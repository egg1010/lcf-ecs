#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <memory>
#include "oom_handler.hpp"
#include "../force_inline.hpp"

// STL Allocator 适配器: 包装支持 allocate(size_t)/deallocate(void*) 的内存池

namespace memory
{

template <typename T, typename Pool>
class stl_allocator
{
private:
    Pool* pool_;

public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::false_type;

    constexpr stl_allocator() noexcept : pool_(nullptr) {}

    explicit stl_allocator(Pool& pool) noexcept : pool_(&pool) {}

    template <typename U>
    stl_allocator(const stl_allocator<U, Pool>& other) noexcept
        : pool_(other.pool_)
    {}

    [[nodiscard]] T* allocate(size_t n) noexcept
    {
        if (n == 0) [[unlikely]]
        {
            return nullptr;
        }
        size_t bytes = n * sizeof(T);
        if (n > static_cast<size_t>(-1) / sizeof(T)) [[unlikely]]
        {
            handle_oom(bytes, "stl_allocator::allocate(overflow)");
        }
        void* p = pool_->allocate(bytes);
        if (!p) [[unlikely]]
        {
            handle_oom(bytes, "stl_allocator::allocate");
        }
        return static_cast<T*>(p);
    }

    void deallocate(T* p, size_t n) noexcept
    {
        if (!p) [[unlikely]]
        {
            return;
        }
        pool_->deallocate(p, n * sizeof(T));
    }

    [[nodiscard]] constexpr Pool* pool() const noexcept { return pool_; }

    template <typename U>
    struct rebind
    {
        using other = stl_allocator<U, Pool>;
    };

    [[nodiscard]] T* address(T& x) const noexcept
    {
        return std::addressof(x);
    }

    [[nodiscard]] const T* address(const T& x) const noexcept
    {
        return std::addressof(x);
    }

    [[nodiscard]] constexpr size_t max_size() const noexcept
    {
        return static_cast<size_t>(-1) / sizeof(T);
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) noexcept
    {
        new (p) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) noexcept
    {
        p->~U();
    }
};

template <typename T, typename U, typename P>
[[nodiscard]] constexpr bool operator==(
    const stl_allocator<T, P>& a, const stl_allocator<U, P>& b) noexcept
{
    return a.pool() == b.pool();
}

template <typename T, typename U, typename P>
[[nodiscard]] constexpr bool operator!=(
    const stl_allocator<T, P>& a, const stl_allocator<U, P>& b) noexcept
{
    return a.pool() != b.pool();
}

} // namespace memory
