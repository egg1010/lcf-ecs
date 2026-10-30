window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['config'] = {
  id: 'config',
  title: "可选宏配置",
  category: 'config',
  icon: '#',
  order: 13,
  content: `## 13. 可选宏配置

本页列出可在包含头文件前定义的编译期宏。未列出的宏属于内部实现，不属于接口。

### 13.1 栈内存

| 宏 | 默认值 | 说明 |
|------|--------|------|
| \`LCF_MINIMAL_STACK\` | \`0\` | \`1\`：大栈缓冲改为堆分配，适用于栈空间受限的环境 |

### 13.2 void_any 存储策略

| 宏 | 说明 |
|------|------|
| \`VOID_ANY_ENABLE_SSO\` | 启用小对象内联存储（SSO） |
| \`VOID_ANY_USE_LAYERED_ALLOCATOR\` | 堆路径使用 \`memory::layered_allocator\` |
| \`VOID_ANY_SSO_NOT_ENABLED\` | 禁用 SSO（与 \`VOID_ANY_ENABLE_SSO\` 互斥） |
| \`VOID_ANY_LAYERED_ALLOCATOR_NOT_ENABLED\` | 禁用分层分配器，堆路径使用 \`std::malloc\` / \`std::free\` |

### 13.3 反射容量

| 宏 | 默认值 | 说明 |
|------|--------|------|
| \`REFLECT_MAX_FIELDS\` | \`64\` | 聚合类型自动遍历的字段数上限，支持 16/32/64/128/256 |

字段与方法数量按实际注册量增长，不设固定上限。字段数超过 256 时不使用自动遍历，改用 \`add_type\` 与 \`add_fields\` 显式注册。

\`\`\`cpp
#define REFLECT_MAX_FIELDS 128
\`\`\`

### 13.4 utf8pp 分配器

| 宏 | 说明 |
|------|------|
| \`UTF8PP_USE_LAYERED_ALLOCATOR\` | 堆路径使用 \`memory::layered_allocator\`（默认） |
| \`UTF8PP_LAYERED_ALLOCATOR_NOT_ENABLED\` | 堆路径使用 \`std::malloc\` / \`std::free\` |

### 13.5 配置示例

\`\`\`cpp
#define LCF_MINIMAL_STACK 1
#define VOID_ANY_ENABLE_SSO
#define VOID_ANY_USE_LAYERED_ALLOCATOR
#define REFLECT_MAX_FIELDS 128
#define UTF8PP_USE_LAYERED_ALLOCATOR

#include "ecs/manager.hpp"
\`\`\`

### 13.6 注意事项

| 做法 | 结果 | 建议 |
|------|------|------|
| 在包含头文件之后定义宏 | 不生效 | 所有配置宏在包含头文件前定义 |
| 同时定义启用与禁用宏 | 编译期冲突 | 只定义其中一个 |
`
};
