#pragma once

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <new>

#include "part/operating_message.hpp"
#include "thread/base/job.hpp"
#include "thread/base/sync.hpp"
#include "thread/base/tls.hpp"

namespace thread
{

// ---------------------------------------------------------------------------
// 协程帧分级池（P7）
// ---------------------------------------------------------------------------

inline constexpr std::size_t frame_tier_count = 3;
inline constexpr std::size_t frame_tier_sizes[frame_tier_count] = {128, 256, 512};
inline constexpr std::size_t frame_arena_alignment = alignof(std::max_align_t);
inline constexpr std::size_t frame_magazine_capacity = 8;
inline constexpr std::size_t frame_default_blocks_per_tier = 1024;

inline std::size_t frame_tier_of(std::size_t size, std::size_t alignment) noexcept
{
    if (alignment > frame_arena_alignment)
    {
        return frame_tier_count;
    }
    for (std::size_t i = 0; i < frame_tier_count; ++i)
    {
        if (size <= frame_tier_sizes[i])
        {
            return i;
        }
    }
    return frame_tier_count;
}

struct frame_arena
{
    unsigned char* memory_{nullptr};
    std::size_t block_size_{0};
    std::size_t capacity_{0};
    std::atomic<std::int64_t> in_use_{0};
    std::atomic<std::uint64_t> high_water_{0};
    bool owns_{false};

    bool create(std::size_t block_size, std::size_t capacity) noexcept
    {
        if (memory_ != nullptr || capacity == 0)
        {
            return false;
        }
        const std::size_t step =
            block_size < frame_arena_alignment ? frame_arena_alignment : block_size;
        void* raw = ::operator new(step * capacity, std::align_val_t(frame_arena_alignment),
                                   std::nothrow);
        if (raw == nullptr)
        {
            return false;
        }
        memory_ = static_cast<unsigned char*>(raw);
        block_size_ = step;
        capacity_ = capacity;
        in_use_.store(0, std::memory_order_relaxed);
        high_water_.store(0, std::memory_order_relaxed);
        owns_ = true;
        return true;
    }

    void destroy() noexcept
    {
        if (memory_ != nullptr && owns_)
        {
            ::operator delete(static_cast<void*>(memory_),
                              std::align_val_t(frame_arena_alignment));
        }
        memory_ = nullptr;
        block_size_ = 0;
        capacity_ = 0;
        owns_ = false;
    }

    bool contains(const void* p) const noexcept
    {
        if (memory_ == nullptr || p == nullptr)
        {
            return false;
        }
        const auto* bytes = static_cast<const unsigned char*>(p);
        return bytes >= memory_ && bytes < memory_ + block_size_ * capacity_;
    }

    std::size_t index_of(const void* p) const noexcept
    {
        const auto* bytes = static_cast<const unsigned char*>(p);
        return static_cast<std::size_t>(bytes - memory_) / block_size_;
    }

    void* block_at(std::size_t index) const noexcept
    {
        return memory_ + index * block_size_;
    }

    void note_alloc() noexcept
    {
        const std::int64_t used = in_use_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (static_cast<std::uint64_t>(used) > high_water_.load(std::memory_order_relaxed))
        {
            high_water_.store(static_cast<std::uint64_t>(used), std::memory_order_relaxed);
        }
    }

    void note_free() noexcept
    {
        in_use_.fetch_sub(1, std::memory_order_relaxed);
    }
};

// 每线程弹夹（magazine）：热点分配路径零原子操作
struct frame_magazine
{
    void* items_[frame_tier_count][frame_magazine_capacity]{};
    std::uint32_t count_[frame_tier_count]{};
};

inline frame_magazine& frame_magazine_of_thread() noexcept
{
    thread_local frame_magazine magazine{};
    return magazine;
}

struct frame_pool
{
    frame_arena arenas_[frame_tier_count]{};
    index_pool free_[frame_tier_count]{};
    std::atomic<std::uint64_t> hits_{0};
    std::atomic<std::uint64_t> misses_{0};
    std::atomic<std::uint64_t> heap_allocs_{0};
    std::atomic<std::uint64_t> returns_{0};

    frame_pool() noexcept = default;

    frame_pool(const frame_pool&) = delete;
    frame_pool& operator=(const frame_pool&) = delete;

    bool create(std::size_t blocks_per_tier = frame_default_blocks_per_tier) noexcept
    {
        const std::size_t blocks = blocks_per_tier == 0 ? frame_default_blocks_per_tier
                                                       : blocks_per_tier;
        for (std::size_t i = 0; i < frame_tier_count; ++i)
        {
            if (!arenas_[i].create(frame_tier_sizes[i], blocks) ||
                !free_[i].create(static_cast<std::uint32_t>(blocks)))
            {
                destroy();
                return false;
            }
        }
        return true;
    }

    void destroy() noexcept
    {
        for (std::size_t i = 0; i < frame_tier_count; ++i)
        {
            arenas_[i].destroy();
            free_[i].destroy();
        }
    }

    bool created() const noexcept
    {
        return arenas_[0].memory_ != nullptr;
    }

    std::uint64_t high_water(std::size_t tier) const noexcept
    {
        return tier < frame_tier_count ? arenas_[tier].high_water_.load(std::memory_order_relaxed)
                                       : 0;
    }

    void* allocate(std::size_t size, std::size_t alignment) noexcept
    {
        const std::size_t tier = frame_tier_of(size, alignment);
        if (tier >= frame_tier_count || !created())
        {
            heap_allocs_.fetch_add(1, std::memory_order_relaxed);
            if (alignment > frame_arena_alignment)
            {
                return ::operator new(size, std::align_val_t(alignment), std::nothrow);
            }
            return ::operator new(size, std::nothrow);
        }
        frame_magazine& magazine = frame_magazine_of_thread();
        if (magazine.count_[tier] > 0)
        {
            hits_.fetch_add(1, std::memory_order_relaxed);
            return magazine.items_[tier][--magazine.count_[tier]];
        }
        void* first = pop_block(tier);
        if (first == nullptr)
        {
            misses_.fetch_add(1, std::memory_order_relaxed);
            heap_allocs_.fetch_add(1, std::memory_order_relaxed);
            return ::operator new(size, std::nothrow);
        }
        hits_.fetch_add(1, std::memory_order_relaxed);
        // 批量回补：满弹夹为止，摊薄全局栈 CAS 争用
        while (magazine.count_[tier] + 1u < frame_magazine_capacity)
        {
            void* extra = pop_block(tier);
            if (extra == nullptr)
            {
                break;
            }
            magazine.items_[tier][magazine.count_[tier]++] = extra;
        }
        return first;
    }

    void deallocate(void* p) noexcept
    {
        if (p == nullptr)
        {
            return;
        }
        for (std::size_t tier = 0; tier < frame_tier_count; ++tier)
        {
            if (arenas_[tier].contains(p))
            {
                return_block(tier, p);
                return;
            }
        }
        ::operator delete(p);
    }

    void deallocate_aligned(void* p, std::size_t alignment) noexcept
    {
        if (p == nullptr)
        {
            return;
        }
        for (std::size_t tier = 0; tier < frame_tier_count; ++tier)
        {
            if (arenas_[tier].contains(p))
            {
                return_block(tier, p);
                return;
            }
        }
        if (alignment > frame_arena_alignment)
        {
            ::operator delete(p, std::align_val_t(alignment));
            return;
        }
        ::operator delete(p);
    }

  private:
    void* pop_block(std::size_t tier) noexcept
    {
        std::uint32_t index = index_pool::none;
        if (!free_[tier].allocate(index))
        {
            return nullptr;
        }
        arenas_[tier].note_alloc();
        return arenas_[tier].block_at(index);
    }

    void push_block(std::size_t tier, void* p) noexcept
    {
        free_[tier].release(static_cast<std::uint32_t>(arenas_[tier].index_of(p)));
        arenas_[tier].note_free();
    }

    void return_block(std::size_t tier, void* p) noexcept
    {
        returns_.fetch_add(1, std::memory_order_relaxed);
        frame_magazine& magazine = frame_magazine_of_thread();
        if (magazine.count_[tier] < frame_magazine_capacity)
        {
            magazine.items_[tier][magazine.count_[tier]++] = p;
            return;
        }
        // 弹夹满：批量回吐一半到全局栈
        const std::uint32_t half = frame_magazine_capacity / 2;
        for (std::uint32_t i = 0; i < half; ++i)
        {
            push_block(tier, magazine.items_[tier][--magazine.count_[tier]]);
        }
        magazine.items_[tier][magazine.count_[tier]++] = p;
    }
};

// ---------------------------------------------------------------------------
// 调度器持有的协程帧载荷（P4）
// ---------------------------------------------------------------------------

// 平凡可拷贝、8 字节 —— 可直接内联进 job 的 64 字节存储，无需堆分配
struct resume_payload
{
    std::coroutine_handle<> handle_{};
};

inline void run_resume_payload(void* ctx) noexcept
{
    auto* self = reinterpret_cast<resume_payload*>(ctx);
    if (self->handle_ != nullptr && !self->handle_.done())
    {
        self->handle_.resume();
        // detach 语义：帧已交由调度器持有，协程运行结束后在此回收，
        // 调用方的 task 对象必须调用 release() 放弃所有权（见 spawn）。
        if (self->handle_.done())
        {
            self->handle_.destroy();
        }
    }
}

// ---------------------------------------------------------------------------
// 挂起协程登记（P3.3）：侵入式 Treiber 栈 + 惰性删除，登记/注销均 O(1)
// ---------------------------------------------------------------------------

struct suspended_node
{
    waiter_node node_{};
    std::coroutine_handle<> handle_{};
    std::atomic<bool> dead_{false};
};

struct suspended_registry
{
    // 兼容旧签名：容量参数保留但不再预分配（链表结构无需固定容量）
    void init(std::size_t) noexcept
    {
    }

    void release() noexcept
    {
        (void)list_.drain();
        count_.store(0, std::memory_order_relaxed);
    }

    bool add(suspended_node* node) noexcept
    {
        if (node == nullptr)
        {
            return false;
        }
        node->dead_.store(false, std::memory_order_relaxed);
        waiter_node_init(&node->node_, nullptr);
        list_.push(&node->node_);
        count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // 惰性删除：只标记，不摘链——注销 O(1) 且与并发 add 无结构竞争
    void remove(suspended_node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }
        bool expected = false;
        if (node->dead_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                std::memory_order_relaxed))
        {
            count_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    std::size_t resume_all() noexcept
    {
        return sweep(false);
    }

    std::size_t destroy_all() noexcept
    {
        return sweep(true);
    }

    std::size_t size() const noexcept
    {
        return count_.load(std::memory_order_acquire);
    }

  private:
    std::size_t sweep(bool destroy) noexcept
    {
        waiter_node* list = list_.drain();
        std::size_t touched = 0;
        while (list != nullptr)
        {
            waiter_node* const next = list->next_.load(std::memory_order_relaxed);
            auto* node = reinterpret_cast<suspended_node*>(list);
            bool expected = false;
            if (node->dead_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                    std::memory_order_relaxed))
            {
                count_.fetch_sub(1, std::memory_order_relaxed);
            }
            if (node->handle_ != nullptr && !node->handle_.done())
            {
                if (destroy)
                {
                    node->handle_.destroy();
                }
                else
                {
                    node->handle_.resume();
                }
                ++touched;
            }
            list = next;
        }
        return touched;
    }

    waiter_list list_{};
    std::atomic<std::size_t> count_{0};
};

// ---------------------------------------------------------------------------
// lane 就绪位图（P3.2）：默认 lane 上限 64，恰好一个 64 位字
// ---------------------------------------------------------------------------

inline constexpr std::size_t lane_bitmap_bits = 64;

inline void lane_mark_ready(std::atomic<std::uint64_t>& summary, std::uint32_t lane_id) noexcept
{
    if (lane_id >= lane_bitmap_bits)
    {
        return;
    }
    summary.fetch_or(1ull << lane_id, std::memory_order_release);
}

inline void lane_clear_ready(std::atomic<std::uint64_t>& summary, std::uint32_t lane_id) noexcept
{
    if (lane_id >= lane_bitmap_bits)
    {
        return;
    }
    // acq_rel：读入生产者的置位以建立同步，供清位后的队列复查使用
    summary.fetch_and(~(1ull << lane_id), std::memory_order_acq_rel);
}

// ---------------------------------------------------------------------------
// runtime_context
// ---------------------------------------------------------------------------

inline constexpr std::size_t runtime_default_job_slots = default_job_slot_count;

struct runtime_context
{
    job_pool jobs_{};
    frame_pool frames_{};
    suspended_registry suspended_{};
    std::atomic<std::uint64_t> lane_ready_{0};

    runtime_context() noexcept = default;

    runtime_context(const runtime_context&) = delete;
    runtime_context& operator=(const runtime_context&) = delete;

    // 幂等：池已创建则跳过。失败不影响“回退到进程默认池”的老行为。
    bool create(std::size_t job_slots = runtime_default_job_slots,
                std::size_t frame_blocks = frame_default_blocks_per_tier) noexcept
    {
        if (jobs_.capacity() == 0)
        {
            const std::size_t slots = job_slots == 0 ? runtime_default_job_slots : job_slots;
            if (!jobs_.create(slots))
            {
                return false;
            }
        }
        if (!frames_.created())
        {
            (void)frames_.create(frame_blocks);
        }
        return true;
    }

    void destroy() noexcept
    {
        suspended_.release();
        frames_.destroy();
        jobs_.destroy();
        lane_ready_.store(0, std::memory_order_relaxed);
    }
};

inline runtime_context* default_runtime() noexcept
{
    static runtime_context context{};
    return &context;
}

inline runtime_context*& current_runtime() noexcept
{
    runtime_context*& slot = tls().runtime_;
    if (slot == nullptr)
    {
        slot = default_runtime();
    }
    return slot;
}

// 绑定当前线程的运行时上下文；同时把 job 池覆盖点指向该上下文（供 job.hpp 的自由函数使用）
inline void bind_runtime(runtime_context* context) noexcept
{
    runtime_context* const target = context != nullptr ? context : default_runtime();
    tls().runtime_ = target;
    tls().job_pool_ = &target->jobs_;
}

// RAII 作用域绑定：外部线程临时操作某个 manager 的运行时
struct runtime_scope
{
    explicit runtime_scope(runtime_context* context) noexcept : previous_(current_runtime())
    {
        bind_runtime(context);
    }

    ~runtime_scope() noexcept
    {
        bind_runtime(previous_);
    }

    runtime_scope(const runtime_scope&) = delete;
    runtime_scope& operator=(const runtime_scope&) = delete;

  private:
    runtime_context* previous_;
};
} // namespace thread
