window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['config'] = {
  title: "Macros — Optional configuration",
  content: `## 13. Optional macros

This page lists compile-time macros that can be defined before including the headers. Macros not listed here are internal and are not part of the interface.

### 13.1 Stack memory

| Macro | Default | Description |
|------|--------|------|
| \`LCF_MINIMAL_STACK\` | \`0\` | \`1\`: large stack buffers move to the heap, for stack-constrained environments |

### 13.2 void_any storage policy

| Macro | Description |
|------|------|
| \`VOID_ANY_ENABLE_SSO\` | Enable small-object inline storage (SSO) |
| \`VOID_ANY_USE_LAYERED_ALLOCATOR\` | Use \`memory::layered_allocator\` for heap paths |
| \`VOID_ANY_SSO_NOT_ENABLED\` | Disable SSO (mutually exclusive with \`VOID_ANY_ENABLE_SSO\`) |
| \`VOID_ANY_LAYERED_ALLOCATOR_NOT_ENABLED\` | Disable the layered allocator; heap paths use \`std::malloc\` / \`std::free\` |

### 13.3 Reflection capacity

| Macro | Default | Description |
|------|--------|------|
| \`REFLECT_MAX_FIELDS\` | \`64\` | Upper bound of fields traversed for an aggregate type; supported values are 16/32/64/128/256 |

Type, field and method registration has no fixed count limit and grows with actual registrations. When a type has more than 256 fields, skip automatic traversal and register all fields explicitly with \`add_type\` and \`add_fields\`.

\`\`\`cpp
#define REFLECT_MAX_FIELDS 128
\`\`\`

### 13.4 utf8pp allocator

| Macro | Description |
|------|------|
| \`UTF8PP_USE_LAYERED_ALLOCATOR\` | Use \`memory::layered_allocator\` for heap paths (default) |
| \`UTF8PP_LAYERED_ALLOCATOR_NOT_ENABLED\` | Use \`std::malloc\` / \`std::free\` for heap paths |

### 13.5 Configuration example

\`\`\`cpp
#define LCF_MINIMAL_STACK 1
#define VOID_ANY_ENABLE_SSO
#define VOID_ANY_USE_LAYERED_ALLOCATOR
#define REFLECT_MAX_FIELDS 128
#define UTF8PP_USE_LAYERED_ALLOCATOR

#include "ecs/manager.hpp"
\`\`\`

### 13.6 Notes

| Practice | Result | Recommendation |
|------|------|------|
| Define macros after including headers | No effect | Define all configuration macros before including headers |
| Define both enabling and disabling macros | Compile-time conflict | Define only one of them |
`
};
