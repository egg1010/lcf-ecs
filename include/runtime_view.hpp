#pragma once
#include <limits>
#include <type_traits>
#include "class_pool.hpp"
#include "single_class_set.hpp"

namespace ecs
{

class manager;
struct component_meta;

// ======================== runtime_query ========================
class runtime_query
{
public:
    class_pool<int> required_ids_;
    uint64_t req_mask_{0};
    uint64_t exc_mask_{0};
    single_class_set* primary_set_{nullptr};

    runtime_query() noexcept = default;

    runtime_query(manager* mgr, class_pool<int> required_ids,
                  class_pool<int> excluded_ids = {}) noexcept;
};

// ======================== runtime_view ========================
class runtime_view
{
private:
    manager* mgr_;
    runtime_query query_;
    uint64_t cached_primary_version_{0};

    [[nodiscard]] bool all_sets_valid() const noexcept;

    void ensure_fresh() noexcept
    {
        if (query_.primary_set_ && query_.primary_set_->get_pool_version() != cached_primary_version_)
        {
            rebuild();
        }
    }

public:
    runtime_view(manager* mgr, runtime_query query) noexcept
        : mgr_(mgr), query_(std::move(query)) {}

    [[nodiscard]] size_t size() noexcept
    {
        ensure_fresh();
        return query_.primary_set_ ? query_.primary_set_->size() : 0;
    }

    [[nodiscard]] bool empty() noexcept
    {
        ensure_fresh();
        return query_.primary_set_ == nullptr || query_.primary_set_->empty();
    }

    [[nodiscard]] bool contains(entity e) noexcept;

    template <typename T>
    [[nodiscard]] T* get_ptr(entity e) noexcept;

    template <typename Func>
    void for_each(Func&& func) noexcept;

    void rebuild() noexcept;
};

}