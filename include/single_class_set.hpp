#pragma once
#include <span>
#include <new>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#if defined(__AVX2__) || (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
#include <immintrin.h>
#endif
#include "part/operating_message.hpp"
#include "entity.hpp"
#include "part/dense.hpp"
#include "part/class_pool.hpp"
#include "part/type_id.hpp"
#include "part/tiered_sort.hpp"
// PREFETCH_R 宏: 集中定义于 part/force_inline.hpp


static inline void nt_fill_uint32_(uint32_t* dst, size_t count, uint32_t value) noexcept
{
#if defined(__AVX2__)
    if (count >= 8)
    {
        const __m256i fill = _mm256_set1_epi32(static_cast<int>(value));
        const size_t ymm_count = count / 8;
        for (size_t i = 0; i < ymm_count; ++i)
        {
            _mm256_stream_si256(reinterpret_cast<__m256i*>(dst) + i, fill);
        }
        _mm_sfence();
        const size_t tail_start = ymm_count * 8;
        for (size_t i = tail_start; i < count; ++i)
        {
            dst[i] = value;
        }
        return;
    }
#endif
    for (size_t i = 0; i < count; ++i)
    {
        dst[i] = value;
    }
}

namespace ecs
{
class manager;
template <typename> class query_context;

// 合并存储: dense 索引 + version 同一 cache line, 减少 get_ptr 慢路径 cache miss
struct sparse_entry
{
    uint32_t dense;
    uint32_t version;
};

class single_class_set
{
public:
    static constexpr uint32_t dense_invalid = 0xFFFFFFFFu;

private:
    static constexpr size_t hot_set_capacity_ = 256;

    // sparse table: class_pool<sparse_entry> 替代 flat+paged 混合存储
    class_pool<sparse_entry> sparse_table_;
    size_t sparse_size_{0};

    // hot set cache storage — 16B 紧凑布局 (原 32B alignas(32) 浪费 12B/entry)
    //   entity_key = entity_index(低32) | version(高32), 与 entity.parts_ 内存布局一致
    //   pool_version_lo: version_ 低 32 位 (bump_pool_version 达 2^32 才 wrap, 实际不可达)
    //   256 * 16B = 4KB (原 8KB), 每 cache line(64B) 放 4 entry (原 2), conflict miss 减半
    struct hot_entry_
    {
        uint64_t entity_key;       // entity_index | (version << 32)
        uint32_t dense_index;
        uint32_t pool_version_lo;  // (uint32_t)version_
    };
    hot_entry_ hot_set_[hot_set_capacity_]{};

    dense<uint32_t> dense_;
    // 与 dense_ 同步的 version 数组, 用于遍历时直接读连续内存, 避免 sparse_entry 间接查找
    dense<uint32_t> versions_;
    int type_id_{-1};

    void* typed_pool_{nullptr};
    void* typed_pool_data_{nullptr};
    size_t pending_increase_capacity_{0};
    size_t component_size_{0};

    struct pool_ops {
        void (*destroy_pool)(void* pool) noexcept;
        void (*swap_pop)(void* pool, size_t index) noexcept;
        void (*clear_pool)(void* pool) noexcept;
        void (*increase_capacity_pool)(void* pool, size_t cap) noexcept;
        void (*swap_pool)(void* pool, size_t i, size_t j) noexcept;
        void* (*get_pool_data)(void* pool) noexcept;
        bool is_trivially_copyable{false};
        // trivial 类型快速路径: 基于 memcpy 的 swap_pop 和 swap
        void (*swap_pop_trivial)(void* pool, size_t index, size_t component_size) noexcept;
        void (*swap_pool_trivial)(void* pool, size_t i, size_t j, size_t component_size) noexcept;
        void* (*get_pool_element)(void* pool, size_t index) noexcept;
        size_t (*get_pool_size)(void* pool) noexcept;
        void (*pool_pop_back)(void* pool) noexcept;
    } ops_{};

    uint64_t version_{0};
    struct change_tracking_entry
    {
        uint64_t change_version;
        uint64_t added_version;
    };
    dense<change_tracking_entry> entity_change_tracking_;
    uint64_t global_change_counter_{0};
    uint64_t global_added_counter_{0};
    bool track_changes_enabled_{true};

    void (*on_add_)(entity, void* component, void* user_data) noexcept = nullptr;
    void* on_add_data_{nullptr};
    void (*on_remove_)(entity, void* component, void* user_data) noexcept = nullptr;
    void* on_remove_data_{nullptr};
    void (*on_modify_)(entity, void* component, void* user_data) noexcept = nullptr;
    void* on_modify_data_{nullptr};

    friend class ecs::manager;
    template <typename> friend class ecs::query_context;

    // slow path 优化: 单次加载 sparse_entry (8B = dense + version),
    //   替代原 sparse_dense_at + sparse_version_at 两次独立调用
    //   (原方案每次调用都重复 idx>=sparse_size_ 和 is_constructed_at 检查)

    [[nodiscard]] uint32_t sparse_dense_at(uint32_t idx) const noexcept
    {
        if (idx >= sparse_size_) [[unlikely]]
            return dense_invalid;
        if (!sparse_table_.is_constructed_at(idx)) [[unlikely]]
            return dense_invalid;
        return sparse_table_[idx].dense;
    }

    [[nodiscard]] uint32_t sparse_version_at(uint32_t idx) const noexcept
    {
        if (idx >= sparse_size_) [[unlikely]]
            return 0;
        if (!sparse_table_.is_constructed_at(idx)) [[unlikely]]
            return 0;
        return sparse_table_[idx].version;
    }

    [[nodiscard]] uint32_t sparse_dense_at_unchecked(uint32_t idx) const noexcept
    {
        return sparse_table_[idx].dense;
    }

    [[nodiscard]] uint32_t sparse_version_at_unchecked(uint32_t idx) const noexcept
    {
        return sparse_table_[idx].version;
    }

    // 单次加载 sparse_entry (8B), 返回 dense+version, 供 slow path 合并比较
    //   先检查 is_constructed_at (bitmap), 未构造返回 nullptr, 避免读到垃圾数据
    //   比 sparse_dense_at + sparse_version_at 少 1 次 is_constructed_at + 1 次 sparse_entry 加载
    [[nodiscard]] const sparse_entry* sparse_entry_checked_(uint32_t idx) const noexcept
    {
        if (!sparse_table_.is_constructed_at(idx)) [[unlikely]]
            return nullptr;
        return &sparse_table_[idx];
    }

    // unchecked: 调用方保证 idx 已构造 (如 hot_set hit 后的 fast path)
    [[nodiscard]] const sparse_entry& sparse_entry_at_unchecked(uint32_t idx) const noexcept
    {
        return sparse_table_[idx];
    }

    void sparse_set_at(uint32_t idx, uint32_t dense, uint32_t version) noexcept
    {
        sparse_table_.sparse_emplace_at(idx, sparse_entry{dense, version});
        if (idx >= sparse_size_)
            sparse_size_ = static_cast<size_t>(idx) + 1;
    }

    void sparse_set_at_unchecked(uint32_t idx, uint32_t dense, uint32_t version) noexcept
    {
        sparse_table_.sparse_emplace_at(idx, sparse_entry{dense, version});
        if (idx >= sparse_size_)
            sparse_size_ = static_cast<size_t>(idx) + 1;
    }

    void deallocate_all_pages_() noexcept
    {
        sparse_table_.clear();
        sparse_size_ = 0;
    }

    void check_mode_switch_() noexcept {}

    // 16B 紧凑布局: entity_key (64b) + dense_index (32b) + pool_version_lo (32b)
    //   hit 判断: entity_key 匹配 + pool_version_lo 匹配 (2 次独立比较, 可并行发射)

    // 从 entity 构造 key (与 entity.parts_ 内存布局一致: index 低 32 | version 高 32)
    static uint64_t make_entity_key_(entity e) noexcept
    {
        // 注: entity.parts_ 是 {index_, version_}, 直接 memcpy 避免 UB (违反严格别名)
        uint64_t key;
        std::memcpy(&key, &e.parts_, sizeof(key));
        return key;
    }

    void hot_set_update_(uint32_t entity_index, uint32_t dense_index, uint32_t version) noexcept
    {
        const size_t slot = entity_index & (hot_set_capacity_ - 1);
        hot_set_[slot] = {make_entity_key_(entity{entity_index, version}), dense_index, static_cast<uint32_t>(version_)};
    }

    void hot_set_invalidate_(uint32_t entity_index) noexcept
    {
        const size_t slot = entity_index & (hot_set_capacity_ - 1);
        // 仅当 entity_index 匹配时清空 (检查 entity_key 低 32 位)
        if (static_cast<uint32_t>(hot_set_[slot].entity_key) == entity_index)
        {
            hot_set_[slot] = {0, 0, 0};
        }
    }

    void hot_set_clear_() noexcept
    {
        std::memset(hot_set_, 0, sizeof(hot_set_));
    }

    void sparse_set(uint32_t idx, uint32_t version, uint32_t dense) noexcept
    {
        sparse_set_at(idx, dense, version);
    }

    // unchecked: 调用方保证 page 已存在, 不更新 hot set
    void sparse_set_unchecked(uint32_t idx, uint32_t version, uint32_t dense) noexcept
    {
        sparse_set_at_unchecked(idx, dense, version);
    }

    template <typename T>
    void init_typed_storage()
    {
        if (typed_pool_) [[unlikely]] return;
        auto* pool = new (std::nothrow) dense<T>();
        if (!pool) [[unlikely]] std::abort();
        component_size_ = sizeof(T);
        if (pending_increase_capacity_ > 0)
        {
            pool->increase_capacity(pending_increase_capacity_);
            dense_.increase_capacity(pending_increase_capacity_);
            versions_.increase_capacity(pending_increase_capacity_);
            entity_change_tracking_.increase_capacity(pending_increase_capacity_);
            pending_increase_capacity_ = 0;
        }
        typed_pool_ = pool;
        typed_pool_data_ = pool->data();
        ops_ = {
            /*.destroy_pool =*/[](void* p) noexcept { delete static_cast<dense<T>*>(p); },
            /*.swap_pop =*/[](void* p, size_t index) noexcept {
                auto* pool = static_cast<dense<T>*>(p);
                const size_t last = pool->size() - 1;
                if (index != last) [[likely]]
                {
                    if constexpr (std::is_trivially_copyable_v<T>)
                    {
                        std::memcpy(&(*pool)[index], &(*pool)[last], sizeof(T));
                    }
                    else
                    {
                        (*pool)[index].~T();
                        new (&(*pool)[index]) T(std::move((*pool)[last]));
                    }
                }
                pool->pop_back();
            },
            /*.clear_pool =*/[](void* p) noexcept { static_cast<dense<T>*>(p)->clear(); },
            /*.increase_capacity_pool =*/[](void* p, size_t cap) noexcept { static_cast<dense<T>*>(p)->increase_capacity(cap); },
            /*.swap_pool =*/[](void* p, size_t i, size_t j) noexcept {
                auto* pool = static_cast<dense<T>*>(p);
                std::swap((*pool)[i], (*pool)[j]);
            },
            /*.get_pool_data =*/[](void* p) noexcept -> void* { return static_cast<dense<T>*>(p)->data(); },
            /*.is_trivially_copyable =*/std::is_trivially_copyable_v<T>,
            /*.swap_pop_trivial =*/[](void* p, size_t index, size_t comp_size) noexcept {
                auto* pool = static_cast<dense<T>*>(p);
                const size_t last = pool->size() - 1;
                if (index != last) [[likely]]
                {
                    auto* data = reinterpret_cast<char*>(pool->data());
                    std::memcpy(data + index * comp_size, data + last * comp_size, comp_size);
                }
                pool->pop_back();
            },
            /*.swap_pool_trivial =*/[](void* p, size_t i, size_t j, size_t comp_size) noexcept {
                auto* pool = static_cast<dense<T>*>(p);
                alignas(alignof(std::max_align_t)) char temp[256];
                auto* data = reinterpret_cast<char*>(pool->data());
                std::memcpy(temp, data + i * comp_size, comp_size);
                std::memcpy(data + i * comp_size, data + j * comp_size, comp_size);
                std::memcpy(data + j * comp_size, temp, comp_size);
            },
            /*.get_pool_element =*/[](void* p, size_t index) noexcept -> void* {
                return &(*static_cast<dense<T>*>(p))[index];
            },
            /*.get_pool_size =*/[](void* p) noexcept -> size_t {
                return static_cast<dense<T>*>(p)->size();
            },
            /*.pool_pop_back =*/[](void* p) noexcept {
                static_cast<dense<T>*>(p)->pop_back();
            },
        };
    }

    template <typename T>
    [[nodiscard]] dense<T>* get_typed_pool() noexcept
    {
        assert(typed_pool_ != nullptr && type_id_ == type_id::get_type_id<T>()
            && "get_typed_pool<T>(): type mismatch or pool not initialized");
        return static_cast<dense<T>*>(typed_pool_);
    }

    template <typename T>
    [[nodiscard]] const dense<T>* get_typed_pool() const noexcept
    {
        assert(typed_pool_ != nullptr && type_id_ == type_id::get_type_id<T>()
            && "get_typed_pool<T>(): type mismatch or pool not initialized");
        return static_cast<const dense<T>*>(typed_pool_);
    }

    template <typename T>
    [[nodiscard]] T* fast_get_ptr_by_index(size_t index) noexcept
    {
        if (index >= get_typed_pool<T>()->size()) [[unlikely]] return nullptr;
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T>
    [[nodiscard]] const T* fast_get_ptr_by_index(size_t index) const noexcept
    {
        if (index >= get_typed_pool<T>()->size()) [[unlikely]] return nullptr;
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_unchecked_by_index(size_t index) noexcept
    {
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_unchecked_by_index(size_t index) const noexcept
    {
        return &(*get_typed_pool<T>())[index];
    }

    template <typename T, typename Entities, typename F>
    operating_message add_batch_impl(Entities& entities, size_t count, F&& get_component) noexcept
    {
        operating_message result;
        using DT = std::decay_t<T>;
        const int tid = type_id::get_type_id<DT>();
        if (type_id_ == -1) [[unlikely]]
        {
            type_id_ = tid;
            init_typed_storage<DT>();
        }

        size_t max_index = 0;
        bool all_new = true;
        for (size_t i = 0; i < count; ++i)
        {
            const entity& e = entities[i];
            if (!e.is_valid()) [[unlikely]]
            {
                result.write_message(false, "single_class_set::add_batch(): invalid entity index ", std::to_string(e.parts_.index_));
                return result;
            }
            if (e.parts_.index_ > max_index) max_index = e.parts_.index_;
            if (all_new && e.parts_.index_ < sparse_size_ && sparse_version_at(e.parts_.index_) == e.parts_.version_)
                all_new = false;
        }

        sparse_table_.increase_capacity(max_index + 1);
        if (max_index >= sparse_size_)
            sparse_size_ = max_index + 1;

        auto* pool = get_typed_pool<DT>();

        if (all_new) [[likely]]
        {
            size_t dense_start = dense_.size();
            dense_.increase_capacity(dense_start + count);
            versions_.increase_capacity(dense_start + count);
            pool->increase_capacity(pool->size() + count);
            typed_pool_data_ = pool->data();

            for (size_t i = 0; i < count; ++i)
            {
                dense_.push_back_unchecked(entities[i].parts_.index_);
            }
            for (size_t i = 0; i < count; ++i)
                versions_.push_back_unchecked(entities[i].parts_.version_);

            // 顺序追加检测: 若 entities 恰好为 [append_pos, append_pos+1, ..., append_pos+count-1]
            //   则用 emplace_back_dense_unchecked 替代 sparse_set_unchecked
            //   优势: 跳过 invalidate_count_cache + bitmap_test + update_dense_status
            //   (append 场景下 is_dense_/hole_count_ 不变, 这些操作均为 no-op 但仍有指令开销)
            const size_t append_pos = sparse_table_.size();
            bool sequential = (count > 0 && entities[0].parts_.index_ == append_pos);
            if (sequential) [[likely]]
            {
                for (size_t i = 1; i < count; ++i)
                {
                    if (entities[i].parts_.index_ != append_pos + i) { sequential = false; break; }
                }
            }

            using component_return_t = decltype(get_component(0));
            if constexpr (std::is_lvalue_reference_v<component_return_t>)
            {
                if (sequential) [[likely]]
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        if (i + 16 < count) [[likely]] PREFETCH_R(&entities[i + 16]);
                        sparse_table_.emplace_back_dense_unchecked(
                            sparse_entry{static_cast<uint32_t>(dense_start + i), entities[i].parts_.version_});
                    }
                }
                else
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        if (i + 16 < count) [[likely]] PREFETCH_R(&entities[i + 16]);
                        uint32_t idx = entities[i].parts_.index_;
                        sparse_set_unchecked(idx, entities[i].parts_.version_, static_cast<uint32_t>(dense_start + i));
                    }
                }
                pool->append_bulk(&get_component(0), count);
            }
            else
            {
                dense<DT> temp_components;
                temp_components.increase_capacity(count);
                if (sequential) [[likely]]
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        if (i + 16 < count) [[likely]] PREFETCH_R(&entities[i + 16]);
                        sparse_table_.emplace_back_dense_unchecked(
                            sparse_entry{static_cast<uint32_t>(dense_start + i), entities[i].parts_.version_});
                        temp_components.push_back_unchecked(get_component(i));
                    }
                }
                else
                {
                    for (size_t i = 0; i < count; ++i)
                    {
                        if (i + 16 < count) [[likely]] PREFETCH_R(&entities[i + 16]);
                        uint32_t idx = entities[i].parts_.index_;
                        sparse_set_unchecked(idx, entities[i].parts_.version_, static_cast<uint32_t>(dense_start + i));
                        temp_components.push_back_unchecked(get_component(i));
                    }
                }
                pool->append_bulk_move(temp_components.data(), count);
            }

            entity_change_tracking_.increase_capacity(entity_change_tracking_.size() + count);
            for (size_t i = 0; i < count; ++i)
            {
                entity_change_tracking_.push_back_unchecked(change_tracking_entry{++global_change_counter_, ++global_added_counter_});
            }
        }
        else
        {
            dense<uint32_t> new_positions;
            dense<uint32_t> exist_positions;
            new_positions.increase_capacity(count);
            exist_positions.increase_capacity(count);
            for (size_t i = 0; i < count; ++i)
            {
                const entity& e = entities[i];
                uint32_t ver = sparse_version_at(e.parts_.index_);
                if (ver == e.parts_.version_)
                    exist_positions.push_back_unchecked(static_cast<uint32_t>(i));
                else
                    new_positions.push_back_unchecked(static_cast<uint32_t>(i));
            }

            size_t new_count = new_positions.size();
            size_t exist_count = exist_positions.size();

            if (new_count > 0)
            {
                size_t dense_old = dense_.size();
                dense_.increase_capacity(dense_old + new_count);
                versions_.increase_capacity(dense_old + new_count);
                pool->increase_capacity(pool->size() + new_count);
                typed_pool_data_ = pool->data();

                dense<uint32_t> new_entity_indices;
                dense<uint32_t> new_entity_versions;
                dense<DT> new_components;
                new_entity_indices.increase_capacity(new_count);
                new_entity_versions.increase_capacity(new_count);
                new_components.increase_capacity(new_count);
                for (size_t j = 0; j < new_count; ++j)
                {
                    size_t i = new_positions[j];
                    new_entity_indices.push_back_unchecked(entities[i].parts_.index_);
                    new_entity_versions.push_back_unchecked(entities[i].parts_.version_);
                    new_components.push_back_unchecked(get_component(i));
                }
                dense_.append_bulk(new_entity_indices.data(), new_count);
                for (size_t j = 0; j < new_count; ++j)
                    versions_.push_back_unchecked(new_entity_versions[j]);
                using component_return_t = decltype(get_component(0));
                if constexpr (std::is_rvalue_reference_v<component_return_t>)
                    pool->append_bulk_move(new_components.data(), new_count);
                else
                    pool->append_bulk(new_components.data(), new_count);

                for (size_t j = 0; j < new_count; ++j)
                {
                    size_t i = new_positions[j];
                    size_t idx = entities[i].parts_.index_;
                    sparse_set_unchecked(static_cast<uint32_t>(idx), entities[i].parts_.version_, static_cast<uint32_t>(dense_old + j));
                }

                entity_change_tracking_.increase_capacity(entity_change_tracking_.size() + new_count);
                for (size_t i = 0; i < new_count; ++i)
                {
                    entity_change_tracking_.push_back_unchecked(change_tracking_entry{++global_change_counter_, ++global_added_counter_});
                }
            }

            for (size_t j = 0; j < exist_count; ++j)
            {
                size_t i = exist_positions[j];
                const entity& e = entities[i];
                uint32_t dense_idx = sparse_dense_at(e.parts_.index_);
                (*pool)[dense_idx].~DT();
                new (&(*pool)[dense_idx]) DT(get_component(i));
                entity_change_tracking_[dense_idx] = change_tracking_entry{++global_change_counter_, entity_change_tracking_[dense_idx].added_version};
            }
        }

        ++version_;
        if (typed_pool_ && ops_.get_pool_data) typed_pool_data_ = ops_.get_pool_data(typed_pool_);
        check_mode_switch_();
        return result;
    }

public:
    void clear() noexcept
    {
        deallocate_all_pages_();
        hot_set_clear_();
        dense_.clear();
        versions_.clear();
        entity_change_tracking_.clear();
        if (typed_pool_ && ops_.clear_pool) ops_.clear_pool(typed_pool_);
    }

    single_class_set() noexcept = default;

    explicit single_class_set(size_t capacity) noexcept
    {
        increase_capacity(capacity);
    }

    template <typename T>
    single_class_set(entity e, T&& object, size_t r_size = 1024) noexcept
    {
        increase_capacity(r_size);
        add(e, std::forward<T>(object));
    }

    template <typename T>
    operating_message add(entity e, T&& object) noexcept
    {
        operating_message result;
        using DT = std::decay_t<T>;
        const int tid = type_id::get_type_id<DT>();
        if (type_id_ == -1) [[unlikely]]
        {
            type_id_ = tid;
            init_typed_storage<DT>();
        }
        else if (type_id_ != tid) [[unlikely]]
        {
            result.write_message(false, "single_class_set::add(): type mismatch");
            return result;
        }

        if (!e.is_valid()) [[unlikely]]
        {
            result.write_message(false, "single_class_set::add(): ID is invalid, index=", std::to_string(e.parts_.index_));
            return result;
        }

        auto* pool = get_typed_pool<DT>();

        // fast path: 末尾追加
        //   不变量: sparse_size_ == sparse_table_.index_ (由 sparse_set_at/clear 同步维护)
        //   因此 e.parts_.index_ == sparse_table_.index_, 可直接 append
        if (e.parts_.index_ == sparse_size_) [[likely]]
        {
            uint32_t dense_idx = static_cast<uint32_t>(dense_.size());
            if (dense_idx >= dense_.capacity()) [[unlikely]]
            {
                size_t new_cap = (dense_.capacity() == 0) ? 64 : dense_.capacity() * 2;
                dense_.increase_capacity(new_cap);
                versions_.increase_capacity(new_cap);
                entity_change_tracking_.increase_capacity(new_cap);
                pool->increase_capacity(new_cap);
                typed_pool_data_ = pool->data();
            }
            // 优化: emplace_back_dense_unchecked 替代 sparse_set_at -> sparse_emplace_at
            //   原方案: invalidate_count_cache + 多分支 bitmap_test + update_dense_status
            //   新方案: count_cache_ 增量更新 + 直接 placement new + bitmap_set, 无分支
            //   (append 场景下 is_dense_/hole_count_ 不变, update_dense_status 为 no-op)
            sparse_table_.emplace_back_dense_unchecked(sparse_entry{dense_idx, e.parts_.version_});
            sparse_size_ = static_cast<size_t>(e.parts_.index_) + 1;
            dense_.push_back_unchecked(e.parts_.index_);
            versions_.push_back_unchecked(e.parts_.version_);
            pool->push_back_unchecked(std::forward<T>(object));
            ++version_;
            if (track_changes_enabled_) [[likely]] {
                entity_change_tracking_.push_back_unchecked(
                    change_tracking_entry{++global_change_counter_, ++global_added_counter_});
            }
            if (on_add_) [[unlikely]] on_add_(e, &(*pool)[dense_idx], on_add_data_);
            check_mode_switch_();
            return result;
        }

        // slow path: index 可能超出当前范围或落在已存在区间内
        if (e.parts_.index_ >= sparse_size_)
        {
            sparse_size_ = static_cast<size_t>(e.parts_.index_) + 1;
        }

        // 单次加载 sparse_entry (8B = dense + version), 替代原 sparse_version_at + sparse_dense_at 两次加载
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        uint32_t ver = se ? se->version : 0;
        uint32_t dense_idx = se ? se->dense : dense_invalid;

        bool is_new_add = (ver != e.parts_.version_);

        if (se != nullptr && ver == e.parts_.version_) [[likely]]
        {
            void* old_ptr = &(*pool)[dense_idx];
            if (on_modify_) [[unlikely]]
            {
                on_modify_(e, old_ptr, on_modify_data_);
            }
            else
            {
                if (on_remove_) [[unlikely]] on_remove_(e, old_ptr, on_remove_data_);
            }
            (*pool)[dense_idx].~DT();
            new (&(*pool)[dense_idx]) DT(std::forward<T>(object));
            if (!on_modify_ && on_add_) [[unlikely]] on_add_(e, &(*pool)[dense_idx], on_add_data_);
        }
        else
        {
            dense_.push_back(e.parts_.index_);
            dense_idx = static_cast<uint32_t>(dense_.size() - 1);
            ver = e.parts_.version_;
            versions_.push_back(ver);
            // 优化: se != nullptr 表示 bitmap 已构造 (如 hard_remove 后复用 slot)
            //   直接赋值 sparse_entry (8B store), 替代 sparse_set_at -> sparse_emplace_at
            //   (sparse_emplace_at 会重复 bitmap_test/析构/重建, 此处 bitmap 已 set 无需操作)
            //   se == nullptr 表示从未构造, 需 sparse_set_at 设置 bitmap
            //   注: se != nullptr 时 e.parts_.index_ < sparse_table_.index_ (bitmap 已 set 说明
            //   之前 emplace 过, index_ 必 > idx), 不会破坏 sparse_size_ == index_ 不变量
            if (se) [[likely]]
            {
                sparse_table_[e.parts_.index_] = {dense_idx, ver};
            }
            else
            {
                sparse_set_at(e.parts_.index_, dense_idx, ver);
            }
            pool->push_back(std::forward<T>(object));
            if (on_add_) [[unlikely]] on_add_(e, &(*pool)[dense_idx], on_add_data_);
        }
        ++version_;
        if (track_changes_enabled_) [[unlikely]] {
            uint64_t preserved_added = (dense_idx < entity_change_tracking_.size())
                ? entity_change_tracking_[dense_idx].added_version : 0;
            uint64_t new_added = is_new_add ? ++global_added_counter_ : preserved_added;
            if (dense_idx < entity_change_tracking_.size())
            {
                entity_change_tracking_[dense_idx] =
                    change_tracking_entry{++global_change_counter_, new_added};
            }
            else
            {
                entity_change_tracking_.push_back(
                    change_tracking_entry{++global_change_counter_, new_added});
            }
        }
        typed_pool_data_ = pool->data();
        check_mode_switch_();
        return result;
    }

    template <typename T>
    operating_message add_batch(std::span<const entity> entities, std::span<const T> components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            operating_message result;
            result.write_message(false, "single_class_set::add_batch(): entities and components size mismatch");
            return result;
        }
        return add_batch_impl<T>(entities, entities.size(),
            [&components](size_t i) -> const T& { return components[i]; });
    }

    template <typename T>
    [[nodiscard]] T* get_ptr(entity e) noexcept
    {
        if (!e.is_valid() || type_id_ != type_id::get_type_id<T>()) [[unlikely]]
        {
            return nullptr;
        }
        // hot set 快速路径: 16B 紧凑布局, 2 次独立比较 (entity_key + pool_version_lo)
        //   entity_key = index|version 64-bit 单指令比较, 与 pool_version_lo 无依赖可并行
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        const uint64_t key = make_entity_key_(e);
        const uint32_t pool_ver_lo = static_cast<uint32_t>(version_);
        if (entry.entity_key == key && entry.pool_version_lo == pool_ver_lo) [[likely]]
        {
            return &(*get_typed_pool<T>())[entry.dense_index];
        }
        // slow path: 单次 is_constructed_at + 单次 sparse_entry 加载 (原方案 2 次各自检查)
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
            return nullptr;
        hot_set_[slot] = {key, se->dense, pool_ver_lo};
        return &(*get_typed_pool<T>())[se->dense];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr(entity e) const noexcept
    {
        if (!e.is_valid() || type_id_ != type_id::get_type_id<T>()) [[unlikely]]
            return nullptr;
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        const uint64_t key = make_entity_key_(e);
        const uint32_t pool_ver_lo = static_cast<uint32_t>(version_);
        if (entry.entity_key == key && entry.pool_version_lo == pool_ver_lo) [[likely]]
        {
            return &(*get_typed_pool<T>())[entry.dense_index];
        }
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[se->dense];
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_fast(entity e) noexcept
    {
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        const uint64_t key = make_entity_key_(e);
        const uint32_t pool_ver_lo = static_cast<uint32_t>(version_);
        if (entry.entity_key == key && entry.pool_version_lo == pool_ver_lo) [[likely]]
        {
            return &(*get_typed_pool<T>())[entry.dense_index];
        }
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
            return nullptr;
        hot_set_[slot] = {key, se->dense, pool_ver_lo};
        return &(*get_typed_pool<T>())[se->dense];
    }

    // 内联快速路径: 使用缓存的 typed_pool_data_ 避免 get_typed_pool 间接寻址
    // 热集 miss 时: 单次 is_constructed_at + 单次 sparse_entry 加载
    template <typename T>
    [[nodiscard]] T* get_ptr_fast_inline(entity e) noexcept
    {
        if (!typed_pool_data_) [[unlikely]]
            return get_ptr_fast<T>(e);
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        const uint64_t key = make_entity_key_(e);
        const uint32_t pool_ver_lo = static_cast<uint32_t>(version_);
        if (entry.entity_key == key && entry.pool_version_lo == pool_ver_lo) [[likely]]
        {
            return static_cast<T*>(typed_pool_data_) + entry.dense_index;
        }
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
            return nullptr;
        hot_set_[slot] = {key, se->dense, pool_ver_lo};
        return static_cast<T*>(typed_pool_data_) + se->dense;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast_inline(entity e) const noexcept
    {
        if (!typed_pool_data_) [[unlikely]]
            return get_ptr_fast<T>(e);
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        const uint64_t key = make_entity_key_(e);
        const uint32_t pool_ver_lo = static_cast<uint32_t>(version_);
        if (entry.entity_key == key && entry.pool_version_lo == pool_ver_lo) [[likely]]
        {
            return static_cast<const T*>(typed_pool_data_) + entry.dense_index;
        }
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
            return nullptr;
        return static_cast<const T*>(typed_pool_data_) + se->dense;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast(entity e) const noexcept
    {
        const size_t slot = e.parts_.index_ & (hot_set_capacity_ - 1);
        const auto& entry = hot_set_[slot];
        const uint64_t key = make_entity_key_(e);
        const uint32_t pool_ver_lo = static_cast<uint32_t>(version_);
        if (entry.entity_key == key && entry.pool_version_lo == pool_ver_lo) [[likely]]
        {
            return &(*get_typed_pool<T>())[entry.dense_index];
        }
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &(*get_typed_pool<T>())[se->dense];
    }

    // raw: 无任何边界检查, 调用方保证 idx 有效. 直接读 sparse_table_[idx].dense
    //   优化: 跳过 sparse_dense_at 的 is_constructed_at + sparse_size_ 检查 (原 2 次 bitmap 加载)
    template <typename T>
    [[nodiscard]] T* get_ptr_raw(entity e) noexcept
    {
        return static_cast<T*>(typed_pool_data_) + sparse_dense_at_unchecked(e.parts_.index_);
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_raw(entity e) const noexcept
    {
        return static_cast<const T*>(typed_pool_data_) + sparse_dense_at_unchecked(e.parts_.index_);
    }

    void prefetch_component(uint32_t entity_index) const noexcept
    {
        if (entity_index < sparse_table_.capacity())
            PREFETCH_R(&sparse_table_[entity_index]);
    }

    void prefetch_ptr(entity e) const noexcept
    {
        if (e.parts_.index_ < sparse_table_.capacity())
            PREFETCH_R(&sparse_table_[e.parts_.index_]);
    }

    void prefetch_ptr_batch(const entity* entities, size_t count) const noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            const uint32_t idx = entities[i].parts_.index_;
            if (idx < sparse_table_.capacity())
                PREFETCH_R(&sparse_table_[idx]);
        }
    }

    template <typename T>
    void prefetch_ptr_data(entity e) const noexcept
    {
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return;
        const uint32_t dense = sparse_dense_at(e.parts_.index_);
        if (dense == dense_invalid) [[unlikely]]
            return;
        PREFETCH_R(&(*get_typed_pool<T>())[dense]);
    }

    // 排序预取批量查询: 大批量时按 entity index 排序后顺序访问 sparse 表, 减少 cache miss
    // 使用 radix_sort_entries 直接排序结构体数组, 消除间接寻址
    // 返回 false 表示分配失败, 调用方回退到 chunk 路径
    template <typename T>
    bool get_ptr_batch_sorted_(const entity* entities, T** results, size_t count) noexcept
    {
        auto* pool = get_typed_pool<T>();

        // 条目: { key=entity_index, index=(version<<32 | orig_i) }
        struct batch_sort_entry
        {
            uint32_t key;
            size_t index;
        };

        constexpr size_t align = 64;
        auto* entries = static_cast<batch_sort_entry*>(
            ::operator new(count * sizeof(batch_sort_entry), std::align_val_t{align}, std::nothrow));
        if (!entries) [[unlikely]]
            return false;

        // 填充: 顺序访问 entities[], 无效 entity 标记 key=UINT32_MAX 排到末尾
        for (size_t i = 0; i < count; ++i)
        {
            const entity& e = entities[i];
            if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]]
            {
                entries[i].key = UINT32_MAX;
                entries[i].index = i;
            }
            else
            {
                entries[i].key = e.parts_.index_;
                entries[i].index = (static_cast<size_t>(e.parts_.version_) << 32) | i;
            }
        }

        // 按 entity_index 排序 (radix sort, O(n))
        radix_sort_entries<uint32_t>(entries, count);

        // 顺序处理: entries[] 顺序读, sparse 表顺序访问, 无间接寻址
        constexpr size_t pf_dist = 8;
        for (size_t i = 0; i < count; ++i)
        {
            if (i + pf_dist < count) [[likely]]
            {
                uint32_t pf_idx = entries[i + pf_dist].key;
                if (pf_idx != UINT32_MAX && pf_idx < sparse_table_.capacity())
                {
                    PREFETCH_R(&sparse_table_[pf_idx]);
                }
            }

            uint32_t eidx = entries[i].key;
            if (eidx == UINT32_MAX) [[unlikely]]
            {
                results[entries[i].index & 0xFFFFFFFF] = nullptr;
                continue;
            }

            size_t orig_i = entries[i].index & 0xFFFFFFFF;
            uint32_t ver = static_cast<uint32_t>(entries[i].index >> 32);

            const sparse_entry* se = sparse_entry_checked_(eidx);
            if (!se || se->dense == dense_invalid || se->version != ver) [[unlikely]]
            {
                results[orig_i] = nullptr;
                continue;
            }
            results[orig_i] = &(*pool)[se->dense];
        }

        ::operator delete(entries, count * sizeof(batch_sort_entry), std::align_val_t{align});
        return true;
    }

    template <typename T>
    void get_ptr_batch(const entity* entities, T** results, size_t count) noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]]
        {
            for (size_t i = 0; i < count; ++i) results[i] = nullptr;
            return;
        }

        // 大批量 + sparse 表超出 L3 缓存时走排序预取路径
        // sparse_size_ * 8 > 16MB 时随机访问 cache miss 率高, 排序后顺序访问更优
        // 小规模 sparse 表可被 L3 缓存, chunk 路径 + 预取已足够快
        if (count > 4096 && sparse_size_ > 2000000) [[unlikely]]
        {
            if (get_ptr_batch_sorted_<T>(entities, results, count))
                return;
        }

        auto* pool = get_typed_pool<T>();

        constexpr size_t chunk = 16;
        uint32_t dense_buf[chunk];

        for (size_t base = 0; base < count; base += chunk)
        {
            size_t n = base + chunk <= count ? chunk : count - base;

            for (size_t j = 0; j < n; ++j)
            {
                size_t i = base + j;
                const entity& e = entities[i];
                if (i + 8 < count) [[likely]]
                {
                    const uint32_t pf_idx = entities[i + 8].parts_.index_;
                    if (pf_idx < sparse_table_.capacity())
                        PREFETCH_R(&sparse_table_[pf_idx]);
                }

                if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]]
                {
                    dense_buf[j] = UINT32_MAX;
                    continue;
                }
                const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
                if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
                {
                    dense_buf[j] = UINT32_MAX;
                    continue;
                }
                dense_buf[j] = se->dense;
                PREFETCH_R(&(*pool)[se->dense]);
            }

            for (size_t j = 0; j < n; ++j)
            {
                uint32_t dense = dense_buf[j];
                if (dense == UINT32_MAX) [[unlikely]]
                {
                    results[base + j] = nullptr;
                    continue;
                }
                results[base + j] = &(*pool)[dense];
            }
        }
    }

    [[nodiscard]] uint32_t get_version(uint32_t entity_index) const noexcept
    {
        if (entity_index >= sparse_size_) [[unlikely]] return 0;
        return sparse_version_at(entity_index);
    }

    [[nodiscard]] uint32_t get_version_unchecked(uint32_t entity_index) const noexcept
    {
        return sparse_version_at(entity_index);
    }

    [[nodiscard]] uint64_t get_pool_version() const noexcept
    {
        return version_;
    }

    [[nodiscard]] uint64_t get_entity_change_version(size_t dense_index) const noexcept
    {
        if (dense_index >= entity_change_tracking_.size()) [[unlikely]] return 0;
        return entity_change_tracking_[dense_index].change_version;
    }

    [[nodiscard]] uint64_t get_entity_added_version(size_t dense_index) const noexcept
    {
        if (dense_index >= entity_change_tracking_.size()) [[unlikely]] return 0;
        return entity_change_tracking_[dense_index].added_version;
    }

    [[nodiscard]] uint64_t get_global_added_counter() const noexcept
    {
        return global_added_counter_;
    }

    [[nodiscard]] uint64_t get_global_change_counter() const noexcept
    {
        return global_change_counter_;
    }

    operating_message hard_remove(entity e) noexcept
    {
        operating_message result;
        if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]]
        {
            result.write_message(false, "single_class_set::hard_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return result;
        }
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
        {
            result.write_message(false, "single_class_set::hard_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return result;
        }

        auto index = se->dense;

        void* comp_ptr = typed_pool_ ? static_cast<char*>(typed_pool_) + index * component_size_ : nullptr;
        if (on_remove_ && comp_ptr) [[unlikely]] on_remove_(e, comp_ptr, on_remove_data_);

        auto moved_entity_id = dense_.back();
        dense_[index] = dense_.back();
        if (index < versions_.size() && !versions_.empty())
        {
            if (index < versions_.size() - 1)
                versions_[index] = versions_.back();
            versions_.pop_back();
        }

        if (moved_entity_id != e.parts_.index_) [[likely]]
        {
            // moved_entity_id 一定已构造 (它是 dense_.back() 对应的 entity)
            // 单次更新 dense 字段, 替代原 sparse_version_at_unchecked + sparse_set_unchecked
            // (原方案: 1 次 load version + 1 次 store {dense, version}; 现方案: 1 次 store dense)
            sparse_table_[moved_entity_id].dense = static_cast<uint32_t>(index);
        }
        dense_.pop_back();

        if (index < entity_change_tracking_.size() && entity_change_tracking_.size() > 0)
        {
            if (index < entity_change_tracking_.size() - 1)
            {
                entity_change_tracking_[index] = entity_change_tracking_.back();
            }
            entity_change_tracking_.pop_back();
        }

        if (typed_pool_)
        {
            if (ops_.is_trivially_copyable && typed_pool_data_ && ops_.pool_pop_back)
            {
                // 内联 trivial swap_pop: memcpy last→index + pop_back
                // dense_ 已 pop_back, dense_.size() 即为 last index (pool size 与 dense_ 同步)
                const size_t last = dense_.size();
                if (index != last) [[likely]]
                {
                    auto* data = static_cast<char*>(typed_pool_data_);
                    std::memcpy(data + index * component_size_, data + last * component_size_, component_size_);
                }
                ops_.pool_pop_back(typed_pool_);
            }
            else if (ops_.swap_pop)
            {
                ops_.swap_pop(typed_pool_, index);
            }
        }

        // 直接赋值 sparse_entry, 替代 sparse_set_at_unchecked
        // (sparse_emplace_at 内部有 bitmap 检查/析构/重建等冗余操作, 此处只需标记删除)
        // 必须同时重置 version=0, 否则后续 add slow path 误判为 "已存在" 导致 dense_invalid 越界
        sparse_table_[e.parts_.index_] = {dense_invalid, 0};
        ++version_;
        return result;
    }

    operating_message soft_remove(entity e) noexcept
    {
        operating_message result;
        if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]]
        {
            result.write_message(false, "single_class_set::soft_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return result;
        }
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
        {
            result.write_message(false, "single_class_set::soft_remove(): invalid entity or version mismatch, index=", std::to_string(e.parts_.index_));
            return result;
        }

        sparse_table_[e.parts_.index_] = {dense_invalid, 0};
        ++version_;
        return result;
    }

    [[nodiscard]] bool contains_entity(entity e) const noexcept
    {
        // 优化: 2 次检查 + 单次 sparse_entry 加载 (原 5 次冗余检查)
        //   原方案: 外层 idx>=sparse_size_ + sparse_dense_at 内部 idx>=sparse_size_
        //           + is_constructed_at + sparse_version_at 内部重复 2 次检查 = 5 次
        //   现方案: 1 次边界检查 + 1 次 is_constructed_at + 单次加载 8B sparse_entry
        if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]] return false;
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se) [[unlikely]] return false;
        return se->dense != dense_invalid && se->version == e.parts_.version_;
    }

    [[nodiscard]] int& get_type_id() noexcept
    {
        return type_id_;
    }

    template <typename T>
    [[nodiscard]] dense<T>* get_typed_pool_ptr() noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]] return nullptr;
        return static_cast<dense<T>*>(typed_pool_);
    }

    template <typename T>
    [[nodiscard]] const dense<T>* get_typed_pool_ptr() const noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]] return nullptr;
        return static_cast<const dense<T>*>(typed_pool_);
    }

    // 直接返回 typed_pool_data_ (缓存指针), 跳过 dense<T>::data() 间接寻址
    //   用于按 dense 索引顺序访问的热路径 (如 get_component_at_index)
    template <typename T>
    [[nodiscard]] T* get_typed_pool_data_ptr() noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]] return nullptr;
        return static_cast<T*>(typed_pool_data_);
    }

    template <typename T>
    [[nodiscard]] const T* get_typed_pool_data_ptr() const noexcept
    {
        if (type_id_ != type_id::get_type_id<T>()) [[unlikely]] return nullptr;
        return static_cast<const T*>(typed_pool_data_);
    }

    single_class_set(single_class_set&& other) noexcept
    : sparse_table_(std::move(other.sparse_table_))
    , sparse_size_(other.sparse_size_)
    , dense_(std::move(other.dense_))
    , type_id_(other.type_id_)
    , typed_pool_(other.typed_pool_)
    , typed_pool_data_(other.typed_pool_data_)
    , pending_increase_capacity_(other.pending_increase_capacity_)
    , component_size_(other.component_size_)
    , ops_(other.ops_)
    , version_(other.version_)
    , entity_change_tracking_(std::move(other.entity_change_tracking_))
    , global_change_counter_(other.global_change_counter_)
    , global_added_counter_(other.global_added_counter_)
    , track_changes_enabled_(other.track_changes_enabled_)
    , on_add_(other.on_add_)
    , on_add_data_(other.on_add_data_)
    , on_remove_(other.on_remove_)
    , on_remove_data_(other.on_remove_data_)
    , on_modify_(other.on_modify_)
    , on_modify_data_(other.on_modify_data_)
    {
        std::memcpy(hot_set_, other.hot_set_, sizeof(hot_set_));
        other.sparse_size_ = 0;
        other.typed_pool_ = nullptr;
        other.typed_pool_data_ = nullptr;
        other.ops_ = {};
        other.type_id_ = -1;
        other.pending_increase_capacity_ = 0;
        other.component_size_ = 0;
        other.version_ = 0;
        other.global_change_counter_ = 0;
        other.global_added_counter_ = 0;
        other.on_add_ = nullptr;
        other.on_add_data_ = nullptr;
        other.on_remove_ = nullptr;
        other.on_remove_data_ = nullptr;
        other.on_modify_ = nullptr;
        other.on_modify_data_ = nullptr;
    }

    single_class_set& operator=(single_class_set&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            if (typed_pool_ && ops_.destroy_pool) ops_.destroy_pool(typed_pool_);

            sparse_table_ = std::move(other.sparse_table_);
            sparse_size_ = other.sparse_size_;
            std::memcpy(hot_set_, other.hot_set_, sizeof(hot_set_));
            dense_ = std::move(other.dense_);
            typed_pool_ = other.typed_pool_;
            typed_pool_data_ = other.typed_pool_data_;
            ops_ = other.ops_;
            pending_increase_capacity_ = other.pending_increase_capacity_;
            component_size_ = other.component_size_;
            type_id_ = other.type_id_;
            version_ = other.version_;
            entity_change_tracking_ = std::move(other.entity_change_tracking_);
            global_change_counter_ = other.global_change_counter_;
            global_added_counter_ = other.global_added_counter_;
            track_changes_enabled_ = other.track_changes_enabled_;
            on_add_ = other.on_add_;
            on_add_data_ = other.on_add_data_;
            on_remove_ = other.on_remove_;
            on_remove_data_ = other.on_remove_data_;
            on_modify_ = other.on_modify_;
            on_modify_data_ = other.on_modify_data_;

            other.sparse_size_ = 0;
            other.typed_pool_ = nullptr;
            other.typed_pool_data_ = nullptr;
            other.ops_ = {};
            other.type_id_ = -1;
            other.pending_increase_capacity_ = 0;
            other.component_size_ = 0;
            other.version_ = 0;
            other.global_change_counter_ = 0;
            other.global_added_counter_ = 0;
            other.on_add_ = nullptr;
            other.on_add_data_ = nullptr;
            other.on_remove_ = nullptr;
            other.on_remove_data_ = nullptr;
            other.on_modify_ = nullptr;
            other.on_modify_data_ = nullptr;
        }
        return *this;
    }

    single_class_set(const single_class_set&) = delete;
    single_class_set& operator=(const single_class_set&) = delete;

    [[nodiscard]] constexpr size_t size() const noexcept
    {
        return dense_.size();
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return dense_.empty();
    }

    void increase_capacity(size_t capacity) noexcept
    {
        dense_.increase_capacity(capacity);
        versions_.increase_capacity(capacity);
        entity_change_tracking_.increase_capacity(capacity);
        sparse_table_.increase_capacity(capacity);
        if (typed_pool_ && ops_.increase_capacity_pool)
        {
            ops_.increase_capacity_pool(typed_pool_, capacity);
            if (ops_.get_pool_data) typed_pool_data_ = ops_.get_pool_data(typed_pool_);
        }
        else
        {
            if (capacity > pending_increase_capacity_) pending_increase_capacity_ = capacity;
        }
    }

    [[nodiscard]] dense<uint32_t>& get_entity_indices() noexcept
    {
        return dense_;
    }

    [[nodiscard]] const dense<uint32_t>& get_entity_indices() const noexcept
    {
        return dense_;
    }

    // 与 dense_ 同步的 version 数组, 遍历时直接读连续内存
    [[nodiscard]] dense<uint32_t>& get_entity_versions() noexcept
    {
        return versions_;
    }

    [[nodiscard]] const dense<uint32_t>& get_entity_versions() const noexcept
    {
        return versions_;
    }

    // >64 类型 slow path 的 sparse 交集检查
    [[nodiscard]] static bool sparse_contains_version(const single_class_set* set,
                                                     uint32_t idx,
                                                     uint32_t version) noexcept
    {
        if (!set) return false;
        if (idx >= set->sparse_size_) return false;
        uint32_t dense = set->sparse_dense_at(idx);
        uint32_t ver = set->sparse_version_at(idx);
        return dense != dense_invalid && ver == version;
    }

    [[nodiscard]] uint32_t get_dense_at(uint32_t entity_index) const noexcept
    {
        return sparse_dense_at(entity_index);
    }

    void swap_dense_and_pool(size_t i, size_t j) noexcept
    {
        if (i == j) [[unlikely]] return;
        uint32_t tmp = dense_[i];
        dense_[i] = dense_[j];
        dense_[j] = tmp;
        if (i < versions_.size() && j < versions_.size())
        {
            uint32_t tmp_v = versions_[i];
            versions_[i] = versions_[j];
            versions_[j] = tmp_v;
        }
        // 单次更新 dense 字段, 替代原 sparse_version_at_unchecked + sparse_set_unchecked
        // (dense_[i]/dense_[j] 已交换, 它们的 sparse_entry 一定已构造)
        sparse_table_[dense_[i]].dense = static_cast<uint32_t>(i);
        sparse_table_[dense_[j]].dense = static_cast<uint32_t>(j);
        if (i < entity_change_tracking_.size() && j < entity_change_tracking_.size())
        {
            change_tracking_entry ct_tmp = entity_change_tracking_[i];
            entity_change_tracking_[i] = entity_change_tracking_[j];
            entity_change_tracking_[j] = ct_tmp;
        }
        if (typed_pool_) [[likely]]
        {
            if (ops_.is_trivially_copyable && typed_pool_data_ && component_size_ <= 256)
            {
                auto* data = static_cast<char*>(typed_pool_data_);
                alignas(alignof(std::max_align_t)) char temp[256];
                std::memcpy(temp, data + i * component_size_, component_size_);
                std::memcpy(data + i * component_size_, data + j * component_size_, component_size_);
                std::memcpy(data + j * component_size_, temp, component_size_);
            }
            else if (ops_.swap_pool)
            {
                ops_.swap_pool(typed_pool_, i, j);
            }
        }
    }

    template <typename T>
    void reorder_dense_by_indices(const dense<size_t>& sorted_indices) noexcept
    {
        const size_t n = dense_.size();
        if (n <= 1 || sorted_indices.size() < n) [[unlikely]] return;

        dense<uint32_t> new_dense;
        new_dense.increase_capacity(n);
        for (size_t i = 0; i < n; ++i)
            new_dense.push_back(dense_[sorted_indices[i]]);

        dense<uint32_t> new_versions;
        if (versions_.size() >= n)
        {
            new_versions.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
                new_versions.push_back(versions_[sorted_indices[i]]);
        }

        dense<T>* typed_pool = get_typed_pool_ptr<T>();
        if (typed_pool)
        {
            dense<T> new_pool;
            new_pool.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
                new_pool.push_back(std::move((*typed_pool)[sorted_indices[i]]));
            *typed_pool = std::move(new_pool);
            if (ops_.get_pool_data) typed_pool_data_ = ops_.get_pool_data(typed_pool_);
        }

        dense<change_tracking_entry> new_tracking;
        if (entity_change_tracking_.size() >= n)
        {
            new_tracking.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
            {
                new_tracking.push_back(entity_change_tracking_[sorted_indices[i]]);
            }
            entity_change_tracking_ = std::move(new_tracking);
        }

        for (size_t i = 0; i < n; ++i)
            sparse_table_[new_dense[i]].dense = static_cast<uint32_t>(i);

        dense_ = std::move(new_dense);
        if (new_versions.size() > 0)
            versions_ = std::move(new_versions);
        ++version_;
    }

    [[nodiscard]] uint32_t sparse_dense_at_public(uint32_t idx) const noexcept
    {
        return sparse_dense_at(idx);
    }

    [[nodiscard]] uint32_t sparse_version_at_public(uint32_t idx) const noexcept
    {
        return sparse_version_at(idx);
    }

    // 合并 dense+version 查找: 单次 sparse_entry 加载, 替代两次独立调用
    //   返回 dense_index; 若未构造或越界返回 dense_invalid, version 通过 out 参数输出
    [[nodiscard]] uint32_t sparse_dense_version_public(uint32_t idx, uint32_t& out_version) const noexcept
    {
        if (idx >= sparse_size_) [[unlikely]]
        {
            out_version = 0;
            return dense_invalid;
        }
        if (!sparse_table_.is_constructed_at(idx)) [[unlikely]]
        {
            out_version = 0;
            return dense_invalid;
        }
        const auto& entry = sparse_table_[idx];
        out_version = entry.version;
        return entry.dense;
    }

    void prefetch_sparse_entry(uint32_t idx) const noexcept
    {
        if (idx < sparse_size_) [[likely]]
            PREFETCH_R(&sparse_table_[idx]);
    }

    [[nodiscard]] size_t get_sparse_size() const noexcept
    {
        return sparse_size_;
    }

    void clear_hot_set() noexcept
    {
        hot_set_clear_();
    }

    void bump_pool_version() noexcept
    {
        ++version_;
    }

    ~single_class_set() noexcept
    {
        if (typed_pool_ && ops_.destroy_pool) ops_.destroy_pool(typed_pool_);
    }
};

} // namespace ecs
