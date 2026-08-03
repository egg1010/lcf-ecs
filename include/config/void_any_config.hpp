#pragma once

// 分层分配器配置
// 选择以下两种配置之一：

// 选项 1: 禁用分层分配器
// #define VOID_ANY_LAYERED_ALLOCATOR_NOT_ENABLED
// 选项 2: 启用分层分配器 (小对象<=128B 走 slab, 大对象走 TLSF)
#define VOID_ANY_USE_LAYERED_ALLOCATOR

// 小对象优化 (SSO) 配置
// 选择以下两种配置之一：

// 选项 1: 禁用小对象优化
// #define VOID_ANY_SSO_NOT_ENABLED
// 选项 2: 启用小对象优化
#define VOID_ANY_ENABLE_SSO

// SSO 缓冲区大小与对齐为内部实现细节, 由 void_any.hpp 中的模板参数确定,
// 用户只需使用 void_any 别名, 无需关心 SSO 配置
