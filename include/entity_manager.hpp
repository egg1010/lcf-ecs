#pragma once

#include "entity.hpp"
#include "id_.hpp"
#include <vector>

class entity_manager
{
private:
    id_allocation<uint32_t> id_manager_; 
    std::vector<uint32_t> version_v_;
    
    std::vector<entity> preallocated_entities_;
    size_t current_preallocated_index_ = 0;
public:
    entity_manager() noexcept { append_preallocated_entities(500*1000); }
    
    void append_preallocated_entities(size_t count) noexcept
    {
        size_t initial_size = preallocated_entities_.size();
        preallocated_entities_.reserve(initial_size + count);
        
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t idx = id_manager_.get_id();
            if (idx >= version_v_.size()) [[unlikely]]
            {
                version_v_.resize(idx + 1, 1);
            }
            preallocated_entities_.emplace_back(entity(idx, version_v_[idx]));
        }
    }
    
    explicit entity_manager(size_t count) noexcept
    {
        append_preallocated_entities(count);
    }

    [[nodiscard]] bool is_version_valid(entity entitys) const noexcept
    {
        if(entitys.index_ >= version_v_.size()) [[unlikely]] return false;
        return entitys.version_ == version_v_[entitys.index_];
    }
    
    void destroy_entity(entity &entitys) noexcept
    {
        if(!is_version_valid(entitys)) [[unlikely]] return;
        id_manager_.free_id(entitys.index_);
        version_v_[entitys.index_]++;
    }
    
    [[nodiscard]] entity get_entity() noexcept
    {
        if (current_preallocated_index_ < preallocated_entities_.size()) [[likely]]
        {
            return preallocated_entities_[current_preallocated_index_++];
        }
        else [[unlikely]]
        {
            uint32_t idx = id_manager_.get_id();
            if (idx >= version_v_.size()) [[unlikely]]
            {
                version_v_.resize(idx + 1, 1);
            }
            return entity(idx, version_v_[idx]);
        }
    }
};