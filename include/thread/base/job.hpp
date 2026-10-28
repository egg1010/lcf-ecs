#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "part/index_pool.hpp"
#include "part/type/inline_callable.hpp"
#include "part/operating_message.hpp"
#include "thread/base/tls.hpp"

namespace thread
{
inline constexpr std::uint32_t job_unordered_lane = 0xFFFFFFFFu;
inline constexpr std::uint32_t no_affinity_worker = 0xFFFFFFFFu;
inline constexpr std::size_t job_storage_size = 64;
inline constexpr std::size_t job_align = 64;
inline constexpr std::size_t default_job_slot_count = 8192;
inline constexpr std::size_t max_priority_levels = 255;

enum class priority : std::uint8_t
{
    background = 0,
    normal = 1,
    critical = 2,
};

inline constexpr std::uint8_t default_priority_levels = 3;

inline std::uint8_t priority_for_level(std::uint8_t level, std::uint8_t levels) noexcept
{
    if (levels <= 1)
    {
        return 0;
    }
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(level) * (levels - 1) + 1) / 2);
}

using job_invoke_fn = void (*)(void* ctx) noexcept;
using job_destroy_fn = void (*)(void* ctx) noexcept;

struct alignas(job_align) job
{
    static constexpr std::uint8_t flag_coroutine = 1u << 0;
    static constexpr std::uint8_t flag_slot_backed = 1u << 1;
    static constexpr std::uint8_t flag_overflow_capture = 1u << 2;

    job_invoke_fn invoke_{nullptr};
    job_destroy_fn destroy_{nullptr};
    unsigned char storage_[job_storage_size] = {};

    std::uint32_t lane_id_{job_unordered_lane};
    std::uint16_t tag_{0};
    std::uint8_t priority_{static_cast<std::uint8_t>(priority::normal)};
    std::uint8_t generation_{0};
    std::atomic<std::uint32_t> pending_deps_{0};
    job* next_{nullptr};
    std::uint32_t slot_index_{0xFFFFFFFFu};
    std::uint32_t aux_{0};

    std::uint8_t flags_{0};
    std::uint8_t reserved_[3] = {};

    unsigned char trailing_pad_[8] = {};

    void* context() noexcept
    {
        return storage_;
    }

    template <typename T> T* as() noexcept
    {
        static_assert(sizeof(T) <= job_storage_size, "capture exceeds inline storage");
        return reinterpret_cast<T*>(storage_);
    }

    bool is_coroutine_resume() const noexcept
    {
        return (flags_ & flag_coroutine) != 0;
    }

    void set_coroutine_resume(bool value) noexcept
    {
        if (value)
        {
            flags_ |= flag_coroutine;
        }
        else
        {
            flags_ = static_cast<std::uint8_t>(flags_ & ~flag_coroutine);
        }
    }

    bool is_slot_backed() const noexcept
    {
        return (flags_ & flag_slot_backed) != 0;
    }

    void set_slot_backed(bool value) noexcept
    {
        if (value)
        {
            flags_ |= flag_slot_backed;
        }
        else
        {
            flags_ = static_cast<std::uint8_t>(flags_ & ~flag_slot_backed);
        }
    }

    // soft 删除跳过析构仅对内联捕获成立；溢出捕获持堆内存，任何策略都必须释放

    bool is_overflow_capture() const noexcept
    {
        return (flags_ & flag_overflow_capture) != 0;
    }

    void set_overflow_capture(bool value) noexcept
    {
        if (value)
        {
            flags_ |= flag_overflow_capture;
        }
        else
        {
            flags_ = static_cast<std::uint8_t>(flags_ & ~flag_overflow_capture);
        }
    }
};

template <typename T>
inline constexpr bool fits_inline_capture =
    sizeof(T) <= job_storage_size && alignof(T) <= job_align && std::is_trivially_copyable_v<T> &&
    std::is_trivially_destructible_v<T>;

template <typename F> inline void invoke_thunk(void* ctx) noexcept
{
    (*static_cast<F*>(ctx))();
}

template <typename F> inline void destroy_thunk(void* ctx) noexcept
{
    if constexpr (!std::is_trivially_destructible_v<F>)
    {
        static_cast<F*>(ctx)->~F();
    }
    else
    {
        (void)ctx;
    }
}

struct overflow_payload
{
    void* object_{nullptr};
};

inline std::atomic<std::uint64_t>& overflow_capture_counter() noexcept
{
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}

template <typename F> inline bool fill_overflow_job(job* target, F&& callable) noexcept
{
    const callable_placement placement =
        callable_bind_heap<job_storage_size, alignof(std::max_align_t)>(
            target->storage_, std::forward<F>(callable), target->invoke_, target->destroy_);
    if (placement == callable_placement::none)
    {
        return false;
    }
    target->set_overflow_capture(true);
    overflow_capture_counter().fetch_add(1, std::memory_order_relaxed);
    return true;
}

template <typename F> inline bool fill_inline_job(job* target, F&& callable) noexcept
{
    (void)callable_bind_inline<job_storage_size, alignof(std::max_align_t)>(
        target->storage_, std::forward<F>(callable), target->invoke_, target->destroy_);
    target->set_overflow_capture(false);
    return true;
}

template <typename F> inline bool fill_job(job* target, F&& callable) noexcept
{
    const callable_placement placement =
        callable_bind<job_storage_size, alignof(std::max_align_t)>(
            target->storage_, std::forward<F>(callable), target->invoke_, target->destroy_);
    if (placement == callable_placement::none)
    {
        return false;
    }
    const bool overflow = placement == callable_placement::heap;
    target->set_overflow_capture(overflow);
    if (overflow)
    {
        overflow_capture_counter().fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

inline constexpr std::uint32_t slot_index_none = 0xFFFFFFFFu;

struct job_slot
{
    job data_{};
    std::uint32_t reserved_{0};
};

struct job_pool_stats
{
    std::atomic<std::uint64_t> allocs_{0};
    std::atomic<std::uint64_t> frees_{0};
    std::atomic<std::uint64_t> overflows_{0};
    std::atomic<std::uint64_t> exhaustion_{0};
};

struct job_pool
{
    job_slot* slots_{nullptr};
    std::size_t capacity_{0};
    index_pool free_{};
    job_pool_stats stats_{};
    bool owns_memory_{false};

    job_pool() noexcept = default;

    job_pool(const job_pool&) = delete;
    job_pool& operator=(const job_pool&) = delete;

    ~job_pool()
    {
        destroy();
    }

    static std::uint64_t pack(std::uint32_t next_index, std::uint32_t generation) noexcept
    {
        return index_pool::pack(next_index, generation);
    }

    bool create(std::size_t capacity) noexcept
    {
        if (slots_ != nullptr)
        {
            return false;
        }
        if (capacity < 2)
        {
            capacity = 2;
        }
        slots_ = static_cast<job_slot*>(
            ::operator new(sizeof(job_slot) * capacity, std::align_val_t(job_align), std::nothrow));
        if (slots_ == nullptr)
        {
            return false;
        }
        owns_memory_ = true;
        capacity_ = capacity;

        for (std::size_t i = 0; i < capacity; ++i)
        {
            new (&slots_[i]) job_slot{};
            slots_[i].data_.slot_index_ = static_cast<std::uint32_t>(i);
        }
        if (!free_.create(static_cast<std::uint32_t>(capacity)))
        {
            ::operator delete(static_cast<void*>(slots_), std::align_val_t(job_align));
            slots_ = nullptr;
            capacity_ = 0;
            owns_memory_ = false;
            return false;
        }
        return true;
    }

    void destroy() noexcept
    {
        if (slots_ != nullptr && owns_memory_)
        {
            ::operator delete(static_cast<void*>(slots_), std::align_val_t(job_align));
        }
        slots_ = nullptr;
        capacity_ = 0;
        free_.destroy();
    }

    std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    std::int64_t available() const noexcept
    {
        return free_.available();
    }

    job* allocate(std::uint32_t& slot_index) noexcept
    {
        std::uint32_t index = index_pool::none;
        if (!free_.allocate(index))
        {
            stats_.exhaustion_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        slot_index = index;
        stats_.allocs_.fetch_add(1, std::memory_order_relaxed);
        job_slot& slot = slots_[index];
        slot.data_.generation_ = 0;
        return &slot.data_;
    }

    void release(std::uint32_t slot_index) noexcept
    {
        if (slot_index >= capacity_)
        {
            return;
        }
        free_.release(slot_index);
        stats_.frees_.fetch_add(1, std::memory_order_relaxed);
    }

    std::uint32_t index_of(const job* target) const noexcept
    {
        if (target == nullptr)
        {
            return 0xFFFFFFFFu;
        }
        return target->slot_index_;
    }

    job* slot_of(std::uint32_t slot_index) const noexcept
    {
        if (slot_index >= capacity_)
        {
            return nullptr;
        }
        return &slots_[slot_index].data_;
    }
};

inline job_pool& global_job_pool() noexcept
{
    static job_pool pool{};
    return pool;
}

inline job_pool*& runtime_job_pool() noexcept
{
    return tls().job_pool_;
}

inline job_pool& current_job_pool() noexcept
{
    job_pool* const pool = runtime_job_pool();
    return pool != nullptr ? *pool : global_job_pool();
}

inline job* take_slot_job(std::uint32_t& slot_index) noexcept
{
    job_pool& pool = current_job_pool();
    if (pool.capacity() == 0)
    {
        if (!pool.create(default_job_slot_count))
        {
            return nullptr;
        }
    }
    job* target = pool.allocate(slot_index);
    if (target != nullptr)
    {
        target->set_slot_backed(true);
    }
    return target;
}

inline void run_job(job* target) noexcept
{
    if (target != nullptr && target->invoke_ != nullptr)
    {
        target->invoke_(target->storage_);
    }
}

inline void destroy_job(job* target) noexcept
{
    if (target == nullptr)
    {
        return;
    }
    if (target->destroy_ != nullptr)
    {
        target->destroy_(target->storage_);
        target->destroy_ = nullptr;
    }
    target->invoke_ = nullptr;
}

inline void release_job(job* target) noexcept
{
    if (target == nullptr)
    {
        return;
    }
    destroy_job(target);
    if (target->is_slot_backed())
    {
        const std::uint32_t index = target->slot_index_;
        target->set_slot_backed(false);
        current_job_pool().release(index);
    }
}

inline void reset_job_metadata(job* target) noexcept
{
    target->next_ = nullptr;
    target->lane_id_ = job_unordered_lane;
    target->tag_ = 0;
    target->priority_ = static_cast<std::uint8_t>(priority::normal);
    target->pending_deps_.store(0, std::memory_order_relaxed);
    target->set_coroutine_resume(false);
    target->set_overflow_capture(false);
}
} // namespace thread
