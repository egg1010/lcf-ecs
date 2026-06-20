# lcf-ecs 库完整接口文档

## 目录

1. [entity — 实体](#1-entity--实体)
2. [operating_message — 操作消息](#2-operating_message--操作消息)
3. [class_pool\<T> — 核心容器](#3-class_poolt--核心容器)
4. [void_any — 类型擦除存储](#4-void_any--类型擦除存储)
5. [type_id — 类型ID](#5-type_id--类型id)
6. [id_allocation\<T> — ID分配器](#6-id_allocationt--id分配器)
7. [memory_pool — 内存池](#7-memory_pool--内存池)
8. [single_class_set — 单组件集合](#8-single_class_set--单组件集合)
9. [ecs::manager — ECS管理器](#9-ecsmanager--ecs管理器)
10. [View系统](#10-view系统)
11. [Group系统](#11-group系统)
12. [runtime_view — 运行时视图](#12-runtime_view--运行时视图)
13. [函数存储（回调作为组件）](#13-函数存储回调作为组件)
14. [生命周期信号](#14-生命周期信号)
15. [编译与运行](#15-编译与运行)
16. [可选宏配置](#16-可选宏配置)

---

## 1. entity — 实体

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

### 示例

```cpp
entity e1;                    // 默认构造，无效实体
entity e2(3, 1);              // index=3, version=1
e2.is_valid();                // true
e2.index_;                    // 3
e2.version_;                  // 1

std::unordered_map<entity, int> map;
map[e2] = 42;                 // 可用作哈希键
```

---

## 2. operating_message — 操作消息

记录操作结果（成功/失败）和调试信息。核心特性：

- **粘性 false 语义**：一旦失败就保持 false，只有 `reset()` 能恢复
- **全局开关**：`ecs_debug_messages()` 运行时控制是否写入字符串
- **零开销**：禁用时仅更新 `switch_`，无字符串操作

### 接口

| 接口 | 说明 |
|------|------|
| `ecs_debug_messages()` | 全局开关引用（控制是否写入字符串） |
| `operating_message()` | 默认构造，`switch_=true` |
| `operator bool()` | 是否成功（返回 `switch_`） |
| `reset()` | 重置为成功并清空消息 |
| `clear_message()` | 仅清空消息字符串 |
| `set_switch_bool(bool)` | 直接设置开关值 |
| `get_switch_bool()` | 获取开关引用 |
| `get_switch_bool() const` | 获取开关 const 引用 |
| `write_message(bool sw, Args... args)` | 写入消息（`sw=false` 标记失败，粘性） |
| `write_message_fmt(bool sw, fmt, Args...)` | 格式化写入消息 |
| `read_message()` | 读取消息字符串（返回 `string_view`） |
| `operator+=(string_view)` | 追加字符串到消息 |
| `operator+=(operating_message&&)` | 合并右值消息（`switch_ = switch_ && other.switch_`） |
| `operator+=(const operating_message&)` | 合并左值消息 |
| `operator<<(ostream, operating_message)` | 输出到 `ostream` |
| `operating_message(operating_message&&)` | 移动构造 |
| `operator=(operating_message&&)` | 移动赋值 |
| `operating_message(const operating_message&)` | 拷贝构造 |
| `operator=(const operating_message&)` | 拷贝赋值 |

### 示例

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
```

---

## 3. class_pool\<T> — 核心容器

基于 bitmap 稀疏集的高性能容器，替代 `std::vector`。

**两种模式：**

| 模式 | 触发条件 | 迭代行为 |
|------|---------|---------|
| **dense（密集）** | `usage_` 范围内所有位均为 1（无空洞） | 零开销线性扫描 `[0, usage_)`，无 bitmap 跳转 |
| **sparse（稀疏）** | `usage_` 范围内存在空洞（有未构造的槽位） | 自动跳过未构造槽位，仅遍历已构造元素 |

**模式切换：** 自动判断，无需手动干预。每次修改操作后自动更新 `is_dense_` 缓存（O(usage_/64) bitmap 扫描，仅当模式可能变化时触发）。

**性能特征：**

- 对齐内存（`std::assume_aligned`）
- `count()` 带缓存，避免重复 popcount
- bitmap 操作使用 word 级批量移动
- 禁止异常：分配失败时 `std::terminate()`

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
| `data()` | 原始数据指针（对齐） |
| `span()` | 返回 `std::span<T>` |
| `span() const` | 返回 `std::span<const T>` |

### 容量

| 接口 | 说明 |
|------|------|
| `size()` | 已使用大小 |
| `capacity()` | 总容量 |
| `sparse_capacity()` | 稀疏集容量（同 capacity） |
| `empty()` | 是否为空 |
| `count()` | 已构造元素数（bitmap popcount，带缓存） |
| `valid()` | 是否已分配（`data_ptr_` != null） |
| `size_bytes()` | 已使用字节数 |
| `capacity_bytes()` | 总容量字节数 |

### 修改器

| 接口 | 说明 |
|------|------|
| `emplace_back(Args...)` | 尾部构造元素 |
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
| `resize(size_t)` | 扩容（不填充值） |
| `resize(size_t, const T& value)` | 调整大小并填充值 |
| `swap(other)` | 交换两个容器 |

### 稀疏集/Bitmap

| 接口 | 说明 |
|------|------|
| `is_constructed_at(index)` | 检查指定位置是否已构造 |
| `is_dense()` | 前 `usage_` 位是否全部为 1（O(1) 缓存，每次修改操作自动更新） |
| `recompute_is_dense()` | 强制重新扫描 bitmap 并更新 `is_dense_`（通常无需手动调用） |
| `invalidate_count_cache()` | 使 count 缓存失效 |

### 各操作对 contiguity 的影响

| 操作 | 对 contiguity 的影响 | `is_dense_` 更新 |
|------|---------------------|-----------------|
| `emplace_back()` | 保持连续 | 无需更新 |
| `emplace(pos)` / `insert(pos)` | 保持连续（元素右移） | 无需更新 |
| `erase(pos)` / `erase(first,last)` | 保持连续（元素左移） | 无需更新 |
| `pop_back()` | 保持连续 | 无需更新 |
| `clear()` | 重置为连续 | 直接设为 `true` |
| `sparse_erase_at(index)` | **产生空洞** → 变稀疏 | 直接设为 `false` |
| `emplace_at(index)` | **可能填充空洞** → 可能变连续 | 自动调用 `recompute_is_dense()` |
| `sparse_emplace_at(index)` | **可能填充空洞** → 可能变连续 | 自动调用 `recompute_is_dense()` |
| `resize()` 缩小 | **可能消除空洞** → 可能变连续 | 自动调用 `recompute_is_dense()` |
| `reduce_capacity()` 缩小 | **可能消除空洞** → 可能变连续 | 自动调用 `recompute_is_dense()` |

> **关键规则：** 连续的操作保持连续，产生空洞的操作变稀疏，填满空洞的操作自动切回连续。无需手动管理 `is_dense_`。

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

### 自由函数

| 接口 | 说明 |
|------|------|
| `swap(class_pool&, class_pool&)` | 交换两个容器 |

### 示例

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

// 位置插入
pool.emplace(std::next(pool.begin(), 1), 42);  // 在位置 1 插入

// 迭代
for (auto v : pool) { /* ... */ }

// span
std::span<int> s = pool.span();

// 稀疏集查询
pool.is_constructed_at(100);  // true
pool.is_dense();              // 检查是否连续
```

### 应该用什么操作？

| 场景 | 推荐操作 | 原因 |
|------|---------|------|
| 尾部追加元素 | `emplace_back()` | O(1)，保持连续，最快 |
| 任意位置插入/删除并保持连续 | `emplace(pos)` / `erase(pos)` | 移动后续元素，O(n)，保持连续 |
| 稀疏数组（大索引跳跃） | `emplace_at()` / `sparse_erase_at()` | O(1)，不移动其他元素，但产生空洞 |
| 批量填充已知索引 | `emplace_at()` | 填充空洞后自动切回连续 |
| 删除整个容器 | `clear()` | 重置为连续，O(1) |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `sparse_erase_at()` 后仍期望连续迭代 | 产生空洞，迭代变稀疏模式 | 用 `emplace_at()` 填充空洞，或用 `erase()` 替代 |
| 频繁 `sparse_erase_at()` + `emplace_at()` 来回切换 | 每次切换触发 O(usage_/64) 扫描 | 批量操作，或统一使用 `erase()`/`emplace()` 保持连续 |
| `emplace_at()` 在远超 `usage_` 的索引上构造 | 中间留大量未初始化槽位，`size()` 暴增（`usage_` = index+1） | 用 `resize()` 预填充，或改用 `sparse_emplace_at()` |

---

## 4. void_any — 类型擦除存储

类似 `std::any`，使用 `type_id` 进行类型识别。核心特性：

- **SSO 支持**：小对象内联存储（通过 `VOID_ANY_ENABLE_SSO` 启用）
- **内存池可选**：通过 `VOID_ANY_ENABLE_MEMORY_POOL` 启用
- **三级获取接口**：`get_ptr`（带类型检查）/ `fast_get_ptr`（无 type_id 检查）/ `get_ptr_unchecked`（无检查）

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
| `get_ptr_unchecked<T>()` | 无检查获取（最快，不验证 has_value 和 type_id） |
| `get_ptr_unchecked<T>() const` | const 版本 |
| `get<T>()` | 获取值副本（空值或类型不匹配返回默认构造） |
| `has_value()` | 是否有值 |
| `reset()` | 清空（析构并置空） |

### 示例

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

---

## 5. type_id — 类型ID

为每种类型分配唯一整数 ID（编译时确定，线程安全）。内部使用 `id_allocation<int>` 管理。

### 接口

| 接口 | 说明 |
|------|------|
| `type_id::get_type_id<T>()` | 获取类型 T 的唯一 ID（静态函数） |

### 示例

```cpp
int id1 = type_id::get_type_id<int>();
int id2 = type_id::get_type_id<double>();
// 同类型 ID 相同
assert(type_id::get_type_id<int>() == id1);
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

### 示例

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

### memory_block — 内存块

| 接口 | 说明 |
|------|------|
| `memory_block()` | 默认构造，空块 |
| `memory_block(uint8_t* data, size_t size)` | 指定数据和大小构造 |
| `memory_block(memory_block&&)` | 移动构造 |
| `operator=(memory_block&&)` | 移动赋值 |

> 注：`memory_block` 禁止拷贝。

### memory_pool — 内存池

| 接口 | 说明 |
|------|------|
| `memory_pool(size_t chunk_size = 4096)` | 构造，指定块大小 |
| `memory_pool(memory_pool&&)` | 移动构造 |
| `operator=(memory_pool&&)` | 移动赋值 |
| `allocate(size_t size)` | 分配内存（O(1) TLSF 查找） |
| `deallocate(void* ptr)` | 释放内存（自动合并相邻块） |
| `construct<T>(Args...)` | 分配并构造对象 |
| `destroy<T>(T* ptr)` | 析构并释放对象 |
| `total_allocated()` | 已分配总量 |
| `total_used()` | 已使用量 |
| `chunk_size()` | 获取块大小 |
| `empty()` | 是否空闲（`total_used_ == 0`） |
| `increase_capacity(size_t size)` | 扩容（只扩容不缩容，确保总分配量至少为 size 字节） |
| `reduce_capacity(size_t target)` | 缩容（只缩容不扩容，释放空闲 chunk 直到总量 <= target） |
| `reset()` | 释放所有内存块，回到初始状态 |

> 注：`memory_pool` 禁止拷贝。

### 示例

```cpp
memory_pool pool;
void* p = pool.allocate(64);
pool.deallocate(p);

std::string* s = pool.construct<std::string>("hello");
pool.destroy(s);

pool.increase_capacity(1024 * 1024);  // 扩容至 1MB
pool.reduce_capacity(4096);          // 缩容，释放空闲 chunk 至总量 <= 4096
pool.reset();                        // 释放所有，回到初始状态
```

---

## 8. single_class_set — 单组件集合

管理单一类型组件的稀疏集存储。内部维护 sparse 数组（`sparse_entry`）、dense 数组和类型擦除的组件池。

### sparse_entry — 稀疏条目

| 接口 | 说明 |
|------|------|
| `sparse_entry()` | 默认构造，`dense_index_=0, version_=0` |
| `is_valid()` | 是否有效（`version_ != 0`） |

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
| `get_version(uint32_t entity_index)` | 获取实体版本号 |
| `get_version_unchecked(uint32_t entity_index)` | 无检查获取版本号 |

### 删除与清空

| 接口 | 说明 |
|------|------|
| `hard_remove(entity)` | 硬删除（交换删除，O(1)） |
| `soft_remove(entity)` | 软删除（仅清除 sparse 条目，不移动组件。副作用：typed pool 和 dense 数组留下"空洞"，`size()` 仍包含已删除组件，遍历时通过版本号跳过。若需紧凑布局，使用 `hard_remove`） |
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
| `get_operating_message()` | 获取操作消息引用 |
| `get_entity_indices()` | 获取实体索引数组（dense 数组） |
| `get_entity_indices() const` | const 版本 |
| `get_pool_version()` | 获取组件池版本号（持久化视图自动同步用） |

### 示例

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

---

## 9. ecs::manager — ECS管理器

ECS 核心管理类，管理实体和所有组件集合。内部组成：`entity_manager` 和 `class_pool<single_class_set>`。

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
| `add_batch<T>(span<const entity>, span<const T>)` | 批量添加（span 版本） |
| `add_batch<T>(const class_pool<entity>&, const class_pool<T>&)` | 批量添加（左值引用） |
| `add_batch<T>(class_pool<entity>&&, class_pool<T>&&)` | 批量添加（右值引用） |

### 获取组件

| 接口 | 说明 |
|------|------|
| `get_ptr<T>(entity)` | 获取组件指针（带检查） |
| `get_ptr<T>(entity) const` | const 版本 |
| `get_ptr_fast<T>(entity)` | 快速获取（跳过 type_id 检查） |
| `get_ptr_fast<T>(entity) const` | const 版本 |

### 删除组件

| 接口 | 说明 |
|------|------|
| `soft_remove<T>(entity)` | 软删除组件（仅清除 sparse，留下空洞） |
| `hard_remove<T>(entity)` | 硬删除组件 |
| `soft_removec<T>(entity)` | 链式软删除（返回 `manager&`） |
| `hard_removec<T>(entity)` | 链式硬删除（返回 `manager&`） |
| `delete_type_container<T>()` | 删除整个类型容器 |

### 容器访问

| 接口 | 说明 |
|------|------|
| `get_single_class_set<T>()` | 获取单组件集合指针 |
| `get_single_class_set<T>() const` | const 版本 |
| `get_component_vector<T>()` | 获取类型化组件池指针 |
| `get_component_vector<T>() const` | const 版本 |
| `reserve_component_capacity<T>(capacity)` | 预留组件容量 |
| `get_operating_message()` | 获取操作消息引用 |

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

### Group系统

| 接口 | 说明 |
|------|------|
| `group<First, Rest...>()` | Non-OwningGroup（缓存匹配索引，零分支迭代） |
| `group<First, Rest...>(owned<First>)` | OwningGroup（重排主集，纯线性扫描） |

### runtime_view

| 接口 | 说明 |
|------|------|
| `runtime_view_create({ids...})` | 运行时视图（位掩码匹配，零分支迭代） |
| `runtime_view_create({ids}, {exclude_ids})` | 排除式运行时视图 |
| `get_entity_mask(entity)` | 获取实体组件位掩码 |
| `get_component_bit<T>()` | 获取类型的位掩码位 |

### 生命周期信号

> 详见 [14. 生命周期信号](#14-生命周期信号)

| 接口 | 说明 |
|------|------|
| `set_on_entity_created(fn, data)` | 绑定实体创建即时回调 |
| `set_on_entity_destroyed(fn, data)` | 绑定实体销毁即时回调 |
| `set_on_add<T>(fn, data)` | 绑定组件 T 添加即时回调 |
| `set_on_remove<T>(fn, data)` | 绑定组件 T 移除即时回调 |
| `flush_entity_signals(handler)` | 批量处理实体延迟信号 |
| `has_pending_entity_signals()` | 是否有待处理实体信号 |
| `flush_component_signals(handler)` | 批量处理组件延迟信号 |
| `has_pending_component_signals()` | 是否有待处理组件信号 |

### 综合示例

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

// 获取组件
Position* p = mgr.get_ptr<Position>(e1);

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

// 删除实体
mgr.delete_entity(e2);
```

---

## 10. View系统

提供高效的组件遍历，类似 EnTT。自动选择最小集作为主集迭代，性能最优。

### 10.1 single_view\<T> — 单组件视图

| 接口 | 说明 |
|------|------|
| `size()` | 组件数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否包含指定实体的组件 |
| `for_each(func)` | 遍历组件（自动检测 entity 参数：`func(T&)` 或 `func(entity, T&)`） |
| `begin()` / `end()` | 实体迭代器 |
| `component_begin()` / `component_end()` | 组件迭代器（`T*`） |

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

自动选择最小集作为主集迭代，性能最优。

| 接口 | 说明 |
|------|------|
| `size()` | 主集组件数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否同时拥有所有组件 |
| `for_each(func)` | 遍历多组件（自动检测 entity 参数） |

```cpp
// 双组件
auto v2 = mgr.view<Position, Velocity>();
v2.for_each([](Position& p, Velocity& v) { /* ... */ });
v2.for_each([](entity e, Position& p, Velocity& v) { /* ... */ });

// 三组件
auto v3 = mgr.view<Position, Velocity, Health>();
v3.for_each([](entity e, Position& p, Velocity& v, Health& h) { /* ... */ });

// 四组件
auto v4 = mgr.view<Position, Velocity, Health, Name>();
v4.for_each([](entity e, Position&, Velocity&, Health&, Name& n) { /* ... */ });
```

### 10.3 single_view_without — 排除视图

遍历有 T 但没有 ExcludeTypes 的实体。

| 接口 | 说明 |
|------|------|
| `size()` | 组件数量 |
| `empty()` | 是否为空 |
| `for_each(func)` | 遍历组件（排除指定类型，自动检测 entity 参数） |

```cpp
// 排除单个类型
auto v = mgr.view<Position>(ecs::without<Health>);
v.for_each([](Position& p) { /* 没有 Health 的实体 */ });
v.for_each([](entity e, Position& p) { /* ... */ });

// 排除多个类型
auto v2 = mgr.view<Position>(ecs::without<Health, Name>);
```

### 10.4 single_view_with — 获取视图

遍历 T，同时获取 GetTypes 的指针（可能为 nullptr）。

| 接口 | 说明 |
|------|------|
| `size()` | 组件数量 |
| `empty()` | 是否为空 |
| `for_each(func)` | 遍历组件+可选指针（自动检测 entity 参数） |

```cpp
// 获取单个可选组件
auto v = mgr.view<Position>(ecs::with<Health>);
v.for_each([](Position& p, Health* h) {
    if (h) { /* 有 Health */ }
    else  { /* 无 Health */ }
});
v.for_each([](entity e, Position& p, Health* h) { /* ... */ });

// 获取多个可选组件
auto v2 = mgr.view<Position>(ecs::with<Health, Name>);
v2.for_each([](Position& p, Health* h, Name* n) { /* ... */ });
```

### 10.5 or_view\<A, B> — OR视图（零分配）

遍历拥有 A **或** B 的实体，使用 nullable 指针区分。两阶段遍历，O(N_A + N_B)，零额外内存分配。

| 接口 | 说明 |
|------|------|
| `for_each(func)` | 遍历 A OR B，回调为 `func(entity, A*, B*)` 或 `func(A*, B*)` |

```cpp
// 通过 factory 方法创建
auto ov = mgr.view_or<Position, Velocity>();
ov.for_each([](entity e, Position* p, Velocity* v) {
    if (p && v) { /* 同时拥有 Position 和 Velocity */ }
    else if (p) { /* 仅拥有 Position */ }
    else if (v) { /* 仅拥有 Velocity */ }
});
```

### 10.6 filter_view\<T, Pred> — 谓词过滤视图

预过滤满足谓词的组件，通过 `class_pool<size_t>` 存储 dense 索引实现高效迭代。构造时自动过滤，也可手动 `rebuild()` 触发重新过滤。

| 接口 | 说明 |
|------|------|
| `size()` | 过滤后组件数量 |
| `empty()` | 是否为空 |
| `rebuild()` | 重新执行过滤 |
| `for_each(func)` | 遍历过滤后的组件，回调为 `func(entity, T&)` 或 `func(T&)` |
| `and_<B>()` | 链式调用：在过滤结果上追加 AND 组件 B |
| `or_<B>()` | 链式调用：在过滤结果上追加 OR 组件 B |

```cpp
// 过滤 Position.x > 1
auto fv = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; });
fv.for_each([](Position& p) { /* 仅处理 x > 1 的实体 */ });

// 手动重新过滤（组件数据变化后）
fv.rebuild();
```

### 10.7 filter_and_view — 过滤+AND组合视图

通过 `filter_view::and_<B>()` 链式创建。遍历满足谓词 **且** 同时拥有组件 B 的实体。

```cpp
// Position.x > 1 AND 同时拥有 Health
auto fav = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; })
               .and_<Health>();
fav.for_each([](entity e, Position& p, Health& h) {
    // 仅处理 x > 1 且拥有 Health 的实体
});
```

### 10.8 filter_or_view — 过滤+OR组合视图

通过 `filter_view::or_<B>()` 链式创建。遍历满足谓词 **或** 拥有组件 B 的实体，使用 nullable 指针区分。

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

### 10.9 性能对比

> 测试环境：AMD Ryzen 9700X / DDR5 6000MHz CL28

| 场景 | 当前做法 | 升级后 | 分配 |
|------|----------|--------|------|
| A OR B (各 10K) | 两次 view + 手动去重 | 一次 `or_<B>` 遍历 | 0 |
| 过滤 90% 实体 | 全量遍历 + lambda if | `filter` 预扫 + 只遍历 10% | 1 次 `class_pool` |
| A AND B 且过滤 A | 全量遍历 + 双重 if | `filter<A>` + `and_<B>` | 1 次 `class_pool` |

---
## 11. Group系统

Group 在构造时预先计算匹配实体集，迭代时零分支、零版本检查。

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

**示例：**

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

通过 `mgr.group<First, Rest...>(ecs::owned<First>)` 创建，重排主集 `First` 的 dense 数组，使匹配实体在数组前部连续排列。迭代时纯线性扫描 `[0, owned_size_)`。

**注意：** `owned` 标签标记的组件类型会被重排，组件数据顺序会改变。如果其他代码依赖该组件的 dense 顺序，需谨慎使用。

**接口：** 与 Non-OwningGroup 相同。

**示例：**

```cpp
// OwningGroup: Position 被重排
auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);
og.for_each([](Position& p, Velocity& v) {
    // 纯线性扫描，性能最高
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

### 11.3 性能对比

> 测试环境：AMD Ryzen 9700X / DDR5 6000MHz CL28

| 场景 | `multi_view` | `group` (Non-Owning) | `group` + `owned` (Owning) |
|------|-------------|---------------------|---------------------------|
| 每次迭代 | 检查所有组件是否存在 | 零检查（预缓存） | 零检查（连续排列） |
| 分支预测 | 依赖数据分布 | 无分支 | 无分支 |
| 内存访问 | sparse → dense 间接 | 缓存索引直接访问 | 纯线性扫描 |
| 额外内存 | 0 | `class_pool<size_t>` (N × 8 bytes) | 0 |
| 数据重排 | 无 | 无 | 有（主集 dense 重排） |

---

## 12. runtime_view — 运行时视图

在运行时动态指定组件类型组合进行查询。通过实体组件位掩码（`uint64_t`）实现匹配。

### 12.1 实体掩码

每个实体在添加/删除组件时自动维护组件位掩码：

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
| `for_each(func)` | 遍历所有匹配实体，回调接收 `entity` |
| `size()` | 返回主集大小（上限，非精确匹配数） |
| `empty()` | 是否为空 |
| `contains(entity)` | 检查实体是否匹配查询 |
| `get_ptr<T>(entity)` | 获取实体的组件指针 |
| `rebuild()` | 重新选择最小集合（组件数量变化后调用） |
| `get_entity_mask(entity)` | 获取实体组件位掩码 |
| `get_component_bit<T>()` | 获取组件类型的位掩码位 |

### 12.5 性能对比

> 测试环境：AMD Ryzen 9700X / DDR5 6000MHz CL28

| 视图 | 50万实体迭代 | 匹配方式 |
|------|------------|---------|
| `multi_view<Pos+Vel>` (编译期) | 0.58ms | 多次 get_ptr_fast |
| `group<Pos,Vel>(owned)` (Owning) | 1.33ms | 连续内存扫描 |
| **`runtime_view<Pos+Vel>` (运行时)** | **2.14ms** | **单条 AND 指令** |
| `group<Pos,Vel>` (Non-Owning) | 4.18ms | 缓存索引遍历 |

---

## 13. 函数存储（回调作为组件）

将函数/回调封装为组件，通过 `ecs::manager` 的标准组件接口存储与调用。

### 用法

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

两层架构：**即时信号**（函数指针回调） + **延迟信号**（环形缓冲区，批量处理）。

### 14.1 实体级即时信号

实体创建/销毁时立即触发回调。通过 `void* user_data` 传递上下文，兼容 C 风格函数指针和无捕获 lambda。

**接口：**

| 接口 | 说明 |
|------|------|
| `set_on_entity_created(fn, user_data)` | 绑定实体创建回调：`void fn(entity, void* user_data)` |
| `set_on_entity_destroyed(fn, user_data)` | 绑定实体销毁回调：`void fn(entity, void* user_data)` |

**示例：**

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

> **注意：** 回调必须是 `void (*)(entity, void*) noexcept` 签名。使用 `+` 将无捕获 lambda 转为函数指针。需要上下文时通过 `user_data` 传递。

### 14.2 组件级即时信号

组件添加/移除时立即触发回调。回调接收实体、组件指针和 `user_data`，可在回调中直接修改组件数据。

**接口：**

| 接口 | 说明 |
|------|------|
| `set_on_add<T>(fn, user_data)` | 绑定组件 T 添加回调：`void fn(entity, void* component, void* user_data)` |
| `set_on_remove<T>(fn, user_data)` | 绑定组件 T 移除回调：`void fn(entity, void* component, void* user_data)` |

**示例：**

```cpp
ecs::manager mgr;
mgr.append_preallocated_entities(100);

int add_count = 0, remove_count = 0;

auto on_add = [](entity e, void* comp, void* data) noexcept {
    (*static_cast<int*>(data)) += 1;
    // comp 指向组件数据，可在回调中修改
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
mgr.add(e, Position{3, 4});        // add_count == 2（覆盖也会触发）
mgr.hard_remove<Position>(e);      // remove_count == 1
```

> **注意：** 组件指针在回调期间有效，可用于读取或修改组件数据。`soft_remove` 和 `hard_remove` 均会触发 `on_remove`。

### 14.3 实体级延迟信号

实体创建/销毁事件被推入环形缓冲区，调用 `flush_entity_signals` 时批量处理。适合批量同步、避免重入的场景。

**接口：**

| 接口 | 说明 |
|------|------|
| `flush_entity_signals(handler)` | 批量处理所有待处理实体信号：`handler(uint32_t type, uint32_t entity_idx)` |
| `has_pending_entity_signals()` | 是否有待处理实体信号 |

**信号类型：**
- `type=0`：实体创建
- `type=1`：实体销毁

**示例：**

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
// 缓冲区已清空
assert(!mgr.has_pending_entity_signals());
```

> **缓冲区容量：** 256 条。缓冲区满时丢弃新事件（生产环境可改为先 flush 再插入）。

### 14.4 组件级延迟信号

组件添加/移除事件被推入环形缓冲区，调用 `flush_component_signals` 时批量处理。

**接口：**

| 接口 | 说明 |
|------|------|
| `flush_component_signals(handler)` | 批量处理所有待处理组件信号：`handler(uint32_t type, uint32_t entity_idx, uint32_t component_id)` |
| `has_pending_component_signals()` | 是否有待处理组件信号 |

**信号类型：**
- `type=0`：组件添加
- `type=1`：组件移除

**示例：**

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

### 14.5 即时信号 vs 延迟信号 选择指南

| 场景 | 推荐方案 | 原因 |
|------|----------|------|
| 日志/调试 | 即时信号 | 需要立即输出，延迟无意义 |
| 数据校验 | 即时信号 | 需要在组件写入同时验证 |
| 网络同步 | 延迟信号 | 批量发送，减少网络开销 |
| 物理/渲染回调 | 延迟信号 | 避免在 ECS 操作中途触发重入 |
| 文件持久化 | 延迟信号 | 批量写入，减少 I/O |
| 第三方集成 | 延迟信号 | 解耦 ECS 内部状态与外部系统 |

### 14.6 性能特征

| 操作 | 开销 |
|------|------|
| 即时信号（无订阅者） | 零开销（`if (fn) [[unlikely]]` 分支预测跳转） |
| 即时信号（有订阅者） | 1 次函数指针调用 + `void*` 解引用 |
| 延迟信号推送 | 2 次 `uint32_t` 写入 + 1 次取模运算 |
| 延迟信号 flush | 遍历环形缓冲区，每个事件 1 次回调 |

---

## 15. 编译与运行

### CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### 运行示例

```bash
./build/usagec.exe    # 完整接口示例
./build/test.exe      # 测试与性能基准
```

### 编译要求

- C++20（需支持 `std::format`）
- CMake 3.16+

---

## 16. 可选宏配置

在 `void_any_config.hpp` 中配置，影响 `void_any` 的存储策略。

| 宏 | 说明 |
|------|------|
| `VOID_ANY_ENABLE_SSO` | 启用 void_any 小对象优化（SSO），小对象内联存储 |
| `VOID_ANY_ENABLE_MEMORY_POOL` | 启用 void_any 内存池，使用 `memory_pool` 替代 `::operator new` |
| `VOID_ANY_SSO_BUFFER_SIZE` | SSO 缓冲区大小（默认 32 字节），仅在启用 SSO 时有效 |

### 配置示例

```cpp
// void_any_config.hpp

// 启用内存池
#define VOID_ANY_ENABLE_MEMORY_POOL

// 启用小对象优化
#define VOID_ANY_ENABLE_SSO

// SSO 缓冲区大小
#define VOID_ANY_SSO_BUFFER_SIZE 64
```