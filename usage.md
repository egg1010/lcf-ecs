# lcf-ecs 库接口文档

包含 `component.hpp` 即可使用。

## 目录

- [lcf-ecs 库接口文档](#lcf-ecs-库接口文档)
  - [目录](#目录)
  - [一、库使用接口](#一库使用接口)
    - [1. ecs::entity — 实体](#1-ecsentity--实体)
    - [2. operating_message — 操作消息](#2-operating_message--操作消息)
    - [3. class_pool\<T\> — 核心容器](#3-class_poolt--核心容器)
    - [4. void_any — 类型擦除存储](#4-void_any--类型擦除存储)
    - [5. type_id — 类型ID](#5-type_id--类型id)
    - [6. id_allocation\<T\> — ID分配器](#6-id_allocationt--id分配器)
    - [7. memory_pool — 内存池](#7-memory_pool--内存池)
    - [8. ecs::single_class_set — 单组件集合](#8-ecssingle_class_set--单组件集合)
    - [9. ecs::manager — ECS管理器](#9-ecsmanager--ecs管理器)
    - [10. View系统](#10-view系统)
    - [11. Group系统](#11-group系统)
    - [12. runtime_view — 运行时视图](#12-runtime_view--运行时视图)
    - [13. 函数存储（回调作为组件）](#13-函数存储回调作为组件)
    - [14. 生命周期信号](#14-生命周期信号)
    - [15. command_buffer — 延迟结构变更](#15-command_buffer--延迟结构变更)
  - [二、宏配置](#二宏配置)
    - [16. 编译与运行](#16-编译与运行)
    - [17. 可选宏配置](#17-可选宏配置)
  - [三、各种模块](#三各种模块)
    - [18. tiered_sort / pdqsort / sort_n — 分级排序](#18-tiered_sort--pdqsort--sort_n--分级排序)
    - [19. radix_sort — 基数排序](#19-radix_sort--基数排序)
    - [20. FORCE_INLINE — 跨平台内联宏](#20-force_inline--跨平台内联宏)
    - [21. view_tags — 视图标签类型](#21-view_tags--视图标签类型)
    - [22. arena_allocator — 线性 bump 分配器](#22-arena_allocator--线性-bump-分配器)
    - [23. slab_allocator — 固定块分配器](#23-slab_allocator--固定块分配器)
    - [24. layered_allocator — 分层分配器](#24-layered_allocator--分层分配器)
    - [25. ring_buffer — 环形缓冲区](#25-ring_buffer--环形缓冲区)
    - [26. time — 计时与基准测量](#26-time--计时与基准测量)
    - [27. entity_mask_manager — 无上限实体掩码存储](#27-entity_mask_manager--无上限实体掩码存储)

---


# 一、库使用接口

## 1. ecs::entity — 实体

实体是轻量级句柄，由 32 位索引和 32 位版本号组成，合并存储在 64 位 `handle_` 中。

### 接口

| 接口 | 说明 |
|------|------|
| `entity()` | 默认构造，`handle_=0`，无效实体 |
| `entity(uint32_t idx, uint32_t ver)` | 指定索引和版本号构造 |
| `is_valid()` | 判断实体是否有效（`handle_ != 0`） |
| `index_` | 实体索引（32 位） |
| `version_` | 实体版本号（32 位） |
| `handle_` | 64 位句柄（与 index_ + version_ 共用联合体） |
| `operator==` | 判断两个实体是否相等（比较 handle_） |
| `operator!=` | 判断两个实体是否不等 |
| `std::hash<entity>` | 哈希特化，可用于 `std::unordered_map` |

### 使用

```cpp
entity e1;                    // 默认构造，无效实体
entity e2(3, 1);              // index=3, version=1
e2.is_valid();                // true
e2.index_;                    // 3
e2.version_;                  // 1

std::unordered_map<entity, int> map;
map[e2] = 42;                 // 可用作哈希键
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 手动构造 entity 的 index_ 和 version_ | 版本号不匹配会导致 ECS 管理器认为实体无效 | 始终通过 `manager::create_entity()` 创建实体 |
| 复用已删除的 entity 句柄 | 版本号已递增，旧句柄失效 | 删除后丢弃句柄，重新创建 |
| 将 entity 成员当作普通整数运算 | `index_` 和 `version_` 是内部实现细节 | 仅通过公开接口操作 entity |

---

## 2. operating_message — 操作消息

记录操作结果（成功/失败）和调试信息。核心特性：

- **值语义返回**：`single_class_set` / `manager` 的 `add` / `add_batch` / `hard_remove` / `soft_remove` 均按值返回 `operating_message`，C++17 RVO 保证零拷贝。容器自身不持有 `operating_message` 成员，每次操作返回独立结果
- **粘性 false 语义**：单个返回值对象一旦失败就保持 false，只有 `reset()` 能恢复
- **全局开关**：`ecs_debug_messages()` 运行时控制是否写入字符串
- **日志级别过滤**：`msg_level` 枚举 + `min_level_` 表驱动过滤，被过滤的级别跳过全部格式化
- **类型特化写入**：`write_message` 对整型/浮点走 `std::to_chars`，对字符串走 `append`，其他类型才退化到 `std::format_to`

### msg_level 日志级别

```cpp
enum class msg_level : uint8_t {
    debug = 0,  // 最低
    info  = 1,  // 默认 min_level
    warn  = 2,
    error = 3   // 最高
};
```

级别用 `uint8_t` 存储，过滤仅做一次整数比较（`lv < min_level_` 返回），前缀通过 `k_level_prefix[]` 字符串数组索引写入，无 switch-case 分支。

### 接口

| 接口 | 说明 |
|------|------|
| `ecs_debug_messages()` | 全局开关引用（控制是否写入字符串） |
| `msg_level` | 日志级别枚举（debug/info/warn/error） |
| `operating_message()` | 默认构造，`switch_=true`，`min_level_=info` |
| `operator bool()` | 是否成功（返回 `switch_`） |
| `reset()` | 重置为成功并清空消息 |
| `clear_message()` | 仅清空消息字符串 |
| `set_switch_bool(bool)` | 直接设置开关值 |
| `get_switch_bool()` | 获取开关引用 |
| `get_switch_bool() const` | 获取开关 const 引用 |
| `set_min_level(msg_level)` | 设置最低记录级别（默认 info） |
| `get_min_level()` | 获取当前最低记录级别 |
| `reserve(size_t)` | 预分配消息缓冲区（避免循环内重分配） |
| `capacity()` | 当前缓冲区容量 |
| `message_size()` | 当前消息长度 |
| `write_message(bool sw, Args... args)` | 写入消息（`sw=false` 标记失败，粘性）；整型/浮点走 to_chars |
| `write_message_level(lv, sw, Args...)` | 带级别的写入（级别不足则跳过，自动加前缀） |
| `write_message_fmt(bool sw, fmt, Args...)` | 格式化写入消息（`std::format_to`） |
| `write_message_fmt_level(lv, sw, fmt, Args...)` | 带级别的格式化写入 |
| `read_message()` | 读取消息字符串（返回 `string_view`） |
| `operator+=(string_view)` | 追加字符串到消息 |
| `operator+=(operating_message&&)` | 合并右值消息（`switch_ = switch_ && other.switch_`） |
| `operator+=(const operating_message&)` | 合并左值消息 |
| `operator<<(ostream, operating_message)` | 输出到 `ostream` |
| `operating_message(operating_message&&)` | 移动构造 |
| `operator=(operating_message&&)` | 移动赋值 |
| `operating_message(const operating_message&)` | 拷贝构造 |
| `operator=(const operating_message&)` | 拷贝赋值 |

### 使用

```cpp
operating_message msg;
msg.write_message(false, "Error: ", "file not found");
if (!msg) { /* 处理错误 */ }

// 粘性 false：write_message(true) 不会恢复
msg.write_message(true, "This won't recover");
// msg 仍为 false

msg.reset();  // 恢复为 true

// 全局开关
ecs_debug_messages() = false;  // 禁用字符串写入
ecs_debug_messages() = true;   // 启用字符串写入

// 格式化
msg.write_message_fmt(true, "x={}, y={}", 10, 20);

// 合并
operating_message msg2;
msg2.write_message(false, "Another error");
msg += msg2;  // switch_ = msg.switch_ && msg2.switch_

// 日志级别过滤
msg.set_min_level(msg_level::warn);       // 只记 warn 及以上
msg.write_message_level(msg_level::info, true, "这条被过滤");  // 不写入
msg.write_message_level(msg_level::error, true, "严重错误: ", 42);
// message_ = "[ERROR] 严重错误: 42\n"

// 级别格式化
msg.write_message_fmt_level(msg_level::warn, true, "v={} k={}", 1, "x");
// message_ += "[WARN]  v=1 k=x\n"

// 整型/浮点走 to_chars 快速路径（比 format 快）
msg.write_message(true, "i=", 100, " d=", 3.14);

// 预分配缓冲区（循环场景避免首次分配）
msg.reserve(4096);
for (int i = 0; i < 1000; ++i) {
    msg.reset();
    msg.write_message(true, "iter ", i);
}
```

### 性能说明

- **`write_message`（整型参数）**：走 `std::to_chars`，比 `std::format_to` 快约 30%
- **`write_message_fmt`**：fast path 检测 — 若格式串仅含简单 `{}` 占位符（无 `{{` `}}` 转义、无 `{:spec}` 复杂格式），走 `append_arg` 的 `to_chars` / 字符串 append 路径，绕过 `std::format_to` + `back_inserter`，吞吐 38M 次/秒（与 `write_message` 相当）；复杂格式仍走 `std::format_to` 通用路径
- **级别过滤快速路径**：被过滤的级别仅做一次整数比较 + reset，吞吐约 2800M 次/秒
- **`reserve`**：循环 `reset()` 不释放容量，首次写入后无需重分配；`reserve` 用于避免首次分配

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 依赖 `write_message(true)` 恢复失败状态 | 粘性 false 语义，成功后不会恢复 | 调用 `reset()` 显式恢复 |
| 在 Release 构建中依赖 `read_message()` | 全局开关关闭时字符串为空 | 使用 `operator bool()` 判断成败，而非消息内容 |
| 忘记检查 `operator bool()` | 操作失败被静默忽略 | 每次关键操作后检查 `if (!msg) { ... }` |
| 高频日志用 `{:08x}` 等复杂格式 | 走 `std::format_to` 通用路径，吞吐约 14M 次/秒 | 高频路径仅用 `{}` 简单占位符以走 fast path |
| 默认级别记录所有 debug 日志 | Release 中 debug 日志拖累性能 | `set_min_level(msg_level::warn)` 过滤低级别 |

---

## 3. class_pool\<T> — 核心容器

基于 bitmap 稀疏集的容器，替代 `std::vector`。

**两种模式：**

| 模式 | 触发条件 | 迭代行为 |
|------|---------|---------|
| **dense（密集）** | `index_` 范围内所有位均为 1（无空洞） | 线性扫描 `[0, index_)`，无 bitmap 跳转 |
| **sparse（稀疏）** | `index_` 范围内存在空洞（有未构造的槽位） | 自动跳过未构造槽位，仅遍历已构造元素 |

**模式切换：** 自动判断，无需手动干预。

### 构造与赋值

| 接口 | 说明 |
|------|------|
| `class_pool()` | 默认构造 |
| `class_pool(size_t capacity)` | 预留容量构造 |
| `class_pool(size_t count, const T& value)` | 构造 count 个 value 副本 |
| `class_pool(InputIt first, InputIt last)` | 迭代器范围构造 |
| `class_pool(initializer_list<T>)` | 初始化列表构造 |
| `class_pool(const class_pool&)` | 拷贝构造 |
| `class_pool(class_pool&&)` | 移动构造 |
| `operator=(const class_pool&)` | 拷贝赋值 |
| `operator=(class_pool&&)` | 移动赋值 |

### 元素访问

| 接口 | 说明 |
|------|------|
| `operator[](size_t)` | 下标访问（无边界检查） |
| `at(size_t)` | 下标访问（越界时 `terminate`） |
| `front()` | 首元素引用 |
| `back()` | 尾元素引用 |
| `get(size_t)` | 获取指定位置指针 |
| `data()` | 原始数据指针 |
| `span()` | 返回 `std::span<T>` |
| `span() const` | 返回 `std::span<const T>` |
| `dense_view()` | 返回 `dense_view`（原始指针范围视图，仅 dense 模式可用） |
| `dense_view() const` | 返回 `const_dense_view`（const 版本） |

### 容量

| 接口 | 说明 |
|------|------|
| `size()` | 已使用大小 |
| `capacity()` | 总容量 |
| `sparse_capacity()` | 稀疏集容量（同 capacity） |
| `empty()` | 是否为空 |
| `count()` | 已构造元素数（bitmap popcount，带缓存） |
| `valid()` | 是否已分配 |
| `size_bytes()` | 已使用字节数 |
| `capacity_bytes()` | 总容量字节数 |

### 修改器

| 接口 | 说明 |
|------|------|
| `emplace_back(Args...)` | 尾部构造元素 |
| `push_back_unchecked(const T&)` | 尾部拷贝追加（跳过 bitmap 设置，仅 dense 连续模式可用） |
| `emplace_back_unchecked(Args...)` | 尾部原地构造（跳过 bitmap 设置，仅 dense 连续模式可用） |
| `emplace_back_dense_unchecked(Args...)` | 尾部原地构造（仅设置当前位 bitmap，dense 模式快速路径） |
| `append_n(n, const T&)` | 批量追加 n 个 value 副本（AVX2 批量设置 bitmap，1M int 仅 0.51ms，比逐个 emplace_back 快 3x） |
| `emplace(pos, Args...)` | 在指定位置插入（移动后续元素） |
| `emplace_at(index, Args...)` | 任意位置构造（get-or-create：已存在则返回现有值，不覆盖） |
| `sparse_emplace_at(index, Args...)` | 任意位置构造（insert-or-assign：已存在则覆盖） |
| `sparse_erase_at(index)` | 稀疏删除（仅析构并清除 bitmap，不移动元素） |
| `erase(pos)` | 删除指定位置元素（移动后续元素填补） |
| `erase(first, last)` | 删除范围元素 |
| `pop_back()` | 删除尾部元素 |
| `clear()` | 清空所有元素 |
| `increase_capacity(capacity)` | 扩容（不增加元素） |
| `increase_capacity(capacity, value)` | 扩容并填充值到新槽位 |
| `reduce_capacity(capacity)` | 缩容（截断超出元素） |
| `reduce_capacity(capacity, dst)` | 缩容，超出元素移至 dst |
| `shrink_to_fit()` | 缩容至 `size()` |
| `reserve_exact(capacity)` | 精确扩容（不填充值，不增加元素，分配到精确大小） |
| `resize(size_t, const T& value)` | 调整大小并填充值（支持缩小） |
| `swap(other)` | 交换两个容器 |

### 稀疏集/Bitmap

| 接口 | 说明 |
|------|------|
| `is_constructed_at(index)` | 检查指定位置是否已构造 |
| `is_dense()` | 前 `index_` 位是否全部为 1 |
| `invalidate_count_cache()` | 使 count 缓存失效 |

### 各操作对 contiguity 的影响

| 操作 | 对 contiguity 的影响 |
|------|---------------------|
| `emplace_back()` | 保持连续 |
| `emplace(pos)` / `insert(pos)` | 保持连续（元素右移） |
| `erase(pos)` / `erase(first,last)` | 保持连续（元素左移） |
| `pop_back()` | 保持连续 |
| `clear()` | 重置为连续 |
| `sparse_erase_at(index)` | **产生空洞** → 变稀疏 |
| `emplace_at(index)` | **可能填充空洞** → 可能变连续 |
| `sparse_emplace_at(index)` | **可能填充空洞** → 可能变连续 |
| `resize()` 缩小 | **可能消除空洞** → 可能变连续 |
| `reduce_capacity()` 缩小 | **可能消除空洞** → 可能变连续 |

### 插入/删除

| 接口 | 说明 |
|------|------|
| `emplace(const_iterator pos, args...)` | 在指定位置原位构造，返回指向新元素的迭代器 |
| `insert(const_iterator pos, const T&)` | 拷贝插入 |
| `insert(const_iterator pos, T&&)` | 移动插入 |
| `erase(const_iterator pos)` | 删除指定位置元素，返回下一个迭代器 |
| `erase(const_iterator first, const_iterator last)` | 范围删除，返回下一个迭代器 |

### 迭代器

| 接口 | 说明 |
|------|------|
| `begin()` / `end()` | sparse-aware 正向迭代器（dense 模式零开销，sparse 模式自动跳过未构造槽位） |
| `cbegin()` / `cend()` | const 版本 |
| `rbegin()` / `rend()` | 反向迭代器（bidirectional，sparse 模式同样自动跳过未构造槽位） |
| `crbegin()` / `crend()` | const 反向版本 |
| `for_each(F&& f)` | 批量遍历回调，dense 路径可被编译器自动向量化（比 range-for 快约 15%） |
| `for_each(F&& f) const` | const 版本 |

### dense_view — 密集遍历视图

`dense_view` 是一个轻量视图类型，持有两个原始指针 `begin_ptr` / `end_ptr`，`begin()` / `end()` 直接返回裸 `T*`。与迭代器不同，它不携带 bitmap 指针，编译器可将其完全放入寄存器，遍历开销与 `std::vector` 的 range-for 相同。

#### 机制

- `dense_view()` 返回 `{data_ptr_, data_ptr_ + index_}`，不检查 `is_dense_`
- 基于范围的 for 循环展开后等价于 `for (T* p = data_ptr_; p != data_ptr_ + index_; ++p)`
- 无 bitmap 分支判断，无 `skip_to_next_valid()` 调用

#### 何时使用

| 条件 | 推荐 |
|------|------|
| 已确认 `is_dense() == true` | `for (auto& x : pool.dense_view())` |
| 不确定模式 | `for (auto& x : pool)` 或 `pool.for_each(cb)` |
| sparse 模式 | `pool.for_each(cb)`（word 级批量提取） |

#### 前置条件

**调用前必须确认 `is_dense()` 为 true。** 在 sparse 模式下使用 `dense_view()` 会遍历到未初始化槽位的垃圾数据，导致未定义行为。

```cpp
class_pool<int> pool;
pool.append_n(1'000'000, 0);

if (pool.is_dense())
{
    // 安全: 已确认 dense 模式
    for (auto& x : pool.dense_view())
    {
        x += 1;
    }
}
```

### 自由函数

| 接口 | 说明 |
|------|------|
| `swap(class_pool&, class_pool&)` | 交换两个容器 |

### 使用

```cpp
class_pool<int> pool;
pool.emplace_back(10);
pool.emplace_back(20);

// 任意位置构造（get-or-create）
pool.emplace_at(100, 999);      // 在索引 100 构造
pool.emplace_at(100, 888);      // 已构造，返回现有值（不覆盖）

// insert-or-assign 语义
pool.sparse_emplace_at(100, 777);  // 覆盖

// 稀疏删除
pool.sparse_erase_at(100);
pool.is_dense();                   // false（索引 100 处产生空洞）

// 填满空洞后自动切回连续
pool.emplace_at(100, 123);         // 填充空洞
pool.is_dense();                   // true（空洞消失，自动切回）

// unchecked 快速追加（仅 dense 连续模式，跳过 bitmap 设置）
class_pool<int> dense_pool;
dense_pool.emplace_back(1);
dense_pool.emplace_back(2);
dense_pool.push_back_unchecked(3);              // 跳过 bitmap，更快
dense_pool.emplace_back_unchecked(4);           // 跳过 bitmap，更快
dense_pool.emplace_back_dense_unchecked(5);     // 仅设置当前位 bitmap

// 批量插入最佳实践（方案 F）：reserve + unchecked 组合，性能接近 std::vector
class_pool<int> batch_pool;
batch_pool.reserve_exact(1'000'000);            // 预分配，避免多次扩容
for (int i = 0; i < 1'000'000; ++i) {
    batch_pool.emplace_back_dense_unchecked(i); // dense 快速路径，已预分配无容量检查
}
// 实测：reserve + emplace_back_dense_unchecked 比 emplace_back 快约 20%（跳过 invalidate_count_cache）

// 批量插入最快路径（方案 J）：append_n 单次调用批量追加
class_pool<int> fastest;
fastest.append_n(1'000'000, 0);  // 直接追加 1M 个 0, AVX2 批量设置 bitmap
// 实测：1M int 仅 0.51ms, 比逐个 emplace_back 快 3x, 比 std::vector emplace_back 快 2.2x

// 精确扩容（不增加元素，分配到精确大小）
class_pool<int> reserved;
reserved.reserve_exact(1000);        // capacity >= 1000，size 不变
reserved.resize(10, 0);             // 调整 size 到 10 并填充 0

// 位置插入
pool.emplace(std::next(pool.begin(), 1), 42);  // 在位置 1 插入

// 迭代
for (auto v : pool) { /* ... */ }

// span
std::span<int> s = pool.span();

// dense_view: 已确认 dense 模式时的最快遍历
if (pool.is_dense())
{
    for (auto& x : pool.dense_view())
    {
        x += 1;
    }
}

// 稀疏集查询
pool.is_constructed_at(100);  // true
pool.is_dense();              // 检查是否连续
```

### 应该用什么操作？

| 场景 | 推荐操作 | 原因 |
|------|---------|------|
| 尾部追加元素 | `emplace_back()` | O(1)，保持连续 |
| 任意位置插入/删除并保持连续 | `emplace(pos)` / `erase(pos)` | 移动后续元素，O(n)，保持连续 |
| 稀疏数组（大索引跳跃） | `emplace_at()` / `sparse_erase_at()` | O(1)，不移动其他元素，但产生空洞 |
| 批量填充已知索引 | `emplace_at()` | 填充空洞后自动切回连续 |
| 删除整个容器 | `clear()` | 重置为连续 |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `sparse_erase_at()` 后仍期望连续迭代 | 产生空洞，迭代变稀疏模式 | 用 `emplace_at()` 填充空洞，或用 `erase()` 替代 |
| 频繁 `sparse_erase_at()` + `emplace_at()` 来回切换 | 每次切换触发模式扫描 | 批量操作，或统一使用 `erase()`/`emplace()` 保持连续 |
| `emplace_at()` 在远超 `index_` 的索引上构造 | 中间留大量未初始化槽位，`size()` 暴增 | 用 `resize(n, value)` 预填充，或改用 `sparse_emplace_at()` |
| 在 sparse 模式下使用 `data()` + `span()` 做线性遍历 | 未初始化槽位包含垃圾数据 | 始终通过迭代器遍历，或先确认 `is_dense()` 为 true |
| 在未确认 `is_dense()` 的情况下使用 `dense_view()` 遍历 | sparse 模式下会访问到未初始化槽位的垃圾数据，属于未定义行为 | 调用前必须判断 `is_dense() == true`，或使用 `for_each(cb)` / 迭代器 |
| `emplace_at()` 期望覆盖已有值 | `emplace_at` 是 get-or-create，不覆盖 | 使用 `sparse_emplace_at()` 实现 insert-or-assign |
| 在 sparse 模式下使用 `push_back_unchecked` / `emplace_back_unchecked` | 不设置 bitmap 位，迭代器无法看到新元素 | 仅在已知 dense 连续模式下使用，或用 `emplace_back()` 替代 |

### fill_the_hole — 填洞或追加

`class_pool<T>` 的填洞接口，复用内部 `hole_count_` 与 `sparse_bits_`，零额外内存。`fill_the_hole` 优先填第一个空洞（最低索引的 bitmap=0 位），无洞则 `emplace_back` 末尾追加。

#### 机制

| 操作 | 行为 |
|------|------|
| `fill_the_hole(args...)` | `hole_count_==0` → `emplace_back`；有洞 → `find_first_hole_` 扫描 bitmap 找首个 0 位 → `emplace_at` 填洞（自动 `--hole_count_`） |
| `sparse_erase_at(idx)` | 产生空洞，`++hole_count_`（已删除位置不重复计数，`bitmap_test` 检查） |

- fast path（`hole_count_==0`）：一次比较短路 + `emplace_back`，与直接 `emplace_back` 几乎无差异
- slow path（有洞）：bitmap 字扫描 + `std::countr_one` 单指令定位首个 0 位，平均 first word 命中，接近 O(1)
- 不新增成员变量，不影响其他接口性能
- 填洞顺序：**最低索引优先**（非 LIFO），找 `[0, index_)` 范围内第一个 bitmap=0 位

#### 接口

| 接口 | 说明 |
|------|------|
| `fill_the_hole(args...)` | 填第一个空洞或末尾追加，返回 `T&` |
| `fill_the_hole_at(args...)` | 同 `fill_the_hole` 语义，但返回被填补位置的索引 `size_t`（填洞返回洞索引，追加返回末尾索引），调用方可通过 `operator[](idx)` 或 `data_ptr_[idx]` 访问元素 |

填洞依赖现有接口：`sparse_erase_at` 产生空洞、`emplace_at` 填洞、`emplace_back` 追加。

#### 使用

```cpp
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
```

#### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 期望 `fill_the_hole` 填最近删除的空洞 | 最低索引优先，非 LIFO | 如需 LIFO 顺序，自行维护栈 |
| 混用 compacting `erase(iterator)` | 索引前移导致空洞位置变化 | 填洞场景用 `sparse_erase_at` |
| `sparse_erase_at` 后期望 `is_dense()` 为 true | 产生空洞变稀疏 | `fill_the_hole` 填满后自动恢复 |
| 对已删除位置重复 `sparse_erase_at` | `bitmap_test` 检查，不重复计数 | 删除前可 `is_constructed_at` 检查 |

---

## 4. void_any — 类型擦除存储

使用 `type_id` 进行类型识别。支持 SSO 和内存池（通过宏配置）。

### 内存布局

`sizeof(void_any) == 64`（恰好 1 个 cache line），由两部分组成：

| 字段 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| `storage_` | 0 | 56 | SSO 缓冲区（内联存储小对象）或 `void*` 指针（heap） |
| `vtable_sso_type_` | 56 | 8 | 位编码字段：`[63:48]`=type_id(16位)，`[47:1]`=vtable指针，`[0]`=SSO标志 |

位编码利用 x86-64 用户空间指针高 16 位为 0 的特性，将 `type_id` 编码进 `vtable_sso_type_` 高位，使 `type_id()` 和 `get_ptr<T>()` 无需解引用 vtable 即可完成类型检查。

### vtable 与函数指针跳过

每个类型对应一个静态 `vtable`，含 `type_id`、`element_size`、`destroy`、`copy_to`、`move_to`、`clone` 函数指针。对 `std::is_trivially_copyable_v<T>` 为真的类型：

- `copy_to` / `move_to` / `destroy`（SSO 路径）设为 `nullptr`
- 拷贝/移动时直接 `memcpy`（编译器自动向量化为 AVX2）
- 析构时跳过函数调用

非平凡可拷贝类型仍走函数指针路径，保证语义正确（如深拷贝、自定义析构）。

### 构造与赋值

| 接口 | 说明 |
|------|------|
| `void_any()` | 默认构造，空值 |
| `void_any(T&&)` | 从任意类型构造（排除 `void_any` 自身） |
| `void_any(const void_any&)` | 拷贝构造 |
| `void_any(void_any&&)` | 移动构造 |
| `operator=(const void_any&)` | 拷贝赋值 |
| `operator=(void_any&&)` | 移动赋值 |

### 访问与操作

| 接口 | 说明 |
|------|------|
| `set(T&&)` | 设置新值（先析构旧值再构造新值） |
| `type_id()` | 获取类型 ID（空值返回 -1） |
| `get_ptr<T>()` | 获取指针（带类型检查，不匹配返回 nullptr） |
| `get_ptr<T>() const` | const 版本 |
| `fast_get_ptr<T>()` | 快速获取（跳过 type_id 检查） |
| `fast_get_ptr<T>() const` | const 版本 |
| `get_ptr_unchecked<T>()` | 无检查获取（不验证 has_value 和 type_id） |
| `get_ptr_unchecked<T>() const` | const 版本 |
| `get<T>()` | 获取值副本（空值或类型不匹配返回默认构造） |
| `has_value()` | 是否有值 |
| `reset()` | 清空（析构并置空） |

### 使用

```cpp
void_any a(42);
a.has_value();                 // true
a.type_id();                   // int 的 ID

int* p = a.get_ptr<int>();     // 带类型检查
int* pf = a.fast_get_ptr<int>();  // 无 type_id 检查
int* pu = a.get_ptr_unchecked<int>();  // 无检查
int val = a.get<int>();        // 获取值副本

double* pd = a.get_ptr<double>();  // 类型不匹配返回 nullptr

a.set(99);                     // 设置新值
a.reset();                     // 清空

// 拷贝与移动
void_any b(std::string("world"));
void_any b_copy(b);            // 拷贝构造
void_any b_move(std::move(b)); // 移动构造
void_any b_assign;
b_assign = b_copy;             // 拷贝赋值
void_any b_move_assign;
b_move_assign = std::move(b_move);  // 移动赋值
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 使用 `get_ptr_unchecked` 前不检查 `has_value()` 和类型 | 返回悬垂指针，未定义行为 | 仅在确定类型和值存在时使用，否则用 `get_ptr<T>()` |
| 依赖 `get<T>()` 返回默认值来判断类型 | 默认构造值可能与实际值相同 | 先用 `get_ptr<T>()` 检查指针是否为空 |
| 移动后继续使用 | 移动后源对象为空 | 移动后仅可调用 `reset()` 或重新赋值 |
| 在 `set()` 之前访问 | `has_value()` 为 false，get_ptr 返回 nullptr | 先 `set()` 或构造时传值 |

---

## 5. type_id — 类型ID

为每种类型分配唯一整数 ID（编译时确定，线程安全）。

### 接口

| 接口 | 说明 |
|------|------|
| `type_id::get_type_id<T>()` | 获取类型 T 的唯一 ID（静态函数） |

### 使用

```cpp
int id1 = type_id::get_type_id<int>();
int id2 = type_id::get_type_id<double>();
assert(type_id::get_type_id<int>() == id1);  // 同类型 ID 相同
```

---

## 6. id_allocation\<T> — ID分配器

管理可回收的 ID 池，避免 ID 无限增长。默认模板参数为 `size_t`。

### 接口

| 接口 | 说明 |
|------|------|
| `get_id()` | 获取一个 ID（优先回收已释放的，否则递增） |
| `free_id(T id)` | 释放 ID（放入回收池） |
| `total_number_of_ids()` | 回收池大小 |
| `maximum_id()` | 已分配的最大 ID |

### 使用

```cpp
id_allocation<uint32_t> alloc;
uint32_t id1 = alloc.get_id();  // 1
uint32_t id2 = alloc.get_id();  // 2
alloc.free_id(id1);             // 释放 1
uint32_t id3 = alloc.get_id();  // 1（复用）
```

---

## 7. memory_pool — 内存池

基于 TLSF（Two-Level Segregated Fit）算法的分桶式内存池，减少频繁 malloc/free 开销。

### 内存布局

每个块由 16 字节 `block_header` + 用户数据区组成：

| 字段 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| `block_header.size_` | 0 | 8 | 块大小（低 1 位为 in_use 标志） |
| `block_header.prev_physical_` | 8 | 8 | 物理前驱块指针（用于合并） |
| 用户数据区 | 16 | size | `allocate` 返回的指针 |

空闲块的数据区前 16 字节复用为链表节点（`free_node`：`next_`/`prev_`），不额外占空间。`allocate(16)` 实际占用 32 字节（header 16 + 数据 16），小对象利用率高。

### memory_block — 内存块

| 接口 | 说明 |
|------|------|
| `memory_block()` | 默认构造，空块 |
| `memory_block(uint8_t* data, size_t size)` | 指定数据和大小构造 |
| `memory_block(memory_block&&)` | 移动构造 |
| `operator=(memory_block&&)` | 移动赋值 |
| `data_` | 数据指针（`uint8_t*`） |
| `size_` | 数据大小（`size_t`） |

> 注：`memory_block` 禁止拷贝。

### pool_stats — 统计信息

| 字段 | 说明 |
|------|------|
| `total_allocated` | 已分配 chunk 总量 |
| `total_used` | 用户使用量（含 header） |
| `total_free` | 空闲量（含 header） |
| `free_block_count` | 空闲块数量 |
| `max_contiguous_free` | 最大连续空闲块大小 |
| `fragmentation` | 碎片率 [0,1]，`1 - max_contiguous_free / total_free` |

### memory_pool — 内存池

| 接口 | 说明 |
|------|------|
| `memory_pool(size_t chunk_size = 4096)` | 构造，指定块大小 |
| `memory_pool(memory_pool&&)` | 移动构造 |
| `operator=(memory_pool&&)` | 移动赋值 |
| `allocate(size_t size)` | 分配内存，返回 16 字节对齐指针 |
| `deallocate(void* ptr)` | 释放内存（自动合并相邻块） |
| `construct<T>(Args...)` | 分配并构造对象 |
| `destroy<T>(T* ptr)` | 析构并释放对象 |
| `total_allocated()` | 已分配总量 |
| `total_used()` | 已使用量 |
| `chunk_size()` | 获取块大小 |
| `empty()` | 是否空闲（`total_used_ == 0`） |
| `owns(const void* ptr)` | 判断指针是否属于本池 |
| `stats()` | 返回 `pool_stats` 统计信息 |
| `iterate_free(Fn&& fn)` | 遍历空闲块，回调签名 `void(void* data_ptr, size_t block_size)` |
| `increase_capacity(size_t size)` | 扩容（只扩容不缩容） |
| `reduce_capacity(size_t target)` | 缩容（只缩容不扩容，释放空闲 chunk 直到总量 <= target） |
| `reset()` | 释放所有内存块，回到初始状态 |

> 注：`memory_pool` 禁止拷贝。

### 使用

```cpp
memory_pool pool;
void* p = pool.allocate(64);
pool.deallocate(p);

std::string* s = pool.construct<std::string>("hello");
pool.destroy(s);

pool.increase_capacity(1024 * 1024);  // 扩容至 1MB
pool.reduce_capacity(4096);          // 缩容，释放空闲 chunk 至总量 <= 4096
pool.reset();                        // 释放所有，回到初始状态

// owns: 判断指针归属
void* q = pool.allocate(32);
bool in_pool = pool.owns(q);         // true
int stack_var = 0;
bool in_stack = pool.owns(&stack_var); // false

// stats: 统计信息
pool_stats s = pool.stats();
// s.total_allocated, s.total_used, s.total_free
// s.free_block_count, s.max_contiguous_free, s.fragmentation

// iterate_free: 遍历空闲块
size_t free_count = 0;
pool.iterate_free([&](void* data_ptr, size_t block_size) {
    ++free_count;
    // data_ptr: 空闲块数据区指针, block_size: 空闲块大小
});
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用 `new`/`delete` 管理 `construct` 分配的对象 | 内存池有自己的分配器，`delete` 会崩溃 | 始终用 `destroy<T>()` 释放 |
| `allocate` 后忘记 `deallocate` | 内存泄漏 | 每次 `allocate` 配对一个 `deallocate` |
| 拷贝 `memory_pool` | 禁止拷贝，内部指针所有权混乱 | 使用移动语义或引用传递 |
| 在 `iterate_free` 回调中修改池状态 | 遍历中增删块会破坏链表 | 仅在回调中读取信息，不调用 `allocate`/`deallocate` |
| 依赖 `owns()` 区分池内不同块 | `owns` 仅判断指针是否属于本池，不区分具体块 | 用 `stats()`/`iterate_free()` 获取块信息 |

### arena_allocator — 线性 bump 分配器

线性 bump 分配器，无 header，无单个 deallocate，仅 `reset()` 整体回收。两种模式：自有内存（析构释放）/ 借用外部 buffer（零所有权）。适合 command_buffer 等批量分配、整体回收场景。

#### 内存布局

`base_` 64 字节对齐（cache line），`allocate(n, align)` 支持 `align <= 64`（覆盖 AVX2 32 / AVX512 64）。无 per-block header，零开销。bump 分配用位运算对齐 offset，无分支。

#### 接口

| 接口 | 说明 |
|------|------|
| `arena_allocator()` | 默认构造，空 |
| `arena_allocator(size_t capacity)` | 自有模式：分配 capacity 字节，析构释放 |
| `arena_allocator(void* buffer, size_t size)` | 借用模式：使用外部 buffer，不分配不释放 |
| `arena_allocator(arena_allocator&&)` | 移动构造（原对象置空防 double free） |
| `operator=(arena_allocator&&)` | 移动赋值 |
| `allocate(size_t n, size_t align = 16)` | bump 分配，位运算对齐，溢出返回 nullptr |
| `reset()` | 整体回收，offset 归零（不调用析构） |
| `used()` | 已用字节数 |
| `capacity()` | 总容量 |
| `remaining()` | 剩余字节数 |
| `empty()` | 是否已 reset（offset == 0） |
| `owns(const void* p)` | 判断指针是否属于本 arena |

> 注：禁止拷贝；无单个 deallocate，必须 `reset()` 整体回收。

#### 使用

```cpp
// 自有模式
arena_allocator ar(4096);
void* p = ar.allocate(128);
void* q = ar.allocate(64, 32);  // 32 字节对齐
ar.reset();  // 整体回收，p/q 失效

// 借用模式（零所有权，不分配不释放）
uint8_t buf[1024];
arena_allocator ar2(buf, sizeof(buf));
void* r = ar2.allocate(32);

// owns: 判断指针归属
bool in_arena = ar.owns(p);   // true（reset 前）
int stack_var = 0;
bool in_stack = ar.owns(&stack_var);  // false

// 移动语义
arena_allocator ar3(2048);
arena_allocator ar4(std::move(ar3));  // ar3 置空
```

#### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对单个指针调用 `deallocate` | arena 无此接口 | 仅用 `reset()` 整体回收 |
| `reset()` 后继续使用已分配指针 | 内存被整体回收，指针悬空 | `reset()` 后重新分配 |
| 借用模式析构后期望释放 buffer | 借用模式零所有权，不释放 | 由 buffer 所有者管理生命周期 |
| 期望 `allocate(n, 128)` 对齐 | base_ 仅 64 字节对齐 | 对齐上限 64，超过不保证 |

### slab_allocator — 固定块对象池

固定块大小对象池，侵入式 free list（块首 8 字节存 next 指针），零 header，O(1) push/pop。chunk 用 `operator new` 独立分配，不依赖 memory_pool。适合 void_any 小对象（≤128B）高频分配/释放。

#### 内存布局

每个 chunk 切分为固定大小块，空闲块首 8 字节复用为 free list next 指针。`allocate()` 返回的块无 header，零开销。`block_size` 构造时向上对齐到 `alignment`。

#### 接口

| 接口 | 说明 |
|------|------|
| `slab_allocator(size_t block_size, size_t alignment = 16, size_t blocks_per_chunk = 256)` | 构造，block_size 向上对齐 |
| `slab_allocator(slab_allocator&&)` | 移动构造 |
| `operator=(slab_allocator&&)` | 移动赋值 |
| `allocate()` | O(1) pop，无空闲块则 grow 新 chunk |
| `deallocate(void* p)` | O(1) push，归还块到 free list |
| `owns(const void* p)` | 遍历 chunk 链表判断归属 |
| `block_size()` | 实际块大小（对齐后） |
| `total_blocks()` | 总块数 |
| `free_blocks()` | 空闲块数 |
| `empty()` | 是否全部空闲（free == total） |

> 注：禁止拷贝；块大小固定，不支持变长分配。

#### 使用

```cpp
slab_allocator sl(64);  // 64 字节块
void* p = sl.allocate();
void* q = sl.allocate();
sl.deallocate(p);  // O(1) 归还
sl.deallocate(q);

// 释放后重用（同指针，LIFO）
void* r = sl.allocate();
assert(r == q);  // q 最后释放，最先重用

// 批量分配自动 grow
slab_allocator sl2(32, 16, 64);  // 32B 块, 64 块/chunk
void* batch[100];
for (int i = 0; i < 100; ++i) batch[i] = sl2.allocate();
for (int i = 0; i < 100; ++i) sl2.deallocate(batch[i]);

// owns
bool in_slab = sl.owns(p);
```

#### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `deallocate` 非 slab 分配的指针 | 写入非法内存，破坏 free list | 仅释放 `allocate()` 返回的指针 |
| 同一指针 `deallocate` 两次 | double free，破坏 free list | 释放后置空，避免重复释放 |
| 期望块大小可变 | slab 固定块大小 | 变长用 `layered_allocator` 或 `memory_pool` |

### layered_allocator — 分层分配器

组合 slab + TLSF，按大小路由：小对象（≤128B）走 slab（0 header, O(1)），大对象走 memory_pool（TLSF）。void_any 默认通过 `VOID_ANY_USE_LAYERED_ALLOCATOR` 启用此分配器。

#### 路由表

| 分配大小 | 路径 | size class |
|---------|------|-----------|
| 1-16 | slab[0] | 16B |
| 17-32 | slab[1] | 32B |
| 33-48 | slab[2] | 48B |
| 49-64 | slab[3] | 64B |
| 65-80 | slab[4] | 80B |
| 81-96 | slab[5] | 96B |
| 97-112 | slab[6] | 112B |
| 113-128 | slab[7] | 128B |
| >128 | memory_pool (TLSF) | 变长 |

`deallocate` 遍历 8 个 slab 的 `owns()` 判断归属，未命中走 `memory_pool.deallocate`。

#### 接口

| 接口 | 说明 |
|------|------|
| `layered_allocator()` | 默认构造，初始化 8 个 slab + memory_pool |
| `layered_allocator(layered_allocator&&)` | 移动构造 |
| `operator=(layered_allocator&&)` | 移动赋值 |
| `allocate(size_t n)` | 按大小路由：≤128 走 slab，>128 走 TLSF |
| `deallocate(void* p)` | 遍历 `owns()` 判断归属后释放 |
| `construct<T>(Args...)` | 分配并构造对象 |
| `destroy<T>(T* ptr)` | 析构并释放对象 |
| `owns(const void* p)` | 判断指针是否属于本分配器 |
| `slab_max()` | slab 路径上限（128） |
| `big_pool()` | 访问内部 memory_pool（大对象路径） |

> 注：禁止拷贝；void_any 通过 `VOID_ANY_USE_LAYERED_ALLOCATOR` 宏启用。

#### 使用

```cpp
layered_allocator la;
void* small = la.allocate(64);   // 走 slab[3]
void* big = la.allocate(256);    // 走 memory_pool
la.deallocate(small);            // 遍历 owns → slab[3]
la.deallocate(big);              // 遍历 owns → memory_pool
la.deallocate(small, 64);        // size-aware 快速路径 → slab[3], O(1)
la.deallocate(big, 256);         // size-aware 快速路径 → memory_pool, O(1)

// construct/destroy
struct Foo { int a; double b; Foo(int x, double y) : a(x), b(y) {} };
Foo* foo = la.construct<Foo>(42, 3.14);
la.destroy(foo);                 // 内部走 size-aware 快速路径

// owns
bool in_la = la.owns(small);
```

#### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `deallocate` 非 layered 分配的指针 | 遍历 owns 未命中后错误走 memory_pool | 仅释放 `allocate()` 返回的指针 |
| 同一指针 `deallocate` 两次 | double free | 释放后置空 |
| 期望 slab 路径支持 >128B | slab 最大 128B | >128B 自动走 memory_pool |

---

## 8. ecs::single_class_set — 单组件集合

管理单一类型组件的分页稀疏集存储。内部使用分页稀疏表（paged sparse table）+ 热集缓存（hot set cache），替代传统的连续稀疏数组。

### sparse 访问

| 接口 | 说明 |
|------|------|
| `sparse_dense_at_public(uint32_t idx)` | 获取稀疏条目的 dense 索引，不存在返回 `dense_invalid` (0xFFFFFFFF) |
| `sparse_version_at_public(uint32_t idx)` | 获取稀疏条目的 version，不存在返回 0 |
| `dense_invalid` | 无效 dense 索引常量（0xFFFFFFFF） |
| `get_sparse_size()` | 稀疏表已使用的最大索引+1 |
| `get_page_directory_capacity()` | 页目录容量 |
| `get_page_directory()` | 获取页目录指针 |
| `clear_hot_set()` | 清空热集缓存（调试用，正常使用无需手动调用） |
| `bump_pool_version()` | 递增池版本号，使所有热集缓存自动失效 |

机制：

- `sparse_dense_at_public` / `sparse_version_at_public`：合并存储（sparse_entry {dense, version}），二级页目录查找 `entry_pages_[idx>>shift][idx&mask]`
- flat 模式：连续 `flat_entries_[]` 数组（sparse_entry 合并存储），实体数 ≤ 65536 时启用
- paged 模式：按需分页，实体数 > 65536 时自动切换
- 未映射条目：dense 返回 `dense_invalid`，version 返回 0
- hot_set 是 256 项直接映射缓存（alignas(32)），每个条目存储 `(entity_index, dense_index, entity_version, pool_version)`
- 查询命中条件：`entity_index` 匹配 ∧ `entity_version` 匹配 ∧ `pool_version` 与当前池版本号一致
- 查询未命中时自动从稀疏表查找并填充缓存
- **自动失效**：每次增删改、dense 重排、sparse 表重建后，`pool_version` 自动递增，旧缓存条目自动失效，无需手动调用 `clear_hot_set()`
- `clear_hot_set()` 保留用于调试场景强制清空缓存

### 构造与赋值

| 接口 | 说明 |
|------|------|
| `single_class_set()` | 默认构造 |
| `single_class_set(size_t reserve_capacity)` | 预留容量构造 |
| `single_class_set(entity e, T&& object)` | 实体+对象构造 |
| `single_class_set(single_class_set&&)` | 移动构造 |
| `operator=(single_class_set&&)` | 移动赋值 |

> 注：`single_class_set` 禁止拷贝。

### 添加组件

| 接口 | 说明 |
|------|------|
| `add(entity, T)` | 添加/覆盖组件（已存在则替换） |
| `add_batch(span<const entity>, span<const T>)` | 批量添加（span 版本） |
| `add_batch(const class_pool<entity>&, const class_pool<T>&)` | 批量添加（左值引用） |
| `add_batch(class_pool<entity>&&, class_pool<T>&&)` | 批量添加（右值引用，移动语义） |

### 获取组件

| 接口 | 说明 |
|------|------|
| `get_ptr<T>(entity)` | 获取组件指针（带有效性、type_id、版本号检查） |
| `get_ptr<T>(entity) const` | const 版本 |
| `get_ptr_fast<T>(entity)` | 快速获取（跳过 type_id 检查） |
| `get_ptr_fast<T>(entity) const` | const 版本 |
| `get_ptr_raw<T>(entity)` | 零检查获取（调用者保证 entity 有效） |
| `get_ptr_raw<T>(entity) const` | const 版本 |
| `get_version(uint32_t entity_index)` | 获取实体版本号 |
| `get_version_unchecked(uint32_t entity_index)` | 无检查获取版本号 |
| `get_dense_at(uint32_t entity_index)` | 通过 entity index 获取 dense 索引 |
| `prefetch_component(uint32_t entity_index)` | 预取 sparse 条目（按 entity index） |
| `prefetch_ptr(entity)` | 预取 sparse 条目（按 entity） |
| `prefetch_ptr_batch(const entity*, size_t)` | 批量预取 sparse 条目 |
| `prefetch_ptr_data<T>(entity)` | 预取组件数据（按 entity，需先加载 sparse 条目） |
| `get_ptr_batch(const entity*, T**, size_t)` | 批量查询组件指针（管线化预取，大规模 sparse 表自动走排序预取路径） |

### 删除与清空

| 接口 | 说明 |
|------|------|
| `hard_remove(entity)` | 硬删除（交换删除，O(1)） |
| `soft_remove(entity)` | 软删除（仅清除 sparse 条目，不移动组件。副作用：组件池和 dense 数组留下"空洞"，`size()` 仍包含已删除组件，遍历时通过版本号跳过。若需紧凑布局，使用 `hard_remove`） |
| `clear()` | 清空所有数据 |

### 容量与查询

| 接口 | 说明 |
|------|------|
| `size()` | 组件数量 |
| `empty()` | 是否为空 |
| `increase_capacity(capacity)` | 预留容量 |
| `get_type_id()` | 获取类型 ID 引用 |
| `get_typed_pool_ptr<T>()` | 获取类型化组件池指针（带 type_id 检查） |
| `get_typed_pool_ptr<T>() const` | const 版本 |
| `add/remove` 系列接口返回值 | 返回 `operating_message`（值类型，RVO 零拷贝） |
| `get_entity_indices()` | 获取实体索引数组（dense 数组） |
| `get_entity_indices() const` | const 版本 |
| `get_entity_versions()` | 获取实体版本号数组（与 dense_ 同步，遍历时直接读连续内存） |
| `get_entity_versions() const` | const 版本 |
| `get_pool_version()` | 获取组件池版本号（持久化视图自动同步用） |

### 使用

```cpp
single_class_set set;
entity e1(1, 1);
set.add(e1, Position{10, 20});

Position* p = set.get_ptr<Position>(e1);
Position* pf = set.get_ptr_fast<Position>(e1);  // 快速
set.soft_remove(e1);  // 软删除（O(1)，但留下空洞，size() 不变）
set.hard_remove(e1);  // 硬删除（swap-pop，size() 减少）

// 批量添加
class_pool<entity> ents = {entity(2,1), entity(3,1)};
class_pool<Position> comps = {Position{1,2}, Position{3,4}};
set.add_batch(ents, comps);
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `soft_remove` 后依赖 `size()` 和连续遍历 | 留下空洞，`size()` 不减少，dense 数组不连续 | 若需紧凑布局，使用 `hard_remove` |
| 在需要频繁增删时只用 `hard_remove` | 每次交换删除 O(1) 但破坏顺序 | 可接受顺序变化时用 `hard_remove`，需保持顺序时用 `soft_remove` 后定期重建 |
| 拷贝 `single_class_set` | 禁止拷贝 | 使用移动语义 |
| 批量增删后手动调用 `clear_hot_set` | 不必要，pool_version 自动递增已使缓存失效 | 正常使用无需手动调用；调试场景可用 `clear_hot_set()` 强制清空 |
| 依赖 `sparse_dense_at_public` 返回值判断条目是否存在 | 需检查返回值是否等于 `dense_invalid` | 检查 `sparse_dense_at_public(idx) != dense_invalid`，或使用 `get_ptr` 系列接口 |

---

## 9. ecs::manager — ECS管理器

ECS 核心管理类，管理实体和所有组件集合。

> 注：`manager` 可移动，禁止拷贝。

### 实体管理

| 接口 | 说明 |
|------|------|
| `create_entity()` | 创建实体（优先使用预分配池） |
| `is_entity_valid(entity)` | 检查实体有效性（版本号匹配） |
| `append_preallocated_entities(count)` | 预分配实体到池中 |
| `delete_entity(entity&)` | 删除实体（释放 ID，递增版本号） |

### 添加组件

| 接口 | 说明 |
|------|------|
| `add<T>(entity, T)` | 添加组件（实体在前） |
| `add<T>(T, entity)` | 添加组件（组件在前，参数顺序可互换） |
| `addc<T>(entity, T)` | 链式添加（返回 `manager&`） |
| `addc<T>(T, entity)` | 链式添加（组件在前） |
| `addc<T>(T, EEs...)` | 正向变参链式添加：单组件加到多个实体 `addc(comp, e1, e2, e3)` |
| `addc<TT...>(EE, TT&&...)` | 反向变参链式添加：多组件加到同一实体 `addc(e, comp1, comp2, comp3)` |
| `add_batch<T>(span<const entity>, span<const T>)` | 批量添加（span 版本） |
| `add_batch<T>(const class_pool<entity>&, const class_pool<T>&)` | 批量添加（左值引用） |
| `add_batch<T>(class_pool<entity>&&, class_pool<T>&&)` | 批量添加（右值引用） |
| `add_batch<T>(const std::vector<entity>&, const std::vector<T>&)` | 批量添加（vector 入参，内部转 span） |
| `add_batch<T>(const std::array<entity, N>&, const std::array<T, N>&)` | 批量添加（array 入参，编译期固定长度） |
| `add_batch<T>(const entity*, const T*, size_t)` | 批量添加（裸指针 + 长度，内部转 span） |

### 获取组件

| 接口 | 说明 |
|------|------|
| `get_ptr<T>(entity)` | 获取组件指针（带检查） |
| `get_ptr<T>(entity) const` | const 版本 |
| `get_ptr_fast<T>(entity)` | 快速获取（跳过 type_id 检查） |
| `get_ptr_fast<T>(entity) const` | const 版本 |
| `get_ptr_batch<T>(entities, results, count)` | 批量查询组件指针（裸指针 + 长度，管线化预取） |
| `get_ptr_batch<T>(span<const entity>, span<T*>)` | 批量查询（span 入参，长度需一致） |
| `get_ptr_batch<T>(const vector<entity>&, vector<T*>&)` | 批量查询（vector 入参） |
| `get_ptr_batch<T>(const array<entity, N>&, array<T*, N>&)` | 批量查询（array 入参） |
| `prefetch_ptr<T>(entity)` | 预取实体 sparse 条目 |
| `prefetch_ptr_batch<T>(entities, count)` | 批量预取（裸指针 + 长度） |
| `prefetch_ptr_batch<T>(span<const entity>)` | 批量预取（span 入参） |
| `prefetch_ptr_batch<T>(const vector<entity>&)` | 批量预取（vector 入参） |
| `prefetch_ptr_batch<T>(const array<entity, N>&)` | 批量预取（array 入参） |
| `prefetch_ptr_data<T>(entity)` | 预取组件数据（需先加载 sparse 条目获取 dense 索引） |
| `get_ptr_fast_cached<T>(set, entity)` | 用缓存的 set 指针快速查询（避免重复 get_single_class_set） |
| `prefetch_ptr_cached<T>(set, entity)` | 用缓存的 set 指针预取 sparse 条目 |
| `prefetch_ptr_data_cached<T>(set, entity)` | 用缓存的 set 指针预取组件数据 |

> `get_ptr` 和 `get_ptr_fast` 内部已自动使用 `get_ptr_fast_inline`，通过缓存的 `typed_pool_data_` 指针直接访问组件数据，无需 `get_typed_pool` 间接寻址。无需手动调用。

### query_context 查询上下文

| 接口 | 说明 |
|------|------|
| `query_context<T>(manager&)` | 构造查询上下文，缓存 set/sparse/pool 指针 |
| `get_ptr(entity)` | 内联快速查询组件指针，返回 `T*` |
| `get_ptr(entity) const` | const 版本，返回 `const T*` |
| `prefetch_sparse(entity) const` | 预取 sparse 条目到 L1 |
| `prefetch_data(entity) const` | 预取组件数据到 L1 |
| `valid() const` | 上下文是否有效（组件类型是否已注册） |

机制：

- `query_context` 内部持有 `typed_pool_data_` 指针，直接访问组件数据，无需经过 `get_typed_pool` 间接寻址
- 构造时一次性缓存所有指针（set / sparse / pool），后续查询为内联路径

### 删除组件

| 接口 | 说明 |
|------|------|
| `soft_remove<T>(entity)` | 软删除组件（仅清除 sparse，留下空洞） |
| `hard_remove<T>(entity)` | 硬删除组件 |
| `soft_removec<T>(entity)` | 链式软删除（返回 `manager&`） |
| `hard_removec<T>(entity)` | 链式硬删除（返回 `manager&`） |
| `hard_removec<TT...>(EEs...)` | 变参链式硬删除：多类型 × 多实体 笛卡尔积 `hard_removec<Comp1, Comp2>(e1, e2)` |
| `soft_removec<TT...>(EEs...)` | 变参链式软删除：多类型 × 多实体 笛卡尔积 `soft_removec<Comp1, Comp2>(e1, e2)` |
| `delete_type_container<T>()` | 删除整个类型容器 |

> `hard_remove` 和 `swap_dense_and_pool` 对 `std::is_trivially_copyable` 类型使用 `typed_pool_data_` + `memcpy` 直接操作，跳过函数指针间接调用。非 trivial 类型回退到 `ops_.swap_pop` / `ops_.swap_pool` 函数指针路径。

### 容器访问

| 接口 | 说明 |
|------|------|
| `get_single_class_set<T>()` | 获取单组件集合指针 |
| `get_single_class_set<T>() const` | const 版本 |
| `get_component_container<T>()` | 获取类型化组件池指针 |
| `reserve_component_capacity<T>(capacity)` | 预留组件容量 |
| `add/add_batch/hard_remove/soft_remove` | 返回 `operating_message`（值类型，RVO 零拷贝） |
| `get_component_meta(int type_id)` | 获取组件元数据（含 `mask_block`/`mask_offset` 掩码位信息） |
| `get_single_class_set_by_id(int type_id)` | 通过 type_id 获取组件集合（运行时视图用） |
| `get_entity_manager()` | 获取 `entity_manager&` 引用（暴露掩码 / 状态 / 标志等底层接口） |
| `get_entity_manager() const` | const 版本 |

### single_class_set 合并稀疏表+热集接口

稀疏表使用 flat + paged 混合存储：实体数 ≤ 65536 时用 flat 连续数组（1次加载），超过时自动切换为 paged 分页模式。dense 和 version 合并存储为 `sparse_entry {uint32_t dense; uint32_t version}`（8 字节），同一 cache line 减少 get_ptr 慢路径 cache miss。

| 接口 | 说明 |
|------|------|
| `sparse_dense_at_public(uint32_t idx) const` | 读取稀疏条目的 dense 索引，不存在返回 `dense_invalid` |
| `sparse_version_at_public(uint32_t idx) const` | 读取稀疏条目的 version，不存在返回 0 |
| `dense_invalid` | 无效 dense 索引常量（0xFFFFFFFF），public 静态成员 |
| `get_sparse_size() const` | 稀疏表已覆盖的最大索引+1 |
| `get_page_directory_capacity() const` | 页目录容量（paged 模式） |
| `get_entry_pages() const` | 获取 sparse_entry 页目录指针（paged 模式） |
| `is_flat_mode() const` | 当前是否为 flat 模式 |
| `get_flat_entries() const` | 获取 flat sparse_entry 数组指针（flat 模式） |
| `get_flat_capacity() const` | flat 数组容量 |
| `get_dense_page(uint32_t entity_index) const` | 获取 sparse_entry 页指针（合并存储，flat 模式返回 flat_entries_） |
| `get_version_page(uint32_t entity_index) const` | 获取 sparse_entry 页指针（与 get_dense_page 返回同一指针） |
| `read_dense_from_page(page, entity_index, mask)` | 从 sparse_entry 页指针读取 dense index（static） |
| `read_version_from_page(page, entity_index, mask)` | 从 sparse_entry 页指针读取 version（static） |
| `clear_hot_set()` | 清空热集缓存（调试用，正常使用无需手动调用） |
| `bump_pool_version()` | 递增池版本号，使所有热集缓存自动失效 |
| `page_shift` / `page_size` / `page_mask` | 实例级分页参数（flat 模式下 page_shift=32, page_mask=SIZE_MAX） |
| `get_page_size_shift() const` | 获取当前分页 shift 值 |
| `set_page_size_shift(size_t shift)` | 设置分页 shift 值（6~20），已有数据时自动重建 |
| `dense_invalid` | 无效 dense 索引哨兵值（`0xFFFFFFFFu`），public 静态成员 |

```cpp
auto* set = mgr.get_single_class_set<Position>();

// 查询模式
if (set->is_flat_mode())
{
    // flat 模式: 直接数组访问 (sparse_entry 合并存储)
    const sparse_entry* entries = set->get_flat_entries();
    // entries[entity_index].dense 即为 dense index
    // entries[entity_index].version 即为 version
}
else
{
    // paged 模式: 页指针缓存遍历
    const sparse_entry* cur_page = nullptr;
    size_t cur_page_idx = SIZE_MAX;
    for (size_t i = 0; i < set->get_sparse_size(); ++i)
    {
        size_t pid = i >> set->page_shift;
        if (pid != cur_page_idx)
        {
            cur_page = set->get_dense_page(static_cast<uint32_t>(i));
            cur_page_idx = pid;
        }
        if (cur_page)
        {
            uint32_t dense = single_class_set::read_dense_from_page(
                cur_page, static_cast<uint32_t>(i), set->page_mask);
        }
    }
}

// 清空热集缓存
set->clear_hot_set();

// 运行时修改分页大小
set->set_page_size_shift(12);
```

### manager 分页大小配置

| 接口 | 说明 |
|------|------|
| `set_component_page_size_shift<T>(size_t shift)` | 按类型设置分页 shift 值（6~20），已有数据时自动重建 |
| `get_component_page_size_shift<T>() const` | 查询类型的当前分页 shift 值 |

```cpp
mgr.set_component_page_size_shift<Position>(12);
mgr.set_component_page_size_shift<Velocity>(8);
size_t shift = mgr.get_component_page_size_shift<Position>();
```

### 信号与追踪开关

| 接口 | 说明 |
|------|------|
| `disable_comp_signals()` | 禁用组件延迟信号入队 |
| `enable_comp_signals()` | 启用组件延迟信号入队 |
| `disable_entity_signals()` | 禁用实体延迟信号入队 |
| `enable_entity_signals()` | 启用实体延迟信号入队 |
| `disable_track_changes()` | 禁用版本追踪 |
| `enable_track_changes()` | 启用版本追踪 |

### 信号溢出与容量

| 接口 | 说明 |
|------|------|
| `comp_signal_overflow_count()` | 组件信号溢出到 chain 的累计次数 |
| `entity_signal_overflow_count()` | 实体信号溢出到 chain 的累计次数 |
| `reset_comp_signal_overflow_count()` | 清零组件溢出计数 |
| `reset_entity_signal_overflow_count()` | 清零实体溢出计数 |
| `reserve_comp_signal_capacity(n)` | 预分配组件溢出 chain 容量 |
| `reserve_entity_signal_capacity(n)` | 预分配实体溢出 chain 容量 |

### View系统

| 接口 | 说明 |
|------|------|
| `view<T>()` | 单组件视图 |
| `view<T>().for_each(func)` | 单组件视图 + 遍历 |
| `view<First, Second, Rest...>()` | 多组件视图 |
| `view<T>(without<Types...>)` | 排除视图 |
| `view<T>(with<Types...>)` | 获取视图 |
| `view_or<A, B>()` | OR视图 |
| `view_filtered<T>(Pred)` | 谓词过滤视图 |
| `view<T>().page(offset, limit)` | 分页视图（链式） |
| `view<T>().sorted_by_component<T>(cmp)` | 排序视图（链式） |
| `view<T>().sorted_by_component_value(keyFn)` | 分组视图（链式） |
| `view<T>().track_changes()` | 变更检测视图（链式） |
| `view<T>().filter_changed()` | 逐实体变更检测（链式） |
| `view<T>().filter_added()` | 逐实体添加检测（链式） |
| `view_any_of<Types...>()` | N元OR视图（任意组件匹配） |
| `view<T>().exactly_one()` | 精确获取单个实体组件 |
| `view<First, Rest...>().exactly_one()` | 精确获取单个实体多组件 |
| `view<First, Rest...>().find_one(entity)` | 查询指定实体多组件 |
| `view<First, Rest...>().iter_over_entities(entities)` | 批量指定实体查询 |

### 分级排序

`#include "part/tiered_sort.hpp"`，位于 `detail` 命名空间。

| 接口 | 说明 |
|------|------|
| `tiered_sort<T>(data, n, cmp)` | 分级排序值数组 |
| `tiered_sort_indices<T>(indices, values, n)` | 索引排序，按 values[indices[i]] 升序排列 indices |

**分级策略**：

| 数据量 n | tiered_sort 算法 | tiered_sort_indices 算法 |
|----------|-----------------|------------------------|
| n < 32 | 插入排序 | 插入排序 |
| 32 ≤ n < 256 | 插入排序（ascending 特化，trivial 类型） | 3-way pdqsort |
| 256 ≤ n < 4096 | 3-way pdqsort | 3-way pdqsort |
| 4096 ≤ n < 65536 | 11位基数排序（3趟，trivial 类型） | 11位基数排序（3趟，算术类型） |
| n ≥ 65536 | 11位基数排序 + 预取距离8 | 11位基数排序 + 预取距离8 |

- `tiered_sort` 要求 `T` 满足 `std::is_trivially_copyable_v`
- `tiered_sort_indices` 对算术类型在 n≥4096 时使用基数排序，否则 pdqsort
- 基数排序使用 11-11-10 位配置（3趟完成 32 位），64 位类型 6 趟
- 3-way pdqsort 使用 Dutch National Flag 分区，高效处理重复键
- 两者均为 `noexcept`

```cpp
#include "part/tiered_sort.hpp"

// 值排序
int data[] = {5, 3, 1, 4, 2};
tiered_sort(data, 5, std::less<int>{});

// 索引排序
size_t indices[] = {0, 1, 2, 3, 4};
float values[] = {5.0f, 3.0f, 1.0f, 4.0f, 2.0f};
tiered_sort_indices(indices, values, 5);
// indices: {2, 1, 4, 3, 0}
```

### Group系统

| 接口 | 说明 |
|------|------|
| `group<First, Rest...>()` | Non-OwningGroup（缓存匹配索引） |
| `group<First, Rest...>(owned<First>)` | OwningGroup（重排主集，线性扫描） |

### runtime_view

| 接口 | 说明 |
|------|------|
| `runtime_view_create(class_pool<int>, class_pool<int> = {})` | 运行时视图（class_pool 入参，位掩码匹配） |
| `runtime_view_create(span<const int>, span<const int> = {})` | 运行时视图（span 入参，内部构造 class_pool） |
| `runtime_view_create(const vector<int>&, const vector<int>& = {})` | 运行时视图（vector 入参） |
| `runtime_view_create(const array<int, N>&)` | 运行时视图（array 入参，仅 required） |
| `runtime_view_create(const array<int, N>&, const array<int, M>&)` | 运行时视图（array 入参，required + excluded） |
| `runtime_view_create(const int*, size_t, const int* = nullptr, size_t = 0)` | 运行时视图（裸指针 + 长度） |
| `runtime_view_create_from_terms(class_pool<runtime_term>)` | term 查询（支持 OR/OPTIONAL/NOT） |
| `runtime_view_create_from_terms(span<const runtime_term>)` | term 查询（span 入参） |
| `runtime_view_create_from_terms(const vector<runtime_term>&)` | term 查询（vector 入参） |
| `runtime_view_create_from_terms(const array<runtime_term, N>&)` | term 查询（array 入参） |
| `runtime_view_create_from_terms(const runtime_term*, size_t)` | term 查询（裸指针 + 长度） |
| `get_entity_mask(entity)` | 获取实体组件位掩码 |
| `get_component_bit<T>()` | 获取类型的位掩码位 |

> 批量入参重载说明:`add_batch` / `get_ptr_batch` / `prefetch_ptr_batch` / `runtime_view_create` / `runtime_view_create_from_terms` 均支持 `std::vector` / `std::array` / 裸指针 + 长度 / `std::span` 入参。内部统一转 `std::span` 委托现有实现,不持久持有外部容器。`runtime_view_create` 的非 class_pool 重载内部构造 `class_pool<int>` 填充后移动给现有实现。

### 生命周期信号

| 接口 | 说明 |
|------|------|
| `set_on_entity_created(fn, data)` | 绑定实体创建即时回调 |
| `set_on_entity_destroyed(fn, data)` | 绑定实体销毁即时回调 |
| `set_on_add<T>(fn, data)` | 绑定组件 T 添加即时回调 |
| `set_on_remove<T>(fn, data)` | 绑定组件 T 移除即时回调 |
| `set_on_modify<T>(fn, data)` | 绑定组件 T 覆盖写即时回调 |
| `flush_entity_signals(handler)` | 批量处理实体延迟信号 |
| `has_pending_entity_signals()` | 是否有待处理实体信号 |
| `flush_component_signals(handler)` | 批量处理组件延迟信号 |
| `has_pending_component_signals()` | 是否有待处理组件信号 |

### 使用

```cpp
ecs::manager mgr;
mgr.append_preallocated_entities(1000);

entity e1 = mgr.create_entity();
entity e2 = mgr.create_entity();

// 添加组件
mgr.add(e1, Position{1, 2});
mgr.add(Velocity{10, 20}, e1);  // 参数顺序可互换

// 链式添加
mgr.addc(e1, Health{100, 100})
   .addc(e1, Name{"Alice"});

// 变参链式添加: 单组件 + 多实体 (comp 在前)
mgr.addc(Position{7, 8}, e1, e2, e3);  // Position 加到 e1/e2/e3

// 变参链式添加: 多组件 + 单实体 (entity 在前)
mgr.addc(e1, Position{1, 2}, Velocity{3, 4, 5}, Health{100, 200});

// 获取组件
Position* p = mgr.get_ptr<Position>(e1);

// 批量查询组件
class_pool<Position*> results;
results.reserve_exact(entities.size());
mgr.get_ptr_batch<Position>(entities.data(), results.data(), entities.size());

// 预取组件指针
mgr.prefetch_ptr<Position>(e1);
mgr.prefetch_ptr_batch<Position>(entities.data(), entities.size());

// 双级预取：先预取 sparse 条目，再预取组件数据
mgr.prefetch_ptr<Position>(e1);
mgr.prefetch_ptr_data<Position>(e1);

// cached 双级预取：缓存 set 指针避免重复查找，适合批量循环查询
auto* set = mgr.get_single_class_set<Position>();
mgr.prefetch_ptr_cached<Position>(set, e1);
mgr.prefetch_ptr_data_cached<Position>(set, e1);
auto* p = mgr.get_ptr_fast_cached<Position>(set, e1);

// query_context: 一次性缓存所有指针，内联查询
ecs::query_context<Position> ctx(mgr);
ctx.prefetch_sparse(e1);
ctx.prefetch_data(e1);
auto* p5 = ctx.get_ptr(e1);

// 批量添加
class_pool<entity> ents = {e1, e2};
class_pool<Health> comps = {Health{100,100}, Health{80,100}};
mgr.add_batch(ents, comps);

// 删除
mgr.soft_remove<Health>(e1);
mgr.hard_remove<Velocity>(e2);

// 链式删除
mgr.soft_removec<Name>(e1)
   .soft_removec<Name>(e2);

// 变参链式删除: 多类型 × 多实体 笛卡尔积
mgr.hard_removec<Position, Velocity>(e1, e2);  // 从 e1 和 e2 都移除 Position 和 Velocity

// 删除实体
mgr.delete_entity(e2);
```

### 实体状态池

| 接口 | 说明 |
|------|------|
| `entity_flag` | 实体状态标志枚举：`active`, `disabled`, `pending_destroy`, `static_entity` |
| `get_entity_state(entity_index)` | 获取实体状态引用（`entity_state&`） |
| `set_entity_flag(entity_index, flag)` | 设置实体状态标志 |
| `clear_entity_flag(entity_index, flag)` | 清除实体状态标志 |
| `has_entity_flag(entity_index, flag)` | 检查实体是否具有某状态标志 |

```cpp
mgr.set_entity_flag(e.parts_.index_, entity_flag::disabled);
if (mgr.has_entity_flag(e.parts_.index_, entity_flag::active))
{
    // 实体处于活跃状态
}
auto& state = mgr.get_entity_state(e.parts_.index_);
state.tag = 1;   // 自定义标签
state.layer = 3; // 渲染层
```

### 变更日志池

每次 `add`/`remove` 操作自动记录变更，帧末可消费。

| 接口 | 说明 |
|------|------|
| `enable_change_log()` | 启用变更日志记录 |
| `disable_change_log()` | 禁用变更日志记录 |
| `flush_change_log(handler)` | 消费所有待处理的变更记录 |
| `end_frame()` | 帧结束，递增帧计数器 |
| `has_pending_change_records()` | 是否有待处理的变更记录 |

```cpp
mgr.enable_change_log();
// ... 增删组件操作 ...
mgr.end_frame();
mgr.flush_change_log([](const ecs::change_record& r) {
    // r.entity_index, r.type_id, r.op (0=add,1=remove,2=modify)
    // r.frame, r.dense_index
});
```

### 系统上下文池

注册系统执行上下文，管理执行顺序和并行分组。

| 接口 | 说明 |
|------|------|
| `system_context` | 系统上下文结构体：`required_mask`, `excluded_mask`, `order`, `phase`, `parallel_group`, `dependencies` |
| `register_system(ctx)` | 注册系统上下文 |
| `get_system_contexts()` | 获取所有系统上下文（`const class_pool<system_context>&`） |

```cpp
mgr.register_system(ecs::system_context{
    .required_mask = 0x3,  // 需要 Position + Velocity
    .phase = 1,            // update 阶段
    .order = 100,          // 执行顺序
    .parallel_group = 0,   // 串行执行
});
```

### 实体掩码（无上限）

基于 `entity_mask_manager` 的动态位掩码存储，无组件类型上限。`num_blocks_==1` 时等价于直接数组访问（零乘法开销）。通过 `reserve_blocks(n)` 预分配掩码块数，避免 reshape 开销。

#### `component_meta` 结构体

每个已注册组件类型对应一份元数据，存储其掩码位置。

| 字段 | 类型 | 说明 |
|------|------|------|
| `size` | `size_t` | 组件类型大小（字节） |
| `mask_block` | `uint32_t` | 掩码块索引 `(type_id-1)/64`（type_id=1..64 落入块 0） |
| `mask_offset` | `uint32_t` | 块内位偏移 `(type_id-1)%64`（0..63） |

#### manager 接口

| 接口 | 说明 |
|------|------|
| `get_entity_mask(entity)` | 获取实体块 0 掩码（`uint64_t`，type_id 1-64；等价于 `get_entity_block(e, 0)`） |
| `get_entity_block(entity, uint32_t block_idx)` | 获取实体指定块的掩码（`uint64_t`，block_idx 块对应 type_id `block_idx*64+1` 到 `block_idx*64+64`） |
| `get_entity_block_by_idx(uint32_t entity_index, uint32_t block_idx)` | 同上，接受 entity_index 而非 entity 句柄 |
| `get_component_bit<T>()` | 获取组件 T 的掩码位（`mask_block==0` 时返回 `1ULL<<offset`，否则返回 0） |
| `get_component_meta(int type_id)` | 获取 `component_meta*`（含 `mask_block`/`mask_offset`，type_id 越界返回 nullptr） |
| `reserve_mask_blocks(uint32_t num_blocks)` | 预分配每实体掩码块数（每块 64 种组件；注册组件前调用避免 reshape；`register_component_meta` 在 type_id 超出时自动扩容） |
| `num_mask_blocks() const` | 当前每实体掩码块数 |
| `get_entity_manager()` | 获取 `entity_manager&`，可继续调用 `set_mask_bit` / `clear_mask_bit` / `get_mask` / `get_block` / `set_entity_flag` 等 |

```cpp
uint64_t mask = mgr.get_entity_mask(e);
uint64_t pos_bit = mgr.get_component_bit<Position>();
if ((mask & pos_bit) != 0)
{
    // 实体拥有 Position 组件
}

// 通过 component_meta 查询任意 type_id 的掩码位置
const auto* meta = mgr.get_component_meta(type_id::get_type_id<Velocity>());
if (meta && meta->mask_block == 0)
{
    uint64_t vel_bit = 1ULL << meta->mask_offset;
    bool has_vel = (mgr.get_entity_mask(e) & vel_bit) != 0;
}

// 启动时预分配 2 块掩码（支持 128 种组件），避免运行中 reshape
mgr.reserve_mask_blocks(2);
```

> 默认块数为 1（支持 64 种组件）。组件注册时 `register_component_meta` 自动扩容掩码块数，无需手动调用 `reserve_mask_blocks`。手动预分配可避免运行中 reshape 的开销。多块掩码查询通过 `get_entity_block(e, block_idx)` 或 `get_entity_block_by_idx(idx, block_idx)` 访问任意块。详见 [§ 27. entity_mask_manager](#27-entity_mask_manager--无上限实体掩码存储)。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 拷贝 `manager` | 禁止拷贝，内部资源所有权混乱 | 使用移动语义或引用传递 |
| 在遍历 View 的同时增删组件 | 迭代器失效或数据竞争 | 先收集变更，遍历结束后批量操作 |
| 删除实体后继续使用其句柄 | 句柄版本号失效，`is_entity_valid` 返回 false | 删除后丢弃句柄，或重新创建 |
| 忘记 `append_preallocated_entities` | 每个实体创建都可能触发扩容 | 启动时预估实体数量并预分配 |
| 在 `soft_remove` 后假设 `size()` 减少 | 软删除不减少 `size()` | 使用 `hard_remove` 或通过 View 遍历 |
| 使用 `get_ptr_fast` 跨越不同类型集合 | 跳过 type_id 检查，可能返回错误类型指针 | 同一类型集合内使用 `get_ptr_fast`，跨类型用 `get_ptr` |

---

## 10. View系统

提供高效的组件遍历，自动选择最小集作为主集迭代。

### 10.1 single_view\<T> — 单组件视图

| 接口 | 说明 |
|------|------|
| `size()` | 组件数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否包含指定实体的组件 |
| `for_each(func)` | 遍历组件（自动检测 entity 参数：`func(T&)` 或 `func(entity, T&)`） |
| `begin()` / `end()` | 实体迭代器 |
| `component_begin()` / `component_end()` | 组件迭代器（`T*`） |
| `get_component_for_entity(entity)` | 获取指定实体的组件引用（无则 nullptr） |
| `get_first_entity()` | 返回第一个实体句柄 |
| `get_last_entity()` | 返回最后一个实体句柄 |
| `get_entity_at_index(index)` | 返回第 index 个实体句柄 |
| `get_component_at_index(index)` | 返回第 index 个组件的指针 |

```cpp
auto view = mgr.view<Position>();

// 遍历组件
view.for_each([](Position& p) { /* ... */ });

// 遍历（带实体）
view.for_each([](entity e, Position& p) { /* ... */ });

// 范围 for
for (auto e : view) { /* e 是实体 */ }

// 组件迭代器
for (auto it = view.component_begin(); it != view.component_end(); ++it) {
    /* *it 是组件引用 */
}

// 快捷写法
mgr.view<Position>().for_each([](Position& p) { /* ... */ });
```

### 10.2 multi_view\<T1, T2, ...> — 多组件视图

自动选择最小集作为主集迭代。

| 接口 | 说明 |
|------|------|
| `size()` | 主集组件数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否同时拥有所有组件 |
| `for_each(func)` | 遍历多组件（自动检测 entity 参数） |
| `get_component_for_entity<T>(entity)` | 获取指定实体的指定类型组件（无则 nullptr） |
| `get_first_entity()` | 返回第一个匹配所有组件的实体 |
| `get_last_entity()` | 返回最后一个匹配所有组件的实体 |
| `get_entity_at_index(index)` | 返回主集第 index 个实体句柄 |
| `include_optional_component<U>()` | 链式追加可选组件，回调中为指针（无组件时为 nullptr） |

```cpp
// 双组件
auto v2 = mgr.view<Position, Velocity>();
v2.for_each([](Position& p, Velocity& v) { /* ... */ });
v2.for_each([](entity e, Position& p, Velocity& v) { /* ... */ });

// 获取指定实体的组件
Position* pp = v2.get_component_for_entity<Position>(some_entity);
Velocity* vp = v2.get_component_for_entity<Velocity>(some_entity);

// 获取首/尾实体
entity first = v2.get_first_entity();
entity last  = v2.get_last_entity();
entity nth   = v2.get_entity_at_index(5);

// 三组件
auto v3 = mgr.view<Position, Velocity, Health>();
v3.for_each([](entity e, Position& p, Velocity& v, Health& h) { /* ... */ });

// 四组件
auto v4 = mgr.view<Position, Velocity, Health, Name>();
v4.for_each([](entity e, Position&, Velocity&, Health&, Name& n) { /* ... */ });

// 追加可选组件
auto v_opt = mgr.view<Position, Velocity>()
    .include_optional_component<Health>()
    .include_optional_component<Name>();
v_opt.for_each([](entity e, Position& p, Velocity& v, Health* h, Name* n) {
    if (h) { /* 有 Health */ }
    if (n) { /* 有 Name */ }
});
```

### 10.3 single_view_without — 排除视图

遍历有 T 但没有 ExcludeTypes 的实体。

| 接口 | 说明 |
|------|------|
| `size()` | 组件数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 检查实体是否有 T 且无 ExcludeTypes |
| `get_component_for_entity(entity)` | 获取 T 的引用（无则 nullptr） |
| `get_first_entity()` | 返回第一个匹配的实体 |
| `for_each(func)` | 遍历组件（排除指定类型，自动检测 entity 参数） |

```cpp
// 排除单个类型
auto v = mgr.view<Position>(ecs::without<Health>);
v.for_each([](Position& p) { /* 没有 Health 的实体 */ });
v.for_each([](entity e, Position& p) { /* ... */ });

// 检查实体是否匹配
if (v.contains(some_entity)) { /* ... */ }
Position* p = v.get_component_for_entity(some_entity);
entity first = v.get_first_entity();

// 排除多个类型
auto v2 = mgr.view<Position>(ecs::without<Health, Name>);
```

### 10.4 single_view_with — 获取视图

遍历 T，同时获取 GetTypes 的指针（可能为 nullptr）。

| 接口 | 说明 |
|------|------|
| `size()` | 组件数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 检查实体是否有 T |
| `get_component_for_entity(entity)` | 获取 T 的引用（无则 nullptr） |
| `get_optional_component_for_entity<U>(entity)` | 获取可选组件 U 的指针 |
| `get_first_entity()` | 返回第一个匹配的实体 |
| `for_each(func)` | 遍历组件+可选指针（自动检测 entity 参数） |

```cpp
// 获取单个可选组件
auto v = mgr.view<Position>(ecs::with<Health>);
v.for_each([](Position& p, Health* h) {
    if (h) { /* 有 Health */ }
    else  { /* 无 Health */ }
});
v.for_each([](entity e, Position& p, Health* h) { /* ... */ });

// 获取指定实体的组件
Position* p = v.get_component_for_entity(some_entity);
Health*  h = v.get_optional_component_for_entity<Health>(some_entity);

// 获取多个可选组件
auto v2 = mgr.view<Position>(ecs::with<Health, Name>);
v2.for_each([](Position& p, Health* h, Name* n) { /* ... */ });
```

### 10.5 or_view\<A, B> — OR视图（零分配）

遍历拥有 A **或** B 的实体，使用 nullable 指针区分。两阶段遍历，零额外内存分配。

| 接口 | 说明 |
|------|------|
| `size()` | 近似大小（A.size + B.size，上界） |
| `empty()` | 是否两个集都为空 |
| `contains(entity)` | 是否有 A 或 B |
| `get_first_entity()` | 返回第一个匹配实体 |
| `for_each(func)` | 遍历 A OR B，回调为 `func(entity, A*, B*)` 或 `func(A*, B*)` |

```cpp
auto ov = mgr.view_or<Position, Velocity>();
ov.for_each([](entity e, Position* p, Velocity* v) {
    if (p && v) { /* 同时拥有 Position 和 Velocity */ }
    else if (p) { /* 仅拥有 Position */ }
    else if (v) { /* 仅拥有 Velocity */ }
});

if (ov.contains(some_entity)) { /* ... */ }
entity first = ov.get_first_entity();
```

### 10.6 filter_view\<T, Pred> — 谓词过滤视图

预过滤满足谓词的组件，通过 `class_pool<size_t>` 存储 dense 索引实现高效迭代。构造时自动过滤，也可手动 `rebuild()` 触发重新过滤。

| 接口 | 说明 |
|------|------|
| `size()` | 过滤后组件数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否在过滤结果中（线性扫描） |
| `get_component_for_entity(entity)` | 获取组件引用（无则 nullptr） |
| `get_first_entity()` | 返回第一个过滤结果实体 |
| `get_entity_at_index(index)` | 返回第 index 个过滤结果实体 |
| `get_component_at_index(index)` | 返回第 index 个过滤结果组件指针 |
| `rebuild()` | 重新执行过滤 |
| `for_each(func)` | 遍历过滤后的组件，回调为 `func(entity, T&)` 或 `func(T&)` |
| `and_<B>()` | 链式调用：在过滤结果上追加 AND 组件 B |
| `or_<B>()` | 链式调用：在过滤结果上追加 OR 组件 B |

```cpp
// 过滤 Position.x > 1
auto fv = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; });
fv.for_each([](Position& p) { /* 仅处理 x > 1 的实体 */ });

// 索引访问
if (!fv.empty()) {
    entity first = fv.get_first_entity();
    entity nth   = fv.get_entity_at_index(3);
    Position* p  = fv.get_component_at_index(3);
    if (fv.contains(some_entity)) { /* ... */ }
}

// 手动重新过滤（组件数据变化后）
fv.rebuild();
```

### 10.7 filter_and_view — 过滤+AND组合视图

通过 `filter_view::and_<B>()` 链式创建。遍历满足谓词 **且** 同时拥有组件 B 的实体。

| 接口 | 说明 |
|------|------|
| `size()` | 过滤后数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否在过滤结果中（线性扫描） |
| `get_component_for_entity<T>(entity)` | 获取 T 引用（无则 nullptr） |
| `get_optional_component_for_entity<B>(entity)` | 获取 B 指针 |
| `get_first_entity()` | 返回第一个匹配实体 |
| `get_entity_at_index(index)` | 返回第 index 个匹配实体 |
| `rebuild()` | 重新执行过滤 |
| `for_each(func)` | 遍历过滤+AND结果 |

```cpp
// Position.x > 1 AND 同时拥有 Health
auto fav = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; })
               .and_<Health>();
fav.for_each([](entity e, Position& p, Health& h) {
    // 仅处理 x > 1 且拥有 Health 的实体
});

entity first = fav.get_first_entity();
entity nth   = fav.get_entity_at_index(3);
if (fav.contains(some_entity)) { /* ... */ }
```

### 10.8 filter_or_view — 过滤+OR组合视图

通过 `filter_view::or_<B>()` 链式创建。遍历满足谓词 **或** 拥有组件 B 的实体，使用 nullable 指针区分。

| 接口 | 说明 |
|------|------|
| `size()` | 过滤后数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否在过滤结果中 |
| `get_first_entity()` | 返回第一个匹配实体 |
| `rebuild()` | 重新执行过滤 |
| `for_each(func)` | 遍历过滤+OR结果 |

```cpp
// Position.x > 1 OR 拥有 Velocity
auto fov = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; })
               .or_<Velocity>();
fov.for_each([](entity e, Position* p, Velocity* v) {
    if (p && v) { /* 满足谓词且拥有 Velocity */ }
    else if (p) { /* 仅满足谓词 */ }
    else if (v) { /* 仅拥有 Velocity（不满足谓词或没有 Position） */ }
});
```

### 10.9 sort_entities_by_component / reorder_by_component — 排序工具

manager 级别的排序工具，将 dense 数组按组件值重排，后续迭代即按排序顺序。

| 接口 | 说明 |
|------|------|
| `sort_entities_by_component<T>(cmp)` | 按组件 T 的值排序 dense 数组（同步更新 sparse 映射） |
| `reorder_by_component<T, Other>(cmp)` | 按 Other 的值重排 T 的 dense 数组 |
| `sort_component_container<T>(cmp)` | 按组件 T 的值排序并同步更新 dense/sparse 映射（等价于 `sort_entities_by_component`） |

**reorder_by_component 语义**：遍历 T 池的所有实体，按 Other 的值排序。若某实体没有 Other 组件，使用默认构造的 `Other{}` 参与比较。

```cpp
// 按 Position.x 升序排序
mgr.sort_entities_by_component<Position>([](Position& a, Position& b) {
    return a.x < b.x;
});

// 按 Velocity.dx 降序重排 Position
mgr.reorder_by_component<Position, Velocity>([](Velocity& a, Velocity& b) {
    return a.dx > b.dx;
});

// 排序组件池（同步更新 dense/sparse，保持 entity-component 映射一致）
mgr.sort_component_container<Position>([](Position& a, Position& b) {
    return a.x < b.x;
});
```

**不要做什么**

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `reorder_by_component` 期望实体没有 Other 时被排除 | 没有 Other 的实体会用 `Other{}` 参与排序，不会被排除 | 若需排除，先过滤实体再排序 |
| 排序后仍用旧 index 访问组件 | dense 数组已重排，旧 index 失效 | 排序后通过 `get_ptr<T>(entity)` 重新获取 |

### 10.10 page — 分页视图

通过 `page(offset, limit)` 链式调用，跳过前 `offset` 个结果并限制返回 `limit` 个。适用于 `single_view` 和 `multi_view`。

| 接口 | 说明 |
|------|------|
| `size()` | 分页后数量（`min(limit, base_size - offset)`，offset 超界则为 0） |
| `empty()` | 是否为空 |
| `for_each(func)` | 分页遍历（跳过前 offset 个，最多处理 limit 个） |

```cpp
// 跳过前 1 个，最多处理 3 个
auto paged = mgr.view<Position, Velocity>().page(1, 3);
paged.for_each([](Position& p, Velocity& v) {
    // 仅处理第 2~4 个匹配实体
});
```

### 10.11 sorted_by_component — 排序视图

通过 `sorted_by_component<T>(cmp)` 链式调用，按指定组件值临时排序查询结果。通过版本号检测变更自动重建缓存。适用于 `single_view` 和 `multi_view`。

| 接口 | 说明 |
|------|------|
| `size()` | 排序后数量（仅含拥有全部组件的有效实体） |
| `empty()` | 是否为空 |
| `for_each(func)` | 按排序顺序遍历（自动检测 entity 参数） |

```cpp
// 按 Position.x 升序排序
auto sorted = mgr.view<Position, Velocity>()
    .sorted_by_component<Position>([](Position& a, Position& b) {
        return a.x < b.x;
    });
sorted.for_each([](Position& p, Velocity& v) {
    // 按 p.x 升序遍历
});
```

### 10.12 sorted_by_component_value — 分组视图

通过 `sorted_by_component_value(keyFn)` 链式调用，按组件值分组，支持逐组遍历。适用于 `single_view` 和 `multi_view`。

| 接口 | 说明 |
|------|------|
| `size()` | 分组后总数 |
| `empty()` | 是否为空 |
| `group_count()` | 分组数量 |
| `for_each(func)` | 按分组顺序遍历所有元素 |
| `for_each_group(func)` | 逐组遍历，回调为 `func(key, start_index, end_index)` |

```cpp
// 按 Position.x / 20 分组
auto grouped = mgr.view<Position>()
    .sorted_by_component_value([](Position& p) -> int {
        return p.x / 20;
    });

// 逐组遍历
grouped.for_each_group([](int key, size_t start, size_t end) {
    std::cout << "Group " << key << ": " << (end - start) << " entities\n";
});
```

### 10.13 track_changes — 变更检测视图

通过 `track_changes()` 链式调用，仅返回自上次迭代以来组件发生变化的实体。基于组件池版本号实现，适用于 `single_view` 和 `multi_view`。

| 接口 | 说明 |
|------|------|
| `size()` | 变更实体数量 |
| `empty()` | 是否为空 |
| `for_each(func)` | 遍历变更的实体（首次全量返回，后续仅返回变更实体） |
| `reset_tracking()` | 重置跟踪基准（下次 for_each 重新全量返回） |

```cpp
auto changed = mgr.view<Position, Velocity>().track_changes();

// 首次遍历：全量返回
changed.for_each([](Position& p, Velocity& v) {
    // 处理所有实体
});

// 修改组件 ...
mgr.add(some_entity, Position{999, 0, 0}); // add 触发版本变更

// 再次遍历：仅返回变更的实体
changed.for_each([](Position& p, Velocity& v) {
    // 仅处理变更的实体
});

// 重置跟踪
changed.reset_tracking();
```

### 10.14 链式组合

新视图均支持链式组合，可灵活组合使用：

```cpp
// 分页 + 排序
mgr.view<Position, Velocity>()
   .sorted_by_component<Position>([](Position& a, Position& b) { return a.x < b.x; })
   .page(0, 10)
   .for_each([](Position& p, Velocity& v) { /* 前10个排序结果 */ });

// 分页 + 变更检测
mgr.view<Position>()
   .track_changes()
   .page(0, 5)
   .for_each([](Position& p) { /* 最多5个变更实体 */ });
```

### 10.15 filter_changed — 逐实体变更检测

通过 `filter_changed()` 链式调用，仅返回自上次迭代以来组件值发生变化的实体。基于逐实体版本号追踪，可精确到单个实体。

> **注意：** 仅 `add()` 操作（包括覆盖添加）会触发变更版本号递增。通过 `get_ptr()` 直接修改组件内存不会触发变更检测。

| 接口 | 说明 |
|------|------|
| `size()` | 变更实体数量 |
| `empty()` | 是否为空 |
| `for_each(func)` | 遍历变更实体（首次全量返回，后续仅返回变更实体） |
| `reset_tracking()` | 重置快照基准（下次 for_each 重新全量返回） |

```cpp
// 单组件变更检测
auto cv = mgr.view<Position>().filter_changed();
cv.for_each([](Position& p) {
    // 首次：全量返回所有 Position 实体
    // 后续：仅返回 Position 被修改过的实体
});

// 多组件变更检测（跟踪指定组件，需指定模板参数）
auto mcv = mgr.view<Position, Velocity>().filter_changed<Position>();
mcv.for_each([](Position& p, Velocity& v) {
    // 跟踪 Position 的变更，同时返回 Velocity
});

// 重置跟踪基准
cv.reset_tracking();  // 下次 for_each 重新全量返回
```

### 10.16 filter_added — 逐实体添加检测

通过 `filter_added()` 链式调用，仅返回视图创建后**新添加**的组件。基于全局添加计数器实现。

| 接口 | 说明 |
|------|------|
| `size()` | 新增实体数量 |
| `empty()` | 是否为空 |
| `for_each(func)` | 遍历新增实体（首次全量返回，后续仅返回新添加的实体） |

```cpp
// 单组件添加检测
auto av = mgr.view<Position>().filter_added();
// 先创建视图，再添加组件
mgr.add(e1, Position{1, 0});
mgr.add(e2, Position{2, 0});
av.for_each([](Position& p) {
    // 首次：返回所有已添加的实体
});
av.for_each([](Position& p) {
    // 再次：无新添加，返回空
});

// 多组件添加检测（需指定跟踪的组件类型）
auto mav = mgr.view<Position, Velocity>().filter_added<Position>();
mav.for_each([](Position& p, Velocity& v) {
    // 仅返回 Position 新添加的实体（同时需有 Velocity）
});
```

> **与 `filter_changed` 的区别：** `filter_changed` 追踪"修改"（覆盖添加也会触发），`filter_added` 仅追踪"首次添加"（覆盖添加不触发）。

### 10.17 view_any_of — N元OR视图

通过 `view_any_of<Types...>()` 创建，遍历拥有**任意一个**指定组件的实体。使用 bitset 去重，确保每个实体仅出现一次。

| 接口 | 说明 |
|------|------|
| `size()` | 近似大小（各集合大小之和，上界） |
| `empty()` | 是否所有集合都为空 |
| `for_each(func)` | 遍历任意匹配的实体，回调为 `func(Types*...)` 或 `func(entity, Types*...)` |

```cpp
// 双组件 OR：Position OR Velocity
auto av2 = mgr.view_any_of<Position, Velocity>();
av2.for_each([](Position* p, Velocity* v) {
    if (p && v) { /* 同时拥有 */ }
    else if (p) { /* 仅有 Position */ }
    else if (v) { /* 仅有 Velocity */ }
});

// 三组件 OR：Position OR Velocity OR Health
auto av3 = mgr.view_any_of<Position, Velocity, Health>();
av3.for_each([](entity e, Position* p, Velocity* v, Health* h) {
    // 指针非空即拥有该组件
});

// 四组件 OR
auto av4 = mgr.view_any_of<Position, Velocity, Health, Name>();
av4.for_each([](Position* p, Velocity* v, Health* h, Name* n) {
    // 至少拥有一个组件
});
```

> **与 `or_view` 的区别：** `or_view` 仅支持 2 组件，`view_any_of` 支持任意数量组件。

### 10.18 exactly_one — 精确获取单个实体

通过 `exactly_one()` 获取恰好一个实体的组件引用。若实体数量不为 1，行为未定义。

**返回值：**
- `single_view<T>::exactly_one()` → `T&`
- `multi_view<T1, T2, ...>::exactly_one()` → `std::tuple<T1&, T2&, ...>`

```cpp
// 恰好一个实体有 Position
auto& pos = mgr.view<Position>().exactly_one();
pos.x = 100;

// 恰好一个实体同时有 Position 和 Velocity
auto [p, v] = mgr.view<Position, Velocity>().exactly_one();
p.x += v.dx;
p.y += v.dy;

// 三组件
auto [p2, v2, h] = mgr.view<Position, Velocity, Health>().exactly_one();
h.hp -= 10;
```

> **注意：** 视图内实体数量不为 1 时行为未定义，调用者需自行保证。适用于单例实体、玩家实体等场景。

### 10.19 find_one — 查询指定实体

通过 `find_one(entity)` 查询指定实体是否拥有视图要求的全部组件。若拥有则返回组件指针元组，否则返回空指针。

**返回值：** `std::tuple<First*, Rest*...>`，所有组件都存在时所有指针非空，否则所有指针为空。

```cpp
// 查询 e1 是否有 Position + Velocity
auto [p, v] = mgr.view<Position, Velocity>().find_one(e1);
if (p && v) {
    // e1 拥有 Position 和 Velocity
    p->x += v->dx;
}

// 查询不存在的实体
auto [p2, v2] = mgr.view<Position, Velocity>().find_one(invalid_entity);
// p2 == nullptr, v2 == nullptr

// 三组件查询
auto [p3, v3, h] = mgr.view<Position, Velocity, Health>().find_one(e1);
if (p3 && v3 && h) {
    // e1 拥有全部三个组件
}
```

### 10.20 iter_over_entities — 批量指定实体查询

通过 `iter_over_entities(entities)` 在指定实体列表上迭代，仅处理同时拥有视图所有组件的实体。

**参数：** `entities` 支持 `std::array<entity, N>`、`std::span<entity>`、`class_pool<entity>` 等可迭代容器。

```cpp
// 在指定实体列表中迭代
std::array<entity, 3> targets = {e1, e2, e3};
auto ev = mgr.view<Position, Velocity>().iter_over_entities(targets);
ev.for_each([](Position& p, Velocity& v) {
    // 仅处理 e1, e2, e3 中同时拥有 Position 和 Velocity 的实体
    p.x += v.dx;
});

// 使用 class_pool<entity>
class_pool<entity> entity_list;
entity_list.emplace_back(e1);
entity_list.emplace_back(e2);
auto ev2 = mgr.view<Position, Health>().iter_over_entities(entity_list);
ev2.for_each([](Position& p, Health& h) {
    // 仅处理列表中有 Position 和 Health 的实体
});
```

> **注意：** 实体列表中不满足组件条件的实体会被静默跳过，不会报错。

### View 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 在 `for_each` 回调中增删组件 | 迭代器失效，可能导致崩溃或漏处理 | 先收集变更实体列表，遍历结束后批量操作 |
| `filter_view` 过滤条件变化后忘记 `rebuild()` | 过滤结果过期，仍返回旧数据 | 组件数据变化后调用 `rebuild()` |
| `exactly_one()` 在实体数不为 1 时使用 | 行为未定义 | 先检查 `size() == 1`，或使用 `find_one()` |
| 依赖 `filter_changed` 检测 `get_ptr()` 修改 | 直接修改内存不触发变更检测 | 通过 `add()` 覆盖触发变更，或使用 `track_changes` |
| 在多组件 View 中混用 `get_ptr_fast` 和 `get_ptr` | 类型安全边界模糊 | 同一 View 中统一使用一种获取方式 |

---

## 11. Group系统

Group 在构造时预先计算匹配实体集，迭代时零分支。

### 11.1 Non-OwningGroup (`group`)

通过 `mgr.group<First, Rest...>()` 创建，缓存匹配实体的 dense 索引。

**接口：**

| 接口 | 说明 |
|------|------|
| `size()` | 匹配实体数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否包含指定实体 |
| `for_each(func)` | 遍历匹配实体（自动检测 entity 参数：`func(Ts&...)` 或 `func(entity, Ts&...)`） |
| `get<T>(entity)` | 获取指定实体的组件 T 指针 |
| `front()` | 首个匹配实体 |
| `back()` | 末尾匹配实体 |
| `rebuild()` | 重建缓存（组件增删后调用） |

```cpp
// 双组件 Group
auto g = mgr.group<Position, Velocity>();
g.for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
    p.y += v.vy;
});

// 带 entity 参数
g.for_each([](entity e, Position& p, Velocity& v) {
    std::cout << "Entity " << e.parts_.index_ << ": pos=(" << p.x << "," << p.y << ")\n";
});

// 组件增删后重建
mgr.add(new_entity, Position{0, 0});
mgr.add(new_entity, Velocity{1, 0});
g.rebuild();

// 三组件 Group
auto g3 = mgr.group<Position, Velocity, Health>();
g3.for_each([](entity e, Position& p, Velocity& v, Health& h) {
    // 同时拥有三个组件的实体
});
```

### 11.2 OwningGroup (`group` + `owned`)

通过 `mgr.group<First, Rest...>(ecs::owned<First>)` 创建，重排主集 `First` 的 dense 数组，使匹配实体在数组前部连续排列。

**注意：** `owned` 标签标记的组件类型会被重排，组件数据顺序会改变。如果其他代码依赖该组件的 dense 顺序，需谨慎使用。

```cpp
// OwningGroup: Position 被重排
auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);
og.for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
});

// 带 entity 参数
og.for_each([](entity e, Position& p, Velocity& v) {
    std::cout << "Entity " << e.parts_.index_ << ": pos=(" << p.x << "," << p.y << ")\n";
});

// 三组件 OwningGroup
auto og3 = mgr.group<Position, Velocity, Health>(ecs::owned<Position>);
og3.for_each([](entity e, Position& p, Velocity& v, Health& h) {
    // 同时拥有三个组件的实体
});
```

### 11.3 ReorderGroup (`group` + `reorder`)

通过 `mgr.group<First, Rest...>(ecs::reorder<First>)` 创建，与 OwningGroup 同样重排主集，但语义更轻——仅表达"允许重排"，不暗示生命周期所有权。

```cpp
// ReorderGroup: Position 被重排
auto rg = mgr.group<Position, Velocity>(ecs::reorder<Position>);
rg.for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
});

// 带 entity 参数
rg.for_each([](entity e, Position& p, Velocity& v) {
    std::cout << "Entity " << e.parts_.index_ << ": pos=(" << p.x << "," << p.y << ")\n";
});

// 三组件 ReorderGroup
auto rg3 = mgr.group<Position, Velocity, Health>(ecs::reorder<Position>);
rg3.for_each([](entity e, Position& p, Velocity& v, Health& h) {
    // 同时拥有三个组件的实体
});
```

**多 Group 共享重排：** 多个相同组件类型的 ReorderGroup 可通过 `share_with()` 共享重排状态，避免重复重排。

```cpp
auto rg1 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
auto rg2 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
rg2.share_with(rg1);  // rg2 共享 rg1 的重排状态
// 两者 size() 和迭代结果一致，共享同一份缓存
```

### Group 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 组件增删后忘记 `rebuild()` | Group 缓存过期，可能漏掉新实体或包含已删除实体 | 每次批量增删后调用 `rebuild()` |
| 在 OwningGroup / ReorderGroup 中依赖 dense 顺序 | `owned` / `reorder` 会重排主集 dense 数组 | 若需保持顺序，使用 Non-OwningGroup |
| 对频繁增删的组件使用 Group | 每次增删都需 `rebuild()`，开销大 | 稳定组件用 Group，频繁变化组件用 View |

---

## 12. runtime_view — 运行时视图

在运行时动态指定组件类型组合进行查询。组件类型数量无上限，前 64 种组件类型自动维护实体位掩码，超过 64 种的组件类型同样参与所有视图/分组查询。

### 12.1 实体掩码

每个实体在添加/删除组件时自动维护组件位掩码（仅覆盖 type_id ≤ 64 的组件类型）：

```cpp
ecs::manager mgr;
auto e = mgr.create_entity();
mgr.add(e, Position{1, 0});
mgr.add(e, Velocity{2, 0});

uint64_t mask = mgr.get_entity_mask(e);
// mask 包含 Position 和 Velocity 的位

uint64_t pos_bit = mgr.get_component_bit<Position>();
bool has_pos = (mask & pos_bit) != 0;  // true
```

### 12.2 运行时视图

```cpp
// 双组件运行时视图
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<Position>(),
    type_id::get_type_id<Velocity>()
});

rv.for_each([](entity e) {
    auto* p = rv.get_ptr<Position>(e);
    auto* v = rv.get_ptr<Velocity>(e);
    p->x += v->vx;
});
```

### 12.3 排除视图

```cpp
// 有 Position 但无 Velocity 的实体
auto rv = mgr.runtime_view_create(
    { type_id::get_type_id<Position>() },
    { type_id::get_type_id<Velocity>() }
);

rv.for_each([](entity e) {
    // 处理只有 Position 的实体
});
```

### 12.4 接口

| 接口 | 说明 |
|------|------|
| `runtime_view_create({ids...})` | 创建运行时视图，传入必须拥有的组件 type_id 列表 |
| `runtime_view_create({ids}, {exclude_ids})` | 创建排除式运行时视图 |
| `runtime_view_create_from_terms(terms)` | 通过 `runtime_term` 创建视图，支持 OR / OPTIONAL / NOT |
| `for_each(func)` | 遍历所有匹配实体，回调接收 `entity` 或无参 |
| `for_each_typed<Ts...>(func)` | 遍历并回传组件引用，回调接收 `entity, Ts&...` 或 `Ts&...` |
| `for_each_parallel(worker_id, worker_count, func)` | 分片并行遍历，外部线程池驱动 |
| `for_each_paged(offset, limit, func)` | 分页遍历 |
| `for_each_changed(func)` | 遍历自上次调用后发生变更的实体 |
| `size()` | 返回主集大小（上限，非精确匹配数） |
| `count()` | 精确命中数（遍历计算） |
| `empty()` | 是否为空 |
| `contains(entity)` | 检查实体是否匹配查询 |
| `get_ptr<T>(entity)` | 获取实体的组件指针 |
| `get_first_entity()` | 返回第一个匹配实体 |
| `sort_by_component<T>(cmp)` | 按组件值排序，结果存于 `get_sorted_entities()` |
| `changed()` | 检测组件池版本是否变化 |
| `reset_change_tracking()` | 重置变更检测基线 |
| `begin()` / `end()` | 迭代器，支持 range-for |
| `rebuild()` | 重新选择最小集合（组件数量变化后调用） |
| `get_entity_mask(entity)` | 获取实体组件位掩码 |
| `get_component_bit<T>()` | 获取组件类型的位掩码位 |

### 12.5 组件类型无上限

组件类型数量不受 64 限制。type_id ≤ 64 的组件参与实体位掩码；type_id > 64 的组件 `get_component_bit<T>()` 返回 0，但仍可正常添加、查询，并参与所有视图/分组（`view` / `group` / `owning_group` / `reorder_group` / `runtime_view` / `view(without)`）的匹配。无需任何特殊处理。

```cpp
ecs::manager mgr;

// 假设已注册超过 64 种组件类型，CompA 的 type_id = 64，CompB 的 type_id = 65
mgr.add(e1, CompA{});
mgr.add(e2, CompB{});
mgr.add(e3, CompA{});
mgr.add(e3, CompB{});

// CompB 超过 64，get_component_bit 返回 0，但查询照常工作
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<CompA>(),
    type_id::get_type_id<CompB>()
});
rv.for_each([](entity e) { /* 命中 e3 */ });

// group / view(without) 同样支持超过 64 的组件类型
auto g = mgr.group<CompA, CompB>();
mgr.view<CompA>(ecs::without<CompB>).for_each([](CompA&) {});
```

### 12.6 for_each_typed — 组件引用回传

`for_each` 只回调 `entity`，需手动调用 `get_ptr` 获取组件。`for_each_typed<Ts...>` 直接回传组件引用，`Ts` 顺序对应 `runtime_view_create` 传入的 type_id 顺序。

```cpp
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<Position>(),
    type_id::get_type_id<Velocity>()
});

// 回调接收 entity + 组件引用
rv.for_each_typed<Position, Velocity>([](entity e, Position& p, Velocity& v) {
    p.x += v.dx;
});

// 也可不接收 entity
rv.for_each_typed<Position>([](Position& p) {
    p.x += 1;
});
```

机制：内部遍历命中实体后，通过 sparse 数组按 version 校验获取组件指针，全部存在才回调。`Ts` 必须是 `runtime_view_create` 中 required_ids 的子集。

### 12.7 for_each_parallel — 并行迭代

按 primary dense 数组分片，由外部线程池驱动。每个 worker 处理 `[worker_id * per_worker, (worker_id+1) * per_worker)` 区间。

```cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

// 2 个 worker
rv.for_each_parallel(0, 2, [](entity e, size_t worker_id) {
    // worker 0 处理前半
});
rv.for_each_parallel(1, 2, [](entity e) {
    // worker 1 处理后半
});
```

机制：分片基于 primary_set 的 dense 索引，非实体索引。纯 OR 查询无 primary_set，不支持并行分片。

### 12.8 for_each_paged — 分页遍历

```cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

// 每页 100 个，处理第 2 页
rv.for_each_paged(100, 100, [](entity e) {
    // 处理实体
});
```

机制：`offset` 和 `limit` 基于 primary dense 索引。offset 超出范围时回调不触发。

### 12.9 变更检测

通过组件池版本号检测变更。首次调用 `reset_change_tracking` 记录基线，`changed` 比较当前版本与基线。

```cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

rv.reset_change_tracking();      // 记录基线
// ... 修改组件 ...
if (rv.changed()) {              // 版本变化则 true
    rv.for_each_changed([](entity e) {
        // 遍历所有匹配实体（非增量，全量遍历）
    });
    // for_each_changed 内部调用 reset_change_tracking
}
```

机制：`changed` 比较所有 required 集合的 `pool_version`。`for_each_changed` 在 `changed` 为 true 时全量遍历并重置基线。检测的是"有无变更"而非"哪些实体变更"。

### 12.10 sort_by_component — 按组件排序

```cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

rv.sort_by_component<Position>([](const Position& a, const Position& b) {
    return a.x < b.x;  // 升序
});

// 排序结果缓存在视图中
for (const auto& e : rv.get_sorted_entities()) {
    auto* p = mgr.get_ptr<Position>(e);
    // 按 x 升序处理
}
```

机制：收集所有命中实体的组件副本，用 `std::sort` 排序，结果存入 `sorted_entities_`。排序后实体顺序与 primary dense 顺序无关。`rebuild` 或组件变更后需重新排序。

### 12.11 count — 精确命中数

```cpp
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<Position>(),
    type_id::get_type_id<Velocity>()
});

size_t n = rv.count();  // 精确匹配数，遍历计算
```

机制：`size()` 返回 primary_set 大小（上限），`count()` 遍历所有命中实体计数。`count` 是 O(n) 操作。

### 12.12 iterator — 迭代器

```cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

// range-for
for (auto it = rv.begin(); it != rv.end(); ++it) {
    entity e = *it;
    auto* p = mgr.get_ptr<Position>(e);
}

// 等价于
for (entity e : rv) {
    // 处理 e
}
```

机制：迭代器基于 primary dense 索引，`advance_to_valid` 跳过不匹配的实体。`end()` 的 index 为 primary_set 大小。

### 12.13 runtime_term — OR / OPTIONAL / NOT 查询

通过 `runtime_term` 构造查询条件，支持 OR（并集）、NOT（排除）、OPTIONAL（可选）操作。

```cpp
class_pool<ecs::runtime_term> terms;
// AND: op=0
terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Position>(), 0, ecs::access_mode::read_write});
// OR: op=1
terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Velocity>(), 1, ecs::access_mode::read_only});
// NOT: op=2
terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Health>(), 2, ecs::access_mode::read_only});
// OPTIONAL: op=3
terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Name>(), 3, ecs::access_mode::read_only});

auto rv = mgr.runtime_view_create_from_terms(std::move(terms));
rv.for_each([](entity e) {
    // 命中：有 Position OR Velocity，且无 Health
});
```

| op 值 | 语义 | 说明 |
|-------|------|------|
| 0 | AND | 必须拥有 |
| 1 | OR | 至少命中一个 OR term |
| 2 | NOT | 必须不拥有 |
| 3 | OPTIONAL | 可选，不影响命中 |

机制：纯 OR 查询（无 AND term）时遍历所有 OR 集合并集去重。有 AND term 时以最小 AND 集合为 primary_set 遍历，对每个实体检查 OR / NOT 条件。OR 查询以及 1-2 个 AND term 的简单查询走 sparse 交集；3+ AND term 且 block 数合理时走实体掩码快路径。

### 12.14 access_mode — 读写标注

`runtime_term.access` 标注组件访问模式，用于意图声明。

```cpp
class_pool<ecs::runtime_term> terms;
terms.emplace_back(ecs::runtime_term{
    type_id::get_type_id<Position>(), 0, ecs::access_mode::read_only});
terms.emplace_back(ecs::runtime_term{
    type_id::get_type_id<Velocity>(), 0, ecs::access_mode::read_write});

auto rv = mgr.runtime_view_create_from_terms(std::move(terms));
```

| 值 | 语义 |
|----|------|
| `access_mode::read_only` | 只读访问 |
| `access_mode::read_write` | 读写访问 |

机制：当前版本 `access_mode` 仅作为意图标注，不影响查询行为。可用于后续并行调度时的依赖分析。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `size()` 依赖精确值 | 返回的是主集大小上限，非精确匹配数 | 使用 `count()` 获取精确值或 `for_each` 遍历 |
| 用 `get_component_bit<T>() != 0` 判断组件是否存在 | type_id > 64 的组件 bit 恒为 0 | 用 `get_ptr<T>(e) != nullptr` 或视图查询判断 |
| 组件数量变化后忘记 `rebuild()` | 主集选择可能不是最优 | 增删组件类型后调用 `rebuild()` |
| `for_each_changed` 依赖增量语义 | 全量遍历匹配实体，非仅变更实体 | 变更检测仅判断"有无变更" |
| 纯 OR 查询使用 `for_each_parallel` | 纯 OR 无 primary_set，不支持分片 | 纯 OR 查询使用 `for_each` 或 `count` |
| `sort_by_component` 后不重新排序就修改组件 | 排序缓存与实际数据不一致 | 组件变更后重新调用 `sort_by_component` |

---

## 13. 函数存储（回调作为组件）

将函数/回调封装为组件，通过 `ecs::manager` 的标准组件接口存储与调用。

### 使用

定义一个包含 `std::function` 的组件结构体，然后像普通组件一样使用。

```cpp
// 定义回调组件
struct CallbackComponent
{
    std::function<void(int)> callback;
    CallbackComponent(std::function<void(int)> cb) : callback(std::move(cb)) {}
};

ecs::manager mgr;
entity e = mgr.create_entity();

// 存储 lambda 作为组件
mgr.add(e, CallbackComponent([](int x) {
    std::cout << "Lambda called: x=" << x << "\n";
}));

// 获取并调用
auto* cb = mgr.get_ptr<CallbackComponent>(e);
if (cb)
{
    cb->callback(42);  // 输出: Lambda called: x=42
}
```

### 通过 View 批量调用

```cpp
// 为多个实体添加回调
entity e1 = mgr.create_entity();
entity e2 = mgr.create_entity();
mgr.add(e1, CallbackComponent([](int x) { std::cout << "e1: " << x << "\n"; }));
mgr.add(e2, CallbackComponent([](int x) { std::cout << "e2: " << x << "\n"; }));

// 遍历所有回调组件并调用
mgr.view<CallbackComponent>().for_each([](entity e, CallbackComponent& c) {
    c.callback(0);  // 调用每个回调
});
```

---

## 14. 生命周期信号

两层架构：**即时信号**（函数指针回调） + **延迟信号**（环形缓冲区 + 溢出 chain，批量处理）。

**核心机制：即时/延迟互斥。** 注册了即时回调的事件同步触发且不入延迟队列；未注册才入队，由 `flush_*_signals` 处理。同一事件不会两路重复通知。

### 14.1 实体级即时信号

实体创建/销毁时立即触发回调。通过 `void* user_data` 传递上下文。

**接口：**

| 接口 | 说明 |
|------|------|
| `set_on_entity_created(fn, user_data)` | 绑定实体创建回调：`void fn(entity, void* user_data)` |
| `set_on_entity_destroyed(fn, user_data)` | 绑定实体销毁回调：`void fn(entity, void* user_data)` |

```cpp
ecs::manager mgr;
mgr.append_preallocated_entities(100);

int created = 0, destroyed = 0;

// 无捕获 lambda（可转换为函数指针）
auto on_created = [](entity e, void* data) noexcept {
    (*static_cast<int*>(data)) += 1;
};
auto on_destroyed = [](entity e, void* data) noexcept {
    (*static_cast<int*>(data)) += 1;
};

mgr.set_on_entity_created(+on_created, &created);
mgr.set_on_entity_destroyed(+on_destroyed, &destroyed);

entity e1 = mgr.create_entity();  // created == 1
entity e2 = mgr.create_entity();  // created == 2
mgr.delete_entity(e1);           // destroyed == 1
```

> **注意：** 回调必须是 `void (*)(entity, void*) noexcept` 签名。使用 `+` 将无捕获 lambda 转为函数指针。

### 14.2 组件级即时信号

组件添加/移除时立即触发回调。回调接收实体、组件指针和 `user_data`，可在回调中直接修改组件数据。

**接口：**

| 接口 | 说明 |
|------|------|
| `set_on_add<T>(fn, user_data)` | 绑定组件 T 添加回调：`void fn(entity, void* component, void* user_data)` |
| `set_on_remove<T>(fn, user_data)` | 绑定组件 T 移除回调：`void fn(entity, void* component, void* user_data)` |

```cpp
ecs::manager mgr;
mgr.append_preallocated_entities(100);

int add_count = 0, remove_count = 0;

auto on_add = [](entity e, void* comp, void* data) noexcept {
    (*static_cast<int*>(data)) += 1;
    auto* pos = static_cast<Position*>(comp);
    pos->x = 100;  // 直接修改组件
};
auto on_remove = [](entity e, void* comp, void* data) noexcept {
    (*static_cast<int*>(data)) += 1;
};

mgr.set_on_add<Position>(+on_add, &add_count);
mgr.set_on_remove<Position>(+on_remove, &remove_count);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 2});        // add_count == 1, Position.x 被改为 100
mgr.add(e, Position{3, 4});        // 覆盖写:未注册 on_modify 时回退为 on_remove(旧)+on_add(新)
mgr.hard_remove<Position>(e);      // remove_count == 2(覆盖 1 + hard_remove 1)
```

> **注意：** 组件指针在回调期间有效，可用于读取或修改组件数据。`hard_remove` 触发 `on_remove`；`soft_remove` 仅逻辑隐藏组件（未析构），**不触发** `on_remove` 也不入延迟队列。

### 14.3 覆盖写与 on_modify

对同一实体的同一组件再次 `add` 称为覆盖写。覆盖写语义由 `on_modify` 是否注册决定：

- **注册了 `on_modify<T>`**：覆盖写只触发 `on_modify`，不触发 `on_remove`/`on_add`。
- **未注册 `on_modify<T>`**：覆盖写回退为 `on_remove`(旧组件) + `on_add`(新组件)。

| 接口 | 说明 |
|------|------|
| `set_on_modify<T>(fn, user_data)` | 绑定组件 T 覆盖写回调：`void fn(entity, void* component, void* user_data)` |

```cpp
ecs::manager mgr;
mgr.append_preallocated_entities(10);
int add_cnt = 0, remove_cnt = 0, modify_cnt = 0;
mgr.set_on_add<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &add_cnt);
mgr.set_on_remove<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &remove_cnt);
mgr.set_on_modify<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &modify_cnt);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 0});   // add_cnt == 1
mgr.add(e, Position{2, 0});   // modify_cnt == 1, add_cnt/remove_cnt 不变
```

> **不应：** 靠 `on_add` 区分新增与覆盖。注册 `on_modify` 后覆盖路径不再走 `on_add`。

### 14.4 实体级延迟信号

实体创建/销毁事件被推入环形缓冲区，调用 `flush_entity_signals` 时批量处理。适合批量同步、避免重入的场景。

**接口：**

| 接口 | 说明 |
|------|------|
| `flush_entity_signals(handler)` | 批量处理所有待处理实体信号：`handler(uint32_t type, uint32_t entity_idx)` |
| `has_pending_entity_signals()` | 是否有待处理实体信号 |

**信号类型：**
- `type=0`：实体创建
- `type=1`：实体销毁

```cpp
ecs::manager mgr;
mgr.append_preallocated_entities(100);

entity e1 = mgr.create_entity();  // 推入缓冲区
entity e2 = mgr.create_entity();  // 推入缓冲区
mgr.delete_entity(e1);           // 推入缓冲区

int created = 0, destroyed = 0;

// 批量处理
mgr.flush_entity_signals([&](uint32_t type, uint32_t entity_idx) {
    if (type == 0)
        created += 1;
    else if (type == 1)
        destroyed += 1;
});

// created == 2, destroyed == 1
assert(!mgr.has_pending_entity_signals());
```

> **缓冲区容量：** 1024 条（2 的幂）。缓冲区满时事件落入 `overflow_chain`，`overflow_count` 累计，`flush` 一并消费，不静默丢弃。

### 14.5 组件级延迟信号

组件添加/移除事件被推入环形缓冲区，调用 `flush_component_signals` 时批量处理。

**接口：**

| 接口 | 说明 |
|------|------|
| `flush_component_signals(handler)` | 批量处理所有待处理组件信号：`handler(uint32_t type, uint32_t entity_idx, uint32_t component_id)` |
| `has_pending_component_signals()` | 是否有待处理组件信号 |

**信号类型：**
- `type=0`：组件添加
- `type=1`：组件移除

```cpp
ecs::manager mgr;
mgr.append_preallocated_entities(100);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 2});    // 推入缓冲区
mgr.add(e, Velocity{10, 20});  // 推入缓冲区
mgr.add(e, Health{100, 100});  // 推入缓冲区
mgr.hard_remove<Position>(e);  // 推入缓冲区

int added = 0, removed = 0;

mgr.flush_component_signals([&](uint32_t type, uint32_t entity_idx, uint32_t component_id) {
    if (type == 0)
        added += 1;
    else if (type == 1)
        removed += 1;
});

// added == 3, removed == 1
assert(!mgr.has_pending_component_signals());
```

### 14.6 即时/延迟互斥机制

即时回调与延迟队列对同一事件**互斥**，避免重复通知：

- `set_on_add<T>` 注册后：`add` 同步触发 `on_add`，**不**入组件延迟队列。
- `set_on_remove<T>` 注册后：`hard_remove` 同步触发 `on_remove`，**不**入组件延迟队列。
- `set_on_entity_created` 注册后：`create_entity` 同步触发，**不**入实体延迟队列。
- `set_on_entity_destroyed` 注册后：`delete_entity` 同步触发，**不**入实体延迟队列。

```cpp
ecs::manager mgr;
mgr.append_preallocated_entities(10);
int add_cb = 0;
mgr.set_on_add<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &add_cb);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 0});   // add_cb == 1,不入队

int add_sig = 0;
mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
    if (type == 0) ++add_sig;
});
// add_sig == 0(已注册即时回调,不重复入队)
```

> **不应：** 同一事件既订阅即时回调又期望 `flush` 收到。互斥设计下二者只走一路。

### 14.7 信号开关与溢出

**信号开关**控制延迟队列是否入队（不影响即时回调）：

| 接口 | 说明 |
|------|------|
| `disable_comp_signals()` / `enable_comp_signals()` | 关/开组件延迟信号入队 |
| `disable_entity_signals()` / `enable_entity_signals()` | 关/开实体延迟信号入队 |

**溢出处理：** 环形缓冲区满时事件落入 `overflow_chain`，`flush_*_signals` 先消费缓冲区再消费 chain。`overflow_count` 累计溢出次数（不随 flush 清零），需手动 `reset_*_overflow_count`。

| 接口 | 说明 |
|------|------|
| `comp_signal_overflow_count()` / `entity_signal_overflow_count()` | 查询累计溢出次数 |
| `reset_comp_signal_overflow_count()` / `reset_entity_signal_overflow_count()` | 清零溢出计数 |
| `reserve_comp_signal_capacity(n)` / `reserve_entity_signal_capacity(n)` | 预分配 overflow chain 容量 |

```cpp
ecs::manager mgr;
mgr.disable_entity_signals();
auto e1 = mgr.create_entity();   // 不入队
mgr.enable_entity_signals();
auto e2 = mgr.create_entity();   // 入队
// has_pending_entity_signals() == true

mgr.reserve_comp_signal_capacity(2048);  // 预分配溢出容量
```

> **不应：** 长期忽略 `overflow_count`。其值 >0 表示曾发生溢出，应确认 `flush` 已消费完 chain（`has_pending_*_signals()` 为 false）。

### 14.8 delete_entity 的组件清理

`delete_entity` 会先遍历该实体身上所有组件触发清理，再销毁实体：

- 已注册 `on_remove<T>` 的组件：同步触发 `on_remove`。
- 未注册 `on_remove<T>` 的组件：remove 信号入组件延迟队列。

```cpp
ecs::manager mgr;
mgr.append_preallocated_entities(10);
int pos_removed = 0;
mgr.set_on_remove<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &pos_removed);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 0});
mgr.add(e, Velocity{2, 0});   // Velocity 未注册 on_remove
mgr.delete_entity(e);         // pos_removed == 1;Velocity remove 信号入队

int removed_sig = 0;
mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
    if (type == 1) ++removed_sig;
});
// removed_sig == 1(Velocity)
```

> **不应：** 在 `on_remove` 回调内访问已销毁实体。回调在实体销毁前触发，实体此时仍有效；回调返回后实体才被销毁。
>
> **flush 顺序建议：** 先 `flush_component_signals` 再 `flush_entity_signals`。组件 remove 信号在实体销毁前入队，先消费组件信号可避免实体 id 被复用导致的归属错乱。

### 14.9 即时信号 vs 延迟信号 选择指南

| 场景 | 推荐方案 | 原因 |
|------|----------|------|
| 日志/调试 | 即时信号 | 需要立即输出，延迟无意义 |
| 数据校验 | 即时信号 | 需要在组件写入同时验证 |
| 网络同步 | 延迟信号 | 批量发送，减少网络开销 |
| 物理/渲染回调 | 延迟信号 | 避免在 ECS 操作中途触发重入 |
| 文件持久化 | 延迟信号 | 批量写入，减少 I/O |
| 第三方集成 | 延迟信号 | 解耦 ECS 内部状态与外部系统 |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 在即时信号回调中增删实体/组件 | 可能导致重入 | 使用延迟信号，在 flush 时处理；flush 内部有重入保护与 budget 上限 |
| 依赖延迟信号缓冲区不丢事件 | 缓冲区满时落入 overflow_chain | 定期 flush；批量场景用 `reserve_*_signal_capacity` 预分配 |
| 忘记 `flush` 延迟信号 | 事件堆积在缓冲区与 chain 中未处理 | 每帧开头或结尾调用 `flush_*_signals` |
| 使用有捕获的 lambda 作为即时信号回调 | 无法转换为函数指针 | 使用无捕获 lambda + `user_data` 传上下文 |
| 期望 `soft_remove` 触发 `on_remove` | `soft_remove` 仅逻辑隐藏，不析构不触发 | 需要回调改用 `hard_remove` |
| 同一事件既注册即时回调又等 flush | 互斥设计下不会重复通知，flush 收不到该事件 | 二选一：要即时就注册回调，要批量就不注册 |
| 在 `on_remove` 回调内访问已销毁实体 | 实体在回调返回后才销毁，回调内访问的是销毁前状态 | 回调内可安全读取组件，但不要依赖实体后续有效性 |
| 回调 `user_data` 指向局部变量后让变量超出作用域 | 回调持有悬垂指针，后续触发写入野内存，污染相邻栈变量 | 块结束前 `set_on_*(nullptr, nullptr)` 解绑，或让 `user_data` 指向生命周期足够长的对象 |

---

## 15. command_buffer — 延迟结构变更

将组件添加、移除、实体销毁等结构变更操作暂存，在 `flush` 时一次性应用到 manager。适用于帧末批量提交、主循环延迟执行等场景。

### 使用

```cpp
ecs::manager mgr;
auto a = mgr.create_entity();
auto b = mgr.create_entity();
auto c = mgr.create_entity();
mgr.add(a, Position{1, 0, 0});

// 创建 command_buffer
auto cb = mgr.create_command_buffer();

// 录制命令(不立即执行)
cb.add_component<Position>(b, Position{2, 0, 0});
cb.add_component<Velocity>(a, Velocity{10, 0, 0});
cb.remove_component<Position>(a);
cb.destroy_entity(c);

// flush 前状态不变
// mgr.get_ptr<Position>(b) == nullptr
// mgr.get_ptr<Position>(a) != nullptr

// 一次性应用
cb.flush();

// flush 后
// mgr.get_ptr<Position>(b) != nullptr, x == 2.0f
// mgr.get_ptr<Velocity>(a) != nullptr
// mgr.get_ptr<Position>(a) == nullptr
// !mgr.is_entity_valid(c)
```

**接口：**

| 接口 | 说明 |
|------|------|
| `create_command_buffer()` | manager 工厂方法，返回绑定到该 manager 的 command_buffer |
| `add_component<T>(entity, T&&)` | 录制添加组件命令 |
| `remove_component<T>(entity)` | 录制移除组件命令（soft_remove 语义） |
| `destroy_entity(entity)` | 录制销毁实体命令 |
| `flush()` | 按录入顺序应用所有命令，应用后清空缓冲区 |
| `clear()` | 清空所有未应用命令 |
| `size()` | 未应用命令数 |
| `empty()` | 是否为空 |

机制：每条命令存储 `entity` 副本 + `void_any` 载荷 + 类型擦除的 apply 函数指针。`flush` 按顺序调用各 apply 函数，内部调用 manager 的 `add` / `soft_remove` / `delete_entity`。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `flush` 后用 `entity.is_valid()` 判断实体是否被销毁 | `destroy_entity` 按值传入，修改的是副本 | 用 `mgr.is_entity_valid(entity)` 检查 |
| `flush` 后继续使用已销毁实体的句柄 | version 已过期，操作无效 | `flush` 后重新获取有效实体 |
| 跨 manager 使用 command_buffer | apply 函数绑定到创建时的 manager | 每个 manager 独立创建 command_buffer |
| 在 `flush` 过程中向同一 command_buffer 录入新命令 | `flush` 结束时清空缓冲区，新命令丢失 | `flush` 完成后再录入新命令 |

---


# 二、宏配置

## 16. 编译与运行

### CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### 运行示例

```bash
./build/usagec.exe    # 完整接口示例
./build/test.exe      # 测试
```

### 编译要求

- C++20（需支持 `std::format`）
- CMake 3.16+

---

## 17. 可选宏配置

### 17.1 栈内存控制（`config/ecs_config.hpp`）

嵌入式 / RTOS 环境通过 `LCF_MINIMAL_STACK` 关闭所有栈优化，将基数排序的大数组从栈分配切换到堆分配。桌面环境默认 `0`，保留栈分配以获得最佳性能。

| 宏 | 默认值 | 说明 |
|------|--------|------|
| `LCF_MINIMAL_STACK` | `0` | `1` 关闭栈优化：基数排序直方图（16KB×2）与 `count_stack`（16KB×2）改走堆分配 |

```cmake
# 嵌入式项目在 CMakeLists.txt 中定义
target_compile_definitions(my_target PRIVATE LCF_MINIMAL_STACK=1)
```

| 受控点 | 栈占用（默认） | 嵌入式回退 |
|--------|---------------|-----------|
| `radix_count_pass` bc≤512 分支 | 16KB（h0-h3 局部直方图） | `::operator new` 堆分配，失败回退单直方图 |
| `radix_count_pass` bc≤1024 分支 | 16KB（h0-h1 局部直方图） | `::operator new` 堆分配，失败回退单直方图 |
| `radix_sort_entries_with_cfg` count_stack | 16KB（2048 个 size_t） | `::operator new` 堆分配 |
| `radix_sort_indices_with_cfg` count_stack | 16KB（2048 个 size_t） | `::operator new` 堆分配 |

### 17.2 void_any 存储策略（`config/void_any_config.hpp`）

影响 `void_any` 的存储策略与内存分配方式。

| 宏 | 说明 |
|------|------|
| `VOID_ANY_ENABLE_SSO` | 启用 void_any 小对象存储（SSO），小对象内联存储 |
| `VOID_ANY_ENABLE_MEMORY_POOL` | 启用 void_any 内存池，使用 `memory_pool` 替代 `::operator new` |
| `VOID_ANY_USE_LAYERED_ALLOCATOR` | 启用分层分配器：小对象（≤128B）走 slab，大对象走 TLSF（优先级高于 `VOID_ANY_ENABLE_MEMORY_POOL`） |
| `VOID_ANY_SSO_BUFFER_SIZE` | SSO 缓冲区大小（默认 56 字节，与 `vtable_sso_type_` 共 64 字节 = 1 cache line） |
| `VOID_ANY_SSO_ALIGNMENT` | SSO 对齐（默认 8 字节；设为 32 会破坏 `sizeof==64` 不变量） |
| `VOID_ANY_MEMORY_POOL_NOT_ENABLED` | 禁用内存池（与 `VOID_ANY_ENABLE_MEMORY_POOL` 互斥） |
| `VOID_ANY_SSO_NOT_ENABLED` | 禁用 SSO（与 `VOID_ANY_ENABLE_SSO` 互斥） |

### 17.3 配置示例

```cpp
// config/void_any_config.hpp

// 启用内存池
#define VOID_ANY_ENABLE_MEMORY_POOL

// 启用分层分配器（小对象走 slab, 大对象走 TLSF, 优先级高于 memory_pool）
#define VOID_ANY_USE_LAYERED_ALLOCATOR

// 启用小对象存储
#define VOID_ANY_ENABLE_SSO

// SSO 缓冲区大小: 56 + 8(vtable_sso_type_) = 64 (1 cache line)
#define VOID_ANY_SSO_BUFFER_SIZE 56

// SSO 对齐: 8 确保 sizeof(void_any)==64
#define VOID_ANY_SSO_ALIGNMENT 8
```

### 17.4 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `VOID_ANY_SSO_ALIGNMENT` 设为 32 | storage padding 至 64，总大小变 96，破坏 1 cache line 不变量 | 保持默认 8，现代 x86 unaligned AVX2 已接近 aligned 性能 |
| 同时定义 `VOID_ANY_ENABLE_MEMORY_POOL` 和 `VOID_ANY_MEMORY_POOL_NOT_ENABLED` | 互斥宏冲突 | 二选一 |
| `LCF_MINIMAL_STACK=1` 后期望排序性能不变 | 堆分配有额外开销，且失去栈直方图并行计数加速 | 嵌入式场景排序非热路径，可接受 |
| 在 `config/` 文件夹外查找配置文件 | `ecs_config.hpp` 和 `void_any_config.hpp` 均在 `include/config/` | include 路径为 `"config/ecs_config.hpp"` 和 `"config/void_any_config.hpp"` |

---


# 三、各种模块

## 18. tiered_sort / pdqsort / sort_n — 分级排序

`#include "part/tiered_sort.hpp"`，位于 `detail` 命名空间。所有函数 `noexcept`。

### 接口

| 接口 | 签名 | 说明 |
|------|------|------|
| `pdqsort` | `void pdqsort<T>(T* data, size_t n, Compare&& cmp)` | 3-way pdqsort，要求 `is_trivially_copyable_v<T>` |
| `tiered_sort` | `void tiered_sort<T>(T* data, size_t n, Compare&& cmp)` | 分级排序值数组，按 `cmp` 升序 |
| `tiered_sort_indices` | `void tiered_sort_indices<T>(size_t* indices, const T* values, size_t n)` | 索引排序，按 `values[indices[i]]` 升序排列 `indices` |
| `sort_n` | `void sort_n<N, T>(T* data, Compare&& cmp)` | 编译期已知 N 的零开销排序，N≤16 时使用排序网络 |
| `sort_indices_n` | `void sort_indices_n<N, T>(size_t* indices, const T* values)` | 编译期已知 N 的零开销索引排序 |
| `sort` | `void sort<T>(T* data, size_t n, Compare&& cmp)` | `tiered_sort` 的别名 |
| `sort_indices` | `void sort_indices<T>(size_t* indices, const T* values, size_t n)` | `tiered_sort_indices` 的别名 |

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

- 排序网络使用 `constexpr` 在编译期生成 Batcher 奇偶归并网络，运行时无分支、无循环
- `sort_n<N>` 在 N≤16 时编译期展开为排序网络，无派发开销
- 基数排序分配失败时自动降级为 pdqsort
- `pdqsort` 使用 3-way Dutch National Flag 分区，高效处理重复键

### 机制

- **排序网络**：n≤16 时，通过 `constexpr` 在编译期生成比较交换对序列，运行时通过 fold expression 完全展开为无分支指令序列。N=2 时 1 次比较，N=16 时 63 次比较
- **`sort_n<N>`**：N 为编译期常量时，`if constexpr` 在编译期选择算法，无运行时派发开销。N≤16 时直接展开排序网络
- **`tiered_sort`**：n 为运行时参数时，n≤16 通过 switch 跳转表派发到对应排序网络

### 使用

```cpp
#include "part/tiered_sort.hpp"

// 运行时 n: 分级排序
int data[] = {5, 3, 1, 4, 2};
tiered_sort(data, 5, std::less<int>{});
// data: {1, 2, 3, 4, 5}

// 编译期 N: 零开销排序网络
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
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对非 trivially_copyable 类型调用 `pdqsort` | 编译错误（concept 约束） | 对复杂类型用 `tiered_sort`（无此约束） |
| `tiered_sort_indices` 传入非算术类型 T | 非算术类型走 pdqsort，无法用基数排序 | 若需基数排序，确保 T 为整数或浮点 |
| `data` 或 `indices` 为空指针且 n > 0 | 未定义行为 | 确保 n == 0 或指针有效 |
| 排序期间并发读写同一数组 | 数据竞争 | 排序完成后再访问 |
| `sort_n<N>` 传入 N=0 | 编译期返回，无操作 | 确保数组实际大小 ≥ N |

---

## 19. radix_sort — 基数排序

`#include "part/radix_sort_helper.hpp"`，位于 `detail` 命名空间。所有函数 `noexcept`。

### 接口

| 接口 | 签名 | 说明 |
|------|------|------|
| `is_radix_sortable_v<T>` | concept | `is_integral_v<T> \|\| is_floating_point_v<T>`，T 是否可基数排序 |
| `radix_key(T val)` | `auto` | 将 T 转换为无序保持的 unsigned 值（负数翻转） |
| `radix_sort_entries` | `void radix_sort_entries<KeyType>(void* entries, size_t n)` | 排序 `{KeyType key; size_t index;}` 数组，按 key 升序 |
| `radix_sort_indices` | `void radix_sort_indices<KeyType>(size_t* indices, const KeyType* keys, size_t n, size_t* temp_buf)` | 索引基数排序，按 keys[indices[i]] 升序 |

### radix 配置

| 类型 | 趟数 | 每趟位数 | 总桶数 |
|------|------|----------|--------|
| uint32_t / float | 3 | 11-11-10 | 2048+2048+1024 |
| uint64_t / double | 6 | 11-11-11-11-11-9 | 5×2048+512 |

### 机制

- `radix_key` 将有符号整数做 XOR 翻转（`u ^= (1 << (bits-1))`），浮点做 IEEE 754 无序保持转换（负数全翻转、正数翻转符号位），使排序后保持原始序
- `radix_sort_entries` 分配临时缓冲区（entries + keys），失败时回退 `fallback_heap_sort`
- `radix_sort_indices` 需调用方提供 `temp_buf`（`n * sizeof(size_t)` 字节），内部分配 keys 缓冲区
- 所有函数要求 `requires is_radix_sortable_v<KeyType>`，n ≤ 1 直接返回

### 使用

```cpp
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
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对非算术/浮点类型调用 `radix_sort_*` | 编译错误（concept 约束） | 仅用于 `int`/`uint`/`float`/`double` |
| `radix_sort_indices` 传入空 `temp_buf` | 写入无效内存 | 分配 `n * sizeof(size_t)` 字节的 temp 缓冲区 |
| `radix_sort_entries` 的 entries 不是 `{KeyType key; size_t index;}` 布局 | 内存解释错误 | 确保结构体首字段为 KeyType、次字段为 size_t |
| n = 0 时调用 | 安全返回（n ≤ 1 短路） | 无问题，但无意义 |

---

## 20. FORCE_INLINE — 跨平台内联宏

`#include "part/force_inline.hpp"`

| 宏 | 说明 |
|------|------|
| `FORCE_INLINE` | 强制函数内联，跨编译器适配 |

| 编译器 | 展开为 |
|--------|--------|
| MSVC | `__forceinline` |
| GCC / Clang | `inline __attribute__((always_inline))` |
| 其他 | `inline` |

### 使用

```cpp
#include "part/force_inline.hpp"

FORCE_INLINE int add(int a, int b) noexcept
{
    return a + b;
}
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 递归函数标记 `FORCE_INLINE` | 编译器可能忽略或导致代码膨胀 | 递归函数不使用 `FORCE_INLINE` |
| 大函数标记 `FORCE_INLINE` | 代码膨胀，icache 压力增大 | 仅对热路径小函数使用 |

---

## 21. view_tags — 视图标签类型

`#include "view_tags.hpp"`，位于 `ecs` 命名空间。用于构造 View / Group / runtime_view 的标签参数。

### 接口

| 标签 | 类型 | 说明 |
|------|------|------|
| `without<Types...>` | `without_t<Types...>` | 排除含有任一 Types 组件的实体 |
| `with<Types...>` | `with_t<Types...>` | 额外获取 Types 组件的引用 |
| `exclude<Types...>` | `without_t<Types...>` | `without` 的别名 |
| `get<Types...>` | `with_t<Types...>` | `with` 的别名 |
| `owned<Types...>` | `owned_t<Types...>` | 标记 Types 为 Group 所拥有（重排 dense 数组） |
| `reorder<Types...>` | `reorder_t<Types...>` | 标记 Types 为 Group 可重排（轻量 owned 语义） |
| `ordered<Types...>` | `struct` | 标记排序顺序 |
| `Component<T>` | concept | `is_copy_constructible_v<T> \|\| is_move_constructible_v<T>` |

### 使用

```cpp
// 单组件视图, 排除含 Static 标记的实体
auto v = mgr.view<Position>(ecs::without<Static>{});

// 多组件视图, 额外获取 Velocity 引用
auto v = mgr.view<Position, Velocity>(ecs::with<Acceleration>{});

// OwningGroup: Position 的 dense 数组与 Velocity 连续排布
auto g = mgr.group<Position, Velocity>(ecs::owned<Position>{});

// ReorderGroup: 允许重排 Position
auto g = mgr.group<Position, Velocity>(ecs::reorder<Position>{});

// runtime_view 排除
auto rv = mgr.runtime_view_create({pos_id, vel_id}, {static_id});
// 等价于 view<Position, Velocity>(without<Static>{})
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `without` 和 `with` 传同一类型 | 语义矛盾，行为未定义 | 不要对同一类型同时使用 |
| `owned` 标记非首模板参数 | Group 要求 owned 必须是首参数 | `group<First, Rest...>(owned<First>{})` |
| `reorder` 和 `owned` 对同一 Group 混用 | 语义冲突 | 一个 Group 只用 `owned` 或 `reorder`，不混用 |

---

## 22. arena_allocator — 线性 bump 分配器

`#include "part/arena_allocator.hpp"`，全局命名空间。`noexcept`。

线性 bump 分配器：无 header，无单个 `deallocate`，仅 `reset` 整体回收。两种模式：自有内存（析构释放）和借用外部 buffer（零所有权）。`base_` 64 字节对齐，支持 `align <= 64` 的分配请求。

### 接口

| 接口 | 说明 |
|------|------|
| `arena_allocator()` | 默认构造，空状态 |
| `arena_allocator(size_t capacity)` | 自有模式：分配 capacity 字节，析构释放 |
| `arena_allocator(void* buffer, size_t size)` | 借用模式：使用外部 buffer，不分配不释放 |
| `allocate(n, align=16)` | bump 分配，位运算对齐，返回指针或 nullptr |
| `reset()` | 整体回收，不调用析构 |
| `used()` | 已使用字节数 |
| `capacity()` | 总容量 |
| `remaining()` | 剩余字节数 |
| `empty()` | 是否未分配（offset==0） |
| `owns(p)` | 指针 p 是否属于本 arena |

### 使用

```cpp
#include "part/arena_allocator.hpp"

// 自有模式
arena_allocator arena(4096);
void* p1 = arena.allocate(128, 16);
void* p2 = arena.allocate(256, 32);
// arena.used() == 384 (对齐后)
arena.reset();  // 整体回收，不析构
// arena.empty() == true

// 借用模式（零所有权）
alignas(64) uint8_t buffer[2048];
arena_allocator borrowed(buffer, sizeof(buffer));
void* p3 = borrowed.allocate(100);
// 析构时不释放 buffer
```

### 机制

- 分配仅移动 offset 指针，无链表无 header，O(1)
- `reset` 将 offset 归零，不调用任何析构函数
- `owns` 通过指针范围比较，O(1)
- 不支持单个 deallocate，仅支持整体 reset

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对 arena 分配的对象调用 `deallocate` | 接口不存在 | 用 `reset` 整体回收 |
| `reset` 后继续使用之前分配的指针 | 内存已回收，数据未定义 | `reset` 后丢弃所有指针 |
| 借用模式下析构后访问 buffer | buffer 生命周期由外部管理 | 确保外部 buffer 生命周期覆盖 arena 使用期 |
| 分配超过 `remaining()` 的内存 | 返回 nullptr | 先检查 `remaining()` 或预分配足够容量 |

---

## 23. slab_allocator — 固定块分配器

`#include "part/slab_allocator.hpp"`，全局命名空间。`noexcept`。

固定块大小分配器，侵入式 free list，O(1) allocate/deallocate。每个 chunk 按序插入 `class_pool<chunk_node>` 保持地址有序，`owns` 用二分查找定位。

### 接口

| 接口 | 说明 |
|------|------|
| `slab_allocator(block_size, alignment=16, blocks_per_chunk=256)` | 构造，block_size 向上对齐到 alignment |
| `allocate()` | 分配一个块，返回指针或 nullptr |
| `deallocate(p)` | 释放一个块 |
| `owns(p)` | 指针 p 是否属于本 slab |
| `block_size()` | 实际块大小（对齐后） |
| `total_blocks()` | 总块数 |
| `free_blocks()` | 空闲块数 |
| `empty()` | 是否全部空闲 |
| `min_addr()` / `max_addr()` | 地址范围 |

### 使用

```cpp
#include "part/slab_allocator.hpp"

// 64 字节块分配器
slab_allocator slab(64);
void* p1 = slab.allocate();
void* p2 = slab.allocate();
// slab.total_blocks() >= 2, free_blocks() 减 2
slab.deallocate(p1);
// slab.free_blocks() 增 1

// 检查指针归属
bool mine = slab.owns(p2);  // true
```

### 机制

- 每个 chunk 分配 `block_size * blocks_per_chunk` 字节，按序插入 `chunks_` 保持地址有序
- free list 是侵入式的：空闲块的起始字节存储下一个空闲块指针，零 header 开销
- `allocate` 从 free list 头部取，`deallocate` 插入 free list 头部
- `owns` 先用 `min_addr/max_addr` 快速排除，单 chunk 直接范围判断，多 chunk 二分查找
- 自动 `grow`：free list 空时分配新 chunk

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `deallocate` 非本 slab 分配的指针 | free list 损坏，后续崩溃 | 先 `owns(p)` 验证 |
| `deallocate` 同一指针两次 | 双重释放，free list 损坏 | 确保每块只释放一次 |
| 跨 `block_size` 混用分配器 | 块大小不匹配 | 每种块大小用独立 slab_allocator |

---

## 24. layered_allocator — 分层分配器

`#include "part/layered_allocator.hpp"`，全局命名空间。`noexcept`。

组合 8 个 slab_allocator（16/32/48/64/80/96/112/128 字节）和 1 个 memory_pool（TLSF）。小对象（≤128B）走 slab，大对象走 TLSF。`deallocate` 通过 `find_slab` 遍历判断归属。

### 接口

| 接口 | 说明 |
|------|------|
| `layered_allocator()` | 默认构造，初始化 8 个 slab + 1 个 TLSF |
| `allocate(n)` | 按大小路由：≤128 走 slab，>128 走 TLSF |
| `deallocate(p)` | 通过 `find_slab` 判断归属，路由到 slab 或 TLSF |
| `deallocate(p, n)` | 按 size 直接路由，避免遍历 slab |
| `construct<T>(args...)` | 分配 + placement new 构造 T |
| `destroy<T>(p)` | 析构 + 释放 |
| `owns(p)` | 指针 p 是否属于本分层分配器 |
| `slab_max()` | slab 上限（128） |
| `big_pool()` | 内部 TLSF memory_pool 引用 |

### 使用

```cpp
#include "part/layered_allocator.hpp"

layered_allocator alloc;
void* small = alloc.allocate(64);   // 走 slab (64B 块)
void* large = alloc.allocate(256);  // 走 TLSF

// 带构造
auto* obj = alloc.construct<MyStruct>(arg1, arg2);
alloc.destroy(obj);

// 带 size 的 deallocate 更高效
alloc.deallocate(small, 64);  // 直接路由到 slab
```

### 机制

- 8 个 slab 大小固定：16/32/48/64/80/96/112/128，`slab_index(n)` 线性查找第一个 `n <= SLAB_SIZES[i]`
- `find_slab(p)` 遍历 8 个 slab 的 `owns`，O(8)
- `deallocate(p, n)` 用 `slab_index(n)` 直接定位，避免遍历
- TLSF `big_pool_` 处理 >128B 的分配

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用 `deallocate(p)` 释放大对象 | 遍历 8 个 slab 后才路由到 TLSF，慢 | 用 `deallocate(p, n)` 直接路由 |
| 修改 `memory_pool` 与 `slab_allocator` 的关系 | 项目规范禁止 | 保持分层路由不变 |

---

## 25. ring_buffer — 环形缓冲区

`#include "part/ring_buffer.hpp"`，全局命名空间。`noexcept`。

通用固定容量环形缓冲区，堆分配存储，栈上仅占指针大小。容量 N 必须为 2 的幂，内部用位掩码取模。不做元素零初始化，调用方仅在 `[read_, write_)` 区间读取。

### 接口

| 接口 | 说明 |
|------|------|
| `ring_buffer()` | 默认构造，堆分配 N 个元素 |
| `push(const T&)` / `push(T&&)` | 写入一个事件，满返回 false |
| `emplace(args...)` | 原位构造写入，满返回 false |
| `drain(handler)` | 读取并处理所有待处理事件，返回处理数 |
| `drain_with_budget(budget, handler)` | 带预算的 drain，防止 handler 内追加导致无限循环 |
| `peek()` | 仅读队首（不推进），空返回 nullptr |
| `pop()` | 弹出队首，空返回 false |
| `empty()` / `has_pending()` | 是否空 / 是否有待处理 |
| `pending_count()` | 待处理数量 |
| `capacity()` | 容量（编译期常量 N） |
| `clear()` | 清空（read_=write_=0） |

### 使用

```cpp
#include "part/ring_buffer.hpp"

struct event { int type; int data; };
ring_buffer<event, 1024> buf;

// 写入
buf.push({1, 100});
buf.emplace(2, 200);

// 批量处理
size_t n = buf.drain([](const event& e) {
    std::cout << "type=" << e.type << " data=" << e.data << "\n";
});
// n == 2

// 带预算处理（防重入）
buf.push({3, 300});
buf.drain_with_budget(1, [](const event& e) {
    // 只处理 1 个，即使 handler 内追加也不会无限循环
});
```

### 机制

- 存储用 `std::unique_ptr<T[]>`，栈上仅 `unique_ptr` + 2 个 `uint32_t`（read_/write_）≈ 16 字节
- 容量 N 必须为 2 的幂，取模用 `& (N-1)` 位运算
- `drain_with_budget` 限制最大处理数，防止 handler 内 `push` 导致无限循环
- 不做元素零初始化，trivial 类型不 memset

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 容量 N 不是 2 的幂 | `static_assert` 编译失败 | N 必须为 2, 4, 8, ..., 1024, ... |
| `drain` 的 handler 内 `push` 新事件 | 可能无限循环 | 用 `drain_with_budget` 限制处理数 |
| 依赖 `peek()` 指针在 `pop` 后有效 | `pop` 推进 read_，指针指向数据未定义 | `peek` 后立即处理或先拷贝 |

---

## 26. time — 计时与基准测量

`#include "part/time.hpp"`，全局命名空间。`noexcept`。

计时与基准测量工具：墙钟计时、CPU 周期计数、缓存屏障、统计分布、在线分位数、缓存延迟测量。x86/x64 提供 `rdtsc`/`rdtscp` / `clflush` / `mfence` / `lfence`，其他平台返回 0 或空操作。

### 26.1 timer — 墙钟计时器

| 接口 | 说明 |
|------|------|
| `timer()` | 构造并记录起始时间点 |
| `reset()` | 重置起始时间点 |
| `elapsed_ns()` | 纳秒数 |
| `elapsed_us()` | 微秒数 |
| `elapsed_ms()` | 毫秒数 |
| `elapsed_s()` | 秒数 |

```cpp
timer t;
// ... 执行操作 ...
double ns = t.elapsed_ns();
```

### 26.2 cycle_timer — CPU 周期计时器

| 接口 | 说明 |
|------|------|
| `cycle_timer()` | 构造并记录起始周期 |
| `reset()` | 重置起始周期 |
| `elapsed_cycles()` | CPU 周期数 |
| `elapsed_ns_estimated(cpu_ghz)` | 按 CPU 频率估算纳秒 |

```cpp
cycle_timer ct;
// ... 执行操作 ...
uint64_t cycles = ct.elapsed_cycles();
double ns = ct.elapsed_ns_estimated(3.5);  // 3.5 GHz
```

### 26.3 stats — 统计分布

| 字段 | 说明 |
|------|------|
| `min` / `max` / `mean` / `median` | 基本统计量 |
| `p50` / `p90` / `p95` / `p99` | 百分位 |
| `stddev` | 标准差 |
| `count` | 样本数 |

```cpp
class_pool<double> samples;
samples.emplace_back(1.0);
samples.emplace_back(2.0);
samples.emplace_back(3.0);
stats s = compute_stats(std::move(samples));
// s.mean == 2.0, s.median == 2.0
```

> 内部使用 `tiered_sort` 分级排序：n≤16 排序网络，n<1024 pdqsort，n≥1024 radix sort（O(n)）。

### 26.4 benchmark — 基准测量

| 接口 | 说明 |
|------|------|
| `benchmark_ns(iterations, warmup, fn)` | 纳秒级基准，运行 fn iterations 次 |
| `benchmark_cycles(iterations, warmup, fn)` | 周期级基准，精度更高（无 RDTSC 平台回退到 ns） |
| `benchmark_p2(iterations, warmup, fn)` | 流式基准，P² 在线估计，O(1) 空间，返回 `p2_benchmark_result` |

```cpp
auto s = benchmark_ns(1000, 10, []() {
    // 被测代码
});
// s.p99 为 99 百分位延迟

// 流式基准：不存储样本，适合超大迭代次数
p2_benchmark_result r = benchmark_p2(1000000, 100, []() {
    // 被测代码
});
// r.p50, r.p90, r.p95, r.p99
```

### 26.5 缓存命中测量

| 接口 | 说明 |
|------|------|
| `measure_cache_hits(addresses, thresholds)` | 测量一组地址访问的缓存命中情况 |
| `measure_cache_batch(addresses, repeats)` | 批量测量，取 3 次最优值，扣除基线（适合 L1/L2 精确测量） |
| `measure_loop_cycles(fn)` | 单次 rdtscp 包裹循环，测量总周期 |
| `detect_cache_latency_thresholds()` | 自适应检测缓存层级和阈值（1KB→16MB 步进扫描） |
| `latency_thresholds` | 阈值：l1_max/l2_max/l3_max + `cache_levels`（1/2/3） |
| `cache_report` | 报告：l1/l2/l3 命中数与率、miss 数与率、p50/p95/p99、`active_levels` |
| `batch_cache_result` | 批量结果：总周期、平均/净周期、基线周期 |
| `make_sequential_addresses(base, count, stride)` | 顺序访问地址序列（缓存友好） |
| `make_random_addresses(base, count, stride, seed)` | 随机访问地址序列（缓存不友好，确定性可复现） |

```cpp
class_pool<const void*> addrs;
// 填充地址列表
addrs.emplace_back(&some_data);

cache_report r = measure_cache_hits(addrs);
// r.l1_hit_rate, r.miss_rate, r.avg_cycles 等

// 批量模式（精确 L1/L2 延迟）
batch_cache_result bcr = measure_cache_batch(addrs, 10);
// bcr.net_cycles_per_access 为净每次访问周期

// 自适应检测缓存层级 (不同 CPU 缓存层级不同)
latency_thresholds auto_th = detect_cache_latency_thresholds();
// auto_th.cache_levels == 1/2/3, auto_th.l1_max/l2_max/l3_max 自动标定

// 模拟 1 级缓存场景
latency_thresholds th_l1 = auto_th;
th_l1.cache_levels = 1;
cache_report r_l1 = measure_cache_hits(addrs, th_l1);
// r_l1.active_levels == 1, 仅 L1 命中分类有效
```

> 不同 CPU 缓存层级不同（嵌入式可能仅 1-2 级），默认 `cache_levels=3`。可通过 `detect_cache_latency_thresholds()` 自动检测实际层级，或手动设置 `cache_levels`。

### 26.6 x86 缓存屏障

| 接口 | 说明 |
|------|------|
| `cache_flush(p)` | 刷新单条缓存行（`clflush`） |
| `cache_flush_range(p, bytes)` | 逐缓存行刷新范围 + `mfence` 尾屏障 |
| `mfence()` | 全屏障（Store/Load 序列化） |
| `lfence()` | Load 屏障 |
| `rdtsc_fenced()` | Intel 推荐 `lfence; rdtsc; lfence` 全屏障周期测量 |

```cpp
// 冷缓存测量：先刷出缓存，再测访问延迟
cache_flush_range(&data, sizeof(data));
// 现在 data 不在任何缓存层级中
```

> 非 x86 平台以上均为空操作。

### 26.7 P² 在线分位数

| 接口 | 说明 |
|------|------|
| `p2_quantile(quantile)` | 构造指定分位数估计器（0.0~1.0） |
| `add(x)` | 添加观测值，O(1) |
| `estimate()` | 当前分位数估计值 |
| `count()` | 已观测样本数 |
| `reset()` | 重置估计器 |

```cpp
p2_quantile est(0.99);  // p99 估计器
for (int i = 0; i < 1000000; ++i)
{
    est.add(measure_something());
}
double p99 = est.estimate();
// 无需存储 100 万个样本，内存 O(1)
```

### 26.8 CPU 频率

| 接口 | 说明 |
|------|------|
| `estimate_cpu_ghz(calibration_ms=100)` | 忙等校准估算 TSC 频率（GHz），非核心频率 |
| `cpu_ghz_cached()` | 首次调用校准，后续返回缓存值（零开销） |

```cpp
double ghz = cpu_ghz_cached();
// 首次调用耗时 ~100ms，后续 O(1)
// 注: 测量的是 invariant TSC 频率 (恒定), 非核心频率 (受 Turbo Boost 影响)
//      rdtsc 计数速率 = TSC 频率, 不随核心频率变化
//      cycle_timer::elapsed_ns_estimated() 基于 TSC 频率, 适合相对比较
```

> 若需精确墙钟时间，优先使用 `timer`（`high_resolution_clock`），而非 `cycle_timer` + 频率估算。

### 26.9 延迟异常检测

| 接口 | 说明 |
|------|------|
| `latency_anomaly_detector` | 结构体，内置 p50/p99 P² 估计器 |
| `add(latency_ns)` | 添加延迟样本，建立基线 |
| `is_anomaly(latency_ns)` | 判断当前延迟是否异常（超过 p99 × multiplier） |
| `anomaly_threshold()` | 当前异常阈值（p99 × multiplier） |

```cpp
latency_anomaly_detector detector;
detector.multiplier = 3.0;  // 超过 3x p99 视为异常

for (auto& e : entities)
{
    timer t;
    process(e);
    double ns = t.elapsed_ns();
    detector.add(ns);
    if (detector.is_anomaly(ns))
    {
        // 延迟异常，记录或告警
    }
}
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 非 x86 平台依赖 `rdtsc` 精度 | `TIME_HAS_RDTSC=0`，返回 0 | 非 x86 平台用 `benchmark_ns` 而非 `benchmark_cycles` |
| `compute_stats` 传入空 samples | count=0，所有统计量为 0 | 先检查 samples 非空 |
| `measure_cache_hits` 地址列表含无效指针 | 访问野指针崩溃 | 确保所有地址有效 |
| `cycle_timer` 跨 CPU 频率变化测量 | 频率动态调整导致估算不准 | 短时间测量或锁定频率 |
| 大样本用 `benchmark_ns` 存储全部样本 | 内存占用 O(n) | 大样本（>10M）用 `benchmark_p2`，O(1) 空间 |

---

## 27. entity_mask_manager — 无上限实体掩码存储

`include/part/entity_mask_manager.hpp`，无命名空间。基于 `class_pool<uint64_t>` 的扁平化位掩码存储，每实体 `num_blocks_` 个 `uint64_t`，支持无限组件类型。

### 内存布局

- 实体 `e` 第 `b` 块位置：`e * num_blocks_ + b`
- 默认 `num_blocks_==1`：等价于直接数组访问，零乘法开销
- `reserve_blocks(n)` 在注册组件前预分配 `n` 块，避免后续 reshape 搬移

### 接口

| 接口 | 说明 |
|------|------|
| `reserve_blocks(uint32_t num_blocks)` | 预分配掩码块数（仅扩容，不缩容；注册组件前调用以避免 reshape） |
| `num_blocks() const` | 当前每实体掩码块数 |
| `ensure_entity(uint32_t entity_index)` | 确保实体容量（与 `entity_manager` 同步增长） |
| `set_bit(entity_index, block_idx, bit_offset)` | 设置掩码位（带边界检查，必要时自动扩容） |
| `set_bit_no_check(entity_index, block_idx, bit_offset)` | 设置掩码位（无边界检查，调用方保证 `entity_index` 已分配且 `block_idx < num_blocks_`） |
| `clear_bit(entity_index, block_idx, bit_offset)` | 清除掩码位（带边界检查） |
| `clear_bit_no_check(entity_index, block_idx, bit_offset)` | 清除掩码位（无边界检查） |
| `get_block(entity_index, block_idx) const` | 获取掩码块值（越界返回 0；`get_block(e, 0)` 等价于原 `get_mask`） |
| `clear_entity(entity_index)` | 清零该实体所有掩码块（删除实体时调用） |
| `resize_entities(uint32_t new_count)` | 批量扩容到 `new_count` 个实体（预分配实体时使用） |

### 机制

- **快速路径**：`num_blocks_==1` 时所有方法走无乘法分支，等价 `masks_[entity_index]` 直接访问
- **reshape（从后往前搬移）**：扩容到 `n` 块时，新位置 `e*n+b` 始终 ≥ 旧位置 `e*old+b`，反向遍历保证无覆盖
- **越界保护**：`set_bit` / `clear_bit` 检查 `block_idx < num_blocks_`，越界静默返回；`*_no_check` 版本跳过检查用于热路径

### 使用

```cpp
entity_mask_manager masks;
masks.reserve_blocks(2);            // 预分配 2 块 = 支持 128 种组件
masks.ensure_entity(0);
masks.set_bit(0, 0, 5);             // 实体 0 块 0 位 5
masks.set_bit(0, 1, 10);            // 实体 0 块 1 位 10
uint64_t b0 = masks.get_block(0, 0);
uint64_t b1 = masks.get_block(0, 1);
masks.clear_entity(0);              // 删除实体时清零
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 注册组件后再调 `reserve_blocks` | 触发 reshape 全量搬移 | 启动时预估类型数，注册前调 `reserve_blocks` |
| 对未 `ensure_entity` 的实体调 `set_bit_no_check` | 越界写崩 | 先 `ensure_entity` 或使用带边界检查的 `set_bit` |
| 对 `block_idx >= num_blocks()` 调 `set_bit` | 静默丢弃（不会扩容块数） | 先 `reserve_blocks` 扩容 |
| 假设 `get_block(e, b)` 跨进程稳定 | reshape 改变布局 | 仅当前进程内有效 |