window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['entity'] = {
  id: 'entity',
  title: "ecs::entity — 实体",
  category: 'ecs',
  icon: 'E',
  order: 1,
  content: `## 1. ecs::entity — 实体

实体是轻量级句柄，由 32 位索引和 32 位版本号组成，合并存储在 64 位 \`handle_\` 中。

### 接口

| 接口 | 说明 |
|------|------|
| \`entity()\` | 默认构造，\`handle_=0\`，无效实体 |
| \`entity(uint32_t idx, uint32_t ver)\` | 指定索引和版本号构造 |
| \`is_valid()\` | 判断实体是否有效（\`handle_ != 0\`） |
| \`parts_.index_\` | 实体索引（32 位） |
| \`parts_.version_\` | 实体版本号（32 位） |
| \`handle_\` | 64 位句柄（与 index_ + version_ 共用联合体） |
| \`operator==\` | 判断两个实体是否相等（比较 handle_） |
| \`operator!=\` | 判断两个实体是否不等 |
| \`std::hash<entity>\` | 哈希特化，可用于 \`std::unordered_map\` |

### 使用

\`\`\`cpp
entity e1;                    // 默认构造，无效实体
entity e2(3, 1);              // index=3, version=1
e2.is_valid();                // true
e2.parts_.index_;             // 3
e2.parts_.version_;           // 1

std::unordered_map<entity, int> map;
map[e2] = 42;                 // 可用作哈希键
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 手动构造 entity 的 parts_.index_ 和 parts_.version_ | 版本号不匹配会导致 ECS 管理器认为实体无效 | 始终通过 \`manager::create_entity()\` 创建实体 |
| 复用已删除的 entity 句柄 | 版本号已递增，旧句柄失效 | 删除后丢弃句柄，重新创建 |
| 将 entity 内部字段当作普通整数运算 | \`parts_.index_\` 和 \`parts_.version_\` 不应直接操作 | 仅通过公开接口操作 entity |

---
`
};

window.DOCS_DATA['view_tags'] = {
  id: 'view_tags',
  title: "view_tags — 视图标签类型",
  category: 'ecs',
  icon: 'V',
  order: 2,
  content: `## 2. view_tags — 视图标签类型

位于 \`ecs\` 命名空间。用于构造 View / Group / runtime_view 的标签参数。

### 接口

| 标签 | 类型 | 说明 |
|------|------|------|
| \`without<Types...>\` | \`without_t<Types...>\` | 排除含有任一 Types 组件的实体 |
| \`with<Types...>\` | \`with_t<Types...>\` | 额外获取 Types 组件的指针（可能为 \`nullptr\`，判空责任在用户） |
| \`exclude<Types...>\` | \`without_t<Types...>\` | \`without\` 的别名 |
| \`get<Types...>\` | \`with_t<Types...>\` | \`with\` 的别名 |
| \`owned<Types...>\` | \`owned_t<Types...>\` | 标记 Types 为 Group 所拥有（重排 dense 数组） |
| \`reorder<Types...>\` | \`reorder_t<Types...>\` | 标记 Types 为 Group 可重排（轻量 owned 语义） |
| \`ordered<Types...>\` | \`struct\` | 标记排序顺序 |
| \`Component<T>\` | concept | \`is_copy_constructible_v<T> \\|\\| is_move_constructible_v<T>\` |

### 使用

\`\`\`cpp
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
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`without\` 和 \`with\` 传同一类型 | 语义矛盾，行为未定义 | 不要对同一类型同时使用 |
| \`owned\` 标记非首模板参数 | Group 要求 owned 必须是首参数 | \`group<First, Rest...>(owned<First>{})\` |
| \`reorder\` 和 \`owned\` 对同一 Group 混用 | 语义冲突 | 一个 Group 只用 \`owned\` 或 \`reorder\`，不混用 |

---
`
};

window.DOCS_DATA['single_class_set'] = {
  id: 'single_class_set',
  title: "ecs::single_class_set — 单组件集合",
  category: 'ecs',
  icon: 'S',
  order: 3,
  content: `## 3. ecs::single_class_set — 单组件集合

管理单一类型组件的存储。稀疏表为扁平 \`sparse_entry\` 数组（8B/条目：dense 索引 + version），\`dense_invalid\` 兼作墓碑标记，单次内存访问完成判活与取值。

### sparse 访问

| 接口 | 说明 |
|------|------|
| \`sparse_dense_at(uint32_t idx)\` | 获取稀疏条目的 dense 索引，不存在返回 \`dense_invalid\` (0xFFFFFFFF) |
| \`sparse_version_at(uint32_t idx)\` | 获取稀疏条目的 version，不存在返回 0 |
| \`dense_invalid\` | 无效 dense 索引常量（0xFFFFFFFF） |
| \`get_sparse_size()\` | 稀疏表已使用的最大索引+1 |
| \`clear_hot_set()\` | 清空热集缓存（调试用，正常使用无需手动调用） |
| \`bump_pool_version()\` | 递增池版本号，使所有热集缓存自动失效 |

### 构造与赋值

| 接口 | 说明 |
|------|------|
| \`single_class_set()\` | 默认构造 |
| \`single_class_set(size_t capacity)\` | 预留容量构造 |
| \`single_class_set(entity e, T&& object)\` | 实体+对象构造 |
| \`single_class_set(single_class_set&&)\` | 移动构造 |
| \`operator=(single_class_set&&)\` | 移动赋值 |

> 注：\`single_class_set\` 禁止拷贝。

### 添加组件

| 接口 | 说明 |
|------|------|
| \`add(entity, T)\` | 添加/覆盖组件（已存在则替换） |
| \`add_batch(span<const entity>, span<const T>)\` | 批量添加（span 版本） |
| \`add_batch(const class_pool<entity>&, const class_pool<T>&)\` | 批量添加（左值引用） |
| \`add_batch(class_pool<entity>&&, class_pool<T>&&)\` | 批量添加（右值引用，移动语义） |

### 运行期 def 组件（type_id::register_type_def 注册的类型）

| 接口 | 说明 |
|------|------|
| \`add_def(entity, def_id, const type_def&, const void* data)\` | 添加 def 组件（平凡类型按字节复制；非平凡类型调用注册的 \`copy_construct\`；首次调用惰性初始化 def 池） |
| \`get_def_ptr(entity)\` | 获取 def 组件指针（hot set 快速路径） |
| \`get_def_ptr(entity) const\` | const 版本 |
| \`get_def_element(dense_index)\` | 按 dense 索引直接访问（遍历用） |
| \`get_def_element(dense_index) const\` | const 版本 |

### 获取组件

| 接口 | 说明 |
|------|------|
| \`get_ptr<T>(entity)\` | 获取组件指针（带有效性、type_id、版本号检查） |
| \`get_ptr<T>(entity) const\` | const 版本 |
| \`get_ptr_fast<T>(entity)\` | 快速获取（跳过 type_id 检查，经缓存指针寻址） |
| \`get_ptr_fast<T>(entity) const\` | const 版本 |
| \`get_ptr_raw<T>(entity)\` | 零检查获取（调用者保证 entity 有效） |
| \`get_ptr_raw<T>(entity) const\` | const 版本 |
| \`get_version(uint32_t entity_index)\` | 获取实体版本号 |
| \`get_version_unchecked(uint32_t entity_index)\` | 无检查获取版本号 |
| \`prefetch_ptr_batch(const entity*, size_t)\` | 批量预取 sparse 条目 |
| \`prefetch_ptr_data<T>(entity)\` | 预取组件数据（按 entity，需先加载 sparse 条目） |
| \`get_ptr_batch(const entity*, T**, size_t)\` | 批量查询组件指针（裸指针 + 长度） |

> 查询接口（\`get_ptr\` / \`get_ptr_fast\` / \`get_ptr_batch\`）查不到时返回 \`nullptr\`：实体句柄无效、实体不含该组件（含软删除与版本号不匹配）、\`T\` 与集合类型不一致。不触发编译期断言，也不产生运行期报错。

> \`select_primary_set(sets, count, out_index = nullptr)\` 为静态辅助：在候选集合列表中选出 \`size()\` 最小者作为遍历主集，返回该集合指针，\`out_index\` 可选回填命中下标；空指针项被跳过，全空时返回 \`nullptr\`（下标回填 0）。

### 删除与清空

| 接口 | 说明 |
|------|------|
| \`hard_remove(entity)\` | 硬删除（交换删除，O(1)；def 池与模板池通用） |
| \`soft_remove(entity)\` | 软删除（仅清除 sparse 条目，不移动组件；死槽登记后可被 \`add\` 复用；墓碑数超阈值时自动回收） |
| \`compact()\` | 密度回收：活条目前压、墓碑物理移除并补发 \`on_remove\` 回调（迭代期禁止调用） |
| \`clear()\` | 清空所有数据 |

> \`soft_remove\` 墓碑的物理移除延迟到 \`compact\`（手动或阈值自动触发，阈值为 \`max(n/4, 64)\`）；\`remove→add\` 循环通过死槽复用保持物理槽位有界。

### 容量与查询

| 接口 | 说明 |
|------|------|
| \`size()\` | 物理槽位数（含软删除墓碑） |
| \`live_count()\` | 活条目数（O(1)，随添加/删除维护） |
| \`tombstone_count()\` | 软删除墓碑数（O(1)） |
| \`has_tombstones()\` | 是否存在软删除死槽（O(1)，视图分派用） |
| \`empty()\` | 是否为空 |
| \`increase_capacity(capacity)\` | 预留容量 |
| \`set_hot_set_capacity(entries)\` | 设置热集容量（2 的幂，非 2 的幂被拒绝；调整后热集清空） |
| \`hot_set_capacity()\` | 获取当前热集容量 |
| \`get_type_id_value()\` | 获取类型 ID 值 |
| \`get_typed_pool_ptr<T>()\` | 获取类型化组件池指针（带 type_id 检查） |
| \`get_typed_pool_ptr<T>() const\` | const 版本 |
| \`add/remove\` 系列接口返回值 | \`operating_message\`，失败时带错误码与级别 |
| \`get_entity_indices()\` | 获取实体索引数组（dense 数组） |
| \`get_entity_indices() const\` | const 版本 |
| \`get_entity_versions()\` | 获取实体版本号数组 |
| \`get_entity_versions() const\` | const 版本 |
| \`get_pool_version()\` | 获取组件池版本号（持久化视图自动同步用） |

### 使用

\`\`\`cpp
single_class_set set;
entity e1(1, 1);
set.add(e1, Position{10, 20});

Position* p = set.get_ptr<Position>(e1);
Position* pf = set.get_ptr_fast<Position>(e1);  // 快速
set.soft_remove(e1);  // 软删除（O(1)，墓碑延迟回收，add 自动复用死槽）
set.compact();        // 密度回收（活条目前压，墓碑物理移除；超阈值时 soft_remove 自动触发）
set.hard_remove(e1);  // 硬删除（swap-pop，立即移除）
set.set_hot_set_capacity(4096);  // 调整热集容量（2 的幂）

// 批量添加
class_pool<entity> ents = {entity(2,1), entity(3,1)};
class_pool<Position> comps = {Position{1,2}, Position{3,4}};
set.add_batch(ents, comps);
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`soft_remove\` 后依赖 \`size()\` 统计存活数 | \`size()\` 为物理槽位数（含墓碑） | 用 \`live_count()\` 获取存活数，\`tombstone_count()\` 获取墓碑数 |
| 迭代中调用 \`compact()\` | 会破坏迭代器位置 | 手动 \`compact()\` 在迭代外调用；自动回收在迭代期自动延后 |
| 在需要频繁增删时只用 \`hard_remove\` | 每次交换删除 O(1) 但破坏顺序 | 可接受顺序变化时用 \`hard_remove\`，需保持顺序时用 \`soft_remove\`（自动回收保持密度） |
| 拷贝 \`single_class_set\` | 禁止拷贝 | 使用移动语义 |
| 批量增删后手动调用 \`clear_hot_set\` | 不必要，pool_version 自动递增已使缓存失效 | 正常使用无需手动调用；调试场景可用 \`clear_hot_set()\` 强制清空 |
| 依赖 \`sparse_dense_at\` 返回值判断条目是否存在 | 需检查返回值是否等于 \`dense_invalid\` | 检查 \`sparse_dense_at(idx) != dense_invalid\`，或使用 \`get_ptr\` 系列接口 |
| 用 \`get_ptr<T>\` 的返回值区分“实体无效”与“组件不存在” | 实体句柄失效、实体未挂载该组件都返回 \`nullptr\` | 判活由 \`manager::is_entity_valid\` 负责，组件查询用 \`get_ptr\` 系列 |

---
`
};

window.DOCS_DATA['manager'] = {
  id: 'manager',
  title: "ecs::manager — ECS管理器",
  category: 'ecs',
  icon: 'M',
  order: 4,
  content: `## 4. ecs::manager — ECS管理器

ECS 核心管理类，管理实体和所有组件集合。

> 注：\`manager\` 可移动，禁止拷贝。
>
> 本章示例中的 \`error\` 均为同一作用域内复用的 \`operating_message\` 对象：\`operating_message error;\`。失败时库写码、级别与站点，成功时不读写该对象。

### 预分配接口

所有高层预分配接口统一以 \`preallocate_\` 前缀命名，在启动阶段一次性预留容量，避免运行期扩容。各项互相独立，按需调用。

| 接口 | 说明 |
|------|------|
| \`preallocate_entities(count)\` | 预分配实体到池中（同时抬升各组件集合容量） |
| \`preallocate_components<T>(capacity)\` | 预留组件 T 的集合容量 |
| \`preallocate_mask_blocks(num_blocks)\` | 预分配每实体掩码块数（每块 64 种组件，注册组件前调用） |
| \`preallocate_entity_signals(n)\` | 预分配实体延迟信号 overflow chain 容量 |
| \`preallocate_component_signals(n)\` | 预分配组件延迟信号 overflow chain 容量 |

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100000);
mgr.preallocate_components<Position>(100000);
mgr.preallocate_components<Velocity>(50000);
mgr.preallocate_mask_blocks(2);
mgr.preallocate_entity_signals(4096);
mgr.preallocate_component_signals(4096);
\`\`\`

### 实体管理

| 接口 | 说明 |
|------|------|
| \`create_entity()\` | 创建实体（优先使用预分配池） |
| \`is_entity_valid(entity)\` | 检查实体有效性（版本号匹配） |
| \`delete_entity(entity)\` | 删除实体及其全部组件（释放 ID，递增版本号），返回 \`operating_message\` |
| \`delete_entity_batch(entities, size_t& deleted_count)\` | 批量删除实体，跳过无效句柄，返回 \`operating_message\` |
| \`clear_entity(entity)\` | 清除实体全部组件，实体本身保留，返回 \`operating_message\` |
| \`alive_count()\` | 当前存活实体数量 |
| \`empty()\` | 是否没有存活实体 |

\`\`\`cpp
ecs::entity e = mgr.create_entity();
if (!mgr.delete_entity(e))
{
    // 句柄无效或已删除
}

size_t removed = 0;
std::span<const ecs::entity> targets(entities.data(), entities.size());
mgr.delete_entity_batch(targets, removed);
\`\`\`

### 添加组件

| 接口 | 说明 |
|------|------|
| \`add<T>(entity, T)\` | 添加组件（实体在前） |
| \`add<T>(T, entity)\` | 添加组件（组件在前，参数顺序可互换） |
| \`addc<T>(entity, T)\` | 链式添加（返回 \`manager&\`） |
| \`addc<T>(T, entity)\` | 链式添加（组件在前） |
| \`addc<T>(T, EEs...)\` | 正向变参链式添加：单组件加到多个实体 \`addc(comp, e1, e2, e3)\` |
| \`addc<TT...>(EE, TT&&...)\` | 反向变参链式添加：多组件加到同一实体 \`addc(e, comp1, comp2, comp3)\` |
| \`add_batch<T>(span<const entity>, span<const T>)\` | 批量添加（span 版本） |
| \`add_batch<T>(const class_pool<entity>&, const class_pool<T>&)\` | 批量添加（左值引用） |
| \`add_batch<T>(class_pool<entity>&&, class_pool<T>&&)\` | 批量添加（右值引用） |
| \`add_batch<T>(const std::vector<entity>&, const std::vector<T>&)\` | 批量添加（vector 入参） |
| \`add_batch<T>(const std::array<entity, N>&, const std::array<T, N>&)\` | 批量添加（array 入参，编译期固定长度） |
| \`add_batch<T>(const entity*, const T*, size_t)\` | 批量添加（裸指针 + 长度） |
| \`create_with<Ts...>(entity& out, Ts&&...)\` | 创建实体并附加组件，失败回滚，返回 \`operating_message\` |
| \`clone(entity source, entity& out)\` | 复制源实体的全部组件与用户状态，失败回滚，返回 \`operating_message\` |

\`\`\`cpp
ecs::entity e{};
if (!mgr.create_with(e, Position{1.0f, 2.0f}, Velocity{3.0f, 4.0f}))
{
    // 创建失败，e 保持无效
}

ecs::entity copy{};
if (!mgr.clone(e, copy))
{
    // 存在不可复制组件或容量不足，copy 保持无效
}
\`\`\`

> \`clone\` 复制全部模板组件、运行期 def 组件、\`tag\` / \`layer\` / \`group\`，以及 \`active\` / \`disabled\` / \`static_entity\` 标志；不复制实体 index/version、组件槽位、\`pending_destroy\`、信号队列、迭代状态和变更日志。任一组件不可复制时返回 \`om_err_unsupported_operation\`，容量不足时返回 \`om_err_capacity_exceeded\`。

### 运行期 def 组件

配合 \`type_id::register_type_def\` 注册的类型使用（\`type_def\` 见 type_id 页）。平凡类型按字节复制；非平凡类型必须提供 \`copy_construct\` 与 \`destruct\`。def 组件与模板组件共用掩码/视图/信号机制。

| 接口 | 说明 |
|------|------|
| \`add_def(entity, def_id, const void* data)\` | 按 ID 添加 def 组件（data 长度为注册时的 \`type_def::size\`） |
| \`add_def(entity, name, const void* data)\` | 按名字添加（按名字查找） |
| \`get_def_ptr(entity, def_id)\` | 按 ID 获取 def 组件指针 |
| \`get_def_ptr(entity, def_id) const\` | const 版本 |
| \`get_def_ptr(entity, name)\` | 按名字获取 |
| \`get_def_ptr(entity, name) const\` | const 版本 |
| \`hard_remove_def(entity, def_id)\` | 按 ID 硬删除 |
| \`hard_remove_def(entity, name)\` | 按名字硬删除 |
| \`soft_remove_def(entity, def_id)\` | 按 ID 软删除 |
| \`soft_remove_def(entity, name)\` | 按名字软删除 |
| \`get_single_class_set_by_id(type_id)\` | 按 ID 获取组件集合（def/模板通用） |

### 获取组件

| 接口 | 说明 |
|------|------|
| \`get_ptr<T>(entity)\` | 获取组件指针（带检查） |
| \`get_ptr<T>(entity) const\` | const 版本 |
| \`get_ptr_fast<T>(entity)\` | 快速获取（跳过 type_id 检查） |
| \`get_ptr_fast<T>(entity) const\` | const 版本 |
| \`get_ptr_batch<T>(entities, results, count)\` | 批量查询组件指针（裸指针 + 长度） |
| \`get_ptr_batch<T>(span<const entity>, span<T*>)\` | 批量查询（span 入参，长度需一致） |
| \`get_ptr_batch<T>(const vector<entity>&, vector<T*>&)\` | 批量查询（vector 入参） |
| \`get_ptr_batch<T>(const array<entity, N>&, array<T*, N>&)\` | 批量查询（array 入参） |
| \`prefetch_ptr<T>(entity)\` | 预取实体 sparse 条目 |
| \`prefetch_ptr_batch<T>(entities, count)\` | 批量预取（裸指针 + 长度） |
| \`prefetch_ptr_batch<T>(span<const entity>)\` | 批量预取（span 入参） |
| \`prefetch_ptr_batch<T>(const vector<entity>&)\` | 批量预取（vector 入参） |
| \`prefetch_ptr_batch<T>(const array<entity, N>&)\` | 批量预取（array 入参） |
| \`prefetch_ptr_data<T>(entity)\` | 预取组件数据（需先加载 sparse 条目获取 dense 索引） |
| \`get_ptr_fast_cached<T>(set, entity)\` | 用缓存的 set 指针快速查询（避免重复 get_single_class_set） |
| \`prefetch_ptr_cached<T>(set, entity)\` | 用缓存的 set 指针预取 sparse 条目 |
| \`prefetch_ptr_data_cached<T>(set, entity)\` | 用缓存的 set 指针预取组件数据 |

> 查询接口（\`get_ptr\` / \`get_ptr_fast\` / \`get_ptr_batch\`）查不到时统一返回 \`nullptr\`：实体句柄无效、实体未挂载该组件、\`T\` 与该组件类型不一致、该组件类型从未注册，四种情形都得到 \`nullptr\`。不触发编译期断言，也不产生运行期报错或提示，需由调用方判空。

> \`get_ptr\` 与 \`get_ptr_fast\` 均可直接使用，无需额外预热或缓存。

### 组件存在性查询

| 接口 | 说明 |
|------|------|
| \`has_component<T>(entity)\` | 实体是否挂载组件（实体无效时为 \`false\`） |
| \`has_all_components<Ts...>(entity)\` | 实体是否挂载全部指定组件 |
| \`has_any_component<Ts...>(entity)\` | 实体是否挂载任一指定组件 |
| \`has_component_batch<T>(span<const entity>, span<uint8_t>)\` | 批量查询，长度不足返回 \`false\` |
| \`component_count<T>()\` | 该组件类型的存活实例数 |
| \`component_count_by_id(int)\` | 按 type_id 统计存活实例数 |

\`\`\`cpp
if (mgr.has_all_components<Position, Velocity>(e))
{
    // 两个组件都在
}

const size_t positions = mgr.component_count<Position>();
\`\`\`

### query_context 查询上下文

| 接口 | 说明 |
|------|------|
| \`query_context<T>(manager&)\` | 构造查询上下文，缓存 set/sparse/pool 指针 |
| \`get_ptr(entity)\` | 内联快速查询组件指针，返回 \`T*\` |
| \`get_ptr(entity) const\` | const 版本，返回 \`const T*\` |
| \`prefetch_sparse(entity) const\` | 预取 sparse 条目 |
| \`prefetch_data(entity) const\` | 预取组件数据 |
| \`valid() const\` | 上下文是否有效（组件类型是否已注册） |

> \`valid()\` 为 false（组件类型未注册）时 \`get_ptr\` 恒返回 \`nullptr\`；实体未挂载该组件时同样返回 \`nullptr\`。不触发编译期断言，也不产生运行期报错。

### 删除组件

| 接口 | 说明 |
|------|------|
| \`soft_remove<T>(entity)\` | 软删除组件（仅清除 sparse，留下空洞） |
| \`hard_remove<T>(entity)\` | 硬删除组件 |
| \`soft_removec<T>(entity)\` | 链式软删除（返回 \`manager&\`） |
| \`hard_removec<T>(entity)\` | 链式硬删除（返回 \`manager&\`） |
| \`hard_removec<TT...>(EEs...)\` | 变参链式硬删除：多类型 × 多实体 笛卡尔积 \`hard_removec<Comp1, Comp2>(e1, e2)\` |
| \`soft_removec<TT...>(EEs...)\` | 变参链式软删除：多类型 × 多实体 笛卡尔积 \`soft_removec<Comp1, Comp2>(e1, e2)\` |
| \`soft_remove_batch<T>(span<const entity>, size_t& removed_count)\` | 批量软删除，返回 \`operating_message\` |
| \`hard_remove_batch<T>(span<const entity>, size_t& removed_count)\` | 批量硬删除，返回 \`operating_message\` |
| \`clear_component_type<T>()\` | 清空整个类型容器（含掩码位与实体状态），返回 \`operating_message\` |

> \`hard_remove\` 和 \`swap_dense_and_pool\` 对 \`std::is_trivially_copyable\` 类型使用 \`typed_pool_data_\` + \`memcpy\` 直接操作，跳过函数指针间接调用。非 trivial 类型回退到 \`ops_.swap_pop\` / \`ops_.swap_pool\` 函数指针路径。

### 容器访问

| 接口 | 说明 |
|------|------|
| \`get_single_class_set<T>()\` | 获取单组件集合指针 |
| \`get_single_class_set<T>() const\` | const 版本 |
| \`get_component_container<T>()\` | 获取类型化组件池指针 |
| \`is_component_type_registered<T>()\` | 查询组件类型是否已在管理器中注册（存在组件集合，不关心是否已有实例） |
| \`add/add_batch/hard_remove/soft_remove\` | 返回 \`operating_message\`，成功时该对象为空 |
| \`get_component_meta(int type_id)\` | 获取组件元数据（含组件大小） |
| \`get_single_class_set_by_id(int type_id)\` | 通过 type_id 获取组件集合（运行时视图用） |
| \`get_entity_manager()\` | 获取 \`entity_manager&\` 引用（暴露掩码 / 状态 / 标志接口） |
| \`get_entity_manager() const\` | const 版本 |

### single_class_set 稀疏表接口

稀疏表使用 \`class_pool<sparse_entry>\` 存储 \`entity_index → {dense_index, version}\` 映射。\`sparse_entry\` 合并存储 dense 索引与 version。

| 接口 | 说明 |
|------|------|
| \`sparse_dense_at(uint32_t idx) const\` | 读取稀疏条目的 dense 索引，不存在返回 \`dense_invalid\` |
| \`sparse_version_at(uint32_t idx) const\` | 读取稀疏条目的 version，不存在返回 0 |
| \`sparse_find(uint32_t idx, uint32_t& out_version) const\` | 合并查询：单次加载 sparse_entry，返回 dense 索引并输出 version |
| \`dense_invalid\` | 无效 dense 索引常量（\`0xFFFFFFFFu\`），public 静态成员 |
| \`get_sparse_size() const\` | 稀疏表已覆盖的最大索引+1 |
| \`prefetch_sparse_entry(uint32_t idx) const\` | 预取 sparse 条目 |
| \`clear_hot_set()\` | 清空热集缓存（调试用，正常使用无需手动调用） |
| \`bump_pool_version()\` | 递增池版本号，使所有热集缓存自动失效 |

\`\`\`cpp
auto* set = mgr.get_single_class_set<Position>();

// 查询实体的 dense 索引和 version
uint32_t ver = 0;
uint32_t dense = set->sparse_find(entity_index, ver);

if (dense == single_class_set::dense_invalid)
{
    // 实体未注册该组件
}

// 清空热集缓存
set->clear_hot_set();
\`\`\`

### 信号与追踪开关

| 接口 | 说明 |
|------|------|
| \`disable_comp_signals()\` | 禁用组件延迟信号入队 |
| \`enable_comp_signals()\` | 启用组件延迟信号入队 |
| \`disable_entity_signals()\` | 禁用实体延迟信号入队 |
| \`enable_entity_signals()\` | 启用实体延迟信号入队 |
| \`disable_track_changes()\` | 禁用版本追踪 |
| \`enable_track_changes()\` | 启用版本追踪 |

### 信号溢出与容量

| 接口 | 说明 |
|------|------|
| \`comp_signal_overflow_count()\` | 组件信号溢出到 chain 的累计次数 |
| \`entity_signal_overflow_count()\` | 实体信号溢出到 chain 的累计次数 |
| \`reset_comp_signal_overflow_count()\` | 清零组件溢出计数 |
| \`reset_entity_signal_overflow_count()\` | 清零实体溢出计数 |
| \`preallocate_component_signals(n)\` | 预分配组件溢出 chain 容量 |
| \`preallocate_entity_signals(n)\` | 预分配实体溢出 chain 容量 |

### View系统

| 接口 | 说明 |
|------|------|
| \`view<T>()\` | 单组件视图 |
| \`view<T>().for_each(func)\` | 单组件视图 + 遍历 |
| \`view<First, Second, Rest...>()\` | 多组件视图 |
| \`view<T>(without<Types...>)\` | 排除视图 |
| \`view<T>(with<Types...>)\` | 获取视图 |
| \`view_or<A, B>()\` | OR视图 |
| \`view_filtered<T>(Pred)\` | 谓词过滤视图 |
| \`view<T>().page(offset, limit)\` | 分页视图（链式） |
| \`view<T>().sorted_by_component<T>(cmp)\` | 排序视图（链式） |
| \`view<T>().sorted_by_component_value(keyFn)\` | 分组视图（链式） |
| \`view<T>().track_changes()\` | 变更检测视图（链式） |
| \`view<T>().filter_changed()\` | 逐实体变更检测（链式） |
| \`view<T>().filter_added()\` | 逐实体添加检测（链式） |
| \`view_any_of<Types...>()\` | N元OR视图（任意组件匹配） |
| \`view<T>().exactly_one()\` | 精确获取单个实体组件 |
| \`view<First, Rest...>().exactly_one()\` | 精确获取单个实体多组件 |
| \`view<First, Rest...>().find_one(entity)\` | 查询指定实体多组件 |
| \`view<First, Rest...>().iter_over_entities(entities)\` | 批量指定实体查询 |

### 分级排序

位于 \`detail\` 命名空间。

| 接口 | 说明 |
|------|------|
| \`tiered_sort<T>(data, n, cmp)\` | 分级排序值数组 |
| \`tiered_sort_indices<T>(indices, values, n)\` | 索引排序，按 values[indices[i]] 升序排列 indices |

**约束**：

- \`tiered_sort\` 要求 \`T\` 满足 \`std::is_trivially_copyable_v\`
- \`tiered_sort_indices\` 用于索引数组，按 \`values[indices[i]]\` 升序排列
- 两者均为 \`noexcept\`

\`\`\`cpp

// 值排序
int data[] = {5, 3, 1, 4, 2};
tiered_sort(data, 5, std::less<int>{});

// 索引排序
size_t indices[] = {0, 1, 2, 3, 4};
float values[] = {5.0f, 3.0f, 1.0f, 4.0f, 2.0f};
tiered_sort_indices(indices, values, 5);
// indices: {2, 1, 4, 3, 0}
\`\`\`

### Group系统

| 接口 | 说明 |
|------|------|
| \`group<First, Rest...>()\` | Non-OwningGroup（缓存匹配索引） |
| \`group<First, Rest...>(owned<First>)\` | OwningGroup（重排主集，线性扫描） |
| \`group<First, Rest...>(reorder<First>)\` | ReorderGroup（重排主集，允许共享重排状态） |

### runtime_view

| 接口 | 说明 |
|------|------|
| \`runtime_view_create(class_pool<int>, class_pool<int> = {})\` | 运行时视图（class_pool 入参，位掩码匹配） |
| \`runtime_view_create(span<const int>, span<const int> = {})\` | 运行时视图（span 入参） |
| \`runtime_view_create(const vector<int>&, const vector<int>& = {})\` | 运行时视图（vector 入参） |
| \`runtime_view_create(const array<int, N>&)\` | 运行时视图（array 入参，仅 required） |
| \`runtime_view_create(const array<int, N>&, const array<int, M>&)\` | 运行时视图（array 入参，required + excluded） |
| \`runtime_view_create(const int*, size_t, const int* = nullptr, size_t = 0)\` | 运行时视图（裸指针 + 长度） |
| \`runtime_view_create_from_terms(class_pool<runtime_term>)\` | term 查询（支持 OR/OPTIONAL/NOT） |
| \`runtime_view_create_from_terms(span<const runtime_term>)\` | term 查询（span 入参） |
| \`runtime_view_create_from_terms(const vector<runtime_term>&)\` | term 查询（vector 入参） |
| \`runtime_view_create_from_terms(const array<runtime_term, N>&)\` | term 查询（array 入参） |
| \`runtime_view_create_from_terms(const runtime_term*, size_t)\` | term 查询（裸指针 + 长度） |
| \`get_component_mask(entity)\` | 获取实体组件位掩码 |
| \`get_component_bit<T>()\` | 获取类型的位掩码位 |

> 批量入参重载说明：\`add_batch\` / \`get_ptr_batch\` / \`prefetch_ptr_batch\` / \`runtime_view_create\` / \`runtime_view_create_from_terms\` 均支持 \`std::vector\` / \`std::array\` / 裸指针 + 长度 / \`std::span\` 入参，不持有外部容器。

### 生命周期信号

| 接口 | 说明 |
|------|------|
| \`set_on_entity_created(fn, data)\` | 绑定实体创建即时回调 |
| \`set_on_entity_destroyed(fn, data)\` | 绑定实体销毁即时回调 |
| \`set_on_add<T>(fn, data)\` | 绑定组件 T 添加即时回调 |
| \`set_on_remove<T>(fn, data)\` | 绑定组件 T 移除即时回调 |
| \`set_on_modify<T>(fn, data)\` | 绑定组件 T 覆盖写即时回调 |
| \`flush_entity_signals(handler)\` | 批量处理实体延迟信号 |
| \`has_pending_entity_signals()\` | 是否有待处理实体信号 |
| \`flush_component_signals(handler)\` | 批量处理组件延迟信号 |
| \`has_pending_component_signals()\` | 是否有待处理组件信号 |

### 使用

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(1000);

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

// 运行期 def 组件: 注册类型 (一次性, 冷路径) 后按名/ID 存取
type_def def;
def.size = 12; def.alignment = 4; def.trivially_copyable = true;
int did = type_id::register_type_def("VelocityDef", def);
float vel[3] = {1.0f, 2.0f, 3.0f};
mgr.add_def(e1, did, vel);                  // 按 ID 添加
mgr.add_def(e2, "VelocityDef", vel);         // 按名字添加
float* v = static_cast<float*>(mgr.get_def_ptr(e1, did));   // v[0]==1.0f
mgr.hard_remove_def(e1, did);                // 硬删除
mgr.soft_remove_def(e2, "VelocityDef");      // 按名软删除

// 批量查询组件
class_pool<Position*> results;
results.increase_capacity(entities.size());
mgr.get_ptr_batch<Position>(entities.data(), results.data(), entities.size());

// 预取组件指针
mgr.prefetch_ptr<Position>(e1);
mgr.prefetch_ptr_batch<Position>(entities.data(), entities.size());

// 先预取 sparse 条目，再预取组件数据
mgr.prefetch_ptr<Position>(e1);
mgr.prefetch_ptr_data<Position>(e1);

// cached: 复用 set 指针，适合批量循环查询
auto* set = mgr.get_single_class_set<Position>();
mgr.prefetch_ptr_cached<Position>(set, e1);
mgr.prefetch_ptr_data_cached<Position>(set, e1);
auto* p = mgr.get_ptr_fast_cached<Position>(set, e1);

// query_context: 一次获取，后续查询复用
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
\`\`\`

### 实体状态池

| 接口 | 说明 |
|------|------|
| \`entity_flag\` | 实体状态标志枚举：\`active\`, \`disabled\`, \`pending_destroy\`, \`static_entity\` |
| \`get_entity_state(entity, entity_state&)\` | 按实体句柄读取状态，失败返回 \`operating_message\` |
| \`get_entity_state_by_index(entity_index)\` | 按索引获取状态引用（\`entity_state&\`，不做有效性校验） |
| \`set_entity_flag(entity, flag)\` | 按句柄设置状态标志，返回 \`operating_message\` |
| \`clear_entity_flag(entity, flag)\` | 按句柄清除状态标志，返回 \`operating_message\` |
| \`has_entity_flag(entity, flag)\` | 按句柄检查状态标志（实体无效时为 \`false\`） |
| \`set_tag(entity, uint32_t)\` / \`get_tag(entity, uint32_t&)\` | 按句柄读写自定义标签，返回 \`operating_message\` |
| \`set_layer(entity, uint32_t)\` / \`get_layer(entity, uint32_t&)\` | 按句柄读写渲染层，返回 \`operating_message\` |
| \`set_group(entity, uint32_t)\` / \`get_group(entity, uint32_t&)\` | 按句柄读写分组，返回 \`operating_message\` |
| \`set_entity_flag_by_index(entity_index, flag)\` | 按索引设置标志（不做有效性校验） |
| \`clear_entity_flag_by_index(entity_index, flag)\` | 按索引清除标志（不做有效性校验） |
| \`has_entity_flag_by_index(entity_index, flag)\` | 按索引检查标志（不做有效性校验） |

\`\`\`cpp
if (!mgr.set_entity_flag(e, ecs::entity_flag::disabled))
{
    // 句柄无效
}

if (mgr.has_entity_flag(e, ecs::entity_flag::active))
{
    // 实体处于活跃状态
}

ecs::entity_state state{};
if (mgr.get_entity_state(e, state))
{
    // 读取成功
}

mgr.set_tag(e, 1);    // 自定义标签
mgr.set_layer(e, 3);  // 渲染层
\`\`\`

> 句柄重载先校验实体是否存活，失败返回 \`om_err_invalid_entity\`；\`*_by_index\` 版本直接按索引读写，供已确认索引有效的批处理路径使用。

### 变更日志池

每次 \`add\`/\`remove\` 操作自动记录变更，帧末可消费。

| 接口 | 说明 |
|------|------|
| \`enable_change_log()\` | 启用变更日志记录 |
| \`disable_change_log()\` | 禁用变更日志记录 |
| \`flush_change_log(handler)\` | 消费所有待处理的变更记录 |
| \`end_frame()\` | 帧结束，递增帧计数器 |
| \`has_pending_change_records()\` | 是否有待处理的变更记录 |

\`\`\`cpp
mgr.enable_change_log();
// ... 增删组件操作 ...
mgr.end_frame();
mgr.flush_change_log([](const ecs::change_record& r) {
    // r.entity_index, r.type_id, r.op (0=add,1=remove,2=modify)
    // r.frame, r.dense_index
});
\`\`\`

### 系统上下文池

注册系统执行上下文，管理执行顺序。

| 接口 | 说明 |
|------|------|
| \`system_context\` | 系统上下文结构体：\`required_mask\`, \`excluded_mask\`, \`order\`, \`phase\`, \`dependencies\` |
| \`register_system(ctx)\` | 注册系统上下文 |
| \`get_system_contexts()\` | 获取所有系统上下文（\`const class_pool<system_context>&\`） |

\`\`\`cpp
mgr.register_system(ecs::system_context{
    .required_mask = 0x3,  // 需要 Position + Velocity
    .phase = 1,            // update 阶段
    .order = 100,          // 执行顺序
});
\`\`\`

### 实体掩码（无上限）

基于 \`multi_block_bitmask\` 的动态位掩码存储，无组件类型上限。掩码位置（块索引/块内偏移）由 \`type_id::mask_block_of(tid)\` / \`type_id::mask_offset_of(tid)\` 计算。通过 \`reserve_blocks(n)\` 预分配掩码块数。

#### \`component_meta\` 结构体

每个已注册组件类型对应一份元数据。

| 字段 | 类型 | 说明 |
|------|------|------|
| \`size\` | \`size_t\` | 组件类型大小（字节） |

#### manager 接口

| 接口 | 说明 |
|------|------|
| \`get_component_mask(entity)\` | 获取实体块 0 掩码（\`uint64_t\`，type_id 1-64；等价于 \`get_component_mask_block(e, 0)\`） |
| \`get_component_mask_block(entity, uint32_t block_idx)\` | 获取实体指定块的掩码（\`uint64_t\`，block_idx 块对应 type_id \`block_idx*64+1\` 到 \`block_idx*64+64\`） |
| \`get_component_mask_block_by_index(uint32_t entity_index, uint32_t block_idx)\` | 同上，接受 entity_index 而非 entity 句柄 |
| \`get_component_bit<T>()\` | 获取组件 T 的掩码位（块 0 类型返回 \`1ULL<<offset\`，其余返回 0） |
| \`get_component_meta(int type_id)\` | 获取 \`component_meta*\`（含组件大小，type_id 越界返回 nullptr） |
| \`preallocate_mask_blocks(uint32_t num_blocks)\` | 预分配每实体掩码块数（每块 64 种组件；注册组件前调用；\`register_component_meta\` 在 type_id 超出时自动扩容） |
| \`num_mask_blocks() const\` | 当前每实体掩码块数 |
| \`get_entity_manager()\` | 获取 \`entity_manager&\`，可继续调用 \`set_mask_bit\` / \`clear_mask_bit\` / \`get_mask\` / \`get_block\` / \`set_entity_flag\` 等 |

\`\`\`cpp
uint64_t mask = mgr.get_component_mask(e);
uint64_t pos_bit = mgr.get_component_bit<Position>();
if ((mask & pos_bit) != 0)
{
    // 实体拥有 Position 组件
}

// 任意 type_id 的掩码位 (块 0 类型)
int vel_id = type_id::get_type_id<Velocity>();
if (type_id::mask_block_of(vel_id) == 0)
{
    uint64_t vel_bit = 1ULL << type_id::mask_offset_of(vel_id);
    bool has_vel = (mgr.get_component_mask(e) & vel_bit) != 0;
}

// 启动时预分配 2 块掩码（支持 128 种组件）
mgr.preallocate_mask_blocks(2);
\`\`\`

> 默认块数为 1（支持 64 种组件）。组件注册时 \`register_component_meta\` 自动扩容掩码块数，无需手动调用 \`preallocate_mask_blocks\`。多块掩码查询通过 \`get_component_mask_block(e, block_idx)\` 或 \`get_component_mask_block_by_index(idx, block_idx)\` 访问任意块。详见 [§ 29. multi_block_bitmask](#29-multi_block_bitmask--多块位掩码存储)。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 拷贝 \`manager\` | 禁止拷贝，内部资源所有权混乱 | 使用移动语义或引用传递 |
| 在遍历 View 的同时增删组件 | 迭代器失效或数据竞争 | 先收集变更，遍历结束后批量操作 |
| 删除实体后继续使用其句柄 | 句柄版本号失效，\`is_entity_valid\` 返回 false | 删除后丢弃句柄，或重新创建 |
| 忘记 \`preallocate_entities\` | 每个实体创建都可能触发扩容 | 启动时预估实体数量并预分配 |
| 在 \`soft_remove\` 后假设 \`size()\` 减少 | 软删除不减少 \`size()\` | 用 \`live_count()\` 获取存活数，或通过 View 遍历（遍历自动过滤软删除） |
| 使用 \`get_ptr_fast\` 跨越不同类型集合 | 跳过 type_id 检查，可能返回错误类型指针 | 同一类型集合内使用 \`get_ptr_fast\`，跨类型用 \`get_ptr\` |
| 把 \`get_ptr<T>\` 的返回值当作“类型已注册 + 实体有效 + 组件存在”的完整判断 | 实体句柄失效、实体未挂载该组件、类型未注册都返回 \`nullptr\`，无编译期断言也无运行期提示（类型名拼写错误同样静默返回 \`nullptr\`） | 判活用 \`is_entity_valid(entity)\`；类型是否已注册可用 \`get_single_class_set<T>() != nullptr\` 确认；组件查询结果一律判空 |

---
`
};

window.DOCS_DATA['views'] = {
  id: 'views',
  title: "View系统",
  category: 'ecs',
  icon: 'W',
  order: 5,
  content: `## 5. View系统

提供高效的组件遍历，自动选择最小集作为主集迭代。软删除（\`soft_remove\`）的组件对所有视图遍历均不可见（single_view / multi_view / 复合视图 / runtime_view 统一过滤），遍历计数即为存活数。

### 5.1 single_view\\<T> — 单组件视图

| 接口 | 说明 |
|------|------|
| \`size()\` | 组件占位数（软删除条目计入） |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 是否包含指定实体的组件 |
| \`for_each(func)\` | 遍历组件（自动检测 entity 参数：\`func(T&)\` 或 \`func(entity, T&)\`） |
| \`for_each_safe(func)\` | 遍历组件，回调内可安全调用 \`hard_remove\` 删除当前实体（自动检测 entity 参数） |
| \`begin()\` / \`end()\` | 实体迭代器（跳过软删除条目） |
| \`component_begin()\` / \`component_end()\` | 组件迭代器（\`T*\`） |
| \`get_component_for_entity(entity)\` | 获取指定实体的组件引用（无则 nullptr） |
| \`get_first_entity()\` | 返回第一个实体句柄 |
| \`get_last_entity()\` | 返回最后一个实体句柄 |
| \`get_entity_at_index(index)\` | 返回第 index 个实体句柄 |
| \`get_component_at_index(index)\` | 返回第 index 个组件的指针 |

\`\`\`cpp
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

// 迭代期安全删除: 回调内可 hard_remove 当前实体
mgr.view<Health>().for_each_safe([](ecs::entity e, Health& h) {
    if (h.hp <= 0) mgr.hard_remove<Health>(e);
});
\`\`\`

### 5.2 multi_view\\<T1, T2, ...> — 多组件视图

自动选择最小集作为主集迭代。

| 接口 | 说明 |
|------|------|
| \`size()\` | 实际匹配数（同时拥有所有组件的实体数） |
| \`pool_size()\` | 主集组件数量（主集池大小，不含匹配过滤） |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 是否同时拥有所有组件 |
| \`for_each(func)\` | 遍历多组件（自动检测 entity 参数） |
| \`get_component_for_entity<T>(entity)\` | 获取指定实体的指定类型组件（无则 nullptr） |
| \`get_first_entity()\` | 返回第一个匹配所有组件的实体 |
| \`get_last_entity()\` | 返回最后一个匹配所有组件的实体 |
| \`get_entity_at_index(index)\` | 返回主集第 index 个实体句柄 |
| \`include_optional_component<U>()\` | 链式追加可选组件，回调中为指针（无组件时为 nullptr） |

\`size()\` 与 \`pool_size()\` 的区别：当部分实体只拥有部分组件时，\`pool_size()\` 返回主集的实体总数，\`size()\` 返回其中真正同时拥有全部组件的实体数；若所有实体都拥有全部组件，两者相等。

\`\`\`cpp
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
\`\`\`

### 5.3 single_view_without — 排除视图

遍历有 T 但没有 ExcludeTypes 的实体。

| 接口 | 说明 |
|------|------|
| \`size()\` | 组件占位数（软删除条目计入） |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 检查实体是否有 T 且无 ExcludeTypes |
| \`get_component_for_entity(entity)\` | 获取 T 的引用（无则 nullptr） |
| \`get_first_entity()\` | 返回第一个匹配的实体 |
| \`for_each(func)\` | 遍历组件（排除指定类型，自动检测 entity 参数） |

\`\`\`cpp
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
\`\`\`

### 5.4 single_view_with — 获取视图

遍历 T，同时获取 GetTypes 的指针（可能为 nullptr）。

| 接口 | 说明 |
|------|------|
| \`size()\` | 组件占位数（软删除条目计入） |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 检查实体是否有 T |
| \`get_component_for_entity(entity)\` | 获取 T 的引用（无则 nullptr） |
| \`get_optional_component_for_entity<U>(entity)\` | 获取可选组件 U 的指针 |
| \`get_first_entity()\` | 返回第一个匹配的实体 |
| \`for_each(func)\` | 遍历组件+可选指针（自动检测 entity 参数） |

\`\`\`cpp
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
\`\`\`

### 5.5 or_view\\<A, B> — OR视图

遍历拥有 A **或** B 的实体，使用 nullable 指针区分。两阶段遍历，零额外内存分配。

| 接口 | 说明 |
|------|------|
| \`size()\` | 近似大小（A.size + B.size，上界） |
| \`empty()\` | 是否两个集都为空 |
| \`contains(entity)\` | 是否有 A 或 B |
| \`get_first_entity()\` | 返回第一个匹配实体 |
| \`for_each(func)\` | 遍历 A OR B，回调为 \`func(entity, A*, B*)\` 或 \`func(A*, B*)\` |

\`\`\`cpp
auto ov = mgr.view_or<Position, Velocity>();
ov.for_each([](entity e, Position* p, Velocity* v) {
    if (p && v) { /* 同时拥有 Position 和 Velocity */ }
    else if (p) { /* 仅拥有 Position */ }
    else if (v) { /* 仅拥有 Velocity */ }
});

if (ov.contains(some_entity)) { /* ... */ }
entity first = ov.get_first_entity();
\`\`\`

### 5.6 filter_view\\<T, Pred> — 谓词过滤视图

预过滤满足谓词的组件，通过 \`class_pool<size_t>\` 存储 dense 索引实现高效迭代。构造时自动过滤，也可手动 \`rebuild()\` 触发重新过滤。

| 接口 | 说明 |
|------|------|
| \`size()\` | 过滤后组件数量 |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 是否在过滤结果中（线性扫描） |
| \`get_component_for_entity(entity)\` | 获取组件引用（无则 nullptr） |
| \`get_first_entity()\` | 返回第一个过滤结果实体 |
| \`get_entity_at_index(index)\` | 返回第 index 个过滤结果实体 |
| \`get_component_at_index(index)\` | 返回第 index 个过滤结果组件指针 |
| \`rebuild()\` | 重新执行过滤 |
| \`for_each(func)\` | 遍历过滤后的组件，回调为 \`func(entity, T&)\` 或 \`func(T&)\` |
| \`and_<B>()\` | 链式调用：在过滤结果上追加 AND 组件 B |
| \`or_<B>()\` | 链式调用：在过滤结果上追加 OR 组件 B |

\`\`\`cpp
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
\`\`\`

### 5.7 filter_and_view — 过滤+AND组合视图

通过 \`filter_view::and_<B>()\` 链式创建。遍历满足谓词 **且** 同时拥有组件 B 的实体。

| 接口 | 说明 |
|------|------|
| \`size()\` | 过滤后数量 |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 是否在过滤结果中（线性扫描） |
| \`get_component_for_entity<T>(entity)\` | 获取 T 引用（无则 nullptr） |
| \`get_optional_component_for_entity<B>(entity)\` | 获取 B 指针 |
| \`get_first_entity()\` | 返回第一个匹配实体 |
| \`get_entity_at_index(index)\` | 返回第 index 个匹配实体 |
| \`rebuild()\` | 重新执行过滤 |
| \`for_each(func)\` | 遍历过滤+AND结果 |

\`\`\`cpp
// Position.x > 1 AND 同时拥有 Health
auto fav = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; })
               .and_<Health>();
fav.for_each([](entity e, Position& p, Health& h) {
    // 仅处理 x > 1 且拥有 Health 的实体
});

entity first = fav.get_first_entity();
entity nth   = fav.get_entity_at_index(3);
if (fav.contains(some_entity)) { /* ... */ }
\`\`\`

### 5.8 filter_or_view — 过滤+OR组合视图

通过 \`filter_view::or_<B>()\` 链式创建。遍历满足谓词 **或** 拥有组件 B 的实体，使用 nullable 指针区分。

| 接口 | 说明 |
|------|------|
| \`size()\` | 过滤后数量 |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 是否在过滤结果中 |
| \`get_first_entity()\` | 返回第一个匹配实体 |
| \`rebuild()\` | 重新执行过滤 |
| \`for_each(func)\` | 遍历过滤+OR结果 |

\`\`\`cpp
// Position.x > 1 OR 拥有 Velocity
auto fov = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; })
               .or_<Velocity>();
fov.for_each([](entity e, Position* p, Velocity* v) {
    if (p && v) { /* 满足谓词且拥有 Velocity */ }
    else if (p) { /* 仅满足谓词 */ }
    else if (v) { /* 仅拥有 Velocity（不满足谓词或没有 Position） */ }
});
\`\`\`

### 5.9 sort_entities_by_component / reorder_by_component — 排序工具

manager 级别的排序工具，将 dense 数组按组件值重排，后续迭代即按排序顺序。

| 接口 | 说明 |
|------|------|
| \`sort_entities_by_component<T>(cmp)\` | 按组件 T 的值排序 dense 数组（同步更新 sparse 映射） |
| \`reorder_by_component<T, Other>(cmp)\` | 按 Other 的值重排 T 的 dense 数组 |
| \`sort_component_container<T>(cmp)\` | 按组件 T 的值排序并同步更新 dense/sparse 映射（等价于 \`sort_entities_by_component\`） |

**reorder_by_component 语义**：遍历 T 池的所有实体，按 Other 的值排序。若某实体没有 Other 组件，使用默认构造的 \`Other{}\` 参与比较。

\`\`\`cpp
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
\`\`\`

**不要做什么**

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`reorder_by_component\` 期望实体没有 Other 时被排除 | 没有 Other 的实体会用 \`Other{}\` 参与排序，不会被排除 | 若需排除，先过滤实体再排序 |
| 排序后仍用旧 index 访问组件 | dense 数组已重排，旧 index 失效 | 排序后通过 \`get_ptr<T>(entity)\` 重新获取 |

### 5.10 page — 分页视图

通过 \`page(offset, limit)\` 链式调用，跳过前 \`offset\` 个结果并限制返回 \`limit\` 个。适用于 \`single_view\` 和 \`multi_view\`。

| 接口 | 说明 |
|------|------|
| \`size()\` | 分页后数量（\`min(limit, base_size - offset)\`，软删除条目计入） |
| \`empty()\` | 是否为空 |
| \`for_each(func)\` | 分页遍历（跳过前 offset 个，最多处理 limit 个） |

\`\`\`cpp
// 跳过前 1 个，最多处理 3 个
auto paged = mgr.view<Position, Velocity>().page(1, 3);
paged.for_each([](Position& p, Velocity& v) {
    // 仅处理第 2~4 个匹配实体
});
\`\`\`

### 5.11 sorted_by_component — 排序视图

通过 \`sorted_by_component<T>(cmp)\` 链式调用，按指定组件值临时排序查询结果。通过版本号检测变更自动重建缓存。适用于 \`single_view\` 和 \`multi_view\`。

| 接口 | 说明 |
|------|------|
| \`size()\` | 排序后数量（仅含拥有全部组件的有效实体） |
| \`empty()\` | 是否为空 |
| \`for_each(func)\` | 按排序顺序遍历（自动检测 entity 参数） |

\`\`\`cpp
// 按 Position.x 升序排序
auto sorted = mgr.view<Position, Velocity>()
    .sorted_by_component<Position>([](Position& a, Position& b) {
        return a.x < b.x;
    });
sorted.for_each([](Position& p, Velocity& v) {
    // 按 p.x 升序遍历
});
\`\`\`

### 5.12 sorted_by_component_value — 分组视图

通过 \`sorted_by_component_value(keyFn)\` 链式调用，按组件值分组，支持逐组遍历。适用于 \`single_view\` 和 \`multi_view\`。

| 接口 | 说明 |
|------|------|
| \`size()\` | 分组后总数 |
| \`empty()\` | 是否为空 |
| \`group_count()\` | 分组数量 |
| \`for_each(func)\` | 按分组顺序遍历所有元素 |
| \`for_each_group(func)\` | 逐组遍历，回调为 \`func(key, start_index, end_index)\` |

\`\`\`cpp
// 按 Position.x / 20 分组
auto grouped = mgr.view<Position>()
    .sorted_by_component_value([](Position& p) -> int {
        return p.x / 20;
    });

// 逐组遍历
grouped.for_each_group([](int key, size_t start, size_t end) {
    std::cout << "Group " << key << ": " << (end - start) << " entities\\n";
});
\`\`\`

### 5.13 track_changes — 变更检测视图

通过 \`track_changes()\` 链式调用，仅返回自上次迭代以来组件发生变化的实体。基于组件池版本号实现，适用于 \`single_view\` 和 \`multi_view\`。

| 接口 | 说明 |
|------|------|
| \`size()\` | 变更实体数量 |
| \`empty()\` | 是否为空 |
| \`for_each(func)\` | 遍历变更的实体（首次全量返回，后续仅返回变更实体） |
| \`reset_tracking()\` | 重置跟踪基准（下次 for_each 重新全量返回） |

\`\`\`cpp
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
\`\`\`

### 5.14 链式组合

\`single_view\` 和 \`multi_view\` 支持 \`page\` / \`sorted_by_component\` / \`sorted_by_component_value\` / \`track_changes\` / \`filter_changed\` / \`filter_added\` 等链式调用。注意这些链式方法返回的是独立的视图对象,**不互相嵌套**——每个链式视图只能独立使用其自身的 \`for_each\` / \`size\` / \`empty\` / \`reset_tracking\` 等接口,不能在链式视图后再调用 \`page\` 等其他链式方法。

\`\`\`cpp
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
\`\`\`

### 5.15 filter_changed — 逐实体变更检测

通过 \`filter_changed()\` 链式调用，仅返回自上次迭代以来组件值发生变化的实体。基于逐实体版本号追踪，可精确到单个实体。

> **注意：** 仅 \`add()\` 操作（包括覆盖添加）会触发变更版本号递增。通过 \`get_ptr()\` 直接修改组件内存不会触发变更检测。

| 接口 | 说明 |
|------|------|
| \`size()\` | 变更实体数量 |
| \`empty()\` | 是否为空 |
| \`for_each(func)\` | 遍历变更实体（首次全量返回，后续仅返回变更实体） |
| \`reset_tracking()\` | 重置快照基准（下次 for_each 重新全量返回） |

> \`multi_view\` 还提供 \`filter_any_changed()\`（无模板参数），等价于 \`filter_changed_view<0>\`，即跟踪任意一个组件的变更。\`single_view\` 无此重载。

\`\`\`cpp
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
\`\`\`

### 5.16 filter_added — 逐实体添加检测

通过 \`filter_added()\` 链式调用，仅返回视图创建后**新添加**的组件。基于全局添加计数器实现。

| 接口 | 说明 |
|------|------|
| \`size()\` | 新增实体数量 |
| \`empty()\` | 是否为空 |
| \`for_each(func)\` | 遍历新增实体（首次全量返回，后续仅返回新添加的实体） |
| \`reset_tracking()\` | 重置添加检测基准（下次 for_each 重新全量返回） |

\`\`\`cpp
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
\`\`\`

> **与 \`filter_changed\` 的区别：** \`filter_changed\` 追踪"修改"（覆盖添加也会触发），\`filter_added\` 仅追踪"首次添加"（覆盖添加不触发）。

### 5.17 view_any_of — N元OR视图

通过 \`view_any_of<Types...>()\` 创建，遍历拥有**任意一个**指定组件的实体。使用 bitset 去重，确保每个实体仅出现一次。

| 接口 | 说明 |
|------|------|
| \`size()\` | 近似大小（各集合大小之和，上界） |
| \`empty()\` | 是否所有集合都为空 |
| \`for_each(func)\` | 遍历任意匹配的实体，回调为 \`func(Types*...)\` 或 \`func(entity, Types*...)\` |

\`\`\`cpp
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
\`\`\`

> **与 \`or_view\` 的区别：** \`or_view\` 仅支持 2 组件，\`view_any_of\` 支持任意数量组件。

### 5.18 exactly_one — 精确获取单个实体

通过 \`exactly_one()\` 获取恰好一个实体的组件引用。匹配实体数量必须为 1，否则终止。

**返回值：**
- \`single_view<T>::exactly_one()\` → \`T&\`
- \`multi_view<T1, T2, ...>::exactly_one()\` → \`std::tuple<T1&, T2&, ...>\`

\`\`\`cpp
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
\`\`\`

> **注意：** \`single_view\` 与 \`multi_view\` 都要求视图内实体数量恰好为 1；不满足时终止。适用于单例实体、玩家实体等场景。

### 5.19 find_one — 查询指定实体

通过 \`find_one(entity)\` 查询指定实体是否拥有视图要求的全部组件。若拥有则返回组件指针元组，否则返回空指针。

**返回值：** \`std::tuple<First*, Rest*...>\`，所有组件都存在时所有指针非空，否则所有指针为空。

\`\`\`cpp
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
\`\`\`

### 5.20 iter_over_entities — 批量指定实体查询

通过 \`iter_over_entities(entities)\` 在指定实体列表上迭代，仅处理同时拥有视图所有组件的实体。

**参数：** \`entities\` 支持 \`std::array<entity, N>\`、\`std::span<entity>\`、\`class_pool<entity>\` 等可迭代容器。

\`\`\`cpp
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
\`\`\`

> **注意：** 实体列表中不满足组件条件的实体会被静默跳过，不会报错。

### View 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 在 \`for_each\` 回调中增删组件 | 迭代器失效，可能导致崩溃或漏处理 | 使用 \`for_each_safe\` 安全删除，或先收集变更实体列表遍历结束后批量操作 |
| 在 \`for_each_safe\` 回调中调用 \`swap_dense_and_pool\` / \`reorder_dense_by_indices\` / \`clear\` | 破坏 dense 布局，触发 \`std::abort\` | 迭代结束后再执行重排/清空操作 |
| \`filter_view\` 过滤条件变化后忘记 \`rebuild()\` | 过滤结果过期，仍返回旧数据 | 组件数据变化后调用 \`rebuild()\` |
| \`exactly_one()\` 在活实体数不为 1 时使用 | 终止（\`LCF_TRAP\`） | 先检查 \`live_count() == 1\`（\`size()\` 含软删除条目），或使用 \`find_one()\` |
| 依赖 \`filter_changed\` 检测 \`get_ptr()\` 修改 | 直接修改内存不触发变更检测 | 通过 \`add()\` 覆盖触发变更，或使用 \`track_changes\` |
| 在多组件 View 中混用 \`get_ptr_fast\` 和 \`get_ptr\` | 类型安全边界模糊 | 同一 View 中统一使用一种获取方式 |

---
`
};

window.DOCS_DATA['groups'] = {
  id: 'groups',
  title: "Group系统",
  category: 'ecs',
  icon: 'G',
  order: 6,
  content: `## 6. Group系统

Group 在构造时预先计算匹配实体集，迭代时零分支。

### 6.1 Non-OwningGroup (\`group\`)

通过 \`mgr.group<First, Rest...>()\` 创建，缓存匹配实体的 dense 索引。

**接口：**

| 接口 | 说明 |
|------|------|
| \`size()\` | 匹配实体数量 |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 是否包含指定实体 |
| \`for_each(func)\` | 遍历匹配实体（自动检测 entity 参数：\`func(Ts&...)\` 或 \`func(entity, Ts&...)\`） |
| \`get<T>(entity)\` | 获取指定实体的组件 T 指针 |
| \`front()\` | 首个匹配实体 |
| \`back()\` | 末尾匹配实体 |
| \`rebuild()\` | 重建缓存（组件增删后调用） |

\`\`\`cpp
// 双组件 Group
auto g = mgr.group<Position, Velocity>();
g.for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
    p.y += v.vy;
});

// 带 entity 参数
g.for_each([](entity e, Position& p, Velocity& v) {
    std::cout << "Entity " << e.parts_.index_ << ": pos=(" << p.x << "," << p.y << ")\\n";
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
\`\`\`

### 6.2 OwningGroup (\`group\` + \`owned\`)

通过 \`mgr.group<First, Rest...>(ecs::owned<First>)\` 创建，重排主集 \`First\` 的 dense 数组，使匹配实体在数组前部连续排列。

**注意：** \`owned\` 标签标记的组件类型会被重排，组件数据顺序会改变。如果其他代码依赖该组件的 dense 顺序，需谨慎使用。

**接口：**

| 接口 | 说明 |
|------|------|
| \`size()\` | 匹配实体数量 |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 是否包含指定实体 |
| \`for_each(func)\` | 遍历匹配实体（自动检测 entity 参数） |
| \`get<T>(entity)\` | 获取指定实体的组件 T 指针 |
| \`front()\` | 首个匹配实体 |
| \`back()\` | 末尾匹配实体 |
| \`rebuild()\` | 重建缓存（组件增删后调用） |

\`\`\`cpp
// OwningGroup: Position 被重排
auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);
og.for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
});

// 带 entity 参数
og.for_each([](entity e, Position& p, Velocity& v) {
    std::cout << "Entity " << e.parts_.index_ << ": pos=(" << p.x << "," << p.y << ")\\n";
});

// 三组件 OwningGroup
auto og3 = mgr.group<Position, Velocity, Health>(ecs::owned<Position>);
og3.for_each([](entity e, Position& p, Velocity& v, Health& h) {
    // 同时拥有三个组件的实体
});
\`\`\`

### 6.3 ReorderGroup (\`group\` + \`reorder\`)

通过 \`mgr.group<First, Rest...>(ecs::reorder<First>)\` 创建，与 OwningGroup 同样重排主集，但语义更轻——仅表达"允许重排"，不暗示生命周期所有权。

**接口：**

| 接口 | 说明 |
|------|------|
| \`size()\` | 匹配实体数量 |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 是否包含指定实体 |
| \`for_each(func)\` | 遍历匹配实体（自动检测 entity 参数） |
| \`get<T>(entity)\` | 获取指定实体的组件 T 指针 |
| \`front()\` | 首个匹配实体 |
| \`back()\` | 末尾匹配实体 |
| \`rebuild()\` | 重建缓存（组件增删后调用） |
| \`share_with(other_reorder_group)\` | 与另一个相同类型的 ReorderGroup 共享重排状态 |

\`\`\`cpp
// ReorderGroup: Position 被重排
auto rg = mgr.group<Position, Velocity>(ecs::reorder<Position>);
rg.for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
});

// 带 entity 参数
rg.for_each([](entity e, Position& p, Velocity& v) {
    std::cout << "Entity " << e.parts_.index_ << ": pos=(" << p.x << "," << p.y << ")\\n";
});

// 三组件 ReorderGroup
auto rg3 = mgr.group<Position, Velocity, Health>(ecs::reorder<Position>);
rg3.for_each([](entity e, Position& p, Velocity& v, Health& h) {
    // 同时拥有三个组件的实体
});
\`\`\`

**多 Group 共享重排：** 多个相同组件类型的 ReorderGroup 可通过 \`share_with()\` 共享重排状态，避免重复重排。

\`\`\`cpp
auto rg1 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
auto rg2 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
rg2.share_with(rg1);  // rg2 共享 rg1 的重排状态
// 两者 size() 和迭代结果一致，共享同一份缓存
\`\`\`

### Group 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 组件增删后忘记 \`rebuild()\` | Group 缓存过期，可能漏掉新实体或包含已删除实体 | 每次批量增删后调用 \`rebuild()\` |
| 在 OwningGroup / ReorderGroup 中依赖 dense 顺序 | \`owned\` / \`reorder\` 会重排主集 dense 数组 | 若需保持顺序，使用 Non-OwningGroup |
| 对频繁增删的组件使用 Group | 每次增删都需 \`rebuild()\` | 稳定组件用 Group，频繁变化组件用 View |

---
`
};

window.DOCS_DATA['runtime_view'] = {
  id: 'runtime_view',
  title: "runtime_view — 运行时视图",
  category: 'ecs',
  icon: 'R',
  order: 7,
  content: `## 7. runtime_view — 运行时视图

在运行时动态指定组件类型组合进行查询。组件类型数量无上限，前 64 种组件类型自动维护实体位掩码，超过 64 种的组件类型同样参与所有视图/分组查询。

### 7.1 实体掩码

每个实体在添加/删除组件时自动维护多块组件位掩码；\`get_component_mask\` 返回首块，其他块通过 \`get_component_mask_block\` 读取：

\`\`\`cpp
ecs::manager mgr;
auto e = mgr.create_entity();
mgr.add(e, Position{1, 0});
mgr.add(e, Velocity{2, 0});

uint64_t mask = mgr.get_component_mask(e);
// mask 包含 Position 和 Velocity 的位

uint64_t pos_bit = mgr.get_component_bit<Position>();
bool has_pos = (mask & pos_bit) != 0;  // true
\`\`\`

### 7.2 运行时视图

\`\`\`cpp
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
\`\`\`

### 7.3 排除视图

\`\`\`cpp
// 有 Position 但无 Velocity 的实体
auto rv = mgr.runtime_view_create(
    { type_id::get_type_id<Position>() },
    { type_id::get_type_id<Velocity>() }
);

rv.for_each([](entity e) {
    // 处理只有 Position 的实体
});
\`\`\`

### 7.4 接口

| 接口 | 说明 |
|------|------|
| \`runtime_view_create({ids...})\` | 创建运行时视图，传入必须拥有的组件 type_id 列表 |
| \`runtime_view_create({ids}, {exclude_ids})\` | 创建排除式运行时视图 |
| \`runtime_view_create_from_terms(terms)\` | 通过 \`runtime_term\` 创建视图，支持 OR / OPTIONAL / NOT |
| \`for_each(func)\` | 遍历所有匹配实体，回调接收 \`entity\` 或无参 |
| \`for_each_typed<Ts...>(func)\` | 遍历并回传组件引用，回调接收 \`entity, Ts&...\` 或 \`Ts&...\` |
| \`for_each_paged(offset, limit, func)\` | 分页遍历 |
| \`for_each_changed(func)\` | 遍历自上次调用后发生变更的实体 |
| \`size()\` | 命中实体数 |
| \`count()\` | 精确命中数 |
| \`empty()\` | 是否为空 |
| \`contains(entity)\` | 检查实体是否匹配查询 |
| \`get_ptr<T>(entity)\` | 获取实体的组件指针（组件不存在返回 \`nullptr\`；不检查实体是否属于该视图） |
| \`get_first_entity()\` | 返回第一个匹配实体 |
| \`sort_by_component<T>(cmp)\` | 按组件值排序，结果存于 \`sorted_entities_\` |
| \`get_sorted_entities()\` | 获取排序后的实体列表（\`const dense<entity>&\`，需先调用 \`sort_by_component\`） |
| \`changed()\` | 检测组件池版本是否变化 |
| \`reset_change_tracking()\` | 重置变更检测基线 |
| \`begin()\` / \`end()\` | 迭代器，支持 range-for |
| \`rebuild()\` | 重新选择最小集合（组件数量变化后调用） |

> \`get_component_mask(entity)\` 和 \`get_component_bit<T>()\` 是 \`manager\` 的方法,见 [§4](#4-ecsmanager-ecs管理器) 与 [§7.1](#71-实体掩码)。

### 7.5 组件类型无上限

组件类型数量不受 64 限制。\`get_component_bit<T>()\` 只用于首块掩码，type_id > 64 时返回 0；这类组件仍可正常添加、查询，并参与所有视图/分组（\`view\` / \`group\` / \`owning_group\` / \`reorder_group\` / \`runtime_view\` / \`view(without)\`）的匹配。需要读取其他掩码块时使用 \`get_component_mask_block(entity, block)\`。

\`\`\`cpp
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
\`\`\`

### 7.6 for_each_typed — 组件引用回传

\`for_each\` 只回调 \`entity\`，需手动调用 \`get_ptr\` 获取组件。\`for_each_typed<Ts...>\` 直接回传组件引用，\`Ts\` 顺序对应 \`runtime_view_create\` 传入的 type_id 顺序。

\`\`\`cpp
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
\`\`\`

### 7.7 for_each_paged — 分页遍历

\`\`\`cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

// 每页 100 个，处理第 2 页
rv.for_each_paged(100, 100, [](entity e) {
    // 处理实体
});
\`\`\`

### 7.8 变更检测

通过组件池版本号检测变更。首次调用 \`reset_change_tracking\` 记录基线，\`changed\` 比较当前版本与基线。

\`\`\`cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

rv.reset_change_tracking();      // 记录基线
// ... 修改组件 ...
if (rv.changed()) {              // 版本变化则 true
    rv.for_each_changed([](entity e) {
        // 遍历所有匹配实体（非增量，全量遍历）
    });
    // for_each_changed 会重置变更追踪
}
\`\`\`

### 7.9 sort_by_component — 按组件排序

\`\`\`cpp
auto rv = mgr.runtime_view_create({type_id::get_type_id<Position>()});

rv.sort_by_component<Position>([](const Position& a, const Position& b) {
    return a.x < b.x;  // 升序
});

// 排序结果缓存在视图中
for (const auto& e : rv.get_sorted_entities()) {
    auto* p = mgr.get_ptr<Position>(e);
    // 按 x 升序处理
}
\`\`\`

### 7.10 count — 精确命中数

\`\`\`cpp
auto rv = mgr.runtime_view_create({
    type_id::get_type_id<Position>(),
    type_id::get_type_id<Velocity>()
});

size_t n = rv.count();  // 精确匹配数，遍历计算
\`\`\`

### 7.11 iterator — 迭代器

\`\`\`cpp
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
\`\`\`

### 7.12 runtime_term — OR / OPTIONAL / NOT 查询

通过 \`runtime_term\` 构造查询条件，支持 OR（并集）、NOT（排除）、OPTIONAL（可选）操作。

\`\`\`cpp
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
\`\`\`

| op 值 | 语义 | 说明 |
|-------|------|------|
| 0 | AND | 必须拥有 |
| 1 | OR | 至少命中一个 OR term |
| 2 | NOT | 必须不拥有 |
| 3 | OPTIONAL | 可选，不影响命中 |

### 7.13 access_mode — 读写标注

\`runtime_term.access\` 标注组件访问模式，用于意图声明。

\`\`\`cpp
class_pool<ecs::runtime_term> terms;
terms.emplace_back(ecs::runtime_term{
    type_id::get_type_id<Position>(), 0, ecs::access_mode::read_only});
terms.emplace_back(ecs::runtime_term{
    type_id::get_type_id<Velocity>(), 0, ecs::access_mode::read_write});

auto rv = mgr.runtime_view_create_from_terms(std::move(terms));
\`\`\`

| 值 | 语义 |
|----|------|
| \`access_mode::read_only\` | 只读访问 |
| \`access_mode::read_write\` | 读写访问 |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`size()\` 依赖精确值 | 返回的是主集大小上限，非精确匹配数 | 使用 \`count()\` 获取精确值或 \`for_each\` 遍历 |
| 用 \`get_component_bit<T>() != 0\` 判断组件是否存在 | type_id > 64 的组件 bit 恒为 0 | 用 \`get_ptr<T>(e) != nullptr\` 或视图查询判断 |
| 组件数量变化后忘记 \`rebuild()\` | 主集选择可能不是最优 | 增删组件类型后调用 \`rebuild()\` |
| \`for_each_changed\` 依赖增量语义 | 全量遍历匹配实体，非仅变更实体 | 变更检测仅判断"有无变更" |
| \`sort_by_component\` 后不重新排序就修改组件 | 排序缓存与实际数据不一致 | 组件变更后重新调用 \`sort_by_component\` |

---
`
};

window.DOCS_DATA['function_storage'] = {
  id: 'function_storage',
  title: "函数存储（回调作为组件）",
  category: 'ecs',
  icon: 'F',
  order: 8,
  content: `## 8. 函数存储（回调作为组件）

\`t_fun\` 可直接作为 ECS 组件，无需任何包装结构体。不同函数签名自动推导为不同组件类型，通过 \`ecs::manager\` 的标准组件接口存储与调用。详见 [§ 34. t_fun](#34-t_fun--函数类型延迟调用器)。

### 使用

\`\`\`cpp

// 回调目标函数
int  on_add(int a, int b) { return a + b; }
void on_callback(int x) { std::printf("cb: %d\\n", x); }

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
\`\`\`

### void 返回值与无参函数

\`\`\`cpp
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
\`\`\`

### 通过 View 批量调用

\`\`\`cpp
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
\`\`\`

### 组件内修改持久化

\`\`\`cpp
entity e = mgr.create_entity();
mgr.add(e, t_fun{on_add, 1, 2});

// 第一次查询修改
auto* p1 = mgr.get_ptr<t_fun<int(int,int)>>(e);
p1->set_arg<0>(100);
p1->set_arg<1>(200);

// 第二次查询验证持久化
auto* p2 = mgr.get_ptr<t_fun<int(int,int)>>(e);
(*p2)();                   // 300, 修改已持久化
\`\`\`

---
`
};

window.DOCS_DATA['signals'] = {
  id: 'signals',
  title: "生命周期信号",
  category: 'ecs',
  icon: 'L',
  order: 9,
  content: `## 9. 生命周期信号

两层架构：**即时信号**（函数指针回调） + **延迟信号**（环形缓冲区 + 溢出 chain，批量处理）。

注册即时回调的事件同步触发；未注册的由 \`flush_*_signals\` 处理。

### 9.1 实体级即时信号

实体创建/销毁时立即触发回调。通过 \`void* user_data\` 传递上下文。

**接口：**

| 接口 | 说明 |
|------|------|
| \`set_on_entity_created(fn, user_data)\` | 绑定实体创建回调：\`void fn(entity, void* user_data)\` |
| \`set_on_entity_destroyed(fn, user_data)\` | 绑定实体销毁回调：\`void fn(entity, void* user_data)\` |

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100);

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
\`\`\`

> **注意：** 回调必须是 \`void (*)(entity, void*) noexcept\` 签名。使用 \`+\` 将无捕获 lambda 转为函数指针。

### 9.2 组件级即时信号

组件添加/移除时立即触发回调。回调接收实体、组件指针和 \`user_data\`，可在回调中直接修改组件数据。

**接口：**

| 接口 | 说明 |
|------|------|
| \`set_on_add<T>(fn, user_data)\` | 绑定组件 T 添加回调：\`void fn(entity, void* component, void* user_data)\` |
| \`set_on_remove<T>(fn, user_data)\` | 绑定组件 T 移除回调：\`void fn(entity, void* component, void* user_data)\` |

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100);

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
\`\`\`

> **注意：** 组件指针在回调期间有效，可用于读取或修改组件数据。\`hard_remove\` 触发 \`on_remove\`；\`soft_remove\` 仅逻辑隐藏组件（当时不析构），**不触发** \`on_remove\` 也不入延迟队列；旧对象在死槽被 \`add\` 复用覆盖或 \`compact\` 回收时补析构。

### 9.3 覆盖写与 on_modify

对同一实体的同一组件再次 \`add\` 称为覆盖写。覆盖写语义由 \`on_modify\` 是否注册决定：

- **注册了 \`on_modify<T>\`**：覆盖写只触发 \`on_modify\`，不触发 \`on_remove\`/\`on_add\`。
- **未注册 \`on_modify<T>\`**：覆盖写回退为 \`on_remove\`(旧组件) + \`on_add\`(新组件)。

| 接口 | 说明 |
|------|------|
| \`set_on_modify<T>(fn, user_data)\` | 绑定组件 T 覆盖写回调：\`void fn(entity, void* component, void* user_data)\` |

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(10);
int add_cnt = 0, remove_cnt = 0, modify_cnt = 0;
mgr.set_on_add<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &add_cnt);
mgr.set_on_remove<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &remove_cnt);
mgr.set_on_modify<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &modify_cnt);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 0});   // add_cnt == 1
mgr.add(e, Position{2, 0});   // modify_cnt == 1, add_cnt/remove_cnt 不变
\`\`\`

> **不应：** 靠 \`on_add\` 区分新增与覆盖。注册 \`on_modify\` 后覆盖路径不再走 \`on_add\`。

### 9.4 实体级延迟信号

实体创建/销毁事件被推入环形缓冲区，调用 \`flush_entity_signals\` 时批量处理。适合批量同步、避免重入的场景。

**接口：**

| 接口 | 说明 |
|------|------|
| \`flush_entity_signals(handler)\` | 批量处理所有待处理实体信号：\`handler(uint32_t type, uint32_t entity_idx)\` |
| \`has_pending_entity_signals()\` | 是否有待处理实体信号 |

**信号类型：**
- \`type=0\`：实体创建
- \`type=1\`：实体销毁

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100);

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
\`\`\`

> **缓冲区容量：** 1024 条（2 的幂）。缓冲区满时事件落入 \`overflow_chain\`，\`overflow_count\` 累计，\`flush\` 一并消费，不静默丢弃。

### 9.5 组件级延迟信号

组件添加/移除事件被推入环形缓冲区，调用 \`flush_component_signals\` 时批量处理。

**接口：**

| 接口 | 说明 |
|------|------|
| \`flush_component_signals(handler)\` | 批量处理所有待处理组件信号：\`handler(uint32_t type, uint32_t entity_idx, uint32_t component_id)\` |
| \`has_pending_component_signals()\` | 是否有待处理组件信号 |

**信号类型：**
- \`type=0\`：组件添加
- \`type=1\`：组件移除

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(100);

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
\`\`\`

### 9.6 即时/延迟回调

注册即时回调后，对应事件同步触发：

- \`set_on_add<T>\` 注册后：\`add\` 同步触发 \`on_add\`。
- \`set_on_remove<T>\` 注册后：\`hard_remove\` 同步触发 \`on_remove\`。
- \`set_on_entity_created\` 注册后：\`create_entity\` 同步触发。
- \`set_on_entity_destroyed\` 注册后：\`delete_entity\` 同步触发。

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(10);
int add_cb = 0;
mgr.set_on_add<Position>(+[](entity, void*, void* d) noexcept { (*static_cast<int*>(d))++; }, &add_cb);

entity e = mgr.create_entity();
mgr.add(e, Position{1, 0});   // add_cb == 1,不入队

int add_sig = 0;
mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
    if (type == 0) ++add_sig;
});
// add_sig == 0(已注册即时回调,不重复入队)
\`\`\`

> **不应：** 同一事件既订阅即时回调又期望 \`flush\` 收到。互斥设计下二者只走一路。

### 9.7 信号开关与溢出

**信号开关**控制延迟队列是否入队（不影响即时回调）：

| 接口 | 说明 |
|------|------|
| \`disable_comp_signals()\` / \`enable_comp_signals()\` | 关/开组件延迟信号入队 |
| \`disable_entity_signals()\` / \`enable_entity_signals()\` | 关/开实体延迟信号入队 |

**溢出处理：** 环形缓冲区满时事件落入 \`overflow_chain\`，\`flush_*_signals\` 先消费缓冲区再消费 chain。\`overflow_count\` 累计溢出次数（不随 flush 清零），需手动 \`reset_*_overflow_count\`。

| 接口 | 说明 |
|------|------|
| \`comp_signal_overflow_count()\` / \`entity_signal_overflow_count()\` | 查询累计溢出次数 |
| \`reset_comp_signal_overflow_count()\` / \`reset_entity_signal_overflow_count()\` | 清零溢出计数 |
| \`preallocate_component_signals(n)\` / \`preallocate_entity_signals(n)\` | 预分配 overflow chain 容量 |

\`\`\`cpp
ecs::manager mgr;
mgr.disable_entity_signals();
auto e1 = mgr.create_entity();   // 不入队
mgr.enable_entity_signals();
auto e2 = mgr.create_entity();   // 入队
// has_pending_entity_signals() == true

mgr.preallocate_component_signals(2048);  // 预分配溢出容量
\`\`\`

> **不应：** 长期忽略 \`overflow_count\`。其值 >0 表示曾发生溢出，应确认 \`flush\` 已消费完 chain（\`has_pending_*_signals()\` 为 false）。

### 9.8 delete_entity 的组件清理

\`delete_entity\` 会先遍历该实体身上所有组件触发清理，再销毁实体：

- 已注册 \`on_remove<T>\` 的组件：同步触发 \`on_remove\`。
- 未注册 \`on_remove<T>\` 的组件：remove 信号入组件延迟队列。

\`\`\`cpp
ecs::manager mgr;
mgr.preallocate_entities(10);
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
\`\`\`

> **不应：** 在 \`on_remove\` 回调内访问已销毁实体。回调在实体销毁前触发，实体此时仍有效；回调返回后实体才被销毁。
>
> **flush 顺序建议：** 先 \`flush_component_signals\` 再 \`flush_entity_signals\`。组件 remove 信号在实体销毁前入队，先消费组件信号可避免实体 id 被复用导致的归属错乱。

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
| 在即时信号回调中增删实体/组件 | 可能导致重入 | 使用延迟信号，在 flush 时处理；flush 具备重入保护与 budget 上限 |
| 依赖延迟信号缓冲区不丢事件 | 缓冲区满时落入 overflow_chain | 定期 flush；批量场景用 \`preallocate_*_signals\` 预分配 |
| 忘记 \`flush\` 延迟信号 | 事件堆积在缓冲区与 chain 中未处理 | 每帧开头或结尾调用 \`flush_*_signals\` |
| 使用有捕获的 lambda 作为即时信号回调 | 无法转换为函数指针 | 使用无捕获 lambda + \`user_data\` 传上下文 |
| 期望 \`soft_remove\` 触发 \`on_remove\` | \`soft_remove\` 仅逻辑隐藏，不析构不触发 | 需要回调改用 \`hard_remove\` |
| 同一事件既注册即时回调又等 flush | 互斥设计下不会重复通知，flush 收不到该事件 | 二选一：要即时就注册回调，要批量就不注册 |
| 在 \`on_remove\` 回调内访问已销毁实体 | 实体在回调返回后才销毁，回调内访问的是销毁前状态 | 回调内可安全读取组件，但不要依赖实体后续有效性 |
| 回调 \`user_data\` 指向局部变量后让变量超出作用域 | 回调持有悬垂指针，后续触发写入野内存，污染相邻栈变量 | 块结束前 \`set_on_*(nullptr, nullptr)\` 解绑，或让 \`user_data\` 指向生命周期足够长的对象 |

---
`
};

window.DOCS_DATA['command_buffer'] = {
  id: 'command_buffer',
  title: "command_buffer — 延迟结构变更",
  category: 'ecs',
  icon: 'C',
  order: 10,
  content: `## 10. command_buffer — 延迟结构变更

将组件添加、移除、实体销毁等结构变更操作暂存，在 \`flush\` 时一次性应用到 manager。适用于帧末批量提交、主循环延迟执行等场景。

> 单个 \`view\` 内的迭代期删除可直接使用 \`for_each_safe\`，无需 \`command_buffer\`。\`command_buffer\` 适用于跨 view 或跨帧的延迟批量操作。

### 使用

\`\`\`cpp
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
cb.delete_entity(c);

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
\`\`\`

**接口：**

| 接口 | 说明 |
|------|------|
| \`create_command_buffer()\` | manager 工厂方法，返回绑定到该 manager 的 command_buffer |
| \`add_component<T>(entity, T&&)\` | 录制添加组件命令 |
| \`remove_component<T>(entity)\` | 录制移除组件命令（soft_remove 语义） |
| \`delete_entity(entity)\` | 录制删除实体命令 |
| \`preallocate(n)\` | 预留命令缓冲容量，避免录制过程中扩容 |
| \`capacity()\` | 当前命令缓冲容量 |
| \`flush()\` | 按录入顺序应用所有命令，应用后清空缓冲区 |
| \`clear()\` | 清空所有未应用命令 |
| \`size()\` | 未应用命令数 |
| \`empty()\` | 是否为空 |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`flush\` 后用 \`entity.is_valid()\` 判断实体是否被销毁 | \`delete_entity\` 按值传入，修改的是副本 | 用 \`mgr.is_entity_valid(entity)\` 检查 |
| \`flush\` 后继续使用已销毁实体的句柄 | version 已过期，操作无效 | \`flush\` 后重新获取有效实体 |
| 跨 manager 使用 command_buffer | apply 函数绑定到创建时的 manager | 每个 manager 独立创建 command_buffer |
| 在 \`flush\` 过程中向同一 command_buffer 录入新命令 | \`flush\` 结束时清空缓冲区，新命令丢失 | \`flush\` 完成后再录入新命令 |

---
`
};

window.DOCS_DATA['reorder'] = {
  id: 'reorder',
  title: "reorder_group — 多组件重排序遍历",
  category: 'ecs',
  icon: 'O',
  order: 43,
  content: `## 13. reorder_group — 多组件重排序遍历

命名空间 \`ecs\`。对多个组件集合取交集，提供联合遍历。

### 接口

| 接口 | 说明 |
|------|------|
| \`reorder_group(mgr, sets)\` | 构造，绑定 manager 与组件集合数组 |
| \`rebuild()\` | 重建交集索引 |
| \`share_with(other)\` | 与另一个 reorder_group 共享交集状态 |
| \`size()\` | 交集实体数 |
| \`empty()\` | 交集是否为空 |
| \`contains(e)\` | 实体是否在交集中 |
| \`get<T>(e)\` | 获取实体 e 的 T 类型组件指针 |
| \`front()\` / \`back()\` | 交集首尾实体 |
| \`for_each(func)\` | 遍历交集，\`func\` 签名 \`(entity, T&...)\` 或 \`(T&...)\` |

### 使用

\`\`\`cpp

ecs::manager mgr;
auto e1 = mgr.create_entity();
auto e2 = mgr.create_entity();
mgr.add<Position>(e1, Position{1, 0, 0});
mgr.add<Velocity>(e1, Velocity{2, 0, 0});
mgr.add<Position>(e2, Position{3, 0, 0});
mgr.add<Velocity>(e2, Velocity{4, 0, 0});

// 构造 reorder_group
std::array<single_class_set*, 2> sets = {
    mgr.get_single_class_set<Position>(),
    mgr.get_single_class_set<Velocity>()
};
reorder_group<Position, Velocity> rg(&mgr, sets);

// 遍历所有同时拥有 Position 和 Velocity 的实体
rg.for_each([](entity e, Position& p, Velocity& v) {
    p.x += v.dx;
    p.y += v.dy;
});

// 检查实体是否在交集中
bool has = rg.contains(e1);  // true

// 获取组件
Velocity* v = rg.get<Velocity>(e1);
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 组件池修改后未调用 \`rebuild\` | 遍历旧数据 | 组件增删后调用 \`rebuild()\` |
| \`for_each\` 中修改组件池结构 | 迭代器失效 | 遍历期间不做增删操作 |
| \`share_with\` 后独立 \`rebuild\` | 共享状态被覆盖 | 共享方不要独立 rebuild |
`
};
