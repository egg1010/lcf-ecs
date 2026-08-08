window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['memory_pool'] = {
  id: 'memory_pool',
  title: "memory_pool — 内存池",
  category: 'allocators',
  icon: 'K',
  order: 20,
  content: `## 20. memory_pool — 内存池

\`#include "part/memory/memory_pool.hpp"\`，命名空间 \`memory\`。\`noexcept\`。

变长内存池，提供分配、释放、统计、扩缩容接口。

### memory_block — 内存块

| 接口 | 说明 |
|------|------|
| \`memory_block()\` | 默认构造 |
| \`memory_block(uint8_t* data, size_t size)\` | 指定数据和大小构造 |
| \`memory_block(memory_block&&)\` | 移动构造 |
| \`operator=(memory_block&&)\` | 移动赋值 |
| \`data_\` | 数据指针 |
| \`size_\` | 数据大小 |

> 注：禁止拷贝。

### pool_stats — 统计信息

| 字段 | 说明 |
|------|------|
| \`total_allocated\` | 已分配总量 |
| \`total_used\` | 已使用字节 |
| \`total_free\` | 空闲字节 |
| \`free_block_count\` | 空闲块数量 |
| \`max_contiguous_free\` | 最大连续空闲块大小 |
| \`fragmentation\` | 碎片率 [0,1] |

### memory_pool — 内存池

| 接口 | 说明 |
|------|------|
| \`memory_pool(size_t chunk_size = 4096)\` | 构造，指定块大小 |
| \`memory_pool(memory_pool&&)\` | 移动构造 |
| \`operator=(memory_pool&&)\` | 移动赋值 |
| \`allocate(size_t size)\` | 分配内存，返回 16 字节对齐指针 |
| \`allocate_sized<Size>()\` | 模板化分配，Size 为编译期大小 |
| \`deallocate(void* ptr)\` | 释放内存 |
| \`deallocate(void* ptr, size_t size)\` | 释放内存（size 必须与 allocate 一致） |
| \`deallocate_sized<Size>(void* ptr)\` | 模板化释放，与 \`allocate_sized<Size>()\` 配对 |
| \`allocate_zeroed(size_t size)\` | 分配并清零 |
| \`allocate_aligned(size_t size, size_t align)\` | 对齐分配 |
| \`deallocate_aligned(void* p)\` | 释放对齐分配的内存 |
| \`reallocate(void* ptr, size_t old_size, size_t new_size)\` | 重分配 |
| \`reallocate_inplace(void* ptr, size_t old_size, size_t new_size)\` | 原位重分配，成功返回 true |
| \`construct<T>(Args...)\` | 分配并构造对象 |
| \`destroy<T>(T* ptr)\` | 析构并释放对象 |
| \`total_allocated()\` | 已分配总量 |
| \`total_used()\` | 已使用量 |
| \`peak_used()\` | 峰值使用量 |
| \`chunk_size()\` | 块大小 |
| \`empty()\` | 是否空闲 |
| \`owns(const void* ptr)\` | 判断指针是否属于本池 |
| \`allocation_size(const void* ptr)\` | 查询已分配大小 |
| \`stats()\` | 返回 \`pool_stats\` |
| \`iterate_free(Fn&& fn)\` | 遍历空闲块，回调签名 \`void(void* data_ptr, size_t block_size)\` |
| \`increase_capacity(size_t size)\` | 扩容 |
| \`reduce_capacity(size_t target)\` | 缩容至总量 <= target |
| \`reset()\` | 重置到初始状态 |

> 注：禁止拷贝。

### 使用

\`\`\`cpp
#include "part/memory/memory_pool.hpp"
using namespace memory;

memory_pool pool;
void* p = pool.allocate(64);
pool.deallocate(p);

void* q = pool.allocate(64);
pool.deallocate(q, 64);

// sized 模板路径
void* r = pool.allocate_sized<64>();
pool.deallocate_sized<64>(r);

// 对齐分配
void* a = pool.allocate_aligned(128, 32);
pool.deallocate_aligned(a);

// 对象构造
struct Foo { int x; };
Foo* foo = pool.construct<Foo>();
pool.destroy(foo);

// 重分配
void* buf = pool.allocate(32);
void* bigger = pool.reallocate(buf, 32, 64);
// 或原位重分配
// bool ok = pool.reallocate_inplace(buf, 32, 64);

// 统计
pool_stats s = pool.stats();
// s.total_allocated, s.total_used, s.total_free, s.fragmentation

// 遍历空闲块
pool.iterate_free([](void* ptr, size_t sz) {
    // ptr: 空闲块指针, sz: 空闲块大小
});

// 容量管理
pool.increase_capacity(1024 * 1024);
pool.reduce_capacity(4096);
pool.reset();
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用 \`new\`/\`delete\` 管理 \`construct\` 分配的对象 | 与池分配不匹配 | 始终用 \`destroy<T>()\` 释放 |
| \`allocate\` 后忘记 \`deallocate\` | 内存泄漏 | 每次 \`allocate\` 配对一个 \`deallocate\` |
| 拷贝 \`memory_pool\` | 禁止拷贝 | 使用移动语义或引用传递 |
| 在 \`iterate_free\` 回调中修改池状态 | 遍历中增删块会破坏链表 | 仅在回调中读取信息 |

---

`
};

window.DOCS_DATA['list_memory_pool'] = {
  id: 'list_memory_pool',
  title: "list_memory_pool — 链表内存池",
  category: 'allocators',
  icon: 'J',
  order: 21,
  content: `## 21. list_memory_pool — 链表内存池

\`#include "part/memory/list_memory_pool.hpp"\`，命名空间 \`memory\`。\`noexcept\`。

变长内存池，提供 \`allocate\`/\`hard_deallocate\`/\`soft_deallocate\` 三种释放方式。\`soft_deallocate\` 释放的块可被后续 \`allocate\` 复用。

### 接口

| 接口 | 说明 |
|------|------|
| \`list_memory_pool()\` | 默认构造 |
| \`list_memory_pool(list_memory_pool&&)\` | 移动构造 |
| \`operator=(list_memory_pool&&)\` | 移动赋值 |
| \`allocate(size_t bytes)\` | 分配内存 |
| \`allocate_zeroed(size_t bytes)\` | 分配并清零 |
| \`allocate_aligned(size_t bytes, size_t align)\` | 对齐分配 |
| \`deallocate(void* ptr)\` | 释放内存（默认软释放） |
| \`deallocate(void* ptr, size_t bytes)\` | 释放内存（\`bytes\` 参数未使用，仅为签名统一） |
| \`deallocate_aligned(void* p)\` | 释放对齐分配的内存 |
| \`hard_deallocate(void* ptr)\` | 硬释放：释放内存 |
| \`soft_deallocate(void* ptr)\` | 软释放：标记为可复用 |
| \`reallocate(void* ptr, size_t old_bytes, size_t new_bytes)\` | 重分配 |
| \`reallocate_inplace(void* ptr, size_t old_bytes, size_t new_bytes)\` | 原位重分配，成功返回 true |
| \`construct<T>(Args...)\` | 分配并构造对象 |
| \`destroy<T>(T* ptr)\` | 析构并释放对象 |
| \`owns(void* ptr)\` | 判断指针是否属于本池 |
| \`allocation_size(void* ptr)\` | 查询已分配大小 |
| \`total_allocated_bytes()\` | 已分配字节 |
| \`total_capacity_bytes()\` | 总容量字节 |
| \`total_free_bytes()\` | 空闲字节 |
| \`peak_allocated_bytes()\` | 峰值字节 |
| \`allocation_count()\` | 分配次数 |
| \`deallocation_count()\` | 释放次数 |
| \`chunk_count()\` | chunk 数量 |
| \`big_block_count()\` | 大块数量 |
| \`stats()\` | 返回 \`pool_stats\` |
| \`release_all_memory()\` | 释放所有内存归还系统 |
| \`shrink_to_fit()\` | 归还空闲 wilderness 内存给系统 |
| \`iterate_free(Fn&& fn)\` | 遍历空闲块，回调签名 \`void(void* ptr, size_t size, const char* category)\` |
| \`swap(list_memory_pool& other)\` | 交换两个池的内部状态 |
| \`reset_statistics()\` | 重置统计计数 |
| \`reset()\` | 重置分配状态（保留底层 wilderness 内存） |

> 注：禁止拷贝。

### 使用

\`\`\`cpp
#include "part/memory/list_memory_pool.hpp"
using namespace memory;

list_memory_pool pool;
void* p = pool.allocate(64);
void* q = pool.allocate(128);

// 软删除: 可被后续 allocate 复用
pool.soft_deallocate(p);
void* r = pool.allocate(64);  // 可能复用 p 的内存

// 硬删除: 释放内存
pool.hard_deallocate(q);

// 对齐分配
void* a = pool.allocate_aligned(256, 32);
pool.deallocate_aligned(a);

// 统计
pool_stats s = pool.stats();
size_t used = pool.total_allocated_bytes();
size_t peak = pool.peak_allocated_bytes();

// 释放所有内存
pool.release_all_memory();
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对同一指针调用两次 \`soft_deallocate\` 或 \`hard_deallocate\` | 双重释放 | 释放后置空 |
| 用 \`deallocate\` 释放非本池分配的指针 | 内存损坏 | 先 \`owns(ptr)\` 验证 |
| 期望 \`soft_deallocate\` 立即归还内存给系统 | 软删除保留块供复用 | 需立即归还用 \`hard_deallocate\` 或 \`release_all_memory\` |

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

\`#include "part/memory/arena_allocator.hpp"\`，命名空间 \`memory\`。\`noexcept\`。

线性分配器，无单个 \`deallocate\`，仅 \`reset\`/\`restore\` 整体回收。两种模式：自有内存和借用外部 buffer。

### 接口

| 接口 | 说明 |
|------|------|
| \`arena_allocator()\` | 默认构造，空状态 |
| \`arena_allocator(size_t capacity)\` | 自有模式：分配 capacity 字节 |
| \`arena_allocator(void* buffer, size_t size)\` | 借用模式：使用外部 buffer |
| \`arena_allocator(arena_allocator&&)\` | 移动构造 |
| \`operator=(arena_allocator&&)\` | 移动赋值 |
| \`allocate(size_t n, size_t align = 16)\` | 分配内存，溢出返回 nullptr |
| \`allocate_zeroed(size_t n, size_t align = 16)\` | 分配并清零 |
| \`allocate_array<T>(size_t count, size_t align = alignof(T))\` | 分配 count 个 T 的连续内存 |
| \`construct<T>(Args...)\` | 分配并构造对象 |
| \`construct_array<T>(size_t count, Args...)\` | 分配并构造 count 个对象 |
| \`destroy<T>(T* p)\` | 析构对象（不回收内存） |
| \`deallocate(void* p)\` | 空操作（无单个回收） |
| \`deallocate(void* p, size_t n)\` | 空操作（重载，仅为签名统一） |
| \`save_point()\` | 保存当前分配点 |
| \`restore(size_t point)\` | 恢复到保存的分配点 |
| \`reset()\` | 整体回收，不调用析构 |
| \`used()\` | 已使用字节数 |
| \`capacity()\` | 总容量 |
| \`remaining()\` | 剩余字节数 |
| \`empty()\` | 是否未分配 |
| \`peak_used()\` | 峰值使用量 |
| \`allocation_count()\` | 分配次数 |
| \`owns(const void* p)\` | 判断指针是否属于本 arena |

> 注：禁止拷贝；无单个 deallocate，必须 \`reset\`/\`restore\` 整体回收。

### arena_scope — 作用域守卫

| 接口 | 说明 |
|------|------|
| \`arena_scope(arena_allocator& a)\` | 构造时保存分配点 |
| \`~arena_scope()\` | 析构时恢复分配点 |

### double_buffered_arena — 双帧交替

| 接口 | 说明 |
|------|------|
| \`double_buffered_arena(size_t capacity_per_frame)\` | 构造，每帧容量 |
| \`flip()\` | 切换帧并重置新帧 |
| \`current()\` | 当前帧（写入侧） |
| \`previous()\` | 上一帧（读取侧） |

### 使用

\`\`\`cpp
#include "part/memory/arena_allocator.hpp"
using namespace memory;

// 自有模式
arena_allocator ar(4096);
void* p1 = ar.allocate(128);
void* p2 = ar.allocate(64, 32);  // 32 字节对齐
ar.reset();  // 整体回收

// 借用模式
alignas(64) uint8_t buf[2048];
arena_allocator borrowed(buf, sizeof(buf));
void* p3 = borrowed.allocate(100);

// 作用域守卫
arena_allocator arena(8192);
{
    arena_scope scope(arena);
    void* a = arena.allocate(256);
    void* b = arena.allocate(128);
}  // 离开作用域自动 restore

// 双帧交替
double_buffered_arena dba(65536);
void* w = dba.current().allocate(64);   // 写入当前帧
dba.flip();                            // 切换帧
void* r = dba.previous().allocate(32); // 读取上一帧
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 对单个指针调用 \`deallocate\` | 空操作，内存不回收 | 用 \`reset\`/\`restore\` 整体回收 |
| \`reset\` 后继续使用已分配指针 | 内存已回收，指针悬空 | \`reset\` 后重新分配 |
| 借用模式析构后期望释放 buffer | 借用模式不持有所有权 | 由 buffer 所有者管理生命周期 |

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

\`#include "part/memory/slab_allocator.hpp"\`，命名空间 \`memory\`。\`noexcept\`。

固定块大小分配器，所有块大小相同。

### 接口

| 接口 | 说明 |
|------|------|
| \`slab_allocator(size_t block_size, size_t alignment = 16, size_t blocks_per_chunk = 256)\` | 构造，block_size 向上对齐到 alignment |
| \`slab_allocator(slab_allocator&&)\` | 移动构造 |
| \`operator=(slab_allocator&&)\` | 移动赋值 |
| \`allocate()\` | 分配一个块，返回指针或 nullptr |
| \`allocate_zeroed()\` | 分配并清零 |
| \`deallocate(void* p)\` | 释放块 |
| \`construct<T>(Args...)\` | 分配并构造对象 |
| \`destroy<T>(T* ptr)\` | 析构并释放对象 |
| \`reserve(size_t blocks)\` | 预分配至少 blocks 个空闲块 |
| \`reset()\` | 重置：释放所有块到初始状态 |
| \`owns(const void* p)\` | 判断指针是否属于本分配器 |
| \`block_size()\` | 实际块大小（对齐后） |
| \`total_blocks()\` | 总块数 |
| \`free_blocks()\` | 空闲块数 |
| \`total_used()\` | 已使用块数 |
| \`total_bytes()\` | 总字节数 |
| \`used_bytes()\` | 已使用字节数 |
| \`free_bytes()\` | 空闲字节数 |
| \`peak_used_blocks()\` | 峰值使用块数 |
| \`empty()\` | 是否全部空闲 |
| \`min_addr()\` / \`max_addr()\` | 地址范围 |

> 注：禁止拷贝；块大小固定，不支持变长分配。

### 使用

\`\`\`cpp
#include "part/memory/slab_allocator.hpp"
using namespace memory;

slab_allocator slab(64);  // 64 字节块
void* p1 = slab.allocate();
void* p2 = slab.allocate();
slab.deallocate(p1);
slab.deallocate(p2);

// 预分配
slab_allocator slab2(32, 16, 64);
slab2.reserve(100);  // 预分配至少 100 个块

// 对象构造
struct Foo { int x; Foo(int v) : x(v) {} };
Foo* foo = slab.construct<Foo>(42);
slab.destroy(foo);

// 统计
size_t total = slab.total_blocks();
size_t used = slab.total_used();
bool all_free = slab.empty();
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`deallocate\` 非本 slab 分配的指针 | 损坏内部链表 | 先 \`owns(p)\` 验证 |
| 同一指针 \`deallocate\` 两次 | 双重释放 | 释放后置空 |
| 跨 \`block_size\` 混用分配器 | 块大小不匹配 | 每种块大小用独立 slab_allocator |

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

\`#include "part/memory/layered_allocator.hpp"\`，命名空间 \`memory\`。\`noexcept\`。

分层分配器，小块走 slab，大块走 list_memory_pool。

### 接口

| 接口 | 说明 |
|------|------|
| \`layered_allocator()\` | 默认构造 |
| \`layered_allocator(layered_allocator&&)\` | 移动构造 |
| \`operator=(layered_allocator&&)\` | 移动赋值 |
| \`allocate(size_t n)\` | 分配内存 |
| \`allocate_zeroed(size_t n)\` | 分配并清零 |
| \`allocate_aligned(size_t n, size_t align)\` | 对齐分配 |
| \`deallocate(void* p)\` | 释放内存 |
| \`deallocate(void* p, size_t n)\` | 释放内存（带大小提示） |
| \`deallocate_aligned(void* p)\` | 释放对齐分配的内存 |
| \`reallocate(void* p, size_t old_n, size_t new_n)\` | 重分配 |
| \`reallocate_inplace(void* p, size_t old_n, size_t new_n)\` | 原位重分配，成功返回 true |
| \`construct<T>(Args...)\` | 分配并构造对象 |
| \`construct_array<T>(size_t count, Args...)\` | 分配并构造 count 个对象 |
| \`destroy<T>(T* ptr)\` | 析构并释放对象 |
| \`owns(const void* p)\` | 判断指针是否属于本分配器 |
| \`allocation_size(const void* p)\` | 查询已分配大小 |
| \`reset()\` | 重置到初始状态 |
| \`total_allocated_bytes()\` | 已分配字节 |
| \`total_capacity_bytes()\` | 总容量字节 |
| \`peak_allocated_bytes()\` | 峰值字节 |
| \`stats()\` | 返回 \`layered_stats\` |
| \`slab_max()\` | slab 路径上限（128） |
| \`big_pool()\` | 大块路径的 list_memory_pool 引用 |
| \`slab(size_t i)\` | slab 路径的 slab_allocator 引用 |

> 注：禁止拷贝。

### 使用

\`\`\`cpp
#include "part/memory/layered_allocator.hpp"
using namespace memory;

layered_allocator alloc;
void* small = alloc.allocate(64);    // 走 slab
void* big = alloc.allocate(256);      // 走 list_memory_pool
alloc.deallocate(small);
alloc.deallocate(big, 256);          // 带大小提示

// 对齐分配
void* a = alloc.allocate_aligned(128, 32);
alloc.deallocate_aligned(a);

// 对象构造
struct Foo { int x; Foo(int v) : x(v) {} };
Foo* foo = alloc.construct<Foo>(42);
alloc.destroy(foo);

// 批量构造
Foo* arr = alloc.construct_array<Foo>(10, 0);

// 重分配
void* buf = alloc.allocate(32);
void* bigger = alloc.reallocate(buf, 32, 64);

// 统计
layered_stats s = alloc.stats();
size_t used = alloc.total_allocated_bytes();
size_t peak = alloc.peak_allocated_bytes();

// 重置
alloc.reset();
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`deallocate\` 非本分配器分配的指针 | 内存损坏 | 先 \`owns(p)\` 验证 |
| 同一指针 \`deallocate\` 两次 | 双重释放 | 释放后置空 |
| 期望 slab 路径支持 >128B | slab 上限 128B | >128B 自动走 list_memory_pool |

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

\`#include "part/memory/stl_allocator.hpp"\`，命名空间 \`memory\`。\`noexcept\`。

将项目内存池适配为 C++ STL Allocator，可用于 \`std::vector\`/\`std::list\` 等标准容器。

### 接口

| 接口 | 说明 |
|------|------|
| \`stl_allocator()\` | 默认构造 |
| \`stl_allocator(Pool& pool)\` | 绑定到指定内存池 |
| \`stl_allocator(const stl_allocator<U, Pool>&)\` | 转换构造（跨类型 U → T） |
| \`allocate(size_t n)\` | 分配 n 个 T 的内存，失败调用 \`handle_oom\` |
| \`deallocate(T* p, size_t n)\` | 释放 n 个 T 的内存 |
| \`pool()\` | 获取绑定的内存池指针 |
| \`rebind<U>::other\` | 重绑定类型 U 的 allocator 类型 |
| \`address(T& x)\` / \`address(const T& x)\` | 取对象地址 |
| \`max_size()\` | 最大可分配元素数 |
| \`construct<U>(U* p, Args...)\` | 在 p 处构造 U 对象 |
| \`destroy<U>(U* p)\` | 析构 p 指向的对象 |
| \`operator==\` / \`operator!=\` | 比较两个分配器是否等价 |

### 使用

\`\`\`cpp
#include "part/memory/stl_allocator.hpp"
#include "part/memory/layered_allocator.hpp"
#include <vector>
using namespace memory;

layered_allocator pool;

// 用于 std::vector
using vec_alloc = stl_allocator<int, layered_allocator>;
std::vector<int, vec_alloc> v{vec_alloc(pool)};
v.push_back(1);
v.push_back(2);
v.push_back(3);

// 用于 std::list
#include <list>
using list_alloc = stl_allocator<int, layered_allocator>;
std::list<int, list_alloc> l{list_alloc(pool)};
l.push_back(10);
\`\`\`

> 注：同一内存池上的 stl_allocator 实例互相等价；不同内存池上的实例不等价。

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

\`#include "part/memory/oom_handler.hpp"\`，命名空间 \`memory\`。

内存分配失败时统一调用 \`handle_oom\` 输出诊断信息后终止程序。

### 接口

| 接口 | 说明 |
|------|------|
| \`handle_oom(size_t requested_bytes, const char* allocator_name, std::source_location loc = ...)\` | 输出 OOM 诊断后终止程序 |

### 使用

\`\`\`cpp
#include "part/memory/oom_handler.hpp"
using namespace memory;

void* custom_alloc(size_t n) {
    void* p = std::malloc(n);
    if (!p) {
        handle_oom(n, "custom_alloc");
    }
    return p;
}
\`\`\`

> 注：\`handle_oom\` 标记为 \`[[noreturn]]\`，调用后不会返回。

---

`
};
