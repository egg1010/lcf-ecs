#pragma once

// 内存池配置
// 选择以下两种配置之一：

// 选项 1: 禁用内存池
// #define VOID_ANY_MEMORY_POOL_NOT_ENABLED

// 选项 2: 启用内存池
#define VOID_ANY_ENABLE_MEMORY_POOL

// 分层分配器配置
// 启用分层分配器: 小对象(<=128B)走 slab(0 header, O(1) push/pop), 大对象走 TLSF
// 不启用则纯 memory_pool(TLSF) 路径
#if !defined(VOID_ANY_USE_LAYERED_ALLOCATOR)
#define VOID_ANY_USE_LAYERED_ALLOCATOR
#endif

// 小对象优化 (SSO) 配置
// 选择以下两种配置之一：

// 选项 1: 禁用小对象优化
// #define VOID_ANY_SSO_NOT_ENABLED
// 选项 2: 启用小对象优化
#define VOID_ANY_ENABLE_SSO

// 只有在启用 SSO 时才有效
// 56 + 8(vtable_sso_type_) = 64, 恰好 1 个 cache line
#if !defined(VOID_ANY_SSO_BUFFER_SIZE)
#define VOID_ANY_SSO_BUFFER_SIZE 56
#endif

// 设为 8: 确保 sizeof(void_any)==64 (1 cache line)
// 注意: alignas(32) 会导致 storage padding 至 64, 总大小变 96, 破坏 1 cache line 不变量
// 现代x86 CPU 的 unaligned AVX2 load/store 已接近 aligned 性能, 故选择 8
#if !defined(VOID_ANY_SSO_ALIGNMENT)
#define VOID_ANY_SSO_ALIGNMENT 8
#endif

