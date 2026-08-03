#pragma once

// meta.hpp - 反射元数据结构定义
// 命名空间: reflect
// 包含: 字段/方法/类型/枚举/属性/继承/构造 元数据

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <array>
#include <utility>
#include "../part/type_erasure.hpp"
#include "../part/fnv1a.hpp"
#include "../part/type_ops.hpp"
#include "../part/void_any.hpp"
#include "../part/dense.hpp"
#include "../config/reflect_config.hpp"

namespace reflect {

// 前向声明
struct type_meta;

// === 字段属性/注解 (#4) ===
struct attr_entry
{
    uint64_t key_hash;      // FNV-1a(key)
    void_any value;         // 类型擦除值
};

// 字段元数据
struct field_meta
{
    const char* name;           // 字段名
    uint32_t offset;            // 相对对象起始的偏移量
    int type_id;                // 字段类型 id (数组时为元素类型 id)
    bool is_const;              // 是否 const 成员
    bool is_private;            // 是否私有成员
    uint8_t array_rank;         // 数组维度 (0=标量, 1~4=数组)
    uint8_t reserved;           // 对齐填充
    uint32_t total_elements;    // 数组总元素数 (0=非数组)
    std::array<uint16_t, 4> extents{};  // 每维元素数 (仅前 array_rank 个有效)
    uint32_t element_stride;    // 元素步长 (字节)

    // #4 属性/注解 (动态, 默认空)
    dense<attr_entry> attrs;
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
};

// === 构造/销毁函数指针 (#1) ===
using construct_func = void* (*)(void* result_buf) noexcept;  // result_buf=nullptr 时堆分配
using destruct_func = void (*)(void* obj) noexcept;

// === 继承关系条目 (#2) ===
struct base_offset_entry
{
    int base_type_id;
    ptrdiff_t offset;   // 派生类→基类指针调整量
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
    uint64_t name_hash = 0;                             // #10 FNV-1a(name) 稳定标识
    std::atomic<bool> registered{false};                // 是否已注册
    std::atomic<uint16_t> field_count{0};               // 字段数
    std::atomic<uint16_t> method_count{0};              // 方法数
    uint16_t size = 0;                                  // sizeof(T)
    uint16_t align = 0;                                 // alignof(T)
    int type_id = -1;                                   // type_id::get_type_id<T>()

    // 内嵌字段/方法数组
    std::array<field_meta, MAX_FIELDS_PER_TYPE> fields{};
    std::array<method_meta, MAX_METHODS_PER_TYPE> methods{};

    // #1 构造/销毁
    construct_func default_construct_{nullptr};
    destruct_func destruct_{nullptr};
    bool has_default_construct{false};

    // #2 继承关系
    dense<base_offset_entry> base_offsets;      // 直接基类
    dense<int> derived_type_ids;                // 直接派生类

    // #7 类型转换
    dense<convert_entry> converters;

    // #5 容器特征 (指向全局 container_ops_registry, 此处存指针避免拷贝)
    const container_ops_table* container_ops{nullptr};
};

} // namespace reflect
