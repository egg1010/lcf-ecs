#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "thread/error.hpp"

namespace framework
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
    using stored = std::decay_t<F>;
    auto* mem = static_cast<unsigned char*>(::operator new(sizeof(stored), std::nothrow));
    if (mem == nullptr)
    {
        target->invoke_ = nullptr;
        target->destroy_ = nullptr;
        return false;
    }
    stored* object = new (mem) stored(std::forward<F>(callable));

    target->invoke_ = [](void* ctx) noexcept
    {
        void* pointer = nullptr;
        std::memcpy(&pointer, ctx, sizeof(pointer));
        (*static_cast<stored*>(pointer))();
    };
    target->destroy_ = [](void* ctx) noexcept
    {
        void* pointer = nullptr;
        std::memcpy(&pointer, ctx, sizeof(pointer));
        static_cast<stored*>(pointer)->~stored();
        ::operator delete(pointer);
    };
    std::memcpy(target->storage_, &object, sizeof(object));
    target->set_overflow_capture(true);
    overflow_capture_counter().fetch_add(1, std::memory_order_relaxed);
    return true;
}

template <typename F> inline bool fill_inline_job(job* target, F&& callable) noexcept
{
    using stored = std::decay_t<F>;
    stored* object = new (target->storage_) stored(std::forward<F>(callable));

    target->invoke_ = [](void* ctx) noexcept { (*static_cast<stored*>(ctx))(); };
    target->destroy_ = [](void* ctx) noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<stored>)
        {
            static_cast<stored*>(ctx)->~stored();
        }
        else
        {
            (void)ctx;
        }
    };
    target->set_overflow_capture(false);
    (void)object;
    return true;
}

template <typename F> inline bool fill_job(job* target, F&& callable) noexcept
{
    using stored = std::decay_t<F>;
    if constexpr (fits_inline_capture<stored>)
    {
        return fill_inline_job(target, std::forward<F>(callable));
    }
    else
    {
        return fill_overflow_job(target, std::forward<F>(callable));
    }
}

inline constexpr std::uint32_t slot_index_none = 0xFFFFFFFFu;

struct slot_free_link
{
    static std::uint64_t pack(std::uint32_t next_index, std::uint32_t generation) noexcept
    {
        return (static_cast<std::uint64_t>(generation) << 32) |
               (static_cast<std::uint64_t>(next_index) + 1ull);
    }

    static std::uint32_t next_of(std::uint64_t head) noexcept
    {
        return static_cast<std::uint32_t>(head & 0xFFFFFFFFull) - 1u;
    }

    static std::uint32_t generation_of(std::uint64_t head) noexcept
    {
        return static_cast<std::uint32_t>(head >> 32);
    }

    static bool empty(std::uint64_t head) noexcept
    {
        return next_of(head) == slot_index_none;
    }
};

struct job_slot
{
    job data_{};
    std::uint32_t next_free_{slot_index_none};
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
    std::atomic<std::uint64_t> free_head_{0};
    std::atomic<std::int64_t> free_count_{0};
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
        return slot_free_link::pack(next_index, generation);
    }

    static std::uint32_t unpack_index(std::uint64_t head) noexcept
    {
        return slot_free_link::next_of(head);
    }

    static std::uint32_t unpack_generation(std::uint64_t head) noexcept
    {
        return slot_free_link::generation_of(head);
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
        free_head_.store(0, std::memory_order_relaxed);
        free_count_.store(0, std::memory_order_relaxed);

        for (std::size_t i = 0; i < capacity; ++i)
        {
            new (&slots_[i]) job_slot{};
            slots_[i].data_.slot_index_ = static_cast<std::uint32_t>(i);
            slots_[i].next_free_ =
                (i + 1 < capacity) ? static_cast<std::uint32_t>(i + 1) : slot_index_none;
        }

        free_count_.store(static_cast<std::int64_t>(capacity), std::memory_order_release);
        free_head_.store(pack(0, 0), std::memory_order_release);
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
        free_head_.store(0, std::memory_order_relaxed);
        free_count_.store(0, std::memory_order_relaxed);
    }

    std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    std::int64_t available() const noexcept
    {
        return free_count_.load(std::memory_order_acquire);
    }

    job* allocate(std::uint32_t& slot_index) noexcept
    {
        std::uint64_t head = free_head_.load(std::memory_order_acquire);
        for (;;)
        {
            const std::uint32_t index = unpack_index(head);
            if (index == slot_index_none || index >= capacity_)
            {
                stats_.exhaustion_.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }
            const std::uint32_t generation = unpack_generation(head);

            const std::uint32_t next = slots_[index].next_free_;
            const std::uint64_t new_head = pack(next, generation + 1u);
            if (!free_head_.compare_exchange_weak(head, new_head, std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
            {
                continue;
            }

            slot_index = index;
            free_count_.fetch_sub(1, std::memory_order_relaxed);
            stats_.allocs_.fetch_add(1, std::memory_order_relaxed);

            job_slot& slot = slots_[index];

            const std::uint8_t visible =
                static_cast<std::uint8_t>(((generation + 1u) & 0xFFu) | 0x01u);
            slot.data_.generation_ = visible;
            return &slot.data_;
        }
    }

    void release(std::uint32_t slot_index) noexcept
    {
        if (slot_index >= capacity_)
        {
            return;
        }
        std::uint64_t head = free_head_.load(std::memory_order_acquire);
        for (;;)
        {
            const std::uint32_t generation = unpack_generation(head);
            const std::uint32_t current = unpack_index(head);
            slots_[slot_index].next_free_ = current;

            const std::uint64_t new_head = pack(slot_index, generation + 1u);
            if (free_head_.compare_exchange_weak(head, new_head, std::memory_order_acq_rel,
                                                 std::memory_order_acquire))
            {
                free_count_.fetch_add(1, std::memory_order_relaxed);
                stats_.frees_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
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
    thread_local job_pool* pool = nullptr;
    return pool;
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
} // namespace framework
