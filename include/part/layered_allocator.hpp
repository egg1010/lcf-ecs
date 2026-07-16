#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "slab_allocator.hpp"
#include "memory_pool.hpp"
#include "force_inline.hpp"

class layered_allocator
{
private:
    static constexpr size_t SLAB_COUNT = 8;
    static constexpr size_t SLAB_SIZES[SLAB_COUNT] = {16, 32, 48, 64, 80, 96, 112, 128};
    static constexpr size_t SLAB_MAX = 128;

    std::array<slab_allocator, SLAB_COUNT> slabs_;
    memory_pool big_pool_;

    [[nodiscard]] static constexpr size_t slab_index(size_t n) noexcept
    {
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            if (n <= SLAB_SIZES[i]) return i;
        }
        return SLAB_COUNT;
    }

    [[nodiscard]] FORCE_INLINE size_t find_slab(const void* p) const noexcept
    {
        const uint8_t* up = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < SLAB_COUNT; ++i)
        {
            if (slabs_[i].owns(up)) return i;
        }
        return SLAB_COUNT;
    }

public:
    layered_allocator() noexcept
        : slabs_{
            slab_allocator(SLAB_SIZES[0]),
            slab_allocator(SLAB_SIZES[1]),
            slab_allocator(SLAB_SIZES[2]),
            slab_allocator(SLAB_SIZES[3]),
            slab_allocator(SLAB_SIZES[4]),
            slab_allocator(SLAB_SIZES[5]),
            slab_allocator(SLAB_SIZES[6]),
            slab_allocator(SLAB_SIZES[7]),
        }
        , big_pool_()
    {}

    ~layered_allocator() noexcept = default;

    layered_allocator(const layered_allocator&) = delete;
    layered_allocator& operator=(const layered_allocator&) = delete;
    layered_allocator(layered_allocator&&) noexcept = default;
    layered_allocator& operator=(layered_allocator&&) noexcept = default;

    [[nodiscard]] FORCE_INLINE void* allocate(size_t n) noexcept
    {
        if (n == 0) [[unlikely]] return nullptr;
        size_t idx = slab_index(n);
        if (idx < SLAB_COUNT) [[likely]]
        {
            return slabs_[idx].allocate();
        }
        return big_pool_.allocate(n);
    }

    FORCE_INLINE void deallocate(void* p) noexcept
    {
        if (!p) [[unlikely]] return;
        size_t idx = find_slab(p);
        if (idx < SLAB_COUNT) [[likely]]
        {
            slabs_[idx].deallocate(p);
            return;
        }
        big_pool_.deallocate(p);
    }

    FORCE_INLINE void deallocate(void* p, size_t n) noexcept
    {
        if (!p) [[unlikely]] return;
        size_t idx = slab_index(n);
        if (idx < SLAB_COUNT) [[likely]]
        {
            slabs_[idx].deallocate(p);
            return;
        }
        big_pool_.deallocate(p);
    }

    template <typename T, typename... Args>
    [[nodiscard]] FORCE_INLINE T* construct(Args&&... args) noexcept
    {
        void* ptr = allocate(sizeof(T));
        if (!ptr) [[unlikely]] return nullptr;
        return new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T>
    FORCE_INLINE void destroy(T* ptr) noexcept
    {
        if (!ptr) [[unlikely]] return;
        ptr->~T();
        deallocate(ptr, sizeof(T));
    }

    [[nodiscard]] bool owns(const void* p) const noexcept
    {
        size_t idx = find_slab(p);
        if (idx < SLAB_COUNT) return true;
        return big_pool_.owns(p);
    }

    [[nodiscard]] constexpr size_t slab_max() const noexcept { return SLAB_MAX; }
    [[nodiscard]] memory_pool& big_pool() noexcept { return big_pool_; }
    [[nodiscard]] const memory_pool& big_pool() const noexcept { return big_pool_; }
};
