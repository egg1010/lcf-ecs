# lcf-ecs 库接口文档

包含 `component.hpp` 即可使用。

## 目录

- [lcf-ecs 库接口文档](#lcf-ecs-库接口文档)
  - [目录](#目录)
  - [一、库使用接口](#一库使用接口)
    - [1. ecs::entity — 实体](#1-ecsentity--实体)
    - [2. view_tags — 视图标签类型](#2-view_tags--视图标签类型)
    - [3. ecs::single_class_set — 单组件集合](#3-ecssingle_class_set--单组件集合)
    - [4. ecs::manager — ECS管理器](#4-ecsmanager--ecs管理器)
    - [5. View系统](#5-view系统)
    - [6. Group系统](#6-group系统)
    - [7. runtime_view — 运行时视图](#7-runtime_view--运行时视图)
    - [8. 函数存储（回调作为组件）](#8-函数存储回调作为组件)
    - [9. 生命周期信号](#9-生命周期信号)
    - [10. command_buffer — 延迟结构变更](#10-command_buffer--延迟结构变更)
    - [11. reflection — 反射模块使用](#11-reflection--反射模块使用)
    - [12. serialization — 序列化/反序列化](#12-serialization--序列化反序列化)
  - [二、宏配置](#二宏配置)
    - [13. 可选宏配置](#13-可选宏配置)
  - [三、各种模块](#三各种模块)
    - [14. operating_message — 操作消息](#14-operating_message--操作消息)
    - [15. class_pool<T> — 核心容器](#15-class_poolt--核心容器)
    - [15.5. class_pool 视图](#155-class_pool-视图)
    - [16. void_any — 类型擦除存储](#16-void_any--类型擦除存储)
    - [17. type_id — 类型ID](#17-type_id--类型id)
    - [18. id_allocation<T> — ID分配器](#18-id_allocationt--id分配器)
    - [19. memory_pool — 内存池](#19-memory_pool--内存池)
    - [20. dense<T> — 通用密集容器](#20-denset--通用密集容器)
    - [21. tiered_sort / pdqsort / sort_n — 分级排序](#21-tiered_sort--pdqsort--sort_n--分级排序)
    - [22. radix_sort — 基数排序](#22-radix_sort--基数排序)
    - [23. FORCE_INLINE / NOINLINE — 跨平台内联宏](#23-force_inline--noinline--跨平台内联宏)
    - [24. arena_allocator — 线性 bump 分配器](#24-arena_allocator--线性-bump-分配器)
    - [25. slab_allocator — 固定块分配器](#25-slab_allocator--固定块分配器)
    - [26. layered_allocator — 分层分配器](#26-layered_allocator--分层分配器)
    - [27. ring_buffer — 环形缓冲区](#27-ring_buffer--环形缓冲区)
    - [28. time — 计时与基准测量](#28-time--计时与基准测量)
    - [29. multi_block_bitmask — 多块位掩码存储](#29-multi_block_bitmask--多块位掩码存储)
    - [30. reflection — 反射元数据与存储](#30-reflection--反射元数据与存储)
    - [31. aggregate_reflect — 聚合类型字段遍历](#31-aggregate_reflect--聚合类型字段遍历)
    - [32. type_erasure — 类型擦除方法调用器](#32-type_erasure--类型擦除方法调用器)
    - [33. member_offset — 成员偏移量访问](#33-member_offset--成员偏移量访问)
    - [34. t_fun — 函数类型延迟调用器](#34-t_fun--函数类型延迟调用器)
    - [35. utf8_codec / utf8pp — UTF-8 编解码与拥有型字符串](#35-utf8_codec--utf8pp--utf-8-编解码与拥有型字符串)
    - [36. utf8_view — UTF-8 字符串视图](#36-utf8_view--utf-8-字符串视图)

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

## 2. view_tags — 视图标签类型

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

## 3. ecs::single_class_set — 单组件集合

管理单一类型组件的存储。内部使用 `class_pool<sparse_entry>` 稀疏表 + 热集缓存，替代传统的连续稀疏数组。

### sparse 访问

| 接口 | 说明 |
|------|------|
| `sparse_dense_at_public(uint32_t idx)` | 获取稀疏条目的 dense 索引，不存在返回 `dense_invalid` (0xFFFFFFFF) |
| `sparse_version_at_public(uint32_t idx)` | 获取稀疏条目的 version，不存在返回 0 |
| `dense_invalid` | 无效 dense 索引常量（0xFFFFFFFF） |
| `get_sparse_size()` | 稀疏表已使用的最大索引+1 |
| `clear_hot_set()` | 清空热集缓存（调试用，正常使用无需手动调用） |
| `bump_pool_version()` | 递增池版本号，使所有热集缓存自动失效 |

机制：

- `sparse_dense_at_public` / `sparse_version_at_public`：基于 `class_pool<sparse_entry>` 的稀疏表查找，sparse_entry 合并存储 `{dense, version}`
- 未映射条目：dense 返回 `dense_invalid`，version 返回 0
- 热集缓存用于加速频繁查询，`pool_version` 变化时自动失效
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
| `add/remove` 系列接口返回值 | 返回 `operating_message`（值类型） |
| `get_entity_indices()` | 获取实体索引数组（dense 数组） |
| `get_entity_indices() const` | const 版本 |
| `get_entity_versions()` | 获取实体版本号数组 |
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

## 4. ecs::manager — ECS管理器

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
| `prefetch_sparse(entity) const` | 预取 sparse 条目 |
| `prefetch_data(entity) const` | 预取组件数据 |
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
| `add/add_batch/hard_remove/soft_remove` | 返回 `operating_message`（值类型） |
| `get_component_meta(int type_id)` | 获取组件元数据（含 `mask_block`/`mask_offset` 掩码位信息） |
| `get_single_class_set_by_id(int type_id)` | 通过 type_id 获取组件集合（运行时视图用） |
| `get_entity_manager()` | 获取 `entity_manager&` 引用（暴露掩码 / 状态 / 标志等底层接口） |
| `get_entity_manager() const` | const 版本 |

### single_class_set 稀疏表接口

稀疏表使用 `class_pool<sparse_entry>` 存储 `entity_index → {dense_index, version}` 映射。`sparse_entry` 合并存储 dense 索引与 version。

| 接口 | 说明 |
|------|------|
| `sparse_dense_at_public(uint32_t idx) const` | 读取稀疏条目的 dense 索引，不存在返回 `dense_invalid` |
| `sparse_version_at_public(uint32_t idx) const` | 读取稀疏条目的 version，不存在返回 0 |
| `dense_invalid` | 无效 dense 索引常量（`0xFFFFFFFFu`），public 静态成员 |
| `get_sparse_size() const` | 稀疏表已覆盖的最大索引+1 |
| `clear_hot_set()` | 清空热集缓存（调试用，正常使用无需手动调用） |
| `bump_pool_version()` | 递增池版本号，使所有热集缓存自动失效 |

```cpp
auto* set = mgr.get_single_class_set<Position>();

// 查询实体的 dense 索引和 version
uint32_t dense = set->sparse_dense_at_public(entity_index);
uint32_t ver   = set->sparse_version_at_public(entity_index);

if (dense == single_class_set::dense_invalid)
{
    // 实体未注册该组件
}

// 清空热集缓存
set->clear_hot_set();
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
| `group<First, Rest...>(reorder<First>)` | ReorderGroup（重排主集，允许共享重排状态） |

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

基于 `multi_block_bitmask` 的动态位掩码存储，无组件类型上限。通过 `reserve_blocks(n)` 预分配掩码块数。

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
| `reserve_mask_blocks(uint32_t num_blocks)` | 预分配每实体掩码块数（每块 64 种组件；注册组件前调用；`register_component_meta` 在 type_id 超出时自动扩容） |
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

// 启动时预分配 2 块掩码（支持 128 种组件）
mgr.reserve_mask_blocks(2);
```

> 默认块数为 1（支持 64 种组件）。组件注册时 `register_component_meta` 自动扩容掩码块数，无需手动调用 `reserve_mask_blocks`。手动预分配可避免运行中扩容开销。多块掩码查询通过 `get_entity_block(e, block_idx)` 或 `get_entity_block_by_idx(idx, block_idx)` 访问任意块。详见 [§ 29. multi_block_bitmask](#29-multi_block_bitmask--多块位掩码存储)。

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

## 5. View系统

提供高效的组件遍历，自动选择最小集作为主集迭代。

### 5.1 single_view\<T> — 单组件视图

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

### 5.2 multi_view\<T1, T2, ...> — 多组件视图

自动选择最小集作为主集迭代。

| 接口 | 说明 |
|------|------|
| `size()` | 实际匹配数（同时拥有所有组件的实体数） |
| `pool_size()` | 主集组件数量（主集池大小，不含匹配过滤） |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否同时拥有所有组件 |
| `for_each(func)` | 遍历多组件（自动检测 entity 参数） |
| `get_component_for_entity<T>(entity)` | 获取指定实体的指定类型组件（无则 nullptr） |
| `get_first_entity()` | 返回第一个匹配所有组件的实体 |
| `get_last_entity()` | 返回最后一个匹配所有组件的实体 |
| `get_entity_at_index(index)` | 返回主集第 index 个实体句柄 |
| `include_optional_component<U>()` | 链式追加可选组件，回调中为指针（无组件时为 nullptr） |

`size()` 与 `pool_size()` 的区别：当部分实体只拥有部分组件时，`pool_size()` 返回主集的实体总数，`size()` 返回其中真正同时拥有全部组件的实体数；若所有实体都拥有全部组件，两者相等。

```cpp
// 双组件
auto v2 = mgr.view<Position, Velocity>();
v2.for_each([](Position& p, Velocity& v) { /* ... */ });
v2.for_each([](entity e, Position& p, Velocity& v) { /* ... */ });

// 匹配数与主集大小
size_t matched = v2.size();       // 同时拥有 Position 和 Velocity 的实体数
size_t total   = v2.pool_size();  // 主集（最小集）的实体数

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

### 5.3 single_view_without — 排除视图

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

### 5.4 single_view_with — 获取视图

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

### 5.5 or_view\<A, B> — OR视图（零分配）

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

### 5.6 filter_view\<T, Pred> — 谓词过滤视图

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

### 5.7 filter_and_view — 过滤+AND组合视图

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

### 5.8 filter_or_view — 过滤+OR组合视图

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

### 5.9 sort_entities_by_component / reorder_by_component — 排序工具

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

### 5.10 page — 分页视图

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

### 5.11 sorted_by_component — 排序视图

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

### 5.12 sorted_by_component_value — 分组视图

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

### 5.13 track_changes — 变更检测视图

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

### 5.14 链式组合

`single_view` 和 `multi_view` 支持 `page` / `sorted_by_component` / `sorted_by_component_value` / `track_changes` / `filter_changed` / `filter_added` 等链式调用。注意这些链式方法返回的是独立的视图对象,**不互相嵌套**——每个链式视图只能独立使用其自身的 `for_each` / `size` / `empty` / `reset_tracking` 等接口,不能在链式视图后再调用 `page` 等其他链式方法。

```cpp
// 排序视图 (独立使用)
auto sorted = mgr.view<Position, Velocity>()
   .sorted_by_component<Position>([](Position& a, Position& b) { return a.x < b.x; });
sorted.for_each([](Position& p, Velocity& v) { /* 按排序顺序遍历 */ });

// 分页视图 (独立使用,基础视图才能调用 page)
auto paged = mgr.view<Position, Velocity>().page(0, 10);
paged.for_each([](Position& p, Velocity& v) { /* 前10个匹配实体 */ });

// 变更检测视图 (独立使用)
auto changed = mgr.view<Position>().track_changes();
changed.for_each([](Position& p) { /* 变更实体 */ });
changed.reset_tracking();

// 添加检测视图 (独立使用)
auto added = mgr.view<Position>().filter_added();
added.for_each([](Position& p) { /* 新添加实体 */ });
added.reset_tracking();
```

### 5.15 filter_changed — 逐实体变更检测

通过 `filter_changed()` 链式调用，仅返回自上次迭代以来组件值发生变化的实体。基于逐实体版本号追踪，可精确到单个实体。

> **注意：** 仅 `add()` 操作（包括覆盖添加）会触发变更版本号递增。通过 `get_ptr()` 直接修改组件内存不会触发变更检测。

| 接口 | 说明 |
|------|------|
| `size()` | 变更实体数量 |
| `empty()` | 是否为空 |
| `for_each(func)` | 遍历变更实体（首次全量返回，后续仅返回变更实体） |
| `reset_tracking()` | 重置快照基准（下次 for_each 重新全量返回） |

> `multi_view` 还提供 `filter_any_changed()`（无模板参数），等价于 `filter_changed_view<0>`，即跟踪任意一个组件的变更。`single_view` 无此重载。

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

### 5.16 filter_added — 逐实体添加检测

通过 `filter_added()` 链式调用，仅返回视图创建后**新添加**的组件。基于全局添加计数器实现。

| 接口 | 说明 |
|------|------|
| `size()` | 新增实体数量 |
| `empty()` | 是否为空 |
| `for_each(func)` | 遍历新增实体（首次全量返回，后续仅返回新添加的实体） |
| `reset_tracking()` | 重置添加检测基准（下次 for_each 重新全量返回） |

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

// 重置添加检测基准
av.reset_tracking();  // 下次 for_each 重新全量返回
```

> **与 `filter_changed` 的区别：** `filter_changed` 追踪"修改"（覆盖添加也会触发），`filter_added` 仅追踪"首次添加"（覆盖添加不触发）。

### 5.17 view_any_of — N元OR视图

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

### 5.18 exactly_one — 精确获取单个实体

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

### 5.19 find_one — 查询指定实体

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

### 5.20 iter_over_entities — 批量指定实体查询

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

## 6. Group系统

Group 在构造时预先计算匹配实体集，迭代时零分支。

### 6.1 Non-OwningGroup (`group`)

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

### 6.2 OwningGroup (`group` + `owned`)

通过 `mgr.group<First, Rest...>(ecs::owned<First>)` 创建，重排主集 `First` 的 dense 数组，使匹配实体在数组前部连续排列。

**注意：** `owned` 标签标记的组件类型会被重排，组件数据顺序会改变。如果其他代码依赖该组件的 dense 顺序，需谨慎使用。

**接口：**

| 接口 | 说明 |
|------|------|
| `size()` | 匹配实体数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否包含指定实体 |
| `for_each(func)` | 遍历匹配实体（自动检测 entity 参数） |
| `get<T>(entity)` | 获取指定实体的组件 T 指针 |
| `front()` | 首个匹配实体 |
| `back()` | 末尾匹配实体 |
| `rebuild()` | 重建缓存（组件增删后调用） |

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

### 6.3 ReorderGroup (`group` + `reorder`)

通过 `mgr.group<First, Rest...>(ecs::reorder<First>)` 创建，与 OwningGroup 同样重排主集，但语义更轻——仅表达"允许重排"，不暗示生命周期所有权。

**接口：**

| 接口 | 说明 |
|------|------|
| `size()` | 匹配实体数量 |
| `empty()` | 是否为空 |
| `contains(entity)` | 是否包含指定实体 |
| `for_each(func)` | 遍历匹配实体（自动检测 entity 参数） |
| `get<T>(entity)` | 获取指定实体的组件 T 指针 |
| `front()` | 首个匹配实体 |
| `back()` | 末尾匹配实体 |
| `rebuild()` | 重建缓存（组件增删后调用） |
| `share_with(other_reorder_group)` | 与另一个相同类型的 ReorderGroup 共享重排状态 |

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

## 7. runtime_view — 运行时视图

在运行时动态指定组件类型组合进行查询。组件类型数量无上限，前 64 种组件类型自动维护实体位掩码，超过 64 种的组件类型同样参与所有视图/分组查询。

### 7.1 实体掩码

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

### 7.2 运行时视图

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

### 7.3 排除视图

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

### 7.4 接口

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
| `sort_by_component<T>(cmp)` | 按组件值排序，结果存于 `sorted_entities_` |
| `get_sorted_entities()` | 获取排序后的实体列表（`const dense<entity>&`，需先调用 `sort_by_component`） |
| `changed()` | 检测组件池版本是否变化 |
| `reset_change_tracking()` | 重置变更检测基线 |
| `begin()` / `end()` | 迭代器，支持 range-for |
| `rebuild()` | 重新选择最小集合（组件数量变化后调用） |

> `get_entity_mask(entity)` 和 `get_component_bit<T>()` 是 `manager` 的方法,见 [§4](#4-ecsmanager-ecs管理器) 与 [§7.1](#71-实体掩码)。

### 7.5 组件类型无上限

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

### 7.6 for_each_typed — 组件引用回传

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

### 7.7 for_each_parallel — 并行迭代

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

### 7.8 for_each_paged — 分页遍历

```cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

// 每页 100 个，处理第 2 页
rv.for_each_paged(100, 100, [](entity e) {
    // 处理实体
});
```

机制：`offset` 和 `limit` 基于 primary dense 索引。offset 超出范围时回调不触发。

### 7.9 变更检测

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

### 7.10 sort_by_component — 按组件排序

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

### 7.11 count — 精确命中数

```cpp
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<Position>(),
    type_id::get_type_id<Velocity>()
});

size_t n = rv.count();  // 精确匹配数，遍历计算
```

机制：`size()` 返回 primary_set 大小（上限），`count()` 遍历所有命中实体计数。`count` 是 O(n) 操作。

### 7.12 iterator — 迭代器

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

### 7.13 runtime_term — OR / OPTIONAL / NOT 查询

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

### 7.14 access_mode — 读写标注

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

## 8. 函数存储（回调作为组件）

`t_fun` 可直接作为 ECS 组件，无需任何包装结构体。不同函数签名自动推导为不同组件类型，通过 `ecs::manager` 的标准组件接口存储与调用。详见 [§ 34. t_fun](#34-t_fun--函数类型延迟调用器)。

### 使用

```cpp
#include "part/t_fun.hpp"

// 回调目标函数
int  on_add(int a, int b) { return a + b; }
void on_callback(int x) { std::printf("cb: %d\n", x); }

ecs::manager mgr;
entity e = mgr.create_entity();

// 直接存储 t_fun 作为组件 (无需包装结构体)
mgr.add(e, t_fun{on_add, 10, 20});

// 获取并调用
auto* cb = mgr.get_ptr<t_fun<int(int,int)>>(e);
if (cb)
{
    (*cb)();               // 30, 用绑定参数调用
    (*cb)(3, 4);           // 7,  带参覆盖调用
    cb->set_arg<0>(100);
    (*cb)();               // 300
}
```

### void 返回值与无参函数

```cpp
// void 回调
entity e2 = mgr.create_entity();
mgr.add(e2, t_fun{on_callback, 42});
auto* v = mgr.get_ptr<t_fun<void(int)>>(e2);
(*v)();                    // 打印 "cb: 42"
v->result_ptr();           // nullptr

// 无参函数
void on_noop() {}
entity e3 = mgr.create_entity();
mgr.add(e3, t_fun{on_noop});
auto* n = mgr.get_ptr<t_fun<void()>>(e3);
(*n)();
```

### 通过 View 批量调用

```cpp
// 为多个实体添加回调
entity e1 = mgr.create_entity();
entity e2 = mgr.create_entity();
entity e3 = mgr.create_entity();
mgr.add(e1, t_fun{on_add, 1, 2});
mgr.add(e2, t_fun{on_add, 10, 20});
mgr.add(e3, t_fun{on_add, 100, 200});

// 遍历所有回调组件并调用
size_t count = 0;
int sum = 0;
mgr.view<t_fun<int(int,int)>>().for_each([&](entity, t_fun<int(int,int)>& c) {
    sum += c();
    ++count;
});
// count == 3, sum == 333
```

### 组件内修改持久化

```cpp
entity e = mgr.create_entity();
mgr.add(e, t_fun{on_add, 1, 2});

// 第一次查询修改
auto* p1 = mgr.get_ptr<t_fun<int(int,int)>>(e);
p1->set_arg<0>(100);
p1->set_arg<1>(200);

// 第二次查询验证持久化
auto* p2 = mgr.get_ptr<t_fun<int(int,int)>>(e);
(*p2)();                   // 300, 修改已持久化
```

---

## 9. 生命周期信号

两层架构：**即时信号**（函数指针回调） + **延迟信号**（环形缓冲区 + 溢出 chain，批量处理）。

**核心机制：即时/延迟互斥。** 注册了即时回调的事件同步触发且不入延迟队列；未注册才入队，由 `flush_*_signals` 处理。同一事件不会两路重复通知。

### 9.1 实体级即时信号

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

### 9.2 组件级即时信号

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

### 9.3 覆盖写与 on_modify

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

### 9.4 实体级延迟信号

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

### 9.5 组件级延迟信号

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

### 9.6 即时/延迟互斥机制

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

### 9.7 信号开关与溢出

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

### 9.8 delete_entity 的组件清理

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

### 9.9 即时信号 vs 延迟信号 选择指南

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

## 10. command_buffer — 延迟结构变更

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

## 11. reflection — 反射模块使用

`#include "reflection/reflection.hpp"`，命名空间 `reflect`。`noexcept`。

反射模块提供运行期类型元数据查询、字段访问与方法调用。支持聚合类型自动遍历公有字段、手填偏移量注册私有成员、宏注册成员/静态方法。支持运行时动态注册，注册线程安全。库独立，不创建线程，嵌入外部多线程架构。

### 11.1 注册宏

宏只能在命名空间作用域使用（通过 `inline` 变量初始化）。聚合类型字段名自动生成 `field_0`、`field_1`、…（上限由 `REFLECT_MAX_FIELDS` 决定，默认 64）。

| 宏 | 说明 |
|------|------|
| `REGISTER(Cls)` | 注册类型：聚合类型自动遍历公有字段；非聚合类型仅注册类型元信息（字段需 `REFLECT_PRIVATE` 或 `REGISTER_MEMBERS`） |
| `REGISTER_MEMBERS(Cls, f1, f2, ...)` | 注册成员（标量/数组统一入口），自动判断字段类别；支持 1~20 个字段 |
| `REGISTER_FNS(Cls, m1, m2, ...)` | 批量注册方法（自动判断成员/静态），支持 1~20 个方法 |
| `REGISTER_FN_OVERLOAD(Cls, method, ptr)` | 注册重载方法，显式指定方法指针 |
| `REFLECT(Cls)` | 类内标记宏，声明 friend 授权反射访问私有成员 |
| `REFLECT_PRIVATE(Cls, ...)` | 类外注册私有成员，偏移量和类型由成员指针自动推导；自动注册类型元信息（无需先 `REGISTER`） |
| `REGISTER_PRIVATE_OFFSETS(Cls, ...)` + `PRIV_FIELD(name, offset, Type)` | 手填偏移量注册私有成员，用于无法修改的第三方类型 |
| `REGISTER_TYPE_ONLY(Cls)` | 只注册类型元信息，不自动遍历字段。用于无字段类型，或配合底层 `register_array_field` 手动注册 |

### 11.2 查询入口

查询统一入口，无需区分单/多线程。`query_view` 持有 `type_meta*`，注册后长期有效。

| 接口 | 说明 |
|------|------|
| `reflect::get<T>()` | 按类型查询，未注册触发 `std::abort()`，返回 `query_view` |
| `reflect::get_by_name(name)` | 按类型名查询，未注册触发 `std::abort()`，返回 `query_view` |
| `reflect::try_get<T>()` | 软失败版本，未注册返回 `query_view::valid()==false`，不 abort |
| `reflect::try_get_by_name(name)` | 软失败版本（按名），不 abort |
| `reflect::global()` | 全局存储对象 |

### 11.3 query_view 接口

| 接口 | 说明 |
|------|------|
| `name()` | 类型名 |
| `valid()` | 是否绑定有效元数据（`try_get` 后用于判断是否注册） |
| `size()` | `sizeof(T)` |
| `align()` | `alignof(T)` |
| `field_count()` | 字段数 |
| `method_count()` | 方法数 |
| `type_id_value()` | 类型 id |
| `field(i)` | 按索引取字段元数据 |
| `field_by_name(name)` | 按名取字段元数据，未找到返回 nullptr |
| `method(i)` | 按索引取方法元数据 |
| `method_by_name(name)` | 按名取方法元数据，未找到返回 nullptr |
| `get<R>(obj, i)` | 按索引取字段引用（编译期偏移） |
| `get<R>(obj, i)` (const) | const 对象版本 |
| `get_by_name<R>(obj, name)` | 按名取字段引用 |
| `get_by_name<R>(obj, name)` (const) | const 对象版本 |
| `get_ptr(obj, name)` | 按名取字段 `void*` |
| `get_ptr(obj, name)` (const) | const 对象版本 |
| `invoke<R>(obj, name, args...)` | 调用方法，返回 R。参数数量不匹配或未注册触发 `std::abort()` |
| `try_invoke<R>(obj, name, args...)` | 软失败版本。`R=void` 返回 `bool`；非 void 返回 `std::optional<R>`。失败不 abort |
| `for_each_field(obj, f)` | 遍历实例字段，`f(name, ptr, type_id)` |
| `for_each_field(obj, f)` (const) | const 对象版本 |
| `for_each_field_meta(f)` | 遍历字段元数据，`f(field_meta&)` |
| `for_each_method(f)` | 遍历方法元数据，`f(method_meta&)` |
| `is_array(i)` | 字段 i 是否为数组 |
| `is_array_by_name(name)` | 按名判断字段是否为数组 |
| `array_rank(i)` | 数组维度数（0=标量，1~4=数组） |
| `array_total_elements(i)` | 数组总元素数（非数组为 0） |
| `array_element_stride(i)` | 数组元素步长（字节） |
| `array_extent(field_idx, dim)` | 第 `dim` 维元素数 |
| `array_element_ptr(obj, field_idx, element_idx)` | 取数组元素指针，越界返回 nullptr |
| `array_element_ptr(obj, field_idx, element_idx)` (const) | const 对象版本 |
| `array_element_ptr_by_name(obj, name, element_idx)` | 按名取数组元素指针 |
| `for_each_array_element(obj, field_idx, f)` | 遍历数组元素，`f(ptr, idx, type_id)` |
| `for_each_array_element(obj, field_idx, f)` (const) | const 对象版本 |
| **便捷接口** | |
| `array_info(field_idx)` | 聚合查询，返回 `const field_meta*`，非数组返回 nullptr |
| `array_info_by_name(name)` | 按名聚合查询 |
| `array_get<T>(obj, field_idx, element_idx)` | 类型安全访问 |
| `array_get<T>(obj, field_idx, element_idx)` (const) | const 对象版本 |
| `array_get_by_name<T>(obj, name, element_idx)` | 按名类型安全访问 |
| `array_set<T>(obj, field_idx, element_idx, value)` | 类型安全写入 |
| `array_set_by_name<T>(obj, name, element_idx, value)` | 按名类型安全写入 |

### 使用

```cpp
#include "reflection/reflection.hpp"

// === 聚合类型 (自动遍历公有字段) ===
struct Vec3 { float x, y, z; };
struct Pod16 { int a, b, c, d; };

REGISTER(Vec3);
REGISTER(Pod16);

// === 非聚合类型 (侵入式自动推导) ===
class Account {
    std::string name_;
    int balance_;
public:
    REFLECT(Account);  // 类内: friend 授权
    Account() : name_(""), balance_(0) {}
    Account(std::string n, int b) : name_(n), balance_(b) {}
    void deposit(int amt) { balance_ += amt; }
    int get_balance() const { return balance_; }
    static int version() { return 42; }
};

REGISTER(Account);

// 类外: 只写字段名, 偏移量和类型自动推导
REFLECT_PRIVATE(Account, name_, balance_);

REGISTER_FNS(Account, deposit, get_balance, version);  // 成员 + 静态 (自动判断)

// 重载方法示例 (显式指定方法指针)
struct Calc {
    int add(int a, int b) { return a + b; }
    int add(int a, int b, int c) { return a + b + c; }
};
REGISTER(Calc);
REGISTER_FN_OVERLOAD(Calc, add,
    static_cast<int(Calc::*)(int,int)>(&Calc::add));
REGISTER_FN_OVERLOAD(Calc, add,
    static_cast<int(Calc::*)(int,int,int)>(&Calc::add));

// === 字段访问 ===
Vec3 v{1.0f, 2.0f, 3.0f};
auto view = reflect::get<Vec3>();

view.get<float>(&v, 0);                    // 1.0 (按索引)
view.get_by_name<float>(&v, "field_1");    // 2.0 (按名)
view.get<float>(&v, 0) = 10.0f;            // 修改字段

// 私有成员访问
Account acc{"Alice", 100};
auto acc_view = reflect::get<Account>();
int bal = acc_view.get_by_name<int>(&acc, "balance_");  // 100
acc_view.get_by_name<int>(&acc, "balance_") = 200;      // 修改私有成员

// === 方法调用 ===
acc_view.invoke<void>(&acc, "deposit", 50);  // void 返回
int bal2 = acc_view.invoke<int>(&acc, "get_balance");  // 250
int ver  = acc_view.invoke<int>(nullptr, "version");   // 42 (静态方法)

// === 遍历字段 ===
view.for_each_field(&v, [](const char* name, void* ptr, int tid) {
    std::cout << name << " = " << *static_cast<float*>(ptr) << "\n";
});

view.for_each_field_meta([](const reflect::field_meta& fm) {
    std::cout << fm.name << " offset=" << fm.offset << "\n";
});

// === 按类型名查询 ===
auto v2 = reflect::get_by_name("Vec3");

// === 运行时动态注册 (线程安全) ===
struct Dynamic { int a, b; };
reflect::global().register_type<Dynamic>("Dynamic");
auto dv = reflect::get<Dynamic>();

// === 注册成员 (REGISTER_MEMBERS 统一入口) ===
struct Path { float points[16]; };
struct Grid { float cells[8][8]; };

REGISTER_MEMBERS(Path, points);    // 数组字段
REGISTER_MEMBERS(Grid, cells);     // 二维数组字段

// 多个成员: REGISTER_MEMBERS 批量注册
struct Mesh { float vertices[8]; int indices[16]; float transform[16]; };
REGISTER_MEMBERS(Mesh, vertices, indices, transform);  // 3 个字段

// 混合类型 (标量 + 数组): REGISTER_MEMBERS 批量
struct Mixed { int id; float pos[3]; };
REGISTER_MEMBERS(Mixed, id, pos);  // id 标量, pos 数组

// 聚合类型自定义字段名: REGISTER_MEMBERS 批量
struct Color { float r, g, b, a; };
REGISTER_MEMBERS(Color, r, g, b, a);  // 4 个字段

// 方法注册: REGISTER_FNS 批量
struct Calculator {
    int add(int a, int b) { return a + b; }
    static int multiply(int a, int b) { return a * b; }
};
REGISTER_FNS(Calculator, add, multiply);  // 成员 + 静态

Path p{};
auto pv = reflect::get<Path>();

// 元数据查询
pv.is_array(0);                       // true
pv.array_rank(0);                     // 1
pv.array_total_elements(0);           // 16
pv.array_element_stride(0);           // 4
pv.array_extent(0, 0);                // 16

// 元素访问 (按索引/按名)
float* e = static_cast<float*>(pv.array_element_ptr(&p, 0, 5));
float* e2 = static_cast<float*>(pv.array_element_ptr_by_name(&p, "points", 10));

// 便捷访问 (类型安全)
float val = pv.array_get<float>(&p, 0, 5);           // 读
pv.array_set<float>(&p, 0, 5, 42.0f);                // 写
float val_n = pv.array_get_by_name<float>(&p, "points", 3);  // 按名读
pv.array_set_by_name<float>(&p, "points", 8, 99.0f);         // 按名写

// 聚合查询
if (const auto* info = pv.array_info(0)) {
    // info->array_rank / info->total_elements / info->element_stride / info->extents[0]
}

// 遍历数组元素
pv.for_each_array_element(&p, 0, [](void* ptr, uint32_t idx, int tid) {
    *static_cast<float*>(ptr) = static_cast<float>(idx);
});

// 二维数组
Grid g{};
auto gv = reflect::get<Grid>();
gv.array_rank(0);                     // 2
gv.array_total_elements(0);           // 64
gv.array_extent(0, 0);                // 8
gv.array_extent(0, 1);                // 8
```

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 在函数内部使用 `REGISTER` 等注册宏 | `inline` 变量不能在块作用域声明 | 宏在命名空间作用域使用；函数内直接调 `reflect::global().register_type<T>(name)` |
| 未注册类型就查询 | `std::abort()` | 先 `REGISTER(T)` 再 `reflect::get<T>()` |
| 未注册私有成员就访问 | `field_by_name` 返回 nullptr，解引用崩溃 | 先 `REFLECT_PRIVATE` 或 `REGISTER_PRIVATE_OFFSETS` |
| `REFLECT_PRIVATE` 字段名写错 | 编译错误（成员不存在） | 字段名与类定义一致 |
| `REGISTER_PRIVATE_OFFSETS` 偏移量填错 | 访问错误内存 | 优先用 `REFLECT_PRIVATE` 自动推导；第三方类型用 `offsetof` 确认 |
| 方法参数数量不匹配 | `std::abort()` | 调用 `invoke` 时参数数量与方法签名一致 |
| 静态方法传非空 `obj` | 被忽略，不影响结果 | 静态方法传 `nullptr` |
| 字段数超过 `MAX_FIELDS_PER_TYPE`（256） | `std::abort()` | 单类型字段数不超过上限 |
| 方法数超过 `MAX_METHODS_PER_TYPE`（256） | `std::abort()` | 单类型方法数不超过上限 |
| 类型 id 超过 `MAX_TYPE_ID`（65536） | 注册被忽略 | 类型总数不超过上限 |
| 含 C 数组字段的类型直接用 `REGISTER` | 结构化绑定展开数组为多个标量字段，字段计数错误 | 改用 `REGISTER_MEMBERS`（自动判断标量/数组 + 推导数组元数据） |
| `REGISTER_MEMBERS` 数组维度数超过 4 | 注册被忽略 | 维度数限制 1~4，更高维度需拆分为结构体嵌套 |
| `array_element_ptr` 越界访问 | 返回 nullptr，解引用崩溃 | `element_idx` 必须 < `array_total_elements` |

### 私有成员注册方式选择

| 方式 | 适用场景 | 用户输入 |
|------|---------|---------|
| `REFLECT` + `REFLECT_PRIVATE` | 可修改类定义的类型 | 类内 1 行 friend，类外只写字段名 |
| `REGISTER_PRIVATE_OFFSETS` | 无法修改的第三方类型 | 手填字段名、偏移量、类型 |

`REFLECT_PRIVATE` 通过成员指针自动推导偏移量和类型，编译期保证正确性，优先使用。

---

## 12. serialization — 序列化/反序列化

`#include "serialization/serialization.hpp"`，命名空间 `ecs`。`noexcept`。

序列化模块提供 ECS 组件持久化能力。`serialization` 主类支持 JSON 与原生二进制两种格式；编码器抽象层（`archive_codec` + `codec_registry`）提供 JSON / 二进制 / Protobuf / FlatBuffer 四种格式的统一接口，支持格式自动检测与切换。组件序列化优先级：用户手写 `to_json()`/`from_json()` > 反射注册字段 > base64。实体引用通过两阶段加载自动重映射，保证加载后实体身份关系正确。

底层 JSON 读写器（`part/json_writer.hpp`、`part/json_reader.hpp`）为通用基础模块，可独立使用。

模块文件结构（`include/serialization/` 目录）：

| 文件 | 内容 |
|------|------|
| `serialization/archive_codec.hpp` | 编码器抽象接口（`archive_writer`/`archive_reader`/`archive_codec`） |
| `serialization/codec_json.hpp` | JSON 编码器（`json_codec`） |
| `serialization/codec_binary.hpp` | 原生二进制编码器（`binary_codec`，magic `LCE1`） |
| `serialization/codec_protobuf.hpp` | Protobuf 风格编码器（`protobuf_codec`，magic `LCPB`） |
| `serialization/codec_flatbuffer.hpp` | FlatBuffer 风格编码器（`flatbuffer_codec`，magic `LCFB`） |
| `serialization/codec_registry.hpp` | 编码器注册表 + 格式自动检测 |
| `serialization/archive_types.hpp` | 归档公共类型（`archive_header`/`entity_remap`/`metadata_entry`） |
| `serialization/archive_logic.hpp` | 公共逻辑层（与格式无关的实体收集/过滤/版本操作） |
| `serialization/safety.hpp` | 安全限制 + 字节序处理 + Base64 编解码 + RLE 压缩工具 |
| `serialization/type_name.hpp` | 稳定类型名注册 + 实体引用字段注册 + 枚举类型注册 |
| `serialization/reflect_bridge.hpp` | 反射桥接概念 + 自动序列化/反序列化（含嵌套对象/数组/枚举） |
| `serialization/filter.hpp` | 选择性序列化过滤器（按 layer/tag/group/flags/白名单） |
| `serialization/migration.hpp` | 字段级迁移 + 组件版本控制 |
| `serialization/stats.hpp` | 序列化统计信息 |
| `serialization/binary_writer.hpp` | 原生二进制写入器（类型头含总字节数） |
| `serialization/binary_reader.hpp` | 原生二进制读取器 |
| `serialization/serializer.hpp` | 序列化器主类 |
| `serialization/serialization.hpp` | 统一入口（包含上述全部子文件） |

### 12.1 组件序列化约定

| 组件类型 | 序列化方式 | 要求 |
|---------|-----------|------|
| trivially copyable | base64 编码二进制数据 | 无额外要求 |
| 非 trivial | JSON 对象（`to_json()` 返回值） | 提供 `std::string to_json() const` 和 `void from_json(std::string_view)` |
| 已注册反射 | 遍历字段自动生成 JSON 对象 | 字段类型为基本类型、`std::string`、嵌套对象、数组或已注册枚举 |

未满足上述要求的类型在编译期触发 `static_assert`。

### 12.2 serialization 接口

| 接口 | 说明 |
|------|------|
| `serialization(manager& m)` | 构造，绑定 manager |
| `save_to_file<Ts...>(path, fmt)` | 保存指定类型组件到文件，`fmt` 可选 `format::json`（默认）/ `binary` / `protobuf` / `flatbuffer` |
| `save_to_string<Ts...>(out, fmt)` | 保存指定类型组件到字符串 |
| `save_changed<Ts...>(out, fmt)` | 增量保存，仅输出版本变化的组件类型（无变化返回 `"{}"`） |
| `save_entity<Ts...>(e, out)` | 单实体序列化，只保存指定实体的组件 |
| `load_from_file<Ts...>(path)` | 从文件加载，自动检测格式（JSON/Binary/Protobuf/FlatBuffer） |
| `load_from_string<Ts...>(data)` | 从字符串加载，自动检测格式（四格式），支持压缩/变换 |
| `validate_file(path)` | 仅校验文件格式，不加载组件 |
| `validate_string(data)` | 仅校验字符串格式，不加载组件 |
| `limits()` | 获取安全限制配置引用 |
| `archive_version()` / `set_archive_version(v)` | 存档版本读写 |
| `engine_version()` / `set_engine_version(v)` | 引擎版本读写 |
| `set_filter(f)` / `filter()` | 设置/获取选择性序列化过滤器 |
| `set_load_mode(m)` / `get_load_mode()` | 设置/获取加载模式（`replace`/`append`/`merge`） |
| `set_metadata(key, value)` / `get_metadata(key)` | 设置/查询存档元数据 |
| `all_metadata()` | 获取全部元数据 |
| `last_stats()` | 获取上一次保存/加载的统计信息 |
| `set_progress_callback(cb)` | 设置进度回调（`void(*)(size_t, size_t)`） |
| `on_save(cb)` / `on_load(cb)` | 设置保存/加载变换钩子（`void(*)(std::string&)`） |
| `set_compression(c, d)` | 注册压缩/解压函数对 |
| `serialization::save<Ts...>(m, path, fmt)` | 静态便捷保存接口 |
| `serialization::load<Ts...>(m, path)` | 静态便捷加载接口 |

### 12.3 安全限制

`safety_limits` 结构体控制加载时的各项上限，防止恶意输入。通过 `serialization::limits()` 访问与修改。

| 字段 | 默认值 | 说明 |
|------|--------|------|
| `max_file_size` | 256 MB | 单文件最大字节数 |
| `max_string_length` | 16 MB | 单字符串最大长度 |
| `max_array_elements` | 10,000,000 | 数组最大元素数 |
| `max_object_fields` | 65536 | 对象最大字段数 |
| `max_depth` | 64 | JSON 最大嵌套深度 |
| `max_entity_count` | 10,000,000 | 最大实体数 |

### 12.4 类型名注册

跨编译器加载存档时，`typeid(T).name()` 返回值不同会导致类型名不匹配。`register_type_name` 注册稳定类型名保证可移植性。

| 接口 | 说明 |
|------|------|
| `register_type_name<T>(stable_name)` | 注册类型 T 的稳定名 |
| `lookup_type_name(type_id)` | 按 type_id 查稳定名 |
| `lookup_type_id(name)` | 按稳定名查 type_id |
| `register_entity_field<T>(field_name)` | 注册 entity 引用字段（加载时自动重映射） |

### 12.5 JSON 格式

```json
{
  "version": 1,
  "engine": 0,
  "meta": {"author": "alice", "desc": "测试存档"},
  "cv": {"Hp": 2},
  "entities": [
    {"i": 索引, "v": 版本, "f": flags, "t": tag, "l": layer, "g": group_id}
  ],
  "components": {
    "类型名": [
      {"i": 索引, "v": 版本, "d": 组件数据}
    ]
  }
}
```

- `version` / `engine`：存档版本 / 引擎版本（加载时高版本会被拒绝）
- `meta`：存档元数据（可选，由 `set_metadata` 设置）
- `cv`：组件版本表（可选，键为类型名，值为版本号，用于迁移）
- `i`：实体索引
- `v`：实体版本号
- `f`/`t`/`l`/`g`：实体状态（flags/tag/layer/group_id）
- `d`：组件数据（trivially copyable 类型为 base64 字符串，非 trivial 类型为 JSON 对象）

### 12.6 json_writer 接口

`#include "part/json_writer.hpp"`，全局命名空间。

| 接口 | 说明 |
|------|------|
| `json_writer(reserve, pretty)` | 构造，预分配缓冲区，可选美化输出 |
| `begin_object()` / `end_object()` | 对象 |
| `begin_array()` / `end_array()` | 数组 |
| `key(k)` | 键 |
| `value(v)` | 值（string/int/uint/float/double/bool） |
| `null()` | null 值 |
| `raw_value(json)` | 原始 JSON 片段（嵌入已格式化的 JSON） |
| `string()` | 获取结果字符串 |
| `take()` | 取走结果字符串 |

### 12.7 json_reader 接口

`#include "part/json_reader.hpp"`，全局命名空间。

| 接口 | 说明 |
|------|------|
| `json_reader(src)` | 构造，传入 JSON 字符串 |
| `enter_object()` / `exit_object()` | 进入/退出对象 |
| `enter_array()` / `exit_array()` | 进入/退出数组 |
| `next_key()` | 读取下一个键（空 view 表示结束） |
| `next_element()` | 数组是否有下一元素（自动处理逗号） |
| `end_element()` | 显式结束当前元素（跳过逗号） |
| `read_bool()` / `is_null()` | 读取 bool / 判断 null |
| `read_int32()` / `read_uint32()` / `read_int64()` / `read_uint64()` | 读取整数 |
| `read_float()` / `read_double()` | 读取浮点 |
| `read_string()` | 读取字符串 |
| `read_raw_value()` | 读取原始 JSON 片段 |
| `skip_value()` | 跳过当前值 |
| `has_error()` / `last_error()` | 错误状态 |

### 12.8 用户手写 JSON 偏差容忍

json_reader 兼容用户手写 JSON 的常见偏差（非标准但常见）：

| 偏差类型 | 示例 | 支持 |
|---------|------|------|
| 多空格/换行/tab | `{ "a" : 1 }` | ✓ |
| 尾随逗号 | `{"a":1,}` / `[1,2,]` | ✓ |
| 单引号字符串 | `{'a':'x'}` | ✓ |
| 块注释 `/* */` | `{"a":1/* c */}` | ✓ |
| 行注释 `//` | `{"a":1}\n// c` | ✓ |
| 数字前导 `+` 号 | `{"a":+1}` | ✓ |

```cpp
// 用户手写的非标准 JSON 也能正确解析
std::string user_json = R"({
    // 用户配置
    'name' : 'Alice',
    'age' : +30,
    'scores' : [ +90, +85, +95, ],
    /* 元数据 */
    'meta' : { 'active' : true, },
})";

json_reader r(user_json);
r.enter_object();
std::string_view k;
while (!(k = r.next_key()).empty()) {
    // ... 正常解析
}
```

### 使用

```cpp
#include "serialization/serialization.hpp"

// === trivially copyable 组件 (自动 base64) ===
struct Vec3 { float x, y, z; };
static_assert(std::is_trivially_copyable_v<Vec3>);

manager mgr;
entity e1 = mgr.create_entity();
entity e2 = mgr.create_entity();
mgr.add<Vec3>(e1, Vec3{1.0f, 2.0f, 3.0f});
mgr.add<Vec3>(e2, Vec3{4.0f, 5.0f, 6.0f});

// 保存
serialization(mgr).save_to_file<Vec3>("save.json");

// 加载到新 manager
manager mgr2;
serialization(mgr2).load_from_file<Vec3>("save.json");

// === 非 trivial 组件 (to_json/from_json) ===
struct PlayerInfo {
    std::string name;
    int level;
    std::string to_json() const {
        json_writer w;
        w.begin_object();
        w.key("name").value(name);
        w.key("level").value(level);
        w.end_object();
        return w.take();
    }
    void from_json(std::string_view s) {
        json_reader r(s);
        r.enter_object();
        std::string_view k;
        while (!(k = r.next_key()).empty()) {
            if (k == "name") name = r.read_string();
            else if (k == "level") level = r.read_int32();
            else r.skip_value();
        }
    }
};

// 多类型混合保存/加载
manager mgr3;
entity e = mgr3.create_entity();
mgr3.add<Vec3>(e, Vec3{1, 0, 0});
mgr3.add<PlayerInfo>(e, PlayerInfo{"Alice", 99});

serialization(mgr3).save_to_file<Vec3, PlayerInfo>("save2.json");

manager mgr4;
serialization(mgr4).load_from_file<Vec3, PlayerInfo>("save2.json");

// === 保存到字符串 ===
std::string json;
serialization(mgr).save_to_string<Vec3>(json);

// === 从字符串加载 ===
manager mgr5;
serialization(mgr5).load_from_string<Vec3>(json);

// === 二进制格式 (trivial 类型直存, 体积更小) ===
serialization(mgr).save_to_file<Vec3>("save.bin", serialization::format::binary);

// 二进制加载 (同一个 load_from_file 接口, 自动检测格式)
manager mgr6;
serialization(mgr6).load_from_file<Vec3>("save.bin");

// === Protobuf 格式 (紧凑 wire format, 适合网络传输) ===
serialization(mgr).save_to_file<Vec3>("save.pb", serialization::format::protobuf);

// === FlatBuffer 格式 (零拷贝读取, O(1) 字段访问) ===
serialization(mgr).save_to_file<Vec3>("save.fb", serialization::format::flatbuffer);

// === 格式自动检测 ===
// load_from_string / load_from_file 根据前 4 字节 magic 自动判断:
// "{"    → JSON
// "LCE1" → Binary
// "LCPB" → Protobuf
// "LCFB" → FlatBuffer
std::string pb_data;
serialization(mgr).save_to_string<Vec3>(pb_data, serialization::format::protobuf);
manager mgr7;
serialization(mgr7).load_from_string<Vec3>(pb_data);  // 自动走 Protobuf 路径

// === 仅校验格式 (不加载组件) ===
auto check = serialization(mgr).validate_string(json);
if (!check) { /* 格式错误 */ }

// === 跨编译器稳定类型名 ===
register_type_name<Vec3>("Vec3");
register_type_name<PlayerInfo>("PlayerInfo");

// === 安全限制配置 ===
serialization s(mgr);
s.limits().max_file_size = 64 * 1024 * 1024;  // 限制 64MB
s.limits().max_entity_count = 10000;          // 限制 1 万实体
s.save_to_file<Vec3>("save.json");

// === 版本控制 ===
serialization s2(mgr);
s2.set_archive_version(2);
s2.set_engine_version(100);
s2.save_to_file<Vec3>("save_v2.json");

// 加载时高版本存档会被拒绝
manager mgr8;
serialization(mgr8).set_archive_version(1);
auto r = serialization(mgr8).load_from_file<Vec3>("save_v2.json");
if (!r) { /* r.read_message() 包含版本不匹配信息 */ }

// === 选择性序列化 (filter) ===
manager mgr_f;
entity e_a = mgr_f.create_entity();
entity e_b = mgr_f.create_entity();
mgr_f.get_entity_state(e_a.parts_.index_).layer = 1;
mgr_f.get_entity_state(e_b.parts_.index_).layer = 2;
mgr_f.add<Hp>(e_a, Hp(10, 100));
mgr_f.add<Hp>(e_b, Hp(20, 100));

serialize_filter filter;
filter.by_layer(1);  // 仅保存 layer==1 的实体
serialization sf(mgr_f);
sf.set_filter(&filter);
std::string fjson;
sf.save_to_string<Hp>(fjson);  // 只含 e_a 的 Hp

// === 单实体序列化 ===
std::string ejson;
serialization(mgr_f).save_entity<Hp>(e_b, ejson);  // 只含 e_b 的 Hp

// === 存档元数据 ===
serialization sm(mgr_f);
sm.set_metadata("author", "alice");
sm.set_metadata("level", "boss-fight");
std::string mjson;
sm.save_to_string<Hp>(mjson);  // JSON 含 "meta" 字段
manager mgr_m;
serialization sm2(mgr_m);
sm2.load_from_string<Hp>(mjson);
const std::string* author = sm2.get_metadata("author");  // "alice"

// === 加载模式 ===
manager mgr_dst;
entity e_old = mgr_dst.create_entity();
mgr_dst.add<Hp>(e_old, Hp(999, 999));

// replace: 清空旧数据后加载
serialization(mgr_dst).set_load_mode(load_mode::replace).load_from_string<Hp>(mjson);
// append/merge: 保留旧数据追加加载
serialization(mgr_dst).set_load_mode(load_mode::append).load_from_string<Hp>(mjson);

// === 字段级迁移 ===
// 1. 先存 v1 存档 (未注册版本, cv 字段不写入)
std::string v1_json;
serialization(mgr_f).save_to_string<Hp>(v1_json);

// 2. 升级到 v2 并注册迁移函数
register_component_version<Hp>(2);
register_migration<Hp>(1, 2, [](json_reader& old, json_writer& neu) {
    if (!old.enter_object()) { neu.raw_value("{}"); return; }
    neu.begin_object();
    std::string_view k;
    while (!(k = old.next_key()).empty()) {
        if (k == "m") neu.key("m").value(old.read_int32() + 100);  // max + 100
        else neu.key(k).raw_value(old.read_raw_value());
    }
    neu.end_object();
});

// 3. 加载 v1 存档触发迁移 (saved_cv=1 默认, current_cv=2)
manager mgr_mig;
serialization(mgr_mig).load_from_string<Hp>(v1_json);  // Hp.max 被 +100

// === 增量序列化 ===
serialization si(mgr_f);
std::string inc1;
si.save_changed<Hp, Vec3>(inc1);  // 首次全量
std::string inc2;
si.save_changed<Hp, Vec3>(inc2);  // 无变化, 输出 "{}"

// === 压缩与变换钩子 ===
serialization sc(mgr_f);
sc.set_compression(rle_compress, rle_decompress);
sc.on_save([](std::string& data) { data += "//SIG"; });      // 加签
sc.on_load([](std::string& data) {                             // 验签
    auto pos = data.rfind("//SIG");
    if (pos != std::string::npos) data.erase(pos);
});
std::string cjson;
sc.save_to_string<Hp>(cjson);  // 压缩 + 加签
manager mgr_c;
serialization sc2(mgr_c);
sc2.set_compression(rle_compress, rle_decompress);
sc2.on_load([](std::string& data) {
    auto pos = data.rfind("//SIG");
    if (pos != std::string::npos) data.erase(pos);
});
sc2.load_from_string<Hp>(cjson);  // 验签 + 解压

// === 统计信息 ===
serialization ss(mgr_f);
std::string sj;
ss.save_to_string<Hp, Vec3>(sj);
const auto& stats = ss.last_stats();
// stats.total_bytes / stats.per_type[i].component_count / .bytes

// === 便捷静态接口 ===
serialization::save<Hp>(mgr_f, "quick.json");
manager mgr_q;
serialization::load<Hp>(mgr_q, "quick.json");
```

### 12.9 二进制格式布局

```
偏移  内容
0     magic "LCE1" (4 字节)
4     endianness (1, = LE)
5     format version (1)
6     reserved (2 字节)
8     archive_version (uint32 LE)
12    engine_version (uint32 LE)
16    meta_len (uint32 LE) + meta_json (元数据 JSON 子串)
     entities_json_len (uint32 LE) + entities_json (实体状态, JSON 子串)
     type_count (uint32 LE)
     [每个类型: 类型名(string) + 组件版本(uint32) + 类型数据长度(uint32) + 类型数据]
```

类型数据布局：`元素数(uint32) + [entity_index(uint32) + version(uint32) + 组件数据]*`。trivially copyable 类型组件数据为原始字节(LE)，非 trivial 类型为 JSON 子串。类型头含总字节数，加载时遇未知类型可按长度跳过，避免读取错位。

### 12.10 选择性序列化过滤器

`serialize_filter` 结构体按实体状态过滤，只序列化符合条件的实体及其组件。

```cpp
struct serialize_filter {
    bool use_layer = false;    uint32_t layer = 0;
    bool use_tag = false;      uint32_t tag = 0;
    bool use_group = false;    uint32_t group_id = 0;
    bool use_flags = false;    uint32_t flags_mask = 0;  uint32_t flags_value = 0;
    bool use_whitelist = false;  dense<uint32_t> entity_whitelist;
    // 链式构造
    serialize_filter& by_layer(uint32_t l);
    serialize_filter& by_tag(uint32_t t);
    serialize_filter& by_group(uint32_t g);
    serialize_filter& by_flags(uint32_t mask, uint32_t value);
    serialize_filter& by_entities(dense<uint32_t>&& ids);
};
```

| 字段 | 含义 |
|------|------|
| `use_layer` / `layer` | 仅保留指定 layer 的实体 |
| `use_tag` / `tag` | 仅保留指定 tag 的实体 |
| `use_group` / `group_id` | 仅保留指定 group 的实体 |
| `use_flags` / `flags_mask` / `flags_value` | `(state.flags & mask) == value` 的实体 |
| `use_whitelist` / `entity_whitelist` | 仅保留索引在白名单内的实体 |

多条件组合为 AND 关系。`matches_entity` 同时检查白名单与状态条件。

### 12.11 单实体序列化

`save_entity<Ts...>(e, out)` 利用过滤器只序列化指定实体的组件，用于实体快照、网络同步等场景。

### 12.12 加载模式（load_mode）

| 模式 | 行为 |
|------|------|
| `load_mode::replace` | 加载前调用 `manager::clear()` 清空所有实体与组件，再创建新实体 |
| `load_mode::append` | 保留现有实体，直接追加加载的实体（默认） |
| `load_mode::merge` | 保留现有实体，追加加载（与 append 行为一致，预留语义扩展） |

### 12.13 存档元数据

通过 `set_metadata(key, value)` 存入自定义键值对，保存时写入 `meta` 字段，加载时回填。用于记录保存时间、作者、关卡名等信息。

```json
{
  "version": 1,
  "meta": {"author": "test_user", "desc": "测试存档"},
  "entities": [...],
  "components": {...}
}
```

### 12.14 字段级迁移 + 组件版本控制

`register_component_version<T>(version)` 注册组件当前版本，保存时写入 `cv` 字段。`register_migration<T>(from, to, fn)` 注册迁移函数，加载时按 `from → from+1 → ... → to` 链式调用。

| 接口 | 说明 |
|------|------|
| `register_component_version<T>(version)` | 注册组件当前版本 |
| `register_migration<T>(from, to, migrate_fn)` | 注册单步迁移函数 |
| `lookup_component_version<T>()` | 查询组件当前版本 |
| `migrate_component(tid, from, to, old_data, w)` | 执行迁移链 |

迁移函数签名 `void(*)(json_reader& old, json_writer& neu)`：从 `old` 读取旧 JSON，写入 `neu` 新 JSON。需自行调用 `old.enter_object()`。

```json
{
  "version": 1,
  "cv": {"Hp": 2},
  "entities": [...],
  "components": {"Hp": [{"i":1, "v":1, "d":{"c":42,"m":100}}]}
}
```

未注册版本的组件加载时按 `saved_ver` 处理（不迁移）。存档无 `cv` 字段时 `saved_ver` 默认为 1。

### 12.15 增量序列化（save_changed）

`save_changed<Ts...>(out, fmt)` 跟踪各组件池版本号，仅当某类型池版本变化时输出该类型。首次调用全量输出；无变化时返回 `"{}"`。适用于频繁自动保存场景，减少 IO。

### 12.16 压缩与变换钩子

| 接口 | 说明 |
|------|------|
| `set_compression(compress_fn, decompress_fn)` | 注册压缩/解压函数对，保存后压缩、加载前解压（仅对 JSON 路径生效，二进制不压缩） |
| `on_save(cb)` | 保存后变换数据（如加密、签名） |
| `on_load(cb)` | 加载前变换数据（如解密、校验） |

`safety.hpp` 提供 `rle_compress` / `rle_decompress` 工具函数（RLE1 格式，针对重复字节序列）。变换顺序：save → compress → on_save；on_load → decompress → load。

### 12.17 进度回调与统计

| 接口 | 说明 |
|------|------|
| `set_progress_callback(cb)` | 设置 `void(*)(size_t current, size_t total)` 回调 |
| `last_stats()` | 获取 `serialize_stats`，含 `entity_count`、`total_bytes`、`archive_version`、`per_type`（每类型组件数与字节数） |

### 12.18 反射桥接扩展

`reflect_bridge` 支持嵌套对象、数组字段、枚举类型的自动序列化：

| 字段类型 | 处理方式 |
|---------|---------|
| 基本类型（int/float/bool/string） | 直接读写 |
| 嵌套对象（字段为已注册反射类型） | 递归调用 `to_json`/`from_json` |
| 数组字段（`array_rank > 0`） | 遍历元素逐个序列化，按 `element_stride` 步进 |
| 已注册枚举 | `register_enum<T>()` 注册底层类型，序列化为整数 |

### 12.19 编码器抽象接口

`archive_writer` / `archive_reader` / `archive_codec` 提供与具体格式无关的读写接口，各格式编码器（JSON / 二进制 / Protobuf / FlatBuffer）实现该接口。`#include "serialization/archive_codec.hpp"`，命名空间 `ecs`。

`archive_type` 枚举标识字段类型：`null_t` / `bool_t` / `int32_t` / `uint32_t` / `int64_t` / `uint64_t` / `float32_t` / `float64_t` / `string_t` / `bytes_t` / `object_t` / `array_t`。

`archive_writer` 接口：

| 接口 | 说明 |
|------|------|
| `begin_object()` / `end_object()` | 写入对象起止 |
| `begin_array(count=0)` / `end_array()` | 写入数组起止，`count=0` 表示元素数未知 |
| `key(k)` | 写入字段键 |
| `write_bool(v)` / `write_i32(v)` / `write_u32(v)` | 写入标量 |
| `write_i64(v)` / `write_u64(v)` | 写入 64 位整数 |
| `write_f32(v)` / `write_f64(v)` | 写入浮点 |
| `write_string(v)` | 写入字符串 |
| `write_bytes(data, len)` | 写入原始字节（trivially copyable 组件） |
| `write_raw(fragment)` | 嵌入已格式化片段（用户 `to_json` 输出） |
| `take()` | 取走结果字符串 |
| `size()` | 当前缓冲区大小 |

`archive_reader` 接口（`read_string_view` 返回 `string_view` 指向原缓冲区，零拷贝）：

| 接口 | 说明 |
|------|------|
| `enter_object()` / `leave_object()` | 进入/退出对象 |
| `enter_array()` / `leave_array()` | 进入/退出数组 |
| `next_element()` / `end_element()` | 数组迭代 |
| `next_key()` | 读取下一字段键，空视图表示对象结束 |
| `read_bool()` / `read_i32()` / `read_u32()` | 读取标量 |
| `read_i64()` / `read_u64()` | 读取 64 位整数 |
| `read_f32()` / `read_f64()` | 读取浮点 |
| `read_string_view()` / `read_string()` | 读取字符串（前者零拷贝） |
| `read_bytes_view(len)` | 读取字节视图 |
| `skip_value()` | 跳过当前值（未知字段） |
| `has_error()` / `last_error()` | 错误状态 |
| `peek_type()` | 当前值类型 |

`archive_codec` 工厂接口：

| 接口 | 说明 |
|------|------|
| `magic[4]` | 格式标识（4 字节，用于自动检测） |
| `create_writer()` | 创建写入器 |
| `create_reader(data)` | 创建读取器，绑定数据视图 |
| `destroy_writer(w)` / `destroy_reader(r)` | 销毁实例 |
| `matches(data)` | 检测数据是否匹配本格式 |

### 12.20 编码器注册表

`codec_registry` 单例管理所有内置格式编码器，按 magic 头自动检测格式。`#include "serialization/codec_registry.hpp"`。

| 接口 | 说明 |
|------|------|
| `codec_registry::instance()` | 获取单例 |
| `register_codec(c)` | 注册自定义编码器 |
| `detect(data)` | 按 magic 头检测格式，返回 `archive_codec*` |
| `get(idx)` | 按索引获取编码器 |
| `count()` | 已注册编码器数 |

`codec_index` 命名空间提供索引常量：`json=0` / `binary=1` / `protobuf=2` / `flatbuffer=3`。

自由函数：

| 接口 | 说明 |
|------|------|
| `get_codec(fmt_idx)` | 按索引获取编码器 |
| `detect_codec(data)` | 自动检测并返回编码器 |

### 12.21 四种内置编码器

| 编码器 | magic | 头文件 | 字段标识方式 |
|--------|-------|--------|------------|
| `json_codec` | `{`（首字符） | `codec_json.hpp` | 字段名（字符串键） |
| `binary_codec` | `LCE1` | `codec_binary.hpp` | 字段名（字符串键） |
| `protobuf_codec` | `LCPB` | `codec_protobuf.hpp` | 字段编号（`f1`/`f2`/...） |
| `flatbuffer_codec` | `LCFB` | `codec_flatbuffer.hpp` | 字段编号（`f1`/`f2`/...） |

各编码器均实现 `archive_codec` 工厂接口。`create_writer()` 返回 `archive_writer*`，`create_reader(data)` 返回 `archive_reader*`，用完需调用 `destroy_writer`/`destroy_reader` 释放。

### 12.22 公共逻辑层

`archive_logic` 提供与格式无关的实体收集、过滤、版本检查、组件序列化分发逻辑，通过 `archive_writer`/`archive_reader` 接口操作编码器。`#include "serialization/archive_logic.hpp"`，命名空间 `ecs`。

| 接口 | 说明 |
|------|------|
| `archive_logic(manager& m)` | 构造，绑定 manager |
| `set_filter(f)` / `filter()` | 设置/获取选择性过滤器 |
| `stats()` / `reset_stats()` | 统计信息 / 重置 |
| `save_header(w, archive_ver, engine_ver, metadata)` | 保存存档头（版本 + 元数据） |
| `save_component_versions<Ts...>(w)` | 保存组件版本表 |
| `save_entities<Ts...>(w)` | 保存实体状态（含去重） |
| `save_components<Ts...>(w)` | 保存组件数据 |
| `load_header(r, max_ver, metadata, err)` | 加载存档头，返回 `{archive_ver, engine_ver}` |
| `scan_entities(r, remap, max_count)` | 扫描实体并建立重映射 |

### 12.23 多格式编码使用

```cpp
#include "serialization/serialization.hpp"

// === 直接使用编码器 ===
protobuf_codec pc;
archive_writer* w = pc.create_writer();
w->begin_object();
w->key("version"); w->write_u32(42);
w->key("name");    w->write_string("hello");
w->end_object();
std::string data = w->take();
pc.destroy_writer(w);

// 读取（零拷贝：string_view 指向原缓冲区）
archive_reader* r = pc.create_reader(data);
if (r->enter_object()) {
    std::string_view k;
    while (!(k = r->next_key()).empty()) {
        if (k == "f1") {
            uint32_t ver = r->read_u32();
        } else if (k == "f2") {
            std::string_view name = r->read_string_view();  // 零拷贝
        } else {
            r->skip_value();
        }
    }
}
pc.destroy_reader(r);

// === 格式自动检测 ===
const archive_codec* codec = detect_codec(data);
if (codec) {
    archive_reader* auto_r = codec->create_reader(data);
    // ... 按 archive_reader 接口读取
    codec->destroy_reader(auto_r);
}

// === 通过 serialization 主类统一使用四格式 (推荐) ===
manager mgr;
entity e1 = mgr.create_entity();
mgr.add<Vec3>(e1, Vec3{1.0f, 2.0f, 3.0f});

serialization saver(mgr);
saver.set_archive_version(2);
saver.set_metadata("author", "dev");

// 四种格式用同一个接口, 仅切换 format 枚举
std::string json_data, bin_data, pb_data, fb_data;
saver.save_to_string<Vec3>(json_data, serialization::format::json);
saver.save_to_string<Vec3>(bin_data, serialization::format::binary);
saver.save_to_string<Vec3>(pb_data, serialization::format::protobuf);
saver.save_to_string<Vec3>(fb_data, serialization::format::flatbuffer);

// 加载时自动检测格式 (无需指定)
manager mgr2;
serialization loader(mgr2);
loader.load_from_string<Vec3>(pb_data);   // 自动识别 LCPB
loader.load_from_string<Vec3>(fb_data);   // 自动识别 LCFB

// === 直接使用编码器 (高级场景, 需要手动管理生命周期) ===
protobuf_codec pc;
archive_writer* w = pc.create_writer();
w->begin_object();
w->key("version"); w->write_u32(42);
w->end_object();
std::string data = w->take();
pc.destroy_writer(w);

// === 注册自定义编码器 ===
class my_codec final : public archive_codec {
    // 实现 create_writer / create_reader / destroy_* / matches
};
codec_registry::instance().register_codec(&my_codec_instance);
```

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 未提供 `to_json`/`from_json` 且非 trivially copyable | 编译错误 | 提供 `to_json`/`from_json` 或确保类型 trivially copyable |
| `to_json` 与 `from_json` 字段不一致 | 反序列化数据丢失 | 两端字段保持一致 |
| 加载类型与保存时不一致 | 类型名不匹配，组件被跳过 | 加载时显式指定所有需要的类型 |
| 跨编译器加载未注册类型名 | 类型名（`typeid(T).name()`）不同 | 使用 `register_type_name` 注册稳定名 |
| 加载超限文件 | 触发安全限制失败 | 调整 `limits()` 上限或检查文件来源 |
| 组件含 entity 引用未注册字段 | 引用指向旧索引 | 使用 `register_entity_field<T>(field_name)` 注册 |
| 加载高于当前版本的存档 | 版本检查失败被拒绝 | 升级 `set_archive_version` 或降级存档 |
| 迁移函数未调用 `old.enter_object()` | 读取不到字段，迁移失败 | 在迁移 lambda 开头调用 `old.enter_object()` |
| 先注册版本再保存再加载 | saved_cv == current_cv，不触发迁移 | 模拟真实场景：先存 v1 存档，再升级版本注册迁移，后加载 |
| 对二进制格式调用 `set_compression` | 不生效（仅 JSON 路径压缩） | 改用外部压缩或在 save 后手动压缩 |
| `replace` 模式加载到含信号 manager | 旧实体被销毁触发信号 | 信号触发属预期行为，注意信号槽清理 |
| `create_writer`/`create_reader` 后未调用 `destroy_*` | 内存泄漏 | 用完立即调用对应 destroy 方法 |
| Protobuf/FlatBuffer 字段按写入顺序编号 | 按字段名读取失败 | 用 `f1`/`f2`/... 等编号键迭代 |
| Protobuf/FlatBuffer save/load 用不同 `Ts...` 顺序 | 字段编号错位，数据错乱 | save 和 load 必须用相同的 `Ts...` 类型和顺序 |
| `read_string_view` 返回的视图在原缓冲区释放后失效 | 悬空指针 | 视图生命周期绑定到原 `string`，需保留原数据 |

---

# 二、宏配置

## 13. 可选宏配置

### 13.1 栈内存控制（`config/ecs_config.hpp`）

嵌入式 / RTOS 环境通过 `LCF_MINIMAL_STACK` 关闭栈分配，将基数排序的大数组从栈分配切换到堆分配。桌面环境默认 `0`，保留栈分配。

| 宏 | 默认值 | 说明 |
|------|--------|------|
| `LCF_MINIMAL_STACK` | `0` | `1` 关闭栈分配：基数排序直方图（16KB×2）与 `count_stack`（16KB×2）改走堆分配 |

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

### 13.2 void_any 存储策略（`config/void_any_config.hpp`）

影响 `void_any` 的存储策略与内存分配方式。

| 宏 | 说明 |
|------|------|
| `VOID_ANY_ENABLE_SSO` | 启用 void_any 小对象存储（SSO），小对象内联存储 |
| `VOID_ANY_ENABLE_MEMORY_POOL` | 启用 void_any 内存池，使用 `memory_pool` 替代 `::operator new` |
| `VOID_ANY_USE_LAYERED_ALLOCATOR` | 启用分层分配器：小对象（≤128B）走 slab，大对象走 TLSF（优先级高于 `VOID_ANY_ENABLE_MEMORY_POOL`） |
| `VOID_ANY_SSO_BUFFER_SIZE` | SSO 缓冲区大小（默认 56 字节） |
| `VOID_ANY_SSO_ALIGNMENT` | SSO 对齐（默认 8 字节） |
| `VOID_ANY_MEMORY_POOL_NOT_ENABLED` | 禁用内存池（与 `VOID_ANY_ENABLE_MEMORY_POOL` 互斥） |
| `VOID_ANY_SSO_NOT_ENABLED` | 禁用 SSO（与 `VOID_ANY_ENABLE_SSO` 互斥） |

### 13.3 反射模块配置（`config/reflect_config.hpp`）

影响反射模块的类型注册上限与单类型字段/方法数组大小。

| 宏 | 默认值 | 说明 |
|------|--------|------|
| `REFLECT_MAX_FIELDS` | `64` | 聚合类型字段遍历上限，支持 16/32/64/128/256 |
| `MAX_FIELDS_PER_TYPE` | `256` | 单类型 fields 数组大小 |
| `MAX_METHODS_PER_TYPE` | `256` | 单类型 methods 数组大小 |
| `MAX_TYPE_ID` | `65536` | storage 类型槽位上限，限制可注册类型总数 |

```cpp
// config/reflect_config.hpp
#define REFLECT_MAX_FIELDS 128
#define MAX_FIELDS_PER_TYPE 256
#define MAX_METHODS_PER_TYPE 256
#define MAX_TYPE_ID 65536
```

### 13.4 utf8pp 内存分配器配置（`config/utf8pp_config.hpp`）

`utf8pp` 已实现完整内存分配架构（SSO + 3 级增长 + 堆管理）。默认关闭，堆路径使用 `std::malloc/std::free`；启用后堆路径接入自研分配器。

| 宏 | 默认值 | 说明 |
|------|--------|------|
| `UTF8PP_ENABLE_ALLOCATOR` | `0` | `1` 启用自研分配器接入 utf8pp 堆路径 |
| `UTF8PP_ALLOCATOR_TYPE` | `UTF8PP_ALLOC_MEMORY_POOL` | 启用时的分配器类型：`UTF8PP_ALLOC_MEMORY_POOL`（TLSF 内存池）/ `UTF8PP_ALLOC_LAYERED`（分层分配器：小对象 slab + 大对象 TLSF） |

```cmake
# 启用 utf8pp 自研内存池
target_compile_definitions(my_target PRIVATE UTF8PP_ENABLE_ALLOCATOR=1)

# 启用分层分配器
target_compile_definitions(my_target PRIVATE UTF8PP_ENABLE_ALLOCATOR=1 UTF8PP_ALLOCATOR_TYPE=UTF8PP_ALLOC_LAYERED)
```

### 14.4 配置示例

```cpp
// config/void_any_config.hpp

// 启用内存池
#define VOID_ANY_ENABLE_MEMORY_POOL

// 启用分层分配器（小对象走 slab, 大对象走 TLSF, 优先级高于 memory_pool）
#define VOID_ANY_USE_LAYERED_ALLOCATOR

// 启用小对象存储
#define VOID_ANY_ENABLE_SSO

// SSO 缓冲区大小
#define VOID_ANY_SSO_BUFFER_SIZE 56

// SSO 对齐
#define VOID_ANY_SSO_ALIGNMENT 8
```

### 13.5 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `VOID_ANY_SSO_ALIGNMENT` 设为 32 | `sizeof(void_any)` 会改变 | 保持默认 8 |
| 同时定义 `VOID_ANY_ENABLE_MEMORY_POOL` 和 `VOID_ANY_MEMORY_POOL_NOT_ENABLED` | 互斥宏冲突 | 二选一 |
| `LCF_MINIMAL_STACK=1` 后期望排序性能不变 | 堆分配有额外开销 | 嵌入式场景排序非热路径，可接受 |
| 在 `config/` 文件夹外查找配置文件 | `ecs_config.hpp` 和 `void_any_config.hpp` 均在 `include/config/` | include 路径为 `"config/ecs_config.hpp"` 和 `"config/void_any_config.hpp"` |

---

# 三、各种模块

## 14. operating_message — 操作消息

记录操作结果（成功/失败）和调试信息。核心特性：

- **值语义返回**：`single_class_set` / `manager` 的 `add` / `add_batch` / `hard_remove` / `soft_remove` 均按值返回 `operating_message`。容器自身不持有 `operating_message` 成员，每次操作返回独立结果
- **粘性 false 语义**：单个返回值对象一旦失败就保持 false，只有 `reset()` 能恢复
- **全局开关**：`ecs_debug_messages()` 运行时控制是否写入字符串
- **日志级别过滤**：`msg_level` 枚举 + `min_level_` 表驱动过滤，被过滤的级别跳过全部格式化
- **类型特化写入**：`write_message` 对整型/浮点走 `std::to_chars`，对字符串走 `append`，其他类型走 `std::format_to`

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

### 接口机制

- **`write_message`（整型参数）**：走 `std::to_chars` 路径
- **`write_message_fmt`**：若格式串仅含简单 `{}` 占位符（无 `{{` `}}` 转义、无 `{:spec}` 复杂格式），走 `append_arg` 的 `to_chars` / 字符串 append 路径；复杂格式走 `std::format_to` 通用路径
- **级别过滤快速路径**：被过滤的级别仅做一次整数比较 + reset
- **`reserve`**：循环 `reset()` 不释放容量，首次写入后无需重分配；`reserve` 用于避免首次分配

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 依赖 `write_message(true)` 恢复失败状态 | 粘性 false 语义，成功后不会恢复 | 调用 `reset()` 显式恢复 |
| 在 Release 构建中依赖 `read_message()` | 全局开关关闭时字符串为空 | 使用 `operator bool()` 判断成败，而非消息内容 |
| 忘记检查 `operator bool()` | 操作失败被静默忽略 | 每次关键操作后检查 `if (!msg) { ... }` |
| 高频日志用 `{:08x}` 等复杂格式 | 走 `std::format_to` 通用路径 | 高频路径仅用 `{}` 简单占位符以走 fast path |
| 默认级别记录所有 debug 日志 | Release 中 debug 日志拖累性能 | `set_min_level(msg_level::warn)` 过滤低级别 |

---

## 15. class_pool\<T> — 核心容器

支持密集与稀疏两种存储模式的容器，替代 `std::vector`。

**两种模式：**

| 模式 | 触发条件 | 迭代行为 |
|------|---------|---------|
| **dense（密集）** | 无空洞（所有槽位均已构造） | 线性扫描 `[0, size())` |
| **sparse（稀疏）** | 存在空洞（有未构造的槽位） | 自动跳过未构造槽位，仅遍历已构造元素 |

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
| `get(size_t)` | 等价于 `operator[]`（无边界检查，保留为模板方法便于重载） |
| `get(size_t, size_t error_index)` | 越界保护访问：`index >= size()` 时改访问 `error_index` 位置的元素 |
| `front()` | 首元素引用 |
| `back()` | 尾元素引用 |
| `data()` | 原始数据指针 |
| `span()` | 返回 `std::span<T>` |
| `span() const` | 返回 `std::span<const T>` |

### 容量

| 接口 | 说明 |
|------|------|
| `size()` | 已使用大小 |
| `capacity()` | 总容量 |
| `sparse_capacity()` | 稀疏模式容量（同 capacity） |
| `empty()` | 是否为空 |
| `count()` | 已构造元素数 |
| `valid()` | 是否已分配 |
| `size_bytes()` | 已使用字节数 |
| `capacity_bytes()` | 总容量字节数 |

### 修改器

| 接口 | 说明 |
|------|------|
| `emplace_back(Args...)` | 尾部构造元素 |
| `push_back(const T&)` | 尾部拷贝追加（容量不足自动扩容，trivially copyable 走 memcpy） |
| `push_back(T&&)` | 尾部移动追加（容量不足自动扩容，trivially copyable 走 memcpy） |
| `push_back_unchecked(const T&)` | 尾部拷贝追加（调用方保证容量足够，trivially copyable 走 memcpy） |
| `push_back_unchecked(T&&)` | 尾部移动追加（调用方保证容量足够，trivially copyable 走 memcpy） |
| `emplace_back_unchecked(Args...)` | 尾部原地构造（仅 dense 模式可用，调用方保证容量足够） |
| `emplace_back_dense_unchecked(Args...)` | 尾部原地构造（仅 dense 模式可用） |
| `append_n(n, const T&)` | 批量追加 n 个 value 副本 |
| `emplace(pos, Args...)` | 在指定位置插入（移动后续元素） |
| `emplace_at(index, Args...)` | 任意位置构造（get-or-create：已存在则返回现有值，不覆盖） |
| `sparse_emplace_at(index, Args...)` | 任意位置构造（insert-or-assign：已存在则覆盖） |
| `sparse_erase_at(index)` | 稀疏删除（不移动元素，产生空洞） |
| `soft_sparse_delete(index)` | 软删除单个（保留对象内存，可填洞复用） |
| `soft_dense_delete(start, end)` | 软删除范围（保留对象内存） |
| `erase(pos)` | 删除指定位置元素（移动后续元素填补） |
| `erase(first, last)` | 删除范围元素 |
| `pop_back()` | 删除尾部元素 |
| `clear()` | 清空所有元素 |
| `increase_capacity(capacity)` | 扩容（只扩容不缩容，不增加元素） |
| `increase_capacity(capacity, value)` | 扩容并填充值到新槽位（只扩容不缩容，`capacity <= size` 时直接返回不销毁任何对象） |
| `reduce_capacity(capacity)` | 缩容（截断超出元素） |
| `reduce_capacity(capacity, dst)` | 缩容，超出元素移至 dst |
| `shrink_to_fit()` | 缩容至 `size()` |
| `reserve_exact(capacity)` | 精确扩容（分配到精确大小，不增加元素） |
| `fill_bulk(value, start, count)` | 从 `start` 开始填充 `count` 个 `value`（自动扩容；非平凡类型会先析构已有对象再重新构造） |
| `prepare_dense(new_size)` | 预备密集模式到 `new_size`（扩容 + 默认构造新槽位 + 标记为已分配，使容器进入密集模式） |
| `swap(other)` | 交换两个容器 |

### 状态查询

| 接口 | 说明 |
|------|------|
| `is_constructed_at(index)` | 检查指定位置是否已构造 |
| `is_dense()` | 是否处于密集模式（无空洞） |
| `invalidate_count_cache()` | 使 count 缓存失效 |

### 各操作对 contiguity 的影响

| 操作 | 对 contiguity 的影响 |
|------|---------------------|
| `emplace_back()` / `push_back()` | 保持连续 |
| `emplace(pos)` / `insert(pos)` | 保持连续（元素右移） |
| `erase(pos)` / `erase(first,last)` | 保持连续（元素左移） |
| `pop_back()` | 保持连续 |
| `clear()` | 重置为连续 |
| `sparse_erase_at(index)` | **产生空洞** → 变稀疏 |
| `emplace_at(index)` | **可能填充空洞** → 可能变连续 |
| `sparse_emplace_at(index)` | **可能填充空洞** → 可能变连续 |
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
| `begin()` / `end()` | sparse-aware 正向迭代器（dense 模式直接指针遍历，sparse 模式自动跳过未构造槽位） |
| `cbegin()` / `cend()` | const 版本 |
| `rbegin()` / `rend()` | 反向迭代器（bidirectional，sparse 模式同样自动跳过未构造槽位） |
| `crbegin()` / `crend()` | const 反向版本 |

### 自由函数

| 接口 | 说明 |
|------|------|
| `swap(class_pool&, class_pool&)` | 交换两个容器 |

### 使用

```cpp
class_pool<int> pool;
pool.emplace_back(10);
pool.emplace_back(20);

// push_back 拷贝/移动追加 (trivially copyable 走 memcpy 快路径)
int v = 30;
pool.push_back(v);                  // 拷贝追加
pool.push_back(std::move(v));       // 移动追加

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

// unchecked 快速追加（仅 dense 模式，调用方保证容量足够）
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
reserved.increase_capacity(10, 0);  // 扩容 size 到 10 并填充 0（只扩容不缩容）

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
```

### 应该用什么操作？

| 场景 | 推荐操作 | 原因 |
|------|---------|------|
| 尾部追加元素（已有值对象） | `push_back(value)` | O(1)，保持连续，trivially copyable 走 memcpy |
| 尾部追加元素（直接构造） | `emplace_back(args...)` | O(1)，保持连续，原地构造避免临时对象 |
| 任意位置插入/删除并保持连续 | `emplace(pos)` / `erase(pos)` | 移动后续元素，O(n)，保持连续 |
| 稀疏数组（大索引跳跃） | `emplace_at()` / `sparse_erase_at()` | O(1)，不移动其他元素，但产生空洞 |
| 批量填充已知索引 | `emplace_at()` | 填充空洞后自动切回连续 |
| 删除整个容器 | `clear()` | 重置为连续 |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `sparse_erase_at()` 后仍期望连续迭代 | 产生空洞，迭代变稀疏模式 | 用 `emplace_at()` 填充空洞，或用 `erase()` 替代 |
| 频繁 `sparse_erase_at()` + `emplace_at()` 来回切换 | 每次切换触发模式扫描 | 批量操作，或统一使用 `erase()`/`emplace()` 保持连续 |
| `emplace_at()` 在远超 `size()` 的索引上构造 | 中间留大量未初始化槽位，`size()` 暴增 | 用 `increase_capacity(n, value)` 预填充，或改用 `sparse_emplace_at()` |
| 在 sparse 模式下使用 `data()` + `span()` 做线性遍历 | 未初始化槽位包含垃圾数据 | 始终通过迭代器遍历，或先确认 `is_dense()` 为 true |
| `increase_capacity(n)` 期望精确分配到 n | 实际容量可能大于 n | 精确分配用 `reserve_exact(n)` |
| `emplace_at()` 期望覆盖已有值 | `emplace_at` 是 get-or-create，不覆盖 | 使用 `sparse_emplace_at()` 实现 insert-or-assign |
| 在 sparse 模式下使用 `push_back_unchecked` / `emplace_back_unchecked` | 不更新 `count()` 缓存 | 用 `push_back()` / `emplace_back()` 自动维护缓存，或手动 `invalidate_count_cache()` |
| 软删除后对象仍占内存 | `soft_sparse_delete` / `soft_dense_delete` 保留对象不析构 | 用 `sparse_erase_at()` 真正析构，或 `emplace_at()` / `fill_the_hole()` 填洞复用 |

### fill_the_hole — 填洞或追加

`fill_the_hole` 优先填第一个空洞（最低索引），无洞则 `emplace_back` 末尾追加。

#### 机制

| 操作 | 行为 |
|------|------|
| `fill_the_hole(args...)` | 无空洞 → `emplace_back`；有空洞 → 填最低索引空洞 |
| `sparse_erase_at(idx)` | 产生空洞 |

- 填洞顺序：**最低索引优先**（非 LIFO）

#### 接口

| 接口 | 说明 |
|------|------|
| `fill_the_hole(args...)` | 填第一个空洞或末尾追加，返回 `T&` |
| `fill_the_hole_at(args...)` | 同 `fill_the_hole` 语义，但返回被填补位置的索引 `size_t`（填洞返回洞索引，追加返回末尾索引），调用方可通过 `operator[](idx)` 访问元素 |

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
| 对已删除位置重复 `sparse_erase_at` | 已删除位置不重复计数 | 删除前可 `is_constructed_at` 检查 |

---

## 15.5 class_pool 视图

`class_pool<T>` 视图接口位于独立头文件 `include/part/class_pool_views.hpp`，全局命名空间（与 `dense` / `class_pool` 保持一致）。设计原则：
- **不修改原容器**：仅依赖 `class_pool<T>` 公开 API
- **双路径策略**：`is_dense()` 走连续内存快路径（SIMD/memcpy 友好），稀疏模式复用 `basic_iterator`（AVX2 位图扫描）
- **零分配**：所有视图为 POD 结构或纯函数
- **全 `noexcept`**：与原容器约束一致

### 视图分类表

| 分类 | 接口 | 说明 |
|------|------|------|
| **子范围** | `subspan(p, off, cnt)` / `subspan(p, off)` | 返回 `std::span<T>`，密集模式零开销切片（与 `dense::subspan` / `std::span::subspan` 命名一致） |
|  | `first(p, n)` / `last(p, n)` | 前/后 n 个元素的 span |
|  | `first_fixed<T, N>(p)` / `last_fixed<T, N>(p)` | 编译期固定长度 `std::span<T, N>` |
| **反向** | `reverse_for_each(p, f)` | 反向遍历，复用 `rbegin()/rend()` |
| **步进** | `strided_span_view(p, start, step, cnt)` | 返回 POD 视图结构 `pool_strided_span<T>`（持有 class_pool 指针 + 步进参数，与 `dense::strided_span` 区分） |
|  | `strided_for_each(p, start, step, f)` | 运行时步长遍历 |
|  | `strided_for_each<T, Step>(p, f)` | 编译期步长；`Step=1` 退化为 `for_each` 快路径 |
| **变换** | `transform_for_each(p, tr, con)` | 融合 transform + consume，避免中间临时容器 |
|  | `transform_to<T, R>(p, dst, n, f)` | 变换写入目标裸指针 |
| **过滤** | `find(p, v)` / `find_if(p, pred)` / `find_if_not(p, pred)` | 线性查找，返回指针 |
|  | `contains(p, v)` | 存在性检查 |
|  | `count_if(p, pred)` | 条件计数 |
|  | `filter_for_each(p, pred, f)` | 过滤遍历，零分配 |
|  | `filter_indices_to(p, dst, pred)` | 输出满足条件的索引到 `class_pool<size_t>` |
| **规约** | `reduce(p, f, init)` | 顺序规约 |
|  | `reduce_pairwise(p, f, init)` | 密集模式 ivdep 提示向量化 |
|  | `min_element(p)` / `max_element(p)` / `minmax_element(p)` | 极值查找 |
|  | `sum(p)` | 算术求和（仅算术类型，ivdep） |
|  | `dot_product(p, other, n)` | 点积（密集模式 ivdep） |
| **窗口/分块** | `for_each_window<T, N>(p, f)` | 滑动窗口遍历（密集模式连续切片） |
|  | `for_each_chunk<T, N>(p, f)` | 不重叠分块遍历 |
|  | `window_span<T, N>(p, off)` / `chunk_span<T, N>(p, idx)` | 返回 `std::span<T, N>` |
| **枚举** | `for_each_enumerated(p, f)` | `(index, value)` 同步遍历 |
| **双容器** | `for_each_zip(a, b, f)` | 双 `class_pool` 同步遍历（按活跃元素） |
|  | `for_each_zip(a, ptr, n, f)` | `class_pool` + 裸指针同步 |
|  | `zip_with_to<T, U, R>(a, b, dst, n, f)` | 双容器变换写入目标 |
|  | `equal(a, b)` / `equal(a, ptr, n)` / `equal(a, span)` | 相等性比较（与 `dense::equal` 命名一致；密集 + trivially copyable 走 `memcmp` 快路径） |
| **SIMD/对齐** | `aligned_data(p)` / `aligned_span(p)` | 返回对齐裸指针 / span |
|  | `simd_for_each(p, f)` | trivially copyable 且 `sizeof(T)≤32` 走 SIMD 路径，稀疏退化为 `for_each` |
|  | `unaligned_tail_offset(p)` | AVX2 YMM 无法处理的尾部偏移 |
| **拷贝/移动** | `copy_to(p, dst, n)` / `copy_to(p, span)` | trivially copyable 走 `memcpy` |
|  | `move_to(p, dst, n)` | 移动写入 |
|  | `reverse_copy_to(p, dst, n)` | 反向拷贝 |
| **class_pool 独有** | `compact_to(p, dst, n)` | 压缩稀疏池为密集数组（消除空洞），返回写入元素数 |
|  | `live_count(p)` | 活跃元素数（语义等价 `p.count()`） |
|  | `holes_count(p)` | 空洞数 = `p.size() - p.count()` |

### 使用示例

```cpp
#include "include/part/class_pool_views.hpp"
// 视图接口位于全局命名空间, 无需 using namespace

class_pool<POD32> pool;
for (size_t i = 0; i < 1000; ++i) pool.push_back_unchecked({static_cast<float>(i)});

// A. 子范围遍历
auto sp = subspan(pool, 100, 50);
for (auto& v : sp) { /* ... */ }

// C. 步进遍历（步长 4）
strided_for_each<POD32, 4>(pool, [](POD32& v) { /* ... */ });

// E. 过滤查找
POD32 target{42.0f};
POD32* p = find(pool, target);
size_t n = count_if(pool, [](const POD32& v) { return v.a[0] > 0; });

// F. 规约
POD32 sum = reduce(pool, [](POD32 acc, const POD32& v) -> POD32 { /* ... */ }, POD32{});

// L. 压缩稀疏池为密集数组（ECS 视图重建）
pool.sparse_erase_at(5);  // 制造空洞
POD32* compact = /* ... 分配内存 ... */;
size_t live = compact_to(pool, compact, pool.size());
// compact[0..live) 为连续活跃元素
```

### 稀疏模式行为

稀疏模式（`is_dense() == false`）下视图行为：
- **子范围/窗口/分块**：直接切片，**不跳过空洞**，调用方需自行 `is_constructed_at(i)` 检查
- **步进遍历**：步进槽位（非活跃元素），越过空洞时自动跳过
- **过滤/查找/规约/zip**：复用 `basic_iterator`，**自动跳过空洞**（仅遍历活跃元素）
- **SIMD 遍历**：退化为 `for_each`（仍走位图扫描）
- **compact_to**：按活跃顺序压缩写入（消除空洞）

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 稀疏模式用 `subspan` 后直接遍历 | 包含未活跃槽 | 用 `for_each` / `filter_for_each` 走位图扫描 |
| 期望 `strided_for_each<1>` 与 `for_each` 性能不同 | 已退化为 `for_each` | 两者性能等价 |
| `simd_for_each` 用于稀疏模式 | 退化为 `for_each`，无 SIMD 收益 | 仅密集模式有 SIMD 路径 |
| `compact_to` 后期望源池被清空 | 仅拷贝，源池不变 | 调用方自行 `clear()` |
| 视图持有期间修改容器 | move/swap 后内联位图指针失效 | 视图生命周期 = 容器稳定期 |
| 期望 `last_fixed<N>` 越界返回空 span | `std::span<T, N>` 无法默认构造 | 返回 `count=0` 的 span，调用方需检查 `size()` |

---

## 16. void_any — 类型擦除存储

类型擦除容器，保持 `void*` 设计理念，通过位编码将元信息打包到单个 64 位字中，减少内存访问。支持 SSO 和内存池（通过宏配置）。

### 存储模式

64 位平台下，`void_any` 根据类型特征自动选择三种存储模式之一：

| 模式 | 适用条件 | 特征 |
|------|---------|------|
| inline 编码 | SSO + trivially_copyable + trivially_destructible | 无 vtable 访问，type_id 通过编译期类型标签比较 |
| SSO vtable | SSO 但非 trivially_copyable/destructible | 内联存储，通过 vtable 调用 copy/move/destroy |
| heap vtable | 超出 SSO 容量 | 堆分配，通过 vtable 管理 |

inline 编码模式将 `element_size`、`trivially_destructible`、`trivially_copyable` 等信息编码到 vtable 指针的空闲位中，对 trivial 类型完全跳过 vtable 访问，copy/move 操作仅执行分级内存复制。

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
| `type_id()` | 获取类型 ID（空值返回 -1；inline 编码模式返回类型标签派生值，仅用于同类型一致性比较） |
| `get_ptr<T>()` | 获取指针（带类型检查，不匹配返回 nullptr） |
| `get_ptr<T>() const` | const 版本 |
| `fast_get_ptr<T>()` | 快速获取（跳过 type_id 检查） |
| `fast_get_ptr<T>() const` | const 版本 |
| `get_ptr_unchecked<T>()` | 无检查获取（不验证 has_value 和 type_id） |
| `get_ptr_unchecked<T>() const` | const 版本 |
| `get<T>()` | 获取值副本（空值或类型不匹配返回默认构造） |
| `get_void()` | 获取 `void*`（空值返回 nullptr） |
| `get_void() const` | const 版本 |
| `copy_from<T>(const T&)` | 编译期已知 T 的拷贝赋值 |
| `move_from<T>(T&&)` | 编译期已知 T 的移动赋值 |
| `has_value()` | 是否有值 |
| `reset()` | 清空（析构并置空） |

> 注：`type_id()` 在 inline 编码模式下返回类型标签指针派生值，不等于 `type_id::get_type_id<T>()`。需要类型匹配判断时使用 `get_ptr<T>()`。

### 使用

```cpp
void_any a(42);
a.has_value();                 // true
a.type_id();                   // 类型标识 (空值返回 -1)

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

// void* 访问
void* vp = a.get_void();       // 返回 void* (空值返回 nullptr)

// 编译期已知 T 的高性能接口
void_any c;
c.copy_from(42);               // 编译期已知 int, 跳过 vtable 间接调用
c.move_from(std::string("x")); // 编译期已知 string

// 类型一致性比较 (inline 编码模式)
void_any x(1), y(2);
x.type_id() == y.type_id();    // true, 同类型返回相同值
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 使用 `get_ptr_unchecked` 前不检查 `has_value()` 和类型 | 返回悬垂指针，未定义行为 | 仅在确定类型和值存在时使用，否则用 `get_ptr<T>()` |
| 依赖 `get<T>()` 返回默认值来判断类型 | 默认构造值可能与实际值相同 | 先用 `get_ptr<T>()` 检查指针是否为空 |
| 移动后继续使用 | 移动后源对象为空 | 移动后仅可调用 `reset()` 或重新赋值 |
| 在 `set()` 之前访问 | `has_value()` 为 false，get_ptr 返回 nullptr | 先 `set()` 或构造时传值 |
| 用 `type_id() == type_id::get_type_id<T>()` 判断类型 | inline 编码模式下两者不相等 | 用 `get_ptr<T>() != nullptr` 判断类型 |

---

## 17. type_id — 类型ID

为每种类型分配唯一整数 ID（编译时确定，线程安全）。

### 接口

| 接口 | 说明 |
|------|------|
| `type_id::get_type_id<T>()` | 获取类型 T 的唯一 ID（静态函数，线程安全） |
| `type_id::current_max_id()` | 返回当前已分配的最大 type_id（静态函数） |

### 使用

```cpp
int id1 = type_id::get_type_id<int>();
int id2 = type_id::get_type_id<double>();
assert(type_id::get_type_id<int>() == id1);  // 同类型 ID 相同

int max_id = type_id::current_max_id();  // 已分配的最大 ID
```

---

## 18. id_allocation\<T> — ID分配器

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

## 19. memory_pool — 内存池

基于 TLSF（Two-Level Segregated Fit）算法的分桶式内存池，减少频繁 malloc/free 开销。内部维护 chunk 预分配池，`reset()` 和 `reduce_capacity()` 释放的 chunk 优先归还预分配池，后续 `allocate` 优先从池中取用，减少系统调用。预分配池容量有限，超限的 chunk 归还系统。

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
| `allocate_sized<Size>()` | 模板化分配，Size 为编译期大小，小块走快路径 |
| `deallocate(void* ptr)` | 释放内存（自动合并相邻块） |
| `deallocate(void* ptr, size_t size)` | 释放内存（size 必须与 allocate 一致） |
| `deallocate_sized<Size>(void* ptr)` | 模板化释放，Size 为编译期大小，与 `allocate_sized<Size>()` 配对 |
| `construct<T>(Args...)` | 分配并构造对象（内部走 `allocate_sized<sizeof(T)>()`） |
| `destroy<T>(T* ptr)` | 析构并释放对象（内部走 `deallocate_sized<sizeof(T)>(ptr)`） |
| `total_allocated()` | 已分配总量 |
| `total_used()` | 已使用量 |
| `chunk_size()` | 获取块大小 |
| `empty()` | 是否空闲（`total_used_ == 0`） |
| `owns(const void* ptr)` | 判断指针是否属于本池 |
| `stats()` | 返回 `pool_stats` 统计信息 |
| `iterate_free(Fn&& fn)` | 遍历空闲块，回调签名 `void(void* data_ptr, size_t block_size)` |
| `increase_capacity(size_t size)` | 扩容（只扩容不缩容） |
| `reduce_capacity(size_t target)` | 缩容（只缩容不扩容，释放空闲 chunk 直到总量 <= target） |
| `reset()` | 释放所有内存块，chunk 归还预分配池（不归还系统），回到初始状态 |

> 注：`memory_pool` 禁止拷贝。

### 使用

```cpp
memory_pool pool;
void* p = pool.allocate(64);
pool.deallocate(p);                  // unsized deallocate

void* q = pool.allocate(64);
pool.deallocate(q, 64);              // sized deallocate, size 与 allocate 一致

// sized 模板路径: Size 编译期已知, 小块走快路径, 与 allocate_sized<Size>() 配对
void* r = pool.allocate_sized<64>();
pool.deallocate_sized<64>(r);

std::string* s = pool.construct<std::string>("hello");
pool.destroy(s);

pool.increase_capacity(1024 * 1024);  // 扩容至 1MB
pool.reduce_capacity(4096);          // 缩容，释放空闲 chunk 至总量 <= 4096
pool.reset();                        // 释放所有，chunk 归还预分配池,回到初始状态

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
| 期望 `reset()` 立即归还内存给系统 | chunk 优先进入预分配池，析构时才归还系统 | 如需立即归还，销毁 pool 对象或用新对象替换 |

### arena_allocator — 线性 bump 分配器

线性分配器，无单个 deallocate，仅 `reset()` 整体回收。两种模式：自有内存（析构释放）/ 借用外部 buffer（不持有所有权）。适合 command_buffer 等批量分配、整体回收场景。

#### 接口

| 接口 | 说明 |
|------|------|
| `arena_allocator()` | 默认构造，空 |
| `arena_allocator(size_t capacity)` | 自有模式：分配 capacity 字节，析构释放 |
| `arena_allocator(void* buffer, size_t size)` | 借用模式：使用外部 buffer，不分配不释放 |
| `arena_allocator(arena_allocator&&)` | 移动构造（原对象置空防 double free） |
| `operator=(arena_allocator&&)` | 移动赋值 |
| `allocate(size_t n, size_t align = 16)` | 分配内存，对齐上限 64，溢出返回 nullptr |
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
| 期望 `allocate(n, 128)` 对齐 | 对齐上限 64 | 对齐上限 64，超过不保证 |

### slab_allocator — 固定块对象池

固定块大小对象池，O(1) 分配/释放。适合 void_any 小对象（≤128B）高频分配/释放。

#### 接口

| 接口 | 说明 |
|------|------|
| `slab_allocator(size_t block_size, size_t alignment = 16, size_t blocks_per_chunk = 256)` | 构造，block_size 向上对齐到 alignment |
| `slab_allocator(slab_allocator&&)` | 移动构造 |
| `operator=(slab_allocator&&)` | 移动赋值 |
| `allocate()` | 分配一个块，无空闲块则自动扩容 |
| `deallocate(void* p)` | 释放块 |
| `owns(const void* p)` | 判断指针是否属于本分配器 |
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

## 20. dense\<T> — 通用密集容器

`#include "part/dense.hpp"`，无命名空间。所有接口 `noexcept`。彻底替代 `std::vector` 的通用密集容器，是 `class_pool` 密集模式的独立容器形式。

### 构造与赋值

| 接口 | 说明 |
|------|------|
| `dense()` | 默认构造（空） |
| `explicit dense(size_t capacity)` | 预留容量（仅分配内存，`size()` 为 0） |
| `dense(size_t count, const T& value)` | `count` 个 `value` 拷贝 |
| `dense(InputIt first, InputIt last)` | 迭代器范围构造 |
| `dense(std::initializer_list<T> init)` | 初始化列表构造 |
| `dense(const dense& other)` | 拷贝构造（深拷贝） |
| `dense(dense&& other)` | 移动构造 |
| `operator=(const dense&)` | 拷贝赋值（深拷贝） |
| `operator=(dense&&)` | 移动赋值 |

### 元素访问

| 接口 | 说明 |
|------|------|
| `operator[](size_t index)` | 下标访问（无边界检查） |
| `get(index)` / `get(index, error_index)` | `get` 等价 `operator[]`；带 `error_index` 版本越界时回退访问 `error_index` |
| `front()` / `back()` | 首尾元素 |
| `data()` | 原始指针 |
| `span()` / `span() const` | `std::span<T>` 视图 |

### 容量与状态

| 接口 | 说明 |
|------|------|
| `size()` / `count()` | 元素数（两者等价） |
| `capacity()` | 容量 |
| `empty()` | 是否为空 |
| `valid()` | `data() != nullptr` |
| `size_bytes()` / `capacity_bytes()` | 已用/容量字节数 |
| `max_size()` | 理论最大元素数 |

### 修改器

| 接口 | 说明 |
|------|------|
| `push_back(const T&)` | 尾部拷贝追加（容量不足自动扩容，trivially copyable 走 memcpy） |
| `push_back(T&&)` | 尾部移动追加（容量不足自动扩容，trivially copyable 走 memcpy） |
| `emplace_back(Args&&...)` | 尾部原地构造（容量不足自动扩容） |
| `push_back_unchecked(const T&)` | 尾部拷贝追加（调用方保证容量足够，trivially copyable 走 memcpy） |
| `push_back_unchecked(T&&)` | 尾部移动追加（调用方保证容量足够，trivially copyable 走 memcpy） |
| `emplace_back_unchecked(Args&&...)` | 尾部原地构造（调用方保证容量足够） |
| `emplace_back_dense_unchecked(Args&&...)` | 等价 `emplace_back_unchecked`（dense 路径） |
| `append_n(size_t n, const T& value)` | 批量追加 `n` 个 `value` |
| `append_bulk(const T* src, size_t count)` | 批量拷贝追加 |
| `append_bulk_move(T* src, size_t count)` | 批量移动追加 |
| `append_incrementing(count, counter)` | 批量追加递增值（counter 起始） |
| `append_generated(count, F&& generator)` | 批量追加生成器产生值 |
| `fill_bulk(value, start, count)` | 从 `start` 开始填充 `count` 个 `value` |
| `emplace(pos, args...)` / `insert(pos, value)` | 任意位置插入 |
| `erase(pos)` / `erase(first, last)` | 任意位置删除 |
| `pop_back()` | 尾部删除 |
| `clear()` | 清空（`size=0`，capacity 保留） |
| `swap(dense&)` / 自由函数 `swap(a, b)` | 交换 |

### 容量控制

| 接口 | 说明 |
|------|------|
| `increase_capacity(new_capacity)` | 扩容到 `new_capacity`（只扩容不缩容，不改变 `size`） |
| `increase_capacity(new_capacity, value)` | 扩容并以 `value` 填充新增位置（只扩容不缩容，`new_capacity <= size` 时直接返回不销毁任何对象） |
| `reserve_exact(new_capacity)` | 精确预留容量（强制增长） |
| `shrink_to_fit()` | 缩容到 `size`（释放多余内存） |
| `reduce_capacity(new_capacity)` | 缩容到 `new_capacity`（只减不增，超出部分截断） |
| `reduce_capacity(new_capacity, dense<T>& dst)` | 缩容并将截断元素迁移到 `dst` |

### 迭代器

| 接口 | 说明 |
|------|------|
| `begin()` / `end()` | 正向迭代器（裸指针） |
| `cbegin()` / `cend()` | const 正向迭代器 |
| `for_each(F&& f)` / `for_each(F&& f) const` | 遍历所有元素，调用 `f(v)` |

### 反向迭代

| 接口 | 说明 |
|------|------|
| `rbegin()` / `rend()` | 反向迭代器 |
| `rbegin() const` / `rend() const` | const 反向迭代器 |
| `crbegin()` / `crend()` | const 反向迭代器（仅 const 重载） |
| `reverse_for_each(F&& f)` / `reverse_for_each(F&& f) const` | 反向遍历，调用 `f(v)` |

### 子范围视图

零分配返回 `std::span`，仅切片不改数据。

| 接口 | 说明 |
|------|------|
| `subspan(offset, count)` | 返回 `[offset, offset+count)` 的 span，自动截断到 `size()` |
| `subspan(offset)` | 返回 `[offset, size())` 的 span |
| `first(n)` | 前 `n` 个元素 |
| `last(n)` | 后 `n` 个元素 |
| `first_fixed<N>()` | 前 `N` 个元素，编译期固定长度 span（`std::span<T, N>`） |
| `last_fixed<N>()` | 后 `N` 个元素，编译期固定长度 span |

所有接口均提供 const 重载。模板方法调用需 `template` 关键字：`d.template first_fixed<8>()`。

### 步进视图

按固定步长跳跃遍历，零分配 POD 视图。

| 接口 | 说明 |
|------|------|
| `strided_span_view(start, step, count)` | 返回 `strided_span<T>`，持有 `{指针, 步长, 数量}` |
| `strided_for_each(start, step, F&& f)` | 运行时步长遍历，调用 `f(v)` |
| `strided_for_each<Step>(F&& f)` | 编译期步长遍历，`Step=1` 回退到 `for_each` 快路径 |

`strided_span<T>` 自身提供 `begin()/end()` 迭代器、`for_each(F&&)`、`operator[]`、`size()`、`data()` 等。

### 变换视图

融合变换与消费，避免中间临时数组。

| 接口 | 说明 |
|------|------|
| `transform_for_each(FTransform&& tr, FConsume&& con)` | 对每个元素 `v` 调用 `con(tr(v))`，融合写入 |
| `transform_to(R* dst, count, F&& tr)` | 将 `tr(v)` 写入 `dst`，要求 `count <= size()` |

### 过滤与查找

| 接口 | 说明 |
|------|------|
| `find(const T& value)` | 线性查找，返回首命指针，未命中返回 `nullptr` |
| `find_if(Pred pred)` | 谓词查找 |
| `find_if_not(Pred pred)` | 谓词反查找 |
| `contains(const T& value)` | 是否包含 |
| `count_if(Pred pred)` | 谓词计数 |
| `filter_for_each(Pred pred, F&& f)` | 仅对满足 `pred(v)` 的元素调用 `f(v)` |
| `filter_indices_to(dense<size_t>& dst, Pred pred)` | 将满足谓词的索引追加到 `dst` |

### 规约与极值

| 接口 | 说明 |
|------|------|
| `reduce(F&& f, U init)` | 顺序规约：`acc = f(acc, v)` |
| `reduce_pairwise(F&& f, U init)` | 成对规约：相邻两两合并后递归，减少关键路径深度 |
| `min_element()` / `max_element()` | 返回最小/最大元素指针 |
| `minmax_element()` | 返回 `{min_ptr, max_ptr}` |
| `sum()` | 算术求和（要求 `is_arithmetic_v<T>`），使用 `ivdep` 自动向量化 |
| `dot_product(const U* other, count)` | 点积（要求 `is_arithmetic_v<T>`） |

### 窗口与分块

| 接口 | 说明 |
|------|------|
| `for_each_window<N>(F&& f)` | 滑动窗口遍历，对每个 `[i, i+N)` 调用 `f(std::span<T, N>)`，共 `size()-N+1` 次 |
| `for_each_chunk<N>(F&& f)` | 不重叠分块遍历，对每个 `[i*N, (i+1)*N)` 调用 `f(std::span<T, N>)`，共 `size()/N` 次 |
| `window_span<N>(offset)` | 取偏移 `offset` 处的滑动窗口 span |
| `chunk_span<N>(chunk_idx)` | 取第 `chunk_idx` 个不重叠分块 span |

### 枚举视图

| 接口 | 说明 |
|------|------|
| `for_each_enumerated(F&& f)` | 带索引遍历，调用 `f(index, value)` |

### 双容器同步

| 接口 | 说明 |
|------|------|
| `for_each_zip(U* other, count, F&& f)` | 同步遍历 `*this` 与 `other`，调用 `f(x, y)` |
| `for_each_zip(dense<U>& other, F&& f)` | dense 版本 |
| `for_each_zip(std::span<U> other, F&& f)` | span 版本 |
| `zip_with_to<R>(R* dst, const U* other, count, F&& f)` | 将 `f(x, y)` 写入 `dst`（SoA→AoS 转换） |
| `equal(const T* other, count)` | 逐元素相等比较 |
| `equal(const dense<U>& other)` | dense 版本 |
| `equal(std::span<const U> other)` | span 版本 |

### SIMD 与对齐

| 接口 | 说明 |
|------|------|
| `aligned_data()` | 返回对齐到缓存行（64B）的数据指针 |
| `aligned_span()` | 返回对齐 span |
| `simd_for_each(F&& f)` | SIMD 向量化遍历（要求 `is_trivially_copyable_v<T>`，sizeof ≤ 32 启用 AVX2） |
| `unaligned_tail_offset()` | 返回 SIMD 无法处理的尾部起始偏移 |

### 拷贝与移动

| 接口 | 说明 |
|------|------|
| `copy_to(T* dst, count)` | 批量拷贝（trivially copyable 走 AVX2 路径） |
| `copy_to(std::span<T> dst)` | span 版本 |
| `move_to(T* dst, count)` | 批量移动 |
| `move_to(std::span<T> dst)` | span 版本 |
| `reverse_copy_to(T* dst, count)` | 反向拷贝 |
| `reverse_copy_to(std::span<T> dst)` | span 版本 |

### 使用

```cpp
#include "part/dense.hpp"

dense<int> pool;                          // 默认构造
dense<int> reserved(100);                 // 预留容量 100
dense<int> filled(5, 42);                 // 5 个 42
dense<int> init = {10, 20, 30};           // 初始化列表

pool.emplace_back(1);
pool.emplace_back(2);
pool.emplace_back(3);

// push_back 拷贝/移动 (trivially copyable 走 memcpy 快路径)
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
pool.increase_capacity(10, 99);

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
```

### 视图使用示例

```cpp
#include "part/dense.hpp"

dense<float> d(1000);
for (size_t i = 0; i < 1000; ++i) d[i] = static_cast<float>(i);

// 子范围视图
std::span<float> head = d.first(10);
std::span<float> tail = d.last(10);
std::span<float> mid = d.subspan(100, 50);
std::span<float, 4> fixed_head = d.first_fixed<4>();   // 编译期固定长度

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

// SIMD 遍历（要求 trivially copyable）
d.simd_for_each([](float& v) { v *= 2.0f; });
size_t tail_off = d.unaligned_tail_offset();

// 拷贝/移动
dense<float> dst2(1000);
d.copy_to(dst2.data(), 1000);
d.reverse_copy_to(dst2.span());
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 使用 `std::vector` 代替 `dense<T>` | 性能与对齐不达标 | 始终使用 `dense<T>` |
| 未确认容量就调用 `push_back_unchecked` / `emplace_back_unchecked` | 越界写崩 | 先 `increase_capacity` 或用 `emplace_back` |
| `reduce_capacity(n, dst)` 后访问 `src` 被截断的元素 | 元素已迁移到 `dst` | 访问 `dst` 中对应元素 |
| `fill_bulk` 范围超出 `capacity()` | 越界写崩 | 先 `increase_capacity` 或 `reserve_exact` |
| 模板方法调用缺少 `template` 关键字 | 编译错误 | `d.template first_fixed<N>()` |
| 对非 trivially copyable 类型调用 `simd_for_each` | 编译错误（concept 约束） | 使用 `for_each` 代替 |
| `subspan` 的 `offset` 超过 `size()` | 返回空 span（已截断） | 调用前检查 `offset < size()` |

---

## 21. tiered_sort / pdqsort / sort_n — 分级排序

`#include "part/tiered_sort.hpp"`，全局命名空间。所有函数 `noexcept`。

### 接口

| 接口 | 签名 | 说明 |
|------|------|------|
| `pdqsort` | `void pdqsort<T>(T* data, size_t n, Compare&& cmp)` | 3-way pdqsort，要求 `is_trivially_copyable_v<T>` |
| `tiered_sort` | `void tiered_sort<T>(T* data, size_t n, Compare&& cmp)` | 分级排序值数组，按 `cmp` 升序 |
| `tiered_sort_indices` | `void tiered_sort_indices<T>(size_t* indices, const T* values, size_t n)` | 索引排序，按 `values[indices[i]]` 升序排列 `indices` |
| `sort_n` | `void sort_n<N, T>(T* data, Compare&& cmp)` | 编译期已知 N 的排序，N≤16 时使用排序网络 |
| `sort_indices_n` | `void sort_indices_n<N, T>(size_t* indices, const T* values)` | 编译期已知 N 的索引排序 |
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

## 22. radix_sort — 基数排序

`#include "part/radix_sort_helper.hpp"`，全局命名空间。所有函数 `noexcept`。

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

## 23. FORCE_INLINE / NOINLINE — 跨平台内联宏

`#include "part/force_inline.hpp"`

| 宏 | 说明 |
|------|------|
| `FORCE_INLINE` | 强制函数内联，跨编译器适配 |
| `NOINLINE` | 禁止函数内联，跨编译器适配 |

| 编译器 | `FORCE_INLINE` 展开为 | `NOINLINE` 展开为 |
|--------|----------------------|------------------|
| MSVC | `__forceinline` | `__declspec(noinline)` |
| GCC / Clang | `inline __attribute__((always_inline))` | `__attribute__((noinline))` |
| 其他 | `inline` | (空) |

### 使用

```cpp
#include "part/force_inline.hpp"

FORCE_INLINE int add(int a, int b) noexcept
{
    return a + b;
}

// 标记不内联的函数 (例如避免代码膨胀, 或用于隔离性能测量)
NOINLINE void heavy_function() noexcept
{
    // ...
}
```

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 递归函数标记 `FORCE_INLINE` | 编译器可能忽略或导致代码膨胀 | 递归函数不使用 `FORCE_INLINE` |
| 大函数标记 `FORCE_INLINE` | 代码膨胀，icache 压力增大 | 仅对热路径小函数使用 |
| 对热路径函数标记 `NOINLINE` | 性能下降 | `NOINLINE` 仅用于调试或隔离测量场景 |

---

## 24. arena_allocator — 线性 bump 分配器

`#include "part/arena_allocator.hpp"`，全局命名空间。`noexcept`。

线性分配器：无单个 `deallocate`，仅 `reset` 整体回收。两种模式：自有内存（析构释放）和借用外部 buffer（不持有所有权）。支持 `align <= 64` 的分配请求。

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

## 25. slab_allocator — 固定块分配器

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

## 26. layered_allocator — 分层分配器

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
| 修改 `memory_pool` 与 `slab_allocator` 的关系 | 破坏分层路由完整性 | 保持分层路由不变 |

---

## 27. ring_buffer — 环形缓冲区

`#include "part/ring_buffer.hpp"`，全局命名空间。`noexcept`。

模板参数 `N`（默认 1024）作为编译期最小保证容量（实际无界）。`push` 永不失败（OOM 时 `std::abort`）。不可拷贝，可移动。

### 接口

| 接口 | 说明 |
|------|------|
| `ring_buffer()` | 默认构造 |
| `ring_buffer(ring_buffer&&)` / `operator=(ring_buffer&&)` | 移动构造/赋值 |
| `push(const T&)` / `push(T&&)` | 写入一个事件，恒返回 true |
| `emplace(args...)` | 原位构造写入，恒返回 true |
| `drain(handler)` | 读取并处理所有待处理事件，返回处理数 |
| `drain_with_budget(budget, handler)` | 带预算的 drain，防止 handler 内追加导致无限循环 |
| `peek()` | 仅读队首（不推进），空返回 nullptr |
| `pop()` | 弹出队首，空返回 false |
| `empty()` / `has_pending()` | 是否空 / 是否有待处理 |
| `pending_count()` | 待处理数量 |
| `clear()` | 清空所有事件 |
| `capacity()` | 编译期最小保证容量 N（static） |
| `slots_per_chunk()` | 单块槽位数（static） |
| `static_pool_size()` | 静态池当前缓存块数（static） |
| `shrink_static_pool()` | 释放静态池所有缓存块（static） |

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

// 移动语义
ring_buffer<event, 1024> buf2(std::move(buf));
// buf 现在 empty

// 静态接口
size_t cap = ring_buffer<event, 1024>::capacity();       // 1024
size_t spc = ring_buffer<event, 1024>::slots_per_chunk(); // 编译期常量
size_t ps  = ring_buffer<event, 1024>::static_pool_size();
ring_buffer<event, 1024>::shrink_static_pool();          // 释放缓存
```

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 依赖 `push` 返回 false 判断满 | `push` 恒返回 true | 用 `pending_count()` 监控积压 |
| `drain` 的 handler 内 `push` 新事件 | 可能无限循环 | 用 `drain_with_budget` 限制处理数 |
| 依赖 `peek()` 指针在 `pop` 后有效 | `pop` 推进读位置，指针失效 | `peek` 后立即处理或先拷贝 |
| 拷贝构造 `ring_buffer` | 不可拷贝 | 用移动或重新填充 |

---

## 28. time — 计时与基准测量

`#include "part/time.hpp"`，全局命名空间。`noexcept`。

计时与基准测量工具：墙钟计时、CPU 周期计数、缓存屏障、统计分布、在线分位数、缓存延迟测量。x86/x64 提供 `rdtsc`/`rdtscp` / `clflush` / `mfence` / `lfence`，其他平台返回 0 或空操作。

### 29.1 timer — 墙钟计时器

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

### 29.2 cycle_timer — CPU 周期计时器

| 接口 | 说明 |
|------|------|
| `cycle_timer()` | 构造并记录起始周期 |
| `reset()` | 重置起始周期 |
| `elapsed_cycles()` | CPU 周期数 |
| `elapsed_ns_estimated(cpu_ghz)` | 按 CPU 频率估算纳秒 |
| `rdtsc()` | 读取 TSC 周期计数（自由函数，x86/x64；其他平台返回 0） |
| `rdtscp()` | 序列化读取 TSC 周期计数（自由函数，x86/x64；其他平台返回 0） |

```cpp
cycle_timer ct;
// ... 执行操作 ...
uint64_t cycles = ct.elapsed_cycles();
double ns = ct.elapsed_ns_estimated(3.5);  // 3.5 GHz

// 独立读取 TSC
uint64_t tsc = rdtsc();
uint64_t tsc_serialized = rdtscp();
```

### 29.3 stats — 统计分布

| 字段/接口 | 说明 |
|------|------|
| `min` / `max` / `mean` / `median` | 基本统计量（字段） |
| `p50` / `p90` / `p95` / `p99` | 百分位（字段） |
| `stddev` | 标准差（字段） |
| `count` | 样本数（字段） |
| `compute_stats(dense<double> samples)` | 从样本计算统计量（自由函数，会排序样本，空样本返回全 0） |

```cpp
class_pool<double> samples;
samples.emplace_back(1.0);
samples.emplace_back(2.0);
samples.emplace_back(3.0);
stats s = compute_stats(std::move(samples));
// s.mean == 2.0, s.median == 2.0
```

> 内部使用 `tiered_sort` 分级排序：n≤16 排序网络，n<1024 pdqsort，n≥1024 radix sort（O(n)）。

### 29.4 benchmark — 基准测量

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

### 29.5 缓存命中测量

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

### 29.6 x86 缓存屏障

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

### 29.7 P² 在线分位数

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

### 29.8 CPU 频率

| 接口 | 说明 |
|------|------|
| `estimate_cpu_ghz(calibration_ms=100)` | 忙等校准估算 TSC 频率（GHz），非核心频率 |
| `cpu_ghz_cached()` | 首次调用校准，后续返回缓存值 |

```cpp
double ghz = cpu_ghz_cached();
// 首次调用耗时 ~100ms，后续 O(1)
// 注: 测量的是 invariant TSC 频率 (恒定), 非核心频率 (受 Turbo Boost 影响)
//      rdtsc 计数速率 = TSC 频率, 不随核心频率变化
//      cycle_timer::elapsed_ns_estimated() 基于 TSC 频率, 适合相对比较
```

> 若需精确墙钟时间，优先使用 `timer`（`high_resolution_clock`），而非 `cycle_timer` + 频率估算。

### 29.9 延迟异常检测

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

## 29. multi_block_bitmask — 多块位掩码存储

`include/part/multi_block_bitmask.hpp`，无命名空间。每槽 1+ 个 64 位块的多块位掩码容器，块 0 内嵌，块 1+ 按需分配。通用位掩码场景（布隆过滤、稀疏集合、组件标签、哈希位图等）均适用。

### 接口

#### 静态辅助

| 接口 | 说明 |
|------|------|
| `static constexpr uint32_t bits_per_block` | 每块位数（64） |
| `static constexpr uint32_t block_count_for_bits(size_t bit_count)` | 给定位数算所需块数 |

#### 块管理

| 接口 | 说明 |
|------|------|
| `reserve_blocks(uint32_t num_blocks)` | 预分配掩码块数（仅扩容，不缩容） |
| `num_blocks() const` | 当前每槽掩码块数（= 1 + overflow_block_count_） |
| `overflow_entity_count() const` | 已分配 overflow 的槽位数 |

#### 容量管理

| 接口 | 说明 |
|------|------|
| `ensure_entity(uint32_t slot)` | 确保槽位容量（必要时自动扩容） |
| `resize_entities(uint32_t new_count)` | 批量扩容到 `new_count` 个槽位 |
| `increase_capacity(size_t new_slot_capacity)` | 扩容到指定槽位容量（只增不减，不改变 `size`） |
| `reserve_exact(size_t new_slot_capacity)` | 精确预留容量（不改变 `size`） |
| `shrink_to_fit()` | 缩容到实际槽位数 |
| `reduce_capacity(size_t new_slot_capacity)` | 缩容到指定容量（超出部分截断） |
| `clear()` | 清空所有数据（`size=0`，`capacity`/`num_blocks` 保留） |
| `size() const` | 当前槽位数 |
| `capacity() const` | 槽位容量 |
| `empty() const` | 是否为空 |
| `size_bytes() const` | 已用内存（字节） |
| `capacity_bytes() const` | 容量内存（字节） |

#### 单位写入（带边界检查）

| 接口 | 说明 |
|------|------|
| `set_bit(slot, block_idx, bit_offset)` | 设置位（必要时自动扩容 slot） |
| `clear_bit(slot, block_idx, bit_offset)` | 清除位 |
| `clear_entity(slot)` | 清零该槽位所有块 |

#### 单位写入（无边界检查）

调用方保证 `slot < size()` 且 `block_idx < num_blocks()`。

| 接口 | 说明 |
|------|------|
| `set_bit_no_check(slot, block_idx, bit_offset)` | 设置位 |
| `clear_bit_no_check(slot, block_idx, bit_offset)` | 清除位 |

#### 整块写入

替代读-改-写，直接写入整块值。

| 接口 | 说明 |
|------|------|
| `set_block_value(slot, block_idx, uint64_t value)` | 整块赋值 |
| `or_block_value(slot, block_idx, uint64_t mask)` | 原地或 |
| `and_block_value(slot, block_idx, uint64_t mask)` | 原地与 |
| `xor_block_value(slot, block_idx, uint64_t mask)` | 原地异或 |

#### 批量位操作（同块多位）

| 接口 | 说明 |
|------|------|
| `set_bits_at(slot, block_idx, std::span<const uint32_t> offsets)` | 批量设置多位 |
| `clear_bits_at(slot, block_idx, std::span<const uint32_t> offsets)` | 批量清除多位 |
| `toggle_bits_at(slot, block_idx, std::span<const uint32_t> offsets)` | 批量翻转多位 |

#### 整槽多块读写

| 接口 | 说明 |
|------|------|
| `assign_slot(slot, std::span<const uint64_t> data)` | 写入整槽所有块（`data[0]`→block 0，`data[1]`→block 1，…；若 `data.size() > num_blocks()` 自动 `reserve_blocks`） |
| `copy_slot_to(slot, std::span<uint64_t> dst) const` | 读取整槽所有块到 `dst`（`dst.size()` 决定读取块数，不足部分补零） |

#### 查询接口

| 接口 | 说明 |
|------|------|
| `get_block(slot, block_idx) const` | 获取块值（越界返回 0） |
| `test_bit(slot, block_idx, bit_offset) const` | 测试位是否置位 |
| `any_set_in_block(slot, block_idx) const` | 块是否非零 |
| `any_set(slot) const` | 槽位是否有任意位置位（含 overflow） |
| `is_zero(slot) const` | 槽位是否全零 |
| `count_set_bits(slot) const` | 槽位置位数（含 overflow，基于 `std::popcount`） |
| `find_first_set(slot, out_block, out_offset) const` | 找首个置位，写入 `out_block`/`out_offset`，返回是否找到 |
| `find_last_set(slot, out_block, out_offset) const` | 找末个置位 |
| `find_next_set(slot, after_block, after_offset, out_block, out_offset) const` | 从指定位置之后找下一个置位 |

#### 遍历接口

回调受 `std::invocable` concepts 约束，编译期捕获签名错误。

| 接口 | 回调签名 | 说明 |
|------|---------|------|
| `for_each_set_bit(slot, func) const` | `func(uint32_t block_idx, uint32_t bit_offset)` | 遍历该槽位所有置位 |
| `for_each_set_slot(func) const` | `func(uint32_t slot)` | 遍历所有非空槽位 |
| `for_each_set_bit_global(func) const` | `func(uint32_t slot, uint32_t block_idx, uint32_t bit_offset)` | 全局遍历所有置位 |
| `count_set_bits_global() const` | — | 全局置位总数 |

#### 视图接口

| 接口 | 说明 |
|------|------|
| `inline_span() noexcept` / `inline_span() const noexcept` | 返回 block 0 全局视图 `std::span<uint64_t>` / `std::span<const uint64_t>` |
| `overflow_span(slot) noexcept` / `overflow_span(slot) const noexcept` | 返回某槽位的 overflow 块视图（block 1+），未分配则返回空 span |

#### 复制与交换

| 接口 | 说明 |
|------|------|
| `multi_block_bitmask(const multi_block_bitmask&)` | 深拷贝构造 |
| `operator=(const multi_block_bitmask&)` | 深拷贝赋值 |
| `multi_block_bitmask(multi_block_bitmask&&)` | 移动构造 |
| `operator=(multi_block_bitmask&&)` | 移动赋值 |
| `swap(multi_block_bitmask&) noexcept` | 成员交换 |
| `clone() const` | 显式深拷贝工厂 |
| 自由函数 `swap(a, b) noexcept` | 自由交换 |

#### 集合运算（原地，处理共同 slot 与共同块）

| 接口 | 说明 |
|------|------|
| `and_with(const multi_block_bitmask& o)` | `this &= o`（`this` 中 `o` 不存在的 slot 清零） |
| `or_with(const multi_block_bitmask& o)` | `this \|= o`（仅处理共同 slot，不扩容） |
| `xor_with(const multi_block_bitmask& o)` | `this ^= o` |
| `subtract(const multi_block_bitmask& o)` | `this &= ~o`（差集） |
| `overlaps(const multi_block_bitmask& o) const` | 是否与 `o` 有任意共同置位 |
| `contains_all(const multi_block_bitmask& o) const` | `this` 是否包含 `o` 的所有置位（超集） |
| `equals(const multi_block_bitmask& o) const` | 是否相等 |

#### 内存压缩

| 接口 | 说明 |
|------|------|
| `compact_slot(slot)` | 若该槽位 overflow 全零则释放 |
| `compact_all()` | 全局压缩所有全零 overflow |

### 机制

- **块 0 内嵌**：每槽 `inline_bits_[slot]` 直接存储块 0 的 `uint64_t`，无间接访问
- **溢出按需分配**：块 1+ 仅在写入时按需分配 32 字节对齐的 overflow 数组，未使用溢出的槽位无额外内存
- **越界保护**：`set_bit` / `clear_bit` 检查 `block_idx < num_blocks()`，越界静默返回；`*_no_check` 版本跳过检查用于热路径
- **容量单位**：`increase_capacity` / `reserve_exact` / `reduce_capacity` / `capacity` 的参数与返回值均以"槽位数"为单位
- **集合运算语义**：仅处理双方共同 slot 与共同块，`this` 中超出 `o` 的 slot 在 `and_with` 下清零，其他运算保留
- **深拷贝**：拷贝构造/赋值深拷贝 `inline_bits_` 与每个 overflow 块，独立所有权

### 使用

```cpp
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
masks.increase_capacity(10000);
masks.reserve_exact(50000);
size_t cap = masks.capacity();
size_t n   = masks.size();
masks.shrink_to_fit();
masks.reduce_capacity(1000);
masks.clear();
bool isEmpty = masks.empty();
size_t used  = masks.size_bytes();
size_t total_cap = masks.capacity_bytes();
size_t ovf_count = masks.overflow_entity_count();

// 静态辅助
constexpr uint32_t blk = multi_block_bitmask::block_count_for_bits(200);  // = 4
```

### 批量注册推荐用法

逐个 `ensure_entity` 每次都做边界检查 + size 读取 + 可能的扩容。批量注册时用三步法减少重复检查:

```cpp
multi_block_bitmask masks;
masks.reserve_blocks(2);                   // 步骤 0: 预分配块数 (必须先于步骤 1)

// 三步法批量注册 N 个槽位
masks.increase_capacity(N);                // 步骤 1: 扩 capacity
masks.resize_entities(N);                  // 步骤 2: 撑 size
for (uint32_t i = 0; i < N; ++i)
{
    masks.set_bit_no_check(i, 0, i & 63);  // 步骤 3: 跳过边界检查写入
}
```

| 步骤 | 作用 | 跳过后果 |
|------|------|----------|
| `increase_capacity(N)` | 预留容量,避免 `resize_entities` 内部触发 realloc | 性能回退到逐次扩容路径 |
| `resize_entities(N)` | 把 `size()` 撑到 N,使 `set_bit_no_check` 访问合法 | `set_bit_no_check` 越界写崩 |
| `set_bit_no_check(...)` | 跳过边界检查的写入 | 退回 `set_bit` 的带检查路径 |

> 注:`increase_capacity` 只扩 capacity 不动 size,`set_bit_no_check` 要求 `slot < size()`。
> 因此步骤 1 和步骤 2 **都不可省略**,否则 `set_bit_no_check` 越界。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 注册组件后再调 `reserve_blocks` | 触发现有溢出槽位扩容 | 启动时预估类型数，注册前调 `reserve_blocks` |
| 对未 `ensure_entity` 的槽位调 `set_bit_no_check` | 越界写崩 | 先 `ensure_entity`,或批量场景用 `increase_capacity` + `resize_entities` 三步法 |
| `increase_capacity(N)` 后直接 `set_bit_no_check` | capacity 扩了但 size 未撑,越界写崩 | 必须再调 `resize_entities(N)` 撑 size |
| 对 `block_idx >= num_blocks()` 调 `set_bit` | 静默丢弃（不会扩容块数） | 先 `reserve_blocks` 扩容 |
| 假设 `get_block(slot, b)` 跨进程稳定 | 布局可能变化 | 仅当前进程内有效 |
| `reduce_capacity(n)` 传小于当前 `size` 的值 | 超出部分被截断丢失 | 缩容前确认 `n >= size()`，或先 `shrink_to_fit` |
| `clear()` 后假设 `num_blocks()` 归 1 | `clear` 只清数据不重置块数 | 重置块数需重新构造实例 |
| 集合运算后假设 `this` 的 `num_blocks()` 与 `o` 一致 | 集合运算不扩容块数，仅处理共同块 | 需要扩容先调 `reserve_blocks` |
| `overflow_span(slot)` 返回的 span 跨 `reserve_blocks` 使用 | `reserve_blocks` 会重分配 overflow 内存，span 失效 | 视图即时使用，不跨写操作持有 |

---

## 30. reflection — 反射元数据与存储

`#include "reflection/reflection.hpp"`，命名空间 `reflect`。`noexcept`。

反射模块底层元数据结构与存储接口。高层使用接口见 [§ 11. reflection — 反射模块使用](#11-reflection--反射模块使用)。

### 30.1 元数据结构

| 结构 | 字段 |
|------|------|
| `field_meta` | `name`, `offset`, `type_id`, `is_const`, `is_private`, `array_rank`, `reserved`, `total_elements`, `extents[4]`, `element_stride` |
| `method_meta` | `name`, `arg_count`, `return_type_id`, `invoker`, `is_const`, `is_static` |
| `type_meta` | `name`, `registered`, `field_count`, `method_count`, `size`, `align`, `type_id`, `fields`, `methods` |
| `MAX_TYPE_ID` | 类型槽位上限（65536） |
| `MAX_FIELDS_PER_TYPE` | 单类型字段上限（256） |
| `MAX_METHODS_PER_TYPE` | 单类型方法上限（256） |

`field_meta` 数组字段：`array_rank=1~4`、`extents[0..rank-1]` 为各维元素数、`total_elements` 为总元素数、`element_stride` 为元素步长（字节）。标量字段 `array_rank=0`。

### 30.2 storage 接口

| 接口 | 说明 |
|------|------|
| `storage::register_type<T>(name)` | 注册类型，聚合类型自动遍历公有字段 |
| `storage::register_type_only<T>(name)` | 只注册类型元信息，不自动遍历字段。用于无字段类型或配合底层 `register_array_field` 手动注册 |
| `storage::register_private_offsets<T>(descs, count)` | 手填偏移量注册私有成员 |
| `storage::register_field<T, M, Ptr>(name)` | 成员指针注册字段，需先 `register_type_only` |
| `storage::register_member_auto<T, M, Ptr>(type_name, field_name)` | 注册成员（标量/数组统一入口），自动判断字段类别 |
| `storage::register_array_field<T, M, Ptr>(name, rank, extents, element_type_id)` | 注册数组字段（C 数组/std::array） |
| `storage::register_array_field_auto<T, M, Ptr>(name)` | 注册数组字段，自动推导 rank/extents/element_type |
| `storage::register_method<Fn>(name)` | 注册成员方法 |
| `storage::register_static_method<C, Fn>(name)` | 注册静态方法 |
| `storage::get_type(tid)` | 按类型 id 查询 `type_meta*` |
| `storage::find_type(name)` | 按类型名查询 `type_meta*` |
| `reflect::global()` | 全局 storage 对象 |

---

## 31. aggregate_reflect — 聚合类型字段遍历

`#include "part/aggregate_reflect.hpp"`，全局命名空间。`noexcept`。编译期常量。

编译期探测聚合类型字段数并遍历字段。仅支持聚合类型（无自定义构造、无私有/受保护非静态数据成员、无虚函数、无基类）。非聚合类型字段数为 0。

### 字段上限配置

通过宏 `REFLECT_MAX_FIELDS` 配置自动反射字段上限，支持 16/32/64/128/256。默认 64。配置统一在 `include/config/reflect_config.hpp`，修改该文件或在 include 前定义对应宏即可调整：

```cpp
// 默认 64 字段
#include "part/aggregate_reflect.hpp"

// 扩展到 128 字段: 编辑 config/reflect_config.hpp 或在 include 前定义
#define REFLECT_MAX_FIELDS 128
#include "part/aggregate_reflect.hpp"
```

字段数超过 `REFLECT_MAX_FIELDS` 时触发编译期 `static_assert` 错误。

### 接口

| 接口 | 说明 |
|------|------|
| `aggregate_field_count_v<T>` | 编译期常量，类型 T 的字段数（非聚合为 0，上限由 `REFLECT_MAX_FIELDS` 决定） |
| `for_each_aggregate_member(obj, f)` | 遍历对象字段，`f(member, idx)` 接收引用和索引 |
| `for_each_aggregate_member(obj, f)` (const) | const 对象版本，`f` 接收 const 引用 |
| `member_offset(M T::*member)` | 通过成员指针计算偏移量（需访问权限） |

### 使用

```cpp
#include "part/aggregate_reflect.hpp"

struct Vec3 { float x, y, z; };
struct Pod4 { int a, b, c, d; };
struct Empty {};

// 编译期字段计数
static_assert(aggregate_field_count_v<Vec3> == 3);
static_assert(aggregate_field_count_v<Pod4> == 4);
static_assert(aggregate_field_count_v<Empty> == 0);

// 遍历字段
Vec3 v{1.0f, 2.0f, 3.0f};
for_each_aggregate_member(v, [&](auto& member, size_t idx) {
    std::cout << "field " << idx << " = " << member << "\n";
});

// 修改字段
for_each_aggregate_member(v, [&](auto& member, size_t idx) {
    (void)idx;
    member = 0.0f;
});

// const 对象遍历
const Vec3 cv{5.0f, 6.0f, 7.0f};
for_each_aggregate_member(cv, [&](const auto& member, size_t idx) {
    std::cout << idx << ": " << member << "\n";
});

// 成员偏移量
size_t off_y = member_offset(&Vec3::y);  // 4
```

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用于非聚合类型 | 字段数为 0，遍历不执行 | 仅用于聚合类型 |
| 字段含 C 数组 | 数组被识别为多字段聚合 | 避免在含数组字段类型上使用 |
| 字段数超过 `REFLECT_MAX_FIELDS` | 编译期 `static_assert` 错误 | include 前增大 `REFLECT_MAX_FIELDS` |
| `for_each_aggregate_member` 修改 const 对象 | 编译错误 | 用非 const 对象修改字段 |

---

## 32. type_erasure — 类型擦除方法调用器

`#include "part/type_erasure.hpp"`，全局命名空间。`noexcept`。

将任意成员函数指针、const 成员函数指针、静态函数指针包装为统一签名的 `invoker_func` 函数指针。支持任意参数数量。

### 接口

| 接口 | 说明 |
|------|------|
| `mfn_traits<MFnType>` | 方法签名 traits，提取 `class_type`/`return_type`/`arg_count`/`is_const`/`is_static` |
| `mfn_invoker_t<Fn, MFnType>::invoke(obj, args, result)` | 成员方法 invoker（普通/const 统一） |
| `sfn_invoker_t<Fn, MFnType>::invoke(obj, args, result)` | 静态方法 invoker（`obj` 忽略） |
| `arg_ids_maker<MFnType>::make()` | 生成参数类型 id 的 `dense<int>` |
| `return_type_id<R>()` | 返回类型 id（void 为 -1） |
| `invoker_func` | invoker 函数指针类型 |

### invoker 签名

```cpp
using invoker_func = void(*)(void* obj, const void* const* args, void* result);
```

- `obj`：对象指针（静态方法传 `nullptr`）
- `args`：参数指针数组，每个元素指向一个参数
- `result`：返回值缓冲区（void 方法不写）

### 使用

```cpp
#include "part/type_erasure.hpp"

class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
};

int free_multiply(int a, int b) { return a * b; }

// === 普通/const 成员方法 ===
Calculator calc;
using FnType = decltype(&Calculator::add);
constexpr FnType fn = &Calculator::add;

int a = 10, b = 20;
const void* args[] = { &a, &b };
alignas(alignof(std::max_align_t)) char result_buf[64];

mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
int result = *reinterpret_cast<int*>(result_buf);  // 30

// === 静态方法 ===
using FnType2 = decltype(&free_multiply);
constexpr FnType2 fn2 = &free_multiply;
sfn_invoker_t<fn2, FnType2>::invoke(nullptr, args, result_buf);
int product = *reinterpret_cast<int*>(result_buf);  // 200

// === 通过 invoker_func 调用 ===
invoker_func inv = &mfn_invoker_t<fn, FnType>::invoke;
inv(&calc, args, result_buf);

// === traits 与元数据 ===
using Traits = mfn_traits<decltype(&Calculator::add)>;
static_assert(Traits::arg_count == 2);
static_assert(!Traits::is_const);
static_assert(!Traits::is_static);

dense<int> arg_ids = arg_ids_maker<decltype(&Calculator::add)>::make();
// arg_ids.size() == 2, arg_ids[0] == type_id::get_type_id<int>()

int ret_id = return_type_id<int>();      // 返回 int 的 type_id
int void_id = return_type_id<void>();    // -1
```

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 返回值缓冲区未对齐 | placement new 未对齐访问崩溃 | `alignas(alignof(std::max_align_t))` |
| 参数数量与签名不符 | 未定义行为 | 调用前确认 `arg_count` |
| 返回 std::string 未析构 | 内存泄漏 | 手动调用 `~basic_string()` |
| 静态方法传非空 `obj` | 被忽略 | 静态方法传 `nullptr` |

---

## 33. member_offset — 成员偏移量访问

`#include "part/member_offset.hpp"`，全局命名空间。`noexcept`。

通过偏移量访问对象成员，支持突破私有访问限制。提供 `offset_desc` 结构用于批量描述字段。

### 接口

| 接口 | 说明 |
|------|------|
| `offset_of(M T::*member)` | 通过成员指针计算偏移量（需访问权限） |
| `offset_access<M>(obj, offset)` | 按偏移量访问成员引用 |
| `offset_access<M>(obj, offset)` (const) | const 对象版本 |
| `ub_access<T, M>(obj, offset)` | UB 反向构造成员指针，突破私有访问 |
| `offset_desc` | 字段描述结构 `{name, offset, type_id}` |

### 使用

```cpp
#include "part/member_offset.hpp"

struct Pod3 { float x, y, z; };
struct Mixed { char c; int i; double d; };

class Account
{
    int balance_;
public:
    Account(int b) : balance_(b) {}
    int get_balance() const { return balance_; }
};

// === offset_of 成员指针偏移 ===
size_t off_x = offset_of(&Pod3::x);  // 0
size_t off_y = offset_of(&Pod3::y);  // 4
size_t off_z = offset_of(&Pod3::z);  // 8

// === offset_access 直接指针访问 ===
Pod3 v{1.0f, 2.0f, 3.0f};
float y = offset_access<float>(&v, 4);   // 2.0
offset_access<float>(&v, 8) = 100.0f;    // 修改 z

// const 对象
const Pod3 cv{5.0f, 6.0f, 7.0f};
float cx = offset_access<float>(&cv, 0);  // 5.0

// 混合类型
Mixed m{'A', 42, 3.14};
char c = offset_access<char>(&m, 0);      // 'A'
int i = offset_access<int>(&m, 4);        // 42
double d = offset_access<double>(&m, 8);  // 3.14

// === ub_access 突破私有访问 ===
Account acc(100);
// 假设 balance_ 在偏移 0 (首成员)
int bal = ub_access<Account, int>(acc, 0);  // 100
ub_access<Account, int>(acc, 0) = 999;      // 修改私有成员
acc.get_balance();  // 999

// === offset_desc 批量描述 ===
offset_desc descs[] = {
    {"name_",    0,  type_id::get_type_id<std::string>()},
    {"balance_", 32, type_id::get_type_id<int>()}
};
```

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| `ub_access` 偏移量错误 | 访问错误内存 | 用 `offsetof` 或编译期推导确认偏移量 |
| `ub_access` 用于虚函数类 | vptr 干扰偏移布局 | 仅用于无非虚函数的类 |
| 假设 `ub_access` 可移植 | 严格 UB，依赖编译器实现 | 仅在受控环境使用 |
| 对齐错误的偏移量访问 | 未对齐访问崩溃 | 确保偏移量与类型对齐匹配 |

## 34. t_fun — 函数类型延迟调用器

`#include "part/t_fun.hpp"`，全局命名空间。`noexcept`。

编译期推导函数类型的延迟调用器，支持函数指针、成员函数指针（const/非 const）。用户无需输入模板参数，通过 CTAD 自动推导。void 返回值特化，`result_ptr()` 返回 `nullptr`。

### 接口

| 接口 | 说明 |
|------|------|
| `t_fun v{f, args...}` | 构造，绑定函数与参数（CTAD 推导） |
| `v()` | 用绑定参数调用 |
| `v(a, b)` | 用传入参数调用，不修改绑定参数 |
| `v.fun()` / `v.fun(a, b)` | 等价 operator() |
| `v.result_ptr()` | 非 void 返回 `R*`；void 返回 `nullptr` |
| `v.result_reset()` | 非 void 重置为默认值；void 无副作用 |
| `v.target()` | 原始可调用对象（函数指针 / 成员函数指针） |
| `v.bound_arg<I>()` | 第 I 个绑定参数引用（const/非 const 重载） |
| `v.set_arg<I>(v)` | 修改第 I 个绑定参数 |
| `v.object()` | 成员函数指针版本，对象指针 |
| `v.arity` | 编译期常量，参数数量 |
| `v.return_type` | 编译期类型别名，返回值类型 |
| `v.args_tuple` | 编译期类型别名，参数 tuple |
| `v.then_call(g)` | 链式调用，`g(v())`；void 版本 `v(); g();` |
| `v.compose(g, more...)` | 多级组合，依次调用 g, more... |
| `v.bind_front(args...)` | 设置前 N 个绑定参数 |
| `v.apply_n(count)` | 重复调用 N 次，返回最后结果 |
| `v.apply_range(data, n)` | 对数组批量调用（arity==1） |
| `v.swap(other)` | O(1) 交换 |
| `v == other` / `v != other` | 比较 target 与绑定参数 |
| `v.hash()` | 哈希值 |
| `v.empty()` | 是否已释放 |
| `v.release()` | 释放所有权 |
| `v.reset(args...)` | 重置所有绑定参数 |
| `os << v` | 流输出诊断信息 |

### 使用

```cpp
#include "part/t_fun.hpp"
#include <string>

int  free_add(int a, int b) { return a + b; }
void free_void(int x) { (void)x; }
void free_noop() {}
std::string free_make_str(const char* s) { return std::string(s); }

class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
};

// === 函数指针: 绑定参数调用 ===
t_fun v1{free_add, 10, 20};
v1();                  // 30, 用绑定的 10,20
v1.fun();              // 30, 等价
v1(3, 4);              // 7,  用传入参数, 不修改绑定参数
v1();                  // 仍 30, 绑定参数未被覆盖

// === result_ptr / result_reset ===
int* p = v1.result_ptr();   // &result_, *p == 30
v1.result_reset();          // *p 变为 0
v1.result_ptr();            // &result_, *p == 0

// === target / bound_arg / set_arg ===
v1.target()(10, 20);        // 30, 直接调用原始函数指针
v1.bound_arg<0>();          // 10
v1.set_arg<0>(100);
v1.set_arg<1>(200);
v1();                       // 300

// === 编译期元信息 ===
static_assert(decltype(v1)::arity == 2);
static_assert(std::is_same_v<decltype(v1)::return_type, int>);

// === void 返回值 ===
t_fun v2{free_void, 42};
v2();                       // 调用, 无返回值
void* p2 = v2.result_ptr(); // nullptr
v2.result_reset();          // 无副作用

t_fun v3{free_noop};        // 无参函数
v3();

// === std::string 返回值 ===
t_fun v4{free_make_str, "abc"};
std::string s = v4();       // "abc"
std::string* p4 = v4.result_ptr();  // &result_, *p4 == "abc"
v4.result_reset();          // 空字符串

// === 成员函数指针 ===
Calculator calc;
t_fun m1{&Calculator::add, &calc, 10, 20};
m1();                       // 30, 绑定对象与参数
m1(3, 4);                   // 7,  带参调用
m1.object();                // &calc
m1.target();                // &Calculator::add

// const 成员函数
t_fun m2{&Calculator::is_positive, &calc, 5};
m2();                       // true

// void 成员函数
t_fun m3{&Calculator::no_return, &calc, 0};
m3();

// === 移动与拷贝 ===
t_fun v5{std::move(v1)};    // 移动构造
t_fun v6{v5};                // 拷贝构造
v5 = std::move(v6);          // 移动赋值

// === then_call 链式调用 ===
t_fun v7{free_add, 10, 20};
int doubled = v7.then_call([](int x){ return x * 2; });  // 60
// void 版本: v(); g();

// === compose 多级组合 ===
int composed = v7.compose(
    [](int x){ return x * 2; },
    [](int x){ return x + 1; }
);  // (30*2)+1 = 61

// === bind_front 设置前 N 个参数 ===
t_fun v8{free_add, 0, 0};
v8.bind_front(100);          // 设置第 0 个参数为 100
v8.set_arg<1>(200);
v8();                        // 300

// === apply_n 批量调用 ===
t_fun v9{free_add, 1, 2};
int last = v9.apply_n(1000); // 调用 1000 次, 返回最后结果 (3)

// === apply_range 范围应用 (arity==1) ===
t_fun v10{free_identity};
int data[] = {1, 2, 3, 4, 5};
int last_r = v10.apply_range(data, 5);  // 对每个元素调用, 返回最后 (5)

// === swap 交换 ===
t_fun a{free_add, 1, 2};
t_fun b{free_add, 10, 20};
a.swap(b);
a();                         // 30 (交换后)
b();                         // 3

// === operator== / != ===
t_fun c1{free_add, 10, 20};
t_fun c2{free_add, 10, 20};
t_fun c3{free_add, 30, 40};
c1 == c2;                    // true (target 与绑定参数相同)
c1 != c3;                    // true

// === hash ===
size_t h = c1.hash();        // 哈希值

// === empty / release ===
t_fun v11{free_add, 10, 20};
v11.empty();                 // false
v11.release();               // 释放所有权
v11.empty();                 // true

// === reset 重置所有绑定参数 ===
t_fun v12{free_add, 0, 0};
v12.reset(5, 6);
v12();                       // 11

// === operator<< 流输出 ===
t_fun v13{free_add, 10, 20};
std::cout << v13;            // [t_fun arity=2 target=set]
v13.release();
std::cout << v13;            // [t_fun arity=2 target=null]
```

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 返回值类型不可默认构造 | 编译期报错 | 确保 `R` 可默认构造 |
| `set_arg<I>` 越界 | 编译期报错 | I < arity |
| `bound_arg<I>` 越界 | 编译期报错 | I < arity |
| 带参调用参数数与签名不符 | 编译期报错 | 参数数量与函数签名一致 |
| 成员函数指针版本不传对象 | 编译期报错 | 构造时第二参数传 `C*` |
| const 成员函数传非 const 对象指针 | 编译期报错 | 传 `const C*` |
| `result_ptr()` 未调用就解引用 | 读到默认构造值 | 先调用 `v()` |
| 使用后忘记 `result_reset()` | 读到上次调用结果 | 需要重置时调用 |
| `apply_range` 用于 arity != 1 | 编译期报错 | 仅 arity == 1 时可用 |
| `release` 后再调用 | 解引用空指针 | 调用前检查 `empty()` |
| `bind_front` 参数数超过 arity | 编译期报错 | 参数数 <= arity |
| `reset` 参数数与 arity 不符 | 编译期报错 | 参数数 == arity |
| `then_call` 的 g 参数类型不匹配 | 编译期报错 | g 接受 R 类型参数 |

## 35. utf8_codec / utf8pp — UTF-8 编解码与拥有型字符串

`#include "part/utf8pp.hpp"`（自动包含 `utf8_codec.hpp` 与 `dense.hpp`），全局命名空间。核心接口 `noexcept`，少数修改操作（需扩容时调用 `std::realloc`，失败 `std::abort`）亦为 `noexcept`。

模块分三层：

- **`utf8_codec.hpp`**：底层编解码核心（`detail_utf8` 命名空间 + 对外导出函数），无状态纯函数，被 `utf8pp` 与 `utf8_view` 共享。
- **`utf8pp` 类**：拥有内存的 UTF-8 字符串类，自管裸指针存储 + 码点偏移缓存（`uint32_t*`），码点级访问 O(1)。
- **`utf8pp` 非成员接口**：`operator+` / 流操作符 / `swap` / 字面量 / `std::hash` 特化。

仅支持 Unicode 编码（UTF-8 / UTF-32 码点），不支持其他编码（GBK / UTF-16 等）。非法序列替换为 U+FFFD 替换字符。支持完整 Unicode 字符处理：码点级 + 字形簇级 + East Asian Width 显示宽度 + NFC/NFKC 规范化（含 Hangul 算法） + Script 脚本判断。

### 35.1 底层编解码接口（utf8_codec.hpp）

| 接口 | 说明 |
|------|------|
| `to_char(uint32_t cp)` | 数值转 Unicode 字符（`char32_t`），constexpr |
| `to_int(char32_t ch)` | Unicode 字符转数值（`uint32_t`），constexpr |
| `utf8_to_codepoints(src, src_len, out, out_cap, out_has_err)` | UTF-8 字符串转码点数组（`uint32_t*`） |
| `codepoints_to_char32(cps, cp_count, out, out_cap, out_has_err)` | 码点数组转 `char32_t` 字符串 |
| `char32_to_utf8(src, src_len, out, out_cap, out_has_err)` | `char32_t` 字符串转 UTF-8 字节序列 |

`out_has_err` 为可选输出参数（`bool*`），传入 `nullptr` 表示不接收错误标志。返回值为需要写入的总数：`<= out_cap` 表示已全部写入；`> out_cap` 表示所需容量（调用方扩容后重试）。

`detail_utf8` 命名空间内部接口（通常不直接使用）：`k_utf8_seq_len[256]` 序列长度表、`is_valid_codepoint` / `is_shortest_form` 校验、`utf8_decode_one` / `utf8_encode_one` 单次编解码、`count_codepoints` / `advance_codepoint` / `retreat_codepoint` 游标推进。

### 35.2 utf8pp 类接口

#### 构造与赋值

| 接口 | 说明 |
|------|------|
| `utf8pp()` | 默认构造，空字符串（SSO 模式） |
| `utf8pp(const char* s)` / `utf8pp(const char* s, size_t byte_len)` | 从 UTF-8 C 字符串构造 |
| `utf8pp(const char8_t* s)` / `utf8pp(const char8_t* s, size_t byte_len)` | 从 `char8_t` 字符串构造 |
| `utf8pp(const char32_t* s, size_t cp_count)` | 从码点数组构造 |
| `utf8pp(size_t n, char32_t cp)` | 用 n 个 cp 填充构造 |
| `utf8pp(std::string_view sv)` | 从 string_view 构造 |
| `utf8pp(const std::string& s)` / `utf8pp(const std::u8string& s)` / `utf8pp(const std::u32string& s)` | 从 std 字符串构造 |
| `utf8pp(std::initializer_list<char32_t> il)` | 从码点初始化列表构造 |
| `utf8pp(InputIt first, InputIt last)` | 从迭代器范围构造（SFINAE 排除整数类型） |
| `explicit utf8pp(const std::array<utf8pp, N>& parts)` / `explicit utf8pp(const std::vector<utf8pp>& parts)` | 从范围拼接构造 |
| `utf8pp(const utf8pp&)` / `utf8pp(utf8pp&&) noexcept` | 拷贝/移动构造 |
| `utf8pp(std::nullptr_t) = delete` | 禁止从 nullptr 构造 |
| `operator=(const utf8pp&)` / `operator=(utf8pp&&) noexcept` | 拷贝/移动赋值 |
| `operator=(const char*)` / `operator=(std::string_view)` / `operator=(char32_t)` | 从常见类型赋值 |
| `operator=(const char8_t*)` / `operator=(std::initializer_list<char32_t>)` | 从 char8_t/初始化列表赋值 |
| `operator=(const std::string&)` / `operator=(const std::u8string&)` | 从 std 字符串赋值 |
| `assign(const char* s, size_t byte_len)` / `assign(const char* s)` / `assign(const utf8pp&)` | 重新赋值 |
| `assign(std::string_view)` / `assign(const std::string&)` / `assign(const char8_t*)` | 重新赋值 |
| `assign(std::initializer_list<char32_t>)` / `assign(InputIt first, InputIt last)` | 从初始化列表/迭代器范围赋值 |
| `assign(size_t n, char32_t cp)` | 重新赋值为 n 个 cp |
| `assign(const std::u8string&)` / `assign(const std::u32string&)` | 从 u8/u32 字符串赋值 |
| `assign(const utf8pp& other, size_t pos, size_t n = npos)` | 从 other 的子串 [pos, pos+n) 赋值 |
| `assign(const std::array<utf8pp, N>&)` / `assign(const std::vector<utf8pp>&)` | 从范围重新赋值 |
| `swap(utf8pp&) noexcept` / `swap(utf8pp&, utf8pp&) noexcept` | 交换 |

#### 容量

| 接口 | 说明 |
|------|------|
| `size()` / `length()` | 码点数（O(1)，偏移缓存） |
| `byte_size()` | 字节数 |
| `capacity()` | 当前字节容量 |
| `cp_capacity()` | 当前码点容量（与字节容量解耦） |
| `max_size()` | 理论最大字节数 |
| `empty()` | 是否为空 |
| `is_sso()` | 是否处于 SSO 模式（constexpr） |
| `sso_capacity()` | SSO 容量（constexpr，= 22） |
| `reserve(n)` | 预留字节容量（按增长策略放大） |
| `reserve_exact(n)` | 精确预留字节容量（强制增长到 n） |
| `reserve_cp(n)` | 预留码点容量（仅扩容码点偏移数组） |
| `increase_capacity(new_cap)` | 同时扩容字节缓冲与码点偏移数组（项目规范用法） |
| `shrink_to_fit()` | 释放多余容量（若可回退到 SSO 则回退） |
| `clear() noexcept` | 清空内容（不释放内存） |

#### 访问

| 接口 | 说明 |
|------|------|
| `at(cp_idx)` / `operator[](cp_idx)` | 码点索引访问（O(1) via 偏移缓存，越界返回 U+FFFD） |
| `front()` / `back()` | 首尾码点（空串返回 U+FFFD） |
| `c_str()` / `data()` (const) | C 字符串（**空对象返回 `""`，非 nullptr**） |
| `data()` (非 const) | 可写字节指针（修改后必须调用 `rebuild_cp_offsets` 重建偏移） |
| `view()` / `binary_view()` | `std::string_view`（两者等价，`binary_view` 强调字节语义） |
| `u8view()` | `std::u8string_view` |

#### 迭代器

| 接口 | 说明 |
|------|------|
| `begin()` / `end()` / `cbegin()` / `cend()` | 码点级前向迭代器（`random_access_iterator_tag`，解引用返回 `char32_t`） |
| `rbegin()` / `rend()` / `crbegin()` / `crend()` | 码点级反向迭代器（`random_access_iterator_tag`） |
| `byte_begin()` / `byte_end()` / `byte_cbegin()` / `byte_cend()` | 字节级前向迭代器（`contiguous_iterator_tag`，解引用返回 `char`） |
| `rbyte_begin()` / `rbyte_end()` | 字节级反向迭代器（`contiguous_iterator_tag`） |
| `const_iterator::ptr()` | 暴露底层 `const char*`（用于与 C API 交互） |
| `const_byte_iterator::ptr()` | 暴露底层 `const char*` |

#### 直接内存修改（逃生舱）

| 接口 | 说明 |
|------|------|
| `data()` (非 const) | 获取可写字节指针，允许直接修改缓冲区内容 |
| `rebuild_cp_offsets() noexcept` | 修改 `data()` 后重建码点偏移缓存（重新解码全串） |
| `rebuild(new_byte_size) noexcept` | 重建偏移缓存并设置新的字节大小（修改后长度变化时使用） |

#### 修改操作

| 接口 | 说明 |
|------|------|
| `push_back(char32_t cp)` | 追加单个码点 |
| `append(const char*)` / `append(const char*, size_t)` / `append(const char8_t*)` | 追加 UTF-8 字符串 |
| `append(const char32_t*, size_t)` / `append(const utf8pp&)` / `append(std::string_view)` | 追加其他形式 |
| `append(const std::string&)` / `append(std::initializer_list<char32_t>)` | 追加 std string / 初始化列表 |
| `append(std::array<utf8pp, N>&)` / `append(std::vector<utf8pp>&)` / `append(const utf8pp*, size_t)` / `append(std::span<const utf8pp>)` | 批量追加 |
| `operator+=(char32_t)` / `operator+=(const char*)` / `operator+=(const utf8pp&)` | 追加运算符 |
| `operator+=(std::string_view)` / `operator+=(const char8_t*)` / `operator+=(std::initializer_list<char32_t>)` | 追加运算符扩展 |
| `insert(cp_idx, char32_t)` | 按码点索引插入单码点 |
| `insert(cp_idx, const utf8pp&)` / `insert(cp_idx, const char*)` / `insert(cp_idx, std::string_view)` | 按码点索引插入字符串 |
| `insert(cp_idx, const char* s, size_t n)` / `insert(cp_idx, size_t n, char32_t cp)` / `insert(cp_idx, std::initializer_list<char32_t>)` | 按码点索引插入（指定长度/填充/初始化列表） |
| `insert(const_iterator pos, char32_t)` / `insert(pos, size_t n, char32_t cp)` | 迭代器版插入 |
| `insert(const_iterator pos, InputIt first, InputIt last)` | 迭代器版插入（模板迭代器范围） |
| `insert(const_iterator pos, const utf8pp&)` / `insert(pos, const char*)` / `insert(pos, std::string_view)` / `insert(pos, std::initializer_list<char32_t>)` | 迭代器版插入字符串 |
| `erase(cp_idx, n=1)` | 按码点索引删除 n 个码点 |
| `erase(const_iterator pos)` / `erase(first, last)` | 迭代器版删除 |
| `pop_back()` | 删除末尾码点 |
| `substr(pos, cp_count=npos)` | 按码点索引取子串（返回 utf8pp） |
| `append_cp(n, cp)` | 追加 n 个相同码点 |
| `assign_cp(n, cp)` | 清空后赋 n 个相同码点 |
| `resize_cp(n, cp=U'\0')` / `resize(n)` / `resize(n, cp)` | 调整码点数（小于则截断，大于则补 cp；`resize` 为 `std::string` 别名） |
| `replace(pos, n, const utf8pp&)` / `replace(pos, n, const char*)` / `replace(pos, n, std::string_view)` | 替换 [pos, pos+n) |
| `replace(pos, n, const char* s, size_t n2)` / `replace(pos, n, size_t n2, char32_t cp)` | 替换（指定长度/填充） |
| `replace(pos1, n1, const utf8pp& other, pos2, n2)` / `replace(pos1, n1, const char* s, pos2, n2)` | 双区间替换（本串 [pos1,pos1+n1) ← other [pos2,pos2+n2)） |
| `replace(const_iterator first, last, const utf8pp&)` / `replace(first, last, const char*)` / `replace(first, last, std::string_view)` | 迭代器范围替换 |
| `replace(const_iterator first, last, const char* s, size_t n2)` / `replace(first, last, size_t n2, char32_t cp)` | 迭代器范围替换（指定长度/填充） |
| `replace(const_iterator first, last, InputIt ifirst, InputIt ilast)` | 迭代器范围替换（模板迭代器范围） |
| `replace_all(const utf8pp& old, const utf8pp& new)` / `replace_all(const char*, const char*)` / `replace_all(std::string_view, std::string_view)` | 全局替换子串 |
| `replace_all(const char* old, const utf8pp& new)` / `replace_all(const utf8pp& old, const char* new)` | 全局替换子串（混合重载） |
| `replace_all(char32_t old_cp, char32_t new_cp)` | 全局替换单码点 |
| `trim_left()` / `trim_right()` / `trim()` | 去除首/尾/两端空白（Unicode 空白） |
| `trim_left(Pred pred)` / `trim_right(Pred pred)` / `trim(Pred pred)` | 谓词版去除（Pred: `bool(char32_t)`，SFINAE 约束） |
| `trim(const utf8pp& chars)` / `trim_left(const utf8pp& chars)` / `trim_right(const utf8pp& chars)` | 字符集版去除 |
| `trim(const char* chars)` / `trim_left(const char* chars)` / `trim_right(const char* chars)` | 字符集版去除（C 字符串） |
| `trimmed()` / `trimmed_left()` / `trimmed_right()` | 返回去除后的副本（含谓词/字符集重载） |
| `to_lower()` / `to_upper()` | 大小写转换（完整 Unicode，原地修改） |
| `to_title()` / `swapcase()` | 首字母大写 / 大小写互换（完整 Unicode，原地修改） |
| `lowered()` / `uppered()` / `titled()` / `swapcased()` | 返回转换后的副本 |
| `reverse()` | 码点级反转（原地修改） |
| `reversed()` | 返回反转后的副本 |
| `strip_bom()` | 剥离 UTF-8 BOM |
| `copy(char* buf, size_t n, pos=0)` | 拷贝到外部缓冲区（返回拷贝字节数） |
| `pad_left(width, fill=U' ')` / `pad_right(width, fill=U' ')` / `center(width, fill=U' ')` | 显示宽度对齐填充（East Asian Width 感知，全角=2，原地修改） |
| `padded_left(width, fill=U' ')` / `padded_right(width, fill=U' ')` / `centered(width, fill=U' ')` | 返回对齐填充后的副本 |
| `display_width()` | 计算显示宽度（East Asian Width: 全角=2, 零宽=0, ASCII=1） |
| `to_nfc()` / `to_nfd()` | NFC 规范化 / NFD 分解（原地修改，含 Hangul 算法分解） |
| `to_nfkc()` / `to_nfkd()` | NFKC 规范化 / NFKD 分解（原地修改，含兼容性字符分解：全角→半角、连字、上标、罗马数字等） |
| `nfc()` / `nfd()` / `nfkc()` / `nfkd()` | 返回规范化后的副本 |

#### 字节级访问

| 接口 | 说明 |
|------|------|
| `byte_at(byte_idx)` | 字节索引访问（越界返回 `'\0'`） |
| `at_byte(byte_idx)` | 字节索引访问（越界 `std::abort`） |
| `byte_substr(byte_pos, byte_len=npos)` | 字节级子串（返回 utf8pp，不校验 UTF-8 边界） |
| `byte_to_cp_idx(byte_idx)` | 字节索引 → 码点索引（非码点起点返回 `npos`） |
| `cp_to_byte_idx(cp_idx)` | 码点索引 → 字节索引（越界返回 `byte_size()`） |

#### 字符分类 API（静态方法 + 串级判断）

| 接口 | 说明 |
|------|------|
| `static is_alpha(cp)` / `static is_digit(cp)` / `static is_alnum(cp)` | 字母 / 数字 / 字母数字（完整 Unicode） |
| `static is_space(cp)` / `static is_punct(cp)` | 空白 / 标点（Unicode） |
| `static is_lower(cp)` / `static is_upper(cp)` | 小写 / 大写（完整 Unicode） |
| `static is_xdigit(cp)` / `static is_cntrl(cp)` / `static is_printable(cp)` | 十六进制 / 控制字符 / 可打印 |
| `static is_combining(cp)` / `static is_wide(cp)` / `static is_zero_width(cp)` / `static is_emoji(cp)` | 组合标记 / 宽字符 / 零宽 / Emoji |
| `static cp_width(cp)` | 单码点显示宽度（0/1/2） |
| `static to_lower_cp(cp)` / `static to_upper_cp(cp)` / `static to_title_cp(cp)` | 单码点大小写转换（完整 Unicode） |
| `static script_of(cp)` | 单码点所属脚本（`utf8pp::script` 枚举：Latin/Han/Hiragana/Arabic/Emoji 等） |
| `static is_script(cp, s)` | 单码点是否属于指定脚本 |
| `static script_name(s)` | 脚本枚举 → 名称字符串 |
| `is_all_alpha()` / `is_all_digit()` / `is_all_alnum()` | 整串是否全为字母/数字/字母数字 |
| `is_all_space()` / `is_all_xdigit()` / `is_all_printable()` | 整串是否全为空白/十六进制/可打印 |
| `script_of()` | 整串首字符所属脚本（空串返回 `script::unknown`） |
| `is_all_script(s)` | 整串是否全属指定脚本（空串返回 false） |
| `contains_script(s)` | 是否包含至少一个指定脚本的码点 |

#### 数字转换

| 接口 | 说明 |
|------|------|
| `to_int(pos*, base=10)` / `to_long(pos*, base=10)` / `to_ll(pos*, base=10)` | 字符串 → int/long/long long（失败返回 0） |
| `to_ulong(pos*, base=10)` / `to_ull(pos*, base=10)` | 字符串 → unsigned long/unsigned long long |
| `to_float(pos*)` / `to_double(pos*)` / `to_long_double(pos*)` | 字符串 → float/double/long double |
| `stoi/stol/stoll/stoul/stoull/stof/stod/stold(...)` | std 风格别名（参数同上） |
| `parse_int(out, base=10) noexcept` / `parse_long(out, base=10) noexcept` / `parse_ll(out, base=10) noexcept` | 严格解析为 int/long/long long（返回 bool，允许前导 +/- 与首尾空白） |
| `parse_ulong(out, base=10) noexcept` / `parse_ull(out, base=10) noexcept` | 严格解析为 unsigned long/unsigned long long |
| `parse_float(out) noexcept` / `parse_double(out) noexcept` / `parse_long_double(out) noexcept` | 严格解析为浮点（返回 bool） |
| `is_integer(base=10) noexcept` / `is_float() noexcept` / `is_number() noexcept` | 内容判断：整数 / 浮点 / 数字 |
| `is_hex() noexcept` / `is_binary() noexcept` / `is_octal() noexcept` | 进制判断便捷别名 |

#### format / vformat

| 接口 | 说明 |
|------|------|
| `static format(const char* fmt, ...)` | printf 风格格式化（返回 utf8pp，自动扩容） |
| `static vformat(const char* fmt, std::va_list ap)` | va_list 版本 |

#### 查找

| 接口 | 说明 |
|------|------|
| `find(char32_t, pos=0)` / `find(const utf8pp&, pos=0)` | 正向查找码点/子串 |
| `find(const char* s, pos=0)` / `find(std::string_view sv, pos=0)` | 正向查找 C 字符串/string_view |
| `find(const char* s, pos, n)` | 正向查找 C 字符串前 n 字节（三参，与 `std::string` 对齐） |
| `rfind(char32_t, pos=npos)` / `rfind(const utf8pp&, pos=npos)` | 逆向查找 |
| `rfind(const char* s, pos=npos)` / `rfind(std::string_view sv, pos=npos)` | 逆向查找 C 字符串/string_view |
| `rfind(const char* s, pos, n)` | 逆向查找 C 字符串前 n 字节（三参） |
| `find_first_of(char32_t, pos=0)` / `find_first_of(const utf8pp&, pos=0)` | 首个匹配 |
| `find_first_of(const char* s, pos=0)` / `find_first_of(std::string_view sv, pos=0)` | 首个匹配（C 字符串/string_view） |
| `find_first_of(const char* s, pos, n)` | 首个匹配（三参） |
| `find_last_of(char32_t, pos=npos)` / `find_last_of(const utf8pp&, pos=npos)` | 末个匹配 |
| `find_last_of(const char* s, pos=npos)` / `find_last_of(std::string_view sv, pos=npos)` | 末个匹配（C 字符串/string_view） |
| `find_last_of(const char* s, pos, n)` | 末个匹配（三参） |
| `find_first_not_of(char32_t, pos=0)` / `find_first_not_of(const utf8pp&, pos=0)` | 首个不匹配 |
| `find_first_not_of(const char* s, pos=0)` / `find_first_not_of(std::string_view sv, pos=0)` | 首个不匹配（C 字符串/string_view） |
| `find_first_not_of(const char* s, pos, n)` | 首个不匹配（三参） |
| `find_last_not_of(char32_t, pos=npos)` / `find_last_not_of(const utf8pp&, pos=npos)` | 末个不匹配 |
| `find_last_not_of(const char* s, pos=npos)` / `find_last_not_of(std::string_view sv, pos=npos)` | 末个不匹配（C 字符串/string_view） |
| `find_last_not_of(const char* s, pos, n)` | 末个不匹配（三参） |
| `count(char32_t)` / `count(const utf8pp&)` / `count(const char*)` / `count(std::string_view)` | 统计出现次数 |
| `contains(char32_t)` / `contains(const utf8pp&)` / `contains(const char*)` / `contains(std::string_view)` | 包含判断 |
| `starts_with(char32_t)` / `starts_with(const utf8pp&)` / `starts_with(const char*)` / `starts_with(std::string_view)` | 前缀判断 |
| `ends_with(char32_t)` / `ends_with(const utf8pp&)` / `ends_with(const char*)` / `ends_with(std::string_view)` | 后缀判断 |

#### 比较与转换

| 接口 | 说明 |
|------|------|
| `compare(const utf8pp&)` / `compare(const char*)` / `compare(std::string_view)` | 三态比较（字节序 = 码点序） |
| `compare(const std::string&)` / `compare(const std::u8string&)` / `compare(const std::u32string&)` / `compare(char32_t cp)` | 与 std 字符串/单码点比较 |
| `compare(pos, n, const utf8pp&)` / `compare(pos, n, const char*)` / `compare(pos, n, std::string_view)` | 子串比较（本串 [pos, pos+n)） |
| `compare(pos1, n1, const utf8pp& s, pos2, n2)` / `compare(pos1, n1, const char* s, n2)` | 双区间比较 |
| `operator==/!=/</>/<=/>=` | 与 `utf8pp` / `const char*` / `std::string_view` / `std::string` / `std::u8string` / `std::u32string` / `char32_t` 比较 |
| `operator<=>(const utf8pp&)` / `operator<=>(const char*)` / `operator<=>(std::string_view)` | C++20 三路比较 |
| `operator<=>(const std::string&)` / `operator<=>(const std::u8string&)` / `operator<=>(const std::u32string&)` / `operator<=>(char32_t)` | C++20 三路比较扩展 |
| `to_std_string()` | 转 `std::string` |
| `to_u32string()` | 转 `std::u32string` |
| `to_u8string()` | 转 `std::u8string` |

#### BOM / 校验

| 接口 | 说明 |
|------|------|
| `has_bom()` | 是否以 UTF-8 BOM 开头 |
| `strip_bom()` | 剥离 UTF-8 BOM |
| `valid()` | 整串是否为合法 UTF-8 |
| `validate()` | 返回首个非法码点索引（全部合法返回 `npos`） |

#### split / join / split_view

| 接口 | 说明 |
|------|------|
| `split(char32_t delim)` | 按单码点分割，返回 `dense<utf8pp>` |
| `split(const utf8pp& delim)` / `split(const char*)` / `split(std::string_view)` | 按子串分割 |
| `split_view(char32_t delim)` / `split_view(const utf8pp& delim)` | 零拷贝分割，返回 `dense<utf8_view>`（复用原字符串内存，原串生命周期需覆盖视图使用） |
| `split_view(const char*)` / `split_view(std::string_view)` | 零拷贝分割（C 字符串/string_view） |
| `split_to(char32_t, std::vector<utf8pp>&)` / `split_to(const utf8pp&, std::vector<utf8pp>&)` | 分割到 std 容器 |
| `split_to(const char*, std::vector<utf8pp>&)` / `split_to(std::string_view, std::vector<utf8pp>&)` | 分割到 std 容器（C 字符串/string_view） |
| `split_to(const utf8pp&, utf8pp* out, size_t cap)` / `split_to(const char*, utf8pp* out, size_t cap)` / `split_to(std::string_view, utf8pp* out, size_t cap)` | 分割到裸指针缓冲 |
| `static join(const dense<utf8pp>&, const utf8pp&)` / `static join(..., char32_t)` | 拼接 |
| `static join(const std::array<utf8pp, N>&, const utf8pp&)` / `static join(const std::vector<utf8pp>&, ...)` | 拼接 std 容器 |
| `static join(const utf8pp* parts, size_t count, const utf8pp&)` | 拼接裸指针 |

### 35.3 utf8pp 非成员接口

| 接口 | 说明 |
|------|------|
| `operator+(const utf8pp&, const utf8pp&)` / `operator+(const utf8pp&, char32_t)` / `operator+(char32_t, const utf8pp&)` | 拼接 |
| `operator+(const utf8pp&, const char*)` / `operator+(const char*, const utf8pp&)` | 与 C 字符串拼接 |
| `operator+(const utf8pp&, std::string_view)` / `operator+(std::string_view, const utf8pp&)` | 与 string_view 拼接 |
| `operator+(const utf8pp&, const char8_t*)` / `operator+(const char8_t*, const utf8pp&)` | 与 char8_t 字符串拼接 |
| `operator+(const utf8pp&, const std::string&)` / `operator+(const std::string&, const utf8pp&)` | 与 std::string 拼接 |
| `operator+(const utf8pp&, const std::u8string&)` / `operator+(const std::u8string&, const utf8pp&)` | 与 std::u8string 拼接 |
| `operator+(const utf8pp&, const std::u32string&)` / `operator+(const std::u32string&, const utf8pp&)` | 与 std::u32string 拼接 |
| `operator<<(std::ostream&, const utf8pp&)` | 流输出（写字节内容） |
| `operator>>(std::istream&, utf8pp&)` | 流输入（追加读取） |
| `getline(std::istream&, utf8pp&, char delim='\n')` | 按分隔符读取一行（自由函数） |
| `swap(utf8pp&, utf8pp&) noexcept` | 非成员 swap |
| `to_utf8pp(int/long/long long/unsigned/unsigned long/unsigned long long)` | 数字 → utf8pp（类似 `std::to_string`） |
| `to_utf8pp(float/double/long double)` | 浮点 → utf8pp |
| `utf8pp_format(const char* fmt, ...)` / `utf8pp_vformat(const char* fmt, std::va_list ap)` | 自由函数版格式化（与静态成员 `format`/`vformat` 并存） |
| `"..."_u8` (const char*) / `"..."_u8` (const char8_t*) | 字面量运算符（返回 utf8pp，两个重载） |
| `std::hash<utf8pp>` | hash 特化（**FNV-1a 字节哈希**，分布更均匀） |
| `erase(utf8pp&, char32_t cp)` / `erase_if(utf8pp&, Pred pred)` | 全局删除所有匹配码点（C++20 风格，返回移除数量） |
| `std::formatter<utf8pp>` | C++20 `std::format` 特化（受 `__cpp_lib_format >= 201907L` 保护，委托 `std::formatter<std::string_view>`） |

### 使用

```cpp
#include "part/utf8pp.hpp"

// === 底层编解码函数 ===
char32_t ch = to_char(0x4E2D);       // '中'
uint32_t cps[16];
size_t n = utf8_to_codepoints("你好", 6, cps, 16);  // n = 2

// === utf8pp 字符串类 ===
utf8pp s("Hello你好");     // 从 UTF-8 C 字符串构造
s.size();                  // 7 (码点数)
s.byte_size();             // 11 (字节数: 5 + 6)
s.at(0);                   // 'H' (O(1) via 偏移缓存)
s.at(5);                   // '你' (U+4F60)

// 码点级迭代
for (char32_t cp : s) { /* 遍历每个码点 */ }

// 反向迭代
for (auto it = s.rbegin(); it != s.rend(); ++it) { /* 反向遍历 */ }

// 追加/插入/删除
s.push_back(char32_t(0x1F600));   // 追加 emoji
s.insert(0, char32_t('X'));       // 在首位插入
s.erase(0, 1);                    // 删除首位码点
s.pop_back();                     // 删除末尾
s.append_cp(3, U'-');             // 追加 3 个 '-'
s.resize_cp(10, U'.');            // 调整到 10 码点，补 '.'

// 子串/查找/比较
utf8pp sub = s.substr(0, 3);      // 取前 3 个码点
s.find(char32_t('H'));            // 码点级查找
s.rfind(U'好');                   // 逆向查找
s.contains("Hello");              // 包含判断
s.starts_with(U'H');              // 前缀判断
s.ends_with(U'好');               // 后缀判断
s.count(U'l');                    // 统计出现次数
s == utf8pp("Hello你好");

// 查找系列
s.find_first_of(utf8pp("aeiou"));
s.find_last_not_of(U' ');

// 字符串修改
utf8pp t("  Hello  World  ");
t.trim();                         // "Hello  World"
t.to_lower();                     // "hello  world"  (完整 Unicode)
t.to_title();                     // 首字母大写
t.swapcase();                     // 大小写互换
t.replace(0, 5, utf8pp("Hi"));    // "Hi   World"
t.replace_all(U'l', U'L');        // 全局替换单码点
t.reverse();                      // 码点级反转

// trim 谓词版 / 字符集版
utf8pp csv(",,abc,,");
csv.trim(U',');                   // "abc"  (字符集版)
csv.trim([](char32_t c){ return c == U',' || c == U' '; });  // 谓词版

// 对齐填充
utf8pp num("42");
num.pad_left(6, U'0');            // "000042"
utf8pp centered = utf8pp("Hi").centered(10, U'-');  // "----Hi----"

// NFC / NFD / NFKC / NFKD 规范化
utf8pp e_acute = utf8pp(U"\u00E9");           // é (预组合)
e_acute.to_nfd();                  // → e + U+0301 (分解, size==2)
utf8pp e_acute2("e");
e_acute2.push_back(U'\u0301');
e_acute2.to_nfc();                 // → é (组合, size==1)

utf8pp hangul(U"\uAC00");          // 가 (Hangul 音节)
hangul.to_nfd();                   // → ㄱ + ㅏ (Hangul 算法分解)

utf8pp fullwidth(u8"\uFF21\uFF22\uFF23");  // ＡＢＣ
fullwidth.to_nfkd();               // → ABC (兼容性分解: 全角→半角)

utf8pp lig(u8"\uFB01");            // ﬁ (连字)
lig.to_nfkd();                     // → fi (连字分解)

// 字节级访问
utf8pp cn(u8"Hi中");              // 5 字节: H i 中(3)
cn.byte_at(0);                    // 'H'
cn.byte_substr(2, 3);             // "中" (字节切片)
cn.byte_to_cp_idx(2);             // 2 (字节索引→码点索引)
cn.cp_to_byte_idx(2);             // 2 (码点索引→字节索引)

// 字符分类
utf8pp::is_alpha(U'A');           // true
utf8pp::is_digit(U'5');           // true
utf8pp("12345").is_all_digit();   // true
utf8pp("Hello").is_all_alpha();   // true

// 数字转换
utf8pp num_str("3.14");
num_str.to_double();              // 3.14
num_str.parse_double(d);          // true, d=3.14 (严格解析)
utf8pp("ff").to_int(nullptr, 16); // 255 (十六进制)
utf8pp("42").is_integer();        // true

// format 静态方法
utf8pp fmt = utf8pp::format("x=%d, y=%.2f", 10, 3.14);

// 直接内存修改（逃生舱）
utf8pp mem("Hello");
char* p = mem.data();
p[0] = 'h';                       // 直接改字节
mem.rebuild_cp_offsets();         // 重建码点偏移缓存（长度未变）
// 若长度变化: mem.rebuild(new_byte_size);

// split / join
dense<utf8pp> parts = utf8pp("a,b,c,d").split(U',');
utf8pp joined = utf8pp::join(parts, utf8pp("-"));  // "a-b-c-d"

// split_view 零拷贝（视图复用原串内存，原串生命周期需覆盖视图使用）
utf8pp src("a,b,c");
dense<utf8_view> views = src.split_view(U',');  // 3 个 utf8_view

// 兼容 std::vector / 裸指针输出
std::vector<utf8pp> vparts;
utf8pp("x/y/z").split_to(U'/', vparts);

utf8pp arr[8];
utf8pp("1 2 3").split_to(utf8pp(" "), arr, 8);

// 拼接运算符
utf8pp full = utf8pp("Hello") + U',' + utf8pp(" World");
utf8pp cat = utf8pp("a") + std::string("b") + std::u8string(u8"c");

// 字面量
auto u = "Hello"_u8;              // 返回 utf8pp (const char* 重载)
auto u2 = u8"你好"_u8;            // 返回 utf8pp (const char8_t* 重载)

// 数字 → utf8pp
utf8pp n1 = to_utf8pp(42);
utf8pp n2 = to_utf8pp(3.14);

// 全局 erase / erase_if
utf8pp e("Hello");
erase(e, U'l');                   // "Heo" (移除所有 'l')
erase_if(e, [](char32_t c){ return c == U'H'; });  // "eo"

// C++20 std::format
std::string s = std::format("{}", utf8pp("测试"));

// 空对象 c_str() 返回 ""（非 nullptr）
utf8pp empty;
empty.c_str();                    // ""
empty.data();                     // ""
empty.empty();                    // true
empty.is_sso();                   // true (空串走 SSO)

// BOM 处理
utf8pp bom(u8"\uFEFFHello");
bom.has_bom();                    // true
bom.strip_bom();                  // 剥离后内容为 "Hello"

// 校验
utf8pp bad("abc\xff\xfe");
bad.valid();                      // false
bad.validate();                   // 返回首个非法码点索引

// 流 I/O
std::ostringstream os;
os << utf8pp("测试");
std::istringstream is("line1\nline2");
utf8pp line;
getline(is, line);                // 读取一行
```

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 输入非 UTF-8 编码（如 GBK） | 解码出非法码点，被替换为 U+FFFD | 输入必须是合法 UTF-8 |
| 假设 `size()` 等于 `byte_size()` | 多字节字符两者不等 | 用 `size()` 取码点数，`byte_size()` 取字节数 |
| 越界访问 `at()` 不检查返回值 | 越界返回 U+FFFD，非崩溃 | 检查索引或用 `operator[]`（同样返回 U+FFFD） |
| 越界访问 `at_byte()` | 越界 `std::abort`（与 `byte_at` 返回 `'\0'` 不同） | 用 `byte_at` 容错或确保索引合法 |
| 频繁 `insert`/`erase` 中间位置 | 每次需移动后续字节和偏移数组 | 高频中间修改考虑用其他数据结构 |
| 修改操作后偏移缓存失效 | `insert`/`erase` 已自动维护偏移 | 无需手动更新，但操作有 O(n) 代价 |
| 直接 `data()` 写入后不重建偏移 | 码点偏移缓存与实际内容不一致，`at`/`size` 错乱 | 写入后调用 `rebuild_cp_offsets()` 或 `rebuild(new_size)` |
| `byte_substr` 切断多字节字符 | 不校验 UTF-8 边界，可能产生非法序列 | 仅用于已知边界的场景，或用码点级 `substr` |
| 对 `to_lower()` / `to_upper()` 期望 Unicode 大小写 | 已支持完整 Unicode | 覆盖 Latin/Greek/Cyrillic/Armenian/CJK 等主要脚本 |
| 期望 emoji ZWJ 序列作为单字符 | 码点级 `reverse`/`split` 会拆散 | 用 `grapheme_begin`/`grapheme_end` 按字形簇迭代 |
| 用 `capacity()` 判断码点容量 | `capacity()` 是字节容量，非码点容量 | 用 `cp_capacity()` 取码点容量，`reserve_cp()` 预留 |
| `split_view` 返回的视图悬垂 | 视图复用原串内存，原串析构后视图失效 | 确保原 `utf8pp` 生命周期覆盖所有 `utf8_view` 使用 |
| `to_int` 失败返回 0 无法区分 | 0 可能是合法值 | 严格场景用 `parse_int(out, base)` 返回 bool |
| 误用 `to_nfc()` 处理兼容性字符 | NFC 不分解兼容性字符（如全角、连字） | 兼容性场景用 `to_nfkc()`/`to_nfkd()` |
| 误用 `to_nfd()` 分解 Hangul 音节 | NFD 已支持 Hangul 算法分解（가→ㄱ+ㅏ） | Hangul 分解直接用 `to_nfd()`/`to_nfkd()` |
| 期望 NFKC 保持 A+组合标记不组合 | NFKC 包含 NFC 全部组合规则，A+U+0301→Á | 仅需兼容性分解不需组合时用 `to_nfkd()` |
| `script_of` 对组合标记返回 `inherited` | 组合标记继承前字符脚本 | 需上下文脚本时由调用者跟踪前一个 starter |

---

## 36. utf8_view — UTF-8 字符串视图

`#include "part/utf8_view.hpp"`（自动包含 `utf8_codec.hpp`），全局命名空间。所有接口 `noexcept`。

非拥有型轻量级 UTF-8 字符串视图，类似 `std::string_view`，但提供码点级接口。内部仅存储 `const char*` 与字节数两个字段（16 字节），不分配内存。

**复杂度分层**：

- 字节级操作 O(1)：`byte_size()` / `data()` / `substr_bytes()` / `find_byte()` / `remove_prefix()` / `remove_suffix()` / `copy()` 等。
- 码点级操作 O(n)：`size()` / `at()` / `substr()` / `find()` / `find_first_of()` 等（无偏移缓存，需遍历）。
- 与 `utf8pp` 互转：`utf8pp` 可经 `std::string_view` 中转构造自 `utf8_view`（`utf8_view` 隐式转 `std::string_view`，`utf8pp` 接收 `std::string_view`）；`utf8_view` 可由 `utf8pp` 的 `data()` + `byte_size()` 显式构造。

### 36.1 构造与赋值

| 接口 | 说明 |
|------|------|
| `utf8_view()` | 默认构造，空视图 |
| `utf8_view(const char* s)` / `utf8_view(const char* s, size_t byte_len)` | 从 C 字符串构造 |
| `utf8_view(const char8_t* s)` / `utf8_view(const char8_t* s, size_t byte_len)` | 从 `char8_t` 构造 |
| `utf8_view(std::string_view sv)` | 从 `string_view` 构造 |
| `utf8_view(std::u8string_view sv)` | 从 `u8string_view` 构造 |
| `utf8_view(const std::string& s)` / `utf8_view(const std::u8string& s)` | 从 std 字符串构造 |
| `operator=(const char*)` / `operator=(std::string_view)` | 赋值 |

### 36.2 容量

| 接口 | 说明 |
|------|------|
| `byte_size()` / `size_bytes()` / `length_bytes()` | 字节数（O(1)） |
| `size()` / `length()` | 码点数（O(n)） |
| `empty()` | 是否为空 |
| `max_size()` | 理论最大值 |

### 36.3 数据访问

| 接口 | 说明 |
|------|------|
| `data()` / `c_str()` | 原始字节指针（`data()` 空视图返回 `nullptr`，`c_str()` 返回 `""`） |
| `byte_view()` / `operator std::string_view()` | 转 `std::string_view`（隐式转换） |
| `byte_at(i)` | 字节级访问（O(1)，越界返回 `'\0'`） |
| `at(cp_idx)` / `operator[](cp_idx)` | 码点级访问（O(n)，越界返回 U+FFFD） |
| `front()` / `back()` | 首/尾码点（空视图返回 U+FFFD） |

### 36.4 迭代器

| 接口 | 说明 |
|------|------|
| `begin()` / `end()` / `cbegin()` / `cend()` | 码点级前向迭代器（`forward_iterator_tag`，仅支持 `++`） |
| `rbegin()` / `rend()` / `crbegin()` / `crend()` | 码点级反向迭代器（`bidirectional_iterator_tag`，支持 `++`/`--`） |
| `iterator` / `reverse_iterator` | 类型别名（等价 `const_iterator` / `const_reverse_iterator`） |
| `const_iterator::ptr()` | 暴露底层 `const char*`（用于与 C API 交互） |

> 注：正向迭代器为 forward（不支持 `--`），反向迭代器为 bidirectional（支持 `--`）。两者解引用均按值返回 `char32_t`（无 `->` 运算符）。

### 36.5 子串与修改

| 接口 | 说明 |
|------|------|
| `substr_bytes(byte_pos, byte_len=npos)` | 字节级子串（O(1)） |
| `substr(cp_pos, cp_count=npos)` | 码点级子串（O(n)） |
| `remove_prefix(byte_n)` | 移除前缀（STL 语义，字节级） |
| `remove_suffix(byte_n)` | 移除后缀（字节级） |
| `copy(char* buf, size_t byte_n, byte_pos=0)` | 拷贝到外部缓冲区 |
| `swap(utf8_view&)` / `swap(utf8_view&, utf8_view&)` | 交换 |

### 36.6 查找

| 接口 | 说明 |
|------|------|
| `find_byte(char c, byte_pos=0)` | 字节正向查找（memchr 优化） |
| `rfind_byte(char c, byte_pos=npos)` | 字节逆向查找 |
| `find_bytes(std::string_view str, byte_pos=0)` | 字节子串正向查找 |
| `rfind_bytes(std::string_view str, byte_pos=npos)` | 字节子串逆向查找 |
| `find(char32_t cp, cp_pos=0)` / `find(const utf8_view&, cp_pos=0)` | 码点级正向查找 |
| `rfind(char32_t cp, cp_pos=npos)` / `rfind(const utf8_view&, cp_pos=npos)` | 码点级逆向查找 |
| `find_first_of(char32_t, cp_pos=0)` / `find_first_of(const utf8_view&, cp_pos=0)` | 首个匹配 |
| `find_last_of(char32_t, cp_pos=npos)` / `find_last_of(const utf8_view&, cp_pos=npos)` | 末个匹配 |
| `find_first_not_of(char32_t, cp_pos=0)` / `find_first_not_of(const utf8_view&, cp_pos=0)` | 首个不匹配 |
| `find_last_not_of(char32_t, cp_pos=npos)` / `find_last_not_of(const utf8_view&, cp_pos=npos)` | 末个不匹配 |
| `contains(char32_t)` / `contains(const utf8_view&)` | 包含判断 |
| `starts_with(char32_t)` | 码点级前缀判断（O(n)） |
| `starts_with(const utf8_view&)` | 字节级前缀判断（O(1) memcmp） |
| `ends_with(char32_t)` | 码点级后缀判断（O(n)） |
| `ends_with(const utf8_view&)` | 字节级后缀判断（O(1) memcmp） |

### 36.7 比较

| 接口 | 说明 |
|------|------|
| `compare(const utf8_view&)` / `compare(std::string_view)` / `compare(const char*)` | 三态比较（memcmp） |
| `operator==` / `!=` / `<` / `>` / `<=` / `>=` | 与 `utf8_view` / `std::string_view` / `const char*` 比较 |
| `operator<=>(const utf8_view&)` / `operator<=>(std::string_view)` / `operator<=>(const char*)` | 三态比较运算符 |

### 36.8 非成员接口

| 接口 | 说明 |
|------|------|
| `operator<<(std::ostream&, const utf8_view&)` | 流输出 |
| `std::hash<utf8_view>` | hash 特化（按字节 hash） |

### 使用

```cpp
#include "part/utf8_view.hpp"

// === 构造 ===
utf8_view v1("Hello你好");          // C 字符串
utf8_view v2("Hello你好", 11);      // 显式字节长度
utf8_view v3(std::string_view("abc"));
utf8_view v4(u8"中文");

// === 容量 ===
v1.byte_size();                  // 11 (O(1))
v1.size();                       // 7   (O(n), 码点数)
v1.empty();                      // false

// === 访问 ===
v1.byte_at(0);                   // 'H' (O(1))
v1.at(0);                        // 'H' (O(n))
v1.at(5);                        // '你' (U+4F60)
v1.front();                      // 'H'
v1.back();                       // '好'

// === 迭代器 ===
for (char32_t cp : v1) { /* 码点级遍历 */ }
for (auto it = v1.rbegin(); it != v1.rend(); ++it) { /* 反向 */ }

// === 子串 ===
utf8_view b1 = v1.substr_bytes(0, 5);   // "Hello" (O(1))
utf8_view b2 = v1.substr(0, 5);         // "Hello你" (O(n), 取 5 个码点)
v1.remove_prefix(6);                    // 移除前 6 字节 → "你好"
v1.remove_suffix(3);                    // 移除后 3 字节

// === 查找 ===
v1.find_byte('l');                      // 2 (O(1) memchr)
v1.rfind_byte('l');                     // 3
v1.find_bytes("ll");                    // 字节子串查找
v1.find(U'好');                         // 码点级查找 (O(n))
v1.rfind(U'好');
v1.find_first_of(utf8_view("aeiou"));
v1.find_last_not_of(U' ');
v1.contains(U'好');
v1.starts_with(U'H');
v1.ends_with(U'好');

// === 比较 ===
v1 == utf8_view("Hello你好");
v1 < utf8_view("World");
auto cmp = v1 <=> utf8_view("Hello");

// === 拷贝 ===
char buf[32];
size_t n = v1.copy(buf, sizeof(buf));

// === 与 utf8pp 互转 ===
utf8pp owned(v1.byte_view());          // 经 string_view 中转构造 utf8pp
utf8_view from_owned = utf8_view(owned.data(), owned.byte_size());

// === 流输出 ===
std::cout << v1;

// === 注意: 视图不持有内存, 源生命周期需保证 ===
utf8_view dangling;
{
    std::string temp = "hello";
    dangling = utf8_view(temp);    // temp 析构后 dangling 失效
}
// dangling.data() 已是悬垂指针
```

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 视图持有内存的假设 | `utf8_view` 不拥有内存，源对象析构后视图悬垂 | 确保源对象生命周期覆盖视图使用范围 |
| 用 `size()` 做字节操作 | `size()` 是码点数（O(n)），非字节数 | 用 `byte_size()` 取字节数 |
| 高频调用 `size()` / `at()` | 每次 O(n) 遍历 | 高频场景转 `utf8pp` 用偏移缓存 |
| `remove_prefix(n)` 传码点数 | `remove_prefix` 是字节级 | 码点级用 `substr(cp_pos, cp_count)` |
| 对视图调用 `to_lower()` / `split()` | 视图只读，无修改方法 | 用 `utf8pp` 或自行转换 |
| 字节级 `find_byte` 与码点级 `find` 混用 | 字节查找返回字节位置，码点查找返回码点索引 | 阅读接口前缀区分（`find_byte` vs `find`） |
| `starts_with(char32_t)` 与 `starts_with(utf8_view)` 混用 | 前者 O(n) 码点级，后者 O(1) 字节级 memcmp | 按场景选择，注意参数类型 |
| 正向迭代器 `--it` | 正向迭代器为 `forward_iterator_tag`，不支持 `--` | 用反向迭代器 `rbegin()`/`rend()` |
| 越界 `at()` / `front()` / `back()` | 返回 U+FFFD，非崩溃 | 检查索引或用 `operator[]` |
| 越界 `byte_at()` | 返回 `'\0'`，非崩溃 | 检查索引 |
| `data()` 与 `c_str()` 空视图返回值不同 | `data()` 返回 `nullptr`，`c_str()` 返回 `""` | 需要 C 字符串语义时用 `c_str()` |
| 期望 BOM / 校验接口 | `utf8_view` 不提供 BOM / 校验 | 用 `utf8pp::has_bom()` / `utf8pp::valid()` |
| 期望 `operator>>` / 字面量 / `to_string` | `utf8_view` 不提供这些接口 | 用 `utf8pp` 或经 `std::string_view` 中转 |
| 比较运算期望码点序 | 比较基于 `memcmp` 字节序（与 `std::string_view` 一致） | 字节序 = 码点序（UTF-8 特性），但需注意 |