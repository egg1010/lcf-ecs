window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['memory_pool'] = {
  title: "memory_pool — General-purpose memory pool",
  content: `## 20. memory_pool — General-purpose memory pool

Namespace \`memory\`. All interfaces are \`noexcept\`.

General-purpose variable-length memory pool providing allocation, soft delete, hard delete, reallocation, capacity management, and statistics.

> Threading contract: single-threaded use; for multiple threads, each thread should hold its own instance and not share or free across threads.
>
> Alignment contract: allocated addresses are aligned to at least 16 bytes (\`memory::base_alignment\`).

### memory_stats — Statistics

The \`stats()\` of every allocator on this page shares this structure.

| Field | Description |
|------|------|
| \`reserved_bytes\` | Bytes currently reserved from the system by the pool |
| \`live_bytes\` | Usable bytes of currently live allocations |
| \`reusable_bytes\` | Free bytes that can be reused immediately |
| \`peak_live_bytes\` | Historical peak of live bytes |
| \`acquire_count\` | Number of successful allocations |
| \`release_count\` | Total number of successful deletions (soft delete + hard delete) |
| \`soft_delete_count\` | Number of soft deletions |
| \`hard_delete_count\` | Number of hard deletions |

### Interfaces

| Interface | Description |
|------|------|
| \`explicit memory_pool(size_t chunk_size = 4096)\` | Construct; chunk_size is the size hint for appended chunks |
| \`memory_pool(memory_pool&&)\` | Move constructor |
| \`operator=(memory_pool&&)\` | Move assignment |
| \`swap(memory_pool& other)\` | Swap the state of two pools |
| \`acquire(size_t bytes)\` | Allocate at least bytes bytes; returns nullptr on failure |
| \`acquire_sized<Size>()\` | Compile-time size allocation; Size must be greater than 0 |
| \`acquire_zeroed(size_t bytes)\` | Allocate and zero the first bytes bytes of the request |
| \`acquire_aligned(size_t bytes, size_t alignment)\` | Allocate with alignment; alignment must be a power of 2, values below 16 are treated as 16 |
| \`create<T>(Args&&... args)\` | Allocate and construct T, automatically satisfying alignof(T); returns nullptr on failure |
| \`soft_delete(void* ptr)\` | Soft delete: reclaim the allocation and keep the memory for later reuse; invalid or duplicate pointers are safely ignored |
| \`hard_delete(void* ptr)\` | Hard delete: reclaim the allocation and return mergeable space; invalid or duplicate pointers are safely ignored |
| \`soft_delete_sized<Size>(void* ptr)\` | Soft delete with a compile-time size hint; ignored when size_of(ptr) < Size |
| \`hard_delete_sized<Size>(void* ptr)\` | Hard delete with a compile-time size hint; ignored when size_of(ptr) < Size |
| \`dispose<T>(T* ptr)\` | Only call the destructor, do not reclaim memory |
| \`owns(const void* ptr)\` | Returns true only for the start address of a live allocation |
| \`size_of(const void* ptr)\` | Returns the usable bytes of a live allocation; returns 0 for a non-live address |
| \`contains(const void* ptr)\` | Checks whether the address falls within the address range managed by this pool |
| \`stats()\` | Returns \`memory_stats\` |
| \`empty()\` | Whether there is no live allocation |
| \`free_block_count()\` | Current number of free blocks |
| \`chunk_size()\` | The size hint specified at construction |
| \`reset_stats()\` | Clears the allocation and deletion counters, resets the peak to the current live bytes |
| \`reset()\` | Reclaims all live allocations and keeps reusable capacity, the pool remains usable |
| \`release_all()\` | Reclaims all allocations and returns all underlying memory; counters and peak are preserved |
| \`increase_capacity(size_t bytes)\` | Grow to at least bytes reserved bytes; no-op when not larger than the current reservation |
| \`reduce_capacity(size_t target)\` | Shrink to at most target reserved bytes; live blocks are not moved |
| \`resize_inplace(void* ptr, size_t old_bytes, size_t new_bytes)\` | In-place reallocation; returns true on success, false on failure with the original allocation unchanged |
| \`resize(void* ptr, size_t old_bytes, size_t new_bytes)\` | Reallocation; returns nullptr on failure with the original allocation unchanged |
| \`iterate_free(Fn&& fn)\` | Iterate reusable free blocks, callback signature \`void(void* ptr, size_t bytes)\` |

> Note: copying is prohibited; destruction is equivalent to \`release_all()\`.
>
> \`resize\` convention: when \`ptr\` is nullptr it behaves like \`acquire(new_bytes)\`; when \`new_bytes\` is 0 it soft-deletes and returns nullptr; when in-place is not possible it allocates a new block, copies the data, and soft-deletes the old block. If an over-aligned allocation moves, the original alignment requirement is still preserved.

### Usage

\`\`\`cpp
using namespace memory;

memory_pool pool;

void* p = pool.acquire(64);
pool.soft_delete(p);

void* q = pool.acquire_sized<64>();
pool.hard_delete_sized<64>(q);

void* z = pool.acquire_zeroed(128);  // first 128 bytes are 0
pool.soft_delete(z);

void* a = pool.acquire_aligned(256, 64);
pool.soft_delete(a);

struct Foo { int x; };
Foo* foo = pool.create<Foo>();
if (foo)
{
    pool.dispose(foo);      // destruct only
    pool.soft_delete(foo);  // then reclaim memory
}

// Reallocation
void* buf = pool.acquire(32);
void* bigger = pool.resize(buf, 32, 256);
if (bigger)
{
    buf = bigger;
}

// Statistics
memory_stats s = pool.stats();
size_t live = s.live_bytes;
size_t reusable = s.reusable_bytes;

// Capacity management
pool.increase_capacity(1024 * 1024);
pool.reduce_capacity(64 * 1024);

// Iterate reusable free blocks
pool.iterate_free([](void* ptr, size_t bytes) {
    (void)ptr;
    (void)bytes;
});

// Release everything
pool.release_all();
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Freeing an object constructed by \`create\` with \`delete\` | Pool memory cannot be reclaimed with \`delete\` | \`dispose\` first, then \`soft_delete\` or \`hard_delete\` |
| Forgetting to delete after \`acquire\` | Memory leak | Pair every allocation with a deletion |
| Copying \`memory_pool\` | Copying is prohibited | Use move semantics or pass by reference |
| Calling \`owns\` / \`size_of\` with a non-start address | Only the start address of a live allocation is recognized | Keep and use the original pointer returned by the allocation |
| Deleting the same pointer twice | Duplicate deletion is safely ignored, but it usually indicates a lifetime management error | Null out the pointer after deletion |
| Allocating or deleting inside an \`iterate_free\` callback | The pool state must not change during iteration | Only read information inside the callback |
| Treating \`contains\` as equivalent to \`owns\` | \`contains\` is an address range query | Use \`owns\` to test for a live allocation |

---

`
};

window.DOCS_DATA_EN['list_memory_pool'] = {
  title: "list_memory_pool — Linked-list memory pool",
  content: `## 21. list_memory_pool — Linked-list memory pool

Namespace \`memory\`. All interfaces are \`noexcept\`.

Variable-length memory pool supporting soft delete and hard delete. Soft delete keeps the memory and guarantees that subsequent requests of the same size class reuse that block first.

> Threading contract: single-threaded use; for multiple threads, each thread should hold its own instance and not share or free across threads.
>
> Alignment contract: allocated addresses are aligned to at least 16 bytes (\`memory::base_alignment\`).

### Interfaces

| Interface | Description |
|------|------|
| \`list_memory_pool()\` | Default constructor |
| \`list_memory_pool(list_memory_pool&&)\` | Move constructor |
| \`operator=(list_memory_pool&&)\` | Move assignment |
| \`swap(list_memory_pool& other)\` | Swap the state of two pools |
| \`acquire(size_t bytes)\` | Allocate at least bytes bytes; returns nullptr on failure |
| \`acquire_sized<Size>()\` | Compile-time size allocation; Size must be greater than 0 |
| \`acquire_zeroed(size_t bytes)\` | Allocate and zero the first bytes bytes of the request |
| \`acquire_aligned(size_t bytes, size_t alignment)\` | Allocate with alignment; alignment must be a power of 2, values below 16 are treated as 16 |
| \`create<T>(Args&&... args)\` | Allocate and construct T, automatically satisfying alignof(T); returns nullptr on failure |
| \`soft_delete(void* ptr)\` | Soft delete: keeps the memory for reuse by the same size class; invalid or duplicate pointers are safely ignored |
| \`hard_delete(void* ptr)\` | Hard delete: reclaims the allocation immediately; invalid or duplicate pointers are safely ignored |
| \`soft_delete_sized<Size>(void* ptr)\` | Soft delete with a compile-time size hint; ignored when size_of(ptr) < Size |
| \`hard_delete_sized<Size>(void* ptr)\` | Hard delete with a compile-time size hint; ignored when size_of(ptr) < Size |
| \`dispose<T>(T* ptr)\` | Only call the destructor, do not reclaim memory |
| \`owns(const void* ptr)\` | Returns true only for the start address of a live allocation |
| \`size_of(const void* ptr)\` | Returns the usable capacity of a live allocation, which may be larger than the requested value; returns 0 for a non-live address |
| \`contains(const void* ptr)\` | Checks whether the address falls within the address range managed by this pool |
| \`stats()\` | Returns \`memory_stats\` |
| \`empty()\` | Whether there is no live allocation |
| \`reset_stats()\` | Clears the allocation and deletion counters, resets the peak to the current live bytes |
| \`reset()\` | Reclaims all live allocations and keeps reusable capacity, the pool remains usable |
| \`release_all()\` | Reclaims all allocations and returns all underlying memory; counters and peak are preserved |
| \`increase_capacity(size_t bytes)\` | Grow to at least bytes reserved bytes; no-op when not larger than the current reservation |
| \`reduce_capacity(size_t target)\` | Shrink to at most target reserved bytes; unused reserved regions are returned first |
| \`resize_inplace(void* ptr, size_t old_bytes, size_t new_bytes)\` | In-place reallocation; returns true when the current block capacity satisfies the request |
| \`resize(void* ptr, size_t old_bytes, size_t new_bytes)\` | Reallocation; returns nullptr on failure with the original allocation unchanged |
| \`iterate_free(Fn&& fn)\` | Iterate reusable blocks, callback signature \`void(void* ptr, size_t bytes)\` |

> Note: copying is prohibited; destruction is equivalent to \`release_all()\`.
>
> \`resize\` convention: when \`ptr\` is nullptr it behaves like \`acquire(new_bytes)\`; when \`new_bytes\` is 0 it soft-deletes and returns nullptr; when in-place is not possible it allocates a new block, copies the data, and soft-deletes the old block. If an over-aligned allocation moves, the original alignment requirement is still preserved.

### Usage

\`\`\`cpp
using namespace memory;

list_memory_pool pool;

void* p = pool.acquire(64);
void* q = pool.acquire(8192);

// Soft delete: memory is kept, same-size requests reuse it first
pool.soft_delete(p);
void* r = pool.acquire(64);

// Hard delete: reclaim immediately
pool.hard_delete(q);

// Compile-time size
void* s = pool.acquire_sized<128>();
pool.soft_delete_sized<128>(s);

// Aligned allocation
void* a = pool.acquire_aligned(256, 64);
pool.hard_delete(a);

// Over-aligned object
struct alignas(64) Wide { unsigned char data[64]; };
Wide* w = pool.create<Wide>();
if (w)
{
    pool.dispose(w);
    pool.soft_delete(w);
}

// Any pointer can be queried safely
int local = 0;
bool mine = pool.owns(&local);       // false
bool inside = pool.contains(&local); // false

// Statistics and capacity management
memory_stats st = pool.stats();
pool.increase_capacity(1024 * 1024);
pool.reduce_capacity(64 * 1024);

// Reallocation
void* buf = pool.acquire(128);
void* bigger = pool.resize(buf, 128, 4096);
if (bigger)
{
    buf = bigger;
}

// Release everything
pool.release_all();
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Expecting \`soft_delete\` to return memory to the system immediately | Soft delete keeps the memory for reuse | Use \`hard_delete\`, \`reduce_capacity\`, or \`release_all\` when the memory must be returned |
| Calling \`owns\` / \`size_of\` with a non-start address | Only the start address of a live allocation is recognized | Keep and use the original pointer returned by the allocation |
| Deleting the same pointer twice | Duplicate deletion is safely ignored, but it usually indicates a lifetime management error | Null out the pointer after deletion |
| Deleting a pointer not allocated by this pool with this pool's interface | Pointers from other pools are safely ignored, but the intent to free is wrong | Verify with \`owns(ptr)\` first |
| Treating \`contains\` as equivalent to \`owns\` | After a hard delete the address may still fall inside this pool's range | Use \`owns\` to test for a live allocation |
| Copying \`list_memory_pool\` | Copying is prohibited | Use move semantics or pass by reference |
| Allocating or deleting inside an \`iterate_free\` callback | The pool state must not change during iteration | Only read information inside the callback |

---

`
};

window.DOCS_DATA_EN['arena_allocator'] = {
  title: "arena_allocator — Linear allocator",
  content: `## 26. arena_allocator — Linear allocator

Namespace \`memory\`. All interfaces are \`noexcept\`.

Linear allocator with no single-deletion interface; memory can only be reclaimed as a whole through \`restore\` / \`reset\`. Supports both owned memory and borrowing an external buffer.

> Threading contract: single-threaded use; for multiple threads, each thread should hold its own instance and not share or free across threads.
>
> Alignment contract: addresses returned by \`acquire\` are aligned to at least 16 bytes (\`memory::base_alignment\`).

### Interfaces

| Interface | Description |
|------|------|
| \`arena_allocator()\` | Default constructor, empty state, cannot allocate |
| \`explicit arena_allocator(size_t capacity)\` | Owned mode: allocate capacity bytes |
| \`arena_allocator(void* buffer, size_t size)\` | Borrowed mode: uses an external buffer, does not take ownership |
| \`arena_allocator(arena_allocator&&)\` | Move constructor |
| \`operator=(arena_allocator&&)\` | Move assignment |
| \`swap(arena_allocator& other)\` | Swap the state of two allocators |
| \`acquire(size_t bytes)\` | Allocate at least bytes bytes; returns nullptr when space is insufficient or arguments are invalid |
| \`acquire_zeroed(size_t bytes)\` | Allocate and zero the first bytes bytes |
| \`acquire_aligned(size_t bytes, size_t alignment)\` | Allocate with alignment; alignment must be a power of 2, values below 16 are treated as 16 |
| \`create<T>(Args&&... args)\` | Allocate and construct T, automatically satisfying alignof(T); returns nullptr on failure |
| \`create_array<T>(size_t count, Args&&... args)\` | Construct count objects of T with the same argument list; returns nullptr on failure |
| \`dispose<T>(T* ptr)\` | Only call the destructor, do not reclaim memory |
| \`dispose_array<T>(T* ptr, size_t count)\` | Only call the destructors of count elements in reverse order |
| \`owns(const void* ptr)\` | Returns true only for the start address of a live allocation |
| \`stats()\` | Returns \`memory_stats\`; the deletion counters are always 0 |
| \`empty()\` | Whether there is no live allocation |
| \`reset_stats()\` | Clears the allocation counters, resets the peak to the current live bytes |
| \`save_point()\` | Saves the current allocation point, returns a value usable by \`restore\` |
| \`restore(size_t point)\` | Restores to the specified allocation point; no-op when point exceeds the used bytes or would truncate a live allocation |
| \`reset()\` | Reclaim everything, does not call destructors, does not return underlying memory |
| \`used()\` | Number of used bytes |
| \`capacity()\` | Total capacity in bytes |
| \`remaining()\` | Number of remaining usable bytes |

> Note: copying is prohibited; there is no single-deletion interface; in borrowed mode the buffer's lifetime is managed by the caller.

### Usage

\`\`\`cpp
using namespace memory;

// Owned mode
arena_allocator ar(4096);
void* p = ar.acquire(128);
void* q = ar.acquire_aligned(64, 64);
void* z = ar.acquire_zeroed(32);
ar.reset();

// Borrowed mode
alignas(64) unsigned char buffer[2048];
arena_allocator borrowed(buffer, sizeof(buffer));
void* b = borrowed.acquire(100);

// Save and restore the allocation point
arena_allocator arena(8192);
const size_t mark = arena.save_point();
void* tmp = arena.acquire(256);
arena.restore(mark);

// Scope guard: restores the allocation point on scope exit
{
    arena_scope scope(arena);
    void* scoped = arena.acquire(128);
    (void)scoped;
}

// Objects and arrays
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

// Statistics
memory_stats st = arena.stats();
size_t used = arena.used();
size_t remaining = arena.remaining();

// Double buffering
double_buffered_arena dba(65536);
void* frame_data = dba.current().acquire(64);
dba.flip();
// frame_data belongs to dba.previous(), still readable this frame
\`\`\`

### arena_scope — Scope guard

| Interface | Description |
|------|------|
| \`explicit arena_scope(arena_allocator& a)\` | Saves the allocation point on construction |
| \`~arena_scope()\` | Restores the saved allocation point on destruction |

> Note: copying is prohibited.

### double_buffered_arena — Double buffering

| Interface | Description |
|------|------|
| \`explicit double_buffered_arena(size_t capacity_per_frame)\` | Construct, each frame has capacity_per_frame capacity |
| \`flip()\` | Switch the current frame and reset the new current frame |
| \`current()\` | Current frame (write side) |
| \`previous()\` | Previous frame (read side) |

> Note: both frames have the same capacity; after \`flip()\` the previous frame's data is still readable.

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Calling a release interface on a single pointer | The linear allocator provides no single deletion | Reclaim as a whole with \`restore\` / \`reset\` |
| Continuing to use an old pointer after \`reset\` or \`restore\` | The memory has been reclaimed, the pointer is dangling | Allocate again after reclaiming |
| Passing an arbitrary value to \`restore\` | Values that would truncate a live allocation are ignored, making the reclaim result easy to misjudge | Save and use the result of \`save_point()\` |
| Expecting the buffer to be freed after destruction in borrowed mode | Borrowed mode does not take ownership | Let the buffer's owner manage the lifetime |
| Expecting the deletion counters of \`stats()\` to grow | The linear allocator has no single deletion | Observe the state via \`live_bytes\` / \`reusable_bytes\` |

---

`
};

window.DOCS_DATA_EN['slab_allocator'] = {
  title: "slab_allocator — Fixed-block allocator",
  content: `## 27. slab_allocator — Fixed-block allocator

Namespace \`memory\`. All interfaces are \`noexcept\`.

All allocations use the same fixed block size. Soft delete and hard delete are equivalent; blocks always stay inside the allocator for reuse.

> Threading contract: single-threaded use; for multiple threads, each thread should hold its own instance and not share or free across threads.
>
> Alignment contract: block start addresses are aligned to the alignment parameter, which is not smaller than 16 bytes.

### Interfaces

| Interface | Description |
|------|------|
| \`explicit slab_allocator(size_t block_size, size_t alignment = 16, size_t blocks_per_chunk = 256)\` | Construct; block_size is aligned up to alignment; alignment below 16 or not a power of 2 is treated as 16 |
| \`slab_allocator(slab_allocator&&)\` | Move constructor |
| \`operator=(slab_allocator&&)\` | Move assignment |
| \`swap(slab_allocator& other)\` | Swap the state of two allocators |
| \`acquire()\` | Allocate one block; returns nullptr on failure |
| \`acquire_sized<Size>()\` | Compile-time size allocation; returns nullptr when Size is larger than the block size |
| \`acquire_zeroed()\` | Allocate one block and zero it |
| \`create<T>(Args&&... args)\` | Allocate and construct T; returns nullptr when sizeof(T) or alignof(T) exceeds the block capability |
| \`soft_delete(void* ptr)\` | Soft delete: reclaim the block for reuse; invalid or duplicate pointers are safely ignored |
| \`hard_delete(void* ptr)\` | Hard delete: equivalent to \`soft_delete\` |
| \`soft_delete_sized<Size>(void* ptr)\` | Soft delete with a compile-time size hint; ignored when Size is larger than the block size |
| \`hard_delete_sized<Size>(void* ptr)\` | Hard delete with a compile-time size hint; ignored when Size is larger than the block size |
| \`dispose<T>(T* ptr)\` | Only call the destructor, do not reclaim memory |
| \`owns(const void* ptr)\` | Returns true only for the start address of a live block |
| \`size_of(const void* ptr)\` | Returns the block size; returns 0 for a non-live address |
| \`stats()\` | Returns \`memory_stats\` |
| \`empty()\` | Whether all blocks are free |
| \`reset_stats()\` | Clears the allocation and deletion counters, resets the peak to the current live bytes |
| \`reset()\` | All blocks return to the free state, underlying memory is not returned |
| \`release_all()\` | Releases all underlying memory; counters and peak are preserved |
| \`block_size()\` | Actual block size (after alignment) |
| \`total_blocks()\` | Current total number of blocks |
| \`free_blocks()\` | Current number of free blocks |

> Note: copying is prohibited; variable-length allocation is not supported; when block_size is 0 the allocator is unusable and allocation returns nullptr; destruction is equivalent to \`release_all()\`.

### Usage

\`\`\`cpp
using namespace memory;

slab_allocator slab(64);

void* p = slab.acquire();
void* q = slab.acquire_zeroed();
slab.soft_delete(p);
slab.hard_delete(q);  // equivalent to soft_delete

void* s = slab.acquire_sized<48>();  // 48 <= block_size
slab.soft_delete_sized<48>(s);

struct Foo { int x; Foo(int v) : x(v) {} };
Foo* foo = slab.create<Foo>(42);
if (foo)
{
    slab.dispose(foo);
    slab.soft_delete(foo);
}

// Statistics and pool info
memory_stats st = slab.stats();
size_t total = slab.total_blocks();
size_t free_count = slab.free_blocks();
size_t block = slab.block_size();

// Release everything
slab.reset();        // keeps the underlying memory
slab.release_all();  // returns the underlying memory
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Requesting an object larger than the block size with \`acquire_sized<Size>\` | Returns nullptr, nothing is allocated | Use a large enough block_size or switch to a variable-length pool |
| Constructing a type that exceeds the block capability with \`create<T>\` | Returns nullptr when sizeof/alignof exceed the limit | Check the return value or increase block_size |
| Mixing objects of different sizes in the same allocator | The block size is fixed and does not represent the request size | Use a separate \`slab_allocator\` for each block size |
| Deleting a pointer not from this allocator with this allocator | It is safely ignored, but the intent to free is wrong | Verify with \`owns(p)\` first |
| Deleting the same pointer twice | Duplicate deletion is safely ignored, but it usually indicates a lifetime management error | Null out the pointer after deletion |
| Expecting soft delete to return memory to the system | Both soft delete and hard delete only put the block back into the free state | Use \`release_all\` when the memory must be returned |
| Copying \`slab_allocator\` | Copying is prohibited | Use move semantics or pass by reference |

---

`
};

window.DOCS_DATA_EN['layered_allocator'] = {
  title: "layered_allocator — Layered allocator",
  content: `## 28. layered_allocator — Layered allocator

Namespace \`memory\`. All interfaces are \`noexcept\`.

A layered allocator that automatically selects the fixed-block or variable-length block path according to the request size, providing a unified allocation, deletion, reallocation, and statistics interface.

> Threading contract: single-threaded use; for multiple threads, each thread should hold its own instance and not share or free across threads.
>
> Alignment contract: allocated addresses are aligned to at least 16 bytes (\`memory::base_alignment\`).

### Interfaces

| Interface | Description |
|------|------|
| \`layered_allocator()\` | Default constructor |
| \`layered_allocator(layered_allocator&&)\` | Move constructor |
| \`operator=(layered_allocator&&)\` | Move assignment |
| \`acquire(size_t bytes)\` | Allocate at least bytes bytes; returns nullptr on failure |
| \`acquire_sized<Size>()\` | Compile-time size allocation; Size must be greater than 0 |
| \`acquire_zeroed(size_t bytes)\` | Allocate and zero the first bytes bytes of the request |
| \`acquire_aligned(size_t bytes, size_t alignment)\` | Allocate with alignment; alignment must be a power of 2 |
| \`create<T>(Args&&... args)\` | Allocate and construct T, automatically satisfying alignof(T); returns nullptr on failure |
| \`create_array<T>(size_t count, Args&&... args)\` | Construct count objects of T with the same argument list; returns nullptr on failure |
| \`soft_delete(void* ptr)\` | Soft delete: reclaims according to the pointer's origin and keeps the memory for reuse; invalid or duplicate pointers are safely ignored |
| \`hard_delete(void* ptr)\` | Hard delete: reclaims according to the pointer's origin; invalid or duplicate pointers are safely ignored |
| \`soft_delete_sized<Size>(void* ptr)\` | Soft delete with a compile-time size hint; ignored when size_of(ptr) < Size |
| \`hard_delete_sized<Size>(void* ptr)\` | Hard delete with a compile-time size hint; ignored when size_of(ptr) < Size |
| \`dispose<T>(T* ptr)\` | Only call the destructor, do not reclaim memory |
| \`dispose_array<T>(T* ptr, size_t count)\` | Only call the destructors of count elements in reverse order |
| \`owns(const void* ptr)\` | Returns true only for the start address of a live allocation |
| \`size_of(const void* ptr)\` | Returns the usable capacity of a live allocation; returns 0 for a non-live address |
| \`stats()\` | Returns \`memory_stats\` (sum of all paths) |
| \`empty()\` | Whether there is no live allocation |
| \`reset_stats()\` | Clears the allocation and deletion counters, resets the peak to the current live bytes |
| \`reset()\` | Reclaims all live allocations and keeps reusable capacity, the allocator remains usable |
| \`release_all()\` | Reclaims all allocations and returns all underlying memory; counters and peak are preserved |
| \`resize_inplace(void* ptr, size_t old_bytes, size_t new_bytes)\` | In-place reallocation; returns true when the current block capacity satisfies the request |
| \`resize(void* ptr, size_t old_bytes, size_t new_bytes)\` | Reallocation; returns nullptr on failure with the original allocation unchanged |
| \`slab_max()\` | Maximum request bytes supported by the fixed-block path |
| \`big_pool()\` | Read-only access to the variable-length block path, returns \`const list_memory_pool&\` |
| \`slab(size_t i)\` | Read-only access to the fixed-block path, returns \`const slab_allocator&\`, i ranges from 0 to 3 |

> Note: copying is prohibited; \`create_array\` must be paired with \`dispose_array\` and \`soft_delete\`/\`hard_delete\`; destruction is equivalent to \`release_all()\`.
>
> \`resize\` convention: when \`ptr\` is nullptr it behaves like \`acquire(new_bytes)\`; when \`new_bytes\` is 0 it soft-deletes and returns nullptr; when in-place is not possible it allocates a new block, copies the data, and soft-deletes the old block. If an over-aligned allocation moves, the original alignment requirement is still preserved.

### Usage

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

// Reallocation
void* buf = alloc.acquire(128);
void* bigger = alloc.resize(buf, 128, 4096);
if (bigger)
{
    buf = bigger;
}

// Statistics and read-only path queries
memory_stats st = alloc.stats();
size_t limit = alloc.slab_max();
const list_memory_pool& pool = alloc.big_pool();
const slab_allocator& block_pool = alloc.slab(0);
memory_stats pool_stats = pool.stats();
size_t block_size = block_pool.block_size();

// Release everything
alloc.release_all();
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Deleting a pointer allocated by another allocator with this allocator | It is safely ignored, but the intent to free is wrong | Verify with \`owns(ptr)\` first |
| Deleting the same pointer twice | Duplicate deletion is safely ignored, but it usually indicates a lifetime management error | Null out the pointer after deletion |
| Only calling \`dispose_array\` on the elements of \`create_array\` | The elements are destructed, but the memory is still not reclaimed | Call \`soft_delete\` or \`hard_delete\` after \`dispose_array\` |
| Ignoring the failure return value of \`resize\` | On failure the original allocation is still valid, using the new pointer would be wrong | Check the return value and keep the original pointer on failure |
| Copying \`layered_allocator\` | Copying is prohibited | Use move semantics or pass by reference |

---

`
};

window.DOCS_DATA_EN['stl_allocator'] = {
  title: "stl_allocator — STL adapter",
  content: `## 29. stl_allocator — STL Allocator adapter

Namespace \`memory\`.

Adapts the project's memory pool as a C++ STL Allocator, usable with standard containers such as \`std::vector\` / \`std::list\`.

> Threading contract: single-threaded use, do not share containers across threads.

### Interfaces

| Interface | Description |
|------|------|
| \`stl_allocator()\` | Default constructor, not bound to a pool; allocation triggers \`handle_oom\` |
| \`explicit stl_allocator(Pool& pool)\` | Binds to the specified memory pool |
| \`stl_allocator(const stl_allocator<U, Pool>& other)\` | Converting constructor (across types U → T) |
| \`allocate(size_t n)\` | Allocates memory for n objects of T and satisfies alignof(T); returns nullptr when n is 0, other failures call \`handle_oom\` |
| \`deallocate(T* p, size_t n)\` | Soft-deletes the memory of n objects of T (n does not participate in reclaiming) |
| \`pool()\` | Gets the bound memory pool pointer |
| \`rebind<U>::other\` | Rebinds the allocator type for type U |
| \`address(T& x)\` / \`address(const T& x)\` | Gets the object address |
| \`max_size()\` | Maximum number of allocatable elements |
| \`construct<U>(U* p, Args&&... args)\` | Constructs a U object at p |
| \`destroy<U>(U* p)\` | Destructs the object pointed to by p |
| \`operator==\` / \`operator!=\` | Compares whether two allocators are equivalent |

> Note: \`stl_allocator\` instances on the same memory pool are mutually equivalent; instances on different memory pools are not equivalent. The container's lifetime must not exceed the bound memory pool.

### Usage

\`\`\`cpp
using namespace memory;

layered_allocator pool;

// For std::vector
using vec_alloc = stl_allocator<int, layered_allocator>;
std::vector<int, vec_alloc> v{vec_alloc(pool)};
v.push_back(1);
v.push_back(2);
v.push_back(3);

// For std::list
using list_alloc = stl_allocator<int, layered_allocator>;
std::list<int, list_alloc> l{list_alloc(pool)};
l.push_back(10);
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Allocating with a default-constructed \`stl_allocator\` | Not bound to a pool, triggers \`handle_oom\` | Bind the pool with \`stl_allocator(pool)\` |
| The container outlives the bound memory pool | After the pool is destructed the container still holds its memory | Destroy the container first, then the pool |
| Manually calling \`deallocate\` to free memory not allocated by that container | Lifetime management error | Let the container manage allocation and release |

---

`
};

window.DOCS_DATA_EN['oom_handler'] = {
  title: "oom_handler — OOM handling",
  content: `## 30. oom_handler — Handling allocation failure

Namespace \`memory\`.

When a memory allocation fails, \`handle_oom\` is called uniformly to output diagnostic information and then terminate the program.

### Interfaces

| Interface | Description |
|------|------|
| \`handle_oom(size_t requested_bytes, const char* allocator_name, std::source_location loc = std::source_location::current())\` | Outputs an OOM diagnostic and then terminates the program; \`allocator_name\` may be nullptr |

> Note: \`handle_oom\` is marked \`[[noreturn]]\`, it never returns after being called.

### Usage

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

// A wrapper can forward the caller's source location
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

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Expecting execution to continue after \`handle_oom\` returns | The program terminates after the call | Perform the necessary state cleanup before calling |
| Calling \`handle_oom\` for recoverable allocation failures | It terminates the program directly | Check the nullptr return value of the allocation interface in recoverable cases |

---

`
};
