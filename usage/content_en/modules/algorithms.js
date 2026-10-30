window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['tiered_sort'] = {
  title: "tiered_sort — Tiered sorting",
  content: `## 21. tiered_sort / pdqsort / sort_n — Tiered sorting

Global namespace. All functions are \`noexcept\`.

### Interface

| Interface | Signature | Description |
|------|------|------|
| \`pdqsort\` | \`void pdqsort<T>(T* data, size_t n, Compare&& cmp)\` | 3-way pdqsort, requires \`is_trivially_copyable_v<T>\` |
| \`tiered_sort\` | \`void tiered_sort<T>(T* data, size_t n, Compare&& cmp)\` | Tiered sort of a value array, ascending by \`cmp\` |
| \`tiered_sort_indices\` | \`void tiered_sort_indices<T>(size_t* indices, const T* values, size_t n)\` | Index sort, orders \`indices\` ascending by \`values[indices[i]]\` |
| \`sort_n\` | \`void sort_n<N, T>(T* data, Compare&& cmp)\` | Sort with N known at compile time, uses a sorting network when N≤16 |
| \`sort_indices_n\` | \`void sort_indices_n<N, T>(size_t* indices, const T* values)\` | Index sort with N known at compile time |
| \`sort\` | \`void sort<T>(T* data, size_t n, Compare&& cmp)\` | Alias of \`tiered_sort\` |
| \`sort_indices\` | \`void sort_indices<T>(size_t* indices, const T* values, size_t n)\` | Alias of \`tiered_sort_indices\` |

### Tiering strategy

| Data size n | tiered_sort | tiered_sort_indices |
|----------|-------------|---------------------|
| n < 2 | Return immediately | Return immediately |
| 2 ≤ n ≤ 16 | Sorting network (Batcher odd-even merge, generated at compile time) | Sorting network |
| 16 < n < 32 | Insertion sort | Insertion sort |
| 32 ≤ n < 1024 | 3-way pdqsort | 3-way pdqsort |
| 1024 ≤ n < 1M | Radix sort 3-pass + keys scatter + 4-way sub-histogram | Radix sort 3-pass |
| 1M ≤ n < 5M | Radix sort 3-pass + keys recompute + NT store | Radix sort 3-pass + NT store |
| n ≥ 5M | Radix sort 2-pass (16-16) + NT store + 8-way sub-histogram | Radix sort 2-pass + NT store |

- Sorting networks use \`constexpr\` to generate Batcher odd-even merge networks at compile time, with no branches and no loops at runtime
- \`sort_n<N>\` uses a sorting network when N≤16
- Radix sort automatically falls back to pdqsort when allocation fails
- \`pdqsort\` uses 3-way Dutch National Flag partitioning, handling duplicate keys efficiently

### Usage

\`\`\`cpp

// Runtime n: tiered sort
int data[] = {5, 3, 1, 4, 2};
tiered_sort(data, 5, std::less<int>{});
// data: {1, 2, 3, 4, 5}

// Compile-time N: sorting network
sort_n<5>(data);  // N=5 is known, expand the sorting network directly

// Index sort
size_t indices[] = {0, 1, 2, 3, 4};
float values[] = {5.0f, 3.0f, 1.0f, 4.0f, 2.0f};
tiered_sort_indices(indices, values, 5);
// indices: {2, 4, 1, 3, 0}  (ascending by values)

// Compile-time N index sort
sort_indices_n<5>(indices, values);

// Direct pdqsort
pdqsort(data, 5, std::less<int>{});
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Calling \`pdqsort\` on a non trivially_copyable type | Compile error (concept constraint) | Use \`tiered_sort\` for complex types (no such constraint) |
| Passing a non-arithmetic type T to \`tiered_sort_indices\` | Non-arithmetic types fall back to pdqsort and cannot use radix sort | Ensure T is an integer or floating point type if radix sort is needed |
| \`data\` or \`indices\` is a null pointer with n > 0 | Undefined behavior | Ensure n == 0 or the pointer is valid |
| Concurrent read and write of the same array during sorting | Data race | Access the array only after sorting completes |
| Passing N=0 to \`sort_n<N>\` | Returns at compile time, no operation | Ensure the actual array size is ≥ N |

---
`
};

window.DOCS_DATA_EN['radix_sort'] = {
  title: "radix_sort — Radix sort",
  content: `## 22. radix_sort — Radix sort

Global namespace. All functions are \`noexcept\`.

### Interface

| Interface | Signature | Description |
|------|------|------|
| \`is_radix_sortable_v<T>\` | concept | \`is_integral_v<T> \|\| is_floating_point_v<T>\`, whether T is radix sortable |
| \`radix_key(T val)\` | \`auto\` | Converts T into an order-preserving unsigned value (negatives flipped) |
| \`radix_sort_entries\` | \`void radix_sort_entries<KeyType>(void* entries, size_t n)\` | Sorts a \`{KeyType key; size_t index;}\` array ascending by key |
| \`radix_sort_indices\` | \`void radix_sort_indices<KeyType>(size_t* indices, const KeyType* keys, size_t n, size_t* temp_buf)\` | Index radix sort, ascending by keys[indices[i]] |

### radix configuration

| Type | Passes | Bits per pass | Total buckets |
|------|------|----------|--------|
| uint32_t / float | 3 | 11-11-10 | 2048+2048+1024 |
| uint64_t / double | 6 | 11-11-11-11-11-9 | 5×2048+512 |

### Usage

\`\`\`cpp

// entries sort: struct { int key; size_t index; }
struct entry { int key; size_t index; };
entry entries[5] = {{5,0},{3,1},{1,2},{4,3},{2,4}};
radix_sort_entries<int>(entries, 5);
// entries ascending by key

// Index sort
size_t indices[] = {0, 1, 2, 3, 4};
float values[] = {5.0f, 3.0f, 1.0f, 4.0f, 2.0f};
size_t temp[5];
radix_sort_indices<float>(indices, values, 5, temp);
// indices: {2, 4, 1, 3, 0}
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Calling \`radix_sort_*\` on non-arithmetic/floating point types | Compile error (concept constraint) | Use only with \`int\`/\`uint\`/\`float\`/\`double\` |
| Passing an empty \`temp_buf\` to \`radix_sort_indices\` | Writes to invalid memory | Allocate a temp buffer of \`n * sizeof(size_t)\` bytes |
| entries of \`radix_sort_entries\` do not have the \`{KeyType key; size_t index;}\` layout | Incorrect memory interpretation | Ensure the first struct field is KeyType and the second is size_t |
| Calling when n = 0 | Returns safely (short-circuits when n ≤ 1) | No problem, but pointless |

---
`
};
