window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['config'] = {
  id: 'config',
  title: "可选宏配置",
  category: 'config',
  icon: '#',
  order: 13,
  content: `## 13. 可选宏配置

### 13.1 栈内存控制（\`config/ecs_config.hpp\`）

嵌入式 / RTOS 环境通过 \`LCF_MINIMAL_STACK\` 关闭栈分配，将各模块的大栈缓冲切换到堆分配。桌面环境默认 \`0\`，保留栈分配。

| 宏 | 默认值 | 说明 |
|------|--------|------|
| \`LCF_MINIMAL_STACK\` | \`0\` | \`1\` 全模块大栈缓冲堆化（排序直方图、流读取、格式化、组件交换、消息历史） |

\`\`\`cmake
# 嵌入式项目在 CMakeLists.txt 中定义
target_compile_definitions(my_target PRIVATE LCF_MINIMAL_STACK=1)

# 或使用项目提供的 CMake 开关
cmake -S . -B build -DLCF_STACK_MINIMAL=ON
\`\`\`

| 受控点 | 栈占用（默认） | 嵌入式回退 |
|--------|---------------|-----------|
| \`radix_count_pass\` bc≤512 分支 | 16KB（h0-h3 局部直方图） | \`::operator new\` 堆分配，失败回退单直方图 |
| \`radix_count_pass\` bc≤1024 分支 | 16KB（h0-h1 局部直方图） | \`::operator new\` 堆分配，失败回退单直方图 |
| \`radix_sort_entries_with_cfg\` count_stack | 16KB（2048 个 size_t） | \`::operator new\` 堆分配 |
| \`radix_sort_indices_with_cfg\` count_stack | 16KB（2048 个 size_t） | \`::operator new\` 堆分配 |
| \`utf8pp operator>>\` 流读取缓冲 | 4KB | 堆分配，失败回退 256B 小块分批读取 |
| \`utf8pp_format\` / \`utf8pp::format\` 格式化缓冲 | 1KB×3 | 探长后 \`std::string\` 堆格式化 |
| \`single_class_set\` 组件交换缓冲 | 256B×2 | \`swap_pool\` 成员交换 / 堆分配（失败回退 64B 分块） |
| \`om_history_push\` 消息历史临时对象 | 262B | \`ring_buffer::emplace_get\` 槽位原地填充（默认配置同样生效） |

### 13.2 void_any 存储策略（\`config/void_any_config.hpp\`）

影响 \`void_any\` 的存储策略与内存分配方式。

| 宏 | 说明 |
|------|------|
| \`VOID_ANY_ENABLE_SSO\` | 启用 void_any 小对象存储（SSO），小对象内联存储 |
| \`VOID_ANY_USE_LAYERED_ALLOCATOR\` | 启用分层分配器，堆路径走 \`memory::layered_allocator\` |
| \`VOID_ANY_SSO_NOT_ENABLED\` | 禁用 SSO（与 \`VOID_ANY_ENABLE_SSO\` 互斥） |
| \`VOID_ANY_LAYERED_ALLOCATOR_NOT_ENABLED\` | 禁用分层分配器，堆路径使用 \`std::malloc\`/\`std::free\` |

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

\`utf8pp\` 堆路径可接入项目分层分配器 \`memory::layered_allocator\`。

\`\`\`cpp
// 选项 1: 禁用分层分配器, 堆路径使用 std::malloc/std::free
// #define UTF8PP_LAYERED_ALLOCATOR_NOT_ENABLED
// 选项 2: 启用分层分配器 (默认)
#define UTF8PP_USE_LAYERED_ALLOCATOR
\`\`\`

### 14.4 配置示例

\`\`\`cpp
// config/void_any_config.hpp

// 启用分层分配器
#define VOID_ANY_USE_LAYERED_ALLOCATOR

// 启用小对象存储
#define VOID_ANY_ENABLE_SSO
\`\`\`

### 13.5 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`LCF_MINIMAL_STACK=1\` 后期望排序性能不变 | 堆分配有额外开销 | 嵌入式场景排序非热路径，可接受 |
| 在 \`config/\` 文件夹外查找配置文件 | \`ecs_config.hpp\` 和 \`void_any_config.hpp\` 均在 \`include/config/\` | include 路径为 \`"config/ecs_config.hpp"\` 和 \`"config/void_any_config.hpp"\` |

---

# 三、各种模块
`
};

