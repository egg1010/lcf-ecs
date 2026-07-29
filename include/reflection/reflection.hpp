#pragma once

// reflection.hpp - 反射模块统一入口
// 用户只需 #include "reflection/reflection.hpp" 即可使用全部反射功能
// 命名空间: reflect

// 底层可复用组件 (part)
#include "../part/aggregate_reflect.hpp"
#include "../part/type_erasure.hpp"
#include "../part/member_offset.hpp"

// 反射专用组件
#include "meta.hpp"
#include "storage.hpp"
#include "query.hpp"
#include "macros.hpp"
