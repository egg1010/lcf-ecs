#pragma once

// 反射模块配置
// 用户可在包含反射头文件前覆盖这些宏

// === 聚合类型字段遍历上限 ===
// 影响 aggregate_reflect 的编译期字段探测能力
// 必须是 16/32/64/128/256 之一
#if !defined(REFLECT_MAX_FIELDS)
#  define REFLECT_MAX_FIELDS 64
#endif

// === 单类型字段上限 ===
// type_meta 内嵌 fields 数组大小, 与 REFLECT_MAX_FIELDS 对齐
#if !defined(MAX_FIELDS_PER_TYPE)
#  define MAX_FIELDS_PER_TYPE 256
#endif

// === 单类型方法上限 ===
// type_meta 内嵌 methods 数组大小
#if !defined(MAX_METHODS_PER_TYPE)
#  define MAX_METHODS_PER_TYPE 256
#endif

// === 类型注册槽位上限 ===
// storage 中 type_entries_ 数组大小, 限制可注册类型总数
#if !defined(MAX_TYPE_ID)
#  define MAX_TYPE_ID 65536
#endif
