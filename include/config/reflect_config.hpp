#pragma once

// 反射模块配置
// 用户可在包含反射头文件前覆盖这些宏

// === 聚合类型自动遍历字段数上限 ===
// 仅影响 add_aggregate_type 的编译期字段探测与展开, 不限制显式注册字段总数
// 必须是 16/32/64/128/256 之一
#if !defined(REFLECT_MAX_FIELDS)
#  define REFLECT_MAX_FIELDS 64
#endif
