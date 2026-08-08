#pragma once

// 分层分配器配置
// 选择以下两种配置之一：

// 选项 1: 禁用分层分配器, 堆路径使用 std::malloc/std::free
// #define VOID_ANY_LAYERED_ALLOCATOR_NOT_ENABLED
// 选项 2: 启用分层分配器
#define VOID_ANY_USE_LAYERED_ALLOCATOR

// 小对象优化 (SSO) 配置
// 选择以下两种配置之一：

// 选项 1: 禁用小对象优化
// #define VOID_ANY_SSO_NOT_ENABLED
// 选项 2: 启用小对象优化
#define VOID_ANY_ENABLE_SSO
