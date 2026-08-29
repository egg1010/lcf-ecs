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
#include "part/type_id.hpp"
#include "part/tiered_sort.hpp"
#include "part/memory/memory_pool.hpp"
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
class single_class_set;
template <typename> class query_context;

// 合并存储: dense 索引 + version 同一 cache line, 减少 get_ptr 慢路径 cache miss
struct sparse_entry
{
    uint32_t dense;
    uint32_t version;
};

// 迭代期安全迭代器前置声明
class safe_iterator;

// Global hot memory pool
[[nodiscard]] inline memory::memory_pool& global_hot_memory_pool() noexcept
{
    static memory::memory_pool hot_memory_pool;
    return hot_memory_pool;
}

class single_class_set
{
public:
    static constexpr uint32_t dense_invalid = 0xFFFFFFFFu;

private:
    // 2048 * 16B = 32KB: 桌面级 L1D (48KB) 驻留
    static constexpr size_t default_hot_set_capacity_ = 2048;

    // flat 稀疏表: dense_invalid 即墓碑标记, 单次内存访问完成判活+取值 (原 class_pool 需 bitmap+数据两次访问)
    sparse_entry* sparse_{nullptr};
    size_t sparse_cap_{0};
    size_t sparse_size_{0};

    // 扩容: 倍增 + 增量填墓碑
    void sparse_reserve_(size_t n) noexcept
    {
        if (n <= sparse_cap_) [[likely]] { return; }
        size_t new_cap = (sparse_cap_ == 0) ? 64 : sparse_cap_ * 2;
        if (new_cap < n) { new_cap = n; }
        auto* p = static_cast<sparse_entry*>(
            ::operator new(new_cap * sizeof(sparse_entry)));
        if (sparse_) [[likely]]
        {
            std::memcpy(p, sparse_, sparse_size_ * sizeof(sparse_entry));
            ::operator delete(sparse_, sparse_cap_ * sizeof(sparse_entry));
        }
        for (size_t i = sparse_size_; i < new_cap; ++i)
        {
            p[i] = {dense_invalid, 0};
        }
        sparse_ = p;
        sparse_cap_ = new_cap;
    }

    // 批量墓碑初始化 (clear 复用容量)
    void sparse_fill_tombstones_() noexcept
    {
        for (size_t i = 0; i < sparse_cap_; ++i)
        {
            sparse_[i] = {dense_invalid, 0};
        }
    }

    // hot set cache storage — 16B 紧凑布局 (原 32B alignas(32) 浪费 12B/entry)
    //   entity_key = entity_index(低32) | version(高32), 与 entity.parts_ 内存布局一致
    //   epoch_lo: 位置纪元低 32 位, 仅槽位变动操作递增 (add 纯追加不递增, 热集跨 add 有效)
    struct hot_entry_
    {
        uint64_t entity_key;       // entity_index | (version << 32)
        uint32_t dense_index;
        uint32_t epoch_lo;         // (uint32_t)position_epoch_
    };

    // 热集存储: Global hot memory pool 分配, 容量 = mask + 1 (2 的幂)
    struct hot_set_storage_
    {
        size_t mask;
        hot_entry_* entries;
    };
    hot_set_storage_ hot_{};

    void hot_set_allocate_(size_t entries) noexcept
    {
        hot_.entries = static_cast<hot_entry_*>(
            global_hot_memory_pool().allocate_zeroed(entries * sizeof(hot_entry_)));
        hot_.mask = entries - 1;
    }

    void hot_set_release_() noexcept
    {
        if (hot_.entries)
        {
            global_hot_memory_pool().deallocate(hot_.entries, (hot_.mask + 1) * sizeof(hot_entry_));
            hot_.entries = nullptr;
            hot_.mask = 0;
        }
    }

    // 位置纪元: 与 version_ (视图缓存失效) 解耦, 仅槽位变动操作递增
    uint32_t position_epoch_{0};

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
        void (*move_assign_element)(void* pool, size_t dst, size_t src) noexcept;
    } ops_{};

    // def 池: 运行期注册类型 (type_id::register_type_def) 的字节存储
    //   与模板池互斥复用同一批字段: typed_pool_ 指向 def_pool_t,
    //   typed_pool_data_/component_size_ 指向数据缓冲与元素大小, ops_ 填入 def 实现
    //   → 共享路径 (移除/回收/交换/扩容/清空/析构) 经既有 ops_ 间接调用零分支复用
    //   契约: 元素按字节搬运 (memcpy 重定位合法, 勿存自引用指针)
    struct def_pool_t
    {
        char* data{nullptr};
        size_t size{0};
        size_t capacity{0};
        size_t component_size{0};
        size_t alignment{0};
        bool trivially_copyable{false};
        void (*construct)(void*) noexcept{nullptr};
        void (*destruct)(void*) noexcept{nullptr};

        [[nodiscard]] void* element(size_t i) noexcept
        {
            return data + i * component_size;
        }
        [[nodiscard]] const void* element(size_t i) const noexcept
        {
            return data + i * component_size;
        }

        [[nodiscard]] static char* allocate_bytes(size_t bytes, size_t alignment) noexcept
        {
            if (bytes == 0) bytes = 1;
            if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            {
                return static_cast<char*>(
                    ::operator new(bytes, std::align_val_t(alignment), std::nothrow));
            }
            return static_cast<char*>(::operator new(bytes, std::nothrow));
        }

        static void release_bytes(char* p, size_t bytes, size_t alignment) noexcept
        {
            if (!p) return;
            if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            {
                ::operator delete(p, bytes, std::align_val_t(alignment));
            }
            else
            {
                ::operator delete(p, bytes);
            }
        }

        [[nodiscard]] bool reserve(size_t cap) noexcept
        {
            if (cap <= capacity) [[likely]] return true;
            char* nd = allocate_bytes(cap * component_size, alignment);
            if (!nd) [[unlikely]] return false;
            if (data)
            {
                std::memcpy(nd, data, size * component_size);
                release_bytes(data, capacity * component_size, alignment);
            }
            data = nd;
            capacity = cap;
            return true;
        }
    };

    uint64_t version_{0};
    // 变更追踪条目 (8B): 两字段均为对应 64 位全局计数器的低 32 位截断
    //   契约: 相等判定 (变更检测) 在两次观测间全局增量 < 2^32 时可靠;
    //         排序判定 (added 检测) 需环回安全比较, 跨度 < 2^31 时可靠
    //   8B 而非 16B: filter_changed/filter_added 重建扫描的 cache 行占用减半
    struct change_tracking_entry
    {
        uint32_t change_version;
        uint32_t added_version;
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

    // 迭代期删除支持: 活跃迭代器指针, 非迭代期为 nullptr
    safe_iterator* active_iterator_{nullptr};
    uint32_t iteration_depth_{0};

    // 溢出缓冲: 迭代器栈缓冲满时借用, 析构后保留 capacity 复用
    dense<entity> overflow_buffer_;
    bool overflow_in_use_{false};

    // 软删除死槽登记: soft_remove 登记 / add 复用 / compact 清空
    // 惰性校验: hard_remove 搬运会令旧条目过期, 弹出时重验 (活条目 sparse 回指自身槽位)
    dense<uint32_t> free_dense_;

    [[nodiscard]] dense<entity>* acquire_overflow_buffer_() noexcept
    {
        if (!overflow_in_use_) [[likely]]
        {
            overflow_in_use_ = true;
            overflow_buffer_.clear();
            return &overflow_buffer_;
        }
        // 嵌套迭代同时溢出: 回退到 thread_local
        static thread_local dense<entity> fallback;
        fallback.clear();
        return &fallback;
    }

    void release_overflow_buffer_() noexcept
    {
        overflow_in_use_ = false;
    }

    // 弹出一个有效死槽供 add 复用; 无有效死槽返回 dense_invalid
    // 惰性校验: 过期条目 (越界或槽位已被 hard_remove 搬运成活条目) 直接丢弃
    [[nodiscard]] uint32_t pop_free_slot_() noexcept
    {
        while (!free_dense_.empty())
        {
            const uint32_t pos = free_dense_.back();
            free_dense_.pop_back();
            if (pos < dense_.size() && sparse_[dense_[pos]].dense != pos)
            {
                return pos;
            }
        }
        return dense_invalid;
    }

    // 密度回收: 活条目前压, 墓碑条目析构并回调, 尾部截断
    // 判活: free_dense_ 构建候选墓碑位图, 位图外槽位必为活条目 (免随机 sparse 读)
    //   free_dense_ 可能含过期条目 (hard_remove 尾部搬运遗留), 位图内槽位仍需权威校验
    //   权威判据: sparse_[dense_[i]].dense == i (活条目的 sparse 回指自身槽位)
    void compact_impl_() noexcept
    {
        const size_t n = dense_.size();
        auto* comp_data = static_cast<char*>(typed_pool_data_);

        // 候选墓碑位图 (堆分配兼容 LCF_MINIMAL_STACK); 分配失败回退全量权威校验
        const size_t bitmap_words = (n + 63) >> 6;
        uint64_t* tomb_bitmap = static_cast<uint64_t*>(
            ::operator new(bitmap_words * sizeof(uint64_t), std::nothrow));
        if (tomb_bitmap) [[likely]]
        {
            std::memset(tomb_bitmap, 0, bitmap_words * sizeof(uint64_t));
            for (size_t i = 0; i < free_dense_.size(); ++i)
            {
                const uint32_t p = free_dense_[i];
                if (p < n) [[likely]]
                {
                    tomb_bitmap[p >> 6] |= 1ull << (p & 63);
                }
            }
        }

        size_t write = 0;
        for (size_t read = 0; read < n; ++read)
        {
            const uint32_t eid = dense_[read];
            // 候选者 (位图标记或位图不可用) 需权威校验; 其余槽位必活, 跳过随机 sparse 读
            const bool candidate = !tomb_bitmap ||
                ((tomb_bitmap[read >> 6] >> (read & 63)) & 1ull);
            if (!candidate || sparse_[eid].dense == read)
            {
                if (write != read)
                {
                    dense_[write] = eid;
                    if (write < versions_.size())
                    {
                        versions_[write] = versions_[read];
                    }
                    if (write < entity_change_tracking_.size())
                    {
                        entity_change_tracking_[write] = entity_change_tracking_[read];
                    }
                    if (typed_pool_)
                    {
                        // trivial 走 memcpy; 非 trivial 走 move 赋值 (目标为孤儿时由赋值释放旧资源)
                        if (ops_.is_trivially_copyable && comp_data)
                        {
                            std::memcpy(comp_data + write * component_size_,
                                        comp_data + read * component_size_, component_size_);
                        }
                        else if (ops_.move_assign_element)
                        {
                            ops_.move_assign_element(typed_pool_, write, read);
                        }
                    }
                    sparse_[eid].dense = static_cast<uint32_t>(write);
                }
                ++write;
            }
            else
            {
                // 墓碑: 物理移除点, 补发回调 (组件数据此刻仍完整, 前压写入不会先于本次回调)
                if (on_remove_ && comp_data)
                {
                    const uint32_t ver = (read < versions_.size()) ? versions_[read] : 0;
                    on_remove_(entity(eid, ver), comp_data + read * component_size_, on_remove_data_);
                }
            }
        }
        // 尾部截断: 非 trivial 孤儿由 pop_back 析构, trivial 为 no-op
        while (dense_.size() > write) { dense_.pop_back(); }
        while (versions_.size() > write) { versions_.pop_back(); }
        while (entity_change_tracking_.size() > write) { entity_change_tracking_.pop_back(); }
        if (typed_pool_ && ops_.pool_pop_back)
        {
            while (ops_.get_pool_size(typed_pool_) > write)
            {
                ops_.pool_pop_back(typed_pool_);
            }
        }
        free_dense_.clear();
        if (tomb_bitmap) [[likely]]
        {
            ::operator delete(tomb_bitmap, bitmap_words * sizeof(uint64_t));
        }
        if (write != n)
        {
            // 位置发生变动: 热集与视图缓存全量失效
            ++version_;
            ++position_epoch_;
        }
    }

    // 阈值自动回收: 墓碑数 >= max(n/4, 64) 时触发; 迭代期跳过 (延后到下一个安全点)
    void try_auto_compact_() noexcept
    {
        if (free_dense_.empty() || iteration_depth_ > 0) [[unlikely]]
        {
            return;
        }
        const size_t n = dense_.size();
        const size_t threshold = (n / 4 > 64) ? n / 4 : 64;
        if (free_dense_.size() >= threshold)
        {
            compact_impl_();
        }
    }

    friend class ecs::manager;
    template <typename> friend class ecs::query_context;
    friend class safe_iterator;

    [[nodiscard]] uint32_t sparse_dense_at_unchecked(uint32_t idx) const noexcept
    {
        return sparse_[idx].dense;
    }

    [[nodiscard]] uint32_t sparse_version_at_unchecked(uint32_t idx) const noexcept
    {
        return sparse_[idx].version;
    }

    // 单次加载 sparse_entry (8B), 返回 dense+version, 供 slow path 合并比较
    //   先检查 is_constructed_at (bitmap), 未构造返回 nullptr, 避免读到垃圾数据
    //   比 sparse_dense_at + sparse_version_at 少 1 次 is_constructed_at + 1 次 sparse_entry 加载
    [[nodiscard]] const sparse_entry* sparse_entry_checked_(uint32_t idx) const noexcept
    {
        if (idx >= sparse_cap_ || sparse_[idx].dense == dense_invalid) [[unlikely]]
            return nullptr;
        return &sparse_[idx];
    }

    // unchecked: 调用方保证 idx 已构造 (如 hot_set hit 后的 fast path)
    [[nodiscard]] const sparse_entry& sparse_entry_at_unchecked(uint32_t idx) const noexcept
    {
        return sparse_[idx];
    }

    // 排序预取批量查询实现: 按 entity index 排序后顺序访问 sparse 表, 减少 cache miss
    template <typename T>
    bool get_ptr_batch_sorted_(const entity* entities, T** results, size_t count) noexcept
    {
        auto* pool = get_typed_pool<T>();

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

        radix_sort_entries<uint32_t>(entries, count);

        constexpr size_t pf_dist = 8;
        for (size_t i = 0; i < count; ++i)
        {
            if (i + pf_dist < count) [[likely]]
            {
                uint32_t pf_idx = entries[i + pf_dist].key;
                if (pf_idx != UINT32_MAX && pf_idx < sparse_cap_)
                {
                    PREFETCH_R(&sparse_[pf_idx]);
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

    void sparse_set_at(uint32_t idx, uint32_t dense, uint32_t version) noexcept
    {
        sparse_reserve_(idx + 1); sparse_[idx] = sparse_entry{dense, version};
        if (idx >= sparse_size_)
            sparse_size_ = static_cast<size_t>(idx) + 1;
    }

    void sparse_set_at_unchecked(uint32_t idx, uint32_t dense, uint32_t version) noexcept
    {
        sparse_reserve_(idx + 1); sparse_[idx] = sparse_entry{dense, version};
        if (idx >= sparse_size_)
            sparse_size_ = static_cast<size_t>(idx) + 1;
    }

    void deallocate_all_pages_() noexcept
    {
        sparse_fill_tombstones_();
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
        if (!hot_.entries) [[unlikely]] { return; }
        const size_t slot = entity_index & hot_.mask;
        hot_.entries[slot] = {make_entity_key_(entity{entity_index, version}), dense_index, position_epoch_};
    }

    void hot_set_invalidate_(uint32_t entity_index) noexcept
    {
        if (!hot_.entries) [[unlikely]] { return; }
        const size_t slot = entity_index & hot_.mask;
        // 仅当 entity_index 匹配时清空 (检查 entity_key 低 32 位)
        if (static_cast<uint32_t>(hot_.entries[slot].entity_key) == entity_index)
        {
            hot_.entries[slot] = {0, 0, 0};
        }
    }

    void hot_set_clear_() noexcept
    {
        if (hot_.entries)
        {
            std::memset(hot_.entries, 0, (hot_.mask + 1) * sizeof(hot_entry_));
        }
    }

    // === hot set 查找统一核心 (get_ptr/get_ptr_fast/get_def_ptr/query_context 共用) ===
    // sparse 回退: 版本校验通过返回 dense 索引, 否则 dense_invalid
    [[nodiscard]] uint32_t sparse_valid_dense_(entity e) const noexcept
    {
        if (e.parts_.index_ >= sparse_size_) [[unlikely]]
            return dense_invalid;
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
            return dense_invalid;
        return se->dense;
    }

    // hot set 查找: 命中返回槽内 dense 索引, 未命中走 sparse 校验; epoch 由调用方提供
    //   (query_context 传构造期缓存值, 其余传 position_epoch_)
    [[nodiscard]] uint32_t hot_lookup_(entity e, uint32_t epoch_lo) const noexcept
    {
        if (!hot_.entries) [[unlikely]] { return sparse_valid_dense_(e); }
        const size_t slot = e.parts_.index_ & hot_.mask;
        const auto& entry = hot_.entries[slot];
        const uint64_t key = make_entity_key_(e);
        // entity_key 与 epoch_lo 两次比较无数据依赖, 可并行发射
        if (entry.entity_key == key && entry.epoch_lo == epoch_lo) [[likely]]
        {
            return entry.dense_index;
        }
        return sparse_valid_dense_(e);
    }

    // hot set 查找 + 回填 (非 const 专用): 未命中走 sparse 校验后写入热集
    [[nodiscard]] uint32_t hot_lookup_fill_(entity e, uint32_t epoch_lo) noexcept
    {
        if (!hot_.entries) [[unlikely]] { return sparse_valid_dense_(e); }
        const size_t slot = e.parts_.index_ & hot_.mask;
        const uint64_t key = make_entity_key_(e);
        const auto& entry = hot_.entries[slot];
        if (entry.entity_key == key && entry.epoch_lo == epoch_lo) [[likely]]
        {
            return entry.dense_index;
        }
        const uint32_t d = sparse_valid_dense_(e);
        if (d != dense_invalid)
        {
            hot_.entries[slot] = {key, d, epoch_lo};
        }
        return d;
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
                auto* data = reinterpret_cast<char*>(pool->data());
#if LCF_MINIMAL_STACK
                char* temp = static_cast<char*>(::operator new(comp_size, std::nothrow));
                if (temp) [[likely]]
                {
                    std::memcpy(temp, data + i * comp_size, comp_size);
                    std::memcpy(data + i * comp_size, data + j * comp_size, comp_size);
                    std::memcpy(data + j * comp_size, temp, comp_size);
                    ::operator delete(temp, comp_size);
                    return;
                }
                // 堆失败退化: 小栈块分块交换
                char tbuf[64];
                for (size_t off = 0; off < comp_size; off += sizeof(tbuf))
                {
                    size_t n = comp_size - off < sizeof(tbuf) ? comp_size - off : sizeof(tbuf);
                    std::memcpy(tbuf, data + i * comp_size + off, n);
                    std::memcpy(data + i * comp_size + off, data + j * comp_size + off, n);
                    std::memcpy(data + j * comp_size + off, tbuf, n);
                }
#else
                alignas(alignof(std::max_align_t)) char temp[256];
                std::memcpy(temp, data + i * comp_size, comp_size);
                std::memcpy(data + i * comp_size, data + j * comp_size, comp_size);
                std::memcpy(data + j * comp_size, temp, comp_size);
#endif
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
            /*.move_assign_element =*/[](void* p, size_t d, size_t s) noexcept {
                auto* pool = static_cast<dense<T>*>(p);
                (*pool)[d] = std::move((*pool)[s]);
            },
        };
    }

    // def 池初始化 (冷路径, add_def 首次调用时触发); 与模板池互斥, 已初始化返回 false
    [[nodiscard]] bool init_def_pool(int def_id, const type_def& def) noexcept
    {
        if (typed_pool_) [[unlikely]] return false;
        auto* p = new (std::nothrow) def_pool_t();
        if (!p) [[unlikely]] return false;
        p->component_size = def.size;
        p->alignment = def.alignment;
        p->trivially_copyable = def.trivially_copyable;
        p->construct = def.construct;
        p->destruct = def.destruct;
        component_size_ = def.size;
        if (pending_increase_capacity_ > 0)
        {
            if (!p->reserve(pending_increase_capacity_)) [[unlikely]]
            {
                delete p;
                return false;
            }
            dense_.increase_capacity(pending_increase_capacity_);
            versions_.increase_capacity(pending_increase_capacity_);
            entity_change_tracking_.increase_capacity(pending_increase_capacity_);
            pending_increase_capacity_ = 0;
        }
        typed_pool_ = p;
        typed_pool_data_ = p->data;
        type_id_ = def_id;
        // def ops: void* 均为 def_pool_t*; 非平凡元素按字节重定位 + 生命周期边界调用 destruct
        ops_ = {
            /*.destroy_pool =*/[](void* q) noexcept {
                auto* dp = static_cast<def_pool_t*>(q);
                if (!dp->trivially_copyable && dp->destruct)
                {
                    for (size_t i = 0; i < dp->size; ++i) dp->destruct(dp->element(i));
                }
                def_pool_t::release_bytes(dp->data, dp->capacity * dp->component_size, dp->alignment);
                delete dp;
            },
            /*.swap_pop =*/[](void* q, size_t index) noexcept {
                auto* dp = static_cast<def_pool_t*>(q);
                if (!dp->trivially_copyable && dp->destruct) dp->destruct(dp->element(index));
                if (index + 1 != dp->size)
                {
                    std::memcpy(dp->element(index), dp->element(dp->size - 1), dp->component_size);
                }
                --dp->size;
            },
            /*.clear_pool =*/[](void* q) noexcept {
                auto* dp = static_cast<def_pool_t*>(q);
                if (!dp->trivially_copyable && dp->destruct)
                {
                    for (size_t i = 0; i < dp->size; ++i) dp->destruct(dp->element(i));
                }
                dp->size = 0;
            },
            /*.increase_capacity_pool =*/[](void* q, size_t cap) noexcept {
                // 分配失败静默跳过 (与模板池语义一致, 后续 add 再试)
                (void)static_cast<def_pool_t*>(q)->reserve(cap);
            },
            /*.swap_pool =*/[](void* q, size_t i, size_t j) noexcept {
                auto* dp = static_cast<def_pool_t*>(q);
                char* temp = def_pool_t::allocate_bytes(dp->component_size, dp->alignment);
                if (temp) [[likely]]
                {
                    std::memcpy(temp, dp->element(i), dp->component_size);
                    std::memcpy(dp->element(i), dp->element(j), dp->component_size);
                    std::memcpy(dp->element(j), temp, dp->component_size);
                    def_pool_t::release_bytes(temp, dp->component_size, dp->alignment);
                }
            },
            /*.get_pool_data =*/[](void* q) noexcept -> void* {
                return static_cast<def_pool_t*>(q)->data;
            },
            /*.is_trivially_copyable =*/def.trivially_copyable,
            /*.swap_pop_trivial =*/[](void* q, size_t index, size_t comp_size) noexcept {
                auto* dp = static_cast<def_pool_t*>(q);
                if (index + 1 != dp->size)
                {
                    std::memcpy(dp->element(index), dp->element(dp->size - 1), comp_size);
                }
                --dp->size;
            },
            /*.swap_pool_trivial =*/[](void* q, size_t i, size_t j, size_t comp_size) noexcept {
                auto* dp = static_cast<def_pool_t*>(q);
                char* temp = def_pool_t::allocate_bytes(comp_size, dp->alignment);
                if (temp) [[likely]]
                {
                    std::memcpy(temp, dp->element(i), comp_size);
                    std::memcpy(dp->element(i), dp->element(j), comp_size);
                    std::memcpy(dp->element(j), temp, comp_size);
                    def_pool_t::release_bytes(temp, comp_size, dp->alignment);
                }
            },
            /*.get_pool_element =*/[](void* q, size_t index) noexcept -> void* {
                return static_cast<def_pool_t*>(q)->element(index);
            },
            /*.get_pool_size =*/[](void* q) noexcept -> size_t {
                return static_cast<def_pool_t*>(q)->size;
            },
            /*.pool_pop_back =*/[](void* q) noexcept {
                auto* dp = static_cast<def_pool_t*>(q);
                if (!dp->trivially_copyable && dp->destruct) dp->destruct(dp->element(dp->size - 1));
                --dp->size;
            },
            /*.move_assign_element =*/[](void* q, size_t d, size_t s) noexcept {
                auto* dp = static_cast<def_pool_t*>(q);
                if (!dp->trivially_copyable && dp->destruct) dp->destruct(dp->element(d));
                std::memcpy(dp->element(d), dp->element(s), dp->component_size);
            },
        };
        return true;
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
                result.write(false, "single_class_set::add_batch(): invalid entity index ", e.parts_.index_);
                return result;
            }
            if (e.parts_.index_ > max_index) max_index = e.parts_.index_;
            if (all_new && e.parts_.index_ < sparse_size_ && sparse_version_at(e.parts_.index_) == e.parts_.version_)
                all_new = false;
        }

        sparse_reserve_(max_index + 1);
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
            const size_t append_pos = sparse_size_;
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
                        sparse_[append_pos + i] = (sparse_entry{static_cast<uint32_t>(dense_start + i), entities[i].parts_.version_});
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
                        sparse_[append_pos + i] = (sparse_entry{static_cast<uint32_t>(dense_start + i), entities[i].parts_.version_});
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
                entity_change_tracking_.push_back_unchecked(change_tracking_entry{(uint32_t)++global_change_counter_, (uint32_t)++global_added_counter_});
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
                    entity_change_tracking_.push_back_unchecked(change_tracking_entry{(uint32_t)++global_change_counter_, (uint32_t)++global_added_counter_});
                }
            }

            for (size_t j = 0; j < exist_count; ++j)
            {
                size_t i = exist_positions[j];
                const entity& e = entities[i];
                uint32_t dense_idx = sparse_dense_at(e.parts_.index_);
                (*pool)[dense_idx].~DT();
                new (&(*pool)[dense_idx]) DT(get_component(i));
                entity_change_tracking_[dense_idx] = change_tracking_entry{(uint32_t)++global_change_counter_, entity_change_tracking_[dense_idx].added_version};
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
        // 迭代期禁止清空: 会破坏迭代器位置
        if (iteration_depth_ > 0) [[unlikely]] std::abort();
        deallocate_all_pages_();
        hot_set_clear_();
        dense_.clear();
        versions_.clear();
        entity_change_tracking_.clear();
        if (typed_pool_ && ops_.clear_pool) ops_.clear_pool(typed_pool_);
    }

    // #E 运行时类型擦除访问 (供序列化模块运行时路径使用)
    [[nodiscard]] void* get_raw_pool_data() noexcept { return typed_pool_data_; }
    [[nodiscard]] const void* get_raw_pool_data() const noexcept { return typed_pool_data_; }
    [[nodiscard]] size_t get_component_size() const noexcept { return component_size_; }
    [[nodiscard]] int get_type_id_value() const noexcept { return type_id_; }

    single_class_set() noexcept
    {
        hot_set_allocate_(default_hot_set_capacity_);
    }

    explicit single_class_set(size_t capacity) noexcept
        : single_class_set()
    {
        increase_capacity(capacity);
    }

    template <typename T>
    single_class_set(entity e, T&& object, size_t r_size = 1024) noexcept
        : single_class_set()
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
            result.write(false, "single_class_set::add(): type mismatch");
            return result;
        }

        if (!e.is_valid()) [[unlikely]]
        {
            result.write(false, "single_class_set::add(): ID is invalid, index=", e.parts_.index_);
            return result;
        }

        auto* pool = get_typed_pool<DT>();

        // fast path: 末尾追加
        //   不变量: sparse_size_ == 已初始化槽位数, e.parts_.index_ == sparse_size_ 时可直接写
        //   存在死槽时走 slow path 复用, 阻止 remove→add 循环下的无限增长
        if (e.parts_.index_ == sparse_size_ && free_dense_.empty()) [[likely]]
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
            sparse_reserve_(sparse_size_ + 1);
            sparse_[e.parts_.index_] = sparse_entry{dense_idx, e.parts_.version_};
            sparse_size_ = static_cast<size_t>(e.parts_.index_) + 1;
            dense_.push_back_unchecked(e.parts_.index_);
            versions_.push_back_unchecked(e.parts_.version_);
            pool->push_back_unchecked(std::forward<T>(object));
            ++version_;
            if (track_changes_enabled_) [[likely]] {
                entity_change_tracking_.push_back_unchecked(
                    change_tracking_entry{(uint32_t)++global_change_counter_, (uint32_t)++global_added_counter_});
            }
            if (on_add_) [[unlikely]] on_add_(e, &(*pool)[dense_idx], on_add_data_);
            check_mode_switch_();
            return result;
        }

        // slow path: index 可能超出当前范围或落在已存在区间内
        // 注: 不可在此预抬升 sparse_size_ — sparse_reserve_ 的墓碑填充以 sparse_size_ 为起点,
        //   预抬升会遗留未初始化间隙 [old_size, idx), 后续 checked_ 读到垃圾误判为活条目

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
            // 复用软删除死槽: 阻止 remove→add 循环下的尾部无限增长
            const uint32_t reuse = pop_free_slot_();
            if (reuse != dense_invalid) [[unlikely]]
            {
                dense_idx = reuse;
                ver = e.parts_.version_;
                dense_[reuse] = e.parts_.index_;
                if (reuse < versions_.size())
                {
                    versions_[reuse] = ver;
                }
                // 与下方 push_back 路径同构: bitmap 已构造直接赋值, 未构造需 sparse_set_at
                if (se) [[likely]]
                {
                    sparse_[e.parts_.index_] = {reuse, ver};
                }
                else
                {
                    sparse_set_at(e.parts_.index_, reuse, ver);
                }
                // 赋值覆盖孤儿: 非 trivial 由赋值释放旧资源, trivial 直接覆写
                (*pool)[reuse] = std::forward<T>(object);
                if (reuse < entity_change_tracking_.size())
                {
                    entity_change_tracking_[reuse] =
                        change_tracking_entry{(uint32_t)++global_change_counter_, (uint32_t)++global_added_counter_};
                }
                if (on_add_) [[unlikely]] on_add_(e, &(*pool)[reuse], on_add_data_);
                ++version_;
                check_mode_switch_();
                return result;
            }
            dense_.push_back(e.parts_.index_);
            dense_idx = static_cast<uint32_t>(dense_.size() - 1);
            ver = e.parts_.version_;
            versions_.push_back(ver);
            // 优化: se != nullptr 表示槽位已在容量内 (如 hard_remove 后复用 slot)
            //   直接赋值 8B sparse_entry, 免去 sparse_reserve_ 分支
            //   se == nullptr 表示 idx 超出当前容量, 需 sparse_set_at 扩容
            if (se) [[likely]]
            {
                sparse_[e.parts_.index_] = {dense_idx, ver};
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
            uint32_t preserved_added = (dense_idx < entity_change_tracking_.size())
                ? entity_change_tracking_[dense_idx].added_version : 0;
            uint32_t new_added = is_new_add ? (uint32_t)++global_added_counter_ : preserved_added;
            if (dense_idx < entity_change_tracking_.size())
            {
                entity_change_tracking_[dense_idx] =
                    change_tracking_entry{(uint32_t)++global_change_counter_, new_added};
            }
            else
            {
                entity_change_tracking_.push_back(
                    change_tracking_entry{(uint32_t)++global_change_counter_, new_added});
            }
        }
        typed_pool_data_ = pool->data();
        check_mode_switch_();
        return result;
    }

    // def 组件添加: 数据按字节从 data 拷入 (长度 = 注册时的 type_def::size)
    //   首次调用惰性初始化 def 池; 池语义与模板池互斥, 混用返回错误
    //   路径结构与 add<T> 同构: fast path 末尾追加 / slow path 覆盖|死槽复用|追加
    operating_message add_def(entity e, int def_id, const type_def& def, const void* data) noexcept
    {
        operating_message result;
        if (type_id_ == -1) [[unlikely]]
        {
            if (!init_def_pool(def_id, def)) [[unlikely]]
            {
                result.write(false, "single_class_set::add_def(): def pool init failed, id=", def_id);
                return result;
            }
        }
        else if (type_id_ != def_id) [[unlikely]]
        {
            result.write(false, "single_class_set::add_def(): type mismatch, id=", def_id);
            return result;
        }

        if (!e.is_valid()) [[unlikely]]
        {
            result.write(false, "single_class_set::add_def(): ID is invalid, index=", e.parts_.index_);
            return result;
        }

        auto* dp = static_cast<def_pool_t*>(typed_pool_);

        // fast path: 末尾追加 (同 add<T>)
        if (e.parts_.index_ == sparse_size_ && free_dense_.empty()) [[likely]]
        {
            uint32_t dense_idx = static_cast<uint32_t>(dense_.size());
            if (dense_idx >= dense_.capacity()) [[unlikely]]
            {
                size_t new_cap = (dense_.capacity() == 0) ? 64 : dense_.capacity() * 2;
                dense_.increase_capacity(new_cap);
                versions_.increase_capacity(new_cap);
                entity_change_tracking_.increase_capacity(new_cap);
                if (!dp->reserve(new_cap)) [[unlikely]]
                {
                    result.write(false, "single_class_set::add_def(): pool reserve failed, id=", def_id);
                    return result;
                }
                typed_pool_data_ = dp->data;
            }
            else if (dense_idx >= dp->capacity) [[unlikely]]
            {
                if (!dp->reserve((dp->capacity == 0) ? 64 : dp->capacity * 2)) [[unlikely]]
                {
                    result.write(false, "single_class_set::add_def(): pool reserve failed, id=", def_id);
                    return result;
                }
                typed_pool_data_ = dp->data;
            }
            sparse_reserve_(sparse_size_ + 1);
            sparse_[e.parts_.index_] = sparse_entry{dense_idx, e.parts_.version_};
            sparse_size_ = static_cast<size_t>(e.parts_.index_) + 1;
            dense_.push_back_unchecked(e.parts_.index_);
            versions_.push_back_unchecked(e.parts_.version_);
            std::memcpy(dp->element(dense_idx), data, dp->component_size);
            ++dp->size;  // 与 dense_ 同步 (不变量: dp->size == dense_.size())
            ++version_;
            if (track_changes_enabled_) [[likely]] {
                entity_change_tracking_.push_back_unchecked(
                    change_tracking_entry{(uint32_t)++global_change_counter_, (uint32_t)++global_added_counter_});
            }
            if (on_add_) [[unlikely]] on_add_(e, dp->element(dense_idx), on_add_data_);
            return result;
        }

        // slow path (同 add<T>): 覆盖已存在 / 复用软删除死槽 / 越界追加
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        uint32_t ver = se ? se->version : 0;
        uint32_t dense_idx = se ? se->dense : dense_invalid;

        bool is_new_add = (ver != e.parts_.version_);

        if (se != nullptr && ver == e.parts_.version_) [[likely]]
        {
            void* old_ptr = dp->element(dense_idx);
            if (on_modify_) [[unlikely]]
            {
                on_modify_(e, old_ptr, on_modify_data_);
            }
            else
            {
                if (on_remove_) [[unlikely]] on_remove_(e, old_ptr, on_remove_data_);
            }
            if (!dp->trivially_copyable && dp->destruct) dp->destruct(old_ptr);
            std::memcpy(old_ptr, data, dp->component_size);
            if (!on_modify_ && on_add_) [[unlikely]] on_add_(e, old_ptr, on_add_data_);
        }
        else
        {
            const uint32_t reuse = pop_free_slot_();
            if (reuse != dense_invalid) [[unlikely]]
            {
                dense_idx = reuse;
                ver = e.parts_.version_;
                dense_[reuse] = e.parts_.index_;
                if (reuse < versions_.size())
                {
                    versions_[reuse] = ver;
                }
                if (se) [[likely]]
                {
                    sparse_[e.parts_.index_] = {reuse, ver};
                }
                else
                {
                    sparse_set_at(e.parts_.index_, reuse, ver);
                }
                std::memcpy(dp->element(reuse), data, dp->component_size);
                if (reuse < entity_change_tracking_.size())
                {
                    entity_change_tracking_[reuse] =
                        change_tracking_entry{(uint32_t)++global_change_counter_, (uint32_t)++global_added_counter_};
                }
                if (on_add_) [[unlikely]] on_add_(e, dp->element(reuse), on_add_data_);
                ++version_;
                return result;
            }
            dense_.push_back(e.parts_.index_);
            dense_idx = static_cast<uint32_t>(dense_.size() - 1);
            ver = e.parts_.version_;
            versions_.push_back(ver);
            if (se) [[likely]]
            {
                sparse_[e.parts_.index_] = {dense_idx, ver};
            }
            else
            {
                sparse_set_at(e.parts_.index_, dense_idx, ver);
            }
            if (dense_idx >= dp->capacity) [[unlikely]]
            {
                if (!dp->reserve((dense_idx + 1) * 2)) [[unlikely]]
                {
                    result.write(false, "single_class_set::add_def(): pool reserve failed, id=", def_id);
                    return result;
                }
                typed_pool_data_ = dp->data;
            }
            std::memcpy(dp->element(dense_idx), data, dp->component_size);
            ++dp->size;  // 与 dense_ 同步 (不变量: dp->size == dense_.size())
            if (on_add_) [[unlikely]] on_add_(e, dp->element(dense_idx), on_add_data_);
        }
        ++version_;
        if (track_changes_enabled_) [[unlikely]] {
            uint32_t preserved_added = (dense_idx < entity_change_tracking_.size())
                ? entity_change_tracking_[dense_idx].added_version : 0;
            uint32_t new_added = is_new_add ? (uint32_t)++global_added_counter_ : preserved_added;
            if (dense_idx < entity_change_tracking_.size())
            {
                entity_change_tracking_[dense_idx] =
                    change_tracking_entry{(uint32_t)++global_change_counter_, new_added};
            }
            else
            {
                entity_change_tracking_.push_back(
                    change_tracking_entry{(uint32_t)++global_change_counter_, new_added});
            }
        }
        typed_pool_data_ = dp->data;
        return result;
    }

    template <typename T>
    operating_message add_batch(std::span<const entity> entities, std::span<const T> components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            operating_message result;
            result.write(false, "single_class_set::add_batch(): entities and components size mismatch");
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
        const uint32_t d = hot_lookup_fill_(e, position_epoch_);
        return d == dense_invalid ? nullptr : &(*get_typed_pool<T>())[d];
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr(entity e) const noexcept
    {
        if (!e.is_valid() || type_id_ != type_id::get_type_id<T>()) [[unlikely]]
            return nullptr;
        const uint32_t d = hot_lookup_(e, position_epoch_);
        return d == dense_invalid ? nullptr : &(*get_typed_pool<T>())[d];
    }

    // 信任路径: 跳过类型检查 (调用方保证类型匹配), 经缓存的 typed_pool_data_ 寻址
    template <typename T>
    [[nodiscard]] T* get_ptr_fast(entity e) noexcept
    {
        if (!typed_pool_data_) [[unlikely]]
            return &(*get_typed_pool<T>())[sparse_dense_at(e.parts_.index_)];
        const uint32_t d = hot_lookup_fill_(e, position_epoch_);
        return d == dense_invalid ? nullptr : static_cast<T*>(typed_pool_data_) + d;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast(entity e) const noexcept
    {
        if (!typed_pool_data_) [[unlikely]]
            return &(*get_typed_pool<T>())[sparse_dense_at(e.parts_.index_)];
        const uint32_t d = hot_lookup_(e, position_epoch_);
        return d == dense_invalid ? nullptr : static_cast<const T*>(typed_pool_data_) + d;
    }

    // def 组件指针查询: hot set 快速路径 (同 get_ptr_fast<T>), 经 typed_pool_data_ 寻址
    [[nodiscard]] void* get_def_ptr(entity e) noexcept
    {
        if (!typed_pool_data_) [[unlikely]] return nullptr;
        const uint32_t d = hot_lookup_fill_(e, position_epoch_);
        return d == dense_invalid
            ? nullptr
            : static_cast<char*>(typed_pool_data_) + size_t(d) * component_size_;
    }

    [[nodiscard]] const void* get_def_ptr(entity e) const noexcept
    {
        if (!typed_pool_data_) [[unlikely]] return nullptr;
        const uint32_t d = hot_lookup_(e, position_epoch_);
        return d == dense_invalid
            ? nullptr
            : static_cast<const char*>(typed_pool_data_) + size_t(d) * component_size_;
    }

    // def 组件按 dense 索引直接访问 (遍历用)
    [[nodiscard]] void* get_def_element(size_t dense_index) noexcept
    {
        if (!typed_pool_data_ || dense_index >= dense_.size()) [[unlikely]] return nullptr;
        return static_cast<char*>(typed_pool_data_) + dense_index * component_size_;
    }

    [[nodiscard]] const void* get_def_element(size_t dense_index) const noexcept
    {
        if (!typed_pool_data_ || dense_index >= dense_.size()) [[unlikely]] return nullptr;
        return static_cast<const char*>(typed_pool_data_) + dense_index * component_size_;
    }

    // raw: 无任何边界检查, 调用方保证 idx 有效. 直接读 sparse_[idx].dense
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

    void prefetch_ptr_batch(const entity* entities, size_t count) const noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            const uint32_t idx = entities[i].parts_.index_;
            if (idx < sparse_cap_)
                PREFETCH_R(&sparse_[idx]);
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
                    if (pf_idx < sparse_cap_)
                        PREFETCH_R(&sparse_[pf_idx]);
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

    // sparse 低层查询: 越界或未构造返回 dense_invalid / 0
    [[nodiscard]] uint32_t sparse_dense_at(uint32_t idx) const noexcept
    {
        if (idx >= sparse_size_) [[unlikely]]
            return dense_invalid;
        if (idx >= sparse_cap_ || sparse_[idx].dense == dense_invalid) [[unlikely]]
            return dense_invalid;
        return sparse_[idx].dense;
    }

    [[nodiscard]] uint32_t sparse_version_at(uint32_t idx) const noexcept
    {
        if (idx >= sparse_size_) [[unlikely]]
            return 0;
        if (idx >= sparse_cap_ || sparse_[idx].dense == dense_invalid) [[unlikely]]
            return 0;
        return sparse_[idx].version;
    }

    // 合并查询: 单次加载 sparse_entry, 返回 dense_index 并经 out 参数输出 version
    [[nodiscard]] uint32_t sparse_find(uint32_t idx, uint32_t& out_version) const noexcept
    {
        if (idx >= sparse_size_) [[unlikely]]
        {
            out_version = 0;
            return dense_invalid;
        }
        if (idx >= sparse_cap_ || sparse_[idx].dense == dense_invalid) [[unlikely]]
        {
            out_version = 0;
            return dense_invalid;
        }
        const auto& entry = sparse_[idx];
        out_version = entry.version;
        return entry.dense;
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

    // 返回值仅保证相等判定语义 (32 位截断域), 勿与 get_global_change_counter() 做大小比较
    [[nodiscard]] uint64_t get_entity_change_version(size_t dense_index) const noexcept
    {
        if (dense_index >= entity_change_tracking_.size()) [[unlikely]] return 0;
        return entity_change_tracking_[dense_index].change_version;
    }

    [[nodiscard]] const change_tracking_entry* get_entity_change_tracking_data() const noexcept
    {
        return entity_change_tracking_.data();
    }

    // 返回值仅保证相等判定语义 (32 位截断域), 勿与 get_global_added_counter() 做大小比较
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

    operating_message hard_remove(entity e) noexcept;

    operating_message soft_remove(entity e) noexcept
    {
        operating_message result;
        if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]]
        {
            result.write(false, "single_class_set::soft_remove(): invalid entity or version mismatch, index=", e.parts_.index_);
            return result;
        }
        const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
        {
            result.write(false, "single_class_set::soft_remove(): invalid entity or version mismatch, index=", e.parts_.index_);
            return result;
        }

        // 登记死槽供 add 复用; 墓碑的物理移除延迟到 compact (组件数据暂留)
        free_dense_.push_back(se->dense);
        sparse_[e.parts_.index_] = {dense_invalid, 0};
        ++version_;
        ++position_epoch_;
        try_auto_compact_();
        return result;
    }

    // 密度回收 (手动入口): 活条目前压, 墓碑物理移除并补发 on_remove 回调
    // 迭代期调用为程序错误 (与 clear/swap 同策略)
    operating_message compact() noexcept
    {
        operating_message result;
        if (iteration_depth_ > 0) [[unlikely]]
        {
            result.write(false, "single_class_set::compact(): forbidden during iteration");
            return result;
        }
        const size_t before = dense_.size();
        compact_impl_();
        result.write(true, "single_class_set::compact(): reclaimed ", before - dense_.size(), " tombstones");
        return result;
    }

    // 活条目数 (O(n) 扫描): size() 为物理槽位数 (含墓碑), 本接口为逻辑存活数
    [[nodiscard]] size_t live_count() const noexcept
    {
        const size_t n = dense_.size();
        size_t live = 0;
        for (size_t i = 0; i < n; ++i)
        {
            if (sparse_[dense_[i]].dense == i)
            {
                ++live;
            }
        }
        return live;
    }

    // 墓碑数 (O(n) 扫描): soft_remove 后尚未回收的死条目
    [[nodiscard]] size_t tombstone_count() const noexcept
    {
        return dense_.size() - live_count();
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
    : sparse_(other.sparse_)
    , sparse_cap_(other.sparse_cap_)
    , sparse_size_(other.sparse_size_)
    , hot_(other.hot_)
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
        position_epoch_ = other.position_epoch_;
        other.sparse_ = nullptr;
        other.sparse_cap_ = 0;
        other.sparse_size_ = 0;
        other.hot_ = {};
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
            if (sparse_) [[likely]]
            {
                ::operator delete(sparse_, sparse_cap_ * sizeof(sparse_entry));
            }

            sparse_ = other.sparse_;
            sparse_cap_ = other.sparse_cap_;
            sparse_size_ = other.sparse_size_;
            hot_set_release_();
            hot_ = other.hot_;
            position_epoch_ = other.position_epoch_;
            dense_ = std::move(other.dense_);
            typed_pool_ = other.typed_pool_;
            typed_pool_data_ = other.typed_pool_data_;
            ops_ = other.ops_;
            pending_increase_capacity_ = other.pending_increase_capacity_;
            component_size_ = other.component_size_;
            type_id_ = other.type_id_;
            version_ = other.version_;
            entity_change_tracking_ = std::move(other.entity_change_tracking_);
            free_dense_ = std::move(other.free_dense_);
            global_change_counter_ = other.global_change_counter_;
            global_added_counter_ = other.global_added_counter_;
            track_changes_enabled_ = other.track_changes_enabled_;
            on_add_ = other.on_add_;
            on_add_data_ = other.on_add_data_;
            on_remove_ = other.on_remove_;
            on_remove_data_ = other.on_remove_data_;
            on_modify_ = other.on_modify_;
            on_modify_data_ = other.on_modify_data_;

            other.sparse_ = nullptr;
            other.sparse_cap_ = 0;
            other.sparse_size_ = 0;
            other.hot_ = {};
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
        sparse_reserve_(capacity);
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

    void swap_dense_and_pool(size_t i, size_t j) noexcept
    {
        // 迭代期禁止交换: 会破坏迭代器位置
        if (iteration_depth_ > 0) [[unlikely]] std::abort();
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
        sparse_[dense_[i]].dense = static_cast<uint32_t>(i);
        sparse_[dense_[j]].dense = static_cast<uint32_t>(j);
        if (i < entity_change_tracking_.size() && j < entity_change_tracking_.size())
        {
            change_tracking_entry ct_tmp = entity_change_tracking_[i];
            entity_change_tracking_[i] = entity_change_tracking_[j];
            entity_change_tracking_[j] = ct_tmp;
        }
        if (typed_pool_) [[likely]]
        {
#if LCF_MINIMAL_STACK
            if (ops_.swap_pool)
            {
                ops_.swap_pool(typed_pool_, i, j);
            }
#else
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
#endif
        }
        // 槽位互换: 热集 dense_index 全部过期
        ++position_epoch_;
    }

    template <typename T>
    void reorder_dense_by_indices(const dense<size_t>& sorted_indices) noexcept
    {
        // 迭代期禁止重排: 会破坏迭代器位置
        if (iteration_depth_ > 0) [[unlikely]] std::abort();
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
            sparse_[new_dense[i]].dense = static_cast<uint32_t>(i);

        dense_ = std::move(new_dense);
        if (new_versions.size() > 0)
            versions_ = std::move(new_versions);
        ++version_;
        ++position_epoch_;
    }

    void prefetch_sparse_entry(uint32_t idx) const noexcept
    {
        if (idx < sparse_size_) [[likely]]
            PREFETCH_R(&sparse_[idx]);
    }

    [[nodiscard]] size_t get_sparse_size() const noexcept
    {
        return sparse_size_;
    }

    void clear_hot_set() noexcept
    {
        hot_set_clear_();
    }

    // 热集容量调整 (2 的幂), 改变即清空热集
    void set_hot_set_capacity(size_t entries) noexcept
    {
        if (entries == 0 || (entries & (entries - 1)) != 0) [[unlikely]]
        {
            assert(false && "set_hot_set_capacity(): entries must be power of two");
            return;
        }
        hot_set_release_();
        hot_set_allocate_(entries);
    }

    [[nodiscard]] size_t hot_set_capacity() const noexcept
    {
        return hot_.entries ? hot_.mask + 1 : 0;
    }

    void bump_pool_version() noexcept
    {
        ++version_;
        ++position_epoch_;
    }

    [[nodiscard]] uint32_t get_iteration_depth() const noexcept
    {
        return iteration_depth_;
    }

    ~single_class_set() noexcept
    {
        if (typed_pool_ && ops_.destroy_pool) ops_.destroy_pool(typed_pool_);
        hot_set_release_();
        if (sparse_) [[likely]]
        {
            ::operator delete(sparse_, sparse_cap_ * sizeof(sparse_entry));
        }
    }
};

// 迭代期安全迭代器: swap_pop 后自动补访漏掉的实体
class safe_iterator
{
private:
    single_class_set* set_;
    uint32_t index_{0};

    // 补访队列: 被 swap 到已访问区的实体
    static constexpr uint32_t INLINE_PENDING_CAP = 16;
    entity pending_inline_[INLINE_PENDING_CAP];
    uint32_t pending_count_{0};

    // 栈缓冲溢出时借用的堆缓冲
    dense<entity>* pending_overflow_{nullptr};

    // 当前位置被 back 替换时跳过一次 ++
    bool skip_advance_{false};

    // 嵌套迭代: 保存前一个活跃迭代器
    safe_iterator* prev_iterator_{nullptr};

public:
    safe_iterator(single_class_set* s) noexcept : set_(s)
    {
        prev_iterator_ = set_->active_iterator_;
        set_->active_iterator_ = this;
        ++set_->iteration_depth_;
    }

    struct end_tag {};
    safe_iterator(single_class_set* s, end_tag) noexcept : set_(s) {}

    safe_iterator(const safe_iterator&) = delete;
    safe_iterator& operator=(const safe_iterator&) = delete;

    ~safe_iterator() noexcept
    {
        if (prev_iterator_ != nullptr || set_->active_iterator_ == this)
        {
            set_->active_iterator_ = prev_iterator_;
            --set_->iteration_depth_;
        }
        if (pending_overflow_)
        {
            set_->release_overflow_buffer_();
        }
    }

    // 通知迭代器即将 swap_pop, old_last 为 swap 前的 back 位置
    void on_swap_remove(uint32_t removed_idx, uint32_t old_last, bool is_back) noexcept
    {
        if (is_back) [[unlikely]] return;

        if (removed_idx < index_) [[unlikely]]
        {
            // 被 swap 到已访问区, 加入补访
            push_pending(entity(set_->dense_[old_last], set_->versions_[old_last]));
        }
        else if (removed_idx == index_) [[unlikely]]
        {
            // 当前位置被 back 替换 → 不前进
            skip_advance_ = true;
        }
    }

    void push_pending(entity e) noexcept
    {
        if (pending_count_ < INLINE_PENDING_CAP) [[likely]]
        {
            pending_inline_[pending_count_++] = e;
            return;
        }
        // 栈缓冲满: 借用 set_ 的溢出缓冲
        if (!pending_overflow_) [[unlikely]]
        {
            pending_overflow_ = set_->acquire_overflow_buffer_();
        }
        pending_overflow_->push_back(e);
    }

    [[nodiscard]] bool pending_empty() const noexcept
    {
        return pending_count_ == 0
            && (!pending_overflow_ || pending_overflow_->empty());
    }

    [[nodiscard]] entity pending_back() const noexcept
    {
        if (pending_overflow_ && !pending_overflow_->empty()) [[unlikely]]
            return pending_overflow_->back();
        return pending_inline_[pending_count_ - 1];
    }

    void pending_pop() noexcept
    {
        if (pending_overflow_ && !pending_overflow_->empty()) [[unlikely]]
        {
            pending_overflow_->pop_back();
            return;
        }
        --pending_count_;
    }

    [[nodiscard]] entity operator*() const noexcept
    {
        if (!pending_empty()) [[unlikely]]
            return pending_back();
        return entity(set_->dense_[index_], set_->versions_[index_]);
    }

    [[nodiscard]] uint32_t current_dense_index() const noexcept
    {
        if (!pending_empty()) [[unlikely]]
        {
            return single_class_set::dense_invalid;
        }
        return index_;
    }

    safe_iterator& operator++() noexcept
    {
        if (!pending_empty()) [[unlikely]]
        {
            pending_pop();
            return *this;
        }
        if (skip_advance_) [[unlikely]]
        {
            skip_advance_ = false;
            return *this;
        }
        ++index_;
        return *this;
    }

    [[nodiscard]] bool operator!=(const safe_iterator& /*end*/) const noexcept
    {
        if (!pending_empty()) [[unlikely]] return true;
        return index_ < set_->size();
    }

    [[nodiscard]] bool operator==(const safe_iterator& end) const noexcept
    {
        return !(*this != end);
    }
};

// 类外定义: 依赖 safe_iterator 完整类型
inline operating_message single_class_set::hard_remove(entity e) noexcept
{
    operating_message result;
    if (!e.is_valid() || e.parts_.index_ >= sparse_size_) [[unlikely]]
    {
        result.write(false, "single_class_set::hard_remove(): invalid entity or version mismatch, index=", e.parts_.index_);
        return result;
    }
    const sparse_entry* se = sparse_entry_checked_(e.parts_.index_);
    if (!se || se->dense == dense_invalid || se->version != e.parts_.version_) [[unlikely]]
    {
        result.write(false, "single_class_set::hard_remove(): invalid entity or version mismatch, index=", e.parts_.index_);
        return result;
    }

    auto index = se->dense;

    // 迭代期: swap_pop 前通知活跃迭代器
    if (active_iterator_) [[unlikely]]
    {
        const uint32_t old_last = static_cast<uint32_t>(dense_.size()) - 1;
        const bool is_back = (static_cast<uint32_t>(index) == old_last);
        active_iterator_->on_swap_remove(static_cast<uint32_t>(index), old_last, is_back);
    }

    void* comp_ptr = typed_pool_data_
        ? static_cast<char*>(typed_pool_data_) + index * component_size_ : nullptr;
    if (on_remove_ && comp_ptr) [[unlikely]] on_remove_(e, comp_ptr, on_remove_data_);

    auto moved_entity_id = dense_.back();
    // 尾部可能是墓碑 (软删除条目): 搬运后不得回写 sparse, 否则幽灵复活
    const bool moved_is_tombstone =
        (sparse_[moved_entity_id].dense == dense_invalid);
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
        if (moved_is_tombstone)
        {
            // 墓碑搬运到 index 槽: sparse 保持 dense_invalid, 登记新死槽供复用
            free_dense_.push_back(static_cast<uint32_t>(index));
        }
        else
        {
            sparse_[moved_entity_id].dense = static_cast<uint32_t>(index);
        }
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
    sparse_[e.parts_.index_] = {dense_invalid, 0};
    ++version_;
    ++position_epoch_;
    return result;
}

} // namespace ecs
