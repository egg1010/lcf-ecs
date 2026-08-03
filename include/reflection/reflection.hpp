#pragma once

// reflection.hpp - 反射模块统一入口
// 用户只需 #include "reflection/reflection.hpp" 即可使用全部反射功能
// 命名空间: reflect

// 底层可复用组件 (part, 无命名空间, 通用化)
#include "../part/aggregate_reflect.hpp"
#include "../part/type_erasure.hpp"
#include "../part/member_offset.hpp"
#include "../part/fnv1a.hpp"
#include "../part/type_ops.hpp"
#include "../part/container_traits.hpp"

// 反射专用组件 (reflect 命名空间)
#include "meta.hpp"
#include "storage.hpp"
#include "query.hpp"
#include "macros.hpp"

// 新增功能模块 (1~12)
#include "enum_view.hpp"        // #3 枚举反射
#include "attributes.hpp"       // #4 属性/注解
#include "inheritance.hpp"      // #2 继承关系
#include "virtual_dispatch.hpp" // #6 动态派发
#include "convert.hpp"          // #7 类型转换
#include "construct.hpp"        // #1 对象构造/销毁
#include "compare.hpp"          // #8 字段比较/克隆
#include "hash.hpp"             // #9 字段哈希
#include "container.hpp"        // #5 容器反射
