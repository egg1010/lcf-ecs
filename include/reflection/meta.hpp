#pragma once

// meta.hpp - 反射元数据结构定义
// 命名空间: reflect

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <array>
#include "../part/type_erasure.hpp"
#include "../config/reflect_config.hpp"

namespace reflect {

// 字段元数据
struct field_meta
{
    const char* name;       // 字段名
    uint32_t offset;        // 相对对象起始的偏移量
    int type_id;            // 字段类型 id
    bool is_const;          // 是否 const 成员
    bool is_private;        // 是否私有成员
};

// 方法元数据
struct method_meta
{
    const char* name;               // 方法名
    uint8_t arg_count;              // 参数数量
    int return_type_id;             // 返回类型 id (void = -1)
    invoker_func invoker;           // 类型擦除调用器
    bool is_const;                  // const 方法
    bool is_static;                 // 静态方法
};

// 类型槽位上限来自 config/reflect_config.hpp (MAX_TYPE_ID, MAX_FIELDS_PER_TYPE, MAX_METHODS_PER_TYPE)

// 类型元数据
struct type_meta
{
    const char* name = nullptr;                         // 类型名
    std::atomic<bool> registered{false};                // 是否已注册
    std::atomic<uint16_t> field_count{0};               // 字段数
    std::atomic<uint16_t> method_count{0};              // 方法数
    uint16_t size = 0;                                  // sizeof(T)
    uint16_t align = 0;                                 // alignof(T)
    int type_id = -1;                                   // type_id::get_type_id<T>()

    // 内嵌字段/方法数组
    std::array<field_meta, MAX_FIELDS_PER_TYPE> fields{};
    std::array<method_meta, MAX_METHODS_PER_TYPE> methods{};
};

} // namespace reflect
