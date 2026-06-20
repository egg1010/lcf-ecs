#pragma once
#include "id_.hpp"
#include "force_inline.hpp"

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