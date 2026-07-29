#pragma once
#include <concepts>
#include <tuple>
#include <array>
#include <vector>
#include <limits>
#include <type_traits>
#include "single_class_set.hpp"
#include "part/type_id.hpp"
#include "entity_manager.hpp"
#include "group.hpp"
#include "reorder.hpp"
#include "runtime_view.hpp"
#include "part/radix_sort_helper.hpp"
#include "part/tiered_sort.hpp"
#include "part/ring_buffer.hpp"
#include "view_tags.hpp"

// MinGW GCC: 返回大对象 (group/owning_group/reorder_group) 的工厂方法中,
// GCC 会用 vmovdqa (32 字节对齐的 256 位存储) 在栈上构造 std::array<...> 参数,
// 但 MinGW x64 ABI 遵循 Windows x64 ABI 规范, 仅保证 16 字节栈对齐 (设计规范, 非 bug)
// → 触发 #GP, 进程崩溃 (0xC0000005).
// 解决: 在这些工厂方法上加 [[gnu::target("no-avx")]] 禁用 AVX, 改用 SSE2/scalar
//       (仅需 16 字节对齐, MinGW ABI 可保证). 工厂方法非热路径, 性能无影响.
// Linux/macOS SysV ABI 为 AVX 函数维护 32 字节栈对齐 (设计规范), 无需此属性.
// MSVC /arch:AVX2 会自动插入动态栈对齐, 亦无需此属性.
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
    size_t   size{0};
    uint32_t mask_block{0};   // 掩码块索引 (type_id-1)/64
    uint32_t mask_offset{0};  // 块内位偏移 (type_id-1)%64
};

// 系统上下文（48 bytes，内联 4 个依赖）
struct system_context
{
    uint64_t required_mask;
    uint64_t excluded_mask;
    uint32_t order;
    uint16_t phase;          // 0=pre_update, 1=update, 2=post_update, 3=render
    uint16_t parallel_group; // 0=sequential, 1+=parallel group
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

class manager
{
private:
    dense<single_class_set> components_c_;
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
    static_assert((comp_signal_buffer_size & (comp_signal_buffer_size - 1)) == 0,
                  "comp_signal_buffer_size must be power of 2");
    ring_buffer<component_signal_event, comp_signal_buffer_size> comp_signal_buf_;
    dense<component_signal_event> comp_signal_overflow_chain_;
    size_t comp_signal_overflow_read_{0};
    uint64_t comp_signal_overflow_count_{0};
    bool comp_signal_enabled_{true};
    bool comp_signal_flushing_{false};
    bool track_changes_enabled_default_{true};

    static constexpr size_t change_log_capacity = 4096;
    static_assert((change_log_capacity & (change_log_capacity - 1)) == 0,
                  "change_log_capacity must be power of 2");
    ring_buffer<change_record, change_log_capacity> change_log_;
    dense<change_record> change_log_overflow_;
    size_t change_log_overflow_read_{0};
    uint16_t current_frame_{0};
    bool change_log_enabled_{true};

    void push_change_record(uint32_t op, uint32_t entity_idx, uint32_t type_id, uint32_t dense_index) noexcept
    {
        if (!change_log_enabled_) [[unlikely]] return;
        if (!change_log_.push(change_record{entity_idx, type_id, static_cast<uint8_t>(op), 0, current_frame_, dense_index})) [[unlikely]]
        {
            change_log_overflow_.push_back(change_record{entity_idx, type_id, static_cast<uint8_t>(op), 0, current_frame_, dense_index});
        }
    }

    void push_comp_signal(uint32_t type, uint32_t entity_idx, uint32_t component_id) noexcept
    {
        if (!comp_signal_enabled_) [[unlikely]] return;
        if (!comp_signal_buf_.push(component_signal_event{type, entity_idx, component_id})) [[unlikely]]
        {
            ++comp_signal_overflow_count_;
            comp_signal_overflow_chain_.push_back(component_signal_event{type, entity_idx, component_id});
        }
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

    template <typename T>
    void register_component_meta() noexcept
    {
        int type_id = type_id::get_type_id<T>();
        if (type_id < static_cast<int>(component_metas_.size()) &&
            component_metas_[type_id].size != 0) [[likely]]
        {
            return;
        }
        ensure_type_exists(type_id);
        if (component_metas_[type_id].size == 0) [[unlikely]]
        {
            component_metas_[type_id].size = sizeof(T);
            component_metas_[type_id].mask_block = static_cast<uint32_t>(type_id - 1) / 64;
            component_metas_[type_id].mask_offset = static_cast<uint32_t>(type_id - 1) % 64;
            if (component_metas_[type_id].mask_block >= entity_manager_.num_mask_blocks())
                entity_manager_.reserve_mask_blocks(component_metas_[type_id].mask_block + 1);
        }
    }

    void set_entity_mask_bit(entity entitys, uint32_t block_idx, uint32_t bit_offset) noexcept
    {
        if (entitys.is_valid()) [[likely]]
        {
            entity_manager_.set_mask_bit_no_bounds_check(entitys.parts_.index_, block_idx, bit_offset);
        }
    }

    void clear_entity_mask_bit(entity entitys, uint32_t block_idx, uint32_t bit_offset) noexcept
    {
        if (entitys.is_valid()) [[likely]]
        {
            entity_manager_.clear_mask_bit_no_bounds_check(entitys.parts_.index_, block_idx, bit_offset);
        }
    }

    template <typename T>
    void add_component_without_message(entity entitys, T&& component) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        components_c_[type_id].add(entitys, std::forward<T>(component));
        set_entity_mask_bit(entitys, component_metas_[type_id].mask_block, component_metas_[type_id].mask_offset);
        push_change_record(0, entitys.parts_.index_, static_cast<uint32_t>(type_id), static_cast<uint32_t>(components_c_[type_id].size() - 1));
        if (!components_c_[type_id].on_add_) push_comp_signal(0, entitys.parts_.index_, static_cast<uint32_t>(type_id));
    }

public:
    manager() noexcept = default;

    bool is_entity_valid(entity entitys) const noexcept
    {
        return entity_manager_.is_version_valid(entitys);
    }

    void append_preallocated_entities(size_t count) noexcept
    {
        entity_manager_.append_preallocated_entities(count);
        if (count > default_component_capacity_) default_component_capacity_ = count;
        for (size_t i = 0; i < components_c_.size(); ++i)
        {
            components_c_[i].increase_capacity(count);
        }
    }
    void disable_comp_signals() noexcept { comp_signal_enabled_ = false; }
    void enable_comp_signals() noexcept { comp_signal_enabled_ = true; }
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
    operating_message add(entity entitys, T&& component) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        operating_message result = components_c_[type_id].add(entitys, std::forward<T>(component));
        set_entity_mask_bit(entitys, component_metas_[type_id].mask_block, component_metas_[type_id].mask_offset);
        if (!components_c_[type_id].on_add_) push_comp_signal(0, entitys.parts_.index_, static_cast<uint32_t>(type_id));
        return result;
    }

    template <typename T>
    operating_message add_batch(std::span<const entity> entities, std::span<const T> components) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        register_component_meta<DecayedT>();
        ensure_type_exists(type_id);
        operating_message result = components_c_[type_id].add_batch(entities, components);
        uint32_t block = component_metas_[type_id].mask_block;
        uint32_t offset = component_metas_[type_id].mask_offset;
        for (const auto& e : entities)
        {
            set_entity_mask_bit(e, block, offset);
        }
        return result;
    }

    template <typename T>
    operating_message add_batch(const dense<entity>& entities, const dense<T>& components) noexcept
    {
        return add_batch<T>(std::span<const entity>(entities.data(), entities.size()),
                            std::span<const T>(components.data(), components.size()));
    }

    // 通用容器入参 (vector/array 等): 内部转 span 委托
    // 花括号初始化 {a,b} 不匹配此重载 (无法推导 C), 仍走 dense 路径
    template <typename T, range_container C1, range_container C2>
        requires std::same_as<typename C1::value_type, entity>
              && std::same_as<typename C2::value_type, T>
    operating_message add_batch(const C1& entities, const C2& components) noexcept
    {
        return add_batch<T>(std::span<const entity>(entities.data(), entities.size()),
                            std::span<const T>(components.data(), components.size()));
    }

    // 裸指针 + 长度: 内部转 span 委托
    template <typename T>
    operating_message add_batch(const entity* entities, const T* components,
                                size_t count) noexcept
    {
        return add_batch<T>(std::span<const entity>(entities, count),
                            std::span<const T>(components, count));
    }

    template <IsEntity EE, typename T>
    operating_message add(T&& component, EE entitys) noexcept
    {
        return add(entitys, std::forward<T>(component));
    }
    // 正向变参: 单组件 + 多实体  addc(comp, e1, e2, ...)
    // 一个组件添加到多个实体, 第一个参数为组件, 后续为实体参数包
    // 同时覆盖 2 参 addc(comp, e) 场景
    template <typename T, IsEntity... EEs>
        requires (!IsEntity<std::decay_t<T>>)
    manager& addc(T&& component, EEs... entities) noexcept
    {
        (add_component_without_message(entities, component), ...);
        return *this;
    }

    // 反向变参: 单实体 + 多组件  addc(e, comp1, comp2, ...)
    // 多个组件添加到同一实体, 第一个参数为实体, 后续为组件参数包
    // 同时覆盖 2 参 addc(e, comp) 场景
    template <IsEntity EE, typename... TT>
        requires (sizeof...(TT) >= 1)
    manager& addc(EE entitys, TT&&... components) noexcept
    {
        (add_component_without_message(entitys, std::forward<TT>(components)), ...);
        return *this;
    }
    template <typename T>
    [[nodiscard]] T* get_ptr(entity entitys) noexcept
    {
        if (!entitys.is_valid()) [[unlikely]] return nullptr;
        single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast_inline<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr(entity entitys) const noexcept
    {
        if (!entitys.is_valid()) [[unlikely]] return nullptr;
        const single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast_inline<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] T* get_ptr_fast(entity entitys) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast_inline<T>(entitys) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* get_ptr_fast(entity entitys) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        return set ? set->get_ptr_fast_inline<T>(entitys) : nullptr;
    }

    template <typename T>
    void prefetch_ptr(entity entitys) const noexcept
    {
        const single_class_set* set = get_single_class_set<T>();
        if (set) set->prefetch_ptr(entitys);
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
        if (set) set->prefetch_ptr(e);
    }

    template <typename T>
    void prefetch_ptr_cached(const single_class_set* set, entity e) const noexcept
    {
        if (set) set->prefetch_ptr(e);
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
    operating_message soft_remove(entity entitys) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            int type_id = type_id::get_type_id<T>();
            if (type_id < static_cast<int>(component_metas_.size()))
            {
                clear_entity_mask_bit(entitys, component_metas_[type_id].mask_block, component_metas_[type_id].mask_offset);
            }
            // soft_remove 仅逻辑隐藏,组件未析构,不触发 on_remove_ 也不入队
            return set->soft_remove(entitys);
        }
        operating_message result;
        result.write_message(false, "manager::soft_remove(): component set does not exist, type=", std::to_string(type_id::get_type_id<T>()));
        return result;
    }

    template <typename T>
    operating_message hard_remove(entity entitys) noexcept
    {
        single_class_set* set = get_single_class_set<T>();
        if (set)
        {
            int type_id = type_id::get_type_id<T>();
            if (type_id < static_cast<int>(component_metas_.size()))
            {
                clear_entity_mask_bit(entitys, component_metas_[type_id].mask_block, component_metas_[type_id].mask_offset);
            }
            push_change_record(1, entitys.parts_.index_, static_cast<uint32_t>(type_id), 0);
            // 即时回调与延迟队列互斥:注册了 on_remove_ 则同步触发,否则入队
            if (!set->on_remove_) push_comp_signal(1, entitys.parts_.index_, static_cast<uint32_t>(type_id));
            return set->hard_remove(entitys);
        }
        operating_message result;
        result.write_message(false, "manager::hard_remove(): component set does not exist, type=", std::to_string(type_id::get_type_id<T>()));
        return result;
    }

    // 变参: 多类型 × 多实体 笛卡尔积  hard_removec<T1, T2>(e1, e2, ...)
    // 每个类型 T 从每个实体上移除, 同时覆盖单类型单实体 hard_removec<T>(e) 场景
    template <typename... TT, IsEntity... EEs>
        requires (sizeof...(TT) >= 1)
    manager& hard_removec(EEs... entities) noexcept
    {
        auto remove_one = [&](auto&& e) {
            (hard_remove<TT>(e), ...);
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
            (soft_remove<TT>(e), ...);
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
    void reserve_component_capacity(size_t capacity) noexcept
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

    [[nodiscard]] uint64_t get_entity_mask(entity entitys) const noexcept
    {
        return entity_manager_.get_mask(entitys.parts_.index_);
    }

    [[nodiscard]] uint64_t get_entity_block(entity entitys, uint32_t block_idx) const noexcept
    {
        return entity_manager_.get_block(entitys.parts_.index_, block_idx);
    }

    [[nodiscard]] uint64_t get_entity_block_by_idx(uint32_t entity_index, uint32_t block_idx) const noexcept
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
        int type_id = type_id::get_type_id<T>();
        if (type_id >= static_cast<int>(component_metas_.size())) [[unlikely]]
            return 0;
        const auto& meta = component_metas_[type_id];
        if (meta.mask_block == 0) [[likely]]
            return 1ULL << meta.mask_offset;
        return 0;
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
    void reserve_mask_blocks(uint32_t num_blocks) noexcept
    {
        entity_manager_.reserve_mask_blocks(num_blocks);
    }

    [[nodiscard]] uint32_t num_mask_blocks() const noexcept
    {
        return entity_manager_.num_mask_blocks();
    }

    [[nodiscard]] entity_state& get_entity_state(uint32_t entity_index) noexcept
    {
        return entity_manager_.get_entity_state(entity_index);
    }

    [[nodiscard]] const entity_state& get_entity_state(uint32_t entity_index) const noexcept
    {
        return entity_manager_.get_entity_state(entity_index);
    }

    void set_entity_flag(uint32_t entity_index, entity_flag f) noexcept
    {
        entity_manager_.set_entity_flag(entity_index, f);
    }

    void clear_entity_flag(uint32_t entity_index, entity_flag f) noexcept
    {
        entity_manager_.clear_entity_flag(entity_index, f);
    }

    [[nodiscard]] bool has_entity_flag(uint32_t entity_index, entity_flag f) const noexcept
    {
        return entity_manager_.has_entity_flag(entity_index, f);
    }

    template <typename T>
    void delete_type_container() noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if (type_id < static_cast<int>(components_c_.size())) [[likely]]
            components_c_[type_id].clear();
    }

    void delete_entity(entity& entitys) noexcept
    {
        if (!entitys.is_valid()) [[unlikely]] return;
        // 仅遍历实体实际拥有的组件类型 (O(实体组件数) 而非 O(类型总数))
        // 迭代副本 block, hard_remove 修改原掩码不影响循环
        entity_manager_.for_each_set_bit(entitys.parts_.index_, [&](uint32_t block_idx, uint32_t bit_offset) {
            size_t type_id = static_cast<size_t>(block_idx) * 64 + bit_offset + 1;
            if (type_id >= components_c_.size()) [[unlikely]] return;
            single_class_set& set = components_c_[type_id];
            if (!set.on_remove_) push_comp_signal(1, entitys.parts_.index_, static_cast<uint32_t>(type_id));
            set.hard_remove(entitys);
        });
        entity_manager_.destroy_entity(entitys);
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
        for (size_t i = 0; i < n; ++i) indices.push_back(i);

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
            indices.push_back(i);

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
                uint32_t od = (eid < other_sparse_size) ? set_other->sparse_dense_at_public(eid) : UINT32_MAX;
                other_values.push_back(od != UINT32_MAX ? other_pool_data[od] : default_other);
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
                    uint32_t od_a = (eid_a < other_sparse_size) ? set_other->sparse_dense_at_public(eid_a) : UINT32_MAX;
                    uint32_t od_b = (eid_b < other_sparse_size) ? set_other->sparse_dense_at_public(eid_b) : UINT32_MAX;
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

#include "single_view.inc.hpp"
#include "multi_view.inc.hpp"
#include "composite_views.inc.hpp"

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

    [[nodiscard]] uint64_t entity_signal_overflow_count() const noexcept
    {
        return entity_manager_.signal_overflow_count();
    }
    [[nodiscard]] uint64_t comp_signal_overflow_count() const noexcept
    {
        return comp_signal_overflow_count_;
    }
    void reset_entity_signal_overflow_count() noexcept { entity_manager_.reset_signal_overflow_count(); }
    void reset_comp_signal_overflow_count() noexcept { comp_signal_overflow_count_ = 0; }

    void reserve_entity_signal_capacity(size_t n) noexcept { entity_manager_.reserve_signal_capacity(n); }
    void reserve_comp_signal_capacity(size_t n) noexcept { comp_signal_overflow_chain_.increase_capacity(n); }

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
        // 防 flush 递归重入
        if (comp_signal_flushing_) [[unlikely]] return;
        comp_signal_flushing_ = true;
        // 循环上限防止 handler 内追加导致无限循环
        uint64_t budget = comp_signal_buffer_size * 4 + comp_signal_overflow_chain_.size();
        size_t processed = comp_signal_buf_.drain_with_budget(
            static_cast<size_t>(budget),
            [&](const component_signal_event& ev) noexcept { handler(ev.type, ev.entity_idx, ev.component_id); });
        budget -= processed;
        while (budget > 0 && comp_signal_overflow_read_ < comp_signal_overflow_chain_.size())
        {
            auto& ev = comp_signal_overflow_chain_[comp_signal_overflow_read_];
            handler(ev.type, ev.entity_idx, ev.component_id);
            ++comp_signal_overflow_read_;
            --budget;
        }
        if (comp_signal_overflow_read_ == comp_signal_overflow_chain_.size() && comp_signal_overflow_chain_.size() > 0)
        {
            comp_signal_overflow_chain_.clear();
            comp_signal_overflow_read_ = 0;
        }
        comp_signal_flushing_ = false;
    }
    [[nodiscard]] bool has_pending_component_signals() const noexcept
    {
        return comp_signal_buf_.has_pending() || comp_signal_overflow_read_ < comp_signal_overflow_chain_.size();
    }

    // 变更日志池 — 帧末消费
    void enable_change_log() noexcept { change_log_enabled_ = true; }
    void disable_change_log() noexcept { change_log_enabled_ = false; }

    template <typename Func>
    void flush_change_log(Func&& handler) noexcept
    {
        uint64_t budget = change_log_capacity * 4 + change_log_overflow_.size();
        size_t processed = change_log_.drain_with_budget(
            static_cast<size_t>(budget),
            [&](const change_record& r) noexcept { handler(r); });
        budget -= processed;
        while (budget > 0 && change_log_overflow_read_ < change_log_overflow_.size())
        {
            handler(change_log_overflow_[change_log_overflow_read_]);
            ++change_log_overflow_read_;
            --budget;
        }
        if (change_log_overflow_read_ == change_log_overflow_.size() && change_log_overflow_.size() > 0)
        {
            change_log_overflow_.clear();
            change_log_overflow_read_ = 0;
        }
    }

    void end_frame() noexcept
    {
        ++current_frame_;
    }

    [[nodiscard]] bool has_pending_change_records() const noexcept
    {
        return change_log_.has_pending() || change_log_overflow_read_ < change_log_overflow_.size();
    }

    // 系统上下文池 — 注册与调度
    void register_system(const system_context& ctx) noexcept
    {
        system_contexts_.push_back(ctx);
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
    size_t sparse_size_;
    T* pool_data_;
    uint64_t pool_version_;

public:
    query_context(manager& mgr) noexcept
        : set_(mgr.get_single_class_set<T>())
        , sparse_size_(set_ ? set_->sparse_size_ : 0)
        , pool_data_(set_ ? set_->get_typed_pool<T>()->data() : nullptr)
        , pool_version_(set_ ? set_->get_pool_version() : 0)
    {}

    [[nodiscard]] T* get_ptr(entity e) noexcept
    {
        if (!set_ || e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const size_t slot = e.parts_.index_ & (single_class_set::hot_set_capacity_ - 1);
        const auto& entry = set_->hot_set_[slot];
        const uint64_t key = single_class_set::make_entity_key_(e);
        const uint32_t pool_ver_lo = static_cast<uint32_t>(pool_version_);
        if (entry.entity_key == key && entry.pool_version_lo == pool_ver_lo) [[likely]]
        {
            return &pool_data_[entry.dense_index];
        }
        const sparse_entry* se = set_->sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == single_class_set::dense_invalid || se->version != e.parts_.version_) [[unlikely]]
            return nullptr;
        set_->hot_set_[slot] = {key, se->dense, pool_ver_lo};
        return &pool_data_[se->dense];
    }

    [[nodiscard]] const T* get_ptr(entity e) const noexcept
    {
        if (!set_ || e.parts_.index_ >= sparse_size_) [[unlikely]]
            return nullptr;
        const size_t slot = e.parts_.index_ & (single_class_set::hot_set_capacity_ - 1);
        const auto& entry = set_->hot_set_[slot];
        const uint64_t key = single_class_set::make_entity_key_(e);
        const uint32_t pool_ver_lo = static_cast<uint32_t>(pool_version_);
        if (entry.entity_key == key && entry.pool_version_lo == pool_ver_lo) [[likely]]
        {
            return &pool_data_[entry.dense_index];
        }
        const sparse_entry* se = set_->sparse_entry_checked_(e.parts_.index_);
        if (!se || se->dense == single_class_set::dense_invalid || se->version != e.parts_.version_) [[unlikely]]
            return nullptr;
        return &pool_data_[se->dense];
    }

    void prefetch_sparse(entity e) const noexcept
    {
        if (!set_ || e.parts_.index_ >= sparse_size_) [[unlikely]]
            return;
        if (e.parts_.index_ < set_->sparse_table_.capacity())
            PREFETCH_R(&set_->sparse_table_[e.parts_.index_]);
    }

    void prefetch_data(entity e) const noexcept
    {
        if (!set_ || e.parts_.index_ >= sparse_size_) [[unlikely]]
            return;
        // 单次 is_constructed_at + 单次 sparse_entry 加载
        const sparse_entry* se = set_->sparse_entry_checked_(e.parts_.index_);
        if (se && se->version == e.parts_.version_ && se->dense != single_class_set::dense_invalid)
        {
            PREFETCH_R(&pool_data_[se->dense]);
        }
    }

    [[nodiscard]] bool valid() const noexcept { return set_ != nullptr; }
};

} // namespace ecs

#include "group_impl.hpp"
#include "runtime_view_impl.hpp"
#include "command_buffer.hpp"

namespace ecs
{
// out-of-line:依赖 command_buffer 完整定义
inline command_buffer manager::create_command_buffer() noexcept
{
    return command_buffer(this);
}
} // namespace ecs
