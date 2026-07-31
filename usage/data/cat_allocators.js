window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['memory_pool'] = {
  id: 'memory_pool',
  title: "memory_pool — 内存池",
  category: 'allocators',
  icon: 'K',
  order: 20,
  content: `## 19. memory_pool — 内存池

基于 TLSF（Two-Level Segregated Fit）算法的分桶式内存池，减少频繁 malloc/free 开销。内部维护 chunk 预分配池，\`reset()\` 和 \`reduce_capacity()\` 释放的 chunk 优先归还预分配池，后续 \`allocate\` 优先从池中取用，减少系统调用。预分配池容量有限，超限的 chunk 归还系统。

### 内存布局

每个块由 16 字节 \`block_header\` + 用户数据区组成：

| 字段 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| \`block_header.size_\` | 0 | 8 | 块大小（低 1 位为 in_use 标志） |
| \`block_header.prev_physical_\` | 8 | 8 | 物理前驱块指针（用于合并） |
| 用户数据区 | 16 | size | \`allocate\` 返回的指针 |

空闲块的数据区前 16 字节复用为链表节点（\`free_node\`：\`next_\`/\`prev_\`），不额外占空间。\`allocate(16)\` 实际占用 32 字节（header 16 + 数据 16），小对象利用率高。

### memory_block — 内存块

| 接口 | 说明 |
|------|------|
| \`memory_block()\` | 默认构造，空块 |
| \`memory_block(uint8_t* data, size_t size)\` | 指定数据和大小构造 |
| \`memory_block(memory_block&&)\` | 移动构造 |
| \`operator=(memory_block&&)\` | 移动赋值 |
| \`data_\` | 数据指针（\`uint8_t*\`） |
| \`size_\` | 数据大小（\`size_t\`） |

> 注：\`memory_block\` 禁止拷贝。

### pool_stats — 统计信息

| 字段 | 说明 |
|------|------|
| \`total_allocated\` | 已分配 chunk 总量 |
| \`total_used\` | 用户使用量（含 header） |
| \`total_free\` | 空闲量（含 header） |
| \`free_block_count\` | 空闲块数量 |
| \`max_contiguous_free\` | 最大连续空闲块大小 |
| \`fragmentation\` | 碎片率 [0,1]，\`1 - max_contiguous_free / total_free\` |

### memory_pool — 内存池

| 接口 | 说明 |
|------|------|
| \`memory_pool(size_t chunk_size = 4096)\` | 构造，指定块大小 |
| \`memory_pool(memory_pool&&)\` | 移动构造 |
| \`operator=(memory_pool&&)\` | 移动赋值 |
| \`allocate(size_t size)\` | 分配内存，返回 16 字节对齐指针 |
| \`allocate_sized<Size>()\` | 模板化分配，Size 为编译期大小，小块走快路径 |
| \`deallocate(void* ptr)\` | 释放内存（自动合并相邻块） |
| \`deallocate(void* ptr, size_t size)\` | 释放内存（size 必须与 allocate 一致） |
| \`deallocate_sized<Size>(void* ptr)\` | 模板化释放，Size 为编译期大小，与 \`allocate_sized<Size>()\` 配对 |
| \`construct<T>(Args...)\` | 分配并构造对象（内部走 \`allocate_sized<sizeof(T)>()\`） |
| \`destroy<T>(T* ptr)\` | 析构并释放对象（内部走 \`deallocate_sized<sizeof(T)>(ptr)\`） |
| \`total_allocated()\` | 已分配总量 |
| \`total_used()\` | 已使用量 |
| \`chunk_size()\` | 获取块大小 |
| \`empty()\` | 是否空闲（\`total_used_ == 0\`） |
| \`owns(const void* ptr)\` | 判断指针是否属于本池 |
| \`stats()\` | 返回 \`pool_stats\` 统计信息 |
| \`iterate_free(Fn&& fn)\` | 遍历空闲块，回调签名 \`void(void* data_ptr, size_t block_size)\` |
| \`increase_capacity(size_t size)\` | 扩容（只扩容不缩容） |
| \`reduce_capacity(size_t target)\` | 缩容（只缩容不扩容，释放空闲 chunk 直到总量 <= target） |
| \`reset()\` | 释放所有内存块，chunk 归还预分配池（不归还系统），回到初始状态 |

> 注：\`memory_pool\` 禁止拷贝。

### 使用

\`\`\`cpp
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
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用 \`new\`/\`delete\` 管理 \`construct\` 分配的对象 | 内存池有自己的分配器，\`delete\` 会崩溃 | 始终用 \`destroy<T>()\` 释放 |
| \`allocate\` 后忘记 \`deallocate\` | 内存泄漏 | 每次 \`allocate\` 配对一个 \`deallocate\` |
| 拷贝 \`memory_pool\` | 禁止拷贝，内部指针所有权混乱 | 使用移动语义或引用传递 |
| 在 \`iterate_free\` 回调中修改池状态 | 遍历中增删块会破坏链表 | 仅在回调中读取信息，不调用 \`allocate\`/\`deallocate\` |
| 依赖 \`owns()\` 区分池内不同块 | \`owns\` 仅判断指针是否属于本池，不区分具体块 | 用 \`stats()\`/\`iterate_free()\` 获取块信息 |
| 期望 \`reset()\` 立即归还内存给系统 | chunk 优先进入预分配池，析构时才归还系统 | 如需立即归还，销毁 pool 对象或用新对象替换 |

### arena_allocator — 线性 bump 分配器

线性分配器，无单个 deallocate，仅 \`reset()\` 整体回收。两种模式：自有内存（析构释放）/ 借用外部 buffer（不持有所有权）。适合 command_buffer 等批量分配、整体回收场景。

#### 接口

| 接口 | 说明 |
|------|------|
| \`arena_allocator()\` | 默认构造，空 |
| \`arena_allocator(size_t capacity)\` | 自有模式：分配 capacity 字节，析构释放 |
| \`arena_allocator(void* buffer, size_t size)\` | 借用模式：使用外部 buffer，不分配不释放 |
| \`arena_allocator(arena_allocator&&)\` | 移动构造（原对象置空防 double free） |
| \`operator=(arena_allocator&&)\` | 移动赋值 |
| \`allocate(size_t n, size_t align = 16)\` | 分配内存，对齐上限 64，溢出返回 nullptr |
| \`reset()\` | 整体回收，offset 归零（不调用析构） |
| \`used()\` | 已用字节数 |
| \`capacity()\` | 总容量 |
| \`remaining()\` | 剩余字节数 |
| \`empty()\` | 是否已 reset（offset == 0） |
| \`owns(const void* p)\` | 判断指针是否属于本 arena |

> 注：禁止拷贝；无单个 deallocate，必须 \`reset()\` 整体回收。

#### 使用

\`\`\`cpp
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
\`\`\`

#### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对单个指针调用 \`deallocate\` | arena 无此接口 | 仅用 \`reset()\` 整体回收 |
| \`reset()\` 后继续使用已分配指针 | 内存被整体回收，指针悬空 | \`reset()\` 后重新分配 |
| 借用模式析构后期望释放 buffer | 借用模式零所有权，不释放 | 由 buffer 所有者管理生命周期 |
| 期望 \`allocate(n, 128)\` 对齐 | 对齐上限 64 | 对齐上限 64，超过不保证 |

### slab_allocator — 固定块对象池

固定块大小对象池，O(1) 分配/释放。适合 void_any 小对象（≤128B）高频分配/释放。

#### 接口

| 接口 | 说明 |
|------|------|
| \`slab_allocator(size_t block_size, size_t alignment = 16, size_t blocks_per_chunk = 256)\` | 构造，block_size 向上对齐到 alignment |
| \`slab_allocator(slab_allocator&&)\` | 移动构造 |
| \`operator=(slab_allocator&&)\` | 移动赋值 |
| \`allocate()\` | 分配一个块，无空闲块则自动扩容 |
| \`deallocate(void* p)\` | 释放块 |
| \`owns(const void* p)\` | 判断指针是否属于本分配器 |
| \`block_size()\` | 实际块大小（对齐后） |
| \`total_blocks()\` | 总块数 |
| \`free_blocks()\` | 空闲块数 |
| \`empty()\` | 是否全部空闲（free == total） |

> 注：禁止拷贝；块大小固定，不支持变长分配。

#### 使用

\`\`\`cpp
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
\`\`\`

#### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`deallocate\` 非 slab 分配的指针 | 写入非法内存，破坏 free list | 仅释放 \`allocate()\` 返回的指针 |
| 同一指针 \`deallocate\` 两次 | double free，破坏 free list | 释放后置空，避免重复释放 |
| 期望块大小可变 | slab 固定块大小 | 变长用 \`layered_allocator\` 或 \`memory_pool\` |

### layered_allocator — 分层分配器

组合 slab + TLSF，按大小路由：小对象（≤128B）走 slab（0 header, O(1)），大对象走 memory_pool（TLSF）。void_any 默认通过 \`VOID_ANY_USE_LAYERED_ALLOCATOR\` 启用此分配器。

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

\`deallocate\` 遍历 8 个 slab 的 \`owns()\` 判断归属，未命中走 \`memory_pool.deallocate\`。

#### 接口

| 接口 | 说明 |
|------|------|
| \`layered_allocator()\` | 默认构造，初始化 8 个 slab + memory_pool |
| \`layered_allocator(layered_allocator&&)\` | 移动构造 |
| \`operator=(layered_allocator&&)\` | 移动赋值 |
| \`allocate(size_t n)\` | 按大小路由：≤128 走 slab，>128 走 TLSF |
| \`deallocate(void* p)\` | 遍历 \`owns()\` 判断归属后释放 |
| \`construct<T>(Args...)\` | 分配并构造对象 |
| \`destroy<T>(T* ptr)\` | 析构并释放对象 |
| \`owns(const void* p)\` | 判断指针是否属于本分配器 |
| \`slab_max()\` | slab 路径上限（128） |
| \`big_pool()\` | 访问内部 memory_pool（大对象路径） |

> 注：禁止拷贝；void_any 通过 \`VOID_ANY_USE_LAYERED_ALLOCATOR\` 宏启用。

#### 使用

\`\`\`cpp
layered_allocator la;
void* small = la.allocate(64);   // 走 slab[3]
void* big = la.allocate(256);    // 走 memory_pool
la.deallocate(small);            // 遍历 owns → slab[3]
la.deallocate(big);              // 遍历 owns → memory_pool
la.deallocate(small, 64);        // size-aware 分配 → slab[3]
la.deallocate(big, 256);         // size-aware 分配 → memory_pool

// construct/destroy
struct Foo { int a; double b; Foo(int x, double y) : a(x), b(y) {} };
Foo* foo = la.construct<Foo>(42, 3.14);
la.destroy(foo);                 // 内部走 size-aware 分配

// owns
bool in_la = la.owns(small);
\`\`\`

#### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`deallocate\` 非 layered 分配的指针 | 遍历 owns 未命中后错误走 memory_pool | 仅释放 \`allocate()\` 返回的指针 |
| 同一指针 \`deallocate\` 两次 | double free | 释放后置空 |
| 期望 slab 路径支持 >128B | slab 最大 128B | >128B 自动走 memory_pool |

---
`
};

window.DOCS_DATA['arena_allocator'] = {
  id: 'arena_allocator',
  title: "arena_allocator — 线性 bump 分配器",
  category: 'allocators',
  icon: 'B',
  order: 25,
  content: `## 24. arena_allocator — 线性 bump 分配器

\`#include "part/arena_allocator.hpp"\`，全局命名空间。\`noexcept\`。

线性分配器：无单个 \`deallocate\`，仅 \`reset\` 整体回收。两种模式：自有内存（析构释放）和借用外部 buffer（不持有所有权）。支持 \`align <= 64\` 的分配请求。

### 接口

| 接口 | 说明 |
|------|------|
| \`arena_allocator()\` | 默认构造，空状态 |
| \`arena_allocator(size_t capacity)\` | 自有模式：分配 capacity 字节，析构释放 |
| \`arena_allocator(void* buffer, size_t size)\` | 借用模式：使用外部 buffer，不分配不释放 |
| \`allocate(n, align=16)\` | bump 分配，位运算对齐，返回指针或 nullptr |
| \`reset()\` | 整体回收，不调用析构 |
| \`used()\` | 已使用字节数 |
| \`capacity()\` | 总容量 |
| \`remaining()\` | 剩余字节数 |
| \`empty()\` | 是否未分配（offset==0） |
| \`owns(p)\` | 指针 p 是否属于本 arena |

### 使用

\`\`\`cpp
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
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对 arena 分配的对象调用 \`deallocate\` | 接口不存在 | 用 \`reset\` 整体回收 |
| \`reset\` 后继续使用之前分配的指针 | 内存已回收，数据未定义 | \`reset\` 后丢弃所有指针 |
| 借用模式下析构后访问 buffer | buffer 生命周期由外部管理 | 确保外部 buffer 生命周期覆盖 arena 使用期 |
| 分配超过 \`remaining()\` 的内存 | 返回 nullptr | 先检查 \`remaining()\` 或预分配足够容量 |

---
`
};

window.DOCS_DATA['slab_allocator'] = {
  id: 'slab_allocator',
  title: "slab_allocator — 固定块分配器",
  category: 'allocators',
  icon: 'L',
  order: 26,
  content: `## 25. slab_allocator — 固定块分配器

\`#include "part/slab_allocator.hpp"\`，全局命名空间。\`noexcept\`。

固定块大小分配器，侵入式 free list，O(1) allocate/deallocate。每个 chunk 按序插入 \`class_pool<chunk_node>\` 保持地址有序，\`owns\` 用二分查找定位。

### 接口

| 接口 | 说明 |
|------|------|
| \`slab_allocator(block_size, alignment=16, blocks_per_chunk=256)\` | 构造，block_size 向上对齐到 alignment |
| \`allocate()\` | 分配一个块，返回指针或 nullptr |
| \`deallocate(p)\` | 释放一个块 |
| \`owns(p)\` | 指针 p 是否属于本 slab |
| \`block_size()\` | 实际块大小（对齐后） |
| \`total_blocks()\` | 总块数 |
| \`free_blocks()\` | 空闲块数 |
| \`empty()\` | 是否全部空闲 |
| \`min_addr()\` / \`max_addr()\` | 地址范围 |

### 使用

\`\`\`cpp
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
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`deallocate\` 非本 slab 分配的指针 | free list 损坏，后续崩溃 | 先 \`owns(p)\` 验证 |
| \`deallocate\` 同一指针两次 | 双重释放，free list 损坏 | 确保每块只释放一次 |
| 跨 \`block_size\` 混用分配器 | 块大小不匹配 | 每种块大小用独立 slab_allocator |

---
`
};

window.DOCS_DATA['layered_allocator'] = {
  id: 'layered_allocator',
  title: "layered_allocator — 分层分配器",
  category: 'allocators',
  icon: 'Y',
  order: 27,
  content: `## 26. layered_allocator — 分层分配器

\`#include "part/layered_allocator.hpp"\`，全局命名空间。\`noexcept\`。

组合 8 个 slab_allocator（16/32/48/64/80/96/112/128 字节）和 1 个 memory_pool（TLSF）。小对象（≤128B）走 slab，大对象走 TLSF。\`deallocate\` 通过 \`find_slab\` 遍历判断归属。

### 接口

| 接口 | 说明 |
|------|------|
| \`layered_allocator()\` | 默认构造，初始化 8 个 slab + 1 个 TLSF |
| \`allocate(n)\` | 按大小路由：≤128 走 slab，>128 走 TLSF |
| \`deallocate(p)\` | 通过 \`find_slab\` 判断归属，路由到 slab 或 TLSF |
| \`deallocate(p, n)\` | 按 size 直接路由，避免遍历 slab |
| \`construct<T>(args...)\` | 分配 + placement new 构造 T |
| \`destroy<T>(p)\` | 析构 + 释放 |
| \`owns(p)\` | 指针 p 是否属于本分层分配器 |
| \`slab_max()\` | slab 上限（128） |
| \`big_pool()\` | 内部 TLSF memory_pool 引用 |

### 使用

\`\`\`cpp
#include "part/layered_allocator.hpp"

layered_allocator alloc;
void* small = alloc.allocate(64);   // 走 slab (64B 块)
void* large = alloc.allocate(256);  // 走 TLSF

// 带构造
auto* obj = alloc.construct<MyStruct>(arg1, arg2);
alloc.destroy(obj);

// 带 size 的 deallocate 更高效
alloc.deallocate(small, 64);  // 直接路由到 slab
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用 \`deallocate(p)\` 释放大对象 | 遍历 8 个 slab 后才路由到 TLSF，慢 | 用 \`deallocate(p, n)\` 直接路由 |
| 修改 \`memory_pool\` 与 \`slab_allocator\` 的关系 | 破坏分层路由完整性 | 保持分层路由不变 |

---
`
};

