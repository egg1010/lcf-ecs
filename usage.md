# lcf-ecs 库接口文档

包含 `component.hpp` 即可使用。

## 目录

- [lcf-ecs 库接口文档](#lcf-ecs-库接口文档)
  - [目录](#目录)
  - [1. entity — 实体](#1-entity--实体)
    - [接口](#接口)
    - [使用](#使用)
    - [不要做什么](#不要做什么)
  - [2. operating\_message — 操作消息](#2-operating_message--操作消息)
    - [接口](#接口-1)
    - [使用](#使用-1)
    - [不要做什么](#不要做什么-1)
  - [3. class\_pool\<T\> — 核心容器](#3-class_poolt--核心容器)
    - [构造与赋值](#构造与赋值)
    - [元素访问](#元素访问)
    - [容量](#容量)
    - [修改器](#修改器)
    - [稀疏集/Bitmap](#稀疏集bitmap)
    - [各操作对 contiguity 的影响](#各操作对-contiguity-的影响)
    - [插入/删除](#插入删除)
    - [迭代器](#迭代器)
    - [自由函数](#自由函数)
    - [使用](#使用-2)
    - [应该用什么操作？](#应该用什么操作)
    - [不要做什么](#不要做什么-2)
  - [4. void\_any — 类型擦除存储](#4-void_any--类型擦除存储)
    - [构造与赋值](#构造与赋值-1)
    - [访问与操作](#访问与操作)
    - [使用](#使用-3)
    - [不要做什么](#不要做什么-3)
  - [5. type\_id — 类型ID](#5-type_id--类型id)
    - [接口](#接口-2)
    - [使用](#使用-4)
  - [6. id\_allocation\<T\> — ID分配器](#6-id_allocationt--id分配器)
    - [接口](#接口-3)
    - [使用](#使用-5)
  - [7. memory\_pool — 内存池](#7-memory_pool--内存池)
    - [memory\_block — 内存块](#memory_block--内存块)
    - [memory\_pool — 内存池](#memory_pool--内存池)
    - [使用](#使用-6)
    - [不要做什么](#不要做什么-4)
  - [8. single\_class\_set — 单组件集合](#8-single_class_set--单组件集合)
    - [sparse 访问](#sparse-访问)
    - [构造与赋值](#构造与赋值-2)
    - [添加组件](#添加组件)
    - [获取组件](#获取组件)
    - [删除与清空](#删除与清空)
    - [容量与查询](#容量与查询)
    - [使用](#使用-7)
    - [不要做什么](#不要做什么-5)
  - [9. ecs::manager — ECS管理器](#9-ecsmanager--ecs管理器)
    - [实体管理](#实体管理)
    - [添加组件](#添加组件-1)
    - [获取组件](#获取组件-1)
    - [删除组件](#删除组件)
    - [容器访问](#容器访问)
    - [性能开关](#性能开关)
    - [View系统](#view系统)
    - [Group系统](#group系统)
    - [runtime\_view](#runtime_view)
    - [生命周期信号](#生命周期信号)
    - [使用](#使用-8)
    - [不要做什么](#不要做什么-6)
  - [10. View系统](#10-view系统)
    - [10.1 single\_view\<T\> — 单组件视图](#101-single_viewt--单组件视图)
    - [10.2 multi\_view\<T1, T2, ...\> — 多组件视图](#102-multi_viewt1-t2---多组件视图)
    - [10.3 single\_view\_without — 排除视图](#103-single_view_without--排除视图)
    - [10.4 single\_view\_with — 获取视图](#104-single_view_with--获取视图)
    - [10.5 or\_view\<A, B\> — OR视图（零分配）](#105-or_viewa-b--or视图零分配)
    - [10.6 filter\_view\<T, Pred\> — 谓词过滤视图](#106-filter_viewt-pred--谓词过滤视图)
    - [10.7 filter\_and\_view — 过滤+AND组合视图](#107-filter_and_view--过滤and组合视图)
    - [10.8 filter\_or\_view — 过滤+OR组合视图](#108-filter_or_view--过滤or组合视图)
    - [10.9 sort\_entities\_by\_component / reorder\_by\_component — 排序工具](#109-sort_entities_by_component--reorder_by_component--排序工具)
    - [10.10 page — 分页视图](#1010-page--分页视图)
    - [10.11 sorted\_by\_component — 排序视图](#1011-sorted_by_component--排序视图)
    - [10.12 sorted\_by\_component\_value — 分组视图](#1012-sorted_by_component_value--分组视图)
    - [10.13 track\_changes — 变更检测视图](#1013-track_changes--变更检测视图)
    - [10.14 链式组合](#1014-链式组合)
    - [10.15 filter\_changed — 逐实体变更检测](#1015-filter_changed--逐实体变更检测)
    - [10.16 filter\_added — 逐实体添加检测](#1016-filter_added--逐实体添加检测)
    - [10.17 view\_any\_of — N元OR视图](#1017-view_any_of--n元or视图)
    - [10.18 exactly\_one — 精确获取单个实体](#1018-exactly_one--精确获取单个实体)
    - [10.19 find\_one — 查询指定实体](#1019-find_one--查询指定实体)
    - [10.20 iter\_over\_entities — 批量指定实体查询](#1020-iter_over_entities--批量指定实体查询)
    - [View 不要做什么](#view-不要做什么)
  - [11. Group系统](#11-group系统)
    - [11.1 Non-OwningGroup (`group`)](#111-non-owninggroup-group)
    - [11.2 OwningGroup (`group` + `owned`)](#112-owninggroup-group--owned)
    - [11.3 ReorderGroup (`group` + `reorder`)](#113-reordergroup-group--reorder)
    - [Group 不要做什么](#group-不要做什么)
  - [12. runtime\_view — 运行时视图](#12-runtime_view--运行时视图)
    - [12.1 实体掩码](#121-实体掩码)
    - [12.2 运行时视图](#122-运行时视图)
    - [12.3 排除视图](#123-排除视图)
    - [12.4 接口](#124-接口)
    - [不要做什么](#不要做什么-7)
  - [13. 函数存储（回调作为组件）](#13-函数存储回调作为组件)
    - [使用](#使用-9)
    - [通过 View 批量调用](#通过-view-批量调用)
  - [14. 生命周期信号](#14-生命周期信号)
    - [14.1 实体级即时信号](#141-实体级即时信号)
    - [14.2 组件级即时信号](#142-组件级即时信号)
    - [14.3 实体级延迟信号](#143-实体级延迟信号)
    - [14.4 组件级延迟信号](#144-组件级延迟信号)
    - [14.5 即时信号 vs 延迟信号 选择指南](#145-即时信号-vs-延迟信号-选择指南)
    - [不要做什么](#不要做什么-8)
  - [15. 编译与运行](#15-编译与运行)
    - [CMake](#cmake)
    - [运行示例](#运行示例)
    - [编译要求](#编译要求)
  - [16. 可选宏配置](#16-可选宏配置)
    - [配置示例](#配置示例)

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

- **粘性 false 语义**：一旦失败就保持 false，只有 `reset()` 能恢复
- **全局开关**：`ecs_debug_messages()` 运行时控制是否写入字符串

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
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 依赖 `write_message(true)` 恢复失败状态 | 粘性 false 语义，成功后不会恢复 | 调用 `reset()` 显式恢复 |
| 在 Release 构建中依赖 `read_message()` | 全局开关关闭时字符串为空 | 使用 `operator bool()` 判断成败，而非消息内容 |
| 忘记检查 `operator bool()` | 操作失败被静默忽略 | 每次关键操作后检查 `if (!msg) { ... }` |

---

## 3. class_pool\<T> — 核心容器

基于 bitmap 稀疏集的高性能容器，替代 `std::vector`。

**两种模式：**

| 模式 | 触发条件 | 迭代行为 |
|------|---------|---------|
| **dense（密集）** | `usage_` 范围内所有位均为 1（无空洞） | 线性扫描 `[0, usage_)`，无 bitmap 跳转 |
| **sparse（稀疏）** | `usage_` 范围内存在空洞（有未构造的槽位） | 自动跳过未构造槽位，仅遍历已构造元素 |

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
| `push_back_unchecked(const T&)` | 尾部追加（不设 bitmap 位，不更新 count 缓存，内部批量操作用） |

### 稀疏集/Bitmap

| 接口 | 说明 |
|------|------|
| `is_constructed_at(index)` | 检查指定位置是否已构造 |
| `is_dense()` | 前 `usage_` 位是否全部为 1（O(1) 缓存） |
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
| `emplace_at()` 在远超 `usage_` 的索引上构造 | 中间留大量未初始化槽位，`size()` 暴增 | 用 `resize()` 预填充，或改用 `sparse_emplace_at()` |
| 在 sparse 模式下使用 `data()` + `span()` 做线性遍历 | 未初始化槽位包含垃圾数据 | 始终通过迭代器遍历，或先确认 `is_dense()` 为 true |
| `emplace_at()` 期望覆盖已有值 | `emplace_at` 是 get-or-create，不覆盖 | 使用 `sparse_emplace_at()` 实现 insert-or-assign |

---

## 4. void_any — 类型擦除存储

使用 `type_id` 进行类型识别。支持 SSO 和内存池（通过宏配置）。

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

### memory_pool — 内存池

| 接口 | 说明 |
|------|------|
| `memory_pool(size_t chunk_size = 4096)` | 构造，指定块大小 |
| `memory_pool(memory_pool&&)` | 移动构造 |
| `operator=(memory_pool&&)` | 移动赋值 |
| `allocate(size_t size)` | 分配内存 |
| `deallocate(void* ptr)` | 释放内存（自动合并相邻块） |
| `construct<T>(Args...)` | 分配并构造对象 |
| `destroy<T>(T* ptr)` | 析构并释放对象 |
| `total_allocated()` | 已分配总量 |
| `total_used()` | 已使用量 |
| `chunk_size()` | 获取块大小 |
| `empty()` | 是否空闲（`total_used_ == 0`） |
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
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用 `new`/`delete` 管理 `construct` 分配的对象 | 内存池有自己的分配器，`delete` 会崩溃 | 始终用 `destroy<T>()` 释放 |
| `allocate` 后忘记 `deallocate` | 内存泄漏 | 每次 `allocate` 配对一个 `deallocate` |
| 拷贝 `memory_pool` | 禁止拷贝，内部指针所有权混乱 | 使用移动语义或引用传递 |

---

## 8. single_class_set — 单组件集合

管理单一类型组件的稀疏集存储。内部维护 sparse_version_、sparse_dense_ 数组和类型擦除的组件池。

### sparse 访问

| 接口 | 说明 |
|------|------|
| `get_sparse_combined()` | 获取合并的稀疏数组（[31:0]=version, [63:32]=dense） |

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
| `get_ptr_batch(const entity*, T**, size_t)` | 批量查询组件指针（管线化预取） |

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
| `get_operating_message()` | 获取操作消息引用 |
| `get_entity_indices()` | 获取实体索引数组（dense 数组） |
| `get_entity_indices() const` | const 版本 |
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
| `get_ptr_batch<T>(entities, results, count)` | 批量查询组件指针（管线化预取） |
| `prefetch_ptr<T>(entity)` | 预取实体 sparse 条目 |
| `prefetch_ptr_batch<T>(entities, count)` | 批量预取实体 sparse 条目 |

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
| `get_component_meta(int type_id)` | 获取组件元数据（含 bit 位等信息） |
| `get_single_class_set_by_id(int type_id)` | 通过 type_id 获取组件集合（运行时视图用） |

### 性能开关

| 接口 | 说明 |
|------|------|
| `disable_comp_signals()` | 禁用组件信号推送（高频 add 场景优化） |
| `enable_comp_signals()` | 启用组件信号推送 |
| `disable_track_changes()` | 禁用版本追踪（高频 add 场景优化） |
| `enable_track_changes()` | 启用版本追踪 |

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

### Group系统

| 接口 | 说明 |
|------|------|
| `group<First, Rest...>()` | Non-OwningGroup（缓存匹配索引） |
| `group<First, Rest...>(owned<First>)` | OwningGroup（重排主集，线性扫描） |

### runtime_view

| 接口 | 说明 |
|------|------|
| `runtime_view_create({ids...})` | 运行时视图（位掩码匹配） |
| `runtime_view_create({ids}, {exclude_ids})` | 排除式运行时视图 |
| `get_entity_mask(entity)` | 获取实体组件位掩码 |
| `get_component_bit<T>()` | 获取类型的位掩码位 |

### 生命周期信号

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

// 获取组件
Position* p = mgr.get_ptr<Position>(e1);

// 批量查询组件（管线化预取，性能优于逐个 get_ptr）
class_pool<Position*> results;
results.resize(entities.size());
mgr.get_ptr_batch<Position>(entities.data(), results.data(), entities.size());

// 预取组件指针（搭配 get_ptr 管线化使用）
mgr.prefetch_ptr<Position>(e1);
mgr.prefetch_ptr_batch<Position>(entities.data(), entities.size());

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
| `sort_component_container<T>(cmp)` | 仅排序组件池数据，不重排 dense 和 sparse（适用于临时排序场景） |

```cpp
// 按 Position.x 升序排序
mgr.sort_entities_by_component<Position>([](Position& a, Position& b) {
    return a.x < b.x;
});

// 按 Velocity.dx 降序重排 Position
mgr.reorder_by_component<Position, Velocity>([](Velocity& a, Velocity& b) {
    return a.dx > b.dx;
});

// 仅排序组件池（不更新 dense/sparse，实体顺序不变）
mgr.sort_component_container<Position>([](Position& a, Position& b) {
    return a.x < b.x;
});
```

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

通过 `sorted_by_component<T>(cmp)` 链式调用，按指定组件值临时排序查询结果。排序在首次 `for_each` 时执行（懒构建），通过版本号检测变更自动重建缓存。适用于 `single_view` 和 `multi_view`。

| 接口 | 说明 |
|------|------|
| `size()` | 排序后数量 |
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
| 在多组件 View 中混用 `get_ptr_fast` 和 `get_ptr` | 性能不一致且类型安全边界模糊 | 同一 View 中统一使用一种获取方式 |

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

通过 `mgr.group<First, Rest...>(ecs::reorder<First>)` 创建，与 OwningGroup 同样重排主集，但语义更轻——仅表达"允许重排以换取性能"，不暗示生命周期所有权。

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
| `get_first_entity()` | 返回第一个匹配实体 |
| `rebuild()` | 重新选择最小集合（组件数量变化后调用） |
| `get_entity_mask(entity)` | 获取实体组件位掩码 |
| `get_component_bit<T>()` | 获取组件类型的位掩码位 |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `size()` 依赖精确值 | 返回的是主集大小上限，非精确匹配数 | 使用 `for_each` 遍历或 `contains()` 检查 |
| 忘记自增 `entity_bit_` 计数器 | 位掩码位耗尽（uint64_t 最多 64 种组件类型） | 控制组件类型数量在 64 以内 |
| 组件数量变化后忘记 `rebuild()` | 主集选择可能不是最优 | 增删组件类型后调用 `rebuild()` |

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

两层架构：**即时信号**（函数指针回调） + **延迟信号**（环形缓冲区，批量处理）。

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

> **缓冲区容量：** 256 条。缓冲区满时丢弃新事件。

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

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 在即时信号回调中增删实体/组件 | 可能导致重入和迭代器失效 | 使用延迟信号，在 flush 时处理 |
| 依赖延迟信号缓冲区不丢事件 | 缓冲区满时丢弃新事件 | 定期 flush，或降低事件产生频率 |
| 忘记 `flush` 延迟信号 | 事件堆积在缓冲区中未处理 | 每帧开头或结尾调用 `flush_*_signals` |
| 使用有捕获的 lambda 作为即时信号回调 | 无法转换为函数指针 | 使用无捕获 lambda + `user_data` 传上下文 |

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
./build/test.exe      # 测试
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
| `VOID_ANY_MEMORY_POOL_NOT_ENABLED` | 禁用内存池（与 `VOID_ANY_ENABLE_MEMORY_POOL` 互斥） |
| `VOID_ANY_SSO_NOT_ENABLED` | 禁用 SSO（与 `VOID_ANY_ENABLE_SSO` 互斥） |

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