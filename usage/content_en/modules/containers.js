window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};
window.DOCS_DATA_EN['dense'] = {
  title: "dense\\<T> — Dense container",
  content: `## 20. dense\\<T> — Dense container

No namespace. All interfaces are \`noexcept\`. A general-purpose container that replaces \`std::vector\` in dense scenarios; it is the standalone container form of \`class_pool\`'s dense mode.

### Construction and assignment

| Interface | Description |
|------|------|
| \`dense()\` | Default constructor (empty) |
| \`explicit dense(size_t capacity)\` | Reserve capacity (allocates memory only, \`size()\` is 0) |
| \`dense(size_t count, const T& value)\` | \`count\` copies of \`value\` |
| \`dense(InputIt first, InputIt last)\` | Iterator range construction |
| \`dense(std::initializer_list<T> init)\` | Initializer list construction |
| \`dense(const dense& other)\` | Copy constructor (deep copy) |
| \`dense(dense&& other)\` | Move constructor |
| \`operator=(const dense&)\` | Copy assignment (deep copy) |
| \`operator=(dense&&)\` | Move assignment |

### Element access

| Interface | Description |
|------|------|
| \`operator[](size_t index)\` | Subscript access (no bounds checking) |
| \`get(index)\` | Returns element pointer; returns \`nullptr\` when \`index >= size()\`, the caller decides |
| \`get(index, error_index)\` | Falls back to \`error_index\` when out of range; falls back to 0 if \`error_index\` is also out of range |
| \`front()\` / \`back()\` | First/last element |
| \`data()\` | Raw pointer |
| \`span()\` / \`span() const\` | \`std::span<T>\` view |

### Capacity and state

| Interface | Description |
|------|------|
| \`size()\` | Number of elements |
| \`capacity()\` | Capacity |
| \`empty()\` | Whether empty |
| \`valid()\` | \`data() != nullptr\` |
| \`size_bytes()\` / \`capacity_bytes()\` | Used/capacity bytes |
| \`max_size()\` | Theoretical maximum element count |

### Modifiers

| Interface | Description |
|------|------|
| \`emplace_back(Args&&...)\` | Construct in place at the end (grows automatically when capacity is insufficient) |
| \`emplace_back_unchecked(Args&&...)\` | Construct in place at the end (caller guarantees sufficient capacity) |
| \`append_bulk(const T* src, size_t count)\` | Bulk copy append |
| \`append_bulk_move(T* src, size_t count)\` | Bulk move append |
| \`append_generated(count, F&& generator)\` | Bulk append values produced by a generator |
| \`fill_bulk(value, start, count)\` | Fill \`count\` copies of \`value\` starting at \`start\` |
| \`emplace(pos, args...)\` | Insert at an arbitrary position |
| \`erase(first, last)\` | Range erase (shifts subsequent elements) |
| \`pop_back()\` | Erase at the end |
| \`erase_unordered(index)\` | Unordered erase: moves the last element to this position and pops it; returns the moved last index |
| \`clear()\` | Clear (\`size=0\`, capacity retained) |
| \`swap(dense&)\` / free function \`swap(a, b)\` | Swap |

### Capacity control

| Interface | Description |
|------|------|
| \`increase_capacity(new_capacity)\` | Grow to \`new_capacity\` (grow only, never shrink, \`size\` unchanged) |
| \`resize_fill(new_capacity, value)\` | Grow \`size\` to \`new_capacity\` and fill new positions with \`value\` (returns directly when \`new_capacity <= size\`, destroying no objects) |
| \`reduce_capacity(new_capacity)\` | Shrink to \`new_capacity\` (shrink only, never grow; excess is truncated) |
| \`reduce_capacity(new_capacity, dense<T>& dst)\` | Shrink and migrate truncated elements to \`dst\` |

### Iterators

| Interface | Description |
|------|------|
| \`begin()\` / \`end()\` | Forward iterators (raw pointers) |
| \`cbegin()\` / \`cend()\` | const forward iterators |
| \`for_each(F&& f)\` / \`for_each(F&& f) const\` | Traverse all elements, calling \`f(v)\` |

### Reverse iteration

| Interface | Description |
|------|------|
| \`rbegin()\` / \`rend()\` | Reverse iterators |
| \`rbegin() const\` / \`rend() const\` | const reverse iterators |
| \`crbegin()\` / \`crend()\` | const reverse iterators (const overloads only) |
| \`reverse_for_each(F&& f)\` / \`reverse_for_each(F&& f) const\` | Reverse traversal, calling \`f(v)\` |

### Subrange views

Return \`std::span\` views without copying data.

| Interface | Description |
|------|------|
| \`subspan(offset, count)\` | Returns a span of \`[offset, offset+count)\`, automatically truncated to \`size()\` |
| \`subspan(offset)\` | Returns a span of \`[offset, size())\` |
| \`first(n)\` | First \`n\` elements |
| \`last(n)\` | Last \`n\` elements |

All interfaces provide const overloads. Template method calls require the \`template\` keyword: \`d.window_span<8>(0)\`.

### Strided views

Views that traverse with a fixed stride.

| Interface | Description |
|------|------|
| \`strided_span_view(start, step, count)\` | Returns \`container_strided_span<dense<T>>\` holding \`{container pointer, start, step, count}\` |
| \`strided_for_each(start, step, F&& f)\` | Runtime-stride traversal, calling \`f(v)\` |
| \`strided_for_each<Step>(F&& f)\` | Compile-time-stride traversal; \`Step=1\` degenerates to \`for_each\` |

\`container_strided_span\` itself provides \`begin()/end()\` iterators, \`for_each(F&&)\`, \`operator[]\`, \`size()\`, \`data()\`, \`step()\`, and so on.

### Transform views

Fuse transformation and consumption, avoiding intermediate temporary arrays.

| Interface | Description |
|------|------|
| \`transform_for_each(FTransform&& tr, FConsume&& con)\` | Calls \`con(tr(v))\` for each element \`v\` |
| \`transform_to(R* dst, count, F&& tr)\` | Writes \`tr(v)\` into \`dst\`; requires \`count <= size()\`, and \`dst\` must be unconstructed memory |

### Filtering and lookup

| Interface | Description |
|------|------|
| \`find(const T& value)\` | Linear search, returns pointer to first match, \`nullptr\` if not found |
| \`find_if(Pred pred)\` | Predicate search |
| \`find_if_not(Pred pred)\` | Negated predicate search |
| \`count_if(Pred pred)\` | Predicate count |
| \`filter_for_each(Pred pred, F&& f)\` | Calls \`f(v)\` only for elements satisfying \`pred(v)\` |
| \`filter_indices_to(dense<size_t>& dst, Pred pred)\` | Appends indices satisfying the predicate to \`dst\` |

### Reduction and extremes

| Interface | Description |
|------|------|
| \`reduce(F&& f, U init)\` | Sequential reduction: \`acc = f(acc, v)\` |
| \`min_element()\` / \`max_element()\` | Returns pointer to the minimum/maximum element |
| \`minmax_element()\` | Returns \`{min_ptr, max_ptr}\` |
| \`sum()\` | Arithmetic sum (requires \`is_arithmetic_v<T>\`) |
| \`dot_product(const U* other, count)\` | Dot product (requires \`is_arithmetic_v<T>\`) |

### Windows and chunks

| Interface | Description |
|------|------|
| \`for_each_window<N>(F&& f)\` | Sliding-window traversal, calling \`f(std::span<T, N>)\` for each \`[i, i+N)\`, \`size()-N+1\` times in total |
| \`for_each_chunk<N>(F&& f)\` | Non-overlapping chunk traversal, calling \`f(std::span<T, N>)\` for each \`[i*N, (i+1)*N)\`, \`size()/N\` times in total |
| \`window_span<N>(offset)\` | Returns the sliding-window span at offset \`offset\` (\`std::span<T>\`, truncated when fewer than \`N\`) |

### Enumerated views

| Interface | Description |
|------|------|
| \`for_each_enumerated(F&& f)\` | Traversal with indices, calling \`f(index, value)\` |

### Two-container synchronization

| Interface | Description |
|------|------|
| \`for_each_zip(U* other, count, F&& f)\` | Traverses \`*this\` and \`other\` in sync, calling \`f(x, y)\` |
| \`for_each_zip(dense<U>& other, F&& f)\` | dense version |
| \`for_each_zip(std::span<U> other, F&& f)\` | span version |
| \`zip_with_to<R>(R* dst, const U* other, count, F&& f)\` | Writes \`f(x, y)\` into \`dst\` (SoA→AoS conversion) |
| \`equal(const T* other, count)\` | Element-wise equality comparison |
| \`equal(const dense<U>& other)\` | dense version |
| \`equal(std::span<const U> other)\` | span version |

### Alignment

| Interface | Description |
|------|------|
| \`simd_for_each(F&& f)\` | Traversal (requires \`is_trivially_copyable_v<T>\`, sizeof ≤ 32) |
| \`unaligned_tail_offset()\` | Returns the starting offset of the tail that cannot be handled with alignment |

### Copy and move

| Interface | Description |
|------|------|
| \`copy_to(T* dst, count)\` | Bulk copy |
| \`copy_to(std::span<T> dst)\` | span version |
| \`move_to(T* dst, count)\` | Bulk move |
| \`move_to(std::span<T> dst)\` | span version |
| \`reverse_copy_to(T* dst, count)\` | Reverse copy |
| \`reverse_copy_to(std::span<T> dst)\` | span version |

### Usage

\`\`\`cpp

dense<int> pool;                          // default construction
dense<int> reserved(100);                 // reserve capacity 100
dense<int> filled(5, 42);                 // 5 copies of 42
dense<int> init = {10, 20, 30};           // initializer list

pool.emplace_back(1);
pool.emplace_back(2);
pool.emplace_back(3);

// emplace_back copy/move
int v = 42;
pool.emplace_back(v);                        // append by copy
pool.emplace_back(std::move(v));             // append by move

// bulk append
dense<int> bulk;
bulk.fill_bulk(7, bulk.size(), 100);      // append 100 copies of 7
bulk.append_bulk(init.data(), init.size());

// capacity control
pool.increase_capacity(1000);
pool.reduce_capacity(pool.size());
pool.resize_fill(10, 99);                   // grow size to 10 and fill with 99

// element access
int v = pool[0];
int* p = pool.data();
std::span<int> s = pool.span();

// iteration
for (int x : pool) { /* ... */ }
pool.for_each([](int& x) { x *= 2; });

// reduce_capacity migrates elements
dense<int> src = {1, 2, 3, 4, 5};
dense<int> dst;
src.reduce_capacity(2, dst);              // src: {1, 2}, dst: {3, 4, 5}
\`\`\`

### View usage example

\`\`\`cpp

dense<float> d(1000, 0.0f);
for (size_t i = 0; i < 1000; ++i) d[i] = static_cast<float>(i);

// subrange views
std::span<float> head = d.first(10);
std::span<float> tail = d.last(10);
std::span<float> mid = d.subspan(100, 50);
std::span<float> fixed_head = d.first(4);   // dynamic span, truncated when fewer than 4

// reverse iteration
d.reverse_for_each([](float& v) { v = -v; });
for (auto it = d.rbegin(); it != d.rend(); ++it) { /* ... */ }

// strided traversal
d.strided_for_each(0, 4, [](float& v) { v *= 2.0f; });  // take one out of every 4
auto sv = d.strided_span_view(0, 4, 250);              // container_strided_span<dense<float>>

// fused transform + consume
d.transform_for_each(
    [](float& v) -> float { return v * 2.0f; },
    [](float x) { /* consume x */ });

// filtering
float* p = d.find(42.0f);
bool has = d.find(42.0f) != nullptr;
size_t cnt = d.count_if([](float v) { return v > 0; });
d.filter_for_each([](float v) { return v > 0; }, [](float& v) { v = 0; });

dense<size_t> indices;
d.filter_indices_to(indices, [](float v) { return v > 100; });

// reduction
float sum = d.sum();
float s = d.reduce([](float acc, const float& v) { return acc + v; }, 0.0f);
float* mn = d.min_element();
auto [mn_ptr, mx_ptr] = d.minmax_element();

// windows/chunks
d.for_each_window<4>([](std::span<float, 4> w) { /* sliding window */ });
d.for_each_chunk<4>([](std::span<float, 4> c) { /* non-overlapping chunk */ });

// enumeration
d.for_each_enumerated([](size_t i, float& v) { v += static_cast<float>(i); });

// two-container synchronization
dense<float> other(1000, 0.0f);
d.for_each_zip(other, [](float& a, float& b) { a += b; });
bool same = d.equal(other);

// traversal (requires trivially copyable)
d.simd_for_each([](float& v) { v *= 2.0f; });
size_t tail_off = d.unaligned_tail_offset();

// copy/move
dense<float> dst2(1000, 0.0f);
d.copy_to(dst2.data(), 1000);
d.reverse_copy_to(dst2.span());
\`\`\`

### What not to do

| Wrong usage | Problem | Correct usage |
|---------|------|---------|
| Call \`emplace_back_unchecked\` without confirming capacity | Out-of-bounds write crash | \`increase_capacity\` first, or use \`emplace_back\` |
| Write \`emplace_back({a, b})\` | A brace list has no type, so \`Args\` cannot be deduced | Write \`emplace_back(T{a, b})\` |
| Access elements truncated from \`src\` after \`reduce_capacity(n, dst)\` | Elements have migrated to \`dst\` | Access the corresponding elements in \`dst\` |
| \`fill_bulk\` range exceeds \`capacity()\` | Out-of-bounds write crash | \`increase_capacity\` first |
| Template method call missing the \`template\` keyword | Compile error | \`d.first(N)\` |
| Call \`simd_for_each\` on a non-trivially-copyable type | Compile error (concept constraint) | Use \`for_each\` instead |
| \`subspan\` with \`offset\` beyond \`size()\` | Returns an empty span (already truncated) | Check \`offset < size()\` before calling |

---
`
};

window.DOCS_DATA_EN['class_pool'] = {
  title: "class_pool\\<T> — Core container",
  content: `## 15. class_pool\\<T> — Core container

A container supporting both dense and sparse storage modes, aimed at sparse scenarios (element management with holes). For dense scenarios use \`dense<T>\`.

**Two modes:**

| Mode | Trigger condition | Iteration behavior |
|------|---------|---------|
| **dense** | No holes (all slots constructed) | Linear scan over \`[0, size())\` |
| **sparse** | Holes exist (some slots unconstructed) | Automatically skips unconstructed slots, traversing only constructed elements |

**Mode switching:** Determined automatically, no manual intervention needed.

### Construction and assignment

| Interface | Description |
|------|------|
| \`class_pool()\` | Default constructor |
| \`class_pool(size_t capacity)\` | Reserve-capacity construction |
| \`class_pool(size_t count, const T& value)\` | Construct count copies of value |
| \`class_pool(InputIt first, InputIt last)\` | Iterator range construction |
| \`class_pool(initializer_list<T>)\` | Initializer list construction |
| \`class_pool(const class_pool&)\` | Copy constructor |
| \`class_pool(class_pool&&)\` | Move constructor |
| \`operator=(const class_pool&)\` | Copy assignment |
| \`operator=(class_pool&&)\` | Move assignment |

### Element access

| Interface | Description |
|------|------|
| \`operator[](size_t)\` | Subscript access (no bounds checking) |
| \`get(size_t)\` | Returns element pointer; returns \`nullptr\` when out of range or a hole, the caller decides |
| \`get(size_t, size_t error_index)\` | Bounds-protected access: when \`index >= size()\`, accesses \`error_index\` instead; falls back to 0 if \`error_index\` is also out of range; an empty container triggers a trap |
| \`front()\` | Reference to the first element |
| \`back()\` | Reference to the last element |
| \`data()\` | Raw data pointer |
| \`span()\` | Returns \`std::span<T>\` |
| \`span() const\` | Returns \`std::span<const T>\` |

### Capacity

| Interface | Description |
|------|------|
| \`size()\` | Used size |
| \`capacity()\` | Total capacity |
| \`empty()\` | Whether empty |
| \`count()\` | Number of constructed elements |
| \`valid()\` | Whether allocated |
| \`size_bytes()\` | Used bytes |
| \`capacity_bytes()\` | Total capacity bytes |
| \`max_size()\` | Theoretical maximum element count |

### Modifiers

| Interface | Description |
|------|------|
| \`emplace_back(Args...)\` | Construct an element at the end |
| \`emplace_back_unchecked(Args...)\` | Construct in place at the end (caller guarantees sufficient capacity, no automatic growth) |
| \`append_bulk(const T* src, size_t count)\` | Bulk copy append |
| \`append_bulk_move(T* src, size_t count)\` | Bulk move append |
| \`append_generated(count, F&& generator)\` | Bulk append values produced by a generator |
| \`emplace(pos, Args...)\` | Insert at the specified position (shifts subsequent elements) |
| \`emplace_at(index, Args...)\` | Construct at an arbitrary position (insert-or-assign: if occupied, destroy the old object first then reconstruct) |
| \`sparse_erase_at(index)\` | Sparse erase (does not move elements, creates a hole) |
| \`soft_dense_delete(start, end)\` | Soft-delete a range (retains object memory) |
| \`pop_back()\` | Erase the element at the end |
| \`erase(pos)\` | Erase at the specified position (shifts subsequent elements) |
| \`erase(first, last)\` | Range erase (shifts subsequent elements) |
| \`erase_unordered(index)\` | Unordered erase: moves the last active element to this position, returns the original last index |
| \`clear()\` | Clear all elements |
| \`increase_capacity(capacity)\` | Grow (grow only, never shrink, adds no elements) |
| \`resize_fill(new_capacity, value)\` | Grow \`size\` to \`new_capacity\` and fill new positions with \`value\` (grow only, never shrink; returns directly when \`new_capacity <= size\`, destroying no objects) |
| \`reduce_capacity(capacity)\` | Shrink (truncates excess elements) |
| \`reduce_capacity(capacity, dst)\` | Shrink, moving excess elements to dst |
| \`fill_bulk(value, start, count)\` | Fill \`count\` copies of \`value\` starting at \`start\` (grows automatically; non-trivial types destroy existing objects before reconstructing) |
| \`prepare_dense(new_size)\` | Prepare dense mode up to \`new_size\` (grow + default-construct new slots + mark as allocated, making the container enter dense mode) |
| \`swap(other)\` | Swap two containers |

### State queries

| Interface | Description |
|------|------|
| \`is_constructed_at(index)\` | Checks whether the specified position is constructed |
| \`is_dense()\` | Whether in dense mode (no holes) |
| \`invalidate_count_cache()\` | Invalidates the count cache |

### Effect of each operation on contiguity

| Operation | Effect on contiguity |
|------|---------------------|
| \`emplace_back()\` | Stays contiguous |
| \`emplace(pos)\` | Stays contiguous (elements shift right) |
| \`pop_back()\` | Stays contiguous |
| \`clear()\` | Resets to contiguous |
| \`sparse_erase_at(index)\` | **Creates a hole** → becomes sparse |
| \`emplace_at(index)\` | **May fill a hole** → may become contiguous |
| \`reduce_capacity()\` shrinking | **May eliminate holes** → may become contiguous |

### Insert/erase

| Interface | Description |
|------|------|
| \`emplace(const_iterator pos, args...)\` | Constructs in place at the specified position, returns an iterator to the new element |

### Iterators

| Interface | Description |
|------|------|
| \`begin()\` / \`end()\` | Forward iterators (dense mode iterates pointers directly, sparse mode automatically skips unconstructed slots) |
| \`cbegin()\` / \`cend()\` | const versions |
| \`rbegin()\` / \`rend()\` | Reverse iterators (sparse mode also automatically skips unconstructed slots) |
| \`crbegin()\` / \`crend()\` | const reverse versions |
| \`for_each(F&& f)\` / \`for_each(F&& f) const\` | Traverse all elements, calling \`f(v)\` |

### Traversal and view member functions

Member function interfaces aligned with \`dense<T>\`; sparse mode automatically skips holes.

#### Traversal and reverse

| Interface | Description |
|------|------|
| \`for_each(F&& f)\` / \`for_each(F&& f) const\` | Traverse all elements, calling \`f(v)\` |
| \`reverse_for_each(F&& f)\` / \`reverse_for_each(F&& f) const\` | Reverse traversal, calling \`f(v)\` |

#### Subrange views

Return \`std::span\` views without copying data.

| Interface | Description |
|------|------|
| \`subspan(offset, count)\` | Returns a span of \`[offset, offset+count)\`, automatically truncated to \`size()\` |
| \`subspan(offset)\` | Returns a span of \`[offset, size())\` |
| \`first(n)\` | First \`n\` elements |
| \`last(n)\` | Last \`n\` elements |

All interfaces provide const overloads. Template method calls require the \`template\` keyword: \`p.first(8)\`.

#### Strided views

| Interface | Description |
|------|------|
| \`strided_span_view(start, step, count)\` | Returns \`container_strided_span<class_pool<T>>\` holding \`{container pointer, start, step, count}\`; out-of-range \`start\` resets to zero, \`step=0\` yields an empty view, \`count\` converges to the number of available slots |
| \`strided_for_each(start, step, F&& f)\` | Runtime-stride traversal, calling \`f(v)\` |
| \`strided_for_each<Step>(F&& f)\` | Compile-time-stride traversal; \`Step=1\` degenerates to \`for_each\` |

#### Transform views

| Interface | Description |
|------|------|
| \`transform_for_each(FTransform&& tr, FConsume&& con)\` | Calls \`con(tr(v))\` for each element \`v\` |
| \`transform_to(R* dst, count, F&& tr)\` | Writes \`tr(v)\` into \`dst\` |

#### Lookup and filtering

| Interface | Description |
|------|------|
| \`find(const T& value)\` | Linear search, returns pointer to first match, \`nullptr\` if not found |
| \`find_if(Pred pred)\` | Predicate search |
| \`find_if_not(Pred pred)\` | Negated predicate search |
| \`count_if(Pred pred)\` | Predicate count |
| \`filter_for_each(Pred pred, F&& f)\` | Calls \`f(v)\` only for elements satisfying \`pred(v)\` |
| \`filter_indices_to(class_pool<size_t>& dst, Pred pred)\` | Appends indices satisfying the predicate to \`dst\` |

#### Reduction and extremes

| Interface | Description |
|------|------|
| \`reduce(F&& f, U init)\` | Sequential reduction: \`acc = f(acc, v)\` |
| \`min_element()\` / \`max_element()\` | Returns pointer to the minimum/maximum element |
| \`minmax_element()\` | Returns \`{min_ptr, max_ptr}\` |
| \`sum()\` | Arithmetic sum (requires \`is_arithmetic_v<T>\`) |
| \`dot_product(const U* other, count)\` | Dot product (requires \`is_arithmetic_v<T>\`) |

#### Windows and chunks

| Interface | Description |
|------|------|
| \`for_each_window<N>(F&& f)\` | Sliding-window traversal, calling \`f(std::span<T, N>)\` for each \`[i, i+N)\` |
| \`for_each_chunk<N>(F&& f)\` | Non-overlapping chunk traversal, calling \`f(std::span<T, N>)\` for each \`[i*N, (i+1)*N)\` |
| \`window_span<N>(offset)\` | Returns the sliding-window span at offset \`offset\` (\`std::span<T>\`, truncated when fewer than \`N\`) |

#### Enumeration and two-container synchronization

| Interface | Description |
|------|------|
| \`for_each_enumerated(F&& f)\` | Traversal with indices, calling \`f(index, value)\` |
| \`for_each_zip(class_pool<U>& other, F&& f)\` | Traverses two \`class_pool\` instances in sync, calling \`f(x, y)\` |
| \`for_each_zip(U* other, count, F&& f)\` | \`class_pool\` + raw pointer synchronization |
| \`for_each_zip(std::span<U> other, F&& f)\` | span version |
| \`zip_with_to(R* dst, const U* other, count, F&& f)\` | Writes \`f(x, y)\` into \`dst\` (SoA→AoS) |

#### Equality comparison

| Interface | Description |
|------|------|
| \`equal(const T* other, count)\` | Element-wise equality comparison |
| \`equal(const class_pool<U>& other)\` | class_pool version |
| \`equal(std::span<const U> other)\` | span version |

#### Alignment and SIMD

| Interface | Description |
|------|------|
| \`simd_for_each(F&& f)\` | Traversal (requires \`is_trivially_copyable_v<T>\`, sizeof ≤ 32); sparse degenerates to \`for_each\` |
| \`unaligned_tail_offset()\` | Returns the starting offset of the tail that cannot be handled with alignment |

#### Copy and move

| Interface | Description |
|------|------|
| \`copy_to(T* dst, count)\` / \`copy_to(std::span<T> dst)\` | Bulk copy |
| \`move_to(T* dst, count)\` / \`move_to(std::span<T> dst)\` | Bulk move |
| \`reverse_copy_to(T* dst, count)\` / \`reverse_copy_to(std::span<T> dst)\` | Reverse copy |

#### class_pool-specific

| Interface | Description |
|------|------|

### Free functions

| Interface | Description |
|------|------|
| \`swap(class_pool&, class_pool&)\` | Swap two containers |

### Usage

\`\`\`cpp
class_pool<int> pool;
pool.emplace_back(10);
pool.emplace_back(20);

// emplace_back copy/move append
int v = 30;
pool.emplace_back(v);                  // append by copy
pool.emplace_back(std::move(v));       // append by move

// construct at an arbitrary position (insert-or-assign: if occupied, destroy the old object and reconstruct)
pool.emplace_at(3, 999);       // index 3 occupied, overwritten to 999
pool.emplace_at(3, 888);       // overwritten again to 888
pool.emplace_at(3, 777);  // same overwrite semantics, overwritten to 777

// sparse erase
pool.sparse_erase_at(100);
pool.is_dense();                   // false (a hole is created at index 100)

// filling the hole switches back to contiguous automatically
pool.emplace_at(100, 123);         // fill the hole
pool.is_dense();                   // true (hole gone, switched back automatically)

// unchecked fast append (caller guarantees sufficient capacity, no automatic growth)
class_pool<int> dense_pool;
dense_pool.emplace_back(1);
dense_pool.emplace_back(2);
dense_pool.emplace_back_unchecked(3);
dense_pool.emplace_back_unchecked(std::move(v));  // append by move
dense_pool.emplace_back_unchecked(4);
dense_pool.emplace_back_unchecked(5);

// bulk insert: reserve + unchecked combination
class_pool<int> batch_pool;
batch_pool.increase_capacity(1'000'000);
for (int i = 0; i < 1'000'000; ++i) {
    batch_pool.emplace_back_unchecked(i);
}

// bulk insert: fill_bulk appends in a single call
class_pool<int> fastest;
fastest.fill_bulk(0, fastest.size(), 1'000'000);  // directly append 1M zeros

// pre-grow (adds no elements)
class_pool<int> reserved;
reserved.increase_capacity(1000);    // capacity >= 1000, size unchanged
reserved.resize_fill(10, 0);   // grow size to 10 and fill with 0 (grow only, never shrink)

// positional insert
pool.emplace(std::next(pool.begin(), 1), 42);  // insert at position 1

// iteration
for (auto v : pool) { /* ... */ }

// span
std::span<int> s = pool.span();

// get(): returns nullptr when out of range or a hole
if (int* a = pool.get(0)) { *a = 42; }

// get(index, error_index): bounds-protected access
// if index >= size(), access error_index instead, avoiding out-of-bounds UB
int& b = pool.get(maybe_invalid_idx, 0);

// sparse-set queries
pool.is_constructed_at(100);  // true
pool.is_dense();              // check whether contiguous
\`\`\`

### Which operation should I use?

| Scenario | Recommended operation | Reason |
|------|---------|------|
| Append at the end (value object already available) | \`emplace_back(value)\` | O(1), stays contiguous |
| Append at the end (construct directly) | \`emplace_back(args...)\` | O(1), stays contiguous, in-place construction avoids temporaries |
| Insert/erase at an arbitrary position while staying contiguous | \`emplace(pos)\` / \`erase(pos)\` | Shifts subsequent elements, O(n), stays contiguous |
| Sparse array (large index jumps) | \`emplace_at()\` / \`sparse_erase_at()\` | O(1), moves no other elements, but creates holes |
| Bulk fill at known indices | \`emplace_at()\` | Switches back to contiguous automatically after filling holes |
| Delete the whole container | \`clear()\` | Resets to contiguous |

### What not to do

| Wrong usage | Problem | Correct usage |
|---------|------|---------|
| Expecting contiguous iteration after \`sparse_erase_at()\` | Creates a hole, iteration becomes sparse mode | Fill the hole with \`emplace_at()\`, or use \`erase()\` instead |
| Write \`emplace_back({a, b})\` | A brace list has no type, so \`Args\` cannot be deduced | Write \`emplace_back(T{a, b})\` |
| Frequent back-and-forth \`sparse_erase_at()\` + \`emplace_at()\` | Each switch triggers a mode scan | Batch operations, or consistently use \`erase()\`/\`emplace()\` to stay contiguous |
| \`emplace_at()\` at an index far beyond \`size()\` | Leaves many uninitialized slots in between, \`size()\` explodes | Pre-fill with \`resize_fill(n, value)\` |
| Using \`data()\` + \`span()\` for linear traversal in sparse mode | Uninitialized slots contain garbage data | Always traverse through iterators, or first confirm \`is_dense()\` is true |
| Expecting \`increase_capacity(n)\` to allocate exactly n | Actual capacity may be larger than n | Use \`class_pool(n)\` for exact reservation at construction, or accept rounding up |
| Expecting \`emplace_at()\` to preserve the old value | \`emplace_at\` is insert-or-assign, repeated construction destroys the old object | Check with \`is_constructed_at()\` first when preservation is needed |
| Call \`emplace_back_unchecked\` without confirming capacity | Out-of-bounds write crash (no automatic growth) | \`increase_capacity\` first, or use \`emplace_back()\` |
| Objects still occupy memory after a soft delete | \`soft_dense_delete\` retains objects without destroying them | Use \`sparse_erase_at()\` to really destroy, or \`emplace_at()\` / \`fill_the_hole()\` to reuse the hole |

### fill_the_hole — fill a hole or append

\`fill_the_hole\` fills the first hole (lowest index) when one exists, otherwise appends at the end via \`emplace_back\`.

#### Interface

| Interface | Description |
|------|------|
| \`fill_the_hole(args...)\` | Fill the first hole or append at the end, returns \`T&\` |

Hole filling relies on existing interfaces: \`sparse_erase_at\` creates holes, \`emplace_at\` fills holes, \`emplace_back\` appends.

#### Usage

\`\`\`cpp
class_pool<int> pool;
pool.fill_the_hole(10);   // index 0
pool.fill_the_hole(20);   // index 1
pool.fill_the_hole(30);   // index 2

pool.sparse_erase_at(1);  // creates a hole
pool.fill_the_hole(99);   // fills index 1 (lowest hole)

// multiple holes: fill the lowest index
pool.sparse_erase_at(0);
pool.sparse_erase_at(2);
pool.fill_the_hole(7);    // fills index 0 (lowest)
pool.fill_the_hole(8);    // fills index 2

// iteration skips holes
pool.sparse_erase_at(1);
for (int& v : pool) { /* skip index 1 */ }

// fill_the_hole: fill the first hole if any, otherwise append
class_pool<int> pool2;
pool2.fill_the_hole(10);  // no hole → append
pool2.fill_the_hole(20);  // no hole → append
pool2.sparse_erase_at(0); // creates a hole at 0
pool2.fill_the_hole(99);  // fills the hole at 0
// pool2[0] == 99
\`\`\`

#### What not to do

| Wrong usage | Problem | Correct usage |
|---------|------|---------|
| Expecting \`fill_the_hole\` to fill the most recently erased hole | Lowest index takes priority, not LIFO | Maintain your own stack if LIFO order is needed |
| Mixing in compacting \`erase(iterator)\` | Indices shift forward, changing hole positions | Use \`sparse_erase_at\` in hole-filling scenarios |
| Expecting \`is_dense()\` to be true after \`sparse_erase_at\` | A hole is created and it becomes sparse | It recovers automatically once \`fill_the_hole\` fills everything |
| Calling \`sparse_erase_at\` repeatedly on an already-erased position | Already-erased positions are not counted again | Check with \`is_constructed_at\` before erasing |

---
`
};

window.DOCS_DATA_EN['class_pool_views'] = {
  title: "container_views — Container views",
  content: `## 15.5 container_views — Container views

Generic view interfaces, in the global namespace. Constrained by the \`viewable_container\` concept, \`dense<T>\` and \`class_pool<T>\` share the same set of view functions; \`class_pool<T>\` has additional dedicated views.

- **Does not modify the original container**: relies only on the container's public API
- **Does not copy data**: views hold only pointers and lengths
- **All \`noexcept\`**: consistent with the original container constraints

### Generic view interfaces

The \`viewable_container\` concept requires the container to provide \`data()\` / \`size()\` / \`begin()\` / \`end()\` / \`operator[]\` / \`span()\` / \`rbegin()\` / \`rend()\`; both \`dense<T>\` and \`class_pool<T>\` satisfy this concept.

| Category | Interface | Description |
|------|------|------|
| **Traversal** | \`for_each(c, f)\` | Traverse all active elements, calling \`f(v)\` |
|  | \`reverse_for_each(c, f)\` | Reverse traversal, calling \`f(v)\` |
|  | \`for_each_enumerated(c, f)\` | Traversal with indices, calling \`f(index, v)\` |
| **Subrange** | \`subspan(c, off, cnt)\` / \`subspan(c, off)\` | Returns \`std::span<T>\`, automatically truncated to \`size()\` (naming consistent with \`dense::subspan\` / \`std::span::subspan\`) |
|  | \`first(c, n)\` / \`last(c, n)\` | Span of the first/last \`n\` elements |
| **Strided** | \`strided_span_view(c, start, step, cnt)\` | Returns \`container_strided_span<C>\` (shared by both containers), converging to the container's actual range (out-of-range \`start\` / \`step=0\` yields an empty view) |
|  | \`strided_for_each(c, start, step, f)\` | Runtime-stride traversal |
|  | \`strided_for_each<Step>(c, f)\` | Compile-time-stride traversal; \`Step=1\` degenerates to \`for_each\` |
| **Transform** | \`transform_for_each(c, tr, con)\` | Calls \`con(tr(v))\` for each \`v\` |
|  | \`transform_to(c, dst, n, tr)\` | Writes \`tr(v)\` into \`dst\`; requires \`n <= c.size()\`, and \`dst\` must be unconstructed memory |
| **Find/filter** | \`find(c, v)\` / \`find_if(c, pred)\` / \`find_if_not(c, pred)\` | Linear search, returns pointer to first match, \`nullptr\` if not found |
|  | \`count_if(c, pred)\` | Conditional count |
|  | \`filter_for_each(c, pred, f)\` | Calls \`f(v)\` only for elements satisfying \`pred(v)\` |
| **Reduction** | \`reduce(c, f, init)\` | Sequential reduction: \`acc = f(acc, v)\` |
|  | \`min_element(c)\` / \`max_element(c)\` / \`minmax_element(c)\` | Returns extreme-value pointers |
|  | \`sum(c)\` | Arithmetic sum (requires \`is_arithmetic_v<T>\`) |
|  | \`dot_product(c, other, n)\` | Dot product (requires \`is_arithmetic_v<T>\`) |
| **Windows/chunks** | \`for_each_window<N>(c, f)\` | Sliding-window traversal, calling \`f(std::span<T, N>)\` for each \`[i, i+N)\` |
|  | \`for_each_chunk<N>(c, f)\` | Non-overlapping chunk traversal |
|  | \`window_span<N>(c, off)\` | Returns the sliding-window span (\`std::span<T>\`, truncated when fewer than \`N\`) |
| **Two containers** | \`for_each_zip(a, b, f)\` | Synchronized traversal of two containers, calling \`f(x, y)\` |
|  | \`for_each_zip(a, ptr, n, f)\` | Container + raw pointer synchronization |
|  | \`zip_with_to(a, b, dst, n, f)\` | Writes \`f(x, y)\` into \`dst\` (\`dst\` must be unconstructed memory) |
|  | \`equal(a, b)\` / \`equal(a, ptr, n)\` / \`equal(a, span)\` | Equality comparison |
| **Alignment** | \`simd_for_each(c, f)\` | Traversal (requires \`is_trivially_copyable_v<T>\` and \`sizeof(T) ≤ 32\`) |
|  | \`unaligned_tail_offset(c)\` | Tail offset after 32-byte-aligned processing |
| **Copy/move** | \`copy_to(c, dst, n)\` / \`copy_to(c, span)\` | Bulk copy (\`dst\` must be unconstructed memory) |
|  | \`move_to(c, dst, n)\` / \`move_to(c, span)\` | Bulk move (\`dst\` must be unconstructed memory) |
|  | \`reverse_copy_to(c, dst, n)\` / \`reverse_copy_to(c, span)\` | Reverse copy (\`dst\` must be unconstructed memory) |

### class_pool-specific views

View interfaces specific to \`class_pool<T>\`.

| Interface | Description |
|------|------|
| \`filter_indices_to(pool, dst, pred)\` | Appends indices satisfying \`pred(v)\` to \`class_pool<size_t>& dst\` |

### Usage example

\`\`\`cpp
// view interfaces live in the global namespace, no using namespace needed

class_pool<POD32> pool;
for (size_t i = 0; i < 1000; ++i) pool.emplace_back_unchecked({static_cast<float>(i)});

// subrange traversal
auto sp = subspan(pool, 100, 50);
for (auto& v : sp) { /* ... */ }

// compile-time-stride traversal (stride 4)
strided_for_each<4>(pool, [](POD32& v) { /* ... */ });

// find and count
POD32 target{42.0f};
POD32* p = find(pool, target);
size_t n = count_if(pool, [](const POD32& v) { return v.a[0] > 0; });

// reduction
POD32 sum = reduce(pool, [](POD32 acc, const POD32& v) -> POD32 { /* ... */ }, POD32{});

// compact a sparse pool into a dense array
pool.sparse_erase_at(5);
POD32* compact = /* ... allocate memory ... */;
copy_to(pool, compact, pool.size());
// compact[0..pool.count()) holds the contiguous active elements

// dense works the same way
dense<float> d(1000, 0.0f);
for (size_t i = 0; i < 1000; ++i) d[i] = static_cast<float>(i);
d.for_each([](float& v) { v *= 2.0f; });
strided_for_each<4>(d, [](float& v) { /* ... */ });
\`\`\`

### Sparse-mode behavior

View behavior for \`class_pool<T>\` in sparse mode (\`is_dense() == false\`):
- **Subrange slicing** (\`subspan\` / \`first\` / \`last\` / \`window_span\`): slices directly, **does not skip holes**; the caller must check \`is_constructed_at(i)\` itself
- **Window/chunk traversal** (\`for_each_window\` / \`for_each_chunk\`): skips a window or chunk when it contains a hole
- **Strided traversal**: automatically skips over holes
- **Filter/find/reduce/zip/\`simd_for_each\`**: **automatically skips holes** (traverses active elements only)
- **\`copy_to\`**: compacts into \`dst\` in active order (\`dst\` must be unconstructed memory)

### What not to do

| Wrong usage | Problem | Correct usage |
|---------|------|---------|
| Traversing directly after \`subspan\` in sparse mode | Includes inactive slots | Use \`for_each\` / \`filter_for_each\` to go through iterators |
| Using \`simd_for_each\` in sparse mode | Degenerates to \`for_each\` | Only applies to dense mode |
| Modifying the container while a view is held | Iterators/pointers become invalid | View lifetime = container stability period |
| Expecting \`last<N>\` to return an empty span when out of range | Returns a dynamic \`std::span<T>\`, truncated to the actual length when fewer than \`N\` | Check \`span.size()\` before use |

---
`
};

window.DOCS_DATA_EN['ring_buffer'] = {
  title: "ring_buffer — Ring buffer",
  content: `## 27. ring_buffer — Ring buffer

Global namespace. \`noexcept\`.

Template parameters \`T\` (element type) and \`N\` (capacity, rounded up to a power of two for the real capacity). Fixed capacity; when full, behavior depends on the writing interface: the \`try_*\` family rejects, the \`*_overwrite\` family overwrites the oldest. Not copyable, movable (the source is empty after a move).

### Interfaces

| Interface | Description |
|------|------|
| \`ring_buffer()\` | Default constructor |
| \`ring_buffer(ring_buffer&&)\` / \`operator=(ring_buffer&&)\` | Move construction/assignment; the source object is empty after a move |
| \`try_push(const T&)\` / \`try_push(T&&)\` | Enqueue, rejects when full (returns false, queue unchanged) |
| \`try_emplace(args...)\` | Construct in place and enqueue, rejects when full (returns nullptr) |
| \`push_overwrite(const T&)\` / \`push_overwrite(T&&)\` | Enqueue, overwrites the oldest when full; returns the number of items overwritten this time (0 or 1) |
| \`emplace_overwrite(args...)\` | Construct in place and enqueue, overwrites the oldest when full; returns the new slot pointer (never null) |
| \`front()\` | Peeks at the queue head, returns nullptr when empty (\`const\` overload returns \`const T*\`) |
| \`pop()\` | Pops and destroys the queue head, returns false when empty |
| \`try_pop(T& out)\` | Pops the queue head and move-assigns it to \`out\`, returns false when empty |
| \`drain(handler)\` | Consumes one by one until empty, callback signature \`void(T&)\`, returns the number consumed |
| \`drain_with_budget(budget, handler)\` | Consumes at most \`budget\` items, keeps the rest, returns the number consumed |
| \`visit(handler) const\` | Read-only traversal (no consumption), callback signature \`void(const T&)\`, returns the number visited |
| \`front_run(T*& out_ptr, size_t& out_len)\` | Returns the contiguous run at the queue head (from the read position to the end of the run) for batch processing; returns 0 when empty |
| \`pop_run(size_t n)\` | Batch pops the first \`n\` items and destroys them, returns the actual number popped (excess is truncated) |
| \`size()\` / \`empty()\` | Number of elements / whether empty |
| \`capacity()\` | Real capacity (static constexpr) |
| \`dropped_count()\` | Cumulative number of items evicted by overwriting |
| \`clear()\` | Clears and destroys all elements (buffer retained) |

### Usage

\`\`\`cpp

struct event { int type; int data; };
ring_buffer<event, 1024> buf;

// reject when full
if (!buf.try_push({1, 100})) { /* queue is full */ }

// overwrite the oldest when full (keep the latest N semantics)
buf.push_overwrite({2, 200});
event* slot = buf.emplace_overwrite();
slot->type = 3;

// consume
event out;
if (buf.try_pop(out)) { /* out is the queue head */ }

size_t n = buf.drain([](event& e) {
    std::cout << "type=" << e.type << " data=" << e.data << "\\n";
});

// read-only peek (no consumption)
buf.visit([](const event& e) { std::cout << e.type << "\\n"; });

// batch run processing
event* run = nullptr;
size_t len = 0;
while (buf.front_run(run, len) > 0)
{
    for (size_t i = 0; i < len; ++i) { /* process run[i] in batch */ }
    buf.pop_run(len);
}

// move semantics
ring_buffer<event, 1024> buf2(std::move(buf));
// buf is now empty

// capacity and statistics
size_t cap = ring_buffer<event, 1024>::capacity();  // 1024
uint64_t dropped = buf2.dropped_count();
\`\`\`

### Notes

| Wrong usage | Problem | Correct usage |
|---------|------|---------|
| Expecting "fail when full" after using \`push_overwrite\` | Evicts the oldest element when full | Use \`try_push\`/\`try_emplace\` when failure-on-full is needed |
| Calling \`*_overwrite\` inside the \`drain\` handler to enqueue into this queue | Overwriting evicts the oldest not-yet-consumed element | Use only \`try_push\`/\`try_emplace\` inside the handler, or control manually with \`pop()\` first |
| Modifying this queue inside the \`visit\` handler | The traversal state is corrupted | Separate viewing from modification: \`visit\` first, then modify |
| Relying on the \`front()\` pointer being valid after \`pop\` | \`pop\` advances the read position, invalidating the pointer | Copy first or move out with \`try_pop\` |
| Copy-constructing a \`ring_buffer\` | Not copyable | Use a move or refill |

---
`
};

window.DOCS_DATA_EN['signal_queue'] = {
  title: "signal_queue — Signal and change-log queue",
  content: `## signal_queue — Signal and change-log queue

No namespace. All interfaces are \`noexcept\`. Two-level queue: a fixed-capacity ring buffer plus an overflow buffer (a fallback when the main ring is full), so enqueueing never drops events. Used for unified enqueueing and end-of-frame draining of entity signals, component signals, and change logs.

### Template parameters

| Parameter | Description |
|------|------|
| \`Event\` | Event type, must be copy-constructible |
| \`Capacity\` | Ring buffer capacity (rounded up to a power of two) |

### Interfaces

| Interface | Description |
|------|------|
| \`push(const Event&)\` | Enqueue; no effect while disabled; when the main ring is full or the overflow has not been drained, appends to the overflow buffer, preserving FIFO order |
| \`flush(handler)\` | Drains pending events, callback signature \`void(const Event&)\` (also accepts \`void(Event&)\`) |
| \`has_pending() const\` | Whether pending events exist (including the unconsumed part of the overflow buffer) |
| \`pending_count() const\` | Number of pending events (main ring + overflow remainder) |
| \`set_enabled(bool)\` / \`enabled() const\` | Sets/queries the enqueue switch |

### Drain semantics

- The total budget for a single \`flush\` is \`Capacity × 4\`, shared by the main ring and the overflow; the excess is retained until the next \`flush\`
- The main ring is consumed before the overflow, so the whole queue preserves FIFO order
- Events appended inside \`handler\` are consumed within the budget and are not lost
- \`flush\` reentrancy (calling \`flush\` again inside \`handler\`) is blocked by a guard; nested calls do nothing

### Usage

\`\`\`cpp
struct my_event { uint32_t type; uint32_t index; };

signal_queue<my_event, 1024> q;
q.push(my_event{0, 7});
q.flush([](const my_event& ev) { /* ... */ });

// drain in a loop after a burst backlog
while (q.has_pending())
{
    q.flush([](const my_event& ev) { /* ... */ });
}

// disable enqueue / re-enable
q.set_enabled(false);
q.set_enabled(true);
\`\`\`

### What not to do

| Wrong usage | Problem | Correct usage |
|---------|------|---------|
| Relying on a single \`flush\` to empty an oversized queue | Events beyond the budget are retained | Call \`flush\` in a loop until \`has_pending()\` is false |
| Copying a \`signal_queue\` | Copying is forbidden | Use move semantics |
`
};

window.DOCS_DATA_EN['multi_block_bitmask'] = {
  title: "multi_block_bitmask — Multi-block bitmask storage",
  content: `## 29. multi_block_bitmask — Multi-block bitmask storage

No namespace. A multi-block bitmask container, suitable for bloom filters, sparse sets, component tags, attribute bitmaps, and similar scenarios.

### Interfaces

#### Static helpers

| Interface | Description |
|------|------|
| \`static constexpr uint32_t bits_per_block\` | Bits per block (64) |
| \`static constexpr uint32_t block_count_for_bits(size_t bit_count)\` | Computes the required block count for a given bit count |

#### Block management

| Interface | Description |
|------|------|
| \`reserve_blocks(uint32_t num_blocks)\` | Pre-allocates the number of mask blocks (grow only, never shrink) |
| \`num_blocks() const\` | Current number of mask blocks per slot (= 1 + overflow_block_count_) |
| \`overflow_entity_count() const\` | Number of slots with overflow allocated |

#### Capacity management

| Interface | Description |
|------|------|
| \`ensure_entity(uint32_t slot)\` | Ensures slot capacity (grows automatically when needed) |
| \`resize_entities(uint32_t new_count)\` | Bulk-grows to \`new_count\` slots |
| \`increase_capacity(size_t new_slot_capacity)\` | Grows to the specified slot capacity (grow only, never shrink, \`size\` unchanged) |
| \`reduce_capacity(size_t new_slot_capacity)\` | Shrinks to the specified capacity (excess is truncated) |
| \`clear()\` | Clears all data (\`size=0\`, \`capacity\`/\`num_blocks\` retained) |
| \`size() const\` | Current number of slots |
| \`capacity() const\` | Slot capacity |
| \`empty() const\` | Whether empty |
| \`size_bytes() const\` | Used memory (bytes) |
| \`capacity_bytes() const\` | Capacity memory (bytes) |

#### Single-bit writes (with bounds checking)

| Interface | Description |
|------|------|
| \`set_bit(slot, block_idx, bit_offset)\` | Sets a bit (grows the slot automatically when needed) |
| \`clear_bit(slot, block_idx, bit_offset)\` | Clears a bit |
| \`clear_entity(slot)\` | Clears all blocks of this slot |

#### Single-bit writes (no bounds checking)

The caller guarantees \`slot < size()\` and \`block_idx < num_blocks()\`.

| Interface | Description |
|------|------|
| \`set_bit_no_check(slot, block_idx, bit_offset)\` | Sets a bit |
| \`clear_bit_no_check(slot, block_idx, bit_offset)\` | Clears a bit |

#### Whole-block writes

Replacing read-modify-write by writing the whole block value directly.

| Interface | Description |
|------|------|
| \`set_block_value(slot, block_idx, uint64_t value)\` | Assigns the whole block |
| \`or_block_value(slot, block_idx, uint64_t mask)\` | In-place OR |
| \`and_block_value(slot, block_idx, uint64_t mask)\` | In-place AND |
| \`xor_block_value(slot, block_idx, uint64_t mask)\` | In-place XOR |

#### Batch bit operations (multiple bits in the same block)

| Interface | Description |
|------|------|
| \`set_bits_at(slot, block_idx, std::span<const uint32_t> offsets)\` | Sets multiple bits in batch |
| \`clear_bits_at(slot, block_idx, std::span<const uint32_t> offsets)\` | Clears multiple bits in batch |
| \`toggle_bits_at(slot, block_idx, std::span<const uint32_t> offsets)\` | Toggles multiple bits in batch |

#### Whole-slot multi-block read/write

| Interface | Description |
|------|------|
| \`assign_slot(slot, std::span<const uint64_t> data)\` | Writes all blocks of the whole slot (\`data[0]\`→block 0, \`data[1]\`→block 1, …; automatically calls \`reserve_blocks\` if \`data.size() > num_blocks()\`) |
| \`copy_slot_to(slot, std::span<uint64_t> dst) const\` | Reads all blocks of the whole slot into \`dst\` (\`dst.size()\` determines the number of blocks read, the rest is zero-filled) |

#### Query interfaces

| Interface | Description |
|------|------|
| \`get_block(slot, block_idx) const\` | Gets the block value (returns 0 when out of range) |
| \`test_bit(slot, block_idx, bit_offset) const\` | Tests whether the bit is set |
| \`any_set_in_block(slot, block_idx) const\` | Whether the block is non-zero |
| \`any_set(slot) const\` | Whether any bit in the slot is set (including overflow) |
| \`is_zero(slot) const\` | Whether the slot is all zeros |
| \`count_set_bits(slot) const\` | Number of set bits in the slot (including overflow, based on \`std::popcount\`) |
| \`slot_satisfies(slot, req, exc, block_count) const\` | Whether the slot mask satisfies: contains all set bits of \`req\` and has no intersection with \`exc\`; \`req\`/\`exc\` are mask arrays expanded by block, \`block_count\` is the number of blocks involved in the comparison |
| \`find_first_set(slot, out_block, out_offset) const\` | Finds the first set bit, writes \`out_block\`/\`out_offset\`, returns whether one was found |
| \`find_last_set(slot, out_block, out_offset) const\` | Finds the last set bit |
| \`find_next_set(slot, after_block, after_offset, out_block, out_offset) const\` | Finds the next set bit after the specified position |

#### Traversal interfaces

Callbacks are constrained by \`std::invocable\` concepts, catching signature errors at compile time.

| Interface | Callback signature | Description |
|------|---------|------|
| \`for_each_set_bit(slot, func) const\` | \`func(uint32_t block_idx, uint32_t bit_offset)\` | Traverses all set bits of this slot |
| \`for_each_set_slot(func) const\` | \`func(uint32_t slot)\` | Traverses all non-empty slots |
| \`for_each_set_bit_global(func) const\` | \`func(uint32_t slot, uint32_t block_idx, uint32_t bit_offset)\` | Globally traverses all set bits |
| \`count_set_bits_global() const\` | — | Total number of set bits globally |

#### View interfaces

| Interface | Description |
|------|------|
| \`inline_span() noexcept\` / \`inline_span() const noexcept\` | Returns the global view of block 0: \`std::span<uint64_t>\` / \`std::span<const uint64_t>\` |
| \`overflow_span(slot) noexcept\` / \`overflow_span(slot) const noexcept\` | Returns the overflow block view of a slot (block 1+), or an empty span if not allocated |

#### Copy and swap

| Interface | Description |
|------|------|
| \`multi_block_bitmask(const multi_block_bitmask&)\` | Deep-copy constructor |
| \`operator=(const multi_block_bitmask&)\` | Deep-copy assignment |
| \`multi_block_bitmask(multi_block_bitmask&&)\` | Move constructor |
| \`operator=(multi_block_bitmask&&)\` | Move assignment |
| \`swap(multi_block_bitmask&) noexcept\` | Member swap |
| \`clone() const\` | Explicit deep-copy factory |
| Free function \`swap(a, b) noexcept\` | Free swap |

#### Set operations (in place, handling common slots and common blocks)

| Interface | Description |
|------|------|
| \`and_with(const multi_block_bitmask& o)\` | \`this &= o\` (slots absent in \`o\` are cleared in \`this\`) |
| \`or_with(const multi_block_bitmask& o)\` | \`this \\|= o\` (only common slots are processed, no growth) |
| \`xor_with(const multi_block_bitmask& o)\` | \`this ^= o\` |
| \`subtract(const multi_block_bitmask& o)\` | \`this &= ~o\` (difference set) |
| \`overlaps(const multi_block_bitmask& o) const\` | Whether there is any common set bit with \`o\` |
| \`equals(const multi_block_bitmask& o) const\` | Whether equal |

#### Memory compaction

| Interface | Description |
|------|------|
| \`compact_slot(slot)\` | Releases this slot's overflow if it is all zeros |
| \`compact_all()\` | Globally compacts all all-zero overflow blocks |

### Usage

\`\`\`cpp
multi_block_bitmask masks;
masks.reserve_blocks(2);                    // pre-allocate 2 blocks = supports 128 bits
masks.ensure_entity(0);
masks.set_bit(0, 0, 5);                     // slot 0, block 0, bit 5
masks.set_bit(0, 1, 10);                    // slot 0, block 1, bit 10
uint64_t b0 = masks.get_block(0, 0);
uint64_t b1 = masks.get_block(0, 1);
masks.clear_entity(0);                      // clear the whole slot

// queries
bool has = masks.test_bit(0, 0, 5);
bool any = masks.any_set(0);
uint32_t cnt = masks.count_set_bits(0);
uint32_t blk, off;
if (masks.find_first_set(0, blk, off)) { /* ... */ }

// batch bit operations
uint32_t offsets[] = {1, 3, 5, 7};
masks.set_bits_at(0, 0, offsets);

// whole-block writes
masks.set_block_value(0, 0, 0xFFFFFFFFULL);
masks.or_block_value(0, 0, 0x1ULL << 10);

// whole-slot read/write
uint64_t data[] = {0xFF, 0xAA};
masks.assign_slot(0, data);
uint64_t out[2];
masks.copy_slot_to(0, out);

// views
std::span<const uint64_t> inline_view = masks.inline_span();
std::span<const uint64_t> ovf_view = masks.overflow_span(0);

// traversal
masks.for_each_set_bit(0, [](uint32_t blk, uint32_t off) { /* ... */ });
masks.for_each_set_slot([](uint32_t slot) { /* ... */ });
masks.for_each_set_bit_global([](uint32_t slot, uint32_t blk, uint32_t off) { /* ... */ });
size_t total = masks.count_set_bits_global();

// set operations
multi_block_bitmask a, b;
a.reserve_blocks(2); b.reserve_blocks(2);
a.ensure_entity(0); b.ensure_entity(0);
a.set_bit(0, 0, 1); b.set_bit(0, 0, 2);
a.or_with(b);                             // a is now = {1, 2}
bool inter = a.overlaps(b);
bool sup = a.contains_all(b);
bool eq = a.equals(b);

// copy and swap
multi_block_bitmask c = a.clone();
multi_block_bitmask d = std::move(c);
d.swap(a);
swap(d, a);                               // free swap

// memory compaction
masks.clear_bit(0, 1, 10);
masks.compact_slot(0);                     // release if overflow is all zeros
masks.compact_all();                       // global compaction

// grow / shrink / state queries (naming consistent with class_pool / dense)
masks.increase_capacity(50000);
size_t cap = masks.capacity();
size_t n   = masks.size();
masks.reduce_capacity(masks.size());
masks.reduce_capacity(1000);
masks.clear();
bool isEmpty = masks.empty();
size_t used  = masks.size_bytes();
size_t total_cap = masks.capacity_bytes();
size_t ovf_count = masks.overflow_entity_count();

// static helpers
constexpr uint32_t blk = multi_block_bitmask::block_count_for_bits(200);  // = 4
\`\`\`

### Recommended usage for bulk registration

Calling \`ensure_entity\` one by one performs bounds checking + size reads + possible growth every time. Use the three-step approach for bulk registration to reduce repeated checks:

\`\`\`cpp
multi_block_bitmask masks;
masks.reserve_blocks(2);                   // step 0: pre-allocate block count (must precede step 1)

// three-step bulk registration of N slots
masks.increase_capacity(N);                // step 1: grow capacity
masks.resize_entities(N);                  // step 2: extend size
for (uint32_t i = 0; i < N; ++i)
{
    masks.set_bit_no_check(i, 0, i & 63);  // step 3: write without bounds checking
}
\`\`\`

| Step | Effect | Consequence of skipping |
|------|------|----------|
| \`increase_capacity(N)\` | Reserves capacity | Grows on demand when not reserved |
| \`resize_entities(N)\` | Extends \`size()\` to N, making \`set_bit_no_check\` accesses valid | Out-of-bounds write crash in \`set_bit_no_check\` |
| \`set_bit_no_check(...)\` | Write that skips bounds checking | Falls back to the checked path of \`set_bit\` |

> Note: \`increase_capacity\` only grows capacity and does not touch size, while \`set_bit_no_check\` requires \`slot < size()\`.
> Therefore steps 1 and 2 **are both indispensable**, otherwise \`set_bit_no_check\` is out of bounds.

### What not to do

| Wrong usage | Problem | Correct usage |
|---------|------|---------|
| Calling \`reserve_blocks\` after registering components | Triggers growth of existing overflow slots | Estimate the type count at startup and call \`reserve_blocks\` before registration |
| Calling \`set_bit_no_check\` on a slot not passed through \`ensure_entity\` | Out-of-bounds write crash | \`ensure_entity\` first, or use the \`increase_capacity\` + \`resize_entities\` three-step approach for bulk scenarios |
| Calling \`set_bit_no_check\` directly after \`increase_capacity(N)\` | Capacity grew but size was not extended, out-of-bounds write crash | \`resize_entities(N)\` must also be called to extend size |
| Calling \`set_bit\` with \`block_idx >= num_blocks()\` | Silently dropped (block count is not grown) | \`reserve_blocks\` first to grow |
| Assuming \`get_block(slot, b)\` is stable across processes | Layout may change | Only valid within the current process |
| Passing a value smaller than the current \`size\` to \`reduce_capacity(n)\` | Excess elements are truncated and lost | Confirm \`n >= size()\` before shrinking, or call \`reduce_capacity(size())\` first |
| Assuming \`num_blocks()\` resets to 1 after \`clear()\` | \`clear\` only clears data, it does not reset the block count | Reconstruct the instance to reset the block count |
| Assuming \`this\`'s \`num_blocks()\` matches \`o\`'s after set operations | Set operations do not grow the block count and handle common blocks only | Call \`reserve_blocks\` first when growth is needed |
| Using the span returned by \`overflow_span(slot)\` across \`reserve_blocks\` | \`reserve_blocks\` reallocates overflow memory, invalidating the span | Use the view immediately, do not hold it across write operations |

---
`
};
