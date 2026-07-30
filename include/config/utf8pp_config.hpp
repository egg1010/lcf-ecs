#pragma once

// utf8pp 内存分配器配置
// utf8pp 已实现完整内存分配架构 (SSO + 3 级增长 + 堆管理)
// 默认关闭: 堆路径使用 std::malloc/std::free
// 启用(=1): 堆路径接入自研分配器 (part/ 下的 memory_pool / layered_allocator 等)
#if !defined(UTF8PP_ENABLE_ALLOCATOR)
#define UTF8PP_ENABLE_ALLOCATOR 0
#endif

// 启用时的分配器类型 (仅 UTF8PP_ENABLE_ALLOCATOR=1 时生效)
// UTF8PP_ALLOC_MEMORY_POOL - TLSF 内存池 (part/memory_pool.hpp)
// UTF8PP_ALLOC_LAYERED     - 分层分配器 (part/layered_allocator.hpp, 小对象 slab + 大对象 TLSF)
#if !defined(UTF8PP_ALLOCATOR_TYPE)
#define UTF8PP_ALLOCATOR_TYPE UTF8PP_ALLOC_MEMORY_POOL
#endif
