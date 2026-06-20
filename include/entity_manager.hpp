#pragma once

#include "entity.hpp"
#include "id_.hpp"

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
    static constexpr size_t signal_buffer_size = 256;
    signal_event signal_buffer_[signal_buffer_size]{};
    uint32_t signal_write_{0};
    uint32_t signal_read_{0};

    void push_signal(uint32_t type, uint32_t entity_idx) noexcept
    {
        uint32_t next = (signal_write_ + 1) % signal_buffer_size;
        if (next == signal_read_) [[unlikely]]
        {
            return;
        }
        signal_buffer_[signal_write_].type = type;
        signal_buffer_[signal_write_].entity_idx = entity_idx;
        signal_write_ = next;
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

        for (size_t i = 0; i < count; ++i)
        {
            uint32_t idx = id_manager_.get_id();
            ensure_version_capacity(idx);
            preallocated_entities_.emplace_back(entity(idx, version_v_[idx]));
        }
    }

    [[nodiscard]] bool is_version_valid(entity entitys) const noexcept
    {
        return entitys.parts_.index_ < version_v_.size() && entitys.parts_.version_ == version_v_[entitys.parts_.index_];
    }
    
    void destroy_entity(entity &entitys) noexcept
    {
        if(!is_version_valid(entitys)) [[unlikely]] return;
        if (on_entity_destroyed_) [[unlikely]] on_entity_destroyed_(entitys, on_entity_destroyed_data_);
        push_signal(1, entitys.parts_.index_);
        id_manager_.free_id(entitys.parts_.index_);
        version_v_[entitys.parts_.index_]++;
        if (entitys.parts_.index_ < entity_masks_.size())
        {
            entity_masks_[entitys.parts_.index_] = 0;
        }
    }

    void set_mask_bit(uint32_t entity_index, uint64_t bit) noexcept
    {
        if (entity_index >= entity_masks_.size()) [[unlikely]]
        {
            entity_masks_.resize(entity_index + 1, 0);
        }
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
        if (on_entity_created_) [[unlikely]] on_entity_created_(e, on_entity_created_data_);
        push_signal(0, e.parts_.index_);
        return e;
    }

    template <typename Func>
    void flush_signals(Func&& handler) noexcept
    {
        while (signal_read_ != signal_write_)
        {
            auto& ev = signal_buffer_[signal_read_];
            handler(ev.type, ev.entity_idx);
            signal_read_ = (signal_read_ + 1) % signal_buffer_size;
        }
    }

    [[nodiscard]] bool has_pending_signals() const noexcept
    {
        return signal_read_ != signal_write_;
    }
};