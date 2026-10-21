#pragma once

// reflection.hpp - 反射模块统一入口
// 用户只需 #include "reflection/reflection.hpp" 即可使用全部反射功能
// 命名空间: reflect

// base 基础层：字段偏移、成员调用、容器检测、字段操作、聚合遍历、类型元信息
#include "part/fnv1a.hpp"
#include "reflection/base/member_offset.hpp"
#include "reflection/base/invoke.hpp"
#include "reflection/base/container_ops.hpp"
#include "reflection/base/field_ops.hpp"
#include "reflection/base/aggregate_traverse_pp.hpp"
#include "reflection/base/aggregate_traverse.hpp"
#include "reflection/base/meta.hpp"

// core 核心层：字段存储、查询、宏、枚举视图
#include "reflection/core/storage.hpp"
#include "reflection/core/query.hpp"
#include "reflection/core/macros.hpp"
#include "reflection/core/enum_view.hpp"        // #3 枚举反射

// ops 操作层：属性、继承、动态派发、转换、构造、比较、哈希、容器
#include "reflection/ops/attributes.hpp"        // #4 属性/注解
#include "reflection/ops/inheritance.hpp"       // #2 继承关系
#include "reflection/ops/virtual_dispatch.hpp"  // #6 动态派发
#include "reflection/ops/convert.hpp"           // #7 类型转换
#include "reflection/ops/construct.hpp"         // #1 对象构造/销毁
#include "reflection/ops/compare.hpp"           // #8 字段比较/克隆
#include "reflection/ops/hash.hpp"              // #9 字段哈希
#include "reflection/ops/container.hpp"         // #5 容器反射
