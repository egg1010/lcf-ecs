#pragma once
#include <cstdint>

struct entity
{
    entity() : handle_(0) {}
    entity(uint32_t idx, uint32_t v) : index_(idx), version_(v) {}

    union 
    {
        uint64_t  handle_;
        struct 
        {
            uint32_t index_;
            uint32_t version_;
        };
    };


    bool operator==(const entity& other) const { return handle_ == other.handle_; }
    bool operator!=(const entity& other) const { return handle_ != other.handle_; }
    
    bool is_valid() const { return handle_!=0; }
};




namespace std 
{
    template<> struct hash<entity> 
    {
        size_t operator()(const entity& e) const 
        {
            return hash<uint64_t>()(e.handle_);
        }
    };
}