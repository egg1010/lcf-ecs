#pragma once
#include "id_.hpp"

#define FORCE_INLINE inline

class type_id
{
private:
    inline static id_allocation<int> type_id_allocator{};
public:
    type_id() noexcept = default;
    
    template<typename T>
    [[nodiscard]] static FORCE_INLINE
    int get_type_id() noexcept
    {
        static int id = type_id_allocator.get_id();
        return id;
    }

};
