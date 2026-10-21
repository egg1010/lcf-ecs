window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['entity'] = {
  id: 'entity',
  title: "ecs::entity — 实体",
  category: 'core',
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
| \`index_\` | 实体索引（32 位） |
| \`version_\` | 实体版本号（32 位） |
| \`handle_\` | 64 位句柄（与 index_ + version_ 共用联合体） |
| \`operator==\` | 判断两个实体是否相等（比较 handle_） |
| \`operator!=\` | 判断两个实体是否不等 |
| \`std::hash<entity>\` | 哈希特化，可用于 \`std::unordered_map\` |

### 使用

\`\`\`cpp
entity e1;                    // 默认构造，无效实体
entity e2(3, 1);              // index=3, version=1
e2.is_valid();                // true
e2.index_;                    // 3
e2.version_;                  // 1

std::unordered_map<entity, int> map;
map[e2] = 42;                 // 可用作哈希键
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 手动构造 entity 的 index_ 和 version_ | 版本号不匹配会导致 ECS 管理器认为实体无效 | 始终通过 \`manager::create_entity()\` 创建实体 |
| 复用已删除的 entity 句柄 | 版本号已递增，旧句柄失效 | 删除后丢弃句柄，重新创建 |
| 将 entity 成员当作普通整数运算 | \`index_\` 和 \`version_\` 不应直接操作 | 仅通过公开接口操作 entity |

---
`
};

window.DOCS_DATA['view_tags'] = {
  id: 'view_tags',
  title: "view_tags — 视图标签类型",
  category: 'core',
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
  category: 'core',
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
| \`add_def(entity, def_id, const type_def&, const void* data)\` | 添加 def 组件（数据按字节拷入，长度为注册时的 \`type_def::size\`；首次调用惰性初始化 def 池） |
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
| \`live_count()\` | 活条目数（O(n) 扫描） |
| \`tombstone_count()\` | 软删除墓碑数（O(n) 扫描） |
| \`has_tombstones()\` | 是否存在软删除死槽（O(1)，视图分派用） |
| \`empty()\` | 是否为空 |
| \`increase_capacity(capacity)\` | 预留容量 |
| \`set_hot_set_capacity(entries)\` | 设置热集容量（2 的幂，非 2 的幂被拒绝；调整后热集清空） |
| \`hot_set_capacity()\` | 获取当前热集容量 |
| \`get_type_id_value()\` | 获取类型 ID 值 |
| \`get_typed_pool_ptr<T>()\` | 获取类型化组件池指针（带 type_id 检查） |
| \`get_typed_pool_ptr<T>() const\` | const 版本 |
| \`add/remove\` 系列接口返回值 | 返回 \`operating_message\`（值类型） |
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
  category: 'core',
  icon: 'M',
  order: 4,
  content: `## 4. ecs::manager — ECS管理器

ECS 核心管理类，管理实体和所有组件集合。

> 注：\`manager\` 可移动，禁止拷贝。

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
| \`delete_entity(entity&)\` | 删除实体（释放 ID，递增版本号） |

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

### 运行期 def 组件

配合 \`type_id::register_type_def\` 注册的类型使用（\`type_def\` 见 type_id 页），数据按字节存取，与模板组件共用掩码/视图/信号机制。

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
| \`delete_type_container<T>()\` | 删除整个类型容器 |

> \`hard_remove\` 和 \`swap_dense_and_pool\` 对 \`std::is_trivially_copyable\` 类型使用 \`typed_pool_data_\` + \`memcpy\` 直接操作，跳过函数指针间接调用。非 trivial 类型回退到 \`ops_.swap_pop\` / \`ops_.swap_pool\` 函数指针路径。

### 容器访问

| 接口 | 说明 |
|------|------|
| \`get_single_class_set<T>()\` | 获取单组件集合指针 |
| \`get_single_class_set<T>() const\` | const 版本 |
| \`get_component_container<T>()\` | 获取类型化组件池指针 |
| \`has_component<T>()\` | 查询组件类型是否已在管理器中注册（存在组件集合，不关心是否已有实例） |
| \`add/add_batch/hard_remove/soft_remove\` | 返回 \`operating_message\`（值类型） |
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
| \`get_entity_mask(entity)\` | 获取实体组件位掩码 |
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
results.reserve_exact(entities.size());
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
| \`get_entity_state(entity_index)\` | 获取实体状态引用（\`entity_state&\`） |
| \`set_entity_flag(entity_index, flag)\` | 设置实体状态标志 |
| \`clear_entity_flag(entity_index, flag)\` | 清除实体状态标志 |
| \`has_entity_flag(entity_index, flag)\` | 检查实体是否具有某状态标志 |

\`\`\`cpp
mgr.set_entity_flag(e.parts_.index_, entity_flag::disabled);
if (mgr.has_entity_flag(e.parts_.index_, entity_flag::active))
{
    // 实体处于活跃状态
}
auto& state = mgr.get_entity_state(e.parts_.index_);
state.tag = 1;   // 自定义标签
state.layer = 3; // 渲染层
\`\`\`

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
| \`get_entity_mask(entity)\` | 获取实体块 0 掩码（\`uint64_t\`，type_id 1-64；等价于 \`get_entity_block(e, 0)\`） |
| \`get_entity_block(entity, uint32_t block_idx)\` | 获取实体指定块的掩码（\`uint64_t\`，block_idx 块对应 type_id \`block_idx*64+1\` 到 \`block_idx*64+64\`） |
| \`get_entity_block_by_idx(uint32_t entity_index, uint32_t block_idx)\` | 同上，接受 entity_index 而非 entity 句柄 |
| \`get_component_bit<T>()\` | 获取组件 T 的掩码位（块 0 类型返回 \`1ULL<<offset\`，其余返回 0） |
| \`get_component_meta(int type_id)\` | 获取 \`component_meta*\`（含组件大小，type_id 越界返回 nullptr） |
| \`preallocate_mask_blocks(uint32_t num_blocks)\` | 预分配每实体掩码块数（每块 64 种组件；注册组件前调用；\`register_component_meta\` 在 type_id 超出时自动扩容） |
| \`num_mask_blocks() const\` | 当前每实体掩码块数 |
| \`get_entity_manager()\` | 获取 \`entity_manager&\`，可继续调用 \`set_mask_bit\` / \`clear_mask_bit\` / \`get_mask\` / \`get_block\` / \`set_entity_flag\` 等 |

\`\`\`cpp
uint64_t mask = mgr.get_entity_mask(e);
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
    bool has_vel = (mgr.get_entity_mask(e) & vel_bit) != 0;
}

// 启动时预分配 2 块掩码（支持 128 种组件）
mgr.preallocate_mask_blocks(2);
\`\`\`

> 默认块数为 1（支持 64 种组件）。组件注册时 \`register_component_meta\` 自动扩容掩码块数，无需手动调用 \`preallocate_mask_blocks\`。多块掩码查询通过 \`get_entity_block(e, block_idx)\` 或 \`get_entity_block_by_idx(idx, block_idx)\` 访问任意块。详见 [§ 29. multi_block_bitmask](#29-multi_block_bitmask--多块位掩码存储)。

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
  category: 'core',
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

通过 \`exactly_one()\` 获取恰好一个实体的组件引用。若实体数量不为 1，行为未定义。

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

> **注意：** 视图内实体数量不为 1 时行为未定义，调用者需自行保证。适用于单例实体、玩家实体等场景。

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
  category: 'core',
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
  category: 'core',
  icon: 'R',
  order: 7,
  content: `## 7. runtime_view — 运行时视图

在运行时动态指定组件类型组合进行查询。组件类型数量无上限，前 64 种组件类型自动维护实体位掩码，超过 64 种的组件类型同样参与所有视图/分组查询。

### 7.1 实体掩码

每个实体在添加/删除组件时自动维护组件位掩码（仅覆盖 type_id ≤ 64 的组件类型）：

\`\`\`cpp
ecs::manager mgr;
auto e = mgr.create_entity();
mgr.add(e, Position{1, 0});
mgr.add(e, Velocity{2, 0});

uint64_t mask = mgr.get_entity_mask(e);
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

> \`get_entity_mask(entity)\` 和 \`get_component_bit<T>()\` 是 \`manager\` 的方法,见 [§4](#4-ecsmanager-ecs管理器) 与 [§7.1](#71-实体掩码)。

### 7.5 组件类型无上限

组件类型数量不受 64 限制。type_id ≤ 64 的组件参与实体位掩码；type_id > 64 的组件 \`get_component_bit<T>()\` 返回 0，但仍可正常添加、查询，并参与所有视图/分组（\`view\` / \`group\` / \`owning_group\` / \`reorder_group\` / \`runtime_view\` / \`view(without)\`）的匹配。无需任何特殊处理。

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
  category: 'core',
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
  category: 'core',
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
  category: 'core',
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
\`\`\`

**接口：**

| 接口 | 说明 |
|------|------|
| \`create_command_buffer()\` | manager 工厂方法，返回绑定到该 manager 的 command_buffer |
| \`add_component<T>(entity, T&&)\` | 录制添加组件命令 |
| \`remove_component<T>(entity)\` | 录制移除组件命令（soft_remove 语义） |
| \`destroy_entity(entity)\` | 录制销毁实体命令 |
| \`preallocate(n)\` | 预留命令缓冲容量，避免录制过程中扩容 |
| \`capacity()\` | 当前命令缓冲容量 |
| \`flush()\` | 按录入顺序应用所有命令，应用后清空缓冲区 |
| \`clear()\` | 清空所有未应用命令 |
| \`size()\` | 未应用命令数 |
| \`empty()\` | 是否为空 |

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`flush\` 后用 \`entity.is_valid()\` 判断实体是否被销毁 | \`destroy_entity\` 按值传入，修改的是副本 | 用 \`mgr.is_entity_valid(entity)\` 检查 |
| \`flush\` 后继续使用已销毁实体的句柄 | version 已过期，操作无效 | \`flush\` 后重新获取有效实体 |
| 跨 manager 使用 command_buffer | apply 函数绑定到创建时的 manager | 每个 manager 独立创建 command_buffer |
| 在 \`flush\` 过程中向同一 command_buffer 录入新命令 | \`flush\` 结束时清空缓冲区，新命令丢失 | \`flush\` 完成后再录入新命令 |

---
`
};

window.DOCS_DATA['reflection_usage'] = {
  id: 'reflection_usage',
  title: "reflection — 反射模块使用",
  category: 'core',
  icon: 'R',
  order: 11,
  content: `## 11. reflection — 反射模块使用

命名空间 \`reflect\`。\`noexcept\`。

反射模块提供运行期类型元数据查询、字段访问与方法调用。支持聚合类型自动遍历公有字段、手填偏移量注册私有成员、宏注册成员/静态方法。支持运行时动态注册，注册线程安全。库独立，不创建线程，嵌入外部多线程架构。

### 11.1 注册宏

宏只能在命名空间作用域使用（通过 \`inline\` 变量初始化）。聚合类型字段名自动生成 \`field_0\`、\`field_1\`、…（上限由 \`REFLECT_MAX_FIELDS\` 决定，默认 64）。

| 宏 | 说明 |
|------|------|
| \`REGISTER(Cls)\` | 注册类型：聚合类型自动遍历公有字段；非聚合类型仅注册类型元信息（字段需 \`REFLECT_PRIVATE\` 或 \`REGISTER_MEMBERS\`） |
| \`REGISTER_MEMBERS(Cls, f1, f2, ...)\` | 注册成员（标量/数组统一入口），自动判断字段类别；支持 1~20 个字段 |
| \`REGISTER_FNS(Cls, m1, m2, ...)\` | 批量注册方法（自动判断成员/静态），支持 1~20 个方法 |
| \`REGISTER_FN_OVERLOAD(Cls, method, ptr)\` | 注册重载方法，显式指定方法指针 |
| \`REFLECT(Cls)\` | 类内标记宏，声明 friend 授权反射访问私有成员 |
| \`REFLECT_PRIVATE(Cls, ...)\` | 类外注册私有成员，偏移量和类型由成员指针自动推导；自动注册类型元信息（无需先 \`REGISTER\`） |
| \`REGISTER_PRIVATE_OFFSETS(Cls, ...)\` + \`PRIV_FIELD(name, offset, Type)\` | 手填偏移量注册私有成员，用于无法修改的第三方类型 |
| \`REGISTER_TYPE_ONLY(Cls)\` | 只注册类型元信息，不自动遍历字段。用于无字段类型，或配合 \`register_array_field\` 手动注册 |
| \`REGISTER_ENUM(EnumType, v1, v2, ...)\` | 注册枚举类型，自动生成 \`{E::v, #v}\` 名值对，支持 1~10 个枚举值 |
| \`REGISTER_BASE(Derived, Base)\` | 注册继承关系；指针调整由编译期类型转换承担，单/多继承均适用 |
| \`REGISTER_BASE_OFFSET(Derived, Base, off)\` | 显式指定派生类→基类指针调整量（特殊布局时使用） |
| \`REGISTER_FIELD_ATTR(Cls, field, key, value)\` | 为字段注册属性/注解，\`value\` 任意类型（\`void_any\` 存储） |
| \`REGISTER_CONVERT(From, To)\` | 注册类型转换（要求 \`From\` 可隐式转换到 \`To\`） |

### 11.2 查询入口

查询统一入口，无需区分单/多线程。\`query_view\` 持有 \`type_meta*\`，注册后长期有效。

| 接口 | 说明 |
|------|------|
| \`reflect::get<T>()\` | 按类型查询，未注册触发 \`std::abort()\`，返回 \`query_view\` |
| \`reflect::get_by_name(name)\` | 按类型名查询，未注册触发 \`std::abort()\`，返回 \`query_view\` |
| \`reflect::try_get<T>()\` | 软失败版本，未注册返回 \`query_view::valid()==false\`，不 abort |
| \`reflect::try_get_by_name(name)\` | 软失败版本（按名），不 abort |
| \`reflect::global()\` | 全局存储对象 |

### 11.3 query_view 接口

| 接口 | 说明 |
|------|------|
| \`name()\` | 类型名 |
| \`valid()\` | 是否绑定有效元数据（\`try_get\` 后用于判断是否注册） |
| \`meta()\` | 取底层 \`const type_meta*\`，用于访问构造/继承/容器等扩展字段 |
| \`size()\` | \`sizeof(T)\` |
| \`align()\` | \`alignof(T)\` |
| \`field_count()\` | 字段数 |
| \`method_count()\` | 方法数 |
| \`type_id_value()\` | 类型 id |
| \`field(i)\` | 按索引取字段元数据 |
| \`field_by_name(name)\` | 按名取字段元数据，未找到返回 nullptr |
| \`method(i)\` | 按索引取方法元数据 |
| \`method_by_name(name)\` | 按名取方法元数据，未找到返回 nullptr |
| \`get<R>(obj, i)\` | 按索引取字段引用（编译期偏移） |
| \`get<R>(obj, i)\` (const) | const 对象版本 |
| \`get_by_name<R>(obj, name)\` | 按名取字段引用；字段不存在即终止（带诊断） |
| \`get_by_name<R>(obj, name)\` (const) | const 对象版本 |
| \`try_get_by_name<R>(obj, name)\` | 安全取字段指针，未找到返回 \`nullptr\` |
| \`try_get_by_name<R>(obj, name)\` (const) | const 对象版本 |
| \`get_ptr(obj, name)\` | 按名取字段 \`void*\` |
| \`get_ptr(obj, name)\` (const) | const 对象版本 |
| \`invoke<R>(obj, name, args...)\` | 调用方法，返回 R。参数数量/类型不匹配、返回类型不匹配或未注册均触发 \`std::abort()\` |
| \`try_invoke<R>(obj, name, args...)\` | 软失败版本。\`R=void\` 返回 \`bool\`；非 void 返回 \`std::optional<R>\`。参数或返回类型不匹配即失败，不 abort |
| \`find_overload(name, given_ids, n_args)\` | 按参数类型 id 精确匹配重载，返回 \`const method_meta*\`；无精确匹配返回 nullptr |
| \`get_by_path(obj, path)\` | 按 \`a.b.c\` 嵌套路径取字段 \`void*\`，递归查找子类型；任一级未找到返回 nullptr |
| \`get_by_path(obj, path)\` (const) | const 对象版本 |
| \`get_by_path_as<T>(obj, path)\` | 路径访问并转型为 \`T*\` |
| \`get_by_path_as<T>(obj, path)\` (const) | const 对象版本 |
| \`for_each_field(obj, f)\` | 遍历实例字段，\`f(name, ptr, type_id)\` |
| \`for_each_field(obj, f)\` (const) | const 对象版本 |
| \`for_each_field_meta(f)\` | 遍历字段元数据，\`f(field_meta&)\` |
| \`for_each_method(f)\` | 遍历方法元数据，\`f(method_meta&)\` |
| \`is_array(i)\` | 字段 i 是否为数组 |
| \`is_array_by_name(name)\` | 按名判断字段是否为数组 |
| \`array_rank(i)\` | 数组维度数（0=标量，1~4=数组） |
| \`array_total_elements(i)\` | 数组总元素数（非数组为 0） |
| \`array_element_stride(i)\` | 数组元素步长（字节） |
| \`array_extent(field_idx, dim)\` | 第 \`dim\` 维元素数（\`uint32_t\`） |
| \`array_element_ptr(obj, field_idx, element_idx)\` | 取数组元素指针，越界或非数组返回 nullptr |
| \`array_element_ptr(obj, field_idx, element_idx)\` (const) | const 对象版本 |
| \`array_element_ptr_by_name(obj, name, element_idx)\` | 按名取数组元素指针 |
| \`for_each_array_element(obj, field_idx, f)\` | 遍历数组元素，\`f(ptr, idx, type_id)\` |
| \`for_each_array_element(obj, field_idx, f)\` (const) | const 对象版本 |
| **便捷接口** | |
| \`array_info(field_idx)\` | 聚合查询，返回 \`const field_meta*\`，非数组返回 nullptr |
| \`array_info_by_name(name)\` | 按名聚合查询 |
| \`array_get<T>(obj, field_idx, element_idx)\` | 类型安全访问；越界即终止（带诊断） |
| \`array_get<T>(obj, field_idx, element_idx)\` (const) | const 对象版本 |
| \`array_get_by_name<T>(obj, name, element_idx)\` | 按名类型安全访问；越界或字段不存在即终止 |
| \`try_array_get<T>(obj, field_idx, element_idx)\` | 安全取数组元素指针，越界返回 \`nullptr\` |
| \`try_array_get<T>(obj, field_idx, element_idx)\` (const) | const 对象版本 |
| \`array_set<T>(obj, field_idx, element_idx, value)\` | 类型安全写入 |
| \`array_set_by_name<T>(obj, name, element_idx, value)\` | 按名类型安全写入 |

### 11.4 construct_view — 对象构造/销毁

反射式构造与析构，基于 \`type_meta\` 的默认构造/析构函数指针。需类型可默认构造。

| 接口 | 说明 |
|------|------|
| \`construct_view()\` | 默认构造，未绑定 |
| \`has_default_construct()\` | 类型是否可默认构造 |
| \`create()\` | 堆分配默认构造，返回 \`void*\`；不可构造或未绑定返回 nullptr |
| \`create_inplace(buf)\` | 就地默认构造（\`buf\` 需足够大且对齐），返回 \`void*\` |
| \`destroy(obj)\` | 析构对象（不释放内存，用于就地对象） |
| \`destroy_heap(obj)\` | 析构并释放堆内存，与 \`create()\` 配对（含过对齐类型的对齐释放） |
| \`reflect::get_construct<T>()\` | 按类型取 \`construct_view\` |
| \`reflect::get_construct(q)\` | 由 \`query_view\` 构造 \`construct_view\` |

### 11.5 inheritance_view — 继承关系

查询直接/间接基类与派生类，支持向上转型。需先 \`REGISTER_BASE\` 注册关系。

| 接口 | 说明 |
|------|------|
| \`is_derived_from(base_type_id)\` | 是否继承自指定类型（含间接继承） |
| \`is_derived_from<Base>()\` | 模板版本 |
| \`is_base_of(derived_type_id)\` | 是否是指定类型的基类 |
| \`is_base_of<Derived>()\` | 模板版本 |
| \`upcast(derived_obj, base_type_id)\` | 派生类指针向上转型为基类指针，失败返回 nullptr |
| \`upcast<Base>(derived_obj)\` | 模板版本，返回 \`Base*\` |
| \`base_count()\` | 直接基类数量 |
| \`derived_count()\` | 直接派生类数量 |
| \`base_type_id_at(idx)\` | 取第 idx 个直接基类 type_id，越界返回 -1 |
| \`derived_type_id_at(idx)\` | 取第 idx 个直接派生类 type_id，越界返回 -1 |
| \`for_each_base(f)\` | 遍历直接基类，\`f(base_type_id, offset)\` |
| \`for_each_derived(f)\` | 遍历直接派生类，\`f(derived_type_id)\` |
| \`reflect::get_inheritance<T>()\` | 按类型取 \`inheritance_view\` |
| \`reflect::get_inheritance(q)\` | 由 \`query_view\` 构造 |

### 11.6 enum_view — 枚举反射

枚举值↔名称互转与遍历。需先 \`REGISTER_ENUM\` 注册。

| 接口 | 说明 |
|------|------|
| \`valid()\` | 是否绑定有效枚举元数据 |
| \`name()\` | 枚举类型名 |
| \`type_id_value()\` | 枚举类型 id |
| \`underlying_type_id()\` | 底层整数类型 id |
| \`value_count()\` | 枚举值数量 |
| \`value_to_name(uint64_t)\` | 值→名称，失败返回 nullptr |
| \`value_to_name(E value)\` | 模板版本，接受枚举值 |
| \`name_to_value(name, out)\` | 名称→值（\`uint64_t&\`），失败返回 false |
| \`name_to_value<E>(name, out)\` | 模板版本，\`out\` 为 \`E&\` |
| \`for_each_value(f)\` | 遍历所有枚举值，\`f(uint64_t value, const char* name)\` |
| \`reflect::get_enum<E>()\` | 按枚举类型取 \`enum_view\` |
| \`reflect::get_enum(type_id)\` | 按类型 id 取 \`enum_view\` |

### 11.7 attribute_view — 属性/注解

查询字段附加的键值对属性。需先 \`REGISTER_FIELD_ATTR\` 注册。

| 接口 | 说明 |
|------|------|
| \`has_attr(field_idx, key)\` | 按字段索引判断是否含指定属性 |
| \`has_attr(field_name, key)\` | 按字段名判断 |
| \`get_attr(field_idx, key)\` | 取属性值 \`const void_any*\`，失败返回 nullptr |
| \`get_attr(field_name, key)\` | 按字段名取 |
| \`get_attr_as<V>(field_idx, key)\` | 类型安全取属性值，返回 \`const V*\` |
| \`get_attr_as<V>(field_name, key)\` | 按字段名类型安全取 |
| \`for_each_attr(field_idx, f)\` | 遍历字段所有属性，\`f(key_hash, void_any&)\` |
| \`reflect::get_attributes<T>()\` | 按类型取 \`attribute_view\` |
| \`reflect::get_attributes(q)\` | 由 \`query_view\` 构造 |

### 11.8 container_view — 容器反射

统一访问顺序容器（\`dense<T>\`/\`std::vector\` 等）的元素。类型注册时自动登记容器特征，无需额外调用。

| 接口 | 说明 |
|------|------|
| \`valid()\` | 是否绑定有效容器与 ops |
| \`is_container()\` | ops 是否非空 |
| \`category()\` | 容器类别（\`sequential\`/\`none\`） |
| \`element_type_id()\` | 元素类型 id |
| \`size()\` | 元素数 |
| \`get(index)\` | 取元素 \`void*\`，越界返回 nullptr |
| \`get_as<T>(index)\` | 类型安全取元素 |
| \`push_back(element)\` | 追加元素（\`const void*\`） |
| \`push_back<T>(value)\` | 模板版本 |
| \`clear()\` | 清空容器 |
| \`reserve(n)\` | 预留容量 |
| \`for_each(f)\` | 遍历元素，\`f(void* element, size_t index)\` |
| \`reflect::as_container(obj, type_id)\` | 由对象指针与类型 id 构造 \`container_view\` |
| \`reflect::as_container(q, obj)\` | 由 \`query_view\`（字段类型）与对象指针构造 |

### 11.9 virtual_dispatch_view — 动态派发

查询方法是否虚函数及 vtable 偏移。通过 \`register_method\` 注册的虚函数，invoker 已正确处理 vtable，可直接 \`invoke_virtual\` 调用。

| 接口 | 说明 |
|------|------|
| \`is_virtual(method_name)\` | 方法是否为虚函数 |
| \`vtable_offset(method_name)\` | 虚函数 vtable 偏移（非虚返回 -1） |
| \`invoke_virtual<R>(obj, name, args...)\` | 调用虚函数（复用重载解析），未找到触发 \`std::abort()\` |
| \`reflect::get_virtual_dispatch<T>()\` | 按类型取 \`virtual_dispatch_view\` |
| \`reflect::get_virtual_dispatch(q)\` | 由 \`query_view\` 构造 |

### 11.10 convert_view — 类型转换

查询与执行已注册的类型转换。需先 \`REGISTER_CONVERT\` 注册。

| 接口 | 说明 |
|------|------|
| \`can_convert_to(target_type_id)\` | 是否可转换到目标类型 |
| \`can_convert_to<U>()\` | 模板版本 |
| \`convert_to(src, target_type_id, dst)\` | 执行转换写入 \`dst\`，失败返回 false |
| \`convert_to<U>(src)\` | 类型安全转换，返回 \`std::optional<U>\` |
| \`for_each_convertible(f)\` | 遍历所有可转换目标，\`f(target_type_id)\` |
| \`reflect::get_convert<T>()\` | 按类型取 \`convert_view\` |
| \`reflect::get_convert(q)\` | 由 \`query_view\` 构造 |

### 11.11 compare_view — 字段比较/克隆

逐字段相等比较、克隆、赋值与析构。数组字段逐元素处理；未登记字段操作表的字段回退到 \`memcmp\`/\`memcpy\`。

| 接口 | 说明 |
|------|------|
| \`equal(a, b)\` | 逐字段相等比较（\`a==b\` 返回 true；其一为 nullptr 返回 false） |
| \`clone(src, dst)\` | 逐字段拷贝构造（\`dst\` 必须是未初始化内存） |
| \`copy_assign(src, dst)\` | 逐字段赋值（\`dst\` 已构造） |
| \`destroy_fields(obj)\` | 逐字段析构（对象销毁前调用） |
| \`reflect::get_compare<T>()\` | 按类型取 \`compare_view\` |
| \`reflect::get_compare(q)\` | 由 \`query_view\` 构造 |

### 11.12 hash_view — 字段哈希

逐字段 FNV-1a 哈希组合。已登记字段操作表的字段用其哈希函数计算，否则按字节级 FNV-1a。

| 接口 | 说明 |
|------|------|
| \`hash(obj)\` | 计算对象哈希，返回 \`uint64_t\`；未绑定或 \`obj\` 为空返回 0 |
| \`reflect::get_hash<T>()\` | 按类型取 \`hash_view\` |
| \`reflect::get_hash(q)\` | 由 \`query_view\` 构造 |

### 使用

\`\`\`cpp

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
    std::cout << name << " = " << *static_cast<float*>(ptr) << "\\n";
});

view.for_each_field_meta([](const reflect::field_meta& fm) {
    std::cout << fm.name << " offset=" << fm.offset << "\\n";
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

// === #3 枚举反射 ===
enum class Element { Fire, Water, Earth, Air };
REGISTER_ENUM(Element, Fire, Water, Earth, Air);  // 自动生成名值对

auto ev = reflect::get_enum<Element>();
ev.value_count();                                    // 4
ev.value_to_name(Element::Fire);                     // "Fire"
Element out{};
ev.name_to_value("Air", out);                        // true, out == Element::Air
ev.for_each_value([](uint64_t v, const char* name) {
    std::cout << name << " = " << v << "\\n";
});

// === #2 继承关系 ===
struct Base { int base_val; };
struct Derived : Base { int derived_val; };
REGISTER(Base);
REGISTER_TYPE_ONLY(Derived);
REGISTER_PRIVATE_OFFSETS(Derived,
    PRIV_FIELD("base_val",    offsetof(Derived, base_val),    int),
    PRIV_FIELD("derived_val", offsetof(Derived, derived_val), int));
REGISTER_BASE(Derived, Base);   // 注册继承关系（指针调整自动）

auto iv = reflect::get_inheritance<Derived>();
iv.base_count();                       // 1
iv.is_derived_from<Base>();            // true
Derived d{};
d.base_val = 10;
Base* b = iv.upcast<Base>(&d);         // 向上转型
std::cout << b->base_val;              // 10

auto ivb = reflect::get_inheritance<Base>();
ivb.derived_count();                   // 1
ivb.is_base_of<Derived>();             // true

// === #4 字段属性/注解 ===
struct Player { int hp; int mp; float x; };
REGISTER_TYPE_ONLY(Player);
REGISTER_MEMBERS(Player, hp, mp, x);
REGISTER_FIELD_ATTR(Player, hp, "range_min", 0);       // int 属性
REGISTER_FIELD_ATTR(Player, hp, "range_max", 100);
REGISTER_FIELD_ATTR(Player, hp, "category", "Combat");  // 字符串属性

auto av = reflect::get_attributes<Player>();
av.has_attr("hp", "range_min");                       // true
const int* min_val = av.get_attr_as<int>("hp", "range_min");   // *min_val == 0
const int* max_val = av.get_attr_as<int>("hp", "range_max");   // *max_val == 100

// === #7 类型转换 ===
REGISTER_TYPE_ONLY(int);
REGISTER_TYPE_ONLY(float);
REGISTER_CONVERT(int, int64_t);     // int → int64_t
REGISTER_CONVERT(float, double);    // float → double

auto cv = reflect::get_convert<int>();
cv.can_convert_to<int64_t>();       // true
int src = 42;
int64_t dst{};
cv.convert_to(&src, type_id::get_type_id<int64_t>(), &dst);  // dst == 42
auto result = cv.convert_to<int64_t>(&src);  // std::optional<int64_t>, *result == 42

// === #1 对象构造/销毁 ===
struct Constructable { int a; float b; };
REGISTER(Constructable);

auto ctv = reflect::get_construct<Constructable>();
ctv.has_default_construct();        // true
void* obj = ctv.create();           // 堆分配默认构造
// ... 使用 obj ...
ctv.destroy_heap(obj);              // 析构并释放

alignas(Constructable) char buf[sizeof(Constructable)];
void* obj2 = ctv.create_inplace(buf);  // 就地构造
ctv.destroy(obj2);                     // 仅析构（不释放）

// === #8 字段比较/克隆 ===
struct CompareTest { int a; float b; };
REGISTER(CompareTest);

auto cmpv = reflect::get_compare<CompareTest>();
CompareTest a{1, 2.0f}, b{1, 2.0f}, c{1, 3.0f};
cmpv.equal(&a, &b);                 // true
cmpv.equal(&a, &c);                 // false

alignas(CompareTest) char cbuf[sizeof(CompareTest)];
cmpv.clone(&a, cbuf);               // 拷贝构造到未初始化内存
cmpv.copy_assign(&a, &c);           // 逐字段赋值 (c 已构造)
cmpv.destroy_fields(&c);            // 逐字段析构

// === #9 字段哈希 ===
auto hv = reflect::get_hash<CompareTest>();
uint64_t ha = hv.hash(&a);          // FNV-1a 组合哈希
uint64_t hb = hv.hash(&b);          // ha == hb (字段值相同)

// === #11 字段路径访问 (嵌套) ===
struct Inner { int value; };
struct Outer { Inner inner; };
REGISTER_TYPE_ONLY(Inner);
REGISTER_MEMBERS(Inner, value);
REGISTER_TYPE_ONLY(Outer);
REGISTER_MEMBERS(Outer, inner);

Outer obj{};
obj.inner.value = 42;
auto q = reflect::get<Outer>();
void* p = q.get_by_path(&obj, "inner.value");   // 指向 obj.inner.value
int* ip = q.get_by_path_as<int>(&obj, "inner.value");  // *ip == 42

// === #12 重载按类型匹配 ===
auto q2 = reflect::get<Calculator>();
int ids[] = { type_id::get_type_id<int>(), type_id::get_type_id<int>() };
const reflect::method_meta* m = q2.find_overload("add", ids, 2);  // 精确匹配 add(int,int)

// === #5 容器反射 ===
reflect::global().register_type_only<dense<int>>("dense<int>");  // 类型注册时自动登记容器特征

dense<int> vec;
vec.push_back(10);
vec.push_back(20);
auto ccv = reflect::as_container(&vec, type_id::get_type_id<dense<int>>());
ccv.valid();                // true
ccv.size();                 // 2
int* p0 = ccv.get_as<int>(0);  // *p0 == 10
ccv.for_each([](void* elem, size_t idx) {
    std::cout << idx << ": " << *static_cast<int*>(elem) << "\\n";
});
ccv.push_back<int>(30);     // 追加元素
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 在函数内部使用 \`REGISTER\` 等注册宏 | \`inline\` 变量不能在块作用域声明 | 宏在命名空间作用域使用；函数内直接调 \`reflect::global().register_type<T>(name)\` |
| 未注册类型就查询 | \`std::abort()\` | 先 \`REGISTER(T)\` 再 \`reflect::get<T>()\` |
| 未注册私有成员就访问 | \`field_by_name\` 返回 nullptr，解引用崩溃 | 先 \`REFLECT_PRIVATE\` 或 \`REGISTER_PRIVATE_OFFSETS\` |
| \`REFLECT_PRIVATE\` 字段名写错 | 编译错误（成员不存在） | 字段名与类定义一致 |
| \`REGISTER_PRIVATE_OFFSETS\` 偏移量填错 | 访问错误内存 | 优先用 \`REFLECT_PRIVATE\` 自动推导；第三方类型用 \`offsetof\` 确认 |
| 方法参数数量不匹配 | \`std::abort()\` | 调用 \`invoke\` 时参数数量与方法签名一致 |
| 静态方法传非空 \`obj\` | 被忽略，不影响结果 | 静态方法传 \`nullptr\` |
| 字段数超过 \`MAX_FIELDS_PER_TYPE\`（256） | \`std::abort()\` | 单类型字段数不超过上限 |
| 方法数超过 \`MAX_METHODS_PER_TYPE\`（256） | \`std::abort()\` | 单类型方法数不超过上限 |
| 类型 id 超过 \`MAX_TYPE_ID\`（65536） | 注册被忽略 | 类型总数不超过上限 |
| 含 C 数组字段的类型直接用 \`REGISTER\` | 结构化绑定展开数组为多个标量字段，字段计数错误 | 改用 \`REGISTER_MEMBERS\`（自动判断标量/数组 + 推导数组元数据） |
| \`REGISTER_MEMBERS\` 数组维度数超过 4 | 注册被忽略 | 维度数限制 1~4，更高维度需拆分为结构体嵌套 |
| \`array_element_ptr\` 越界访问 | 返回 nullptr | \`element_idx\` 须 < \`array_total_elements\`；\`array_get\` 越界会带诊断终止 |
| 认为必须先注册类型再注册成员/方法/属性/继承/转换 | 无需如此 | 这些注册会自动创建类型元信息，顺序无关 |
| 未 \`REGISTER_BASE\` 就调用 \`upcast\` | 返回 nullptr | 先 \`REGISTER_BASE(Derived, Base)\` 注册继承关系 |
| 未 \`REGISTER_ENUM\` 就调用 \`get_enum\` | \`enum_view::valid()==false\` | 先 \`REGISTER_ENUM(E, ...)\` 注册枚举 |
| 未 \`REGISTER_FIELD_ATTR\` 就调用 \`get_attr\` | 返回 nullptr | 先 \`REGISTER_FIELD_ATTR\` 注册属性 |
| 未 \`REGISTER_CONVERT\` 就调用 \`convert_to\` | 返回 false/\`nullopt\` | 先 \`REGISTER_CONVERT(From, To)\` 注册转换 |
| \`convert_to\` 的 \`From\` 不可隐式转换到 \`To\` | 编译错误 | \`REGISTER_CONVERT\` 要求 \`is_convertible_v<From, To>\` |
| \`clone\` 的 \`dst\` 已构造 | 内存泄漏/双重析构 | \`clone\` 仅用于未初始化内存；已构造对象用 \`copy_assign\` |
| \`create_inplace\` 的 \`buf\` 未对齐 | placement new 未对齐访问崩溃 | \`alignas(T) char buf[sizeof(T)]\` |
| 容器类型未注册就用 \`as_container\` | \`container_view::valid()==false\` | 先 \`REGISTER\`/\`register_type_only\` 注册类型（容器特征随之登记） |
| \`get_by_path\` 路径中间字段未注册子类型 | 返回 nullptr | 中间字段类型也需 \`REGISTER\`/\`REGISTER_MEMBERS\` |
| \`REGISTER_ENUM\` 枚举值超过 10 个 | 宏展开错误 | 超过 10 个值时直接调 \`reflect::global().register_enum<E>(name, {...})\` |

### 私有成员注册方式选择

| 方式 | 适用场景 | 用户输入 |
|------|---------|---------|
| \`REFLECT\` + \`REFLECT_PRIVATE\` | 可修改类定义的类型 | 类内 1 行 friend，类外只写字段名 |
| \`REGISTER_PRIVATE_OFFSETS\` | 无法修改的第三方类型 | 手填字段名、偏移量、类型 |

\`REFLECT_PRIVATE\` 通过成员指针自动推导偏移量和类型，编译期保证正确性，优先使用。

---
`
};

window.DOCS_DATA['serialization'] = {
  id: 'serialization',
  title: "serialization — 序列化/反序列化",
  category: 'core',
  icon: 'Z',
  order: 12,
  content: `## 12. serialization — 序列化/反序列化

命名空间 \`ecs\`。\`noexcept\`。

模块分两层。**通用对象序列化**（\`object_archive\`）与 \`part/codec\` 编解码原语不依赖 ECS，可序列化任意数据；**ECS 存档**（\`full_archive\`）在其之上枚举实体与组件。两层共用同一份字段层与编解码原语。

| 能力 | 说明 |
|------|------|
| 统一入口 | 一次引入序列化与全部子能力 |
| 通用对象序列化 | 单对象 / 对象数组，不依赖 ECS |
| 完整 ECS 存档 | 实体与组件的保存 / 加载门面 |
| 存档模型 | header / metadata / 实体重映射 / 安全限制 / 格式与模式枚举 |
| 字段层 | 反射字段 ↔ 编码器，格式分派与变换链 |
| 类型登记 | 稳定类型名、类型工厂表、枚举与实体引用字段注册 |
| 实体枚举与重建 | 运行期枚举实体、索引保序重建 |
| 选择性过滤 | 按条件筛选存档内容 |
| 字段迁移 | 字段级迁移 + 组件版本控制 |
| 统计与导出 | 统计信息、跨语言 schema 导出 |
| 编码器 | 抽象接口 + JSON / 二进制 / Protobuf / FlatBuffers |
| 安全工具 | 安全限制、base64 / RLE / CRC32C |

---

# 一、通用对象序列化（不依赖 ECS）

### 12.1 载荷约定

| 数据类型 | 序列化方式 | 要求 |
|---------|-----------|------|
| 提供 \`to_json()\` / \`from_json()\` | 原生 JSON 值 | 两方法成对提供 |
| 已注册反射（\`REGISTER_MEMBERS\`） | 遍历字段生成对象 / 数组 | 字段为基本类型、\`std::string\`、嵌套对象、数组或已注册枚举 |
| trivially copyable 且未反射 | 原始字节（JSON 格式下为 base64 字符串） | 无 |
| 其他 | 空载荷（加载时跳过） | — |

### 12.2 object_archive 接口

命名空间 \`ecs\`。序列化任意数据类型，不需要 \`register_components\`，不依赖 \`manager\`。

| 接口 | 说明 |
|------|------|
| \`object_archive()\` | 构造 |
| \`bridge\` | \`codec_bridge\`：校验开关、压缩 / 加密函数对、保存 / 加载变换钩子 |
| \`limits\` | \`safety_limits\`：文件大小、元素数等上限 |
| \`format_version\` | 存档格式版本，加载时高于当前值拒绝（默认 1） |
| \`save_object(out, fmt, obj)\` / \`load_object(data, obj)\` | 单对象字符串读写 |
| \`save_array(out, fmt, items, count)\` / \`load_array(data, dense<T>& out)\` | 对象数组字符串读写，\`items\` 为 \`const T*\` |
| \`save_object_to_file(path, fmt, obj)\` / \`load_object_from_file(path, obj)\` | 单对象文件读写 |
| \`save_array_to_file(path, fmt, items, count)\` / \`load_array_from_file(path, dense<T>& out)\` | 对象数组文件读写 |

\`fmt\` 为 \`archive_format::json\`（默认）/ \`binary\` / \`protobuf\` / \`flatbuffer\`；加载自动检测格式。

### 12.3 使用示例

\`\`\`cpp

struct item_record { int id; float weight; std::string name; };
REGISTER_TYPE_ONLY(item_record);
REGISTER_MEMBERS(item_record, id, weight, name);

object_archive oa;

// 单对象
item_record one{7, 1.5f, "alpha"};
std::string js;
oa.save_object(js, archive_format::json, one);
item_record back{};
oa.load_object(js, back);

// 对象数组 (配置表 / 数据块)
dense<item_record> items;
items.push_back(item_record{1, 1.0f, "a"});
items.push_back(item_record{2, 2.0f, "b"});
std::string bin;
oa.save_array(bin, archive_format::protobuf, items.data(), items.size());

dense<item_record> loaded;
oa.load_array(bin, loaded);              // 自动检测格式

// 变换链 (校验默认启用)
oa.bridge.enable_checksum = true;
oa.bridge.compress_cb = rle_compress;
oa.bridge.decompress_cb = rle_decompress;
oa.bridge.encrypt_cb = my_encrypt;
oa.bridge.decrypt_cb = my_decrypt;
\`\`\`

### 12.4 对象存档表示

JSON 格式的根结构：

\`\`\`json
{
  "v": 1,
  "items": [
    {"id": 7, "weight": 1.5, "name": "alpha"}
  ]
}
\`\`\`

- \`v\`：格式版本
- \`items\`：对象数组；单对象即长度为 1
- 元素内容按 12.1 生成（反射类型为对象，原始字节为 base64 字符串）

### 12.5 编码器抽象接口

各编码器实现下列接口，与格式无关。

\`archive_writer\`：

| 接口 | 说明 |
|------|------|
| \`begin_object()\` / \`end_object()\` | 对象起止 |
| \`begin_array(count)\` / \`end_array()\` | 数组起止 |
| \`key(k)\` | 字段键 |
| \`write_bool(v)\` / \`write_i32(v)\` / \`write_u32(v)\` / \`write_i64(v)\` / \`write_u64(v)\` | 写入整数 |
| \`write_f32(v)\` / \`write_f64(v)\` | 写入浮点 |
| \`write_string(v)\` | 写入字符串 |
| \`write_bytes(data, len)\` | 写入原始字节 |
| \`write_raw(fragment)\` | 嵌入已格式化片段 |
| \`take()\` / \`size()\` | 取走结果 / 当前长度 |

\`archive_reader\`：

| 接口 | 说明 |
|------|------|
| \`enter_object()\` / \`leave_object()\` | 进入 / 退出对象 |
| \`enter_array()\` / \`leave_array()\` | 进入 / 退出数组 |
| \`next_element()\` / \`end_element()\` | 数组迭代 |
| \`next_key()\` | 下一字段键（空表示结束） |
| \`read_bool()\` / \`read_i32()\` / \`read_u32()\` / \`read_i64()\` / \`read_u64()\` | 读取整数 |
| \`read_f32()\` / \`read_f64()\` | 读取浮点 |
| \`read_string_view()\` / \`read_string()\` | 读取字符串（视图 / 拷贝） |
| \`read_bytes_view(len)\` | 读取字节视图 |
| \`set_element_size(size)\` | 声明数组元素编码宽度 |
| \`skip_value()\` | 跳过当前值 |
| \`has_error()\` / \`last_error()\` / \`peek_type()\` | 状态查询 |

\`archive_codec\`：

| 接口 | 说明 |
|------|------|
| \`magic[4]\` | 格式标识（可覆写 \`matches\` 自定义检测） |
| \`create_writer()\` / \`create_reader(data)\` | 创建读写器 |
| \`destroy_writer(w)\` / \`destroy_reader(r)\` | 销毁 |
| \`matches(data)\` / \`validate(data)\` | 格式检测 / 结构校验 |

### 12.6 编码器注册表


| 接口 | 说明 |
|------|------|
| \`codec_registry::instance()\` | 单例 |
| \`register_codec(c)\` | 注册自定义编码器 |
| \`detect(data)\` | 按各编码器 \`matches\` 检测格式 |
| \`get(idx)\` / \`count()\` | 按索引取编码器 / 数量 |
| \`get_codec(fmt_idx)\` | 按 \`codec_index\` 取编码器 |
| \`detect_codec(data)\` | 自动检测并返回编码器 |

\`codec_index\`：\`json=0\` / \`binary=1\` / \`protobuf=2\` / \`flatbuffer=3\`。

### 12.7 四种内置编码器

| 编码器 | 格式标识 | 字段标识方式 |
|--------|---------|------------|
| \`json_codec\` | 首字符 \`{\` | 字段名 |
| \`binary_codec\` | \`LCE1\` | 字段名 |
| \`protobuf_codec\` | 无（按 wire 结构判定） | 字段编号 = 写入顺序 |
| \`flatbuffer_codec\` | file_identifier \`LCFB\` | 字段编号 = 写入顺序 |

JSON / 二进制按字段名读写；Protobuf / FlatBuffers 不依赖键名，按写入顺序分配字段编号，读取按同样顺序推进，因此任意键名（含非 ECS 数据）均可正确往返。

### 12.8 json_writer 接口

全局命名空间。

| 接口 | 说明 |
|------|------|
| \`json_writer(reserve, pretty)\` | 构造，预分配缓冲、可选美化输出 |
| \`begin_object()\` / \`end_object()\` | 对象 |
| \`begin_array()\` / \`end_array()\` | 数组 |
| \`key(k)\` | 键 |
| \`value(v)\` | 值（string / int / uint / float / double / bool） |
| \`null()\` | null 值 |
| \`raw_value(json)\` | 嵌入原始 JSON 片段 |
| \`string()\` / \`take()\` | 取结果 |

### 12.9 json_reader 接口

全局命名空间。

| 接口 | 说明 |
|------|------|
| \`json_reader(src)\` | 构造，传入 JSON 字符串 |
| \`enter_object()\` / \`exit_object()\` | 进入 / 退出对象 |
| \`enter_array()\` / \`exit_array()\` | 进入 / 退出数组 |
| \`next_key()\` | 下一键（空表示结束） |
| \`next_element()\` / \`end_element()\` | 数组迭代 |
| \`read_int32()\` / \`read_uint32()\` / \`read_int64()\` / \`read_uint64()\` | 读取整数 |
| \`read_double()\` / \`read_bool()\` / \`read_string()\` | 读取其他标量 |
| \`read_raw_value()\` | 读取原始 JSON 片段 |
| \`skip_value()\` | 跳过当前值 |
| \`has_error()\` / \`last_error()\` / \`clear_error()\` | 错误状态 |

### 12.10 用户手写 JSON 偏差容忍

\`json_reader\` 兼容常见非标准写法：

| 偏差 | 示例 | 支持 |
|------|------|------|
| 多余空白 | \`{ "a" : 1 }\` | ✓ |
| 尾随逗号 | \`{"a":1,}\` / \`[1,2,]\` | ✓ |
| 单引号字符串 | \`{'a':'x'}\` | ✓ |
| 块注释 | \`{"a":1/* c */}\` | ✓ |
| 行注释 | \`{"a":1}\` 后跟 \`// c\` | ✓ |
| 数字前导 \`+\` | \`{"a":+1}\` | ✓ |

### 12.11 反射字段层（field_codec）

四种格式共用同一份反射字段实现。

| 字段类型 | 处理方式 |
|---------|---------|
| 基本类型（int / float / bool / string） | 直接读写 |
| 嵌套对象 | 递归进入子对象 |
| 数组字段 | 逐元素读写 |
| 已注册枚举 | 按底层整数读写 |

| 接口 | 说明 |
|------|------|
| \`to_archive(w, obj)\` / \`from_archive(r, obj)\` | 反射对象 ↔ 编码器读写 |
| \`has_json_serialize<T>\` / \`has_json_deserialize<T>\` | 概念：类型提供 \`to_json\` / \`from_json\` |
| \`is_reflected<T>()\` / \`is_type_reflected(tid)\` | 反射注册查询 |

### 12.12 跨语言 schema 导出


| 接口 | 说明 |
|------|------|
| \`to_proto<Ts...>()\` | proto3 schema 文本 |
| \`to_fbs<Ts...>()\` | FlatBuffers schema 文本（file_identifier \`LCFB\`） |

字段编号约定：根 \`version\` / \`engine\` / \`meta\` / \`cv\` / \`entities\` / \`components\` = f1..f6；实体 \`i\` / \`v\` / \`f\` / \`t\` / \`l\` / \`g\` = f1..f6；组件条目 \`i\` / \`v\` / \`d\` = f1..f3；组件表按注册顺序编号。

### 12.13 原语使用示例

\`\`\`cpp

// 直接使用编码器
protobuf_codec pc;
archive_writer* w = pc.create_writer();
w->begin_object();
w->key("version"); w->write_u32(42);
w->key("name");    w->write_string("hello");
w->end_object();
std::string data = w->take();
pc.destroy_writer(w);

archive_reader* r = pc.create_reader(data);
if (r->enter_object()) {
    std::string_view k;
    while (!(k = r->next_key()).empty()) {
        if (k == "f1") { (void)r->read_u32(); }
        else if (k == "f2") { (void)r->read_string_view(); }
        else { r->skip_value(); }
    }
}
pc.destroy_reader(r);

// 自动检测格式
const archive_codec* codec = detect_codec(data);

// 注册自定义编码器
class my_codec final : public archive_codec {
    // create_writer / create_reader / destroy_writer / destroy_reader / matches
};
codec_registry::instance().register_codec(&my_codec_instance);
\`\`\`

---

# 二、ECS 存档（full_archive）

### 12.14 组件载荷约定

| 组件类型 | 序列化方式 | 要求 |
|---------|-----------|------|
| 提供 \`to_json()\` / \`from_json()\` | 原生 JSON 值 | 两方法成对提供 |
| 已注册反射 | 按反射字段顺序写入类型化字段 | 字段编号 = 声明顺序 |
| trivially copyable 且未反射 | 原始字节 | 无 |
| 其他 | 空载荷（加载时跳过） | — |

### 12.15 full_archive 接口

命名空间 \`ecs\`。ECS 存档唯一入口，\`register_components\` 一次后保存 / 加载免 \`Ts...\`。

| 接口 | 说明 |
|------|------|
| \`full_archive(manager& m)\` | 构造，绑定 manager |
| \`register_components<Ts...>()\` | 注册组件类型（幂等），保存与加载两端次序须一致 |
| \`save_to_string(out, fmt)\` / \`save_to_file(path, fmt)\` | 全量保存，\`fmt\` 同 12.2 |
| \`load_from_string(data)\` / \`load_from_file(path)\` | 全量加载，自动检测格式 |
| \`set_archive_version(v)\` / \`archive_version()\` | 存档版本读写 |
| \`set_engine_version(v)\` / \`engine_version()\` | 引擎版本读写 |
| \`set_metadata(key, value)\` / \`get_metadata(key)\` / \`all_metadata()\` | 元数据读写 |
| \`set_checksum_enabled(b)\` / \`is_checksum_enabled()\` | CRC32C 校验开关（默认启用） |
| \`set_filter(f)\` / \`filter()\` | 选择性过滤器 |
| \`set_load_mode(m)\` / \`get_load_mode()\` | 加载模式 |
| \`set_load_policy(p)\` / \`get_load_policy()\` | 加载策略 |
| \`set_compression(c, d)\` / \`set_encryption(e, d)\` | 压缩 / 加密函数对 |
| \`on_save(cb)\` / \`on_load(cb)\` / \`set_progress_callback(cb)\` | 变换钩子 / 进度回调 |
| \`limits()\` / \`last_stats()\` | 安全限制 / 上一次统计 |
| \`component_schema()\` | 运行期收集的组件类型描述 |
| \`schema_proto()\` / \`schema_fbs()\` | 按注册表生成 \`.proto\` / \`.fbs\` |

### 12.16 使用示例

\`\`\`cpp

ecs::manager mgr;
auto e1 = mgr.create_entity();
mgr.add(e1, Position{1.0f, 2.0f, 3.0f});
mgr.get_entity_state(e1.parts_.index_).tag = 5;

full_archive saver(mgr);
saver.register_components<Position, Velocity>();
saver.set_metadata("author", "dev");
saver.set_checksum_enabled(false);

std::string data;
saver.save_to_string(data, archive_format::protobuf);

ecs::manager mgr2;
full_archive loader(mgr2);
loader.register_components<Position, Velocity>();
loader.load_from_string(data);          // 自动检测格式

uint32_t idx = mgr2.get_single_class_set<Position>()->get_entity_indices()[0];
// 索引与状态还原: idx == e1.parts_.index_, mgr2.get_entity_state(idx).tag == 5

std::string proto = saver.schema_proto();
std::string fbs   = saver.schema_fbs();
\`\`\`

### 12.17 存档表示

JSON 格式的根结构：

\`\`\`json
{
  "version": 1,
  "engine": 0,
  "meta": {"author": "dev"},
  "cv": {"Hp": 2},
  "entities": [{"i": 1, "v": 1, "f": 0, "t": 5, "l": 0, "g": 0}],
  "components": {"Hp": [{"i": 1, "v": 1, "d": {"c": 42, "m": 100}}]}
}
\`\`\`

- \`version\` / \`engine\`：存档 / 引擎版本，加载时高版本拒绝
- \`meta\` / \`cv\`：元数据 / 组件版本表（嵌套对象）
- \`entities\`：实体表；\`i\` 索引、\`v\` 版本、\`f\` / \`t\` / \`l\` / \`g\` 为 flags / tag / layer / group_id
- \`components\`：组件表，键为类型名，元素为 \`i\` / \`v\` / \`d\`
- \`d\`：组件载荷（同 12.14）
- 官方 JSON 值：\`NaN\` / \`Infinity\` 映射为 \`null\`，读回取默认值

覆盖范围：存活实体（含无组件实体）、实体状态全部字段、全部已注册模板组件、运行期 def 组件、元数据与组件版本表；瞬态队列与系统表不在范围内。

### 12.18 安全限制

\`safety_limits\` 控制加载上限，经 \`full_archive::limits()\` 或 \`object_archive::limits\` 修改。

| 字段 | 默认值 | 说明 |
|------|--------|------|
| \`max_file_size\` | 256 MB | 单文件最大字节数 |
| \`max_string_length\` | 16 MB | 单字符串最大长度 |
| \`max_array_elements\` | 10,000,000 | 数组最大元素数 |
| \`max_object_fields\` | 65536 | 对象最大字段数 |
| \`max_depth\` | 64 | JSON 最大嵌套深度 |
| \`max_entity_count\` | 10,000,000 | 最大实体数 |

### 12.19 类型名与引用字段注册

跨编译器时 \`typeid(T).name()\` 不一致，用稳定名保证可移植。

| 接口 | 说明 |
|------|------|
| \`register_type_name<T>(stable_name)\` | 注册类型 T 的稳定名 |
| \`register_type_alias<T>(old_name)\` | 注册旧名别名（旧名 → T） |
| \`lookup_type_name(type_id)\` / \`lookup_type_id(name)\` | 名字 ↔ id 查询 |
| \`register_entity_field<T>(field_name)\` | 注册 entity 引用字段（加载时自动重映射） |
| \`register_enum<T>()\` / \`lookup_enum_underlying(type_id)\` | 枚举底层类型注册 / 查询 |

### 12.20 选择性过滤与单实体保存

| 接口 | 说明 |
|------|------|
| \`serialize_filter::by_layer(l)\` | 仅保留指定 layer |
| \`serialize_filter::by_tag(t)\` | 仅保留指定 tag |
| \`serialize_filter::by_group(g)\` | 仅保留指定 group |
| \`serialize_filter::by_flags(mask, value)\` | \`(flags & mask) == value\` |
| \`serialize_filter::by_entities(ids)\` | 仅保留白名单索引；收窄到单个实体即单实体保存 |

多条件为 AND 关系，经 \`full_archive::set_filter(&filter)\` 应用。

### 12.21 加载模式

| 模式 | 行为 |
|------|------|
| \`load_mode::replace\` | 先 \`manager::clear()\`，再按存档索引保序重建（默认） |
| \`load_mode::append\` | 保留现有实体，追加加载（不保序） |
| \`load_mode::merge\` | 与 append 一致（预留语义扩展） |

### 12.22 存档元数据

\`set_metadata(key, value)\` 存入自定义键值对，保存写入 \`meta\`，加载回填；\`get_metadata(key)\` / \`all_metadata()\` 读取。

### 12.23 组件版本与字段级迁移

| 接口 | 说明 |
|------|------|
| \`register_component_version<T>(version)\` | 注册组件当前版本，保存写入 \`cv\` |
| \`register_migration<T>(from, to, fn)\` | 注册单步迁移函数，加载按 \`from → to\` 链式调用 |
| \`lookup_component_version<T>()\` | 查询组件当前版本 |
| \`register_field_rename<T>(old_name, new_name)\` | 加载时字段改名 |
| \`register_field_drop<T>(field_name)\` | 加载时丢弃字段 |
| \`register_field_default<T>(field_name, default_json)\` | 缺失字段注入默认值 |

迁移函数签名 \`void(*)(json_reader& old, json_writer& neu)\`，需自行调用 \`old.enter_object()\`。存档无 \`cv\` 条目时不迁移。

### 12.24 加载策略

\`strict\`（默认）遇错即失败；\`best_effort\` 跳过损坏组件继续加载，跳过数记入 \`last_stats().skipped_count\`。

### 12.25 校验与加密

保存按 \`校验 → 压缩 → 加密 → on_save\` 依次变换，加载严格逆序。

| 接口 | 说明 |
|------|------|
| \`set_checksum_enabled(b)\` | 启用（默认）/ 关闭 CRC32C 校验，启用后数据前加 8 字节 \`LCCS\` 前缀 |
| \`set_encryption(encrypt_fn, decrypt_fn)\` | 加密 / 解密函数对，\`std::string(*)(const std::string&)\` |

兼容未启用校验的数据：无 \`LCCS\` 前缀时原样处理。需外部工具读取格式标识时，先 \`set_checksum_enabled(false)\`。

### 12.26 压缩与变换钩子

| 接口 | 说明 |
|------|------|
| \`set_compression(compress_fn, decompress_fn)\` | 保存后压缩、加载前解压 |
| \`on_save(cb)\` / \`on_load(cb)\` | 保存后 / 加载前自定义变换，\`void(*)(std::string&)\` |

安全工具提供 \`rle_compress\` / \`rle_decompress\`。

### 12.27 进度回调与统计

| 接口 | 说明 |
|------|------|
| \`set_progress_callback(cb)\` | \`void(*)(size_t current, size_t total)\` |
| \`last_stats()\` | \`serialize_stats\`：\`entity_count\` / \`total_bytes\` / \`archive_version\` / \`skipped_count\` / \`per_type\`（每类型组件数与字节数） |

---

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 未提供 \`to_json\` / \`from_json\`，且未反射、非 trivially copyable | 载荷为空，加载被跳过 | 提供两方法、注册反射或确保 trivially copyable |
| \`to_json\` 与 \`from_json\` 字段不一致 | 数据丢失 | 两端字段保持一致 |
| 保存与加载注册的组件类型或次序不一致 | 组件表字段错位、组件被跳过 | 两端 \`register_components\` 一致 |
| 跨编译器加载未注册稳定名 | 类型名不匹配 | 用 \`register_type_name\` 注册稳定名 |
| 组件含 entity 引用字段未注册 | 引用指向旧索引 | 用 \`register_entity_field<T>(field_name)\` 注册 |
| 加载高于当前版本的存档 | 版本检查失败 | 提高 \`set_archive_version\` 或降级存档 |
| 迁移函数未调用 \`old.enter_object()\` | 读取不到字段 | 在迁移 lambda 开头调用 |
| 启用校验后直接读格式标识 | 前 8 字节为 \`LCCS\` | 先 \`set_checksum_enabled(false)\`，或用 \`detect_codec\` |
| \`replace\` 加载到含信号的 manager | 旧实体销毁触发信号 | 属预期行为，注意信号槽清理 |
| \`create_writer\` / \`create_reader\` 后未 \`destroy_*\` | 内存泄漏 | 用完立即销毁 |
| Protobuf / FlatBuffers 按字段名读取 | 读取返回编号键 | 用 \`f1\` / \`f2\` / ... 迭代 |
| \`read_string_view\` 视图超出源缓冲生命周期 | 悬空指针 | 保留源缓冲区 |

`
};

window.DOCS_DATA['reorder'] = {
  id: 'reorder',
  title: "reorder_group — 多组件重排序遍历",
  category: 'core',
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

