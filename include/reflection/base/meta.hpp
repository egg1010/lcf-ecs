#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include "config/reflect_config.hpp"
#include "part/dense.hpp"
#include "part/fnv1a.hpp"
#include "part/force_inline.hpp"
#include "part/operating_message.hpp"
#include "part/type/type_id.hpp"
#include "part/type/void_any.hpp"
#include "reflection/base/invoke.hpp"
#include "reflection/base/type_ops.hpp"

// meta.hpp - 字段 / 方法 / 类型 / 枚举元数据
// 命名空间: reflect

// ============================================================================

// meta.hpp - 反射元数据结构定义
// 命名空间: reflect
// 包含: 字段/方法/类型/枚举/属性/继承/构造 元数据


namespace reflect {

// 前向声明
struct type_meta;

// === 字段属性/注解 (#4) ===
struct attr_entry
{
    uint64_t key_hash;      // FNV-1a(key)
    void_any value;         // 类型擦除值
};

// 字段标志
inline constexpr uint8_t field_flag_const = 1u << 0;
inline constexpr uint8_t field_flag_private = 1u << 1;

// 类型槽位状态
enum class type_state : uint8_t
{
    empty = 0,
    live = 1,
    retired = 2
};

// 字段元数据
struct field_meta
{
    const char* name;           // 字段名
    uint32_t offset;            // 相对对象起始的偏移量
    int type_id;                // 字段类型 id (数组时为元素类型 id)
    uint8_t flags;              // field_flag_* 位标志
    uint8_t array_rank;         // 数组维度 (0=标量, 1~4=数组)
    uint16_t reserved;          // 对齐填充
    uint32_t total_elements;    // 数组总元素数 (0=非数组)
    std::array<uint32_t, 4> extents{};  // 每维元素数 (仅前 array_rank 个有效)
    uint32_t element_stride;    // 元素步长 (字节)
    bool trivially_copyable;    // 字段(数组为元素)是否可平凡复制

    // #4 属性/注解 (动态, 默认空)
    dense<attr_entry> attrs;

    [[nodiscard]] bool is_const() const noexcept { return (flags & field_flag_const) != 0; }
    [[nodiscard]] bool is_private() const noexcept { return (flags & field_flag_private) != 0; }
};

// === 方法参数类型 id 静态存储 (#12) ===
// arg_type_ids 指向静态数组, 生命周期安全

// 方法元数据
struct method_meta
{
    const char* name;               // 方法名
    uint8_t arg_count;              // 参数数量
    int return_type_id;             // 返回类型 id (void = -1)
    invoker_func invoker;           // 类型擦除调用器
    bool is_const;                  // const 方法
    bool is_static;                 // 静态方法
    const int* arg_type_ids;        // #12 参数类型 id 数组 (nullptr=未设置)
    int vtable_offset;              // #6 虚函数 vtable 偏移 (-1=非虚)
    uint8_t fn_size;                // 指针副本字节数 (0=未存)
    std::array<std::byte, fn_storage_size> fn_storage{};  // 成员函数指针副本
};

// === 构造/销毁函数指针 (#1) ===
using construct_func = void* (*)(void* result_buf) noexcept;  // result_buf=nullptr 时堆分配
using destruct_func = void (*)(void* obj) noexcept;

// === 继承关系条目 (#2) ===
struct base_offset_entry
{
    int base_type_id;
    ptrdiff_t offset;   // 派生类→基类指针调整量
    void* (*adjust_fn)(void*) noexcept;   // 类型化调整 (优先于 offset)
};

// === 类型转换条目 (#7) ===
struct convert_entry
{
    int target_type_id;
    void (*convert_fn)(const void* src, void* dst) noexcept;
};

// === 枚举值条目 (#3) ===
struct enum_value_entry
{
    uint64_t value;        // 枚举值 (用 underlying type 存储)
    const char* name;      // 枚举名
    uint64_t name_hash;    // FNV-1a(name)
};

// 枚举元数据
struct enum_meta
{
    const char* name{nullptr};
    int underlying_type_id{-1};
    int type_id{-1};
    dense<enum_value_entry> values;     // value→name (按 value 排序可选)
    bool registered{false};
};

// 类型槽位上限来自 config/reflect_config.hpp (MAX_TYPE_ID, MAX_FIELDS_PER_TYPE, MAX_METHODS_PER_TYPE)

// 类型元数据
struct type_meta
{
    const char* name = nullptr;                         // 类型名
    std::atomic<uint8_t> state{0};                      // type_state
    std::atomic<uint16_t> field_count{0};               // 字段数
    std::atomic<uint16_t> method_count{0};              // 方法数
    size_t size = 0;                                    // sizeof(T)
    size_t align = 0;                                   // alignof(T)
    int type_id = -1;                                   // type_id::get_type_id<T>()

    // 内嵌字段/方法数组
    std::array<field_meta, MAX_FIELDS_PER_TYPE> fields{};
    std::array<method_meta, MAX_METHODS_PER_TYPE> methods{};

    // #1 构造/销毁
    construct_func default_construct_{nullptr};
    destruct_func destruct_{nullptr};
    void (*free_heap_)(void*) noexcept{nullptr};   // 与 create() 匹配的释放 (含对齐)
    bool has_default_construct{false};

    // #2 继承关系
    dense<base_offset_entry> base_offsets;      // 直接基类
    dense<int> derived_type_ids;                // 直接派生类

    // #7 类型转换
    dense<convert_entry> converters;

    // #5 容器特征 (指向统一操作表注册器中的容器表, 此处存指针避免拷贝)
    const container_ops_table* container_ops{nullptr};
};

namespace detail {

// 单份字段遍历骨架的产出: 操作表 + 字段/元素基址 + 对侧基址 + 元素数 + 步长 + 平凡标记
struct field_slot
{
    const type_ops_table* ops;
    const char* base;
    const char* other;
    uint32_t count;
    uint32_t stride;
    bool trivially_copyable;
};

// 遍历对象字段: 标量计 1 次, 数组按元素展开; f 返回 false 时提前结束并返回 false
template<typename F>
inline bool visit_field_slots(const void* lhs, const void* rhs,
                              const type_meta& meta, F&& f) noexcept
{
    size_t n = meta.field_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i)
    {
        const field_meta& fm = meta.fields[i];
        const field_slot slot{
            global_ops().get(fm.type_id),
            static_cast<const char*>(lhs) + fm.offset,
            rhs ? static_cast<const char*>(rhs) + fm.offset : nullptr,
            fm.array_rank > 0 ? fm.total_elements : 1u,
            fm.element_stride,
            fm.trivially_copyable
        };
        if (!f(slot, fm))
        {
            return false;
        }
    }
    return true;
}

} // namespace detail

} // namespace reflect

// ============================================================================
