window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['part_generic'] = {
  id: 'part_generic',
  title: "part 通用设施 — 索引池 / id 表 / 可调用体 / 限流 / 位运算 / 时钟",
  category: 'tools',
  icon: 'U',
  order: 58,
  content: `## 58. part 通用设施

以下设施位于 \`part/\`，可被任意模块直接包含。

### 58.1 index_pool — 索引池

固定容量的索引分配器，可跨线程使用。

| 接口 | 说明 |
|------|------|
| \`create(capacity)\` | 创建，容量固定；重复创建返回 \`false\` |
| \`destroy()\` | 释放；销毁后可重新创建 |
| \`allocate(index)\` | 分配一个索引，耗尽返回 \`false\` |
| \`release(index)\` | 归还索引；越界忽略 |
| \`available()\` / \`capacity()\` / \`created()\` | 状态查询 |

\`\`\`cpp
index_pool pool;
pool.create(1024);

std::uint32_t slot = index_pool::none;
if (pool.allocate(slot))
{
    // 使用 slot
    pool.release(slot);
}
\`\`\`

本设施允许跨线程调用；\`part\` 其余容器为单线程语义。

### 58.2 inline_callable — 可调用体

统一的类型擦除可调用体：绑定、调用与释放。

| 接口 | 说明 |
|------|------|
| \`bind(fn)\` | 绑定可调用对象，成功返回 \`true\` |
| \`operator()\` | 调用已绑定的对象 |
| \`reset()\` | 释放绑定 |
| \`operator bool\` | 是否已绑定 |
| \`heap_allocated()\` | 当前绑定是否使用堆存储 |
| \`callable_bind<Size, Align>(storage, fn, invoke, destroy)\` | 绑定到调用方自备的存储 |

\`\`\`cpp
inline_callable<> callback;
callback.bind([]() noexcept { /* ... */ });
callback();
callback.reset();
\`\`\`

### 58.3 keyed_throttle — 键控限流器

按 \`(key, window)\` 计数；同一窗口内超过上限返回 \`false\`。

| 接口 | 说明 |
|------|------|
| \`keyed_throttle<SlotCount>\` | 槽数模板参数，默认 64 |
| \`allow(key, window, limit)\` | 是否放行；\`limit == 0\` 恒拒绝 |
| \`clear()\` | 清空全部槽位 |

\`\`\`cpp
keyed_throttle<64> throttle;
const bool allowed = throttle.allow(/*key*/ 7, /*window*/ 1, /*limit*/ 8);
\`\`\`

### 58.4 bit_ops — 位运算与快速随机

| 接口 | 说明 |
|------|------|
| \`mix_bits(value)\` | 64 位混合哈希 |
| \`count_trailing_zeros(value)\` / \`count_leading_zeros(value)\` | 尾零 / 前导零 |
| \`popcount(value)\` | 置位计数 |
| \`align_up(value, alignment)\` | 向上对齐；\`alignment\` 为 0 时返回原值 |
| \`is_power_of_two(value)\` | 是否为 2 的幂 |
| \`round_up_power_of_two(value)\` | 向上取最近的 2 的幂 |
| \`fast_rand\` | 轻量随机数：构造种子，\`next()\` 取值 |

\`\`\`cpp
const std::uint64_t seed = mix_bits(42);
fast_rand rng{seed};
const std::uint64_t value = rng.next();
\`\`\`

### 58.5 单调时钟

| 接口 | 说明 |
|------|------|
| \`monotonic_now_ns()\` | 单调递增的纳秒时间戳 |

\`\`\`cpp
const std::uint64_t begin = monotonic_now_ns();
// ...
const std::uint64_t elapsed = monotonic_now_ns() - begin;
\`\`\`

### 58.6 id_table — 稠密 id 表

按整型 id 存取条目。

| 接口 | 说明 |
|------|------|
| \`ensure(id, value)\` | 按 id 插入或更新，返回是否新建 |
| \`find(id)\` | 返回条目指针，未注册返回 \`nullptr\` |
| \`index_of(id)\` | 返回下标，未注册返回 \`-1\` |
| \`at(index)\` / \`id_at(index)\` | 按下标取条目 / id |
| \`size()\` / \`empty()\` / \`clear()\` | 状态与清空 |
| \`for_each(f)\` | 遍历 \`f(id, value)\` |

\`\`\`cpp
id_table<field_info> table;
table.ensure(7, field_info{/* ... */});

const field_info* info = table.find(7);
table.for_each([](int id, const field_info& value) { /* ... */ });
\`\`\`

### 58.7 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 把 \`index_pool\` 当通用内存分配器 | 它只分配索引，不管理对象内存 | 索引 + 自备槽数组 |
| 对已释放索引再次 \`release\` | 索引状态被破坏 | 一个索引只归还一次 |
| 用 \`inline_callable\` 按值绑定大对象 | 每次绑定都产生额外存储 | 大对象用引用或指针绑定 |
| 把 \`keyed_throttle\` 当精确计数 | 槽冲突时同槽键会互相重置 | 需要精确计数时自行维护映射 |
| 在 \`bit_ops\` 上传入 0 求尾零 | 结果无意义 | 先判零或使用返回 64 的约定 |
| 把 \`id_table\` 当稀疏映射用 | 它只适合 id 稠密连续的场景 | 先把 id 压缩到稠密范围 |
`
};
