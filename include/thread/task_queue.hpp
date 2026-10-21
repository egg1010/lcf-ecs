#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "part/dense.hpp"
#include "thread/error.hpp"
#include "thread/task_identity.hpp"
#include "thread/task_table.hpp"
#include "thread/task_node.hpp"

#include "thread/mpmc_ring.hpp"

namespace thread
{

// ---------------------------------------------------------------------------
// 顺序域命令：任意线程生产，域线程单消费者
// ---------------------------------------------------------------------------

enum class task_op : std::uint8_t
{
    arm = 0,
    wake = 1,
    reorder = 2,
    migrate = 3,
    reparent = 4,
    set_mode = 5,
    set_priority = 6,
    disable = 7,
    enable = 8,
    cancel = 9,
    clear_cancel = 10,
    activate = 11,
};

struct task_command
{
    task_op op_{task_op::arm};
    std::uint32_t task_{invalid_task_index};
    std::uint32_t epoch_{0};
    std::uint64_t key_{0};
};

static_assert(sizeof(task_command) == 24, "task_command must stay 24 bytes");

// ---------------------------------------------------------------------------
// Chase-Lev 票据双端队列：所有者 push/pop，窃取者只能 steal
// ---------------------------------------------------------------------------

struct ticket_deque_buffer
{
    std::atomic<std::int64_t> capacity_{0};
    std::atomic<ticket_deque_buffer*> retired_next_{nullptr};
    std::atomic<std::uint64_t> slots_[1];
};

inline ticket_deque_buffer* ticket_deque_make_buffer(std::size_t capacity) noexcept
{
    const std::size_t bytes =
        sizeof(ticket_deque_buffer) + sizeof(std::atomic<std::uint64_t>) * (capacity - 1);
    auto* memory = static_cast<unsigned char*>(::operator new(bytes, std::nothrow));
    if (memory == nullptr)
    {
        return nullptr;
    }
    auto* buffer = new (memory) ticket_deque_buffer{};
    buffer->capacity_.store(static_cast<std::int64_t>(capacity), std::memory_order_relaxed);
    for (std::size_t i = 0; i < capacity; ++i)
    {
        new (&buffer->slots_[i]) std::atomic<std::uint64_t>(ticket::none_value);
    }
    return buffer;
}

inline void ticket_deque_free_buffer(ticket_deque_buffer* buffer) noexcept
{
    ::operator delete(static_cast<void*>(buffer));
}

struct ticket_deque
{
    ticket_deque() noexcept = default;

    ticket_deque(const ticket_deque&) = delete;
    ticket_deque& operator=(const ticket_deque&) = delete;

    ~ticket_deque()
    {
        destroy();
    }

    bool create(std::size_t capacity_hint) noexcept
    {
        if (buffer_.load(std::memory_order_relaxed) != nullptr)
        {
            return false;
        }
        const std::size_t capacity =
            ring_round_up(capacity_hint == 0 ? ring_default_capacity : capacity_hint);
        if (capacity < 2)
        {
            return false;
        }
        ticket_deque_buffer* buffer = ticket_deque_make_buffer(capacity);
        if (buffer == nullptr)
        {
            return false;
        }
        capacity_ = capacity;
        mask_ = capacity - 1;
        top_.store(0, std::memory_order_relaxed);
        bottom_.store(0, std::memory_order_relaxed);
        buffer_.store(buffer, std::memory_order_release);
        return true;
    }

    void destroy() noexcept
    {
        ticket_deque_buffer* buffer = buffer_.exchange(nullptr, std::memory_order_acq_rel);
        if (buffer != nullptr)
        {
            ticket_deque_free_buffer(buffer);
        }
        ticket_deque_buffer* retired = retired_.exchange(nullptr, std::memory_order_acq_rel);
        while (retired != nullptr)
        {
            ticket_deque_buffer* next = retired->retired_next_.load(std::memory_order_relaxed);
            ticket_deque_free_buffer(retired);
            retired = next;
        }
        capacity_ = 0;
        mask_ = 0;
        top_.store(0, std::memory_order_relaxed);
        bottom_.store(0, std::memory_order_relaxed);
    }

    std::int64_t size() const noexcept
    {
        return bottom_.load(std::memory_order_acquire) - top_.load(std::memory_order_acquire);
    }

    bool empty() const noexcept
    {
        return size() <= 0;
    }

    bool push(ticket value) noexcept
    {
        if (value.empty())
        {
            return false;
        }
        std::int64_t bottom = bottom_.load(std::memory_order_relaxed);
        std::int64_t top = top_.load(std::memory_order_acquire);
        while (static_cast<std::size_t>(bottom - top) >= capacity_)
        {
            if (!grow())
            {
                return false;
            }
            top = top_.load(std::memory_order_acquire);
        }
        ticket_deque_buffer* const buffer = buffer_.load(std::memory_order_relaxed);
        buffer->slots_[static_cast<std::size_t>(bottom) & mask_].store(value.value_,
                                                                      std::memory_order_relaxed);
        bottom_.store(bottom + 1, std::memory_order_release);
        return true;
    }

    ticket pop() noexcept
    {
        std::int64_t bottom = bottom_.load(std::memory_order_relaxed);
        ticket_deque_buffer* buffer = buffer_.load(std::memory_order_relaxed);
        if (buffer == nullptr || bottom == 0)
        {
            return ticket::none();
        }
        bottom_.store(bottom - 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const std::int64_t top = top_.load(std::memory_order_relaxed);
        bottom -= 1;

        if (top <= bottom)
        {
            const std::size_t index = static_cast<std::size_t>(bottom) & mask_;
            const std::uint64_t value = buffer->slots_[index].load(std::memory_order_relaxed);
            if (top < bottom)
            {
                return ticket{value};
            }
            std::int64_t expected = top;
            if (!top_.compare_exchange_strong(expected, expected + 1, std::memory_order_seq_cst,
                                              std::memory_order_relaxed))
            {
                bottom_.store(bottom + 1, std::memory_order_relaxed);
                return ticket::none();
            }
            bottom_.store(bottom + 1, std::memory_order_relaxed);
            return ticket{value};
        }

        bottom_.store(bottom + 1, std::memory_order_relaxed);
        if (top > bottom)
        {
            return ticket::none();
        }
        std::int64_t expected = top;
        top_.compare_exchange_strong(expected, expected + 1, std::memory_order_seq_cst,
                                     std::memory_order_relaxed);
        return ticket::none();
    }

    std::size_t steal(ticket* out, std::size_t max_count) noexcept
    {
        if (max_count == 0 || out == nullptr)
        {
            return 0;
        }
        std::int64_t top = top_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const std::int64_t bottom = bottom_.load(std::memory_order_relaxed);
        if (top >= bottom)
        {
            return 0;
        }

        const std::int64_t available = bottom - top;
        std::int64_t wanted = available / 2;
        const std::int64_t quarter = static_cast<std::int64_t>(capacity_ / 4);
        if (quarter > 0 && wanted > quarter)
        {
            wanted = quarter;
        }
        if (wanted > static_cast<std::int64_t>(max_count))
        {
            wanted = static_cast<std::int64_t>(max_count);
        }
        if (wanted < 1)
        {
            wanted = 1;
        }

        ticket_deque_buffer* const buffer = buffer_.load(std::memory_order_acquire);
        if (buffer == nullptr)
        {
            return 0;
        }
        const std::size_t buffer_mask =
            static_cast<std::size_t>(buffer->capacity_.load(std::memory_order_relaxed) - 1);

        if (!top_.compare_exchange_strong(top, top + wanted, std::memory_order_seq_cst,
                                          std::memory_order_relaxed))
        {
            return 0;
        }

        for (std::int64_t i = 0; i < wanted; ++i)
        {
            const std::size_t index = static_cast<std::size_t>(top + i) & buffer_mask;
            out[i] = ticket{buffer->slots_[index].load(std::memory_order_relaxed)};
        }
        return static_cast<std::size_t>(wanted);
    }

  private:
    bool grow() noexcept
    {
        ticket_deque_buffer* old_buffer = buffer_.load(std::memory_order_acquire);
        if (old_buffer == nullptr || capacity_ >= ring_max_capacity)
        {
            return false;
        }
        const std::size_t new_capacity = capacity_ * 2;
        ticket_deque_buffer* new_buffer = ticket_deque_make_buffer(new_capacity);
        if (new_buffer == nullptr)
        {
            return false;
        }
        const std::int64_t top = top_.load(std::memory_order_acquire);
        const std::int64_t bottom = bottom_.load(std::memory_order_acquire);
        const std::size_t new_mask = new_capacity - 1;
        for (std::int64_t i = top; i < bottom; ++i)
        {
            const std::uint64_t element =
                old_buffer->slots_[static_cast<std::size_t>(i) & mask_].load(
                    std::memory_order_relaxed);
            new_buffer->slots_[static_cast<std::size_t>(i) & new_mask].store(
                element, std::memory_order_relaxed);
        }
        capacity_ = new_capacity;
        mask_ = new_mask;
        buffer_.store(new_buffer, std::memory_order_release);

        ticket_deque_buffer* head = retired_.load(std::memory_order_relaxed);
        for (;;)
        {
            old_buffer->retired_next_.store(head, std::memory_order_relaxed);
            if (retired_.compare_exchange_weak(head, old_buffer, std::memory_order_acq_rel,
                                               std::memory_order_relaxed))
            {
                break;
            }
        }
        return true;
    }

    std::atomic<std::int64_t> top_{0};
    std::atomic<std::int64_t> bottom_{0};
    std::atomic<ticket_deque_buffer*> buffer_{nullptr};
    std::atomic<ticket_deque_buffer*> retired_{nullptr};
    std::size_t capacity_{0};
    std::size_t mask_{0};
};

// ---------------------------------------------------------------------------
// 顺序域就绪最小堆：按 (order_key, task_id) 全序
// ---------------------------------------------------------------------------

struct heap_entry
{
    std::uint64_t key_{0};
    std::uint32_t index_{invalid_task_index};
    std::uint32_t epoch_{0};
};

inline bool heap_entry_less(const heap_entry& a, const heap_entry& b) noexcept
{
    if (a.key_ != b.key_)
    {
        return a.key_ < b.key_;
    }
    return a.index_ < b.index_;
}

struct ready_heap
{
    dense<heap_entry> items_{};
    task_table* table_{nullptr};

    ready_heap() noexcept = default;

    ready_heap(const ready_heap&) = delete;
    ready_heap& operator=(const ready_heap&) = delete;

    bool create(std::size_t capacity, task_table& table) noexcept
    {
        table_ = &table;
        items_.reserve_exact(capacity);
        return true;
    }

    void destroy() noexcept
    {
        items_.clear();
        table_ = nullptr;
    }

    std::size_t size() const noexcept
    {
        return items_.size();
    }

    bool empty() const noexcept
    {
        return items_.empty();
    }

    void clear() noexcept
    {
        items_.clear();
    }

    bool push(const heap_entry& entry) noexcept
    {
        items_.push_back(entry);
        const std::size_t position = items_.size() - 1;
        set_slot(position, entry.index_);
        sift_up(position);
        return true;
    }

    bool pop(heap_entry& out) noexcept
    {
        if (items_.empty())
        {
            return false;
        }
        out = items_[0];
        task_node* root = node_of(out.index_);
        if (root != nullptr)
        {
            root->heap_index_.store(0, std::memory_order_relaxed);
        }
        const std::size_t last = items_.size() - 1;
        if (last > 0)
        {
            items_[0] = items_[last];
            items_.pop_back();
            set_slot(0, items_[0].index_);
            sift_down(0);
        }
        else
        {
            items_.pop_back();
        }
        return true;
    }

    // 已就绪任务改键；不在堆中则返回 false（下次入堆生效）
    bool reorder(std::uint32_t index, std::uint64_t key) noexcept
    {
        task_node* node = node_of(index);
        if (node == nullptr)
        {
            return false;
        }
        const std::uint32_t slot = node->heap_index_.load(std::memory_order_relaxed);
        if (slot == 0 || slot > items_.size())
        {
            return false;
        }
        const std::size_t position = slot - 1;
        items_[position].key_ = key;
        sift_up(position);
        const std::uint32_t moved = node->heap_index_.load(std::memory_order_relaxed);
        if (moved != 0 && moved <= items_.size())
        {
            sift_down(moved - 1);
        }
        return true;
    }

    const heap_entry* top() const noexcept
    {
        return items_.empty() ? nullptr : &items_[0];
    }

  private:
    task_node* node_of(std::uint32_t index) noexcept
    {
        return table_ != nullptr ? table_->at_index(index) : nullptr;
    }

    void set_slot(std::size_t position, std::uint32_t index) noexcept
    {
        task_node* node = node_of(index);
        if (node != nullptr)
        {
            node->heap_index_.store(static_cast<std::uint32_t>(position + 1),
                                    std::memory_order_relaxed);
        }
    }

    void sift_up(std::size_t position) noexcept
    {
        while (position > 0)
        {
            const std::size_t parent = (position - 1) / 2;
            if (!heap_entry_less(items_[position], items_[parent]))
            {
                break;
            }
            const heap_entry temp = items_[position];
            items_[position] = items_[parent];
            items_[parent] = temp;
            set_slot(position, items_[position].index_);
            set_slot(parent, items_[parent].index_);
            position = parent;
        }
    }

    void sift_down(std::size_t position) noexcept
    {
        const std::size_t count = items_.size();
        for (;;)
        {
            const std::size_t left = position * 2 + 1;
            if (left >= count)
            {
                break;
            }
            const std::size_t right = left + 1;
            std::size_t best = left;
            if (right < count && heap_entry_less(items_[right], items_[left]))
            {
                best = right;
            }
            if (!heap_entry_less(items_[best], items_[position]))
            {
                break;
            }
            const heap_entry temp = items_[position];
            items_[position] = items_[best];
            items_[best] = temp;
            set_slot(position, items_[position].index_);
            set_slot(best, items_[best].index_);
            position = best;
        }
    }
};
// 顺序域容器：MPSC 命令环 + 单写者就绪堆，只由域线程消费
struct task_sequencer
{
    thread::role id_{thread::role::general};
    order_policy policy_{order_policy::keyed};
    mpmc_ring<task_command> commands_{};
    ready_heap heap_{};

    task_sequencer() noexcept = default;

    task_sequencer(const task_sequencer&) = delete;
    task_sequencer& operator=(const task_sequencer&) = delete;

    bool create(thread::role id, std::size_t command_capacity, std::size_t heap_capacity,
                task_table& table, order_policy policy) noexcept
    {
        id_ = id;
        policy_ = policy;
        if (!commands_.create(command_capacity))
        {
            return false;
        }
        return heap_.create(heap_capacity, table);
    }

    void destroy() noexcept
    {
        commands_.destroy();
        heap_.destroy();
    }

    bool push_command(const task_command& command) noexcept
    {
        return commands_.try_push(command);
    }

    std::size_t drain_commands(task_command* out, std::size_t max_count) noexcept
    {
        return commands_.try_pop_bulk(out, max_count);
    }

    std::size_t command_pending() const noexcept
    {
        const std::int64_t depth = commands_.size();
        return depth > 0 ? static_cast<std::size_t>(depth) : 0;
    }

    bool insert(const heap_entry& entry) noexcept
    {
        return heap_.push(entry);
    }

    bool pop(heap_entry& out) noexcept
    {
        return heap_.pop(out);
    }

    bool reorder(std::uint32_t index, std::uint64_t key) noexcept
    {
        if (policy_ == order_policy::fifo)
        {
            return false;
        }
        return heap_.reorder(index, key);
    }

    std::size_t ready_size() const noexcept
    {
        return heap_.size();
    }

    void clear_ready() noexcept
    {
        heap_.clear();
    }
};
} // namespace thread
