window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['config'] = {
  id: 'config',
  title: "可选宏配置",
  category: 'config',
  icon: '#',
  order: 13,
  content: `## 13. 可选宏配置

### 13.1 栈内存控制（\`config/ecs_config.hpp\`）

嵌入式 / RTOS 环境通过 \`LCF_MINIMAL_STACK\` 关闭栈分配，将基数排序的大数组从栈分配切换到堆分配。桌面环境默认 \`0\`，保留栈分配。

| 宏 | 默认值 | 说明 |
|------|--------|------|
| \`LCF_MINIMAL_STACK\` | \`0\` | \`1\` 关闭栈分配：基数排序直方图（16KB×2）与 \`count_stack\`（16KB×2）改走堆分配 |

\`\`\`cmake
# 嵌入式项目在 CMakeLists.txt 中定义
target_compile_definitions(my_target PRIVATE LCF_MINIMAL_STACK=1)
\`\`\`

| 受控点 | 栈占用（默认） | 嵌入式回退 |
|--------|---------------|-----------|
| \`radix_count_pass\` bc≤512 分支 | 16KB（h0-h3 局部直方图） | \`::operator new\` 堆分配，失败回退单直方图 |
| \`radix_count_pass\` bc≤1024 分支 | 16KB（h0-h1 局部直方图） | \`::operator new\` 堆分配，失败回退单直方图 |
| \`radix_sort_entries_with_cfg\` count_stack | 16KB（2048 个 size_t） | \`::operator new\` 堆分配 |
| \`radix_sort_indices_with_cfg\` count_stack | 16KB（2048 个 size_t） | \`::operator new\` 堆分配 |

### 13.2 void_any 存储策略（\`config/void_any_config.hpp\`）

影响 \`void_any\` 的存储策略与内存分配方式。

| 宏 | 说明 |
|------|------|
| \`VOID_ANY_ENABLE_SSO\` | 启用 void_any 小对象存储（SSO），小对象内联存储 |
| \`VOID_ANY_ENABLE_MEMORY_POOL\` | 启用 void_any 内存池，使用 \`memory_pool\` 替代 \`::operator new\` |
| \`VOID_ANY_USE_LAYERED_ALLOCATOR\` | 启用分层分配器：小对象（≤128B）走 slab，大对象走 TLSF（优先级高于 \`VOID_ANY_ENABLE_MEMORY_POOL\`） |
| \`VOID_ANY_SSO_BUFFER_SIZE\` | SSO 缓冲区大小（默认 56 字节） |
| \`VOID_ANY_SSO_ALIGNMENT\` | SSO 对齐（默认 8 字节） |
| \`VOID_ANY_MEMORY_POOL_NOT_ENABLED\` | 禁用内存池（与 \`VOID_ANY_ENABLE_MEMORY_POOL\` 互斥） |
| \`VOID_ANY_SSO_NOT_ENABLED\` | 禁用 SSO（与 \`VOID_ANY_ENABLE_SSO\` 互斥） |

### 13.3 反射模块配置（\`config/reflect_config.hpp\`）

影响反射模块的类型注册上限与单类型字段/方法数组大小。

| 宏 | 默认值 | 说明 |
|------|--------|------|
| \`REFLECT_MAX_FIELDS\` | \`64\` | 聚合类型字段遍历上限，支持 16/32/64/128/256 |
| \`MAX_FIELDS_PER_TYPE\` | \`256\` | 单类型 fields 数组大小 |
| \`MAX_METHODS_PER_TYPE\` | \`256\` | 单类型 methods 数组大小 |
| \`MAX_TYPE_ID\` | \`65536\` | storage 类型槽位上限，限制可注册类型总数 |

\`\`\`cpp
// config/reflect_config.hpp
#define REFLECT_MAX_FIELDS 128
#define MAX_FIELDS_PER_TYPE 256
#define MAX_METHODS_PER_TYPE 256
#define MAX_TYPE_ID 65536
\`\`\`

### 13.4 utf8pp 内存分配器配置（\`config/utf8pp_config.hpp\`）

\`utf8pp\` 已实现完整内存分配架构（SSO + 3 级增长 + 堆管理）。默认关闭，堆路径使用 \`std::malloc/std::free\`；启用后堆路径接入项目分配器。

| 宏 | 默认值 | 说明 |
|------|--------|------|
| \`UTF8PP_ENABLE_ALLOCATOR\` | \`0\` | \`1\` 启用项目分配器接入 utf8pp 堆路径 |
| \`UTF8PP_ALLOCATOR_TYPE\` | \`UTF8PP_ALLOC_MEMORY_POOL\` | 启用时的分配器类型：\`UTF8PP_ALLOC_MEMORY_POOL\`（TLSF 内存池）/ \`UTF8PP_ALLOC_LAYERED\`（分层分配器：小对象 slab + 大对象 TLSF） |

\`\`\`cmake
# 启用 utf8pp 内存池
target_compile_definitions(my_target PRIVATE UTF8PP_ENABLE_ALLOCATOR=1)

# 启用分层分配器
target_compile_definitions(my_target PRIVATE UTF8PP_ENABLE_ALLOCATOR=1 UTF8PP_ALLOCATOR_TYPE=UTF8PP_ALLOC_LAYERED)
\`\`\`

### 14.4 配置示例

\`\`\`cpp
// config/void_any_config.hpp

// 启用内存池
#define VOID_ANY_ENABLE_MEMORY_POOL

// 启用分层分配器（小对象走 slab, 大对象走 TLSF, 优先级高于 memory_pool）
#define VOID_ANY_USE_LAYERED_ALLOCATOR

// 启用小对象存储
#define VOID_ANY_ENABLE_SSO

// SSO 缓冲区大小
#define VOID_ANY_SSO_BUFFER_SIZE 56

// SSO 对齐
#define VOID_ANY_SSO_ALIGNMENT 8
\`\`\`

### 13.5 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`VOID_ANY_SSO_ALIGNMENT\` 设为 32 | \`sizeof(void_any)\` 会改变 | 保持默认 8 |
| 同时定义 \`VOID_ANY_ENABLE_MEMORY_POOL\` 和 \`VOID_ANY_MEMORY_POOL_NOT_ENABLED\` | 互斥宏冲突 | 二选一 |
| \`LCF_MINIMAL_STACK=1\` 后期望排序性能不变 | 堆分配有额外开销 | 嵌入式场景排序非热路径，可接受 |
| 在 \`config/\` 文件夹外查找配置文件 | \`ecs_config.hpp\` 和 \`void_any_config.hpp\` 均在 \`include/config/\` | include 路径为 \`"config/ecs_config.hpp"\` 和 \`"config/void_any_config.hpp"\` |

---

# 三、各种模块
`
};

