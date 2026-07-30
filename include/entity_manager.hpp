#pragma once

#include "entity.hpp"
#include "part/id_.hpp"
#include "part/dense.hpp"
#include "part/ring_buffer.hpp"
#include "part/multi_block_bitmask.hpp"

namespace ecs
{

// 实体状态标志（位掩码）
enum class entity_flag : uint32_t
{
    active          = 1 << 0,
    disabled        = 1 << 1,
    pending_destroy = 1 << 2,
    static_entity   = 1 << 3,
};

// 实体状态池条目（16 bytes）
struct entity_state
{
    uint32_t flags;
    uint32_t tag;
    uint32_t layer;
    uint32_t group_id;
};

class entity_manager
{
public:
    void (*on_entity_created_)(entity, void* user_data) noexcept = nullptr;
    void* on_entity_created_data_{nullptr};
    void (*on_entity_destroyed_)(entity, void* user_data) noexcept = nullptr;
    void* on_entity_destroyed_data_{nullptr};

private:
    id_allocation<uint32_t> id_manager_;
    dense<uint32_t> version_v_;
    multi_block_bitmask masks_;
    dense<entity_state> entity_states_;

    dense<entity> preallocated_entities_;
    size_t current_preallocated_index_ = 0;

    struct signal_event
    {
        uint32_t type;        // 0=entity_created, 1=entity_destroyed
        uint32_t entity_idx;
    };
    static constexpr size_t signal_buffer_size = 1024;
    static_assert((signal_buffer_size & (signal_buffer_size - 1)) == 0,
                  "signal_buffer_size must be power of 2");
    ring_buffer<signal_event, signal_buffer_size> signal_buf_;
    dense<signal_event> signal_overflow_chain_;
    size_t signal_overflow_read_{0};
    uint64_t signal_overflow_count_{0};
    bool entity_signal_enabled_{true};
    bool entity_signal_flushing_{false};
    uint32_t entity_reentrancy_depth_{0};

    void push_signal(uint32_t type, uint32_t entity_idx) noexcept
    {
        if (!entity_signal_enabled_) [[unlikely]] return;
        if (!signal_buf_.push(signal_event{type, entity_idx})) [[unlikely]]
        {
            ++signal_overflow_count_;
            signal_overflow_chain_.push_back(signal_event{type, entity_idx});
        }
    }

    // 即时回调与延迟队列互斥:注册了即时回调且非重入时同步调用,否则入队
    void notify_created(entity e) noexcept
    {
        if (entity_reentrancy_depth_ == 0 && on_entity_created_) [[unlikely]]
        {
            ++entity_reentrancy_depth_;
            on_entity_created_(e, on_entity_created_data_);
            --entity_reentrancy_depth_;
        }
        else
        {
            push_signal(0, e.parts_.index_);
        }
    }

    void notify_destroyed(entity e) noexcept
    {
        if (entity_reentrancy_depth_ == 0 && on_entity_destroyed_) [[unlikely]]
        {
            ++entity_reentrancy_depth_;
            on_entity_destroyed_(e, on_entity_destroyed_data_);
            --entity_reentrancy_depth_;
        }
        else
        {
            push_signal(1, e.parts_.index_);
        }
    }

    void ensure_version_capacity(uint32_t idx) noexcept
    {
        if (idx >= version_v_.size()) [[unlikely]]
        {
            version_v_.increase_capacity(idx + 1, 1);
        }
        masks_.ensure_entity(idx);
        if (idx >= entity_states_.size()) [[unlikely]]
        {
            entity_states_.increase_capacity(idx + 1, entity_state{static_cast<uint32_t>(entity_flag::active), 0, 0, 0});
        }
    }

    [[nodiscard]] entity allocate_entity() noexcept
    {
        uint32_t idx = id_manager_.get_id();
        ensure_version_capacity(idx);
        return entity(idx, version_v_[idx]);
    }

public:
    entity_manager() noexcept = default;

    explicit entity_manager(size_t prealloc_count) noexcept
    {
        append_preallocated_entities(prealloc_count);
    }

    void append_preallocated_entities(size_t count) noexcept
    {
        size_t initial_size = preallocated_entities_.size();
        preallocated_entities_.increase_capacity(initial_size + count);

        size_t max_idx = count + 1;
        if (max_idx > version_v_.size())
        {
            version_v_.increase_capacity(max_idx, 1);
        }
        if (max_idx > entity_states_.size())
        {
            entity_states_.increase_capacity(max_idx, entity_state{static_cast<uint32_t>(entity_flag::active), 0, 0, 0});
        }
        masks_.resize_entities(static_cast<uint32_t>(max_idx));

        for (size_t i = 0; i < count; ++i)
        {
            uint32_t idx = id_manager_.get_id();
            preallocated_entities_.push_back_unchecked(entity(idx, version_v_[idx]));
        }
    }

    [[nodiscard]] bool is_version_valid(entity entitys) const noexcept
    {
        return entitys.parts_.index_ < version_v_.size() && entitys.parts_.version_ == version_v_[entitys.parts_.index_];
    }

    [[nodiscard]] size_t maximum_entity_index() const noexcept { return version_v_.size(); }

    [[nodiscard]] uint32_t get_version(uint32_t idx) const noexcept
    {
        return idx < version_v_.size() ? version_v_[idx] : 0;
    }

    void destroy_entity(entity &entitys) noexcept
    {
        if(!is_version_valid(entitys)) [[unlikely]] return;
        notify_destroyed(entitys);
        id_manager_.free_id(entitys.parts_.index_);
        version_v_[entitys.parts_.index_]++;
        masks_.clear_entity(entitys.parts_.index_);
        if (entitys.parts_.index_ < entity_states_.size())
        {
            entity_states_[entitys.parts_.index_] = entity_state{};
        }
    }

    void set_mask_bit(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
    {
        masks_.set_bit(entity_index, block_idx, bit_offset);
    }

    void set_mask_bit_no_bounds_check(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
    {
        masks_.set_bit_no_check(entity_index, block_idx, bit_offset);
    }

    void clear_mask_bit(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
    {
        masks_.clear_bit(entity_index, block_idx, bit_offset);
    }

    void clear_mask_bit_no_bounds_check(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
    {
        masks_.clear_bit_no_check(entity_index, block_idx, bit_offset);
    }

    // 预分配掩码块数 — 注册组件前调用避免 reshape
    void reserve_mask_blocks(uint32_t num_blocks) noexcept
    {
        masks_.reserve_blocks(num_blocks);
    }

    [[nodiscard]] uint32_t num_mask_blocks() const noexcept
    {
        return masks_.num_blocks();
    }

    // 已分配的实体索引上限 (用于遍历)
    [[nodiscard]] uint32_t entity_index_count() const noexcept
    {
        return static_cast<uint32_t>(version_v_.size());
    }

    // 取指定索引的当前版本号
    [[nodiscard]] uint32_t get_version_at(uint32_t idx) const noexcept
    {
        return idx < version_v_.size() ? version_v_[idx] : 0;
    }

    // 遍历实体所有置位 bit — O(实体实际组件数) 而非 O(类型总数)
    template <typename Func>
    void for_each_set_bit(uint32_t entity_index, Func&& func) const noexcept
    {
        masks_.for_each_set_bit(entity_index, std::forward<Func>(func));
    }

    [[nodiscard]] uint64_t get_mask(uint32_t entity_index) const noexcept
    {
        return masks_.get_block(entity_index, 0);
    }

    [[nodiscard]] uint64_t get_block(uint32_t entity_index, uint32_t block_idx) const noexcept
    {
        return masks_.get_block(entity_index, block_idx);
    }

    [[nodiscard]] entity get_entity() noexcept
    {
        entity e = current_preallocated_index_ < preallocated_entities_.size()
            ? preallocated_entities_[current_preallocated_index_++]
            : allocate_entity();
        notify_created(e);
        return e;
    }

    template <typename Func>
    void flush_signals(Func&& handler) noexcept
    {
        // 防 flush 递归重入
        if (entity_signal_flushing_) [[unlikely]] return;
        entity_signal_flushing_ = true;
        // 循环上限防止 handler 内追加导致无限循环
        uint64_t budget = signal_buffer_size * 4 + signal_overflow_chain_.size();
        size_t processed = signal_buf_.drain_with_budget(
            static_cast<size_t>(budget),
            [&](const signal_event& ev) noexcept { handler(ev.type, ev.entity_idx); });
        budget -= processed;
        while (budget > 0 && signal_overflow_read_ < signal_overflow_chain_.size())
        {
            auto& ev = signal_overflow_chain_[signal_overflow_read_];
            handler(ev.type, ev.entity_idx);
            ++signal_overflow_read_;
            --budget;
        }
        if (signal_overflow_read_ == signal_overflow_chain_.size() && signal_overflow_chain_.size() > 0)
        {
            signal_overflow_chain_.clear();
            signal_overflow_read_ = 0;
        }
        entity_signal_flushing_ = false;
    }

    [[nodiscard]] bool has_pending_signals() const noexcept
    {
        return signal_buf_.has_pending() || signal_overflow_read_ < signal_overflow_chain_.size();
    }

    void enable_entity_signals() noexcept { entity_signal_enabled_ = true; }
    void disable_entity_signals() noexcept { entity_signal_enabled_ = false; }

    [[nodiscard]] uint64_t signal_overflow_count() const noexcept { return signal_overflow_count_; }
    void reset_signal_overflow_count() noexcept { signal_overflow_count_ = 0; }

    void reserve_signal_capacity(size_t n) noexcept
    {
        signal_overflow_chain_.increase_capacity(n);
    }

    [[nodiscard]] entity_state& get_entity_state(uint32_t entity_index) noexcept
    {
        return entity_states_[entity_index];
    }

    [[nodiscard]] const entity_state& get_entity_state(uint32_t entity_index) const noexcept
    {
        return entity_states_[entity_index];
    }

    void set_entity_flag(uint32_t entity_index, entity_flag f) noexcept
    {
        if (entity_index < entity_states_.size()) [[likely]]
        {
            entity_states_[entity_index].flags |= static_cast<uint32_t>(f);
        }
    }

    void clear_entity_flag(uint32_t entity_index, entity_flag f) noexcept
    {
        if (entity_index < entity_states_.size()) [[likely]]
        {
            entity_states_[entity_index].flags &= ~static_cast<uint32_t>(f);
        }
    }

    [[nodiscard]] bool has_entity_flag(uint32_t entity_index, entity_flag f) const noexcept
    {
        if (entity_index >= entity_states_.size()) [[unlikely]] return false;
        return (entity_states_[entity_index].flags & static_cast<uint32_t>(f)) != 0;
    }
};

} // namespace ecs
