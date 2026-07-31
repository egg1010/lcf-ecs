// stats.hpp - 序列化统计信息
#pragma once

#include "dense.hpp"
#include <cstdint>
#include <string>

struct serialize_stats
{
    size_t entity_count = 0;
    size_t total_bytes = 0;
    uint32_t archive_version = 0;

    struct type_stats
    {
        std::string type_name;
        size_t component_count = 0;
        size_t bytes = 0;
    };

    dense<type_stats> per_type;

    void reset() noexcept
    {
        entity_count = 0;
        total_bytes = 0;
        per_type.clear();
    }
};
