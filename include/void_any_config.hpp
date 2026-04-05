#pragma once

// ========================================================
// void_any 配置文件
// ========================================================

// --------------------------------------------------------
// 内存池配置
// --------------------------------------------------------
// 选择以下两种配置之一：

// 选项 1: Memory pool not enabled (禁用内存池)
// #define VOID_ANY_MEMORY_POOL_NOT_ENABLED

// 选项 2: Enable memory pool (启用内存池)
#define VOID_ANY_ENABLE_MEMORY_POOL

// --------------------------------------------------------
// 小对象优化 (SSO) 配置
// --------------------------------------------------------
// 选择以下两种配置之一：

// 选项 1: SSO not enabled (禁用小对象优化)
// #define VOID_ANY_SSO_NOT_ENABLED
// 选项 2: Enable SSO (启用小对象优化)
#define VOID_ANY_ENABLE_SSO

// SSO 缓冲区大小（字节）
// 只有在启用 SSO 时才有效
// 默认值: 32 字节
#if !defined(VOID_ANY_SSO_BUFFER_SIZE)
#define VOID_ANY_SSO_BUFFER_SIZE 32
#endif

