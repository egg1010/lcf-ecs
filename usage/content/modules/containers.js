window.DOCS_DATA = window.DOCS_DATA || {};

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
| \`get(index)\` | 返回元素指针；\`index >= size()\` 返回 \`nullptr\`，判断交给调用方 |
| \`get(index, error_index)\` | 越界时回退访问 \`error_index\`，\`error_index\` 也越界时回落到 0 |
| \`front()\` / \`back()\` | 首尾元素 |
| \`data()\` | 原始指针 |
| \`span()\` / \`span() const\` | \`std::span<T>\` 视图 |

### 容量与状态

| 接口 | 说明 |
|------|------|
| \`size()\` | 元素数 |
| \`capacity()\` | 容量 |
| \`empty()\` | 是否为空 |
| \`valid()\` | \`data() != nullptr\` |
| \`size_bytes()\` / \`capacity_bytes()\` | 已用/容量字节数 |
| \`max_size()\` | 理论最大元素数 |

### 修改器

| 接口 | 说明 |
|------|------|
| \`emplace_back(Args&&...)\` | 尾部原地构造（容量不足自动扩容） |
| \`emplace_back_unchecked(Args&&...)\` | 尾部原地构造（调用方保证容量足够） |
| \`append_bulk(const T* src, size_t count)\` | 批量拷贝追加 |
| \`append_bulk_move(T* src, size_t count)\` | 批量移动追加 |
| \`append_generated(count, F&& generator)\` | 批量追加生成器产生值 |
| \`fill_bulk(value, start, count)\` | 从 \`start\` 开始填充 \`count\` 个 \`value\` |
| \`emplace(pos, args...)\` | 任意位置插入 |
| \`erase(first, last)\` | 区间删除（移动后续元素） |
| \`pop_back()\` | 尾部删除 |
| \`erase_unordered(index)\` | 无序删除：末尾元素移到该位置并弹出，返回被移动的末下标 |
| \`clear()\` | 清空（\`size=0\`，capacity 保留） |
| \`swap(dense&)\` / 自由函数 \`swap(a, b)\` | 交换 |

### 容量控制

| 接口 | 说明 |
|------|------|
| \`increase_capacity(new_capacity)\` | 扩容到 \`new_capacity\`（只扩容不缩容，不改变 \`size\`） |
| \`resize_fill(new_capacity, value)\` | 增长 \`size\` 到 \`new_capacity\` 并以 \`value\` 填充新增位置（\`new_capacity <= size\` 时直接返回不销毁任何对象） |
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

所有接口均提供 const 重载。模板方法调用需 \`template\` 关键字：\`d.window_span<8>(0)\`。

### 步进视图

按固定步长跳跃遍历的视图。

| 接口 | 说明 |
|------|------|
| \`strided_span_view(start, step, count)\` | 返回 \`container_strided_span<dense<T>>\`，持有 \`{容器指针, start, step, count}\` |
| \`strided_for_each(start, step, F&& f)\` | 运行时步长遍历，调用 \`f(v)\` |
| \`strided_for_each<Step>(F&& f)\` | 编译期步长遍历，\`Step=1\` 退化为 \`for_each\` |

\`container_strided_span\` 自身提供 \`begin()/end()\` 迭代器、\`for_each(F&&)\`、\`operator[]\`、\`size()\`、\`data()\`、\`step()\` 等。

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
| \`count_if(Pred pred)\` | 谓词计数 |
| \`filter_for_each(Pred pred, F&& f)\` | 仅对满足 \`pred(v)\` 的元素调用 \`f(v)\` |
| \`filter_indices_to(dense<size_t>& dst, Pred pred)\` | 将满足谓词的索引追加到 \`dst\` |

### 规约与极值

| 接口 | 说明 |
|------|------|
| \`reduce(F&& f, U init)\` | 顺序规约：\`acc = f(acc, v)\` |
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

// emplace_back 拷贝/移动
int v = 42;
pool.emplace_back(v);                        // 拷贝追加
pool.emplace_back(std::move(v));             // 移动追加

// 批量追加
dense<int> bulk;
bulk.fill_bulk(7, bulk.size(), 100);      // 追加 100 个 7
bulk.append_bulk(init.data(), init.size());

// 容量控制
pool.increase_capacity(1000);
pool.reduce_capacity(pool.size());
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
std::span<float> fixed_head = d.first(4);   // 动态 span，不足 4 个时截断

// 反向迭代
d.reverse_for_each([](float& v) { v = -v; });
for (auto it = d.rbegin(); it != d.rend(); ++it) { /* ... */ }

// 步进遍历
d.strided_for_each(0, 4, [](float& v) { v *= 2.0f; });  // 每 4 个取一个
auto sv = d.strided_span_view(0, 4, 250);              // container_strided_span<dense<float>>

// 变换 + 消费融合
d.transform_for_each(
    [](float& v) -> float { return v * 2.0f; },
    [](float x) { /* consume x */ });

// 过滤
float* p = d.find(42.0f);
bool has = d.find(42.0f) != nullptr;
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
| 未确认容量就调用 \`emplace_back_unchecked\` | 越界写崩 | 先 \`increase_capacity\` 或用 \`emplace_back\` |
| 写 \`emplace_back({a, b})\` | 花括号列表没有类型，模板推导不出 \`Args\` | 写 \`emplace_back(T{a, b})\` |
| \`reduce_capacity(n, dst)\` 后访问 \`src\` 被截断的元素 | 元素已迁移到 \`dst\` | 访问 \`dst\` 中对应元素 |
| \`fill_bulk\` 范围超出 \`capacity()\` | 越界写崩 | 先 \`increase_capacity\` |
| 模板方法调用缺少 \`template\` 关键字 | 编译错误 | \`d.first(N)\` |
| 对非 trivially copyable 类型调用 \`simd_for_each\` | 编译错误（concept 约束） | 使用 \`for_each\` 代替 |
| \`subspan\` 的 \`offset\` 超过 \`size()\` | 返回空 span（已截断） | 调用前检查 \`offset < size()\` |

---
`
};

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
| \`get(size_t)\` | 返回元素指针；越界或空洞返回 \`nullptr\`，判断交给调用方 |
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
| \`emplace_back_unchecked(Args...)\` | 尾部原地构造（调用方保证容量足够，不自动扩容） |
| \`append_bulk(const T* src, size_t count)\` | 批量拷贝追加 |
| \`append_bulk_move(T* src, size_t count)\` | 批量移动追加 |
| \`append_generated(count, F&& generator)\` | 批量追加生成器产生值 |
| \`emplace(pos, Args...)\` | 在指定位置插入（移动后续元素） |
| \`emplace_at(index, Args...)\` | 任意位置构造（insert-or-assign：已占用则先析构旧对象再重建） |
| \`sparse_erase_at(index)\` | 稀疏删除（不移动元素，产生空洞） |
| \`soft_dense_delete(start, end)\` | 软删除范围（保留对象内存） |
| \`pop_back()\` | 删除尾部元素 |
| \`erase(pos)\` | 删除指定位置（移动后续元素） |
| \`erase(first, last)\` | 区间删除（移动后续元素） |
| \`erase_unordered(index)\` | 无序删除：末尾活跃元素移到该位置，返回原末下标 |
| \`clear()\` | 清空所有元素 |
| \`increase_capacity(capacity)\` | 扩容（只扩容不缩容，不增加元素） |
| \`resize_fill(new_capacity, value)\` | 增长 \`size\` 到 \`new_capacity\` 并以 \`value\` 填充新增位置（只增长不缩容，\`new_capacity <= size\` 时直接返回不销毁任何对象） |
| \`reduce_capacity(capacity)\` | 缩容（截断超出元素） |
| \`reduce_capacity(capacity, dst)\` | 缩容，超出元素移至 dst |
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
| \`emplace_back()\` | 保持连续 |
| \`emplace(pos)\` | 保持连续（元素右移） |
| \`pop_back()\` | 保持连续 |
| \`clear()\` | 重置为连续 |
| \`sparse_erase_at(index)\` | **产生空洞** → 变稀疏 |
| \`emplace_at(index)\` | **可能填充空洞** → 可能变连续 |
| \`reduce_capacity()\` 缩小 | **可能消除空洞** → 可能变连续 |

### 插入/删除

| 接口 | 说明 |
|------|------|
| \`emplace(const_iterator pos, args...)\` | 在指定位置原位构造，返回指向新元素的迭代器 |

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

所有接口均提供 const 重载。模板方法调用需 \`template\` 关键字：\`p.first(8)\`。

#### 步进视图

| 接口 | 说明 |
|------|------|
| \`strided_span_view(start, step, count)\` | 返回 \`container_strided_span<class_pool<T>>\`，持有 \`{容器指针, start, step, count}\`；\`start\` 越界归零、\`step=0\` 视为空视图、\`count\` 收敛到可用槽位数 |
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
| \`count_if(Pred pred)\` | 谓词计数 |
| \`filter_for_each(Pred pred, F&& f)\` | 仅对满足 \`pred(v)\` 的元素调用 \`f(v)\` |
| \`filter_indices_to(class_pool<size_t>& dst, Pred pred)\` | 将满足谓词的索引追加到 \`dst\` |

#### 规约与极值

| 接口 | 说明 |
|------|------|
| \`reduce(F&& f, U init)\` | 顺序规约：\`acc = f(acc, v)\` |
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

### 自由函数

| 接口 | 说明 |
|------|------|
| \`swap(class_pool&, class_pool&)\` | 交换两个容器 |

### 使用

\`\`\`cpp
class_pool<int> pool;
pool.emplace_back(10);
pool.emplace_back(20);

// emplace_back 拷贝/移动追加
int v = 30;
pool.emplace_back(v);                  // 拷贝追加
pool.emplace_back(std::move(v));       // 移动追加

// 任意位置构造（insert-or-assign：已占用则销毁旧对象并重建）
pool.emplace_at(3, 999);       // 索引 3 已占用，覆盖为 999
pool.emplace_at(3, 888);       // 再次覆盖为 888
pool.emplace_at(3, 777);  // 同为覆盖语义，覆盖为 777

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
dense_pool.emplace_back_unchecked(3);
dense_pool.emplace_back_unchecked(std::move(v));  // 移动追加
dense_pool.emplace_back_unchecked(4);
dense_pool.emplace_back_unchecked(5);

// 批量插入：reserve + unchecked 组合
class_pool<int> batch_pool;
batch_pool.increase_capacity(1'000'000);
for (int i = 0; i < 1'000'000; ++i) {
    batch_pool.emplace_back_unchecked(i);
}

// 批量插入：fill_bulk 单次调用批量追加
class_pool<int> fastest;
fastest.fill_bulk(0, fastest.size(), 1'000'000);  // 直接追加 1M 个 0

// 预扩容（不增加元素）
class_pool<int> reserved;
reserved.increase_capacity(1000);    // capacity >= 1000，size 不变
reserved.resize_fill(10, 0);   // size 增长到 10 并填充 0（只增长不缩容）

// 位置插入
pool.emplace(std::next(pool.begin(), 1), 42);  // 在位置 1 插入

// 迭代
for (auto v : pool) { /* ... */ }

// span
std::span<int> s = pool.span();

// get(): 越界或空洞返回 nullptr
if (int* a = pool.get(0)) { *a = 42; }

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
| 尾部追加元素（已有值对象） | \`emplace_back(value)\` | O(1)，保持连续 |
| 尾部追加元素（直接构造） | \`emplace_back(args...)\` | O(1)，保持连续，原地构造避免临时对象 |
| 任意位置插入/删除并保持连续 | \`emplace(pos)\` / \`erase(pos)\` | 移动后续元素，O(n)，保持连续 |
| 稀疏数组（大索引跳跃） | \`emplace_at()\` / \`sparse_erase_at()\` | O(1)，不移动其他元素，但产生空洞 |
| 批量填充已知索引 | \`emplace_at()\` | 填充空洞后自动切回连续 |
| 删除整个容器 | \`clear()\` | 重置为连续 |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`sparse_erase_at()\` 后仍期望连续迭代 | 产生空洞，迭代变稀疏模式 | 用 \`emplace_at()\` 填充空洞，或用 \`erase()\` 替代 |
| 写 \`emplace_back({a, b})\` | 花括号列表没有类型，模板推导不出 \`Args\` | 写 \`emplace_back(T{a, b})\` |
| 频繁 \`sparse_erase_at()\` + \`emplace_at()\` 来回切换 | 每次切换触发模式扫描 | 批量操作，或统一使用 \`erase()\`/\`emplace()\` 保持连续 |
| \`emplace_at()\` 在远超 \`size()\` 的索引上构造 | 中间留大量未初始化槽位，\`size()\` 暴增 | 用 \`resize_fill(n, value)\` 预填充 |
| 在 sparse 模式下使用 \`data()\` + \`span()\` 做线性遍历 | 未初始化槽位包含垃圾数据 | 始终通过迭代器遍历，或先确认 \`is_dense()\` 为 true |
| \`increase_capacity(n)\` 期望精确分配到 n | 实际容量可能大于 n | 构造时用 \`class_pool(n)\` 精确预留，或接受向上取整 |
| \`emplace_at()\` 期望保留旧值 | \`emplace_at\` 是 insert-or-assign，重复构造会销毁旧对象 | 需要保留时先用 \`is_constructed_at()\` 检查 |
| 未确认容量就调用 \`emplace_back_unchecked\` | 越界写崩（不自动扩容） | 先 \`increase_capacity\`，或用 \`emplace_back()\` |
| 软删除后对象仍占内存 | \`soft_dense_delete\` 保留对象不析构 | 用 \`sparse_erase_at()\` 真正析构，或 \`emplace_at()\` / \`fill_the_hole()\` 填洞复用 |

### fill_the_hole — 填洞或追加

\`fill_the_hole\` 优先填第一个空洞（最低索引），无洞则 \`emplace_back\` 末尾追加。

#### 接口

| 接口 | 说明 |
|------|------|
| \`fill_the_hole(args...)\` | 填第一个空洞或末尾追加，返回 \`T&\` |

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

// fill_the_hole: 有洞填第一个洞, 无洞追加
class_pool<int> pool2;
pool2.fill_the_hole(10);  // 无洞 → 追加
pool2.fill_the_hole(20);  // 无洞 → 追加
pool2.sparse_erase_at(0); // 产生空洞 at 0
pool2.fill_the_hole(99);  // 填洞 at 0
// pool2[0] == 99
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
  title: "container_views — 容器视图",
  category: 'containers',
  icon: 'P',
  order: 16,
  content: `## 15.5 container_views — 容器视图

通用视图接口，全局命名空间。基于 \`viewable_container\` 概念约束，\`dense<T>\` 与 \`class_pool<T>\` 共用同一套视图函数；\`class_pool<T>\` 另有专属视图。

- **不修改原容器**：仅依赖容器公开 API
- **不复制数据**：视图只持有指针与长度
- **全 \`noexcept\`**：与原容器约束一致

### 通用视图接口

\`viewable_container\` 概念要求容器提供 \`data()\` / \`size()\` / \`begin()\` / \`end()\` / \`operator[]\` / \`span()\` / \`rbegin()\` / \`rend()\`；\`dense<T>\` 与 \`class_pool<T>\` 均满足此概念。

| 分类 | 接口 | 说明 |
|------|------|------|
| **遍历** | \`for_each(c, f)\` | 遍历所有活跃元素，调用 \`f(v)\` |
|  | \`reverse_for_each(c, f)\` | 反向遍历，调用 \`f(v)\` |
|  | \`for_each_enumerated(c, f)\` | 带索引遍历，调用 \`f(index, v)\` |
| **子范围** | \`subspan(c, off, cnt)\` / \`subspan(c, off)\` | 返回 \`std::span<T>\`，自动截断到 \`size()\`（与 \`dense::subspan\` / \`std::span::subspan\` 命名一致） |
|  | \`first(c, n)\` / \`last(c, n)\` | 前/后 \`n\` 个元素的 span |
| **步进** | \`strided_span_view(c, start, step, cnt)\` | 返回 \`container_strided_span<C>\`（两容器共用），按容器实际范围收敛（越界 \`start\` / \`step=0\` 得到空视图） |
|  | \`strided_for_each(c, start, step, f)\` | 运行时步长遍历 |
|  | \`strided_for_each<Step>(c, f)\` | 编译期步长遍历，\`Step=1\` 退化为 \`for_each\` |
| **变换** | \`transform_for_each(c, tr, con)\` | 对每个 \`v\` 调用 \`con(tr(v))\` |
|  | \`transform_to(c, dst, n, tr)\` | 将 \`tr(v)\` 写入 \`dst\`，要求 \`n <= c.size()\`，\`dst\` 必须为未构造内存 |
| **查找过滤** | \`find(c, v)\` / \`find_if(c, pred)\` / \`find_if_not(c, pred)\` | 线性查找，返回首命指针，未命中返回 \`nullptr\` |
|  | \`count_if(c, pred)\` | 条件计数 |
|  | \`filter_for_each(c, pred, f)\` | 仅对满足 \`pred(v)\` 的元素调用 \`f(v)\` |
| **规约** | \`reduce(c, f, init)\` | 顺序规约：\`acc = f(acc, v)\` |
|  | \`min_element(c)\` / \`max_element(c)\` / \`minmax_element(c)\` | 返回极值指针 |
|  | \`sum(c)\` | 算术求和（要求 \`is_arithmetic_v<T>\`） |
|  | \`dot_product(c, other, n)\` | 点积（要求 \`is_arithmetic_v<T>\`） |
| **窗口/分块** | \`for_each_window<N>(c, f)\` | 滑动窗口遍历，对每个 \`[i, i+N)\` 调用 \`f(std::span<T, N>)\` |
|  | \`for_each_chunk<N>(c, f)\` | 不重叠分块遍历 |
|  | \`window_span<N>(c, off)\` | 取滑动窗口 span（\`std::span<T>\`，不足 \`N\` 时截断） |
| **双容器** | \`for_each_zip(a, b, f)\` | 双容器同步遍历，调用 \`f(x, y)\` |
|  | \`for_each_zip(a, ptr, n, f)\` | 容器 + 裸指针同步 |
|  | \`zip_with_to(a, b, dst, n, f)\` | 将 \`f(x, y)\` 写入 \`dst\`（\`dst\` 必须为未构造内存） |
|  | \`equal(a, b)\` / \`equal(a, ptr, n)\` / \`equal(a, span)\` | 相等性比较 |
| **对齐** | \`simd_for_each(c, f)\` | 遍历（要求 \`is_trivially_copyable_v<T>\` 且 \`sizeof(T) ≤ 32\`） |
|  | \`unaligned_tail_offset(c)\` | 32 字节对齐处理后的尾部偏移 |
| **拷贝/移动** | \`copy_to(c, dst, n)\` / \`copy_to(c, span)\` | 批量拷贝（\`dst\` 必须为未构造内存） |
|  | \`move_to(c, dst, n)\` / \`move_to(c, span)\` | 批量移动（\`dst\` 必须为未构造内存） |
|  | \`reverse_copy_to(c, dst, n)\` / \`reverse_copy_to(c, span)\` | 反向拷贝（\`dst\` 必须为未构造内存） |

### class_pool 专属视图

\`class_pool<T>\` 的专属视图接口。

| 接口 | 说明 |
|------|------|
| \`filter_indices_to(pool, dst, pred)\` | 将满足 \`pred(v)\` 的索引追加到 \`class_pool<size_t>& dst\` |

### 使用示例

\`\`\`cpp
// 视图接口位于全局命名空间, 无需 using namespace

class_pool<POD32> pool;
for (size_t i = 0; i < 1000; ++i) pool.emplace_back_unchecked({static_cast<float>(i)});

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
copy_to(pool, compact, pool.size());
// compact[0..pool.count()) 为连续活跃元素

// dense 同样适用
dense<float> d(1000);
for (size_t i = 0; i < 1000; ++i) d[i] = static_cast<float>(i);
d.for_each([](float& v) { v *= 2.0f; });
strided_for_each<4>(d, [](float& v) { /* ... */ });
\`\`\`

### 稀疏模式行为

\`class_pool<T>\` 稀疏模式（\`is_dense() == false\`）下视图行为：
- **子范围切片**（\`subspan\` / \`first\` / \`last\` / \`window_span\`）：直接切片，**不跳过空洞**，调用方需自行 \`is_constructed_at(i)\` 检查
- **窗口/分块遍历**（\`for_each_window\` / \`for_each_chunk\`）：窗口或块内存在空洞时，跳过该窗口/块
- **步进遍历**：越过空洞时自动跳过
- **过滤/查找/规约/zip/\`simd_for_each\`**：**自动跳过空洞**（仅遍历活跃元素）
- **\`copy_to\`**：按活跃顺序压缩写入 \`dst\`（\`dst\` 必须为未构造内存）

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 稀疏模式用 \`subspan\` 后直接遍历 | 包含未活跃槽 | 用 \`for_each\` / \`filter_for_each\` 走迭代器 |
| \`simd_for_each\` 用于稀疏模式 | 退化为 \`for_each\` | 仅密集模式适用 |
| 视图持有期间修改容器 | 迭代器/指针失效 | 视图生命周期 = 容器稳定期 |
| 期望 \`last<N>\` 越界返回空 span | 返回动态 \`std::span<T>\`，不足 \`N\` 时截断到实际长度 | 检查 \`span.size()\` 后使用 |

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

window.DOCS_DATA['multi_block_bitmask'] = {
  id: 'multi_block_bitmask',
  title: "multi_block_bitmask — 多块位掩码存储",
  category: 'containers',
  icon: 'B',
  order: 30,
  content: `## 29. multi_block_bitmask — 多块位掩码存储

无命名空间。多块位掩码容器，适用于布隆过滤、稀疏集合、组件标签、属性位图等场景。

### 接口

#### 静态辅助

| 接口 | 说明 |
|------|------|
| \`static constexpr uint32_t bits_per_block\` | 每块位数（64） |
| \`static constexpr uint32_t block_count_for_bits(size_t bit_count)\` | 给定位数算所需块数 |

#### 块管理

| 接口 | 说明 |
|------|------|
| \`reserve_blocks(uint32_t num_blocks)\` | 预分配掩码块数（仅扩容，不缩容） |
| \`num_blocks() const\` | 当前每槽掩码块数（= 1 + overflow_block_count_） |
| \`overflow_entity_count() const\` | 已分配 overflow 的槽位数 |

#### 容量管理

| 接口 | 说明 |
|------|------|
| \`ensure_entity(uint32_t slot)\` | 确保槽位容量（必要时自动扩容） |
| \`resize_entities(uint32_t new_count)\` | 批量扩容到 \`new_count\` 个槽位 |
| \`increase_capacity(size_t new_slot_capacity)\` | 扩容到指定槽位容量（只增不减，不改变 \`size\`） |
| \`reduce_capacity(size_t new_slot_capacity)\` | 缩容到指定容量（超出部分截断） |
| \`clear()\` | 清空所有数据（\`size=0\`，\`capacity\`/\`num_blocks\` 保留） |
| \`size() const\` | 当前槽位数 |
| \`capacity() const\` | 槽位容量 |
| \`empty() const\` | 是否为空 |
| \`size_bytes() const\` | 已用内存（字节） |
| \`capacity_bytes() const\` | 容量内存（字节） |

#### 单位写入（带边界检查）

| 接口 | 说明 |
|------|------|
| \`set_bit(slot, block_idx, bit_offset)\` | 设置位（必要时自动扩容 slot） |
| \`clear_bit(slot, block_idx, bit_offset)\` | 清除位 |
| \`clear_entity(slot)\` | 清零该槽位所有块 |

#### 单位写入（无边界检查）

调用方保证 \`slot < size()\` 且 \`block_idx < num_blocks()\`。

| 接口 | 说明 |
|------|------|
| \`set_bit_no_check(slot, block_idx, bit_offset)\` | 设置位 |
| \`clear_bit_no_check(slot, block_idx, bit_offset)\` | 清除位 |

#### 整块写入

替代读-改-写，直接写入整块值。

| 接口 | 说明 |
|------|------|
| \`set_block_value(slot, block_idx, uint64_t value)\` | 整块赋值 |
| \`or_block_value(slot, block_idx, uint64_t mask)\` | 原地或 |
| \`and_block_value(slot, block_idx, uint64_t mask)\` | 原地与 |
| \`xor_block_value(slot, block_idx, uint64_t mask)\` | 原地异或 |

#### 批量位操作（同块多位）

| 接口 | 说明 |
|------|------|
| \`set_bits_at(slot, block_idx, std::span<const uint32_t> offsets)\` | 批量设置多位 |
| \`clear_bits_at(slot, block_idx, std::span<const uint32_t> offsets)\` | 批量清除多位 |
| \`toggle_bits_at(slot, block_idx, std::span<const uint32_t> offsets)\` | 批量翻转多位 |

#### 整槽多块读写

| 接口 | 说明 |
|------|------|
| \`assign_slot(slot, std::span<const uint64_t> data)\` | 写入整槽所有块（\`data[0]\`→block 0，\`data[1]\`→block 1，…；若 \`data.size() > num_blocks()\` 自动 \`reserve_blocks\`） |
| \`copy_slot_to(slot, std::span<uint64_t> dst) const\` | 读取整槽所有块到 \`dst\`（\`dst.size()\` 决定读取块数，不足部分补零） |

#### 查询接口

| 接口 | 说明 |
|------|------|
| \`get_block(slot, block_idx) const\` | 获取块值（越界返回 0） |
| \`test_bit(slot, block_idx, bit_offset) const\` | 测试位是否置位 |
| \`any_set_in_block(slot, block_idx) const\` | 块是否非零 |
| \`any_set(slot) const\` | 槽位是否有任意位置位（含 overflow） |
| \`is_zero(slot) const\` | 槽位是否全零 |
| \`count_set_bits(slot) const\` | 槽位置位数（含 overflow，基于 \`std::popcount\`） |
| \`slot_satisfies(slot, req, exc, block_count) const\` | 槽位掩码是否满足：包含 \`req\` 的全部置位且与 \`exc\` 无交集；\`req\`/\`exc\` 为按块展开的掩码数组，\`block_count\` 为参与比较的块数 |
| \`find_first_set(slot, out_block, out_offset) const\` | 找首个置位，写入 \`out_block\`/\`out_offset\`，返回是否找到 |
| \`find_last_set(slot, out_block, out_offset) const\` | 找末个置位 |
| \`find_next_set(slot, after_block, after_offset, out_block, out_offset) const\` | 从指定位置之后找下一个置位 |

#### 遍历接口

回调受 \`std::invocable\` concepts 约束，编译期捕获签名错误。

| 接口 | 回调签名 | 说明 |
|------|---------|------|
| \`for_each_set_bit(slot, func) const\` | \`func(uint32_t block_idx, uint32_t bit_offset)\` | 遍历该槽位所有置位 |
| \`for_each_set_slot(func) const\` | \`func(uint32_t slot)\` | 遍历所有非空槽位 |
| \`for_each_set_bit_global(func) const\` | \`func(uint32_t slot, uint32_t block_idx, uint32_t bit_offset)\` | 全局遍历所有置位 |
| \`count_set_bits_global() const\` | — | 全局置位总数 |

#### 视图接口

| 接口 | 说明 |
|------|------|
| \`inline_span() noexcept\` / \`inline_span() const noexcept\` | 返回 block 0 全局视图 \`std::span<uint64_t>\` / \`std::span<const uint64_t>\` |
| \`overflow_span(slot) noexcept\` / \`overflow_span(slot) const noexcept\` | 返回某槽位的 overflow 块视图（block 1+），未分配则返回空 span |

#### 复制与交换

| 接口 | 说明 |
|------|------|
| \`multi_block_bitmask(const multi_block_bitmask&)\` | 深拷贝构造 |
| \`operator=(const multi_block_bitmask&)\` | 深拷贝赋值 |
| \`multi_block_bitmask(multi_block_bitmask&&)\` | 移动构造 |
| \`operator=(multi_block_bitmask&&)\` | 移动赋值 |
| \`swap(multi_block_bitmask&) noexcept\` | 成员交换 |
| \`clone() const\` | 显式深拷贝工厂 |
| 自由函数 \`swap(a, b) noexcept\` | 自由交换 |

#### 集合运算（原地，处理共同 slot 与共同块）

| 接口 | 说明 |
|------|------|
| \`and_with(const multi_block_bitmask& o)\` | \`this &= o\`（\`this\` 中 \`o\` 不存在的 slot 清零） |
| \`or_with(const multi_block_bitmask& o)\` | \`this \\|= o\`（仅处理共同 slot，不扩容） |
| \`xor_with(const multi_block_bitmask& o)\` | \`this ^= o\` |
| \`subtract(const multi_block_bitmask& o)\` | \`this &= ~o\`（差集） |
| \`overlaps(const multi_block_bitmask& o) const\` | 是否与 \`o\` 有任意共同置位 |
| \`equals(const multi_block_bitmask& o) const\` | 是否相等 |

#### 内存压缩

| 接口 | 说明 |
|------|------|
| \`compact_slot(slot)\` | 若该槽位 overflow 全零则释放 |
| \`compact_all()\` | 全局压缩所有全零 overflow |

### 使用

\`\`\`cpp
multi_block_bitmask masks;
masks.reserve_blocks(2);                    // 预分配 2 块 = 支持 128 位
masks.ensure_entity(0);
masks.set_bit(0, 0, 5);                     // 槽 0 块 0 位 5
masks.set_bit(0, 1, 10);                    // 槽 0 块 1 位 10
uint64_t b0 = masks.get_block(0, 0);
uint64_t b1 = masks.get_block(0, 1);
masks.clear_entity(0);                      // 清零整槽

// 查询
bool has = masks.test_bit(0, 0, 5);
bool any = masks.any_set(0);
uint32_t cnt = masks.count_set_bits(0);
uint32_t blk, off;
if (masks.find_first_set(0, blk, off)) { /* ... */ }

// 批量位操作
uint32_t offsets[] = {1, 3, 5, 7};
masks.set_bits_at(0, 0, offsets);

// 整块写入
masks.set_block_value(0, 0, 0xFFFFFFFFULL);
masks.or_block_value(0, 0, 0x1ULL << 10);

// 整槽读写
uint64_t data[] = {0xFF, 0xAA};
masks.assign_slot(0, data);
uint64_t out[2];
masks.copy_slot_to(0, out);

// 视图
std::span<const uint64_t> inline_view = masks.inline_span();
std::span<const uint64_t> ovf_view = masks.overflow_span(0);

// 遍历
masks.for_each_set_bit(0, [](uint32_t blk, uint32_t off) { /* ... */ });
masks.for_each_set_slot([](uint32_t slot) { /* ... */ });
masks.for_each_set_bit_global([](uint32_t slot, uint32_t blk, uint32_t off) { /* ... */ });
size_t total = masks.count_set_bits_global();

// 集合运算
multi_block_bitmask a, b;
a.reserve_blocks(2); b.reserve_blocks(2);
a.ensure_entity(0); b.ensure_entity(0);
a.set_bit(0, 0, 1); b.set_bit(0, 0, 2);
a.or_with(b);                             // a 现在 = {1, 2}
bool inter = a.overlaps(b);
bool sup = a.contains_all(b);
bool eq = a.equals(b);

// 复制与交换
multi_block_bitmask c = a.clone();
multi_block_bitmask d = std::move(c);
d.swap(a);
swap(d, a);                               // 自由 swap

// 内存压缩
masks.clear_bit(0, 1, 10);
masks.compact_slot(0);                     // overflow 全零则释放
masks.compact_all();                       // 全局压缩

// 扩容 / 缩容 / 状态查询 (与 class_pool / dense 命名一致)
masks.increase_capacity(50000);
size_t cap = masks.capacity();
size_t n   = masks.size();
masks.reduce_capacity(masks.size());
masks.reduce_capacity(1000);
masks.clear();
bool isEmpty = masks.empty();
size_t used  = masks.size_bytes();
size_t total_cap = masks.capacity_bytes();
size_t ovf_count = masks.overflow_entity_count();

// 静态辅助
constexpr uint32_t blk = multi_block_bitmask::block_count_for_bits(200);  // = 4
\`\`\`

### 批量注册推荐用法

逐个 \`ensure_entity\` 每次都做边界检查 + size 读取 + 可能的扩容。批量注册时用三步法减少重复检查:

\`\`\`cpp
multi_block_bitmask masks;
masks.reserve_blocks(2);                   // 步骤 0: 预分配块数 (必须先于步骤 1)

// 三步法批量注册 N 个槽位
masks.increase_capacity(N);                // 步骤 1: 扩 capacity
masks.resize_entities(N);                  // 步骤 2: 撑 size
for (uint32_t i = 0; i < N; ++i)
{
    masks.set_bit_no_check(i, 0, i & 63);  // 步骤 3: 跳过边界检查写入
}
\`\`\`

| 步骤 | 作用 | 跳过后果 |
|------|------|----------|
| \`increase_capacity(N)\` | 预留容量 | 不预留时按需扩展 |
| \`resize_entities(N)\` | 把 \`size()\` 撑到 N,使 \`set_bit_no_check\` 访问合法 | \`set_bit_no_check\` 越界写崩 |
| \`set_bit_no_check(...)\` | 跳过边界检查的写入 | 退回 \`set_bit\` 的带检查路径 |

> 注:\`increase_capacity\` 只扩 capacity 不动 size,\`set_bit_no_check\` 要求 \`slot < size()\`。
> 因此步骤 1 和步骤 2 **都不可省略**,否则 \`set_bit_no_check\` 越界。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 注册组件后再调 \`reserve_blocks\` | 触发现有溢出槽位扩容 | 启动时预估类型数，注册前调 \`reserve_blocks\` |
| 对未 \`ensure_entity\` 的槽位调 \`set_bit_no_check\` | 越界写崩 | 先 \`ensure_entity\`,或批量场景用 \`increase_capacity\` + \`resize_entities\` 三步法 |
| \`increase_capacity(N)\` 后直接 \`set_bit_no_check\` | capacity 扩了但 size 未撑,越界写崩 | 必须再调 \`resize_entities(N)\` 撑 size |
| 对 \`block_idx >= num_blocks()\` 调 \`set_bit\` | 静默丢弃（不会扩容块数） | 先 \`reserve_blocks\` 扩容 |
| 假设 \`get_block(slot, b)\` 跨进程稳定 | 布局可能变化 | 仅当前进程内有效 |
| \`reduce_capacity(n)\` 传小于当前 \`size\` 的值 | 超出部分被截断丢失 | 缩容前确认 \`n >= size()\`，或先 \`reduce_capacity(size())\` |
| \`clear()\` 后假设 \`num_blocks()\` 归 1 | \`clear\` 只清数据不重置块数 | 重置块数需重新构造实例 |
| 集合运算后假设 \`this\` 的 \`num_blocks()\` 与 \`o\` 一致 | 集合运算不扩容块数，仅处理共同块 | 需要扩容先调 \`reserve_blocks\` |
| \`overflow_span(slot)\` 返回的 span 跨 \`reserve_blocks\` 使用 | \`reserve_blocks\` 会重分配 overflow 内存，span 失效 | 视图即时使用，不跨写操作持有 |

---
`
};
