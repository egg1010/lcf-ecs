#pragma once

// manager.hpp - ECS 模块统一入口：manager / 实体 / 组件存储 / 视图 / 分组 / 命令缓冲
#include <concepts>
#include <tuple>
#include <array>
#include <vector>
#include <limits>
#include <type_traits>
#include <cstring>
#include <cstdlib>
#include <new>
#include "ecs/storage/single_class_set.hpp"
#include "part/type/type_id.hpp"
#include "ecs/entity/entity_manager.hpp"
#include "ecs/group/group.hpp"
#include "ecs/group/reorder.hpp"
#include "ecs/view/runtime_view.hpp"
#include "part/radix_sort_helper.hpp"
#include "part/tiered_sort.hpp"
#include "part/signal_queue.hpp"
#include "ecs/view/view_tags.hpp"

// MinGW x64 ABI 仅保证 16 字节栈对齐, GCC 在工厂方法栈上构造 group 参数时
// 用 vmovdqa (需 32 字节对齐) 会触发 #GP; no-avx 属性强制走 SSE2 路径规避.
// Linux SysV ABI 维护 AVX 栈对齐, MSVC 自动动态对齐, 均无需此属性.
#if defined(_WIN32) && defined(__GNUC__) && !defined(__clang__)
#define LCF_NO_AVX [[gnu::target("no-avx")]]
#else
#define LCF_NO_AVX
#endif

template <typename T>
concept IsEntity = std::same_as<T, ecs::entity>;

namespace ecs
{

class manager;
class command_buffer;

struct component_meta
{
    size_t size{0};   // 组件字节大小; 掩码布局经 type_id::mask_block_of/mask_offset_of 计算
    // 类型擦除单组件克隆, 供 manager::clone 使用; nullptr 表示该类型不支持克隆
    bool (*clone_component)(manager& m, int type, entity src, entity dst) noexcept{nullptr};
};

// 系统上下文（48 bytes，内联 4 个依赖）
struct system_context
{
    uint64_t required_mask;
    uint64_t excluded_mask;
    uint32_t order;
    uint16_t phase;          // 0=pre_update, 1=update, 2=post_update, 3=render
    uint32_t dependency_count;
    uint32_t dependencies[4];
};

// 变更日志记录（16 bytes）
struct change_record
{
    uint32_t entity_index;
    uint32_t type_id;
    uint8_t  op;            // 0=add, 1=remove, 2=modify
    uint8_t  pad;
    uint16_t frame;
    uint32_t dense_index;
};

// 辅助: 判断 C 是否为任意 dense 特化
template <typename C>
struct is_dense : std::false_type {};
template <typename T>
struct is_dense<dense<T>> : std::true_type {};

template <typename C>
concept not_dense = !is_dense<std::remove_cvref_t<C>>::value;

// 连续容器 concept: 有 value_type/data()/size(), 且非 dense (避免与 dense 重载冲突)
// 花括号初始化列表 {a,b} 无法推导模板参数 C, 故 dense 花括号惯用法不受影响
template <typename C>
concept range_container = requires(const C& c) {
    typename C::value_type;
    { c.data() } -> std::same_as<const typename C::value_type*>;
    { c.size() } -> std::convertible_to<size_t>;
} && not_dense<C>;

// 地址稳定，扩容不搬移
class component_storage
{
    dense<single_class_set*> sets_;

    void release_() noexcept
    {
        for (size_t i = 0; i < sets_.size(); ++i)
        {
            delete sets_[i];
        }
        sets_.clear();
    }

public:
    component_storage() noexcept = default;

    component_storage(component_storage&& other) noexcept
        : sets_(std::move(other.sets_))
    {
    }

    component_storage& operator=(component_storage&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            release_();
            sets_ = std::move(other.sets_);
        }
        return *this;
    }

    component_storage(const component_storage&) = delete;
    component_storage& operator=(const component_storage&) = delete;

    ~component_storage()
    {
        release_();
    }

    [[nodiscard]] size_t size() const noexcept
    {
        return sets_.size();
    }

    single_class_set& emplace_back() noexcept
    {
        auto* set = new (std::nothrow) single_class_set();
        if (set == nullptr) [[unlikely]]
        {
            std::abort();
        }
        sets_.emplace_back(set);
        return *set;
    }

    [[nodiscard]] single_class_set& back() noexcept
    {
        return *sets_.back();
    }

    [[nodiscard]] single_class_set& operator[](size_t index) noexcept
    {
        return *sets_[index];
    }

    [[nodiscard]] const single_class_set& operator[](size_t index) const noexcept
    {
        return *sets_[index];
    }
};

class manager
{
private:
    component_storage components_c_;
    dense<component_meta> component_metas_;
    entity_manager entity_manager_;
    size_t default_component_capacity_{0};
    dense<system_context> system_contexts_;

    struct component_signal_event
    {
        uint32_t type;
        uint32_t entity_idx;
        uint32_t component_id;
    };
    static constexpr size_t comp_signal_buffer_size = 1024;
    signal_queue<component_signal_event, comp_signal_buffer_size> comp_signals_;
    bool track_changes_enabled_default_{true};

    static constexpr size_t change_log_capacity = 4096;
    signal_queue<change_record, change_log_capacity> change_log_;
    uint16_t current_frame_{0};

    void push_change_record(uint32_t op, uint32_t entity_idx, uint32_t type_id, uint32_t dense_index) noexcept
    {
        change_log_.push(change_record{entity_idx, type_id, static_cast<uint8_t>(op), 0, current_frame_, dense_index});
    }

    void push_comp_signal(uint32_t type, uint32_t entity_idx, uint32_t component_id) noexcept
    {
        comp_signals_.push(component_signal_event{type, entity_idx, component_id});
    }

    void ensure_type_exists(int type_id) noexcept
    {
        if (type_id >= static_cast<int>(components_c_.size())) [[unlikely]]
        {
            for (int i = static_cast<int>(components_c_.size()); i <= type_id; ++i)
            {
                components_c_.emplace_back();
                single_class_set& new_set = components_c_.back();
                new_set.track_changes_enabled_ = track_changes_enabled_default_;
                if (default_component_capacity_ > 0)
                {
                    new_set.increase_capacity(default_component_capacity_);
                }
            }
        }
        if (type_id >= static_cast<int>(component_metas_.size())) [[unlikely]]
        {
            for (int i = static_cast<int>(component_metas_.size()); i <= type_id; ++i)
            {
                component_metas_.emplace_back();
            }
        }
    }

    // 组件元数据填充 (size + 掩码块预留); 模板轨道 (sizeof(T)) 与 def 轨道 (type_def::size) 共用
    void fill_component_meta(int type_id, size_t comp_size) noexcept
    {
        if (type_id < static_cast<int>(component_metas_.size()) &&
            component_metas_[type_id].size != 0) [[likely]]
        {
            return;
        }
        ensure_type_exists(type_id);
        if (component_metas_[type_id].size == 0) [[unlikely]]
        {
            component_metas_[type_id].size = comp_size;
            const uint32_t block = type_id::mask_block_of(type_id);
            if (block >= entity_manager_.num_mask_blocks())
                entity_manager_.preallocate_mask_blocks(block + 1);
        }
    }

    template <typename T>
    void register_component_meta() noexcept
    {
        const int tid = type_id::get_type_id<T>();
        fill_component_meta(tid, sizeof(T));
        if (!component_metas_[tid].clone_component)
        {
            component_metas_[tid].clone_component =
                [](manager& m, int, entity src, entity dst) noexcept -> bool {
                    if constexpr (std::is_copy_constructible_v<T>)
                    {
                        return m.template clone_component_<T>(src, dst);
                    }
                    else
                    {
                        (void)m; (void)src; (void)dst;
                        return false;
                    }
                };
        }
    }

    // def 组件元数据 (懒注册, 首次 add_def 时触发)
    void register_def_component_meta(int def_id) noexcept
    {
        const type_def* d = type_id::get_type_def(def_id);
        if (!d) [[unlikely]] return;
        fill_component_meta(def_id, d->size);
        if (!component_metas_[def_id].clone_component)
        {
            component_metas_[def_id].clone_component =
                [](manager& m, int t, entity src, entity dst) noexcept -> bool {
                    return m.clone_def_component_(t, src, dst);
                };
        }
    }

    // 实体掩码位置位/清位: 布局经 type_id 算术计算, 直接落 multi_block_bitmask
    //   块未预留时跳过 (防御未注册类型, 清位无害, 置位由注册路径保证块已预留)
    void set_entity_mask_for_type(entity entitys, int tid) noexcept
    {
        if (entity_manager_.is_entity_alive(entitys)
            && type_id::mask_block_of(tid) < entity_manager_.num_mask_blocks()) [[likely]]
        {
            entity_manager_.set_mask_bit_no_bounds_check(entitys.parts_.index_,
                type_id::mask_block_of(tid), type_id::mask_offset_of(tid));
        }
    }

    void clear_entity_mask_for_type(entity entitys, int tid) noexcept
    {
        if (entity_manager_.is_entity_alive(entitys)
            && type_id::mask_block_of(tid) < entity_manager_.num_mask_blocks()) [[likely]]
        {
            entity_manager_.clear_mask_bit_no_bounds_check(entitys.parts_.index_,
                type_id::mask_block_of(tid), type_id::mask_offset_of(tid));
        }
    }

    // 组件变更提交: 模板轨道与 def 轨道共用

    // 掩码置位 + 变更日志 + 延迟信号
    void commit_add(entity entitys, int tid, uint32_t dense_index, const single_class_set& set) noexcept
    {
        set_entity_mask_for_type(entitys, tid);
        push_change_record(0, entitys.parts_.index_, static_cast<uint32_t>(tid), dense_index);
        if (!set.on_add_) [[unlikely]] push_comp_signal(0, entitys.parts_.index_, static_cast<uint32_t>(tid));
    }

    // 覆盖既有组件: 操作类型记 modify, 索引为实体实际 dense 位置 (掩码已置位, 不发信号)
    void commit_modify(entity entitys, int tid, uint32_t dense_index) noexcept
    {
        push_change_record(2, entitys.parts_.index_, static_cast<uint32_t>(tid), dense_index);
    }

    // 掩码清位 + 变更日志 + 延迟信号
    void commit_remove(entity entitys, int tid, const single_class_set& set) noexcept
    {
        clear_entity_mask_for_type(entitys, tid);
        push_change_record(1, entitys.parts_.index_, static_cast<uint32_t>(tid), 0);
        if (!set.on_remove_) [[unlikely]] push_comp_signal(1, entitys.parts_.index_, static_cast<uint32_t>(tid));
    }

    // 软删除仅清掩码 (组件不析构, 不触发 on_remove)
    void commit_soft_remove(entity entitys, int tid) noexcept
    {
        clear_entity_mask_for_type(entitys, tid);
    }

    template <typename T>
    bool add_component_without_message_impl(entity entitys, T&& component, operating_message& error) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        add_result added{};
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message fail;
            fail.write(false, om_err_invalid_entity, "manager::addc(): entity is not alive, index=", entitys.parts_.index_, msg::warn, msg::here());
            error = fail;
            return false;
        }
        if (!components_c_[type_id].add_impl(entitys, std::forward<T>(component), error, &added))
        {
            return false;
        }
        if (added.dense_index != single_class_set::dense_invalid)
        {
            if (added.overwrote)
            {
                commit_modify(entitys, type_id, added.dense_index);
            }
            else
            {
                commit_add(entitys, type_id, added.dense_index, components_c_[type_id]);
            }
        }
        return true;
    }

    template <typename T>
    bool clone_component_(entity src, entity dst) noexcept
    {
        using DecayedT = std::decay_t<T>;
        single_class_set* set = get_single_class_set<DecayedT>();
        if (!set) [[unlikely]] return false;
        const DecayedT* value = set->template get_ptr<DecayedT>(src);
        if (!value) [[unlikely]] return false;
        return static_cast<bool>(add(dst, DecayedT(*value)));
    }

    bool clone_def_component_(int type, entity src, entity dst) noexcept
    {
        single_class_set* set = get_single_class_set_by_id(type);
        if (!set) [[unlikely]] return false;
        const void* value = set->get_def_ptr(src);
        if (!value) [[unlikely]] return false;
        return static_cast<bool>(add_def(dst, type, value));
    }

    void remove_all_components_(entity entitys) noexcept
    {
        entity_manager_.for_each_set_bit(entitys.parts_.index_,
            [&](uint32_t block_idx, uint32_t bit_offset) {
                const int type_id = type_id::mask_type_of(block_idx, bit_offset);
                if (type_id >= static_cast<int>(components_c_.size())) [[unlikely]] return;
                single_class_set& set = components_c_[type_id];
                if (!set.on_remove_)
                {
                    push_comp_signal(1, entitys.parts_.index_, static_cast<uint32_t>(type_id));
                }
                operating_message error;
                (void)set.hard_remove_impl(entitys, error);
                clear_entity_mask_for_type(entitys, type_id);
            });
    }

public:
    manager() noexcept { change_log_.set_enabled(false); }

    bool is_entity_valid(entity entitys) const noexcept
    {
        return entity_manager_.is_entity_alive(entitys);
    }

    [[nodiscard]] size_t alive_count() const noexcept
    {
        return entity_manager_.alive_count();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return entity_manager_.alive_count() == 0;
    }

    void preallocate_entities(size_t count) noexcept
    {
        entity_manager_.preallocate_entities(count);
        if (count > default_component_capacity_) default_component_capacity_ = count;
        for (size_t i = 0; i < components_c_.size(); ++i)
        {
            components_c_[i].increase_capacity(count);
        }
    }
    void disable_comp_signals() noexcept { comp_signals_.set_enabled(false); }
    void enable_comp_signals() noexcept { comp_signals_.set_enabled(true); }
    void disable_track_changes() noexcept {
        track_changes_enabled_default_ = false;
        for (size_t i = 0; i < components_c_.size(); ++i)
            components_c_[i].track_changes_enabled_ = false;
    }
    void enable_track_changes() noexcept {
        track_changes_enabled_default_ = true;
        for (size_t i = 0; i < components_c_.size(); ++i)
            components_c_[i].track_changes_enabled_ = true;
    }
    [[nodiscard]] entity create_entity() noexcept
    {
        return entity_manager_.get_entity();
    }

    manager(manager&&) noexcept = default;
    manager(manager const&) = delete;
    manager& operator=(manager&&) noexcept = default;
    manager& operator=(manager const&) = delete;

    template <typename T>
    LCF_FORCE_INLINE bool add_impl(entity entitys, T&& component, operating_message& error) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message fail;
            fail.write(false, om_err_invalid_entity, "manager::add(): entity is not alive, index=", entitys.parts_.index_, msg::warn, msg::here());
            error = fail;
            return false;
        }
        add_result added{};
        if (!components_c_[type_id].add_impl(entitys, std::forward<T>(component), error, &added))
        {
            return false;
        }
        if (added.dense_index != single_class_set::dense_invalid)
        {
            if (added.overwrote)
            {
                commit_modify(entitys, type_id, added.dense_index);
            }
            else
            {
                commit_add(entitys, type_id, added.dense_index, components_c_[type_id]);
            }
        }
        return true;
    }

    template <typename T>
    bool add_batch_impl(std::span<const entity> entities, std::span<const T> components, operating_message& error) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        ensure_type_exists(type_id);
        for (const auto& e : entities)
        {
            if (!entity_manager_.is_entity_alive(e)) [[unlikely]]
            {
                operating_message fail;
                fail.write(false, om_err_invalid_entity, "manager::add_batch(): entity is not alive, index=", e.parts_.index_, msg::warn, msg::here());
                error = fail;
                return false;
            }
        }
        if (!components_c_[type_id].add_batch_impl(entities, components, error))
        {
            return false;
        }
        single_class_set& set = components_c_[type_id];
        for (const auto& e : entities)
        {
            commit_add(e, type_id, set.sparse_dense_at(e.parts_.index_), set);
        }
        return true;
    }

    template <typename T>
    bool add_batch_impl(const dense<entity>& entities, const dense<T>& components, operating_message& error) noexcept
    {
        return add_batch_impl<T>(std::span<const entity>(entities.data(), entities.size()),
                            std::span<const T>(components.data(), components.size()), error);
    }

    // 通用容器入参 (vector/array 等): 内部转 span 委托
    // 花括号初始化 {a,b} 不匹配此重载 (无法推导 C), 仍走 dense 路径
    template <typename T, range_container C1, range_container C2>
        requires std::same_as<typename C1::value_type, entity>
              && std::same_as<typename C2::value_type, T>
    bool add_batch_impl(const C1& entities, const C2& components, operating_message& error) noexcept
    {
        return add_batch_impl<T>(std::span<const entity>(entities.data(), entities.size()),
                            std::span<const T>(components.data(), components.size()), error);
    }

    // 裸指针 + 长度: 内部转 span 委托
    template <typename T>
    bool add_batch_impl(const entity* entities, const T* components,
                                size_t count, operating_message& error) noexcept
    {
        return add_batch_impl<T>(std::span<const entity>(entities, count),
                            std::span<const T>(components, count), error);
    }

    template <IsEntity EE, typename T>
    LCF_FORCE_INLINE bool add_impl(T&& component, EE entitys, operating_message& error) noexcept
    {
        return add(entitys, std::forward<T>(component), error);
    }

    // 正向变参: 单组件 + 多实体  addc(comp, e1, e2, ...)
    // 一个组件添加到多个实体, 第一个参数为组件, 后续为实体参数包
    // 同时覆盖 2 参 addc(comp, e) 场景
    template <typename T, IsEntity... EEs>
        requires (!IsEntity<std::decay_t<T>>)
    manager& addc(T&& component, EEs... entities) noexcept
    {
        operating_message error;
        (add_component_without_message_impl(entities, component, error), ...);
        return *this;
    }

    // 反向变参: 单实体 + 多组件  addc(e, comp1, comp2, ...)
    // 多个组件添加到同一实体, 第一个参数为实体, 后续为组件参数包
    // 同时覆盖 2 参 addc(e, comp) 场景
    template <IsEntity EE, typename... TT>
        requires (sizeof...(TT) >= 1)
    manager& addc(EE entitys, TT&&... components) noexcept
    {
        operating_message error;
        (add_component_without_message_impl(entitys, std::forward<TT>(components), error), ...);
        return *this;
    }
    template <typename T>
    [[nodiscard]] T* get_ptr(entity entitys) noexcept
    {
        if (!entitys.is_valid()) [[unlikely]] return nullptr;
        single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr(entity entitys) const noexcept
    {
        if (!entitys.is_valid()) [[unlikely]] return nullptr;
        const single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_fast(entity entitys) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast(entity entitys) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast<T>(entitys) : nullptr;
    }

    template <typename T>
    void prefetch_ptr(entity entitys) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        if (set) set->prefetch_sparse_entry(entitys.parts_.index_);
    }

    template <typename T>
    void prefetch_ptr_batch(const entity* entities, size_t count) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        if (set) set->prefetch_ptr_batch(entities, count);
    }

    // span 入参: 委托裸指针版本
    template <typename T>
    void prefetch_ptr_batch(std::span<const entity> entities) const noexcept
    {
        prefetch_ptr_batch<T>(entities.data(), entities.size());
    }

    // 通用容器入参 (vector/array 等): 委托裸指针版本
    template <typename T, range_container C>
        requires std::same_as<typename C::value_type, entity>
    void prefetch_ptr_batch(const C& entities) const noexcept
    {
        prefetch_ptr_batch<T>(entities.data(), entities.size());
    }

    template <typename T>
    void prefetch_ptr_data(entity entitys) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        if (set) set->prefetch_ptr_data<T>(entitys);
    }

    template <typename T>
    void get_ptr_batch(const entity* entities, T** results, size_t count) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (set) set->get_ptr_batch<T>(entities, results, count);
        else
        {
            for (size_t i = 0; i < count; ++i) results[i] = nullptr;
        }
    }

    // span 入参: 输入实体 span + 输出指针 span, 长度需一致
    template <typename T>
    void get_ptr_batch(std::span<const entity> entities, std::span<T*> results) noexcept
    {
        if (entities.size() != results.size()) [[unlikely]]
        {
            for (size_t i = 0; i < results.size(); ++i) results[i] = nullptr;
            return;
        }
        get_ptr_batch<T>(entities.data(), results.data(), entities.size());
    }

    // 通用容器入参 (vector/array 等): entities 只读, results 可写
    template <typename T, range_container C1, range_container C2>
        requires std::same_as<typename C1::value_type, entity>
              && std::same_as<typename C2::value_type, T*>
    void get_ptr_batch(const C1& entities, C2& results) noexcept
    {
        get_ptr_batch<T>(std::span<const entity>(entities.data(), entities.size()),
                         std::span<T*>(results.data(), results.size()));
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_fast_cached(single_class_set* set, entity e) noexcept
    {
        return set ? set->get_ptr_fast<T>(e) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast_cached(const single_class_set* set, entity e) const noexcept
    {
        return set ? set->get_ptr_fast<T>(e) : nullptr;
    }

    template <typename T>
    void prefetch_ptr_cached(single_class_set* set, entity e) const noexcept
    {
        if (set) set->prefetch_sparse_entry(e.parts_.index_);
    }

    template <typename T>
    void prefetch_ptr_cached(const single_class_set* set, entity e) const noexcept
    {
        if (set) set->prefetch_sparse_entry(e.parts_.index_);
    }

    template <typename T>
    void prefetch_ptr_data_cached(single_class_set* set, entity e) const noexcept
    {
        if (set) set->prefetch_ptr_data<T>(e);
    }

    template <typename T>
    void prefetch_ptr_data_cached(const single_class_set* set, entity e) const noexcept
    {
        if (set) set->prefetch_ptr_data<T>(e);
    }

    template <typename T>
    LCF_FORCE_INLINE bool soft_remove_impl(entity entitys, operating_message& error) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (!set)
        {
            error.write_failure(om_err_not_found.code, msg_level::warn, msg::here());
            return false;
        }
        if (!set->soft_remove_impl(entitys, error))
        {
            return false;
        }
        commit_soft_remove(entitys, type_id::get_type_id<T>());
        return true;
    }

    template <typename T>
    bool hard_remove_impl(entity entitys, operating_message& error) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (!set)
        {
            error.write_failure(om_err_not_found.code, msg_level::warn, msg::here());
            return false;
        }
        const int type_id = type_id::get_type_id<T>();
        if (!set->hard_remove_impl(entitys, error))
        {
            return false;
        }
        commit_remove(entitys, type_id, *set);
        return true;
    }

    // 变参: 多类型 × 多实体 笛卡尔积  hard_removec<T1, T2>(e1, e2, ...)
    // 每个类型 T 从每个实体上移除, 同时覆盖单类型单实体 hard_removec<T>(e) 场景
    template <typename... TT, IsEntity... EEs>
        requires (sizeof...(TT) >= 1)
    manager& hard_removec(EEs... entities) noexcept
    {
        auto remove_one = [&](auto&& e) {
            operating_message error;
            (void)std::initializer_list<int>{(hard_remove_impl<TT>(e, error), 0)...};
        };
        (remove_one(entities), ...);
        return *this;
    }

    // 变参: 多类型 × 多实体 笛卡尔积  soft_removec<T1, T2>(e1, e2, ...)
    // 同时覆盖单类型单实体 soft_removec<T>(e) 场景
    template <typename... TT, IsEntity... EEs>
        requires (sizeof...(TT) >= 1)
    manager& soft_removec(EEs... entities) noexcept
    {
        auto remove_one = [&](auto&& e) {
            operating_message error;
            (void)std::initializer_list<int>{(soft_remove_impl<TT>(e, error), 0)...};
        };
        (remove_one(entities), ...);
        return *this;
    }

    template <typename T>
    [[nodiscard]] single_class_set* get_single_class_set() noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if (type_id >= static_cast<int>(components_c_.size())) [[unlikely]]
        {
            return nullptr;
        }
        return &components_c_[type_id];
    }

    template <typename T>
    [[nodiscard]] const single_class_set* get_single_class_set() const noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if (type_id >= static_cast<int>(components_c_.size())) [[unlikely]] return nullptr;
        return &components_c_[type_id];
    }

    template <typename T>
    void preallocate_components(size_t capacity) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        ensure_type_exists(type_id);
        components_c_[type_id].increase_capacity(capacity);
    }

    template <typename T>
    [[nodiscard]] dense<T>* get_component_container() noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        return set ? set->get_typed_pool_ptr<T>() : nullptr;
    }

    // 组件类型是否已在管理器中注册
    template <typename T>
    [[nodiscard]] bool is_component_type_registered() const noexcept
    {
        using DecayedT = std::decay_t<T>;
        return type_id::get_type_id<DecayedT>() < static_cast<int>(components_c_.size());
    }

    template <typename T>
    [[nodiscard]] bool has_component(entity entitys) const noexcept
    {
        using DecayedT = std::decay_t<T>;
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            return false;
        }
        const int tid = type_id::get_type_id<DecayedT>();
        const uint64_t block = entity_manager_.get_block(
            entitys.parts_.index_, static_cast<uint32_t>(type_id::mask_block_of(tid)));
        return (block & (1ULL << type_id::mask_offset_of(tid))) != 0;
    }

    template <typename... Ts>
    [[nodiscard]] bool has_all_components(entity entitys) const noexcept
    {
        return (has_component<Ts>(entitys) && ...);
    }

    template <typename... Ts>
    [[nodiscard]] bool has_any_component(entity entitys) const noexcept
    {
        return (has_component<Ts>(entitys) || ...);
    }

    template <typename T>
    [[nodiscard]] size_t component_count() const noexcept
    {
        using DecayedT = std::decay_t<T>;
        const single_class_set* set = get_single_class_set<DecayedT>();
        return set != nullptr ? set->live_count() : 0;
    }

    [[nodiscard]] size_t component_count_by_id(int type_id_value) const noexcept
    {
        const single_class_set* set = get_single_class_set_by_id(type_id_value);
        return set != nullptr ? set->live_count() : 0;
    }

    [[nodiscard]] uint64_t get_component_mask(entity entitys) const noexcept
    {
        return entity_manager_.get_mask(entitys.parts_.index_);
    }

    [[nodiscard]] uint64_t get_component_mask_block(entity entitys, uint32_t block_idx) const noexcept
    {
        return entity_manager_.get_block(entitys.parts_.index_, block_idx);
    }

    [[nodiscard]] uint64_t get_component_mask_block_by_index(uint32_t entity_index,
                                                              uint32_t block_idx) const noexcept
    {
        return entity_manager_.get_block(entity_index, block_idx);
    }

    [[nodiscard]] const component_meta* get_component_meta(int type_id) const noexcept
    {
        if (type_id < 0 || type_id >= static_cast<int>(component_metas_.size())) [[unlikely]]
            return nullptr;
        return &component_metas_[type_id];
    }

    template <typename T>
    [[nodiscard]] uint64_t get_component_bit() const noexcept
    {
        const int tid = type_id::get_type_id<T>();
        if (tid >= static_cast<int>(component_metas_.size())
            || type_id::mask_block_of(tid) != 0) [[unlikely]]
        {
            return 0;
        }
        return 1ULL << type_id::mask_offset_of(tid);
    }

    [[nodiscard]] entity_manager& get_entity_manager() noexcept
    {
        return entity_manager_;
    }

    [[nodiscard]] const entity_manager& get_entity_manager() const noexcept
    {
        return entity_manager_;
    }

    // 预分配实体掩码块数 — 注册组件前调用避免 reshape（每块支持 64 种组件类型）
    void preallocate_mask_blocks(uint32_t num_blocks) noexcept
    {
        entity_manager_.preallocate_mask_blocks(num_blocks);
    }

    [[nodiscard]] uint32_t num_mask_blocks() const noexcept
    {
        return entity_manager_.num_mask_blocks();
    }

    [[nodiscard]] entity_state& get_entity_state_by_index(uint32_t entity_index) noexcept
    {
        return entity_manager_.get_entity_state(entity_index);
    }

    [[nodiscard]] const entity_state& get_entity_state_by_index(
        uint32_t entity_index) const noexcept
    {
        return entity_manager_.get_entity_state(entity_index);
    }

    operating_message get_entity_state(entity entitys, entity_state& out) const noexcept
    {
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
            return error;
        }
        out = entity_manager_.get_entity_state(entitys.parts_.index_);
        return {};
    }

    operating_message set_entity_flag(entity entitys, entity_flag f) noexcept
    {
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
            return error;
        }
        entity_manager_.set_entity_flag(entitys.parts_.index_, f);
        return {};
    }

    operating_message clear_entity_flag(entity entitys, entity_flag f) noexcept
    {
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
            return error;
        }
        entity_manager_.clear_entity_flag(entitys.parts_.index_, f);
        return {};
    }

    [[nodiscard]] bool has_entity_flag(entity entitys, entity_flag f) const noexcept
    {
        return entity_manager_.is_entity_alive(entitys) &&
               entity_manager_.has_entity_flag(entitys.parts_.index_, f);
    }

    void set_entity_flag_by_index(uint32_t entity_index, entity_flag f) noexcept
    {
        entity_manager_.set_entity_flag(entity_index, f);
    }

    void clear_entity_flag_by_index(uint32_t entity_index, entity_flag f) noexcept
    {
        entity_manager_.clear_entity_flag(entity_index, f);
    }

    [[nodiscard]] bool has_entity_flag_by_index(uint32_t entity_index, entity_flag f) const noexcept
    {
        return entity_manager_.has_entity_flag(entity_index, f);
    }

    operating_message set_tag(entity entitys, uint32_t tag) noexcept
    {
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
            return error;
        }
        entity_manager_.get_entity_state(entitys.parts_.index_).tag = tag;
        return {};
    }

    operating_message set_layer(entity entitys, uint32_t layer) noexcept
    {
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
            return error;
        }
        entity_manager_.get_entity_state(entitys.parts_.index_).layer = layer;
        return {};
    }

    operating_message set_group(entity entitys, uint32_t group) noexcept
    {
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
            return error;
        }
        entity_manager_.get_entity_state(entitys.parts_.index_).group_id = group;
        return {};
    }

    operating_message get_tag(entity entitys, uint32_t& out) const noexcept
    {
        entity_state state{};
        operating_message result = get_entity_state(entitys, state);
        if (!result)
        {
            return result;
        }
        out = state.tag;
        return {};
    }

    operating_message get_layer(entity entitys, uint32_t& out) const noexcept
    {
        entity_state state{};
        operating_message result = get_entity_state(entitys, state);
        if (!result)
        {
            return result;
        }
        out = state.layer;
        return {};
    }

    operating_message get_group(entity entitys, uint32_t& out) const noexcept
    {
        entity_state state{};
        operating_message result = get_entity_state(entitys, state);
        if (!result)
        {
            return result;
        }
        out = state.group_id;
        return {};
    }

    template <typename T>
    operating_message clear_component_type() noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if (type_id < static_cast<int>(components_c_.size())) [[likely]]
        {
            single_class_set& set = components_c_[type_id];
            auto& indices = set.get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                if (set.is_dense_slot_live(i))
                {
                    const uint32_t eid = indices[i];
                    clear_entity_mask_for_type(entity(eid, set.get_version_unchecked(eid)), type_id);
                }
            }
            set.clear();
        }
        return {};
    }

    operating_message delete_entity(entity entitys) noexcept
    {
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
            return error;
        }
        remove_all_components_(entitys);
        entity_manager_.destroy_entity(entitys);
        return {};
    }

    operating_message delete_entity_batch(std::span<const entity> entities,
                                          size_t& deleted_count) noexcept
    {
        deleted_count = 0;
        for (const entity e : entities)
        {
            if (entity_manager_.is_entity_alive(e))
            {
                (void)delete_entity(e);
                ++deleted_count;
            }
        }
        return {};
    }

    operating_message clear_entity(entity entitys) noexcept
    {
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
            return error;
        }
        remove_all_components_(entitys);
        return {};
    }

    // === 错误对象返回值形态 ===

    template <typename T>
    LCF_FORCE_INLINE operating_message add_component_without_message(entity entitys, T&& component) noexcept
    {
        operating_message error;
        (void)add_component_without_message_impl(entitys, std::forward<T>(component), error);
        return error;
    }

    template <typename T>
    LCF_FORCE_INLINE operating_message add(entity entitys, T&& component) noexcept
    {
        operating_message error;
        (void)add_impl(entitys, std::forward<T>(component), error);
        return error;
    }

    template <IsEntity EE, typename T>
    LCF_FORCE_INLINE operating_message add(T&& component, EE entitys) noexcept
    {
        operating_message error;
        (void)add_impl(entitys, std::forward<T>(component), error);
        return error;
    }

    template <typename T>
    LCF_FORCE_INLINE operating_message add_batch(std::span<const entity> entities,
                                std::span<const T> components) noexcept
    {
        operating_message error;
        (void)add_batch_impl(entities, components, error);
        return error;
    }

    template <typename T>
    LCF_FORCE_INLINE operating_message add_batch(const dense<entity>& entities, const dense<T>& components) noexcept
    {
        operating_message error;
        (void)add_batch_impl(entities, components, error);
        return error;
    }

    template <typename T>
    LCF_FORCE_INLINE operating_message add_batch(const entity* entities, const T* components, size_t count) noexcept
    {
        operating_message error;
        (void)add_batch_impl(entities, components, count, error);
        return error;
    }

    template <typename... Ts>
    operating_message create_with(entity& out, Ts&&... components) noexcept
    {
        out = entity{};
        entity created = create_entity();
        if (!created.is_valid()) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_capacity_exceeded.code, msg_level::error, msg::here());
            return error;
        }
        operating_message error;
        bool ok = true;
        if constexpr (sizeof...(Ts) > 0)
        {
            auto append = [&](auto&& component) {
                if (!ok)
                {
                    return;
                }
                operating_message result = add(created, std::forward<decltype(component)>(component));
                if (!result)
                {
                    ok = false;
                    error = result;
                }
            };
            (append(std::forward<Ts>(components)), ...);
        }
        if (!ok)
        {
            (void)delete_entity(created);
            return error;
        }
        out = created;
        return {};
    }

    // 克隆实体: 复制全部组件与用户状态, 失败回滚新建实体
    operating_message clone(entity source, entity& out) noexcept
    {
        out = entity{};
        if (!entity_manager_.is_entity_alive(source)) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
            return error;
        }
        entity created = create_entity();
        if (!created.is_valid()) [[unlikely]]
        {
            operating_message error;
            error.write_failure(om_err_capacity_exceeded.code, msg_level::error, msg::here());
            return error;
        }
        bool copied = true;
        entity_manager_.for_each_set_bit(source.parts_.index_,
            [&](uint32_t block_idx, uint32_t bit_offset) {
                if (!copied) return;
                const int type = type_id::mask_type_of(block_idx, bit_offset);
                if (type < 0 || type >= static_cast<int>(component_metas_.size())) [[unlikely]]
                {
                    copied = false;
                    return;
                }
                const auto clone_fn = component_metas_[type].clone_component;
                if (!clone_fn || !clone_fn(*this, type, source, created))
                {
                    copied = false;
                }
            });
        if (!copied) [[unlikely]]
        {
            (void)delete_entity(created);
            operating_message error;
            error.write_failure(om_err_unsupported_operation.code, msg_level::error, msg::here());
            return error;
        }
        // 用户状态: 跳过 pending_destroy
        const entity_state& src_state = entity_manager_.get_entity_state(source.parts_.index_);
        entity_state& dst_state = entity_manager_.get_entity_state(created.parts_.index_);
        dst_state.tag = src_state.tag;
        dst_state.layer = src_state.layer;
        dst_state.group_id = src_state.group_id;
        const uint32_t copy_mask = static_cast<uint32_t>(entity_flag::active)
                                 | static_cast<uint32_t>(entity_flag::disabled)
                                 | static_cast<uint32_t>(entity_flag::static_entity);
        dst_state.flags = (dst_state.flags & ~copy_mask) | (src_state.flags & copy_mask);
        out = created;
        return {};
    }

    template <typename T>
    LCF_FORCE_INLINE operating_message soft_remove(entity entitys) noexcept
    {
        operating_message error;
        (void)this->template soft_remove_impl<T>(entitys, error);
        return error;
    }

    template <typename T>
    LCF_FORCE_INLINE operating_message hard_remove(entity entitys) noexcept
    {
        operating_message error;
        (void)this->template hard_remove_impl<T>(entitys, error);
        return error;
    }

    template <typename T>
    operating_message hard_remove_batch(std::span<const entity> entities,
                                        size_t& removed_count) noexcept
    {
        removed_count = 0;
        if (!is_component_type_registered<T>())
        {
            return {};
        }
        for (const entity e : entities)
        {
            operating_message error;
            if (this->template hard_remove_impl<T>(e, error))
            {
                ++removed_count;
            }
        }
        return {};
    }

    template <typename T>
    operating_message soft_remove_batch(std::span<const entity> entities,
                                        size_t& removed_count) noexcept
    {
        removed_count = 0;
        if (!is_component_type_registered<T>())
        {
            return {};
        }
        for (const entity e : entities)
        {
            operating_message error;
            if (this->template soft_remove_impl<T>(e, error))
            {
                ++removed_count;
            }
        }
        return {};
    }

    template <typename T>
    bool has_component_batch(std::span<const entity> entities,
                             std::span<uint8_t> results) const noexcept
    {
        if (results.size() < entities.size()) [[unlikely]]
        {
            return false;
        }
        for (size_t i = 0; i < entities.size(); ++i)
        {
            results[i] = has_component<T>(entities[i]) ? 1u : 0u;
        }
        return true;
    }

    LCF_FORCE_INLINE operating_message add_def(entity entitys, int def_id, const void* data) noexcept
    {
        operating_message error;
        (void)add_def_impl(entitys, def_id, data, error);
        return error;
    }

    LCF_FORCE_INLINE operating_message add_def(entity entitys, std::string_view name, const void* data) noexcept
    {
        operating_message error;
        (void)add_def_impl(entitys, name, data, error);
        return error;
    }

    LCF_FORCE_INLINE operating_message hard_remove_def(entity entitys, int def_id) noexcept
    {
        operating_message error;
        (void)hard_remove_def_impl(entitys, def_id, error);
        return error;
    }

    LCF_FORCE_INLINE operating_message hard_remove_def(entity entitys, std::string_view name) noexcept
    {
        operating_message error;
        (void)hard_remove_def_impl(entitys, name, error);
        return error;
    }

    LCF_FORCE_INLINE operating_message soft_remove_def(entity entitys, int def_id) noexcept
    {
        operating_message error;
        (void)soft_remove_def_impl(entitys, def_id, error);
        return error;
    }

    LCF_FORCE_INLINE operating_message soft_remove_def(entity entitys, std::string_view name) noexcept
    {
        operating_message error;
        (void)soft_remove_def_impl(entitys, name, error);
        return error;
    }

    // 清空所有组件数据 (用于 load_mode::replace)
    // 注意: 不重置 entity_manager_ 的版本号, 旧 entity 句柄自动失效
    void clear() noexcept
    {
        // 仅销毁真实存活实体 (预分配未发放槽位与索引 0 不回收)
        entity_manager_.for_each_alive_entity([this](entity e) noexcept {
            delete_entity(e);
        });
        // 兜底: 清空所有组件容器 (确保彻底)
        for (size_t i = 0; i < components_c_.size(); ++i)
        {
            components_c_[i].clear();
        }
    }

    template <typename T, typename Compare>
    void sort_entities_by_component(Compare&& cmp) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (!set || set->size() <= 1) [[unlikely]] return;
        auto* pool = set->template get_typed_pool_ptr<T>();
        if (!pool) [[unlikely]] return;

        const size_t n = set->size();
        dense<size_t> indices;
        indices.increase_capacity(n);
        for (size_t i = 0; i < n; ++i) indices.emplace_back(i);

        T* pool_data = pool->data();
        size_t* idx_data = indices.data();

        if constexpr (std::is_same_v<std::decay_t<Compare>, std::less<T>>)
        {
            tiered_sort_indices<T>(idx_data, pool_data, n);
        }
        else
        {
            // MinGW+AVX2 下 std::sort+lambda 会崩溃, 使用 pdqsort 替代
            pdqsort<size_t>(idx_data, n, [pool_data, &cmp](size_t a, size_t b) {
                return cmp(pool_data[a], pool_data[b]);
            });
        }

        set->template reorder_dense_by_indices<T>(indices);
    }

    template <typename T, typename Other, typename Compare>
    void reorder_by_component(Compare&& cmp) noexcept
    {
        single_class_set* set_t = get_single_class_set<T>();
        single_class_set* set_other = get_single_class_set<Other>();
        if (!set_t || !set_other || set_t->size() <= 1) [[unlikely]] return;
        auto* pool_t = set_t->template get_typed_pool_ptr<T>();
        auto* pool_other = set_other->template get_typed_pool_ptr<Other>();
        if (!pool_t || !pool_other) [[unlikely]] return;

        const size_t n = set_t->size();
        dense<size_t> indices;
        indices.increase_capacity(n);
        for (size_t i = 0; i < n; ++i)
            indices.emplace_back(i);

        auto& t_indices = set_t->get_entity_indices();
        const size_t other_sparse_size = set_other->get_sparse_size();
        auto* other_pool_data = pool_other->data();
        size_t* idx_data = indices.data();
        Other default_other{};

        if constexpr (std::is_trivially_copyable_v<Other> && sizeof(Other) <= 64)
        {
            dense<Other> other_values;
            other_values.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
            {
                uint32_t eid = t_indices[i];
                uint32_t od = (eid < other_sparse_size) ? set_other->sparse_dense_at(eid) : UINT32_MAX;
                other_values.emplace_back(od != UINT32_MAX ? other_pool_data[od] : default_other);
            }
            Other* ov_data = other_values.data();
            // MinGW+AVX2 下 std::sort+lambda 会崩溃, 使用 pdqsort 替代
            pdqsort<size_t>(idx_data, n, [ov_data, &cmp](size_t a, size_t b) {
                return cmp(ov_data[a], ov_data[b]);
            });
        }
        else
        {
            // MinGW+AVX2 下 std::sort+lambda 会崩溃, 使用 pdqsort 替代
            pdqsort<size_t>(idx_data, n,
                [t_indices_ptr = t_indices.data(), set_other, other_sparse_size, other_pool_data, &default_other, &cmp](size_t a, size_t b) {
                    uint32_t eid_a = t_indices_ptr[a];
                    uint32_t eid_b = t_indices_ptr[b];
                    uint32_t od_a = (eid_a < other_sparse_size) ? set_other->sparse_dense_at(eid_a) : UINT32_MAX;
                    uint32_t od_b = (eid_b < other_sparse_size) ? set_other->sparse_dense_at(eid_b) : UINT32_MAX;
                    Other& ra = (od_a != UINT32_MAX) ? other_pool_data[od_a] : default_other;
                    Other& rb = (od_b != UINT32_MAX) ? other_pool_data[od_b] : default_other;
                    return cmp(ra, rb);
                });
        }

        set_t->template reorder_dense_by_indices<T>(indices);
    }

    template <typename T, typename Compare>
    void sort_component_container(Compare&& cmp) noexcept
    {
        sort_entities_by_component<T>(std::forward<Compare>(cmp));
    }

#include "ecs/view/single_view.hpp"
#include "ecs/view/multi_view.hpp"
#include "ecs/view/composite_views.hpp"

    template <typename T>
    [[nodiscard]] single_view<T> view() noexcept
    {
        return single_view<T>(get_single_class_set<T>(), this);
    }

    template <typename First, typename Second, typename... Rest>
    [[nodiscard]] multi_view<First, Second, Rest...> view() noexcept
    {
        return multi_view<First, Second, Rest...>(
            std::array<single_class_set*, 2 + sizeof...(Rest)>{
                get_single_class_set<First>(),
                get_single_class_set<Second>(),
                get_single_class_set<Rest>()...
            }, this);
    }

    template <typename T, typename... ExcludeTypes>
    [[nodiscard]] single_view_without<T, ExcludeTypes...> view(without_t<ExcludeTypes...>) noexcept
    {
        return single_view_without<T, ExcludeTypes...>(get_single_class_set<T>(), this);
    }

    template <typename T, typename... GetTypes>
    [[nodiscard]] single_view_with<T, GetTypes...> view(with_t<GetTypes...>) noexcept
    {
        return single_view_with<T, GetTypes...>(get_single_class_set<T>(), this);
    }

    template <typename A, typename B>
    [[nodiscard]] or_view<A, B> view_or() noexcept
    {
        return or_view<A, B>(get_single_class_set<A>(), get_single_class_set<B>());
    }

    template <typename... Types>
    [[nodiscard]] auto view_any_of() noexcept
    {
        return any_of_view<Types...>(std::array<single_class_set*, sizeof...(Types)>{
            get_single_class_set<Types>()...
        });
    }

    template <typename T, typename Pred>
    [[nodiscard]] filter_view<T, Pred> view_filtered(Pred&& pred) noexcept
    {
        return filter_view<T, Pred>(this, std::forward<Pred>(pred));
    }

    template <typename First, typename... Rest>
    [[nodiscard]] LCF_NO_AVX ecs::group<First, Rest...> group() noexcept
    {
        return ecs::group<First, Rest...>(this, std::array<single_class_set*, 1 + sizeof...(Rest)>{
            get_single_class_set<First>(),
            get_single_class_set<Rest>()...
        });
    }

    template <typename First, typename... Rest>
    [[nodiscard]] LCF_NO_AVX ecs::owning_group<First, Rest...> group(owned_t<First>) noexcept
    {
        return ecs::owning_group<First, Rest...>(this, std::array<single_class_set*, 1 + sizeof...(Rest)>{
            get_single_class_set<First>(),
            get_single_class_set<Rest>()...
        });
    }

    template <typename First, typename... Rest>
    [[nodiscard]] LCF_NO_AVX ecs::reorder_group<First, Rest...> group(reorder_t<First>) noexcept
    {
        return ecs::reorder_group<First, Rest...>(this, std::array<single_class_set*, 1 + sizeof...(Rest)>{
            get_single_class_set<First>(),
            get_single_class_set<Rest>()...
        });
    }

    [[nodiscard]] ecs::runtime_view runtime_view_create(std::span<const int> required_ids,
                                                    std::span<const int> excluded_ids = {}) noexcept
    {
        return ecs::runtime_view(this, ecs::runtime_query(this, required_ids, excluded_ids));
    }

    // 通用容器入参 (vector/array 等, 仅 required): 委托 span 版本
    template <range_container C>
        requires std::same_as<typename C::value_type, int>
    [[nodiscard]] ecs::runtime_view runtime_view_create(const C& required_ids) noexcept
    {
        return runtime_view_create(std::span<const int>(required_ids.data(), required_ids.size()));
    }

    // 通用容器入参 (vector/array 等, required + excluded): 委托 span 版本
    template <range_container C1, range_container C2>
        requires std::same_as<typename C1::value_type, int>
              && std::same_as<typename C2::value_type, int>
    [[nodiscard]] ecs::runtime_view runtime_view_create(const C1& required_ids,
                                                    const C2& excluded_ids) noexcept
    {
        return runtime_view_create(std::span<const int>(required_ids.data(), required_ids.size()),
                                   std::span<const int>(excluded_ids.data(), excluded_ids.size()));
    }

    // 裸指针 + 长度
    [[nodiscard]] ecs::runtime_view runtime_view_create(const int* required_ids, size_t req_count,
                                                    const int* excluded_ids = nullptr,
                                                    size_t exc_count = 0) noexcept
    {
        return runtime_view_create(std::span<const int>(required_ids, req_count),
                                   std::span<const int>(excluded_ids, exc_count));
    }

    // 运行时 term 查询(支持 OR/OPTIONAL/NOT 与读写标注)
    [[nodiscard]] ecs::runtime_view runtime_view_create_from_terms(
        std::span<const ecs::runtime_term> terms) noexcept
    {
        return ecs::runtime_view(this, ecs::runtime_query(this, terms));
    }

    // 通用容器入参 (vector/array 等): 委托 span 版本
    template <range_container C>
        requires std::same_as<typename C::value_type, ecs::runtime_term>
    [[nodiscard]] ecs::runtime_view runtime_view_create_from_terms(const C& terms) noexcept
    {
        return runtime_view_create_from_terms(
            std::span<const ecs::runtime_term>(terms.data(), terms.size()));
    }

    // 裸指针 + 长度
    [[nodiscard]] ecs::runtime_view runtime_view_create_from_terms(
        const ecs::runtime_term* terms, size_t count) noexcept
    {
        return runtime_view_create_from_terms(
            std::span<const ecs::runtime_term>(terms, count));
    }

    [[nodiscard]] ecs::command_buffer create_command_buffer() noexcept;

    [[nodiscard]] single_class_set* get_single_class_set_by_id(int type_id) noexcept
    {
        if (type_id < 0 || type_id >= static_cast<int>(components_c_.size())) [[unlikely]]
            return nullptr;
        return &components_c_[type_id];
    }

    [[nodiscard]] const single_class_set* get_single_class_set_by_id(int type_id) const noexcept
    {
        if (type_id < 0 || type_id >= static_cast<int>(components_c_.size())) [[unlikely]]
            return nullptr;
        return &components_c_[type_id];
    }

    // === 运行期 def 组件 (type_id::register_type_def 注册的类型) ===
    // data 长度须为注册时的 type_def::size 字节; 池按字节存储, 与模板组件互不影响

    bool add_def_impl(entity entitys, int def_id, const void* data, operating_message& error) noexcept
    {
        operating_message result;
        const type_def* d = type_id::get_type_def(def_id);
        if (!d) [[unlikely]]
        {
            result.write(false, om_err_not_found, "manager::add_def(): unknown def id ", def_id, msg::warn, msg::here());
            if (!result)
            {
                error = result;
                return false;
            }
            return true;
        }
        if (!entity_manager_.is_entity_alive(entitys)) [[unlikely]]
        {
            result.write(false, om_err_invalid_entity, "manager::add_def(): entity is not alive, index=", entitys.parts_.index_, msg::warn, msg::here());
            if (!result)
            {
                error = result;
                return false;
            }
            return true;
        }
        register_def_component_meta(def_id);
        single_class_set& set = components_c_[def_id];
        const bool had_component = set.sparse_version_at(entitys.parts_.index_) == entitys.parts_.version_;
        if (!set.add_def_impl(entitys, def_id, *d, data, error))
        {
            return false;
        }
        const uint32_t dense_idx = set.sparse_dense_at(entitys.parts_.index_);
        if (dense_idx != single_class_set::dense_invalid)
        {
            if (had_component)
            {
                commit_modify(entitys, def_id, dense_idx);
            }
            else
            {
                commit_add(entitys, def_id, dense_idx, set);
            }
        }
        return true;
    }

    bool add_def_impl(entity entitys, std::string_view name, const void* data, operating_message& error) noexcept
    {
        const int id = type_id::get_def_type_id(name);
        if (id < 0) [[unlikely]]
        {
            operating_message result;
            result.write(false, om_err_not_found, "manager::add_def(): unregistered def name", msg::warn, msg::here());
            if (!result)
            {
                error = result;
                return false;
            }
            return true;
        }
        return add_def_impl(entitys, id, data, error);
    }

    [[nodiscard]] void* get_def_ptr(entity entitys, int def_id) noexcept
    {
        if (!entitys.is_valid()) [[unlikely]] return nullptr;
        single_class_set* set = get_single_class_set_by_id(def_id);
        return set ? set->get_def_ptr(entitys) : nullptr;
    }

    [[nodiscard]] const void* get_def_ptr(entity entitys, int def_id) const noexcept
    {
        if (!entitys.is_valid()) [[unlikely]] return nullptr;
        const single_class_set* set = get_single_class_set_by_id(def_id);
        return set ? set->get_def_ptr(entitys) : nullptr;
    }

    [[nodiscard]] void* get_def_ptr(entity entitys, std::string_view name) noexcept
    {
        const int id = type_id::get_def_type_id(name);
        return id < 0 ? nullptr : get_def_ptr(entitys, id);
    }

    [[nodiscard]] const void* get_def_ptr(entity entitys, std::string_view name) const noexcept
    {
        const int id = type_id::get_def_type_id(name);
        return id < 0 ? nullptr : get_def_ptr(entitys, id);
    }

    bool hard_remove_def_impl(entity entitys, int def_id, operating_message& error) noexcept
    {
        single_class_set* set = get_single_class_set_by_id(def_id);
        if (!set || set->get_type_id_value() != def_id)
        {
            error.write_failure(om_err_not_found.code, msg_level::warn, msg::here());
            return false;
        }
        if (!set->hard_remove_impl(entitys, error))
        {
            return false;
        }
        commit_remove(entitys, def_id, *set);
        return true;
    }

    bool hard_remove_def_impl(entity entitys, std::string_view name, operating_message& error) noexcept
    {
        const int id = type_id::get_def_type_id(name);
        if (id < 0) [[unlikely]]
        {
            operating_message result;
            result.write(false, om_err_not_found, "manager::hard_remove_def(): unregistered def name", msg::warn, msg::here());
            if (!result)
            {
                error = result;
                return false;
            }
            return true;
        }
        return hard_remove_def_impl(entitys, id, error);
    }

    bool soft_remove_def_impl(entity entitys, int def_id, operating_message& error) noexcept
    {
        single_class_set* set = get_single_class_set_by_id(def_id);
        if (!set || set->get_type_id_value() != def_id)
        {
            error.write_failure(om_err_not_found.code, msg_level::warn, msg::here());
            return false;
        }
        if (!set->soft_remove_impl(entitys, error))
        {
            return false;
        }
        commit_soft_remove(entitys, def_id);
        return true;
    }

    bool soft_remove_def_impl(entity entitys, std::string_view name, operating_message& error) noexcept
    {
        const int id = type_id::get_def_type_id(name);
        if (id < 0) [[unlikely]]
        {
            operating_message result;
            result.write(false, om_err_not_found, "manager::soft_remove_def(): unregistered def name", msg::warn, msg::here());
            if (!result)
            {
                error = result;
                return false;
            }
            return true;
        }
        return soft_remove_def_impl(entitys, id, error);
    }

    void set_on_entity_created(void (*fn)(entity, void*) noexcept, void* user_data = nullptr) noexcept
    {
        entity_manager_.on_entity_created_ = fn;
        entity_manager_.on_entity_created_data_ = user_data;
    }
    void set_on_entity_destroyed(void (*fn)(entity, void*) noexcept, void* user_data = nullptr) noexcept
    {
        entity_manager_.on_entity_destroyed_ = fn;
        entity_manager_.on_entity_destroyed_data_ = user_data;
    }

    template <typename T>
    void set_on_add(void (*fn)(entity, void*, void*) noexcept, void* user_data = nullptr) noexcept
    {
        ensure_type_exists(type_id::get_type_id<T>());
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            set->on_add_ = fn;
            set->on_add_data_ = user_data;
        }
    }
    template <typename T>
    void set_on_remove(void (*fn)(entity, void*, void*) noexcept, void* user_data = nullptr) noexcept
    {
        ensure_type_exists(type_id::get_type_id<T>());
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            set->on_remove_ = fn;
            set->on_remove_data_ = user_data;
        }
    }
    template <typename T>
    void set_on_modify(void (*fn)(entity, void*, void*) noexcept, void* user_data = nullptr) noexcept
    {
        ensure_type_exists(type_id::get_type_id<T>());
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            set->on_modify_ = fn;
            set->on_modify_data_ = user_data;
        }
    }

    void enable_entity_signals() noexcept { entity_manager_.enable_entity_signals(); }
    void disable_entity_signals() noexcept { entity_manager_.disable_entity_signals(); }

    template <typename Func>
    void flush_entity_signals(Func&& handler) noexcept
    {
        entity_manager_.flush_signals(std::forward<Func>(handler));
    }
    [[nodiscard]] bool has_pending_entity_signals() const noexcept
    {
        return entity_manager_.has_pending_signals();
    }

    template <typename Func>
    void flush_component_signals(Func&& handler) noexcept
    {
        comp_signals_.flush([&](const component_signal_event& ev) noexcept {
            handler(ev.type, ev.entity_idx, ev.component_id);
        });
    }
    [[nodiscard]] bool has_pending_component_signals() const noexcept
    {
        return comp_signals_.has_pending();
    }

    // 变更日志池 — 帧末消费
    void enable_change_log() noexcept { change_log_.set_enabled(true); }
    void disable_change_log() noexcept { change_log_.set_enabled(false); }

    template <typename Func>
    void flush_change_log(Func&& handler) noexcept
    {
        change_log_.flush(handler);
    }

    void end_frame() noexcept
    {
        ++current_frame_;
    }

    [[nodiscard]] bool has_pending_change_records() const noexcept
    {
        return change_log_.has_pending();
    }

    // 系统上下文池 — 注册与调度
    void register_system(const system_context& ctx) noexcept
    {
        system_contexts_.emplace_back(ctx);
    }

    [[nodiscard]] const dense<system_context>& get_system_contexts() const noexcept
    {
        return system_contexts_;
    }

    ~manager() = default;
};

template <typename T>
class query_context
{
    single_class_set* set_;
    mutable T* pool_data_;
    mutable uint32_t position_epoch_;

    // epoch 变化 (位置变动/池重分配) 后刷新缓存指针, 防止扩容悬垂
    void refresh_if_stale_() const noexcept
    {
        if (position_epoch_ != set_->position_epoch_) [[unlikely]]
        {
            dense<T>* pool = set_->get_typed_pool_ptr<T>();
            pool_data_ = pool ? pool->data() : nullptr;
            position_epoch_ = set_->position_epoch_;
        }
    }

public:
    query_context(manager& mgr) noexcept
        : set_(mgr.get_single_class_set<T>())
        , pool_data_(nullptr)
        , position_epoch_(0)
    {
        if (set_)
        {
            dense<T>* pool = set_->get_typed_pool_ptr<T>();
            if (pool)
            {
                pool_data_ = pool->data();
                position_epoch_ = set_->position_epoch_;
            }
        }
    }

    [[nodiscard]] T* get_ptr(entity e) noexcept
    {
        if (!set_ || e.parts_.index_ >= set_->sparse_size_) [[unlikely]]
            return nullptr;
        refresh_if_stale_();
        // 查找算法委托 single_class_set 统一核心 (epoch 传刷新后的缓存值)
        const uint32_t d = set_->hot_lookup_fill_(e, position_epoch_);
        return d == single_class_set::dense_invalid ? nullptr : &pool_data_[d];
    }

    [[nodiscard]] const T* get_ptr(entity e) const noexcept
    {
        if (!set_ || e.parts_.index_ >= set_->sparse_size_) [[unlikely]]
            return nullptr;
        refresh_if_stale_();
        const uint32_t d = set_->hot_lookup_(e, position_epoch_);
        return d == single_class_set::dense_invalid ? nullptr : &pool_data_[d];
    }

    void prefetch_sparse(entity e) const noexcept
    {
        if (!set_ || e.parts_.index_ >= set_->sparse_size_) [[unlikely]]
            return;
        if (e.parts_.index_ < set_->sparse_cap_)
            LCF_PREFETCH_R(&set_->sparse_[e.parts_.index_]);
    }

    void prefetch_data(entity e) const noexcept
    {
        if (!set_ || e.parts_.index_ >= set_->sparse_size_) [[unlikely]]
            return;
        refresh_if_stale_();
        // 单次 sparse_entry 加载即可判定有效 dense 位置
        const sparse_entry* se = set_->sparse_entry_checked_(e.parts_.index_);
        if (se && se->version == e.parts_.version_ && se->dense != single_class_set::dense_invalid)
        {
            LCF_PREFETCH_R(&pool_data_[se->dense]);
        }
    }

    [[nodiscard]] bool valid() const noexcept { return set_ != nullptr; }
};

} // namespace ecs

#include "ecs/group/group_impl.hpp"
#include "ecs/view/runtime_view_impl.hpp"
#include "ecs/command/command_buffer.hpp"

namespace ecs
{
// out-of-line:依赖 command_buffer 完整定义
inline command_buffer manager::create_command_buffer() noexcept
{
    return command_buffer(this);
}
} // namespace ecs
