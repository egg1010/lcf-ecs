window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['memory_pool'] = {
  id: 'memory_pool',
  title: "memory_pool — 通用内存池",
  category: 'allocators',
  icon: 'K',
  order: 20,
  content: `## 20. memory_pool — 通用内存池

命名空间 \`memory\`。所有接口 \`noexcept\`。

通用变长内存池，提供分配、软删除、硬删除、重分配、容量管理与统计。

> 线程契约：单线程使用；多线程请各自持有独立实例，不跨线程共享或释放。
>
> 对齐契约：分配返回的地址至少按 16 字节对齐（\`memory::base_alignment\`）。

### memory_stats — 统计信息

本页所有分配器的 \`stats()\` 共用该结构。

| 字段 | 说明 |
|------|------|
| \`reserved_bytes\` | 池当前向系统保留的字节数 |
| \`live_bytes\` | 当前存活分配的可用字节数 |
| \`reusable_bytes\` | 当前可立即复用的空闲字节数 |
| \`peak_live_bytes\` | 存活字节数的历史峰值 |
| \`acquire_count\` | 成功分配次数 |
| \`release_count\` | 成功删除总次数（软删除 + 硬删除） |
| \`soft_delete_count\` | 软删除次数 |
| \`hard_delete_count\` | 硬删除次数 |

### 接口

| 接口 | 说明 |
|------|------|
| \`explicit memory_pool(size_t chunk_size = 4096)\` | 构造；chunk_size 为追加区块的大小提示 |
| \`memory_pool(memory_pool&&)\` | 移动构造 |
| \`operator=(memory_pool&&)\` | 移动赋值 |
| \`swap(memory_pool& other)\` | 交换两个池的状态 |
| \`acquire(size_t bytes)\` | 分配至少 bytes 字节；失败返回 nullptr |
| \`acquire_sized<Size>()\` | 编译期大小分配；Size 必须大于 0 |
| \`acquire_zeroed(size_t bytes)\` | 分配并将请求的前 bytes 字节清零 |
| \`acquire_aligned(size_t bytes, size_t alignment)\` | 按 alignment 对齐分配；alignment 必须为 2 的幂，小于 16 时按 16 处理 |
| \`create<T>(Args&&... args)\` | 分配并构造 T，自动满足 alignof(T)；失败返回 nullptr |
| \`soft_delete(void* ptr)\` | 软删除：回收分配并保留内存供后续复用；非法或重复指针安全忽略 |
| \`hard_delete(void* ptr)\` | 硬删除：回收分配并归还可合并空间；非法或重复指针安全忽略 |
| \`soft_delete_sized<Size>(void* ptr)\` | 带编译期大小提示的软删除；size_of(ptr) < Size 时忽略 |
| \`hard_delete_sized<Size>(void* ptr)\` | 带编译期大小提示的硬删除；size_of(ptr) < Size 时忽略 |
| \`dispose<T>(T* ptr)\` | 仅调用析构函数，不回收内存 |
| \`owns(const void* ptr)\` | 仅存活分配的起始地址返回 true |
| \`size_of(const void* ptr)\` | 返回存活分配的可用字节数；非存活地址返回 0 |
| \`contains(const void* ptr)\` | 判断地址是否落在本池管理的地址范围内 |
| \`stats()\` | 返回 \`memory_stats\` |
| \`empty()\` | 是否没有存活分配 |
| \`free_block_count()\` | 当前空闲块数量 |
| \`chunk_size()\` | 构造时指定的大小提示 |
| \`reset_stats()\` | 清零分配与删除计数，峰值重置为当前存活字节 |
| \`reset()\` | 回收所有存活分配并保留可复用容量，池可继续使用 |
| \`release_all()\` | 回收所有分配并归还全部底层内存；计数与峰值保留 |
| \`increase_capacity(size_t bytes)\` | 扩容到至少 bytes 保留字节；不大于当前保留量时无操作 |
| \`reduce_capacity(size_t target)\` | 缩容到至多 target 保留字节；不移动存活块 |
| \`resize_inplace(void* ptr, size_t old_bytes, size_t new_bytes)\` | 原位重分配；成功返回 true，失败返回 false 且原分配不变 |
| \`resize(void* ptr, size_t old_bytes, size_t new_bytes)\` | 重分配；失败返回 nullptr 且原分配不变 |
| \`iterate_free(Fn&& fn)\` | 遍历可复用空闲块，回调签名 \`void(void* ptr, size_t bytes)\` |

> 注：禁止拷贝；析构等价于 \`release_all()\`。
>
> \`resize\` 约定：\`ptr\` 为 nullptr 时按 \`acquire(new_bytes)\` 处理；\`new_bytes\` 为 0 时软删除并返回 nullptr；无法原位时分配新块、拷贝数据并软删除旧块。过对齐分配发生移动时仍保持原对齐要求。

### 使用

\`\`\`cpp
using namespace memory;

memory_pool pool;

void* p = pool.acquire(64);
pool.soft_delete(p);

void* q = pool.acquire_sized<64>();
pool.hard_delete_sized<64>(q);

void* z = pool.acquire_zeroed(128);  // 前 128 字节为 0
pool.soft_delete(z);

void* a = pool.acquire_aligned(256, 64);
pool.soft_delete(a);

struct Foo { int x; };
Foo* foo = pool.create<Foo>();
if (foo)
{
    pool.dispose(foo);      // 仅析构
    pool.soft_delete(foo);  // 再回收内存
}

// 重分配
void* buf = pool.acquire(32);
void* bigger = pool.resize(buf, 32, 256);
if (bigger)
{
    buf = bigger;
}

// 统计
memory_stats s = pool.stats();
size_t live = s.live_bytes;
size_t reusable = s.reusable_bytes;

// 容量管理
pool.increase_capacity(1024 * 1024);
pool.reduce_capacity(64 * 1024);

// 遍历可复用空闲块
pool.iterate_free([](void* ptr, size_t bytes) {
    (void)ptr;
    (void)bytes;
});

// 整体回收
pool.release_all();
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用 \`delete\` 释放 \`create\` 构造的对象 | 池内存不能用 \`delete\` 回收 | 先 \`dispose\` 再 \`soft_delete\` 或 \`hard_delete\` |
| \`acquire\` 后忘记删除 | 内存泄漏 | 每次分配配对一个删除 |
| 拷贝 \`memory_pool\` | 禁止拷贝 | 使用移动语义或引用传递 |
| 用非起始地址调用 \`owns\` / \`size_of\` | 仅存活分配的起始地址可识别 | 保存并使用分配返回的原指针 |
| 对同一指针重复删除 | 重复删除被安全忽略，但通常意味着生命周期管理有误 | 删除后置空 |
| 在 \`iterate_free\` 回调中分配或删除 | 遍历期间池状态不应改变 | 回调内只读取信息 |
| 认为 \`contains\` 等价于 \`owns\` | \`contains\` 是地址范围查询 | 判断存活分配用 \`owns\` |

---

`
};

window.DOCS_DATA['list_memory_pool'] = {
  id: 'list_memory_pool',
  title: "list_memory_pool — 链式内存池",
  category: 'allocators',
  icon: 'J',
  order: 21,
  content: `## 21. list_memory_pool — 链式内存池

命名空间 \`memory\`。所有接口 \`noexcept\`。

支持软删除与硬删除的变长内存池。软删除保留内存，并保证同大小档的后续请求优先复用该块。

> 线程契约：单线程使用；多线程请各自持有独立实例，不跨线程共享或释放。
>
> 对齐契约：分配返回的地址至少按 16 字节对齐（\`memory::base_alignment\`）。

### 接口

| 接口 | 说明 |
|------|------|
| \`list_memory_pool()\` | 默认构造 |
| \`list_memory_pool(list_memory_pool&&)\` | 移动构造 |
| \`operator=(list_memory_pool&&)\` | 移动赋值 |
| \`swap(list_memory_pool& other)\` | 交换两个池的状态 |
| \`acquire(size_t bytes)\` | 分配至少 bytes 字节；失败返回 nullptr |
| \`acquire_sized<Size>()\` | 编译期大小分配；Size 必须大于 0 |
| \`acquire_zeroed(size_t bytes)\` | 分配并将请求的前 bytes 字节清零 |
| \`acquire_aligned(size_t bytes, size_t alignment)\` | 按 alignment 对齐分配；alignment 必须为 2 的幂，小于 16 时按 16 处理 |
| \`create<T>(Args&&... args)\` | 分配并构造 T，自动满足 alignof(T)；失败返回 nullptr |
| \`soft_delete(void* ptr)\` | 软删除：保留内存供同大小档复用；非法或重复指针安全忽略 |
| \`hard_delete(void* ptr)\` | 硬删除：立即回收分配；非法或重复指针安全忽略 |
| \`soft_delete_sized<Size>(void* ptr)\` | 带编译期大小提示的软删除；size_of(ptr) < Size 时忽略 |
| \`hard_delete_sized<Size>(void* ptr)\` | 带编译期大小提示的硬删除；size_of(ptr) < Size 时忽略 |
| \`dispose<T>(T* ptr)\` | 仅调用析构函数，不回收内存 |
| \`owns(const void* ptr)\` | 仅存活分配的起始地址返回 true |
| \`size_of(const void* ptr)\` | 返回存活分配的可用容量，可能大于请求值；非存活地址返回 0 |
| \`contains(const void* ptr)\` | 判断地址是否落在本池管理的地址范围内 |
| \`stats()\` | 返回 \`memory_stats\` |
| \`empty()\` | 是否没有存活分配 |
| \`reset_stats()\` | 清零分配与删除计数，峰值重置为当前存活字节 |
| \`reset()\` | 回收所有存活分配并保留可复用容量，池可继续使用 |
| \`release_all()\` | 回收所有分配并归还全部底层内存；计数与峰值保留 |
| \`increase_capacity(size_t bytes)\` | 扩容到至少 bytes 保留字节；不大于当前保留量时无操作 |
| \`reduce_capacity(size_t target)\` | 缩容到至多 target 保留字节；优先归还未使用的保留区域 |
| \`resize_inplace(void* ptr, size_t old_bytes, size_t new_bytes)\` | 原位重分配；当前块容量满足请求时返回 true |
| \`resize(void* ptr, size_t old_bytes, size_t new_bytes)\` | 重分配；失败返回 nullptr 且原分配不变 |
| \`iterate_free(Fn&& fn)\` | 遍历可复用块，回调签名 \`void(void* ptr, size_t bytes)\` |

> 注：禁止拷贝；析构等价于 \`release_all()\`。
>
> \`resize\` 约定：\`ptr\` 为 nullptr 时按 \`acquire(new_bytes)\` 处理；\`new_bytes\` 为 0 时软删除并返回 nullptr；无法原位时分配新块、拷贝数据并软删除旧块。过对齐分配发生移动时仍保持原对齐要求。

### 使用

\`\`\`cpp
using namespace memory;

list_memory_pool pool;

void* p = pool.acquire(64);
void* q = pool.acquire(8192);

// 软删除: 保留内存, 同大小档请求优先复用
pool.soft_delete(p);
void* r = pool.acquire(64);

// 硬删除: 立即回收
pool.hard_delete(q);

// 编译期大小
void* s = pool.acquire_sized<128>();
pool.soft_delete_sized<128>(s);

// 对齐分配
void* a = pool.acquire_aligned(256, 64);
pool.hard_delete(a);

// 超对齐对象
struct alignas(64) Wide { unsigned char data[64]; };
Wide* w = pool.create<Wide>();
if (w)
{
    pool.dispose(w);
    pool.soft_delete(w);
}

// 任意指针可安全查询
int local = 0;
bool mine = pool.owns(&local);       // false
bool inside = pool.contains(&local); // false

// 统计与容量管理
memory_stats st = pool.stats();
pool.increase_capacity(1024 * 1024);
pool.reduce_capacity(64 * 1024);

// 重分配
void* buf = pool.acquire(128);
void* bigger = pool.resize(buf, 128, 4096);
if (bigger)
{
    buf = bigger;
}

// 整体回收
pool.release_all();
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 期望 \`soft_delete\` 立即把内存归还系统 | 软删除保留内存供复用 | 需要归还时用 \`hard_delete\`、\`reduce_capacity\` 或 \`release_all\` |
| 用非起始地址调用 \`owns\` / \`size_of\` | 仅存活分配的起始地址可识别 | 保存并使用分配返回的原指针 |
| 对同一指针重复删除 | 重复删除被安全忽略，但通常意味着生命周期管理有误 | 删除后置空 |
| 用本池接口删除非本池分配的指针 | 非本池指针会被安全忽略，但释放意图有误 | 先 \`owns(ptr)\` 验证 |
| 认为 \`contains\` 等价于 \`owns\` | 硬删除后地址仍可能落在本池范围内 | 判断存活分配用 \`owns\` |
| 拷贝 \`list_memory_pool\` | 禁止拷贝 | 使用移动语义或引用传递 |
| 在 \`iterate_free\` 回调中分配或删除 | 遍历期间池状态不应改变 | 回调内只读取信息 |

---

`
};

window.DOCS_DATA['arena_allocator'] = {
  id: 'arena_allocator',
  title: "arena_allocator — 线性分配器",
  category: 'allocators',
  icon: 'B',
  order: 26,
  content: `## 26. arena_allocator — 线性分配器

命名空间 \`memory\`。所有接口 \`noexcept\`。

线性分配器，没有单个删除接口，只能通过 \`restore\` / \`reset\` 整体回收。支持自有内存与借用外部 buffer 两种模式。

> 线程契约：单线程使用；多线程请各自持有独立实例，不跨线程共享或释放。
>
> 对齐契约：\`acquire\` 返回的地址至少按 16 字节对齐（\`memory::base_alignment\`）。

### 接口

| 接口 | 说明 |
|------|------|
| \`arena_allocator()\` | 默认构造，空状态，不能分配 |
| \`explicit arena_allocator(size_t capacity)\` | 自有模式：分配 capacity 字节 |
| \`arena_allocator(void* buffer, size_t size)\` | 借用模式：使用外部 buffer，不持有所有权 |
| \`arena_allocator(arena_allocator&&)\` | 移动构造 |
| \`operator=(arena_allocator&&)\` | 移动赋值 |
| \`swap(arena_allocator& other)\` | 交换两个分配器的状态 |
| \`acquire(size_t bytes)\` | 分配至少 bytes 字节；空间不足或参数非法返回 nullptr |
| \`acquire_zeroed(size_t bytes)\` | 分配并将前 bytes 字节清零 |
| \`acquire_aligned(size_t bytes, size_t alignment)\` | 按 alignment 对齐分配；alignment 必须为 2 的幂，小于 16 时按 16 处理 |
| \`create<T>(Args&&... args)\` | 分配并构造 T，自动满足 alignof(T)；失败返回 nullptr |
| \`create_array<T>(size_t count, Args&&... args)\` | 使用同一组参数构造 count 个 T；失败返回 nullptr |
| \`dispose<T>(T* ptr)\` | 仅调用析构函数，不回收内存 |
| \`dispose_array<T>(T* ptr, size_t count)\` | 仅逆序调用 count 个元素的析构函数 |
| \`owns(const void* ptr)\` | 仅存活分配的起始地址返回 true |
| \`stats()\` | 返回 \`memory_stats\`；删除计数恒为 0 |
| \`empty()\` | 是否没有存活分配 |
| \`reset_stats()\` | 清零分配计数，峰值重置为当前存活字节 |
| \`save_point()\` | 保存当前分配点，返回可用于 \`restore\` 的值 |
| \`restore(size_t point)\` | 恢复到指定分配点；point 大于已使用字节或截断存活分配时无操作 |
| \`reset()\` | 整体回收，不调用析构，不归还底层内存 |
| \`used()\` | 已使用的字节数 |
| \`capacity()\` | 总容量字节数 |
| \`remaining()\` | 剩余可用字节数 |

> 注：禁止拷贝；没有单个删除接口；借用模式的 buffer 由调用方管理生命周期。

### 使用

\`\`\`cpp
using namespace memory;

// 自有模式
arena_allocator ar(4096);
void* p = ar.acquire(128);
void* q = ar.acquire_aligned(64, 64);
void* z = ar.acquire_zeroed(32);
ar.reset();

// 借用模式
alignas(64) unsigned char buffer[2048];
arena_allocator borrowed(buffer, sizeof(buffer));
void* b = borrowed.acquire(100);

// 保存与恢复分配点
arena_allocator arena(8192);
const size_t mark = arena.save_point();
void* tmp = arena.acquire(256);
arena.restore(mark);

// 作用域守卫: 离开作用域自动恢复分配点
{
    arena_scope scope(arena);
    void* scoped = arena.acquire(128);
    (void)scoped;
}

// 对象与数组
struct Foo { int x; Foo(int v) : x(v) {} };
Foo* foo = arena.create<Foo>(7);
if (foo)
{
    arena.dispose(foo);
}

Foo* arr = arena.create_array<Foo>(3, 1);
if (arr)
{
    arena.dispose_array(arr, 3);
}

// 统计
memory_stats st = arena.stats();
size_t used = arena.used();
size_t remaining = arena.remaining();

// 双帧交替
double_buffered_arena dba(65536);
void* frame_data = dba.current().acquire(64);
dba.flip();
// frame_data 属于 dba.previous(), 本帧仍可读取
\`\`\`

### arena_scope — 作用域守卫

| 接口 | 说明 |
|------|------|
| \`explicit arena_scope(arena_allocator& a)\` | 构造时保存分配点 |
| \`~arena_scope()\` | 析构时恢复保存的分配点 |

> 注：禁止拷贝。

### double_buffered_arena — 双帧交替

| 接口 | 说明 |
|------|------|
| \`explicit double_buffered_arena(size_t capacity_per_frame)\` | 构造，每帧容量为 capacity_per_frame |
| \`flip()\` | 切换当前帧，并重置新的当前帧 |
| \`current()\` | 当前帧（写入侧） |
| \`previous()\` | 上一帧（读取侧） |

> 注：两帧容量相同；\`flip()\` 后上一帧数据仍可读取。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对单个指针调用释放接口 | 线性分配器不提供单个删除 | 用 \`restore\` / \`reset\` 整体回收 |
| \`reset\` 或 \`restore\` 后继续使用旧指针 | 内存已被回收，指针悬空 | 回收后重新分配 |
| 把任意数值传给 \`restore\` | 会截断存活分配的值被忽略，容易误判回收结果 | 保存并使用 \`save_point()\` 的结果 |
| 借用模式析构后期望释放 buffer | 借用模式不持有所有权 | 由 buffer 所有者管理生命周期 |
| 期望 \`stats()\` 的删除计数增长 | 线性分配器没有单个删除 | 使用 \`live_bytes\` / \`reusable_bytes\` 观察状态 |

---

`
};

window.DOCS_DATA['slab_allocator'] = {
  id: 'slab_allocator',
  title: "slab_allocator — 固定块分配器",
  category: 'allocators',
  icon: 'L',
  order: 27,
  content: `## 27. slab_allocator — 固定块分配器

命名空间 \`memory\`。所有接口 \`noexcept\`。

所有分配使用同一固定块大小。软删除与硬删除等价，块始终保留在分配器内供复用。

> 线程契约：单线程使用；多线程请各自持有独立实例，不跨线程共享或释放。
>
> 对齐契约：块起始地址按对齐参数对齐，对齐参数不小于 16 字节。

### 接口

| 接口 | 说明 |
|------|------|
| \`explicit slab_allocator(size_t block_size, size_t alignment = 16, size_t blocks_per_chunk = 256)\` | 构造；block_size 向上对齐到 alignment；alignment 小于 16 或非 2 的幂时按 16 处理 |
| \`slab_allocator(slab_allocator&&)\` | 移动构造 |
| \`operator=(slab_allocator&&)\` | 移动赋值 |
| \`swap(slab_allocator& other)\` | 交换两个分配器的状态 |
| \`acquire()\` | 分配一个块；失败返回 nullptr |
| \`acquire_sized<Size>()\` | 编译期大小分配；Size 大于块大小时返回 nullptr |
| \`acquire_zeroed()\` | 分配一个块并清零 |
| \`create<T>(Args&&... args)\` | 分配并构造 T；sizeof(T) 或 alignof(T) 超出块能力时返回 nullptr |
| \`soft_delete(void* ptr)\` | 软删除：回收块供复用；非法或重复指针安全忽略 |
| \`hard_delete(void* ptr)\` | 硬删除：与 \`soft_delete\` 等价 |
| \`soft_delete_sized<Size>(void* ptr)\` | 带编译期大小提示的软删除；Size 大于块大小时忽略 |
| \`hard_delete_sized<Size>(void* ptr)\` | 带编译期大小提示的硬删除；Size 大于块大小时忽略 |
| \`dispose<T>(T* ptr)\` | 仅调用析构函数，不回收内存 |
| \`owns(const void* ptr)\` | 仅存活块的起始地址返回 true |
| \`size_of(const void* ptr)\` | 返回块大小；非存活地址返回 0 |
| \`stats()\` | 返回 \`memory_stats\` |
| \`empty()\` | 是否全部块空闲 |
| \`reset_stats()\` | 清零分配与删除计数，峰值重置为当前存活字节 |
| \`reset()\` | 所有块回到空闲状态，不归还底层内存 |
| \`release_all()\` | 释放全部底层内存；计数与峰值保留 |
| \`block_size()\` | 实际块大小（对齐后） |
| \`total_blocks()\` | 当前总块数 |
| \`free_blocks()\` | 当前空闲块数 |

> 注：禁止拷贝；不支持变长分配；block_size 为 0 时分配器不可用，分配返回 nullptr；析构等价于 \`release_all()\`。

### 使用

\`\`\`cpp
using namespace memory;

slab_allocator slab(64);

void* p = slab.acquire();
void* q = slab.acquire_zeroed();
slab.soft_delete(p);
slab.hard_delete(q);  // 与 soft_delete 等价

void* s = slab.acquire_sized<48>();  // 48 <= block_size
slab.soft_delete_sized<48>(s);

struct Foo { int x; Foo(int v) : x(v) {} };
Foo* foo = slab.create<Foo>(42);
if (foo)
{
    slab.dispose(foo);
    slab.soft_delete(foo);
}

// 统计与池信息
memory_stats st = slab.stats();
size_t total = slab.total_blocks();
size_t free_count = slab.free_blocks();
size_t block = slab.block_size();

// 整体回收
slab.reset();        // 保留底层内存
slab.release_all();  // 归还底层内存
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用 \`acquire_sized<Size>\` 请求大于块大小的对象 | 返回 nullptr，不会分配 | 使用足够大的 block_size 或改用变长池 |
| 用 \`create<T>\` 构造超出块能力的类型 | sizeof/alignof 超限时返回 nullptr | 检查返回值或增大 block_size |
| 在同一分配器中混用不同大小的对象 | 块大小固定，不代表请求大小 | 每种块大小使用独立 \`slab_allocator\` |
| 用本分配器删除非本分配器的指针 | 会被安全忽略，但释放意图有误 | 先 \`owns(p)\` 验证 |
| 对同一指针重复删除 | 重复删除被安全忽略，但通常意味着生命周期管理有误 | 删除后置空 |
| 期望软删除归还内存给系统 | 软删除与硬删除都只把块放回空闲状态 | 需要归还时用 \`release_all\` |
| 拷贝 \`slab_allocator\` | 禁止拷贝 | 使用移动语义或引用传递 |

---

`
};

window.DOCS_DATA['layered_allocator'] = {
  id: 'layered_allocator',
  title: "layered_allocator — 分层分配器",
  category: 'allocators',
  icon: 'Y',
  order: 28,
  content: `## 28. layered_allocator — 分层分配器

命名空间 \`memory\`。所有接口 \`noexcept\`。

按请求大小自动选择定长块或变长块路径的分层分配器，统一提供分配、删除、重分配与统计接口。

> 线程契约：单线程使用；多线程请各自持有独立实例，不跨线程共享或释放。
>
> 对齐契约：分配返回的地址至少按 16 字节对齐（\`memory::base_alignment\`）。

### 接口

| 接口 | 说明 |
|------|------|
| \`layered_allocator()\` | 默认构造 |
| \`layered_allocator(layered_allocator&&)\` | 移动构造 |
| \`operator=(layered_allocator&&)\` | 移动赋值 |
| \`acquire(size_t bytes)\` | 分配至少 bytes 字节；失败返回 nullptr |
| \`acquire_sized<Size>()\` | 编译期大小分配；Size 必须大于 0 |
| \`acquire_zeroed(size_t bytes)\` | 分配并将请求的前 bytes 字节清零 |
| \`acquire_aligned(size_t bytes, size_t alignment)\` | 按 alignment 对齐分配；alignment 必须为 2 的幂 |
| \`create<T>(Args&&... args)\` | 分配并构造 T，自动满足 alignof(T)；失败返回 nullptr |
| \`create_array<T>(size_t count, Args&&... args)\` | 使用同一组参数构造 count 个 T；失败返回 nullptr |
| \`soft_delete(void* ptr)\` | 软删除：按指针归属回收，保留内存供复用；非法或重复指针安全忽略 |
| \`hard_delete(void* ptr)\` | 硬删除：按指针归属回收；非法或重复指针安全忽略 |
| \`soft_delete_sized<Size>(void* ptr)\` | 带编译期大小提示的软删除；size_of(ptr) < Size 时忽略 |
| \`hard_delete_sized<Size>(void* ptr)\` | 带编译期大小提示的硬删除；size_of(ptr) < Size 时忽略 |
| \`dispose<T>(T* ptr)\` | 仅调用析构函数，不回收内存 |
| \`dispose_array<T>(T* ptr, size_t count)\` | 仅逆序调用 count 个元素的析构函数 |
| \`owns(const void* ptr)\` | 仅存活分配的起始地址返回 true |
| \`size_of(const void* ptr)\` | 返回存活分配的可用容量；非存活地址返回 0 |
| \`stats()\` | 返回 \`memory_stats\`（各路径合计） |
| \`empty()\` | 是否没有存活分配 |
| \`reset_stats()\` | 清零分配与删除计数，峰值重置为当前存活字节 |
| \`reset()\` | 回收所有存活分配并保留可复用容量，分配器可继续使用 |
| \`release_all()\` | 回收所有分配并归还全部底层内存；计数与峰值保留 |
| \`resize_inplace(void* ptr, size_t old_bytes, size_t new_bytes)\` | 原位重分配；当前块容量满足请求时返回 true |
| \`resize(void* ptr, size_t old_bytes, size_t new_bytes)\` | 重分配；失败返回 nullptr 且原分配不变 |
| \`slab_max()\` | 定长块路径支持的最大请求字节数 |
| \`big_pool()\` | 只读访问变长块路径，返回 \`const list_memory_pool&\` |
| \`slab(size_t i)\` | 只读访问定长块路径，返回 \`const slab_allocator&\`，i 取值 0..3 |

> 注：禁止拷贝；\`create_array\` 需配对 \`dispose_array\` 与 \`soft_delete\`/\`hard_delete\`；析构等价于 \`release_all()\`。
>
> \`resize\` 约定：\`ptr\` 为 nullptr 时按 \`acquire(new_bytes)\` 处理；\`new_bytes\` 为 0 时软删除并返回 nullptr；无法原位时分配新块、拷贝数据并软删除旧块。过对齐分配发生移动时仍保持原对齐要求。

### 使用

\`\`\`cpp
using namespace memory;

layered_allocator alloc;

void* small = alloc.acquire(64);
void* big = alloc.acquire(8192);
alloc.soft_delete(small);
alloc.hard_delete(big);

void* s = alloc.acquire_sized<64>();
alloc.soft_delete_sized<64>(s);

void* a = alloc.acquire_aligned(512, 64);
alloc.hard_delete(a);

struct Foo { int x; Foo(int v) : x(v) {} };
Foo* foo = alloc.create<Foo>(1);
if (foo)
{
    alloc.dispose(foo);
    alloc.soft_delete(foo);
}

Foo* arr = alloc.create_array<Foo>(4, 2);
if (arr)
{
    alloc.dispose_array(arr, 4);
    alloc.soft_delete(arr);
}

// 重分配
void* buf = alloc.acquire(128);
void* bigger = alloc.resize(buf, 128, 4096);
if (bigger)
{
    buf = bigger;
}

// 统计与只读路径查询
memory_stats st = alloc.stats();
size_t limit = alloc.slab_max();
const list_memory_pool& pool = alloc.big_pool();
const slab_allocator& block_pool = alloc.slab(0);
memory_stats pool_stats = pool.stats();
size_t block_size = block_pool.block_size();

// 整体回收
alloc.release_all();
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用本分配器删除非本分配器分配的指针 | 会被安全忽略，但释放意图有误 | 先 \`owns(ptr)\` 验证 |
| 对同一指针重复删除 | 重复删除被安全忽略，但通常意味着生命周期管理有误 | 删除后置空 |
| 只对 \`create_array\` 的元素调用 \`dispose_array\` | 元素已析构，但内存仍未回收 | \`dispose_array\` 后再 \`soft_delete\` 或 \`hard_delete\` |
| 忽略 \`resize\` 的失败返回值 | 失败时原分配仍有效，继续使用新指针会出错 | 检查返回值，失败时保留原指针 |
| 拷贝 \`layered_allocator\` | 禁止拷贝 | 使用移动语义或引用传递 |

---

`
};

window.DOCS_DATA['stl_allocator'] = {
  id: 'stl_allocator',
  title: "stl_allocator — STL 适配器",
  category: 'allocators',
  icon: 'S',
  order: 29,
  content: `## 29. stl_allocator — STL Allocator 适配器

命名空间 \`memory\`。

将项目内存池适配为 C++ STL Allocator，可用于 \`std::vector\` / \`std::list\` 等标准容器。

> 线程契约：单线程使用，不跨线程共享容器。

### 接口

| 接口 | 说明 |
|------|------|
| \`stl_allocator()\` | 默认构造，未绑定池；分配会触发 \`handle_oom\` |
| \`explicit stl_allocator(Pool& pool)\` | 绑定到指定内存池 |
| \`stl_allocator(const stl_allocator<U, Pool>& other)\` | 转换构造（跨类型 U → T） |
| \`allocate(size_t n)\` | 分配 n 个 T 的内存并满足 alignof(T)；n 为 0 返回 nullptr，其余失败调用 \`handle_oom\` |
| \`deallocate(T* p, size_t n)\` | 软删除 n 个 T 的内存（n 不参与回收） |
| \`pool()\` | 获取绑定的内存池指针 |
| \`rebind<U>::other\` | 重绑定类型 U 的 allocator 类型 |
| \`address(T& x)\` / \`address(const T& x)\` | 取对象地址 |
| \`max_size()\` | 最大可分配元素数 |
| \`construct<U>(U* p, Args&&... args)\` | 在 p 处构造 U 对象 |
| \`destroy<U>(U* p)\` | 析构 p 指向的对象 |
| \`operator==\` / \`operator!=\` | 比较两个分配器是否等价 |

> 注：同一内存池上的 \`stl_allocator\` 实例互相等价；不同内存池上的实例不等价。容器生命周期不得超过绑定的内存池。

### 使用

\`\`\`cpp
using namespace memory;

layered_allocator pool;

// 用于 std::vector
using vec_alloc = stl_allocator<int, layered_allocator>;
std::vector<int, vec_alloc> v{vec_alloc(pool)};
v.push_back(1);
v.push_back(2);
v.push_back(3);

// 用于 std::list
using list_alloc = stl_allocator<int, layered_allocator>;
std::list<int, list_alloc> l{list_alloc(pool)};
l.push_back(10);
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 使用默认构造的 \`stl_allocator\` 分配 | 未绑定池，触发 \`handle_oom\` | 用 \`stl_allocator(pool)\` 绑定池 |
| 容器比绑定的内存池存活更久 | 池析构后容器仍持有其内存 | 先销毁容器，再销毁池 |
| 手动调用 \`deallocate\` 释放非该容器分配的内存 | 生命周期管理错误 | 交给容器管理分配与释放 |

---

`
};

window.DOCS_DATA['oom_handler'] = {
  id: 'oom_handler',
  title: "oom_handler — OOM 处理",
  category: 'allocators',
  icon: 'O',
  order: 30,
  content: `## 30. oom_handler — 内存分配失败处理

命名空间 \`memory\`。

内存分配失败时统一调用 \`handle_oom\` 输出诊断信息后终止程序。

### 接口

| 接口 | 说明 |
|------|------|
| \`handle_oom(size_t requested_bytes, const char* allocator_name, std::source_location loc = std::source_location::current())\` | 输出 OOM 诊断后终止程序；\`allocator_name\` 可为 nullptr |

> 注：\`handle_oom\` 标记为 \`[[noreturn]]\`，调用后不会返回。

### 使用

\`\`\`cpp
using namespace memory;

void* custom_alloc(size_t n)
{
    void* p = std::malloc(n);
    if (!p)
    {
        handle_oom(n, "custom_alloc");
    }
    return p;
}

// 包装函数可透传调用处的位置信息
void* checked_alloc(size_t n,
    std::source_location loc = std::source_location::current())
{
    void* p = std::malloc(n);
    if (!p)
    {
        handle_oom(n, "checked_alloc", loc);
    }
    return p;
}
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 期望 \`handle_oom\` 返回后继续执行 | 调用后程序终止 | 在调用前完成必要的状态清理 |
| 对可恢复的分配失败也调用 \`handle_oom\` | 会直接终止程序 | 可恢复场景检查分配接口的 nullptr 返回值 |

---

`
};
