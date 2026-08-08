#pragma once

// utf8pp 内存分配器配置
// 选择以下两种配置之一：

// 选项 1: 禁用分层分配器, 堆路径使用 std::malloc/std::free
// #define UTF8PP_LAYERED_ALLOCATOR_NOT_ENABLED
// 选项 2: 启用分层分配器
#define UTF8PP_USE_LAYERED_ALLOCATOR
