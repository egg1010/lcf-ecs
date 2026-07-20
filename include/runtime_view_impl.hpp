// runtime_view_impl.hpp —— runtime_query / runtime_view 的 out-of-line 定义
//      以及 filter_view 的 and_ / or_ 方法
// 依赖 manager 完整定义,由 component.hpp 在 manager 类定义之后 include
#pragma once
#include "runtime_view.hpp"
#include "single_class_set.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>
#include <tuple>

namespace ecs
{

namespace detail
{
// 从 set 按实体索引获取组件指针
template <typename T>
inline T* get_component_ptr_from_set(single_class_set* set,
                                     uint32_t idx, uint32_t ver) noexcept
{
    if (!set) return nullptr;
    if (idx >= set->get_sparse_size()) return nullptr;
    const uint32_t dense = set->sparse_dense_at_public(idx);
    if (dense == single_class_set::dense_invalid) return nullptr;
    const uint32_t v = set->sparse_version_at_public(idx);
    if (v != ver) return nullptr;
    auto* pool = set->template get_typed_pool_ptr<T>();
    if (!pool || dense >= pool->size()) return nullptr;
    return &(*pool)[dense];
}
} // namespace detail

// ======================== runtime_query out-of-line 定义 ========================

inline runtime_query::runtime_query(manager* mgr, class_pool<int> required_ids,
                                     class_pool<int> excluded_ids) noexcept
    : required_ids_(std::move(required_ids))
{
    if (required_ids_.empty()) [[unlikely]] return;

    auto block_of = [](int tid) noexcept -> uint32_t {
        return static_cast<uint32_t>(tid - 1) / 64;
    };

    size_t min_size = std::numeric_limits<size_t>::max();
    for (int tid : required_ids_)
    {
        uint32_t b = block_of(tid);
        if (b > max_block_) max_block_ = b;
        auto* set = mgr->get_single_class_set_by_id(tid);
        req_sets_.emplace_back(set);
        if (set && set->size() < min_size)
        {
            min_size = set->size();
            primary_set_ = set;
        }
    }

    for (int tid : excluded_ids)
    {
        uint32_t b = block_of(tid);
        if (b > max_block_) max_block_ = b;
        exc_sets_.emplace_back(mgr->get_single_class_set_by_id(tid));
    }

    req_masks_.resize(max_block_ + 1, 0);
    exc_masks_.resize(max_block_ + 1, 0);
    for (int tid : required_ids_)
    {
        uint32_t block = block_of(tid);
        uint32_t offset = static_cast<uint32_t>(tid - 1) % 64;
        req_masks_[block] |= (1ULL << offset);
    }
    for (int tid : excluded_ids)
    {
        uint32_t block = block_of(tid);
        uint32_t offset = static_cast<uint32_t>(tid - 1) % 64;
        exc_masks_[block] |= (1ULL << offset);
    }

    use_mask_path_ = (req_sets_.size() >= 3) || ((max_block_ + 1) <= 5);
}

inline runtime_query::runtime_query(manager* mgr, class_pool<runtime_term> terms) noexcept
{
    if (terms.empty()) [[unlikely]] return;

    auto block_of = [](int tid) noexcept -> uint32_t {
        return static_cast<uint32_t>(tid - 1) / 64;
    };

    // 第一遍: 收集所有类型, 计算 max_block
    for (const auto& term : terms)
    {
        uint32_t b = block_of(term.type_id);
        if (b > max_block_) max_block_ = b;
    }

    req_masks_.resize(max_block_ + 1, 0);
    exc_masks_.resize(max_block_ + 1, 0);

    // term 分发上下文
    struct term_ctx
    {
        runtime_query* self;
        size_t min_size;
    } ctx{this, std::numeric_limits<size_t>::max()};

    using term_handler = void(*)(term_ctx&, const runtime_term&, single_class_set*) noexcept;

    static constexpr term_handler dispatch[4] = {
        // op=0 AND
        [](term_ctx& c, const runtime_term& t, single_class_set* set) noexcept
        {
            c.self->required_ids_.emplace_back(t.type_id);
            c.self->req_sets_.emplace_back(set);
            c.self->req_access_.emplace_back(t.access);
            uint32_t block = static_cast<uint32_t>(t.type_id - 1) / 64;
            uint32_t offset = static_cast<uint32_t>(t.type_id - 1) % 64;
            c.self->req_masks_[block] |= (1ULL << offset);
            if (set && set->size() < c.min_size)
            {
                c.min_size = set->size();
                c.self->primary_set_ = set;
            }
        },
        // op=1 OR
        [](term_ctx& c, const runtime_term&, single_class_set* set) noexcept
        {
            c.self->has_or_ = true;
            c.self->or_sets_.emplace_back(set);
        },
        // op=2 NOT
        [](term_ctx& c, const runtime_term& t, single_class_set* set) noexcept
        {
            c.self->exc_sets_.emplace_back(set);
            uint32_t block = static_cast<uint32_t>(t.type_id - 1) / 64;
            uint32_t offset = static_cast<uint32_t>(t.type_id - 1) % 64;
            c.self->exc_masks_[block] |= (1ULL << offset);
        },
        // op=3 OPTIONAL
        [](term_ctx& c, const runtime_term&, single_class_set* set) noexcept
        {
            c.self->has_optional_ = true;
            c.self->opt_sets_.emplace_back(set);
        }
    };

    for (const auto& term : terms)
    {
        terms_.emplace_back(term);
        auto* set = mgr->get_single_class_set_by_id(term.type_id);

        if (term.op < 4) [[likely]]
        {
            dispatch[term.op](ctx, term, set);
        }
    }

    use_mask_path_ = !has_or_ && ((req_sets_.size() >= 3) || ((max_block_ + 1) <= 5));
}

// ======================== runtime_view out-of-line 定义 ========================

inline bool runtime_view::all_sets_valid() const noexcept
{
    for (int tid : query_.required_ids_)
    {
        if (!mgr_->get_single_class_set_by_id(tid)) return false;
    }
    return true;
}

inline bool runtime_view::is_entity_hit(uint32_t idx, uint32_t ver) noexcept
{
    for (size_t k = 0; k < query_.req_sets_.size(); ++k)
    {
        if (!single_class_set::sparse_contains_version(query_.req_sets_[k], idx, ver))
            return false;
    }
    for (size_t k = 0; k < query_.exc_sets_.size(); ++k)
    {
        if (single_class_set::sparse_contains_version(query_.exc_sets_[k], idx, ver))
            return false;
    }
    if (query_.has_or_)
    {
        bool or_hit = false;
        for (size_t k = 0; k < query_.or_sets_.size(); ++k)
        {
            if (single_class_set::sparse_contains_version(query_.or_sets_[k], idx, ver))
            {
                or_hit = true;
                break;
            }
        }
        if (!or_hit) return false;
    }
    return true;
}

inline bool runtime_view::contains(entity e) noexcept
{
    ensure_fresh();
    if (!all_sets_valid()) [[unlikely]] return false;
    if (query_.use_mask_path_)
    {
        return query_.check_blocks(e.parts_.index_, mgr_);
    }
    return is_entity_hit(e.parts_.index_, e.parts_.version_);
}

inline entity runtime_view::get_first_entity() noexcept
{
    ensure_fresh();
    if (query_.primary_set_ == nullptr || !all_sets_valid()) [[unlikely]] return entity{};

    auto* primary = query_.primary_set_;
    auto& indices = primary->get_entity_indices();

    if (query_.use_mask_path_ && !query_.has_or_)
    {
        const sparse_entry* rv_cur_ver_page = nullptr;
        size_t rv_cur_page_idx = SIZE_MAX;
        for (size_t i = 0; i < indices.size(); ++i)
        {
            uint32_t idx = indices[i];
            if (!query_.check_blocks(idx, mgr_)) continue;
            size_t pid = idx >> primary->page_shift;
            if (pid != rv_cur_page_idx) [[unlikely]]
            {
                rv_cur_ver_page = primary->get_version_page(idx);
                rv_cur_page_idx = pid;
            }
            uint32_t entry = 0;
            if (rv_cur_ver_page) [[likely]]
                entry = single_class_set::read_version_from_page(rv_cur_ver_page, idx, primary->page_mask);
            return entity(idx, entry);
        }
        return entity{};
    }

    for (size_t i = 0; i < indices.size(); ++i)
    {
        uint32_t idx = indices[i];
        uint32_t ver = primary->get_version_unchecked(idx);
        if (is_entity_hit(idx, ver))
        {
            return entity(idx, ver);
        }
    }
    return entity{};
}

inline void runtime_view::rebuild() noexcept
{
    size_t min_size = std::numeric_limits<size_t>::max();
    query_.primary_set_ = nullptr;
    for (int tid : query_.required_ids_)
    {
        auto* set = mgr_->get_single_class_set_by_id(tid);
        if (set && set->size() < min_size)
        {
            min_size = set->size();
            query_.primary_set_ = set;
        }
    }
    if (query_.primary_set_)
    {
        cached_primary_version_ = query_.primary_set_->get_pool_version();
    }
    sorted_valid_ = false;
}

template <typename T>
inline T* runtime_view::get_ptr(entity e) noexcept
{
    ensure_fresh();
    return mgr_->template get_ptr_fast<T>(e);
}

// ===== 内部:遍历命中实体 =====

template <typename Func>
inline void runtime_view::for_each_hit_impl(Func&& func) noexcept
{
    if (!all_sets_valid()) [[unlikely]] return;

    // 纯OR查询(无AND项):遍历 or_sets 并集去重
    if (query_.primary_set_ == nullptr)
    {
        if (!query_.has_or_ || query_.or_sets_.empty()) [[unlikely]] return;

        size_t max_sparse = 0;
        for (size_t k = 0; k < query_.or_sets_.size(); ++k)
        {
            if (query_.or_sets_[k])
            {
                size_t sz = query_.or_sets_[k]->get_sparse_size();
                if (sz > max_sparse) max_sparse = sz;
            }
        }
        if (max_sparse == 0) return;

        class_pool<bool> visited;
        visited.resize(max_sparse, false);

        const sparse_entry* rv_cur_ver_page = nullptr;
        size_t rv_cur_page_idx = SIZE_MAX;
        for (size_t k = 0; k < query_.or_sets_.size(); ++k)
        {
            auto* set = query_.or_sets_[k];
            if (!set) continue;
            auto& indices = set->get_entity_indices();
            const size_t n = indices.size();
            rv_cur_ver_page = nullptr;
            rv_cur_page_idx = SIZE_MAX;
            for (size_t i = 0; i < n; ++i)
            {
                uint32_t idx = indices[i];
                if (idx >= max_sparse) [[unlikely]] continue;
                if (visited[idx]) continue;
                size_t pid = idx >> set->page_shift;
                if (pid != rv_cur_page_idx) [[unlikely]]
                {
                    rv_cur_ver_page = set->get_version_page(idx);
                    rv_cur_page_idx = pid;
                }
                uint32_t entry = 0;
                if (rv_cur_ver_page) [[likely]]
                    entry = single_class_set::read_version_from_page(rv_cur_ver_page, idx, set->page_mask);
                uint32_t ver = entry;
                // NOT 检查
                bool excluded = false;
                for (size_t j = 0; j < query_.exc_sets_.size(); ++j)
                {
                    if (single_class_set::sparse_contains_version(
                            query_.exc_sets_[j], idx, ver))
                    {
                        excluded = true;
                        break;
                    }
                }
                if (excluded) continue;
                visited[idx] = true;
                entity e(idx, ver);
                func(e, i);
            }
        }
        return;
    }

    auto* primary = query_.primary_set_;
    auto& indices = primary->get_entity_indices();
    const size_t n = indices.size();

    if (query_.use_mask_path_ && !query_.has_or_)
    {
        const sparse_entry* rv_cur_ver_page = nullptr;
        size_t rv_cur_page_idx = SIZE_MAX;
        for (size_t i = 0; i < n; ++i)
        {
            uint32_t idx = indices[i];
            if (!query_.check_blocks(idx, mgr_)) continue;
            size_t pid = idx >> primary->page_shift;
            if (pid != rv_cur_page_idx) [[unlikely]]
            {
                rv_cur_ver_page = primary->get_version_page(idx);
                rv_cur_page_idx = pid;
            }
            uint32_t entry = 0;
            if (rv_cur_ver_page) [[likely]]
                entry = single_class_set::read_version_from_page(rv_cur_ver_page, idx, primary->page_mask);
            entity e(idx, entry);
            func(e, i);
        }
        return;
    }

    for (size_t i = 0; i < n; ++i)
    {
        uint32_t idx = indices[i];
        uint32_t ver = primary->get_version_unchecked(idx);
        if (!is_entity_hit(idx, ver)) continue;
        entity e(idx, ver);
        func(e, i);
    }
}

template <typename Func>
inline void runtime_view::for_each_hit_range(size_t start, size_t end, Func&& func) noexcept
{
    if (query_.primary_set_ == nullptr || !all_sets_valid()) [[unlikely]] return;

    auto* primary = query_.primary_set_;
    auto& indices = primary->get_entity_indices();
    const size_t n = indices.size();
    if (start >= n) return;
    if (end > n) end = n;

    if (query_.use_mask_path_ && !query_.has_or_)
    {
        const sparse_entry* rv_cur_ver_page = nullptr;
        size_t rv_cur_page_idx = SIZE_MAX;
        for (size_t i = start; i < end; ++i)
        {
            uint32_t idx = indices[i];
            if (!query_.check_blocks(idx, mgr_)) continue;
            size_t pid = idx >> primary->page_shift;
            if (pid != rv_cur_page_idx) [[unlikely]]
            {
                rv_cur_ver_page = primary->get_version_page(idx);
                rv_cur_page_idx = pid;
            }
            uint32_t entry = 0;
            if (rv_cur_ver_page) [[likely]]
                entry = single_class_set::read_version_from_page(rv_cur_ver_page, idx, primary->page_mask);
            entity e(idx, entry);
            func(e);
        }
        return;
    }

    for (size_t i = start; i < end; ++i)
    {
        uint32_t idx = indices[i];
        uint32_t ver = primary->get_version_unchecked(idx);
        if (!is_entity_hit(idx, ver)) continue;
        entity e(idx, ver);
        func(e);
    }
}

// ===== 现有接口 =====

template <typename Func>
inline void runtime_view::for_each(Func&& func) noexcept
{
    ensure_fresh();
    for_each_hit_impl([&](entity e, size_t) {
        if constexpr (std::is_invocable_v<Func, entity>)
        {
            func(e);
        }
        else
        {
            func();
        }
    });
}

// ===== 1. 组件引用回传 =====

template <typename... Ts, typename Func>
inline void runtime_view::for_each_typed(Func&& func) noexcept
{
    ensure_fresh();
    if (query_.primary_set_ == nullptr) [[unlikely]] return;

    auto sets_tuple = std::make_tuple(mgr_->get_single_class_set<Ts>()...);

    for_each_hit_impl([&](entity e, size_t) {
        auto get_and_call = [&]<size_t... Is>(std::index_sequence<Is...>) {
            auto ptrs = std::make_tuple(
                detail::get_component_ptr_from_set<Ts>(
                    std::get<Is>(sets_tuple),
                    e.parts_.index_, e.parts_.version_)...
            );
            bool all_present = (std::get<Is>(ptrs) && ...);
            if (!all_present) return;
            if constexpr (std::is_invocable_v<Func, entity, Ts&...>)
            {
                func(e, *std::get<Is>(ptrs)...);
            }
            else
            {
                func(*std::get<Is>(ptrs)...);
            }
        };
        get_and_call(std::index_sequence_for<Ts...>{});
    });
}

// ===== 2. 并行迭代 =====

template <typename Func>
inline void runtime_view::for_each_parallel(size_t worker_id, size_t worker_count,
                                             Func&& func) noexcept
{
    ensure_fresh();
    if (worker_count == 0 || query_.primary_set_ == nullptr) return;

    size_t n = query_.primary_set_->get_entity_indices().size();
    size_t per_worker = (n + worker_count - 1) / worker_count;
    size_t start = worker_id * per_worker;
    size_t end = (start + per_worker < n) ? (start + per_worker) : n;

    for_each_hit_range(start, end, [&](entity e) {
        if constexpr (std::is_invocable_v<Func, entity, size_t>)
        {
            func(e, worker_id);
        }
        else if constexpr (std::is_invocable_v<Func, entity>)
        {
            func(e);
        }
        else
        {
            func();
        }
    });
}

// ===== 3. 分页遍历 =====

template <typename Func>
inline void runtime_view::for_each_paged(size_t offset, size_t limit, Func&& func) noexcept
{
    ensure_fresh();
    if (query_.primary_set_ == nullptr) return;

    size_t n = query_.primary_set_->get_entity_indices().size();
    if (offset >= n) return;
    size_t end = (offset + limit < n) ? (offset + limit) : n;

    for_each_hit_range(offset, end, [&](entity e) {
        if constexpr (std::is_invocable_v<Func, entity>)
        {
            func(e);
        }
        else
        {
            func();
        }
    });
}

// ===== 4. 变更检测 =====

inline bool runtime_view::changed() noexcept
{
    ensure_fresh();
    if (query_.primary_set_ == nullptr) return false;
    if (!tracking_changes_) return true;

    for (size_t k = 0; k < query_.req_sets_.size(); ++k)
    {
        auto* set = query_.req_sets_[k];
        if (set && k < baseline_versions_.size())
        {
            if (set->get_pool_version() != baseline_versions_[k])
            {
                return true;
            }
        }
    }
    return false;
}

inline void runtime_view::reset_change_tracking() noexcept
{
    tracking_changes_ = true;
    baseline_versions_.clear();
    for (size_t k = 0; k < query_.req_sets_.size(); ++k)
    {
        auto* set = query_.req_sets_[k];
        baseline_versions_.emplace_back(set ? set->get_pool_version() : 0);
    }
}

template <typename Func>
inline void runtime_view::for_each_changed(Func&& func) noexcept
{
    if (!changed()) return;
    for_each(std::forward<Func>(func));
    reset_change_tracking();
}

// ===== 5. 排序 =====

template <typename T, typename Compare>
inline void runtime_view::sort_by_component(Compare&& cmp) noexcept
{
    ensure_fresh();
    sorted_entities_.clear();
    sorted_valid_ = false;

    if (query_.primary_set_ == nullptr) return;

    auto* sort_set = mgr_->get_single_class_set<T>();
    if (!sort_set) return;

    const size_t sort_sparse_sz = sort_set->get_sparse_size();
    auto* sort_pool = sort_set->template get_typed_pool_ptr<T>();

    class_pool<T> components;
    const sparse_entry* rv_cur_dense_page = nullptr;
    const sparse_entry* rv_cur_ver_page = nullptr;
    size_t rv_cur_page_idx = SIZE_MAX;
    for_each_hit_impl([&](entity e, size_t) {
        uint32_t idx = e.parts_.index_;
        if (idx >= sort_sparse_sz) return;
        size_t pid = idx >> sort_set->page_shift;
        if (pid != rv_cur_page_idx) [[unlikely]]
        {
            rv_cur_dense_page = sort_set->get_dense_page(idx);
            rv_cur_ver_page = sort_set->get_version_page(idx);
            rv_cur_page_idx = pid;
        }
        uint32_t dense = 0xFFFFFFFFu;
        uint32_t ver = 0u;
        if (rv_cur_dense_page) [[likely]]
            dense = single_class_set::read_dense_from_page(rv_cur_dense_page, idx, sort_set->page_mask);
        if (rv_cur_ver_page) [[likely]]
            ver = single_class_set::read_version_from_page(rv_cur_ver_page, idx, sort_set->page_mask);
        if (dense == 0xFFFFFFFFu || ver != e.parts_.version_) return;
        if (!sort_pool || dense >= sort_pool->size()) return;
        sorted_entities_.emplace_back(e);
        components.emplace_back((*sort_pool)[dense]);
    });

    const size_t n = sorted_entities_.size();
    if (n <= 1)
    {
        sorted_valid_ = true;
        return;
    }

    class_pool<size_t> indices;
    indices.increase_capacity(n);
    for (size_t i = 0; i < n; ++i)
    {
        indices.emplace_back(i);
    }

    // MinGW+AVX2 下 std::sort+lambda 会崩溃, 使用 pdqsort 替代
    pdqsort<size_t>(indices.data(), n,
        [&components, &cmp](size_t a, size_t b) {
            return cmp(components[a], components[b]);
        });

    class_pool<entity> temp;
    temp.increase_capacity(n);
    for (size_t i = 0; i < n; ++i)
    {
        temp.emplace_back(sorted_entities_[indices[i]]);
    }
    sorted_entities_ = std::move(temp);
    sorted_valid_ = true;
}

// ===== 6. 精确命中数 =====

inline size_t runtime_view::count() noexcept
{
    ensure_fresh();

    size_t cnt = 0;
    for_each_hit_impl([&](entity, size_t) {
        ++cnt;
    });
    return cnt;
}

// ===== 7. 迭代器 =====

inline void runtime_view::iterator::advance_to_valid() noexcept
{
    if (!mgr_) return;

    auto* primary = query_.primary_set_;
    if (!primary) return;

    auto& indices = primary->get_entity_indices();
    const size_t n = indices.size();

    while (index_ < n)
    {
        uint32_t idx = indices[index_];
        uint32_t ver = primary->get_version_unchecked(idx);
        // 内联 is_entity_hit 检查
        bool hit = true;
        for (size_t k = 0; k < query_.req_sets_.size(); ++k)
        {
            if (!single_class_set::sparse_contains_version(query_.req_sets_[k], idx, ver))
            {
                hit = false;
                break;
            }
        }
        if (hit)
        {
            for (size_t k = 0; k < query_.exc_sets_.size(); ++k)
            {
                if (single_class_set::sparse_contains_version(query_.exc_sets_[k], idx, ver))
                {
                    hit = false;
                    break;
                }
            }
        }
        if (hit && query_.has_or_)
        {
            bool or_hit = false;
            for (size_t k = 0; k < query_.or_sets_.size(); ++k)
            {
                if (single_class_set::sparse_contains_version(query_.or_sets_[k], idx, ver))
                {
                    or_hit = true;
                    break;
                }
            }
            if (!or_hit) hit = false;
        }
        if (hit)
        {
            current_ = entity(idx, ver);
            return;
        }
        ++index_;
    }
    current_ = entity{};
}

inline runtime_view::iterator runtime_view::begin() noexcept
{
    ensure_fresh();
    return iterator(query_, mgr_, 0);
}

inline runtime_view::iterator runtime_view::end() noexcept
{
    ensure_fresh();
    size_t n = query_.primary_set_ ? query_.primary_set_->get_entity_indices().size() : 0;
    return iterator(runtime_query{}, nullptr, n);
}

// ===== size / empty =====

inline size_t runtime_view::size() noexcept
{
    ensure_fresh();
    return query_.primary_set_ ? query_.primary_set_->size() : 0;
}

inline bool runtime_view::empty() noexcept
{
    ensure_fresh();
    return query_.primary_set_ == nullptr || query_.primary_set_->empty();
}

// ======================== filter_view 的 and_ / or_ 方法 ========================
template <typename T, typename Pred>
template <typename B>
auto manager::filter_view<T, Pred>::and_() noexcept
{
    return filter_and_view<T, B, Pred>(mgr_, std::move(pred_));
}

template <typename T, typename Pred>
template <typename B>
auto manager::filter_view<T, Pred>::or_() noexcept
{
    return filter_or_view<T, B, Pred>(mgr_, std::move(pred_));
}

[[nodiscard]] inline bool runtime_query::check_blocks(uint32_t entity_index, const manager* mgr) const noexcept
{
    for (uint32_t b = 0; b <= max_block_; ++b)
    {
        if (req_masks_[b] != 0)
        {
            uint64_t mask = mgr->get_entity_block_by_idx(entity_index, b);
            if ((mask & req_masks_[b]) != req_masks_[b])
                return false;
        }
        if (exc_masks_[b] != 0)
        {
            uint64_t mask = mgr->get_entity_block_by_idx(entity_index, b);
            if ((mask & exc_masks_[b]) != 0)
                return false;
        }
    }
    return true;
}

} // namespace ecs
