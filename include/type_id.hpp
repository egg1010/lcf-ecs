#pragma once
#include "id_.hpp"

class type_id
{
private:
    id_allocation<int> type_id_allocator{};
public:
    type_id()=default;
    
    template<typename T>
    static int get_type_id()
    {
        static int id = type_id_allocator.get_id();
        return id;
    }

};