#pragma once
#include <limits>
#include <type_traits>
#include <cstdint>
#include "part/dense.hpp"
#include "single_class_set.hpp"

namespace ecs
{

class manager;
struct component_meta;

enum class access_mode : uint8_t
{
    read_only,
    read_write
};

struct runtime_term
{
    int type_id{0};
    // 0=and 1=or 2=not 3=optional
    uint8_t op{0};
    access_mode access{access_mode::read_write};
};

// 命中实体缓存条目: entity + primary set 的 dense 索引
//   primary_dense 在 build_cached_hits 时确定, 用于 for_each_typed 跳过 primary 组件的稀疏查找
struct cached_hit
{
    entity e;
    uint32_t primary_dense;
};

class runtime_query
{
public:
    dense<int> required_ids_;
    dense<uint64_t> req_masks_;
    dense<uint64_t> exc_masks_;
    uint32_t max_block_{0};
    single_class_set* primary_set_{nullptr};
    bool use_mask_path_{true};
    dense<single_class_set*> req_sets_;
    dense<single_class_set*> exc_sets_;

    // term 支持(OR/OPTIONAL)
    dense<runtime_term> terms_;
    bool has_or_{false};
    bool has_optional_{false};
    dense<access_mode> req_access_;
    dense<single_class_set*> or_sets_;
    dense<single_class_set*> opt_sets_;

    runtime_query() noexcept = default;

    runtime_query(manager* mgr, std::span<const int> required_ids,
                  std::span<const int> excluded_ids = {}) noexcept;

    // term 构造(支持 OR/OPTIONAL)
    runtime_query(manager* mgr, std::span<const runtime_term> terms) noexcept;

    // 多块掩码检查(entity_index 已保证有效)
    [[nodiscard]] bool check_blocks(uint32_t entity_index, const manager* mgr) const noexcept;
};

class runtime_view
{
private:
    manager* mgr_;
    runtime_query query_;
    uint64_t cached_primary_version_{0};

    dense<uint64_t> baseline_versions_;
    bool tracking_changes_{false};

    dense<entity> sorted_entities_;
    bool sorted_valid_{false};

    // 命中实体缓存 (rebuild 时构建, for_each/count 复用)
    //   避免每次 for_each 都全量遍历 primary 的 indices
    dense<cached_hit> cached_hits_;
    bool cached_hits_valid_{false};

    [[nodiscard]] bool all_sets_valid() const noexcept;

    void ensure_fresh() noexcept
    {
        if (query_.primary_set_ && query_.primary_set_->get_pool_version() != cached_primary_version_)
        {
            rebuild();
        }
    }

    // 构建命中实体缓存 (rebuild 时构建, for_each/count 复用)
    void build_cached_hits() noexcept;
    void ensure_hits_fresh() noexcept;

    // 内部:遍历命中实体,回调 (entity, primary_dense_index)
    template <typename Func>
    void for_each_hit_impl(Func&& func) noexcept;

    template <typename Func>
    void for_each_hit_range(size_t start, size_t end, Func&& func) noexcept;

    // 内部:检查实体是否命中(含 OR/OPTIONAL)
    [[nodiscard]] bool is_entity_hit(uint32_t idx, uint32_t ver) noexcept;

public:
    runtime_view(manager* mgr, runtime_query query) noexcept
        : mgr_(mgr), query_(std::move(query)) {}

    [[nodiscard]] size_t size() noexcept;
    [[nodiscard]] bool empty() noexcept;
    [[nodiscard]] bool contains(entity e) noexcept;
    [[nodiscard]] entity get_first_entity() noexcept;
    template <typename T>
    [[nodiscard]] T* get_ptr(entity e) noexcept;
    template <typename Func>
    void for_each(Func&& func) noexcept;
    void rebuild() noexcept;

    // 1. 组件引用回传(编译期类型 Ts 对应 required_ids_ 顺序)
    template <typename... Ts, typename Func>
    void for_each_typed(Func&& func) noexcept;
    template <typename... Ts, typename Func, size_t... Is>
    void for_each_typed_impl(Func&& func, std::index_sequence<Is...>) noexcept;

    // 2. 并行迭代(按 primary dense 分片,外部线程池驱动)
    template <typename Func>
    void for_each_parallel(size_t worker_id, size_t worker_count, Func&& func) noexcept;

    template <typename Func>
    void for_each_paged(size_t offset, size_t limit, Func&& func) noexcept;

    [[nodiscard]] bool changed() noexcept;
    void reset_change_tracking() noexcept;
    template <typename Func>
    void for_each_changed(Func&& func) noexcept;

    template <typename T, typename Compare>
    void sort_by_component(Compare&& cmp) noexcept;
    [[nodiscard]] const dense<entity>& get_sorted_entities() const noexcept
    {
        return sorted_entities_;
    }

    [[nodiscard]] size_t count() noexcept;

    class iterator;
    [[nodiscard]] iterator begin() noexcept;
    [[nodiscard]] iterator end() noexcept;

    // 8. OR/OPTIONAL 查询通过 runtime_term 构造支持
    // 9. const/读写标注通过 runtime_term.access 设置
};

class runtime_view::iterator
{
private:
    const runtime_query* query_{nullptr};
    manager* mgr_{nullptr};
    size_t index_{0};
    entity current_{};
    // cached_hits_ 快速路径: 已预过滤的命中实体数组
    //   非空时 advance_to_valid 直接索引, 跳过 check_blocks + sparse_entry 查找
    const cached_hit* hits_{nullptr};
    size_t hits_size_{0};
    void advance_to_valid() noexcept;

public:
    iterator() noexcept = default;
    iterator(const runtime_query& query, manager* mgr, size_t idx) noexcept
        : query_(&query), mgr_(mgr), index_(idx)
    {
        advance_to_valid();
    }

    // cached_hits 快速路径构造: 直接遍历预过滤的命中实体
    iterator(const dense<cached_hit>& hits, size_t idx) noexcept
        : index_(idx), hits_(hits.data()), hits_size_(hits.size())
    {
        advance_to_valid();
    }

    [[nodiscard]] entity operator*() const noexcept
    {
        return current_;
    }

    iterator& operator++() noexcept
    {
        ++index_;
        advance_to_valid();
        return *this;
    }

    bool operator==(const iterator& o) const noexcept
    {
        return index_ == o.index_;
    }

    bool operator!=(const iterator& o) const noexcept
    {
        return index_ != o.index_;
    }
};

} // namespace ecs
