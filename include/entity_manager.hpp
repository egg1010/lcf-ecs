#pragma once

#include "entity.hpp"
#include "id_.hpp"
#include <vector>

class entity_manager
{
private:
    inline static id_allocation<uint32_t> id_manager_;
    std::vector<uint32_t> version_v_;
    
public:
    entity_manager(){}
    bool is_version_valid(entity entitys)
    {
        if(entitys.index_ >= version_v_.size())return false;
        return entitys.version_ == version_v_[entitys.index_];
    }
    void destroy_entity(entity &entitys)
    {
        if(!is_version_valid(entitys))return;
        id_manager_.free_id(entitys.index_);
        version_v_[entitys.index_]++;
    }
    entity get_entity()
    {
        uint32_t idx = id_manager_.get_id();
        if (idx >= version_v_.size()) {
            version_v_.resize(idx + 1, 0);
        }
        return entity(idx, version_v_[idx]);
    }

};