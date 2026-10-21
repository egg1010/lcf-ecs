window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['class_pool'] = {
  id: 'class_pool',
  title: "class_pool\\<T> — 核心容器",
  category: 'containers',
  icon: 'P',
  order: 15,
  content: `## 15. class_pool\\<T> — 核心容器

支持密集与稀疏两种存储模式的容器，针对稀疏场景（带空洞的元素管理）。密集场景请使用 \`dense<T>\`。

**两种模式：**

| 模式 | 触发条件 | 迭代行为 |
|------|---------|---------|
| **dense（密集）** | 无空洞（所有槽位均已构造） | 线性扫描 \`[0, size())\` |
| **sparse（稀疏）** | 存在空洞（有未构造的槽位） | 自动跳过未构造槽位，仅遍历已构造元素 |

**模式切换：** 自动判断，无需手动干预。

### 构造与赋值

| 接口 | 说明 |
|------|------|
| \`class_pool()\` | 默认构造 |
| \`class_pool(size_t capacity)\` | 预留容量构造 |
| \`class_pool(size_t count, const T& value)\` | 构造 count 个 value 副本 |
| \`class_pool(InputIt first, InputIt last)\` | 迭代器范围构造 |
| \`class_pool(initializer_list<T>)\` | 初始化列表构造 |
| \`class_pool(const class_pool&)\` | 拷贝构造 |
| \`class_pool(class_pool&&)\` | 移动构造 |
| \`operator=(const class_pool&)\` | 拷贝赋值 |
| \`operator=(class_pool&&)\` | 移动赋值 |

### 元素访问

| 接口 | 说明 |
|------|------|
| \`operator[](size_t)\` | 下标访问（无边界检查） |
| \`get(size_t)\` | 等价于 \`operator[]\`（无边界检查，保留为模板方法便于重载） |
| \`get(size_t, size_t error_index)\` | 越界保护访问：\`index >= size()\` 时改访问 \`error_index\`，\`error_index\` 也越界时回落到 0；空容器触发陷阱 |
| \`front()\` | 首元素引用 |
| \`back()\` | 尾元素引用 |
| \`data()\` | 原始数据指针 |
| \`span()\` | 返回 \`std::span<T>\` |
| \`span() const\` | 返回 \`std::span<const T>\` |

### 容量

| 接口 | 说明 |
|------|------|
| \`size()\` | 已使用大小 |
| \`capacity()\` | 总容量 |
| \`sparse_capacity()\` | 稀疏模式容量（同 capacity） |
| \`empty()\` | 是否为空 |
| \`count()\` | 已构造元素数 |
| \`valid()\` | 是否已分配 |
| \`size_bytes()\` | 已使用字节数 |
| \`capacity_bytes()\` | 总容量字节数 |
| \`max_size()\` | 理论最大元素数 |

### 修改器

| 接口 | 说明 |
|------|------|
| \`emplace_back(Args...)\` | 尾部构造元素 |
| \`push_back(const T&)\` | 尾部拷贝追加（容量不足自动扩容） |
| \`push_back(T&&)\` | 尾部移动追加（容量不足自动扩容） |
| \`push_back_unchecked(const T&)\` | 尾部拷贝追加（调用方保证容量足够） |
| \`push_back_unchecked(T&&)\` | 尾部移动追加（调用方保证容量足够） |
| \`emplace_back_unchecked(Args...)\` | 尾部原地构造（调用方保证容量足够，不自动扩容） |
| \`emplace_back_dense_unchecked(Args...)\` | 尾部原地构造（dense 路径，调用方保证容量足够） |
| \`append_n(n, const T&)\` | 批量追加 n 个 value 副本 |
| \`append_bulk(const T* src, size_t count)\` | 批量拷贝追加 |
| \`append_bulk_move(T* src, size_t count)\` | 批量移动追加 |
| \`append_incrementing(count, counter)\` | 批量追加递增值（counter 起始，要求 trivially copyable） |
| \`append_generated(count, F&& generator)\` | 批量追加生成器产生值 |
| \`emplace(pos, Args...)\` | 在指定位置插入（移动后续元素） |
| \`emplace_at(index, Args...)\` | 任意位置构造（insert-or-assign：已占用则先析构旧对象再重建） |
| \`sparse_emplace_at(index, Args...)\` | 任意位置构造（insert-or-assign：已存在则覆盖） |
| \`sparse_erase_at(index)\` | 稀疏删除（不移动元素，产生空洞） |
| \`soft_sparse_delete(index)\` | 软删除单个（保留对象内存，可填洞复用） |
| \`soft_dense_delete(start, end)\` | 软删除范围（保留对象内存） |
| \`erase(pos)\` | 删除指定位置元素（移动后续元素填补） |
| \`erase(first, last)\` | 删除范围元素 |
| \`pop_back()\` | 删除尾部元素 |
| \`clear()\` | 清空所有元素 |
| \`increase_capacity(capacity)\` | 扩容（只扩容不缩容，不增加元素） |
| \`resize_fill(new_capacity, value)\` | 增长 \`size\` 到 \`new_capacity\` 并以 \`value\` 填充新增位置（只增长不缩容，\`new_capacity <= size\` 时直接返回不销毁任何对象） |
| \`reduce_capacity(capacity)\` | 缩容（截断超出元素） |
| \`reduce_capacity(capacity, dst)\` | 缩容，超出元素移至 dst |
| \`shrink_to_fit()\` | 缩容至 \`size()\` |
| \`reserve_exact(capacity)\` | 精确扩容（分配到精确大小，不增加元素） |
| \`fill_bulk(value, start, count)\` | 从 \`start\` 开始填充 \`count\` 个 \`value\`（自动扩容；非平凡类型会先析构已有对象再重新构造） |
| \`prepare_dense(new_size)\` | 预备密集模式到 \`new_size\`（扩容 + 默认构造新槽位 + 标记为已分配，使容器进入密集模式） |
| \`swap(other)\` | 交换两个容器 |

### 状态查询

| 接口 | 说明 |
|------|------|
| \`is_constructed_at(index)\` | 检查指定位置是否已构造 |
| \`is_dense()\` | 是否处于密集模式（无空洞） |
| \`invalidate_count_cache()\` | 使 count 缓存失效 |

### 各操作对 contiguity 的影响

| 操作 | 对 contiguity 的影响 |
|------|---------------------|
| \`emplace_back()\` / \`push_back()\` | 保持连续 |
| \`emplace(pos)\` / \`insert(pos)\` | 保持连续（元素右移） |
| \`erase(pos)\` / \`erase(first,last)\` | 保持连续（元素左移） |
| \`pop_back()\` | 保持连续 |
| \`clear()\` | 重置为连续 |
| \`sparse_erase_at(index)\` | **产生空洞** → 变稀疏 |
| \`emplace_at(index)\` | **可能填充空洞** → 可能变连续 |
| \`sparse_emplace_at(index)\` | **可能填充空洞** → 可能变连续 |
| \`reduce_capacity()\` 缩小 | **可能消除空洞** → 可能变连续 |

### 插入/删除

| 接口 | 说明 |
|------|------|
| \`emplace(const_iterator pos, args...)\` | 在指定位置原位构造，返回指向新元素的迭代器 |
| \`insert(const_iterator pos, const T&)\` | 拷贝插入 |
| \`insert(const_iterator pos, T&&)\` | 移动插入 |
| \`erase(const_iterator pos)\` | 删除指定位置元素，返回下一个迭代器 |
| \`erase(const_iterator first, const_iterator last)\` | 范围删除，返回下一个迭代器 |

### 迭代器

| 接口 | 说明 |
|------|------|
| \`begin()\` / \`end()\` | 正向迭代器（dense 模式直接指针遍历，sparse 模式自动跳过未构造槽位） |
| \`cbegin()\` / \`cend()\` | const 版本 |
| \`rbegin()\` / \`rend()\` | 反向迭代器（sparse 模式同样自动跳过未构造槽位） |
| \`crbegin()\` / \`crend()\` | const 反向版本 |
| \`for_each(F&& f)\` / \`for_each(F&& f) const\` | 遍历所有元素，调用 \`f(v)\` |

### 遍历与视图成员函数

与 \`dense<T>\` 对齐的成员函数接口，稀疏模式自动跳过空洞。

#### 遍历与反向

| 接口 | 说明 |
|------|------|
| \`for_each(F&& f)\` / \`for_each(F&& f) const\` | 遍历所有元素，调用 \`f(v)\` |
| \`reverse_for_each(F&& f)\` / \`reverse_for_each(F&& f) const\` | 反向遍历，调用 \`f(v)\` |

#### 子范围视图

返回 \`std::span\` 视图，不复制数据。

| 接口 | 说明 |
|------|------|
| \`subspan(offset, count)\` | 返回 \`[offset, offset+count)\` 的 span，自动截断到 \`size()\` |
| \`subspan(offset)\` | 返回 \`[offset, size())\` 的 span |
| \`first(n)\` | 前 \`n\` 个元素 |
| \`last(n)\` | 后 \`n\` 个元素 |
| \`first_fixed<N>()\` | 前最多 \`N\` 个元素，返回 \`std::span<T>\`（不足 \`N\` 时截断到实际长度） |
| \`last_fixed<N>()\` | 后最多 \`N\` 个元素，返回 \`std::span<T>\`（不足 \`N\` 时截断到实际长度） |

所有接口均提供 const 重载。模板方法调用需 \`template\` 关键字：\`p.template first_fixed<8>()\`。

#### 步进视图

| 接口 | 说明 |
|------|------|
| \`strided_span_view(start, step, count)\` | 返回 \`pool_strided_span<T>\`，持有 \`{class_pool 指针, 步长, 数量}\`；\`start\` 越界归零、\`step=0\` 视为空视图、\`count\` 收敛到可用槽位数 |
| \`strided_for_each(start, step, F&& f)\` | 运行时步长遍历，调用 \`f(v)\` |
| \`strided_for_each<Step>(F&& f)\` | 编译期步长遍历，\`Step=1\` 退化为 \`for_each\` |

#### 变换视图

| 接口 | 说明 |
|------|------|
| \`transform_for_each(FTransform&& tr, FConsume&& con)\` | 对每个元素 \`v\` 调用 \`con(tr(v))\` |
| \`transform_to(R* dst, count, F&& tr)\` | 将 \`tr(v)\` 写入 \`dst\` |

#### 查找与过滤

| 接口 | 说明 |
|------|------|
| \`find(const T& value)\` | 线性查找，返回首命指针，未命中返回 \`nullptr\` |
| \`find_if(Pred pred)\` | 谓词查找 |
| \`find_if_not(Pred pred)\` | 谓词反查找 |
| \`contains(const T& value)\` | 是否包含 |
| \`count_if(Pred pred)\` | 谓词计数 |
| \`filter_for_each(Pred pred, F&& f)\` | 仅对满足 \`pred(v)\` 的元素调用 \`f(v)\` |
| \`filter_indices_to(class_pool<size_t>& dst, Pred pred)\` | 将满足谓词的索引追加到 \`dst\` |

#### 规约与极值

| 接口 | 说明 |
|------|------|
| \`reduce(F&& f, U init)\` | 顺序规约：\`acc = f(acc, v)\` |
| \`reduce_pairwise(F&& f, U init)\` | 成对规约，减少关键路径深度 |
| \`min_element()\` / \`max_element()\` | 返回最小/最大元素指针 |
| \`minmax_element()\` | 返回 \`{min_ptr, max_ptr}\` |
| \`sum()\` | 算术求和（要求 \`is_arithmetic_v<T>\`） |
| \`dot_product(const U* other, count)\` | 点积（要求 \`is_arithmetic_v<T>\`） |

#### 窗口与分块

| 接口 | 说明 |
|------|------|
| \`for_each_window<N>(F&& f)\` | 滑动窗口遍历，对每个 \`[i, i+N)\` 调用 \`f(std::span<T, N>)\` |
| \`for_each_chunk<N>(F&& f)\` | 不重叠分块遍历，对每个 \`[i*N, (i+1)*N)\` 调用 \`f(std::span<T, N>)\` |
| \`window_span<N>(offset)\` | 取偏移 \`offset\` 处的滑动窗口 span（\`std::span<T>\`，不足 \`N\` 时截断） |
| \`chunk_span<N>(chunk_idx)\` | 取第 \`chunk_idx\` 个不重叠分块 span（\`std::span<T>\`，不足 \`N\` 时截断） |

#### 枚举与双容器同步

| 接口 | 说明 |
|------|------|
| \`for_each_enumerated(F&& f)\` | 带索引遍历，调用 \`f(index, value)\` |
| \`for_each_zip(class_pool<U>& other, F&& f)\` | 同步遍历两个 \`class_pool\`，调用 \`f(x, y)\` |
| \`for_each_zip(U* other, count, F&& f)\` | \`class_pool\` + 裸指针同步 |
| \`for_each_zip(std::span<U> other, F&& f)\` | span 版本 |
| \`zip_with_to(R* dst, const U* other, count, F&& f)\` | 将 \`f(x, y)\` 写入 \`dst\`（SoA→AoS） |

#### 相等比较

| 接口 | 说明 |
|------|------|
| \`equal(const T* other, count)\` | 逐元素相等比较 |
| \`equal(const class_pool<U>& other)\` | class_pool 版本 |
| \`equal(std::span<const U> other)\` | span 版本 |

#### 对齐与 SIMD

| 接口 | 说明 |
|------|------|
| \`aligned_data()\` | 返回对齐到缓存行的数据指针 |
| \`aligned_span()\` | 返回对齐 span |
| \`simd_for_each(F&& f)\` | 遍历（要求 \`is_trivially_copyable_v<T>\`，sizeof ≤ 32），稀疏退化为 \`for_each\` |
| \`unaligned_tail_offset()\` | 返回无法对齐处理的尾部起始偏移 |

#### 拷贝与移动

| 接口 | 说明 |
|------|------|
| \`copy_to(T* dst, count)\` / \`copy_to(std::span<T> dst)\` | 批量拷贝 |
| \`move_to(T* dst, count)\` / \`move_to(std::span<T> dst)\` | 批量移动 |
| \`reverse_copy_to(T* dst, count)\` / \`reverse_copy_to(std::span<T> dst)\` | 反向拷贝 |

#### class_pool 独有

| 接口 | 说明 |
|------|------|
| \`compact_to(T* dst, count)\` | 压缩稀疏池为密集数组（消除空洞），返回写入元素数 |
| \`live_count()\` | 活跃元素数（等价 \`count()\`） |
| \`holes_count()\` | 空洞数 = \`size() - count()\` |

### 自由函数

| 接口 | 说明 |
|------|------|
| \`swap(class_pool&, class_pool&)\` | 交换两个容器 |

### 使用

\`\`\`cpp
class_pool<int> pool;
pool.emplace_back(10);
pool.emplace_back(20);

// push_back 拷贝/移动追加
int v = 30;
pool.push_back(v);                  // 拷贝追加
pool.push_back(std::move(v));       // 移动追加

// 任意位置构造（insert-or-assign：已占用则销毁旧对象并重建）
pool.emplace_at(3, 999);       // 索引 3 已占用，覆盖为 999
pool.emplace_at(3, 888);       // 再次覆盖为 888
pool.sparse_emplace_at(3, 777);  // 同为覆盖语义，覆盖为 777

// 稀疏删除
pool.sparse_erase_at(100);
pool.is_dense();                   // false（索引 100 处产生空洞）

// 填满空洞后自动切回连续
pool.emplace_at(100, 123);         // 填充空洞
pool.is_dense();                   // true（空洞消失，自动切回）

// unchecked 快速追加（调用方保证容量足够，不自动扩容）
class_pool<int> dense_pool;
dense_pool.emplace_back(1);
dense_pool.emplace_back(2);
dense_pool.push_back_unchecked(3);
dense_pool.push_back_unchecked(std::move(v));  // 移动追加
dense_pool.emplace_back_unchecked(4);
dense_pool.emplace_back_dense_unchecked(5);

// 批量插入：reserve + unchecked 组合
class_pool<int> batch_pool;
batch_pool.reserve_exact(1'000'000);
for (int i = 0; i < 1'000'000; ++i) {
    batch_pool.emplace_back_dense_unchecked(i);
}

// 批量插入：append_n 单次调用批量追加
class_pool<int> fastest;
fastest.append_n(1'000'000, 0);  // 直接追加 1M 个 0

// 精确扩容（不增加元素，分配到精确大小）
class_pool<int> reserved;
reserved.reserve_exact(1000);        // capacity >= 1000，size 不变
reserved.resize_fill(10, 0);   // size 增长到 10 并填充 0（只增长不缩容）

// 位置插入
pool.emplace(std::next(pool.begin(), 1), 42);  // 在位置 1 插入

// 迭代
for (auto v : pool) { /* ... */ }

// span
std::span<int> s = pool.span();

// get(): 等价 operator[], 无边界检查
int& a = pool.get(0);

// get(index, error_index): 越界保护访问
// 若 index >= size() 则改访问 error_index 位置, 避免越界 UB
int& b = pool.get(maybe_invalid_idx, 0);

// 稀疏集查询
pool.is_constructed_at(100);  // true
pool.is_dense();              // 检查是否连续
\`\`\`

### 应该用什么操作？

| 场景 | 推荐操作 | 原因 |
|------|---------|------|
| 尾部追加元素（已有值对象） | \`push_back(value)\` | O(1)，保持连续 |
| 尾部追加元素（直接构造） | \`emplace_back(args...)\` | O(1)，保持连续，原地构造避免临时对象 |
| 任意位置插入/删除并保持连续 | \`emplace(pos)\` / \`erase(pos)\` | 移动后续元素，O(n)，保持连续 |
| 稀疏数组（大索引跳跃） | \`emplace_at()\` / \`sparse_erase_at()\` | O(1)，不移动其他元素，但产生空洞 |
| 批量填充已知索引 | \`emplace_at()\` | 填充空洞后自动切回连续 |
| 删除整个容器 | \`clear()\` | 重置为连续 |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`sparse_erase_at()\` 后仍期望连续迭代 | 产生空洞，迭代变稀疏模式 | 用 \`emplace_at()\` 填充空洞，或用 \`erase()\` 替代 |
| 频繁 \`sparse_erase_at()\` + \`emplace_at()\` 来回切换 | 每次切换触发模式扫描 | 批量操作，或统一使用 \`erase()\`/\`emplace()\` 保持连续 |
| \`emplace_at()\` 在远超 \`size()\` 的索引上构造 | 中间留大量未初始化槽位，\`size()\` 暴增 | 用 \`resize_fill(n, value)\` 预填充，或改用 \`sparse_emplace_at()\` |
| 在 sparse 模式下使用 \`data()\` + \`span()\` 做线性遍历 | 未初始化槽位包含垃圾数据 | 始终通过迭代器遍历，或先确认 \`is_dense()\` 为 true |
| \`increase_capacity(n)\` 期望精确分配到 n | 实际容量可能大于 n | 精确分配用 \`reserve_exact(n)\` |
| \`emplace_at()\` 期望保留旧值 | \`emplace_at\` 是 insert-or-assign，重复构造会销毁旧对象 | 需要保留时先用 \`is_constructed_at()\` 检查 |
| 未确认容量就调用 \`push_back_unchecked\` / \`emplace_back_unchecked\` | 越界写崩（不自动扩容） | 先 \`reserve_exact\` / \`increase_capacity\`，或用 \`push_back()\` / \`emplace_back()\` |
| 软删除后对象仍占内存 | \`soft_sparse_delete\` / \`soft_dense_delete\` 保留对象不析构 | 用 \`sparse_erase_at()\` 真正析构，或 \`emplace_at()\` / \`fill_the_hole()\` 填洞复用 |

### fill_the_hole — 填洞或追加

\`fill_the_hole\` 优先填第一个空洞（最低索引），无洞则 \`emplace_back\` 末尾追加。

#### 接口

| 接口 | 说明 |
|------|------|
| \`fill_the_hole(args...)\` | 填第一个空洞或末尾追加，返回 \`T&\` |
| \`fill_the_hole_at(args...)\` | 同 \`fill_the_hole\` 语义，但返回被填补位置的索引 \`size_t\`（填洞返回洞索引，追加返回末尾索引），调用方可通过 \`operator[](idx)\` 访问元素 |

填洞依赖现有接口：\`sparse_erase_at\` 产生空洞、\`emplace_at\` 填洞、\`emplace_back\` 追加。

#### 使用

\`\`\`cpp
class_pool<int> pool;
pool.fill_the_hole(10);   // index 0
pool.fill_the_hole(20);   // index 1
pool.fill_the_hole(30);   // index 2

pool.sparse_erase_at(1);  // 产生空洞
pool.fill_the_hole(99);   // 填到 index 1 (最低空洞)

// 多空洞: 填最低索引
pool.sparse_erase_at(0);
pool.sparse_erase_at(2);
pool.fill_the_hole(7);    // 填到 index 0 (最低)
pool.fill_the_hole(8);    // 填到 index 2

// 迭代跳过空洞
pool.sparse_erase_at(1);
for (int& v : pool) { /* 跳过 index 1 */ }

// fill_the_hole_at: 返回被填补位置的索引, 便于后续直接访问
class_pool<int> pool2;
pool2.fill_the_hole_at(10);  // 无洞 → 追加, 返回 0
pool2.fill_the_hole_at(20);  // 无洞 → 追加, 返回 1
pool2.sparse_erase_at(0);    // 产生空洞 at 0
size_t idx = pool2.fill_the_hole_at(99);  // 填洞 at 0, 返回 0
// idx == 0, pool2[idx] == 99
\`\`\`

#### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 期望 \`fill_the_hole\` 填最近删除的空洞 | 最低索引优先，非 LIFO | 如需 LIFO 顺序，自行维护栈 |
| 混用 compacting \`erase(iterator)\` | 索引前移导致空洞位置变化 | 填洞场景用 \`sparse_erase_at\` |
| \`sparse_erase_at\` 后期望 \`is_dense()\` 为 true | 产生空洞变稀疏 | \`fill_the_hole\` 填满后自动恢复 |
| 对已删除位置重复 \`sparse_erase_at\` | 已删除位置不重复计数 | 删除前可 \`is_constructed_at\` 检查 |

---
`
};

window.DOCS_DATA['class_pool_views'] = {
  id: 'class_pool_views',
  title: "容器视图",
  category: 'containers',
  icon: 'P',
  order: 16,
  content: `## 15.5 容器视图

通用视图接口，全局命名空间。基于 \`viewable_container\` 概念约束，\`dense<T>\` 与 \`class_pool<T>\` 共用同一套视图函数；\`class_pool<T>\` 另有专属视图。

- **不修改原容器**：仅依赖容器公开 API
- **不复制数据**：视图只持有指针与长度
- **全 \`noexcept\`**：与原容器约束一致

### 通用视图接口

\`viewable_container\` 概念要求容器提供 \`data()\` / \`size()\` / \`is_dense()\` / \`is_constructed_at(i)\` / \`begin()\` / \`end()\` / \`operator[]\` / \`count()\` / \`span()\` / \`rbegin()\` / \`rend()\`。\`dense<T>\` 与 \`class_pool<T>\` 均满足此概念。

| 分类 | 接口 | 说明 |
|------|------|------|
| **遍历** | \`for_each(c, f)\` | 遍历所有活跃元素，调用 \`f(v)\` |
|  | \`reverse_for_each(c, f)\` | 反向遍历，调用 \`f(v)\` |
|  | \`for_each_enumerated(c, f)\` | 带索引遍历，调用 \`f(index, v)\` |
| **子范围** | \`subspan(c, off, cnt)\` / \`subspan(c, off)\` | 返回 \`std::span<T>\`，自动截断到 \`size()\`（与 \`dense::subspan\` / \`std::span::subspan\` 命名一致） |
|  | \`first(c, n)\` / \`last(c, n)\` | 前/后 \`n\` 个元素的 span |
|  | \`first_fixed<N>(c)\` / \`last_fixed<N>(c)\` | 前/后最多 \`N\` 个元素，返回 \`std::span<T>\`（不足 \`N\` 时截断到实际长度） |
| **步进** | \`strided_span_view(c, start, step, cnt)\` | \`class_pool\` 返回 \`pool_strided_span<T>\`；\`dense\` 返回 \`strided_span<T>\`；两者均按容器实际范围收敛（越界 \`start\` / \`step=0\` 得到空视图） |
|  | \`strided_for_each(c, start, step, f)\` | 运行时步长遍历 |
|  | \`strided_for_each<Step>(c, f)\` | 编译期步长遍历，\`Step=1\` 退化为 \`for_each\` |
| **变换** | \`transform_for_each(c, tr, con)\` | 对每个 \`v\` 调用 \`con(tr(v))\`，避免中间临时容器 |
|  | \`transform_to(c, dst, n, tr)\` | 将 \`tr(v)\` 写入 \`dst\`，要求 \`n <= c.size()\`，\`dst\` 必须为未构造内存 |
| **查找过滤** | \`find(c, v)\` / \`find_if(c, pred)\` / \`find_if_not(c, pred)\` | 线性查找，返回首命指针，未命中返回 \`nullptr\` |
|  | \`contains(c, v)\` | 是否包含 |
|  | \`count_if(c, pred)\` | 条件计数 |
|  | \`filter_for_each(c, pred, f)\` | 仅对满足 \`pred(v)\` 的元素调用 \`f(v)\` |
| **规约** | \`reduce(c, f, init)\` | 顺序规约：\`acc = f(acc, v)\` |
|  | \`reduce_pairwise(c, f, init)\` | 成对规约，减少关键路径深度 |
|  | \`min_element(c)\` / \`max_element(c)\` / \`minmax_element(c)\` | 返回极值指针 |
|  | \`sum(c)\` | 算术求和（要求 \`is_arithmetic_v<T>\`） |
|  | \`dot_product(c, other, n)\` | 点积（要求 \`is_arithmetic_v<T>\`） |
| **窗口/分块** | \`for_each_window<N>(c, f)\` | 滑动窗口遍历，对每个 \`[i, i+N)\` 调用 \`f(std::span<T, N>)\` |
|  | \`for_each_chunk<N>(c, f)\` | 不重叠分块遍历 |
|  | \`window_span<N>(c, off)\` / \`chunk_span<N>(c, idx)\` | 取滑动窗口 / 分块 span（\`std::span<T>\`，不足 \`N\` 时截断） |
| **双容器** | \`for_each_zip(a, b, f)\` | 双容器同步遍历，调用 \`f(x, y)\` |
|  | \`for_each_zip(a, ptr, n, f)\` | 容器 + 裸指针同步 |
|  | \`zip_with_to(a, b, dst, n, f)\` | 将 \`f(x, y)\` 写入 \`dst\`（\`dst\` 必须为未构造内存） |
|  | \`equal(a, b)\` / \`equal(a, ptr, n)\` / \`equal(a, span)\` | 相等性比较 |
| **对齐** | \`aligned_data(c)\` / \`aligned_span(c)\` | 返回对齐裸指针 / span |
|  | \`simd_for_each(c, f)\` | 遍历（要求 \`is_trivially_copyable_v<T>\` 且 \`sizeof(T) ≤ 32\`） |
|  | \`unaligned_tail_offset(c)\` | 32 字节对齐处理后的尾部偏移 |
| **拷贝/移动** | \`copy_to(c, dst, n)\` / \`copy_to(c, span)\` | 批量拷贝（\`dst\` 必须为未构造内存） |
|  | \`move_to(c, dst, n)\` / \`move_to(c, span)\` | 批量移动（\`dst\` 必须为未构造内存） |
|  | \`reverse_copy_to(c, dst, n)\` / \`reverse_copy_to(c, span)\` | 反向拷贝（\`dst\` 必须为未构造内存） |

### class_pool 专属视图

\`class_pool<T>\` 的专属视图接口。

| 接口 | 说明 |
|------|------|
| \`filter_indices_to(pool, dst, pred)\` | 将满足 \`pred(v)\` 的索引追加到 \`class_pool<size_t>& dst\` |
| \`compact_to(pool, dst, n)\` | 压缩稀疏池为密集数组（消除空洞），活跃元素连续写入 \`dst\`，返回写入数 |
| \`live_count(pool)\` | 活跃元素数（等价 \`pool.count()\`） |
| \`holes_count(pool)\` | 空洞数 = \`pool.size() - pool.count()\` |

### 使用示例

\`\`\`cpp
// 视图接口位于全局命名空间, 无需 using namespace

class_pool<POD32> pool;
for (size_t i = 0; i < 1000; ++i) pool.push_back_unchecked({static_cast<float>(i)});

// 子范围遍历
auto sp = subspan(pool, 100, 50);
for (auto& v : sp) { /* ... */ }

// 编译期步长遍历 (步长 4)
strided_for_each<4>(pool, [](POD32& v) { /* ... */ });

// 查找与计数
POD32 target{42.0f};
POD32* p = find(pool, target);
size_t n = count_if(pool, [](const POD32& v) { return v.a[0] > 0; });

// 规约
POD32 sum = reduce(pool, [](POD32 acc, const POD32& v) -> POD32 { /* ... */ }, POD32{});

// 压缩稀疏池为密集数组
pool.sparse_erase_at(5);
POD32* compact = /* ... 分配内存 ... */;
size_t live = compact_to(pool, compact, pool.size());
// compact[0..live) 为连续活跃元素

// dense 同样适用
dense<float> d(1000);
for (size_t i = 0; i < 1000; ++i) d[i] = static_cast<float>(i);
d.for_each([](float& v) { v *= 2.0f; });
strided_for_each<4>(d, [](float& v) { /* ... */ });
\`\`\`

### 稀疏模式行为

\`class_pool<T>\` 稀疏模式（\`is_dense() == false\`）下视图行为：
- **子范围/窗口/分块**：直接切片，**不跳过空洞**，调用方需自行 \`is_constructed_at(i)\` 检查
- **步进遍历**：步进槽位（非活跃元素），越过空洞时自动跳过
- **过滤/查找/规约/zip**：复用 \`basic_iterator\`，**自动跳过空洞**（仅遍历活跃元素）
- **compact_to**：按活跃顺序压缩写入

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 稀疏模式用 \`subspan\` 后直接遍历 | 包含未活跃槽 | 用 \`for_each\` / \`filter_for_each\` 走迭代器 |
| \`simd_for_each\` 用于稀疏模式 | 退化为 \`for_each\` | 仅密集模式适用 |
| \`compact_to\` 后期望源池被清空 | 仅拷贝，源池不变 | 调用方自行 \`clear()\` |
| 视图持有期间修改容器 | 迭代器/指针失效 | 视图生命周期 = 容器稳定期 |
| 期望 \`last_fixed<N>\` 越界返回空 span | 返回动态 \`std::span<T>\`，不足 \`N\` 时截断到实际长度 | 检查 \`span.size()\` 后使用 |

---
`
};

window.DOCS_DATA['void_any'] = {
  id: 'void_any',
  title: "void_any — 类型擦除存储",
  category: 'containers',
  icon: 'A',
  order: 17,
  content: `## 16. void_any — 类型擦除存储

无命名空间。所有接口 \`noexcept\`。类型擦除容器，可存储任意类型对象，运行时通过 \`get_ptr<T>()\` 按类型安全取值。

\`void_any\` 是类模板 \`define_void_any<SsoSize, SsoAlign>\` 的预设别名，SSO 缓冲区大小与对齐由模板参数编译期确定。需要更大内联存储时可直接实例化模板。

**模板参数：**

| 参数 | 类型 | 约束 | 说明 |
|------|------|------|------|
| \`SsoSize\` | \`size_t\` | 8 的倍数 | SSO 缓冲区字节数，决定内联存储上限 |
| \`SsoAlign\` | \`size_t\` | ≥ 8 | SSO 缓冲区对齐值 |

**预设别名：**

| 别名 | 模板实参 | 说明 |
|------|---------|------|
| \`void_any\` | \`define_void_any<56, 8>\` | 默认通用别名 |

**\`SsoSize\` 取值参考（\`SsoAlign\` 保持 8）：**

| \`SsoSize\` | \`sizeof\` | 内联存储上限 | 适用场景 |
|-----------|----------|------------|---------|
| \`8\` | 16B | ≤ 8B | 仅存小标量（int/double/指针） |
| \`24\` | 32B | ≤ 24B | 小对象，内存敏感场景 |
| \`56\` | 64B | ≤ 56B | 默认，通用场景 |
| \`120\` | 128B | ≤ 120B | 需内联存储较大对象（如 \`std::string\`） |
| \`248\` | 256B | ≤ 248B | 大对象内联存储 |

> 注：\`sizeof\` = \`SsoSize\` + 8（类型标记字段）。超出内联上限的对象自动转为堆分配，功能不受影响。

**\`SsoAlign\` 取值参考：**

| \`SsoAlign\` | 说明 |
|------------|------|
| \`8\` | 默认，适配大多数类型 |
| \`16\` | 需要 16 字节对齐的类型（如部分 SIMD 类型） |
| \`32\` | 需要 32 字节对齐的类型（如 AVX 向量） |

> 注：\`SsoAlign\` 过大可能增加 \`sizeof\`（padding 填充），无特殊对齐需求时保持 \`8\`。

### 构造与赋值

| 接口 | 说明 |
|------|------|
| \`void_any()\` | 默认构造，空值 |
| \`void_any(T&&)\` | 从任意类型构造（排除 \`void_any\` 自身） |
| \`void_any(const void_any&)\` | 拷贝构造 |
| \`void_any(void_any&&)\` | 移动构造 |
| \`operator=(const void_any&)\` | 拷贝赋值 |
| \`operator=(void_any&&)\` | 移动赋值 |

### 访问与操作

| 接口 | 说明 |
|------|------|
| \`set(T&&)\` | 设置新值（同类型直接覆盖；不同类型先析构旧值再构造新值） |
| \`type_id()\` | 获取类型 ID（空值返回 -1；仅用于同类型一致性比较，不等于 \`type_id::get_type_id<T>()\`） |
| \`get_ptr<T>()\` | 获取指针（带类型检查，不匹配返回 nullptr） |
| \`get_ptr<T>() const\` | const 版本 |
| \`fast_get_ptr<T>()\` | 获取指针（跳过 type_id 检查） |
| \`fast_get_ptr<T>() const\` | const 版本 |
| \`get_ptr_unchecked<T>()\` | 获取指针（不验证 has_value 和 type_id） |
| \`get_ptr_unchecked<T>() const\` | const 版本 |
| \`get<T>()\` | 获取值副本（空值或类型不匹配返回默认构造） |
| \`get_void()\` | 获取 \`void*\`（空值返回 nullptr） |
| \`get_void() const\` | const 版本 |
| \`copy_from<T>(const T&)\` | 拷贝赋值（编译期已知 T） |
| \`move_from<T>(T&&)\` | 移动赋值（编译期已知 T） |
| \`has_value()\` | 是否有值 |
| \`reset()\` | 清空（析构并置空） |
| \`swap(void_any&)\` | 同布局交换（相同 \`SsoSize\`/\`SsoAlign\` 的两个 \`void_any\` 交换内容） |
| \`cross_layout_swap(define_void_any<SsoSize2, SsoAlign2>&)\` | 跨布局交换（不同 \`SsoSize\`/\`SsoAlign\` 的 \`void_any\` 交换内容，空方接管对方值） |

> 注：判断存储类型是否为 T 时，使用 \`get_ptr<T>() != nullptr\`，不要用 \`type_id() == type_id::get_type_id<T>()\`。

### 使用

\`\`\`cpp

// 基本构造
void_any a(42);                     // 存储 int
void_any b(std::string("hello"));   // 存储 std::string
void_any c;                         // 空值

// 状态查询
a.has_value();                      // true
c.has_value();                      // false
a.type_id();                        // 类型标识 (空值返回 -1)

// 取值 (带类型检查)
int* pi = a.get_ptr<int>();         // 匹配, 返回指针
double* pd = a.get_ptr<double>();   // 不匹配, 返回 nullptr
int val = a.get<int>();             // 获取值副本 (空值或类型不匹配返回默认构造)

// void* 访问
void* vp = a.get_void();            // 返回 void* (空值返回 nullptr)

// 设置新值 (同类型直接覆盖; 不同类型先析构旧值再构造新值)
a.set(99);                          // int 42 → 99 (同类型覆盖)
a.set(std::string("world"));        // int 99 → std::string (不同类型, 析构+构造)

// 清空
a.reset();                          // 析构并置空
a.has_value();                      // false

// 拷贝与移动
void_any src(std::string("data"));
void_any cp(src);                   // 拷贝构造
void_any mv(std::move(src));        // 移动构造 (src 变空)

void_any dst;
dst = cp;                           // 拷贝赋值
dst = std::move(mv);                // 移动赋值 (mv 变空)
\`\`\`

### 编译期已知类型的接口

\`\`\`cpp
void_any a;

// copy_from / move_from: 编译期已知 T, 无需运行时类型推导
a.copy_from(42);                    // 拷贝 int
a.move_from(std::string("x"));      // 移动 string

// fast_get_ptr: 跳过 type_id 检查 (需调用方确保类型正确)
int* p = a.fast_get_ptr<int>();

// get_ptr_unchecked: 不验证 has_value 和 type_id (需调用方确保值存在)
int* pu = a.get_ptr_unchecked<int>();
\`\`\`

### 存储自定义类型

\`\`\`cpp
struct player {
    int hp;
    int mp;
};

void_any a(player{100, 50});
player* p = a.get_ptr<player>();
if (p) {
    p->hp -= 10;
}
\`\`\`

### 在容器中存储

\`\`\`cpp

dense<void_any> bag;
bag.push_back(42);
bag.push_back(std::string("item"));
bag.push_back(3.14);

// 遍历并按类型取值
for (size_t i = 0; i < bag.size(); ++i) {
    if (auto* p = bag[i].get_ptr<int>()) {
        // 处理 int
    } else if (auto* p = bag[i].get_ptr<std::string>()) {
        // 处理 string
    } else if (auto* p = bag[i].get_ptr<double>()) {
        // 处理 double
    }
}
\`\`\`

### 类型一致性比较

\`\`\`cpp
void_any x(1), y(2), z(std::string("a"));
x.type_id() == y.type_id();        // true, 同为 int
x.type_id() == z.type_id();        // false, int 与 string 不同
\`\`\`

> 注：\`type_id()\` 仅用于同类型一致性比较，不等于 \`type_id::get_type_id<T>()\`。判断存储类型是否为 T 时，使用 \`get_ptr<T>() != nullptr\`。

### 自定义 SSO 配置

\`\`\`cpp

// 默认 void_any: 56B SSO, 可内联存储 ≤56B 的对象
void_any a(42);

// 自定义更大 SSO 缓冲, 内联存储更大对象避免堆分配
// 例如需要内联存储 100B 的结构体
struct big_data { char buf[100]; };

using my_void_any = define_void_any<120, 8>;   // 120B SSO
my_void_any b(big_data{});

// 自定义对齐值 (需 ≥ 8)
using aligned_any = define_void_any<56, 16>;   // 16 字节对齐
aligned_any c(3.14);

// 注意: 不同模板实参实例化的类型不兼容, 不能互相赋值
// my_void_any x = a;   // 编译错误
\`\`\`

### 跨布局交换

\`\`\`cpp

// 不同 SsoSize 的 void_any 之间交换内容
void_any a(42);                              // define_void_any<56, 8>
define_void_any<120, 8> b(std::string("hi")); // 120B SSO

// 跨布局交换: a 接收 string, b 接收 int
a.cross_layout_swap(b);

// 双方类型和值都已互换
auto* pa = a.get_ptr<std::string>();   // 匹配
auto* pb = b.get_ptr<int>();           // 匹配

// 空值参与: 空方接管对方的值
void_any empty;
void_any has_val(99);
empty.cross_layout_swap(has_val);     // empty 持有 99, has_val 变空

// 同布局交换
void_any x(1), y(2);
x.swap(y);                             // x=2, y=1
\`\`\`

> 注：\`cross_layout_swap\` 用于不同 \`SsoSize\`/\`SsoAlign\` 实例化的 \`void_any\` 之间交换内容。双方任一为空时，空方接管对方的值。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 使用 \`get_ptr_unchecked\` 前不检查 \`has_value()\` 和类型 | 返回悬垂指针，未定义行为 | 仅在确定类型和值存在时使用，否则用 \`get_ptr<T>()\` |
| 依赖 \`get<T>()\` 返回默认值来判断类型 | 默认构造值可能与实际值相同 | 先用 \`get_ptr<T>()\` 检查指针是否为空 |
| 移动后继续使用 | 移动后源对象为空 | 移动后仅可调用 \`reset()\` 或重新赋值 |
| 在 \`set()\` 之前访问 | \`has_value()\` 为 false，get_ptr 返回 nullptr | 先 \`set()\` 或构造时传值 |
| 用 \`type_id() == type_id::get_type_id<T>()\` 判断类型 | 两者不相等 | 用 \`get_ptr<T>() != nullptr\` 判断类型 |
| \`SsoSize\` 非 8 的倍数 | \`static_assert\` 编译失败 | \`SsoSize\` 必须为 8 的倍数 |
| \`SsoAlign\` 小于 8 | \`static_assert\` 编译失败 | \`SsoAlign\` 必须 ≥ 8 |
| 不同模板实参的 \`define_void_any\` 互相赋值 | 类型不兼容，编译错误 | 同类型间赋值，或通过 \`cross_layout_swap\` 交换内容 |

---
`
};

window.DOCS_DATA['dense'] = {
  id: 'dense',
  title: "dense\\<T> — 通用密集容器",
  category: 'containers',
  icon: 'N',
  order: 21,
  content: `## 20. dense\\<T> — 通用密集容器

无命名空间。所有接口 \`noexcept\`。针对密集场景替代 \`std::vector\` 的通用容器，是 \`class_pool\` 密集模式的独立容器形式。

### 构造与赋值

| 接口 | 说明 |
|------|------|
| \`dense()\` | 默认构造（空） |
| \`explicit dense(size_t capacity)\` | 预留容量（仅分配内存，\`size()\` 为 0） |
| \`dense(size_t count, const T& value)\` | \`count\` 个 \`value\` 拷贝 |
| \`dense(InputIt first, InputIt last)\` | 迭代器范围构造 |
| \`dense(std::initializer_list<T> init)\` | 初始化列表构造 |
| \`dense(const dense& other)\` | 拷贝构造（深拷贝） |
| \`dense(dense&& other)\` | 移动构造 |
| \`operator=(const dense&)\` | 拷贝赋值（深拷贝） |
| \`operator=(dense&&)\` | 移动赋值 |

### 元素访问

| 接口 | 说明 |
|------|------|
| \`operator[](size_t index)\` | 下标访问（无边界检查） |
| \`get(index)\` / \`get(index, error_index)\` | \`get\` 等价 \`operator[]\`；带 \`error_index\` 版本越界时回退访问 \`error_index\` |
| \`front()\` / \`back()\` | 首尾元素 |
| \`data()\` | 原始指针 |
| \`span()\` / \`span() const\` | \`std::span<T>\` 视图 |

### 容量与状态

| 接口 | 说明 |
|------|------|
| \`size()\` / \`count()\` | 元素数（两者等价） |
| \`capacity()\` | 容量 |
| \`empty()\` | 是否为空 |
| \`valid()\` | \`data() != nullptr\` |
| \`size_bytes()\` / \`capacity_bytes()\` | 已用/容量字节数 |
| \`max_size()\` | 理论最大元素数 |

### 修改器

| 接口 | 说明 |
|------|------|
| \`push_back(const T&)\` | 尾部拷贝追加（容量不足自动扩容） |
| \`push_back(T&&)\` | 尾部移动追加（容量不足自动扩容） |
| \`emplace_back(Args&&...)\` | 尾部原地构造（容量不足自动扩容） |
| \`push_back_unchecked(const T&)\` | 尾部拷贝追加（调用方保证容量足够） |
| \`push_back_unchecked(T&&)\` | 尾部移动追加（调用方保证容量足够） |
| \`emplace_back_unchecked(Args&&...)\` | 尾部原地构造（调用方保证容量足够） |
| \`emplace_back_dense_unchecked(Args&&...)\` | 等价 \`emplace_back_unchecked\`（dense 路径） |
| \`append_n(size_t n, const T& value)\` | 批量追加 \`n\` 个 \`value\` |
| \`append_bulk(const T* src, size_t count)\` | 批量拷贝追加 |
| \`append_bulk_move(T* src, size_t count)\` | 批量移动追加 |
| \`append_incrementing(count, counter)\` | 批量追加递增值（counter 起始，要求 trivially copyable） |
| \`append_generated(count, F&& generator)\` | 批量追加生成器产生值 |
| \`fill_bulk(value, start, count)\` | 从 \`start\` 开始填充 \`count\` 个 \`value\` |
| \`emplace(pos, args...)\` / \`insert(pos, value)\` | 任意位置插入 |
| \`erase(pos)\` / \`erase(first, last)\` | 任意位置删除 |
| \`pop_back()\` | 尾部删除 |
| \`erase_unordered(index)\` | 无序删除：末尾元素移到该位置并弹出，返回被移动的末下标 |
| \`clear()\` | 清空（\`size=0\`，capacity 保留） |
| \`swap(dense&)\` / 自由函数 \`swap(a, b)\` | 交换 |

### 容量控制

| 接口 | 说明 |
|------|------|
| \`increase_capacity(new_capacity)\` | 扩容到 \`new_capacity\`（只扩容不缩容，不改变 \`size\`） |
| \`resize_fill(new_capacity, value)\` | 增长 \`size\` 到 \`new_capacity\` 并以 \`value\` 填充新增位置（\`new_capacity <= size\` 时直接返回不销毁任何对象） |
| \`reserve_exact(new_capacity)\` | 精确预留容量（强制增长） |
| \`shrink_to_fit()\` | 缩容到 \`size\`（释放多余内存） |
| \`reduce_capacity(new_capacity)\` | 缩容到 \`new_capacity\`（只减不增，超出部分截断） |
| \`reduce_capacity(new_capacity, dense<T>& dst)\` | 缩容并将截断元素迁移到 \`dst\` |

### 迭代器

| 接口 | 说明 |
|------|------|
| \`begin()\` / \`end()\` | 正向迭代器（裸指针） |
| \`cbegin()\` / \`cend()\` | const 正向迭代器 |
| \`for_each(F&& f)\` / \`for_each(F&& f) const\` | 遍历所有元素，调用 \`f(v)\` |

### 反向迭代

| 接口 | 说明 |
|------|------|
| \`rbegin()\` / \`rend()\` | 反向迭代器 |
| \`rbegin() const\` / \`rend() const\` | const 反向迭代器 |
| \`crbegin()\` / \`crend()\` | const 反向迭代器（仅 const 重载） |
| \`reverse_for_each(F&& f)\` / \`reverse_for_each(F&& f) const\` | 反向遍历，调用 \`f(v)\` |

### 子范围视图

返回 \`std::span\` 视图，不复制数据。

| 接口 | 说明 |
|------|------|
| \`subspan(offset, count)\` | 返回 \`[offset, offset+count)\` 的 span，自动截断到 \`size()\` |
| \`subspan(offset)\` | 返回 \`[offset, size())\` 的 span |
| \`first(n)\` | 前 \`n\` 个元素 |
| \`last(n)\` | 后 \`n\` 个元素 |
| \`first_fixed<N>()\` | 前最多 \`N\` 个元素，返回 \`std::span<T>\`（不足 \`N\` 时截断到实际长度） |
| \`last_fixed<N>()\` | 后最多 \`N\` 个元素，返回 \`std::span<T>\`（不足 \`N\` 时截断到实际长度） |

所有接口均提供 const 重载。模板方法调用需 \`template\` 关键字：\`d.template first_fixed<8>()\`。

### 步进视图

按固定步长跳跃遍历的视图。

| 接口 | 说明 |
|------|------|
| \`strided_span_view(start, step, count)\` | 返回 \`strided_span<T>\`，持有 \`{指针, 步长, 数量}\` |
| \`strided_for_each(start, step, F&& f)\` | 运行时步长遍历，调用 \`f(v)\` |
| \`strided_for_each<Step>(F&& f)\` | 编译期步长遍历，\`Step=1\` 退化为 \`for_each\` |

\`strided_span<T>\` 自身提供 \`begin()/end()\` 迭代器、\`for_each(F&&)\`、\`operator[]\`、\`size()\`、\`data()\` 等。

### 变换视图

融合变换与消费，避免中间临时数组。

| 接口 | 说明 |
|------|------|
| \`transform_for_each(FTransform&& tr, FConsume&& con)\` | 对每个元素 \`v\` 调用 \`con(tr(v))\` |
| \`transform_to(R* dst, count, F&& tr)\` | 将 \`tr(v)\` 写入 \`dst\`，要求 \`count <= size()\`，\`dst\` 必须为未构造内存 |

### 过滤与查找

| 接口 | 说明 |
|------|------|
| \`find(const T& value)\` | 线性查找，返回首命指针，未命中返回 \`nullptr\` |
| \`find_if(Pred pred)\` | 谓词查找 |
| \`find_if_not(Pred pred)\` | 谓词反查找 |
| \`contains(const T& value)\` | 是否包含 |
| \`count_if(Pred pred)\` | 谓词计数 |
| \`filter_for_each(Pred pred, F&& f)\` | 仅对满足 \`pred(v)\` 的元素调用 \`f(v)\` |
| \`filter_indices_to(dense<size_t>& dst, Pred pred)\` | 将满足谓词的索引追加到 \`dst\` |

### 规约与极值

| 接口 | 说明 |
|------|------|
| \`reduce(F&& f, U init)\` | 顺序规约：\`acc = f(acc, v)\` |
| \`reduce_pairwise(F&& f, U init)\` | 成对规约：相邻两两合并后递归，减少关键路径深度 |
| \`min_element()\` / \`max_element()\` | 返回最小/最大元素指针 |
| \`minmax_element()\` | 返回 \`{min_ptr, max_ptr}\` |
| \`sum()\` | 算术求和（要求 \`is_arithmetic_v<T>\`） |
| \`dot_product(const U* other, count)\` | 点积（要求 \`is_arithmetic_v<T>\`） |

### 窗口与分块

| 接口 | 说明 |
|------|------|
| \`for_each_window<N>(F&& f)\` | 滑动窗口遍历，对每个 \`[i, i+N)\` 调用 \`f(std::span<T, N>)\`，共 \`size()-N+1\` 次 |
| \`for_each_chunk<N>(F&& f)\` | 不重叠分块遍历，对每个 \`[i*N, (i+1)*N)\` 调用 \`f(std::span<T, N>)\`，共 \`size()/N\` 次 |
| \`window_span<N>(offset)\` | 取偏移 \`offset\` 处的滑动窗口 span（\`std::span<T>\`，不足 \`N\` 时截断） |
| \`chunk_span<N>(chunk_idx)\` | 取第 \`chunk_idx\` 个不重叠分块 span（\`std::span<T>\`，不足 \`N\` 时截断） |

### 枚举视图

| 接口 | 说明 |
|------|------|
| \`for_each_enumerated(F&& f)\` | 带索引遍历，调用 \`f(index, value)\` |

### 双容器同步

| 接口 | 说明 |
|------|------|
| \`for_each_zip(U* other, count, F&& f)\` | 同步遍历 \`*this\` 与 \`other\`，调用 \`f(x, y)\` |
| \`for_each_zip(dense<U>& other, F&& f)\` | dense 版本 |
| \`for_each_zip(std::span<U> other, F&& f)\` | span 版本 |
| \`zip_with_to<R>(R* dst, const U* other, count, F&& f)\` | 将 \`f(x, y)\` 写入 \`dst\`（SoA→AoS 转换） |
| \`equal(const T* other, count)\` | 逐元素相等比较 |
| \`equal(const dense<U>& other)\` | dense 版本 |
| \`equal(std::span<const U> other)\` | span 版本 |

### 对齐

| 接口 | 说明 |
|------|------|
| \`aligned_data()\` | 返回对齐到缓存行（64B）的数据指针 |
| \`aligned_span()\` | 返回对齐 span |
| \`simd_for_each(F&& f)\` | 遍历（要求 \`is_trivially_copyable_v<T>\`，sizeof ≤ 32） |
| \`unaligned_tail_offset()\` | 返回无法对齐处理的尾部起始偏移 |

### 拷贝与移动

| 接口 | 说明 |
|------|------|
| \`copy_to(T* dst, count)\` | 批量拷贝 |
| \`copy_to(std::span<T> dst)\` | span 版本 |
| \`move_to(T* dst, count)\` | 批量移动 |
| \`move_to(std::span<T> dst)\` | span 版本 |
| \`reverse_copy_to(T* dst, count)\` | 反向拷贝 |
| \`reverse_copy_to(std::span<T> dst)\` | span 版本 |

### 使用

\`\`\`cpp

dense<int> pool;                          // 默认构造
dense<int> reserved(100);                 // 预留容量 100
dense<int> filled(5, 42);                 // 5 个 42
dense<int> init = {10, 20, 30};           // 初始化列表

pool.emplace_back(1);
pool.emplace_back(2);
pool.emplace_back(3);

// push_back 拷贝/移动
int v = 42;
pool.push_back(v);                        // 拷贝追加
pool.push_back(std::move(v));             // 移动追加

// 批量追加
dense<int> bulk;
bulk.append_n(100, 7);                    // 100 个 7
bulk.append_bulk(init.data(), init.size());

// 容量控制
pool.increase_capacity(1000);
pool.shrink_to_fit();
pool.resize_fill(10, 99);                   // size 增长到 10 并填充 99

// 元素访问
int v = pool[0];
int* p = pool.data();
std::span<int> s = pool.span();

// 迭代
for (int x : pool) { /* ... */ }
pool.for_each([](int& x) { x *= 2; });

// reduce_capacity 迁移元素
dense<int> src = {1, 2, 3, 4, 5};
dense<int> dst;
src.reduce_capacity(2, dst);              // src: {1, 2}, dst: {3, 4, 5}
\`\`\`

### 视图使用示例

\`\`\`cpp

dense<float> d(1000);
for (size_t i = 0; i < 1000; ++i) d[i] = static_cast<float>(i);

// 子范围视图
std::span<float> head = d.first(10);
std::span<float> tail = d.last(10);
std::span<float> mid = d.subspan(100, 50);
std::span<float> fixed_head = d.first_fixed<4>();   // 动态 span，不足 4 个时截断

// 反向迭代
d.reverse_for_each([](float& v) { v = -v; });
for (auto it = d.rbegin(); it != d.rend(); ++it) { /* ... */ }

// 步进遍历
d.strided_for_each(0, 4, [](float& v) { v *= 2.0f; });  // 每 4 个取一个
auto sv = d.strided_span_view(0, 4, 250);              // strided_span<float>

// 变换 + 消费融合
d.transform_for_each(
    [](float& v) -> float { return v * 2.0f; },
    [](float x) { /* consume x */ });

// 过滤
float* p = d.find(42.0f);
bool has = d.contains(42.0f);
size_t cnt = d.count_if([](float v) { return v > 0; });
d.filter_for_each([](float v) { return v > 0; }, [](float& v) { v = 0; });

dense<size_t> indices;
d.filter_indices_to(indices, [](float v) { return v > 100; });

// 规约
float sum = d.sum();
float s = d.reduce([](float acc, const float& v) { return acc + v; }, 0.0f);
float* mn = d.min_element();
auto [mn_ptr, mx_ptr] = d.minmax_element();

// 窗口/分块
d.for_each_window<4>([](std::span<float, 4> w) { /* 滑动窗口 */ });
d.for_each_chunk<4>([](std::span<float, 4> c) { /* 不重叠分块 */ });

// 枚举
d.for_each_enumerated([](size_t i, float& v) { v += static_cast<float>(i); });

// 双容器同步
dense<float> other(1000);
d.for_each_zip(other, [](float& a, float& b) { a += b; });
bool same = d.equal(other);

// 遍历（要求 trivially copyable）
d.simd_for_each([](float& v) { v *= 2.0f; });
size_t tail_off = d.unaligned_tail_offset();

// 拷贝/移动
dense<float> dst2(1000);
d.copy_to(dst2.data(), 1000);
d.reverse_copy_to(dst2.span());
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 未确认容量就调用 \`push_back_unchecked\` / \`emplace_back_unchecked\` | 越界写崩 | 先 \`increase_capacity\` 或用 \`emplace_back\` |
| \`reduce_capacity(n, dst)\` 后访问 \`src\` 被截断的元素 | 元素已迁移到 \`dst\` | 访问 \`dst\` 中对应元素 |
| \`fill_bulk\` 范围超出 \`capacity()\` | 越界写崩 | 先 \`increase_capacity\` 或 \`reserve_exact\` |
| 模板方法调用缺少 \`template\` 关键字 | 编译错误 | \`d.template first_fixed<N>()\` |
| 对非 trivially copyable 类型调用 \`simd_for_each\` | 编译错误（concept 约束） | 使用 \`for_each\` 代替 |
| \`subspan\` 的 \`offset\` 超过 \`size()\` | 返回空 span（已截断） | 调用前检查 \`offset < size()\` |

---
`
};

window.DOCS_DATA['ring_buffer'] = {
  id: 'ring_buffer',
  title: "ring_buffer — 环形缓冲区",
  category: 'containers',
  icon: 'Q',
  order: 28,
  content: `## 27. ring_buffer — 环形缓冲区

全局命名空间。\`noexcept\`。

模板参数 \`T\`（元素类型）与 \`N\`（容量，向上取整为 2 的幂即真实容量）。固定容量，满时行为由写入接口决定：\`try_*\` 族拒绝，\`*_overwrite\` 族覆盖最旧。不可拷贝，可移动（移动后源为空）。

### 接口

| 接口 | 说明 |
|------|------|
| \`ring_buffer()\` | 默认构造 |
| \`ring_buffer(ring_buffer&&)\` / \`operator=(ring_buffer&&)\` | 移动构造/赋值，移动后源对象为空 |
| \`try_push(const T&)\` / \`try_push(T&&)\` | 入队，满则拒绝（返回 false，队列不变） |
| \`try_emplace(args...)\` | 原地构造入队，满则拒绝（返回 nullptr） |
| \`push_overwrite(const T&)\` / \`push_overwrite(T&&)\` | 入队，满则覆盖最旧；返回本次覆盖条数（0 或 1） |
| \`emplace_overwrite(args...)\` | 原地构造入队，满则覆盖最旧；返回新槽指针（永不为空） |
| \`front()\` | 查看队首，空返回 nullptr（\`const\` 重载返回 \`const T*\`） |
| \`pop()\` | 弹出并析构队首，空返回 false |
| \`try_pop(T& out)\` | 弹出队首并移出赋值给 \`out\`，空返回 false |
| \`drain(handler)\` | 逐个消费排空，回调签名 \`void(T&)\`，返回消费数 |
| \`drain_with_budget(budget, handler)\` | 最多消费 \`budget\` 个，未尽部分保留，返回消费数 |
| \`visit(handler) const\` | 只读遍历（不消费），回调签名 \`void(const T&)\`，返回遍历数 |
| \`front_run(T*& out_ptr, size_t& out_len)\` | 返回队首处连续段（从读位置到段尾），供批量处理；空返回 0 |
| \`pop_run(size_t n)\` | 批量弹出前 \`n\` 个并析构，返回实际弹出数（超出部分截断） |
| \`size()\` / \`empty()\` | 元素数 / 是否为空 |
| \`capacity()\` | 真实容量（static constexpr） |
| \`dropped_count()\` | 累计被覆盖淘汰条数 |
| \`clear()\` | 清空并析构全部元素（保留缓冲） |

### 使用

\`\`\`cpp

struct event { int type; int data; };
ring_buffer<event, 1024> buf;

// 满则拒绝
if (!buf.try_push({1, 100})) { /* 队列已满 */ }

// 满则覆盖最旧（保留最近 N 条语义）
buf.push_overwrite({2, 200});
event* slot = buf.emplace_overwrite();
slot->type = 3;

// 消费
event out;
if (buf.try_pop(out)) { /* out 为队首 */ }

size_t n = buf.drain([](event& e) {
    std::cout << "type=" << e.type << " data=" << e.data << "\\n";
});

// 只读查看（不消费）
buf.visit([](const event& e) { std::cout << e.type << "\\n"; });

// 批量段处理
event* run = nullptr;
size_t len = 0;
while (buf.front_run(run, len) > 0)
{
    for (size_t i = 0; i < len; ++i) { /* 批量处理 run[i] */ }
    buf.pop_run(len);
}

// 移动语义
ring_buffer<event, 1024> buf2(std::move(buf));
// buf 现在 empty

// 容量与统计
size_t cap = ring_buffer<event, 1024>::capacity();  // 1024
uint64_t dropped = buf2.dropped_count();
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用 \`push_overwrite\` 后期待"满了就失败" | 满时会淘汰最旧元素 | 需要满则失败用 \`try_push\`/\`try_emplace\` |
| \`drain\` 的 handler 内调 \`*_overwrite\` 入队本队列 | 覆盖会淘汰尚未消费的最旧元素 | handler 内只用 \`try_push\`/\`try_emplace\`，或先 \`pop()\` 手动控制 |
| \`visit\` 的 handler 内修改本队列 | 遍历状态被破坏 | 查看与修改分开，先 \`visit\` 再修改 |
| 依赖 \`front()\` 指针在 \`pop\` 后有效 | \`pop\` 推进读位置，指针失效 | 先拷贝或用 \`try_pop\` 移出 |
| 拷贝构造 \`ring_buffer\` | 不可拷贝 | 用移动或重新填充 |

---
`
};


window.DOCS_DATA['signal_queue'] = {
  id: 'signal_queue',
  title: "signal_queue — 信号与变更日志队列",
  category: 'containers',
  icon: 'Q',
  order: 44,
  content: `## signal_queue — 信号与变更日志队列

无命名空间。所有接口 \`noexcept\`。两级队列：固定容量环形缓冲 + 溢出缓冲（主环满时兜底），入队不丢事件。用于实体信号、组件信号与变更日志的统一入队与帧末排空。

### 模板参数

| 参数 | 说明 |
|------|------|
| \`Event\` | 事件类型，需可拷贝构造 |
| \`Capacity\` | 环形缓冲容量（向上取整为 2 的幂） |

### 接口

| 接口 | 说明 |
|------|------|
| \`push(const Event&)\` | 入队；禁用状态下不生效；主环满或溢出未排空时追加到溢出缓冲，保证先进先出 |
| \`flush(handler)\` | 排空待处理事件，回调签名 \`void(const Event&)\`（亦可接受 \`void(Event&)\`） |
| \`has_pending() const\` | 是否有待处理事件（含溢出缓冲未消费部分） |
| \`pending_count() const\` | 待处理事件数（主环 + 溢出剩余） |
| \`set_enabled(bool)\` / \`enabled() const\` | 设置/查询入队开关 |

### 排空语义

- 单次 \`flush\` 的总预算为 \`Capacity × 4\`，由主环与溢出共享；超出部分保留到下次 \`flush\`
- 先消费主环再消费溢出，全队列保持先进先出
- \`handler\` 内追加的事件在预算内继续消费，不会丢失
- \`flush\` 重入（\`handler\` 内再次 \`flush\`）被守卫拦截，嵌套调用不做处理

### 使用

\`\`\`cpp
struct my_event { uint32_t type; uint32_t index; };

signal_queue<my_event, 1024> q;
q.push(my_event{0, 7});
q.flush([](const my_event& ev) { /* ... */ });

// 突发积压后循环排空
while (q.has_pending())
{
    q.flush([](const my_event& ev) { /* ... */ });
}

// 禁用入队 / 重新启用
q.set_enabled(false);
q.set_enabled(true);
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 依赖单次 \`flush\` 清空超长队列 | 超出预算的事件保留 | 循环调用 \`flush\` 直到 \`has_pending()\` 为 false |
| 拷贝 \`signal_queue\` | 禁止拷贝 | 使用移动语义 |
`
};
