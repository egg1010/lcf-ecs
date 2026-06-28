#pragma once

#include "entity.hpp"
#include "part/id_.hpp"
#include "part/class_pool.hpp"

class entity_manager
{
public:
    void (*on_entity_created_)(entity, void* user_data) noexcept = nullptr;
    void* on_entity_created_data_{nullptr};
    void (*on_entity_destroyed_)(entity, void* user_data) noexcept = nullptr;
    void* on_entity_destroyed_data_{nullptr};

private:
    id_allocation<uint32_t> id_manager_;
    class_pool<uint32_t> version_v_;
    class_pool<uint64_t> entity_masks_;

    class_pool<entity> preallocated_entities_;
    size_t current_preallocated_index_ = 0;

    struct signal_event
    {
        uint32_t type;        // 0=entity_created, 1=entity_destroyed
        uint32_t entity_idx;
    };
    static constexpr size_t signal_buffer_size = 1024;
    static_assert((signal_buffer_size & (signal_buffer_size - 1)) == 0,
                  "signal_buffer_size must be power of 2");
    signal_event signal_buffer_[signal_buffer_size]{};
    uint32_t signal_write_{0};
    uint32_t signal_read_{0};
    class_pool<signal_event> signal_overflow_chain_;
    size_t signal_overflow_read_{0};
    uint64_t signal_overflow_count_{0};
    bool entity_signal_enabled_{true};
    bool entity_signal_flushing_{false};
    uint32_t entity_reentrancy_depth_{0};

    void push_signal(uint32_t type, uint32_t entity_idx) noexcept
    {
        if (!entity_signal_enabled_) [[unlikely]] return;
        uint32_t next = (signal_write_ + 1) & (signal_buffer_size - 1);
        if (next == signal_read_) [[unlikely]]
        {
            ++signal_overflow_count_;
            signal_overflow_chain_.emplace_back(signal_event{type, entity_idx});
            return;
        }
        signal_buffer_[signal_write_] = {type, entity_idx};
        signal_write_ = next;
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
            version_v_.resize(idx + 1, 1);
        }
        if (idx >= entity_masks_.size()) [[unlikely]]
        {
            entity_masks_.resize(idx + 1, 0);
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
            version_v_.increase_capacity(max_idx);
            version_v_.resize(max_idx, 1);
        }
        if (max_idx > entity_masks_.size())
        {
            entity_masks_.increase_capacity(max_idx);
            entity_masks_.resize(max_idx, 0);
        }

        for (size_t i = 0; i < count; ++i)
        {
            uint32_t idx = id_manager_.get_id();
            preallocated_entities_.emplace_back_unchecked(entity(idx, version_v_[idx]));
        }
    }

    [[nodiscard]] bool is_version_valid(entity entitys) const noexcept
    {
        return entitys.parts_.index_ < version_v_.size() && entitys.parts_.version_ == version_v_[entitys.parts_.index_];
    }

    void destroy_entity(entity &entitys) noexcept
    {
        if(!is_version_valid(entitys)) [[unlikely]] return;
        notify_destroyed(entitys);
        id_manager_.free_id(entitys.parts_.index_);
        version_v_[entitys.parts_.index_]++;
        if (entitys.parts_.index_ < entity_masks_.size())
        {
            entity_masks_[entitys.parts_.index_] = 0;
        }
    }

    void set_mask_bit(uint32_t entity_index, uint64_t bit) noexcept
    {
        if (entity_index < entity_masks_.size()) [[likely]]
        {
            entity_masks_[entity_index] |= bit;
        }
        else [[unlikely]]
        {
            entity_masks_.resize(entity_index + 1, 0);
            entity_masks_[entity_index] |= bit;
        }
    }

    void set_mask_bit_no_bounds_check(uint32_t entity_index, uint64_t bit) noexcept
    {
        entity_masks_[entity_index] |= bit;
    }

    void clear_mask_bit(uint32_t entity_index, uint64_t bit) noexcept
    {
        if (entity_index < entity_masks_.size())
        {
            entity_masks_[entity_index] &= ~bit;
        }
    }

    [[nodiscard]] uint64_t get_mask(uint32_t entity_index) const noexcept
    {
        if (entity_index >= entity_masks_.size()) [[unlikely]] return 0;
        return entity_masks_[entity_index];
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
        while (budget > 0 && signal_read_ != signal_write_)
        {
            auto& ev = signal_buffer_[signal_read_];
            handler(ev.type, ev.entity_idx);
            signal_read_ = (signal_read_ + 1) & (signal_buffer_size - 1);
            --budget;
        }
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
        return signal_read_ != signal_write_ || signal_overflow_read_ < signal_overflow_chain_.size();
    }

    void enable_entity_signals() noexcept { entity_signal_enabled_ = true; }
    void disable_entity_signals() noexcept { entity_signal_enabled_ = false; }

    [[nodiscard]] uint64_t signal_overflow_count() const noexcept { return signal_overflow_count_; }
    void reset_signal_overflow_count() noexcept { signal_overflow_count_ = 0; }

    void reserve_signal_capacity(size_t n) noexcept
    {
        signal_overflow_chain_.increase_capacity(n);
    }
};
