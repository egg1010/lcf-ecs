#pragma once
#include <cstdint>

namespace ecs
{

struct entity
{
    constexpr entity() noexcept : handle_(0) {}
    constexpr entity(uint32_t idx, uint32_t v) noexcept : parts_{idx, v} {}

    union
    {
        uint64_t handle_;
        struct
        {
            uint32_t index_;
            uint32_t version_;
        } parts_;
    };

    constexpr bool operator==(const entity& other) const noexcept { return handle_ == other.handle_; }
    constexpr bool operator!=(const entity& other) const noexcept { return handle_ != other.handle_; }
    constexpr bool is_valid() const noexcept { return handle_ != 0; }
};

} // namespace ecs

namespace std
{
    template<> struct hash<ecs::entity>
    {
        size_t operator()(const ecs::entity& e) const
        {
            return hash<uint64_t>()(e.handle_);
        }
    };
}