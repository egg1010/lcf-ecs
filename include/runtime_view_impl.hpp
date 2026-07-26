// runtime_view_impl.hpp —— runtime_query / runtime_view / filter_view 的 out-of-line 定义
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

inline runtime_query::runtime_query(manager* mgr, std::span<const int> required_ids,
                                     std::span<const int> excluded_ids) noexcept
{
    required_ids_.increase_capacity(required_ids.size());
    for (int tid : required_ids)
    {
        required_ids_.emplace_back(tid);
    }
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

inline runtime_query::runtime_query(manager* mgr, std::span<const runtime_term> terms) noexcept
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
        for (size_t i = 0; i < indices.size(); ++i)
        {
            uint32_t idx = indices[i];
            if (!query_.check_blocks(idx, mgr_)) continue;
            uint32_t entry = primary->sparse_version_at_public(idx);
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
    cached_hits_valid_ = false;
}

// 构建命中实体缓存 (rebuild 时构建, for_each/count 复用)
inline void runtime_view::build_cached_hits() noexcept
{
    cached_hits_.clear();
    if (!all_sets_valid() || query_.primary_set_ == nullptr) [[unlikely]]
    {
        cached_hits_valid_ = true;
        return;
    }

    auto* primary = query_.primary_set_;
    auto& indices = primary->get_entity_indices();
    const size_t n = indices.size();
    cached_hits_.reserve_exact(n);

    if (query_.use_mask_path_ && !query_.has_or_)
    {
        for (size_t i = 0; i < n; ++i)
        {
            uint32_t idx = indices[i];
            if (query_.check_blocks(idx, mgr_))
            {
                uint32_t ver = primary->sparse_version_at_public(idx);
                cached_hits_.emplace_back(idx, ver);
            }
        }
    }
    else
    {
        for (size_t i = 0; i < n; ++i)
        {
            uint32_t idx = indices[i];
            uint32_t ver = primary->get_version_unchecked(idx);
            if (is_entity_hit(idx, ver))
            {
                cached_hits_.emplace_back(idx, ver);
            }
        }
    }
    cached_hits_valid_ = true;
}

inline void runtime_view::ensure_hits_fresh() noexcept
{
    ensure_fresh();
    if (!cached_hits_valid_) build_cached_hits();
}

template <typename T>
inline T* runtime_view::get_ptr(entity e) noexcept
{
    ensure_fresh();
    return mgr_->template get_ptr_fast<T>(e);
}

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

        dense<bool> visited;
        visited.resize(max_sparse, false);

        for (size_t k = 0; k < query_.or_sets_.size(); ++k)
        {
            auto* set = query_.or_sets_[k];
            if (!set) continue;
            auto& indices = set->get_entity_indices();
            const size_t n = indices.size();
            for (size_t i = 0; i < n; ++i)
            {
                uint32_t idx = indices[i];
                if (idx >= max_sparse) [[unlikely]] continue;
                if (visited[idx]) continue;
                uint32_t ver = set->sparse_version_at_public(idx);
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
        // 4x 循环展开, 最大化 ILP
        // check_blocks 是纯位运算, 4 次独立调用可并行发射
        // sparse_version_at_public 是随机访问, 4 次并行发射隐藏 load latency
        const size_t n4 = n & ~size_t{3};
        size_t i = 0;
        for (; i < n4; i += 4)
        {
            uint32_t idx0 = indices[i];
            uint32_t idx1 = indices[i + 1];
            uint32_t idx2 = indices[i + 2];
            uint32_t idx3 = indices[i + 3];
            bool hit0 = query_.check_blocks(idx0, mgr_);
            bool hit1 = query_.check_blocks(idx1, mgr_);
            bool hit2 = query_.check_blocks(idx2, mgr_);
            bool hit3 = query_.check_blocks(idx3, mgr_);
            if (hit0)
            {
                entity e(idx0, primary->sparse_version_at_public(idx0));
                func(e, i);
            }
            if (hit1)
            {
                entity e(idx1, primary->sparse_version_at_public(idx1));
                func(e, i + 1);
            }
            if (hit2)
            {
                entity e(idx2, primary->sparse_version_at_public(idx2));
                func(e, i + 2);
            }
            if (hit3)
            {
                entity e(idx3, primary->sparse_version_at_public(idx3));
                func(e, i + 3);
            }
        }
        for (; i < n; ++i)
        {
            uint32_t idx = indices[i];
            if (!query_.check_blocks(idx, mgr_)) continue;
            uint32_t entry = primary->sparse_version_at_public(idx);
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
        for (size_t i = start; i < end; ++i)
        {
            uint32_t idx = indices[i];
            if (!query_.check_blocks(idx, mgr_)) continue;
            uint32_t entry = primary->sparse_version_at_public(idx);
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

template <typename... Ts, typename Func>
inline void runtime_view::for_each_typed(Func&& func) noexcept
{
    ensure_hits_fresh();
    if (query_.primary_set_ == nullptr || cached_hits_.empty()) [[unlikely]] return;

    for_each_typed_impl<Ts...>(std::forward<Func>(func), std::index_sequence_for<Ts...>{});
}

template <typename... Ts, typename Func, size_t... Is>
inline void runtime_view::for_each_typed_impl(Func&& func, std::index_sequence<Is...>) noexcept
{
    auto sets_tuple = std::make_tuple(mgr_->get_single_class_set<Ts>()...);

    // 用 cached_hits_ 遍历命中实体, 避免重复 check_blocks/is_entity_hit
    const size_t n = cached_hits_.size();
    auto* hits = cached_hits_.data();

    const size_t n4 = n & ~size_t{3};
    size_t i = 0;
    for (; i < n4; i += 4)
    {
        entity e0 = hits[i];
        entity e1 = hits[i + 1];
        entity e2 = hits[i + 2];
        entity e3 = hits[i + 3];

        auto get_ptrs = [&](entity e) {
            return std::make_tuple(
                detail::get_component_ptr_from_set<Ts>(
                    std::get<Is>(sets_tuple),
                    e.parts_.index_, e.parts_.version_)...
            );
        };
        auto ptrs0 = get_ptrs(e0);
        auto ptrs1 = get_ptrs(e1);
        auto ptrs2 = get_ptrs(e2);
        auto ptrs3 = get_ptrs(e3);

        auto call_one = [&](entity e, auto& ptrs) {
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
        call_one(e0, ptrs0);
        call_one(e1, ptrs1);
        call_one(e2, ptrs2);
        call_one(e3, ptrs3);
    }
    for (; i < n; ++i)
    {
        entity e = hits[i];
        auto ptrs = std::make_tuple(
            detail::get_component_ptr_from_set<Ts>(
                std::get<Is>(sets_tuple),
                e.parts_.index_, e.parts_.version_)...
        );
        bool all_present = (std::get<Is>(ptrs) && ...);
        if (!all_present) continue;
        if constexpr (std::is_invocable_v<Func, entity, Ts&...>)
        {
            func(e, *std::get<Is>(ptrs)...);
        }
        else
        {
            func(*std::get<Is>(ptrs)...);
        }
    }
}

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

    dense<T> components;
    for_each_hit_impl([&](entity e, size_t) {
        uint32_t idx = e.parts_.index_;
        if (idx >= sort_sparse_sz) return;
        uint32_t dense = sort_set->sparse_dense_at_public(idx);
        uint32_t ver = sort_set->sparse_version_at_public(idx);
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

    dense<size_t> indices;
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

    dense<entity> temp;
    temp.increase_capacity(n);
    for (size_t i = 0; i < n; ++i)
    {
        temp.emplace_back(sorted_entities_[indices[i]]);
    }
    sorted_entities_ = std::move(temp);
    sorted_valid_ = true;
}

inline size_t runtime_view::count() noexcept
{
    ensure_hits_fresh();
    return cached_hits_.size();
}

inline void runtime_view::iterator::advance_to_valid() noexcept
{
    if (!mgr_ || !query_) return;

    auto* primary = query_->primary_set_;
    if (!primary) return;

    auto& indices = primary->get_entity_indices();
    const size_t n = indices.size();

    while (index_ < n)
    {
        uint32_t idx = indices[index_];
        uint32_t ver = primary->get_version_unchecked(idx);
        bool hit = true;
        for (size_t k = 0; k < query_->req_sets_.size(); ++k)
        {
            if (!single_class_set::sparse_contains_version(query_->req_sets_[k], idx, ver))
            {
                hit = false;
                break;
            }
        }
        if (hit)
        {
            for (size_t k = 0; k < query_->exc_sets_.size(); ++k)
            {
                if (single_class_set::sparse_contains_version(query_->exc_sets_[k], idx, ver))
                {
                    hit = false;
                    break;
                }
            }
        }
        if (hit && query_->has_or_)
        {
            bool or_hit = false;
            for (size_t k = 0; k < query_->or_sets_.size(); ++k)
            {
                if (single_class_set::sparse_contains_version(query_->or_sets_[k], idx, ver))
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
    // end 迭代器: query_ 为 nullptr, advance_to_valid 直接返回
    return iterator(query_, nullptr, n);
}

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
