// type_name.hpp - 稳定类型名注册 + 实体引用字段注册 + 枚举注册
// 名字→id 的存储/查找统一收敛于 type_id 的 def 表 (字节编码, 非 hash):
//   模板类型经 register_type_name / register_type_factory / register_type_alias 绑定
#pragma once

#include "../part/type_id.hpp"
#include "../part/dense.hpp"
#include "../reflection/query.hpp"
#include <array>
#include <cstring>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace serialize {

namespace detail {

struct enum_entry {
    int         type_id;
    int         underlying_type_id;
};

inline dense<enum_entry>& enum_registry() noexcept {
    static dense<enum_entry> registry;
    return registry;
}

} // namespace detail

// 注册稳定类型名 (绑定到 type_id 统一名字表)
template<typename T>
void register_type_name(const char* stable_name) noexcept {
    type_id::bind_def_name(stable_name, type_id::get_type_id<T>());
}

[[nodiscard]] inline const char* lookup_type_name(int tid) noexcept {
    const std::string_view sv = type_id::get_def_type_name(tid);
    return sv.empty() ? nullptr : sv.data();
}

[[nodiscard]] inline int lookup_type_id(const char* name) noexcept {
    return type_id::get_def_type_id(name);
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
// 存档格式存稳定类型名(字符串); 名字→id 经 type_id 统一名字表解析 (字节编码, 零碰撞),
// factory 表内部用 type_id → registry 索引的稠密映射 O(1) 定位条目
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
    void(*save_fn)(serialization*, void* w);   // w 为 json_writer*/binary_writer*
    void(*load_fn)(serialization*, void* r, const entity_remap*, uint32_t saved_cv);
    size_t       type_size;         // sizeof(T)
    bool         is_trivially_copyable;
};

inline dense<type_factory_entry>& type_factory_registry() noexcept {
    static dense<type_factory_entry> registry;
    return registry;
}

// type_id → registry 索引的稠密映射 (按 type_id 索引, -1 表示未注册)
inline dense<int32_t>& factory_id_index() noexcept {
    static dense<int32_t> index;
    return index;
}

// 幂等确保 factory 条目存在 (id→索引映射维护于此), 返回 registry 索引
inline uint32_t ensure_factory_entry(int tid, const char* name,
                                      size_t type_size, bool trivial) noexcept {
    auto& reg = type_factory_registry();
    auto& idx = factory_id_index();
    if (tid >= 0 && tid < static_cast<int>(idx.size())
        && idx[tid] >= 0 && static_cast<size_t>(idx[tid]) < reg.size())
    {
        return static_cast<uint32_t>(idx[tid]);
    }
    type_factory_entry entry{};
    entry.type_id = tid;
    entry.name = name;
    entry.type_size = type_size;
    entry.is_trivially_copyable = trivial;
    if (static_cast<int>(idx.size()) <= tid)
    {
        idx.increase_capacity(static_cast<size_t>(tid) + 1, -1);
    }
    const uint32_t new_idx = static_cast<uint32_t>(reg.size());
    idx[tid] = static_cast<int32_t>(new_idx);
    reg.push_back(entry);
    return new_idx;
}

// 按 type_id 查找 factory 条目 (O(1) 稠密索引)
[[nodiscard]] inline const type_factory_entry* find_factory_by_id(int tid) noexcept {
    auto& idx = factory_id_index();
    if (tid < 0 || tid >= static_cast<int>(idx.size())) return nullptr;
    const int32_t i = idx[tid];
    auto& reg = type_factory_registry();
    if (i < 0 || static_cast<size_t>(i) >= reg.size()) return nullptr;
    return &reg[i];
}

// 按类型名查找 (名字→id 经 type_id 统一表, 再经稠密索引定位)
[[nodiscard]] inline const type_factory_entry* find_factory_by_name(std::string_view name) noexcept {
    if (name.empty()) return nullptr;
    const int id = type_id::get_def_type_id(name);
    if (id < 0) return nullptr;
    return find_factory_by_id(id);
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
// 名字→id 经 type_id 统一绑定; save_fn/load_fn 由 serializer.hpp 填充
template<typename T>
void register_type_factory(const char* stable_name) noexcept {
    int tid = type_id::get_type_id<T>();
    type_id::bind_def_name(stable_name, tid);
    const uint32_t i = detail::ensure_factory_entry(
        tid, stable_name, sizeof(T), std::is_trivially_copyable_v<T>);
    detail::type_factory_registry()[i].name = stable_name;
}

namespace detail {

// #C3 别名判定: 名字解析到目标 type_id 即为该类型的 (别名) 名
[[nodiscard]] inline bool is_alias_of(int type_id, std::string_view name) noexcept {
    if (name.empty()) return false;
    return type_id::get_def_type_id(name) == type_id;
}

} // namespace detail

// 注册类型别名 (旧名 → 当前类型 T)
// 经 type_id 统一名字表绑定, 与注册顺序无关
template<typename T>
void register_type_alias(const char* old_name) noexcept {
    type_id::bind_def_name(old_name, type_id::get_type_id<T>());
}

} // namespace serialize

