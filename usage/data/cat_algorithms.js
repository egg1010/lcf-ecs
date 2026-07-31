window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['tiered_sort'] = {
  id: 'tiered_sort',
  title: "tiered_sort / pdqsort / sort_n — 分级排序",
  category: 'algorithms',
  icon: 'T',
  order: 22,
  content: `## 21. tiered_sort / pdqsort / sort_n — 分级排序

\`#include "part/tiered_sort.hpp"\`，全局命名空间。所有函数 \`noexcept\`。

### 接口

| 接口 | 签名 | 说明 |
|------|------|------|
| \`pdqsort\` | \`void pdqsort<T>(T* data, size_t n, Compare&& cmp)\` | 3-way pdqsort，要求 \`is_trivially_copyable_v<T>\` |
| \`tiered_sort\` | \`void tiered_sort<T>(T* data, size_t n, Compare&& cmp)\` | 分级排序值数组，按 \`cmp\` 升序 |
| \`tiered_sort_indices\` | \`void tiered_sort_indices<T>(size_t* indices, const T* values, size_t n)\` | 索引排序，按 \`values[indices[i]]\` 升序排列 \`indices\` |
| \`sort_n\` | \`void sort_n<N, T>(T* data, Compare&& cmp)\` | 编译期已知 N 的排序，N≤16 时使用排序网络 |
| \`sort_indices_n\` | \`void sort_indices_n<N, T>(size_t* indices, const T* values)\` | 编译期已知 N 的索引排序 |
| \`sort\` | \`void sort<T>(T* data, size_t n, Compare&& cmp)\` | \`tiered_sort\` 的别名 |
| \`sort_indices\` | \`void sort_indices<T>(size_t* indices, const T* values, size_t n)\` | \`tiered_sort_indices\` 的别名 |

### 分级策略

| 数据量 n | tiered_sort | tiered_sort_indices |
|----------|-------------|---------------------|
| n < 2 | 直接返回 | 直接返回 |
| 2 ≤ n ≤ 16 | 排序网络（Batcher 奇偶归并，编译期生成） | 排序网络 |
| 16 < n < 32 | 插入排序 | 插入排序 |
| 32 ≤ n < 1024 | 3-way pdqsort | 3-way pdqsort |
| 1024 ≤ n < 1M | 基数排序 3-pass + keys 散射 + 4 路子直方图 | 基数排序 3-pass |
| 1M ≤ n < 5M | 基数排序 3-pass + keys 重算 + NT store | 基数排序 3-pass + NT store |
| n ≥ 5M | 基数排序 2-pass(16-16) + NT store + 8 路子直方图 | 基数排序 2-pass + NT store |

- 排序网络使用 \`constexpr\` 在编译期生成 Batcher 奇偶归并网络，运行时无分支、无循环
- \`sort_n<N>\` 在 N≤16 时编译期展开为排序网络，无派发开销
- 基数排序分配失败时自动降级为 pdqsort
- \`pdqsort\` 使用 3-way Dutch National Flag 分区，高效处理重复键

### 使用

\`\`\`cpp
#include "part/tiered_sort.hpp"

// 运行时 n: 分级排序
int data[] = {5, 3, 1, 4, 2};
tiered_sort(data, 5, std::less<int>{});
// data: {1, 2, 3, 4, 5}

// 编译期 N: 排序网络
sort_n<5>(data);  // N=5 已知，直接展开排序网络

// 索引排序
size_t indices[] = {0, 1, 2, 3, 4};
float values[] = {5.0f, 3.0f, 1.0f, 4.0f, 2.0f};
tiered_sort_indices(indices, values, 5);
// indices: {2, 4, 1, 3, 0}  (按 values 升序)

// 编译期 N 索引排序
sort_indices_n<5>(indices, values);

// 直接 pdqsort
pdqsort(data, 5, std::less<int>{});
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对非 trivially_copyable 类型调用 \`pdqsort\` | 编译错误（concept 约束） | 对复杂类型用 \`tiered_sort\`（无此约束） |
| \`tiered_sort_indices\` 传入非算术类型 T | 非算术类型走 pdqsort，无法用基数排序 | 若需基数排序，确保 T 为整数或浮点 |
| \`data\` 或 \`indices\` 为空指针且 n > 0 | 未定义行为 | 确保 n == 0 或指针有效 |
| 排序期间并发读写同一数组 | 数据竞争 | 排序完成后再访问 |
| \`sort_n<N>\` 传入 N=0 | 编译期返回，无操作 | 确保数组实际大小 ≥ N |

---
`
};

window.DOCS_DATA['radix_sort'] = {
  id: 'radix_sort',
  title: "radix_sort — 基数排序",
  category: 'algorithms',
  icon: 'X',
  order: 23,
  content: `## 22. radix_sort — 基数排序

\`#include "part/radix_sort_helper.hpp"\`，全局命名空间。所有函数 \`noexcept\`。

### 接口

| 接口 | 签名 | 说明 |
|------|------|------|
| \`is_radix_sortable_v<T>\` | concept | \`is_integral_v<T> \\|\\| is_floating_point_v<T>\`，T 是否可基数排序 |
| \`radix_key(T val)\` | \`auto\` | 将 T 转换为无序保持的 unsigned 值（负数翻转） |
| \`radix_sort_entries\` | \`void radix_sort_entries<KeyType>(void* entries, size_t n)\` | 排序 \`{KeyType key; size_t index;}\` 数组，按 key 升序 |
| \`radix_sort_indices\` | \`void radix_sort_indices<KeyType>(size_t* indices, const KeyType* keys, size_t n, size_t* temp_buf)\` | 索引基数排序，按 keys[indices[i]] 升序 |

### radix 配置

| 类型 | 趟数 | 每趟位数 | 总桶数 |
|------|------|----------|--------|
| uint32_t / float | 3 | 11-11-10 | 2048+2048+1024 |
| uint64_t / double | 6 | 11-11-11-11-11-9 | 5×2048+512 |

### 使用

\`\`\`cpp
#include "part/radix_sort_helper.hpp"

// entries 排序: 结构体 { int key; size_t index; }
struct entry { int key; size_t index; };
entry entries[5] = {{5,0},{3,1},{1,2},{4,3},{2,4}};
radix_sort_entries<int>(entries, 5);
// entries 按 key 升序

// 索引排序
size_t indices[] = {0, 1, 2, 3, 4};
float values[] = {5.0f, 3.0f, 1.0f, 4.0f, 2.0f};
size_t temp[5];
radix_sort_indices<float>(indices, values, 5, temp);
// indices: {2, 4, 1, 3, 0}
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对非算术/浮点类型调用 \`radix_sort_*\` | 编译错误（concept 约束） | 仅用于 \`int\`/\`uint\`/\`float\`/\`double\` |
| \`radix_sort_indices\` 传入空 \`temp_buf\` | 写入无效内存 | 分配 \`n * sizeof(size_t)\` 字节的 temp 缓冲区 |
| \`radix_sort_entries\` 的 entries 不是 \`{KeyType key; size_t index;}\` 布局 | 内存解释错误 | 确保结构体首字段为 KeyType、次字段为 size_t |
| n = 0 时调用 | 安全返回（n ≤ 1 短路） | 无问题，但无意义 |

---
`
};

