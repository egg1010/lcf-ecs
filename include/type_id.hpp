#pragma once
#include "id_.hpp"

class type_id
{
private:
    inline static id_allocation<int> type_id_allocator{};
public:
    type_id() noexcept = default;
    
    template<typename T>
    [[nodiscard]] static int get_type_id() noexcept
    {
        static int id = type_id_allocator.get_id();
        return id;
    }

};