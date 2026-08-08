// type_name.hpp - 稳定类型名注册 + 实体引用字段注册 + 枚举注册
#pragma once

#include "../part/type_id.hpp"
#include "../part/fnv1a.hpp"
#include "../part/dense.hpp"
#include "../reflection/query.hpp"
#include <array>
#include <cstring>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace serialize {

namespace detail {

struct type_name_entry {
    int         type_id;
    const char* name;
};

inline dense<type_name_entry>& type_name_registry() noexcept {
    static dense<type_name_entry> registry;
    return registry;
}

struct enum_entry {
    int         type_id;
    int         underlying_type_id;
};

inline dense<enum_entry>& enum_registry() noexcept {
    static dense<enum_entry> registry;
    return registry;
}

} // namespace detail

// 注册稳定类型名
template<typename T>
void register_type_name(const char* stable_name) noexcept {
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::type_name_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            reg[i].name = stable_name;
            return;
        }
    }
    reg.push_back({tid, stable_name});
}

[[nodiscard]] inline const char* lookup_type_name(int tid) noexcept {
    auto& reg = detail::type_name_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            return reg[i].name;
        }
    }
    return nullptr;
}

[[nodiscard]] inline int lookup_type_id(const char* name) noexcept {
    auto& reg = detail::type_name_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (std::strcmp(reg[i].name, name) == 0)
        {
            return reg[i].type_id;
        }
    }
    return -1;
}

// 枚举注册 (序列化为整数)
template<typename T>
void register_enum() noexcept {
    static_assert(std::is_enum_v<T>);
    int tid = type_id::get_type_id<T>();
    using underlying = std::underlying_type_t<T>;
    int utid = type_id::get_type_id<underlying>();
    auto& reg = detail::enum_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            reg[i].underlying_type_id = utid;
            return;
        }
    }
    reg.push_back({tid, utid});
}

[[nodiscard]] inline int lookup_enum_underlying(int tid) noexcept {
    auto& reg = detail::enum_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid)
        {
            return reg[i].underlying_type_id;
        }
    }
    return -1;
}

// ============================================================================
// 实体引用字段注册 (加载时重映射)
// ============================================================================
namespace detail {

struct entity_field_info {
    int         type_id;
    const char* field_name;
    uint32_t    offset;
};

inline dense<entity_field_info>& entity_field_registry() noexcept {
    static dense<entity_field_info> registry;
    return registry;
}

} // namespace detail

template<typename T>
void register_entity_field(const char* field_name) noexcept {
    auto qv = reflect::try_get<T>();
    if (!qv.valid())
    {
        return;
    }
    const auto* fm = qv.field_by_name(field_name);
    if (!fm)
    {
        return;
    }
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::entity_field_registry();
    for (size_t i = 0; i < reg.size(); ++i)
    {
        if (reg[i].type_id == tid && std::strcmp(reg[i].field_name, field_name) == 0)
        {
            reg[i].offset = fm->offset;
            return;
        }
    }
    reg.push_back({tid, field_name, fm->offset});
}

// ============================================================================
// 类型名查询 (自由函数, 供 archive_logic 等模块复用)
// 优先返回注册的稳定名, 未注册则返回 typeid().name()
// ============================================================================
template<typename T>
[[nodiscard]] inline std::string_view get_type_name() noexcept {
    const char* stable = lookup_type_name(type_id::get_type_id<T>());
    if (stable)
    {
        return stable;
    }
    static std::string name = typeid(T).name();
    return name;
}

// ============================================================================
// #E 运行时类型工厂注册表
// 双轨架构: 编译期 Ts... fold 路径 + 运行时 registry 遍历路径
// 存档格式存稳定类型名(字符串), registry 内部用 name_hash(uint64) 索引 O(1) 查找
// save_fn/load_fn 是模板实例化的函数指针, 编译器对每个具体 T 生成优化函数体
// 运行时仅一次间接调用(~1 cycle), 相比序列化的 us~ms 级开销可忽略
// ============================================================================
class serialization; // 前向声明

namespace detail {

// 运行时类型工厂条目
// save_fn_ptr/load_fn_ptr 指向模板实例化的函数, 类型擦除为 serialization* 参数
struct type_factory_entry {
    int          type_id;           // type_id::get_type_id<T>()
    const char*  name;              // 稳定类型名 (存档格式)
    uint64_t     name_hash;         // fnv1a_runtime(name), O(1) 查找
    void(*save_fn)(serialization*, void* w);   // w 为 json_writer*/binary_writer*
    void(*load_fn)(serialization*, void* r, const entity_remap*, uint32_t saved_cv);
    size_t       type_size;         // sizeof(T)
    bool         is_trivially_copyable;
};

inline dense<type_factory_entry>& type_factory_registry() noexcept {
    static dense<type_factory_entry> registry;
    return registry;
}

// name_hash → registry 索引的开放寻址表 (O(1) 查找)
// 空间换速度: 注册阶段写入, 查找阶段无锁读取
struct factory_hash_slot {
    uint64_t hash = 0;       // 0 表示空槽
    uint32_t index = 0;      // registry 数组索引
};

inline constexpr size_t FACTORY_HASH_SIZE = 1u << 12;  // 4096, 类型数 <1000 时负载因子 < 0.25

inline std::array<factory_hash_slot, FACTORY_HASH_SIZE>& factory_hash_table() noexcept {
    static std::array<factory_hash_slot, FACTORY_HASH_SIZE> table{};
    return table;
}

// 插入 hash 索引 (注册时调用)
inline void insert_factory_hash(uint64_t hash, uint32_t index) noexcept {
    if (hash == 0) return;
    auto& table = factory_hash_table();
    size_t mask = FACTORY_HASH_SIZE - 1;
    size_t idx = static_cast<size_t>(hash) & mask;
    for (size_t i = 0; i < FACTORY_HASH_SIZE; ++i) {
        if (table[idx].hash == 0) {
            table[idx].hash = hash;
            table[idx].index = index;
            return;
        }
        if (table[idx].hash == hash) return; // 幂等
        idx = (idx + 1) & mask;
    }
}

// 按 name_hash 查找 registry 索引 (O(1), 无锁)
[[nodiscard]] inline const type_factory_entry* find_factory_by_hash(uint64_t hash) noexcept {
    if (hash == 0) return nullptr;
    auto& table = factory_hash_table();
    auto& reg = type_factory_registry();
    size_t mask = FACTORY_HASH_SIZE - 1;
    size_t idx = static_cast<size_t>(hash) & mask;
    for (size_t i = 0; i < FACTORY_HASH_SIZE; ++i) {
        const auto& slot = table[idx];
        if (slot.hash == 0) return nullptr;
        if (slot.hash == hash) {
            return slot.index < reg.size() ? &reg[slot.index] : nullptr;
        }
        idx = (idx + 1) & mask;
    }
    return nullptr;
}

// 按类型名查找 (运行时 hash 后 O(1) 查找)
[[nodiscard]] inline const type_factory_entry* find_factory_by_name(std::string_view name) noexcept {
    if (name.empty()) return nullptr;
    // 运行时 hash (load 时从存档字符串名计算)
    uint64_t hash = fnv1a_runtime(name.data(), name.size());
    return find_factory_by_hash(hash);
}

// ============================================================================
// #D2 循环检测: thread_local visiting 栈
// 序列化对象图时检测循环引用, 防止无限递归
// ============================================================================

struct visiting_entry {
    const void* ptr;       // 对象地址
    int         type_id;   // 类型 id (0 = 任意类型)
};

// thread_local 只能修饰变量, 不能修饰函数返回类型
inline dense<visiting_entry>& visiting_stack() noexcept {
    thread_local dense<visiting_entry> stack;
    return stack;
}

// #C3 类型别名表 (向后兼容)
// 前置定义: register_type_factory 需在注册时同步别名 hash 到索引表
struct type_alias_entry {
    uint64_t    old_name_hash;  // fnv1a_runtime(old_name)
    int         type_id;        // type_id::get_type_id<T>()
    const char* old_name;       // 旧名 (调试用)
};

inline dense<type_alias_entry>& type_alias_registry() noexcept {
    static dense<type_alias_entry> registry;
    return registry;
}

inline void push_visiting(const void* ptr, int type_id = 0) noexcept {
    visiting_stack().push_back({ptr, type_id});
}

inline void pop_visiting() noexcept {
    auto& s = visiting_stack();
    if (s.size() > 0) {
        s.pop_back();
    }
}

[[nodiscard]] inline bool is_visiting(const void* ptr) noexcept {
    auto& s = visiting_stack();
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i].ptr == ptr) {
            return true;
        }
    }
    return false;
}

inline void clear_visiting() noexcept {
    visiting_stack().clear();
}

[[nodiscard]] inline size_t visiting_depth() noexcept {
    return visiting_stack().size();
}

} // namespace detail

// #D2 RAII 守卫: 构造时 push, 析构时 pop, 异常安全 (虽然项目禁异常, 但保证 pop 执行)
struct visiting_guard {
    visiting_guard(const void* ptr, int type_id = 0) noexcept {
        detail::push_visiting(ptr, type_id);
    }
    ~visiting_guard() noexcept {
        detail::pop_visiting();
    }
    visiting_guard(const visiting_guard&) = delete;
    visiting_guard& operator=(const visiting_guard&) = delete;
};

// 注册类型工厂 (显式注册, 幂等)
// 模板实例化生成 save_fn/load_fn 函数指针, 编译器对每个 T 优化
template<typename T>
void register_type_factory(const char* stable_name) noexcept {
    int tid = type_id::get_type_id<T>();
    auto& reg = detail::type_factory_registry();

    // 幂等检查
    for (size_t i = 0; i < reg.size(); ++i) {
        if (reg[i].type_id == tid) {
            reg[i].name = stable_name;
            reg[i].name_hash = fnv1a_runtime(stable_name);
            detail::insert_factory_hash(reg[i].name_hash, static_cast<uint32_t>(i));
            return;
        }
    }

    uint32_t idx = static_cast<uint32_t>(reg.size());
    detail::type_factory_entry entry;
    entry.type_id = tid;
    entry.name = stable_name;
    entry.name_hash = fnv1a_runtime(stable_name);
    entry.save_fn = nullptr;  // 由 serializer.hpp 的 register_factory_for_type 填充
    entry.load_fn = nullptr;
    entry.type_size = sizeof(T);
    entry.is_trivially_copyable = std::is_trivially_copyable_v<T>;
    reg.push_back(entry);
    detail::insert_factory_hash(reg[idx].name_hash, idx);

    // #C3 应用该类型已注册的别名 (将旧名 hash 也指向此 factory entry)
    auto& aliases = detail::type_alias_registry();
    for (size_t i = 0; i < aliases.size(); ++i) {
        if (aliases[i].type_id == tid) {
            detail::insert_factory_hash(aliases[i].old_name_hash, idx);
        }
    }
}

// ============================================================================
// #C3 类型别名查询 (entry / registry 已在前置 detail 块定义)
// ============================================================================

namespace detail {

[[nodiscard]] inline bool is_alias_of(int type_id, std::string_view name) noexcept {
    if (name.empty()) return false;
    uint64_t hash = fnv1a_runtime(name.data(), name.size());
    auto& reg = type_alias_registry();
    for (size_t i = 0; i < reg.size(); ++i) {
        if (reg[i].type_id == type_id && reg[i].old_name_hash == hash) {
            return true;
        }
    }
    return false;
}

} // namespace detail

// 注册类型别名 (旧名 → 当前类型 T)
// 必须在 register_type_name<T> / register_type_factory<T> 之后调用
template<typename T>
void register_type_alias(const char* old_name) noexcept {
    int tid = type_id::get_type_id<T>();
    uint64_t hash = fnv1a_runtime(old_name);
    auto& reg = detail::type_alias_registry();

    // 幂等检查
    for (size_t i = 0; i < reg.size(); ++i) {
        if (reg[i].old_name_hash == hash) {
            reg[i].type_id = tid;
            reg[i].old_name = old_name;
            return;
        }
    }
    reg.push_back({hash, tid, old_name});

    // 若 T 的 factory 已注册, 立即将别名 hash 加入 hash 表
    auto& fac_reg = detail::type_factory_registry();
    for (size_t i = 0; i < fac_reg.size(); ++i) {
        if (fac_reg[i].type_id == tid) {
            detail::insert_factory_hash(hash, static_cast<uint32_t>(i));
            break;
        }
    }
}

} // namespace serialize

