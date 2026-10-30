window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['operating_message'] = {
  title: "operating_message — Error object",
  content: `## 14. operating_message — Error object

One object per call, describing only the most recent failure.

- 2 bytes, trivially copyable, no allocation on the failure path
- 14-bit error code, returned by \`code()\`, 0 means success
- 2-bit level, returned by \`level()\`: debug / info / warn / error

### msg_level

\`\`\`cpp
enum class msg_level : uint8_t {
    debug = 0,
    info  = 1,
    warn  = 2,
    error = 3
};
\`\`\`

### Interfaces

| Interface | Description |
|------|------|
| \`ok()\` / \`operator bool()\` | Whether it succeeded |
| \`code()\` | Error code, 0 on success |
| \`is_code(c)\` | Test the error code |
| \`level()\` | Level of this failure |
| \`write_failure(code, level)\` | Record a failure, keeping the first failure |
| \`write(false, code, level)\` | Tagged failure write |
| \`check(cond, ...)\` | Write and return false when the condition does not hold |
| \`operator+=(other)\` | Merge, keeping the first failure |
| \`reset()\` | Restore to success |

### Usage

\`\`\`cpp
const operating_message result = manager.add(e, position{1.0f, 2.0f, 3.0f});
if (!result)
{
    std::string_view name = om_error_name(result.code());
    std::fprintf(stderr, "add failed: %.*s level=%d\\n",
                 static_cast<int>(name.size()), name.data(),
                 static_cast<int>(result.level()));
}
\`\`\`

### Error codes

| Constant | Meaning |
|------|------|
| \`om_err_type_mismatch\` | Type mismatch |
| \`om_err_invalid_entity\` | Invalid entity |
| \`om_err_version_mismatch\` | Version mismatch |
| \`om_err_out_of_range\` | Index out of range |
| \`om_err_null_pointer\` | Null pointer |
| \`om_err_capacity_exceeded\` | Capacity exceeded |
| \`om_err_not_found\` | Not found |
| \`om_err_already_exists\` | Already exists |
| \`om_err_out_of_memory\` | Memory allocation failed |
| \`om_err_invalid_argument\` | Invalid argument |
| \`om_err_unsupported_operation\` | Not supported in the current mode |
| \`om_err_cancelled\` | Cancelled |
| \`om_err_timeout\` | Wait timed out |
| \`om_err_not_running\` | Target not started |
| \`om_err_shutting_down\` | Shutting down |
| \`om_err_queue_full\` | Queue full |
| \`om_err_decode_failed\` | Decode failed |
| \`om_err_io_failed\` | IO failed |
| \`om_err_checksum_failed\` | Checksum failed |
| \`om_err_spawn_failed\` | Failed to create the execution body |

Custom error codes are generated with \`om_error_code("my_error")\`; the caller stores the name itself.

### First-failure helpers

Chain and pipeline executions share one error object per run and keep only the first failure:

\`\`\`cpp
operating_message error;
if (!stage1(in, mid))
{
    return false;
}
if (!stage2(mid, out))
{
    return false;
}
\`\`\`

| Interface | Description |
|------|------|
| \`om_write_first_failure(dst, failed, level, code, site)\` | Write the first failure when the destination is successful and return true; return false on failure or an empty destination |
| \`om_merge_first_failure(dst, src)\` | Copy the first failure of src when the destination is successful; does not overwrite an already failed destination |

### Name lookup

| Interface | Description |
|------|------|
| \`om_error_name(code)\` | Look up a name in the compile-time catalog; returns \`"unknown"\` if missing |
| \`om_error_desc(code)\` | Look up a description in the compile-time catalog |

### Usage notes

| Note | Description |
|--------|------|
| No concurrent writes to the same object | One object is used in one thread only |
| No location information | Troubleshooting relies on error code names and the call stack |
| No allocation on the failure path | No heap allocation at all |
| No global state | Statistics, deduplication and recovery policies are implemented with caller-owned containers |
`
};

window.DOCS_DATA_EN['force_inline'] = {
  title: "FORCE_INLINE / NOINLINE — Cross-platform inline macros",
  content: `## 23. FORCE_INLINE / NOINLINE — Cross-platform inline macros


| Macro | Description |
|------|------|
| \`FORCE_INLINE\` | Force function inlining, adapted across compilers |
| \`NOINLINE\` | Forbid function inlining, adapted across compilers |

| Compiler | \`FORCE_INLINE\` expands to | \`NOINLINE\` expands to |
|--------|----------------------|------------------|
| MSVC | \`__forceinline\` | \`__declspec(noinline)\` |
| GCC / Clang | \`inline __attribute__((always_inline))\` | \`__attribute__((noinline))\` |
| Other | \`inline\` | (empty) |

### Usage

\`\`\`cpp

FORCE_INLINE int add(int a, int b) noexcept
{
    return a + b;
}

// Mark a function as not inlined
NOINLINE void heavy_function() noexcept
{
    // ...
}
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Marking a recursive function \`FORCE_INLINE\` | The compiler may ignore it or code may bloat | Do not use \`FORCE_INLINE\` on recursive functions |
| Marking a large function \`FORCE_INLINE\` | Code bloat, increased icache pressure | Use only on small hot-path functions |
| Marking an ordinary function \`NOINLINE\` | Calls are no longer inlined | Use only for debugging or isolated measurement scenarios |

---
`
};

window.DOCS_DATA_EN['analysis'] = {
  title: "analyzer — Microarchitecture analysis",
  content: `## 29. analyzer — Microarchitecture analysis

Global namespace. \`noexcept\`.

Microarchitecture analyzer: memory barriers, cache control, serialized cycle measurement, address generation, cache hit measurement, adaptive hierarchy detection. Full x86/x64 support; on other platforms the barriers are compiler barriers and cache measurement falls back to cycle counting.

### 29.1 Configuration and report types

| Type | Description |
|------|------|
| \`analyzer::config\` | Cache level threshold configuration (\`l1_max\`/\`l2_max\`/\`l3_max\`/\`l4_max\`/\`cache_levels\`, 3 levels by default) |
| \`analyzer::cache_report\` | Hit measurement report (hit rate per level + percentile cycles) |
| \`analyzer::batch_result\` | Batch measurement result (with baseline subtraction) |
| \`analyzer::address_view\` | Address view POD (holds a pointer + count) |

\`\`\`cpp
analyzer::config c;            // default 3-level thresholds
c.cache_levels = 4;            // switch to 4 levels (including L4/DRAM)
c.l4_max = 150.0;

analyzer::config auto_c = analyzer::detect_config();  // adaptive detection
\`\`\`

### 29.2 Construction and configuration

| Interface | Description |
|------|------|
| \`analyzer()\` | Default construction (3-level thresholds) |
| \`analyzer(config)\` | Construct with the given configuration |
| \`get_config()\` / \`set_config(c)\` | Read / modify the configuration |

\`\`\`cpp
analyzer a;                    // default 3 levels
analyzer a_auto(auto_c);       // adaptive configuration
a.set_config(c);               // switch configuration at runtime
\`\`\`

### 29.3 Memory barrier primitives (static)

| Interface | Description |
|------|------|
| \`analyzer::mfence()\` | Full barrier (Store + Load serialization) |
| \`analyzer::lfence()\` | Load barrier |
| \`analyzer::sfence()\` | Store barrier |

\`\`\`cpp
store_data();
analyzer::sfence();
load_result();
\`\`\`

### 29.4 Cache control (static)

| Interface | Description |
|------|------|
| \`analyzer::cache_flush(p)\` | Flush a single cache line |
| \`analyzer::cache_flush_range(p, bytes)\` | Flush a byte range (line by line + trailing mfence) |

\`\`\`cpp
analyzer::cache_flush_range(&data, sizeof(data));
\`\`\`

### 29.5 Cycle measurement (static)

| Interface | Description |
|------|------|
| \`analyzer::now_cycles_fenced()\` | Fenced cycle read (lfence; rdtsc; lfence) |
| \`analyzer::measure_cycles_fenced(fn)\` | Fenced paired timing (precise measurement of short code segments) |
| \`analyzer::measure_loop_cycles(fn)\` | Single rdtscp wrap (returns the total loop cycles) |

\`\`\`cpp
uint64_t cyc = analyzer::measure_cycles_fenced([]() { /* short code */ });

uint64_t total = analyzer::measure_loop_cycles([]() {
    for (int i = 0; i < 1000; ++i) { work(i); }
});
\`\`\`

### 29.6 Address generation (static)

| Interface | Description |
|------|------|
| \`analyzer::make_sequential_addresses(base, count, stride)\` | Sequential address sequence |
| \`analyzer::make_random_addresses(base, count, stride, seed)\` | Random address sequence (deterministic and reproducible) |

\`\`\`cpp
int buf[4096];
auto seq = analyzer::make_sequential_addresses(buf, 4096, sizeof(int));
auto rnd = analyzer::make_random_addresses(buf, 4096, sizeof(int), 42);

analyzer::address_view av{seq.data(), seq.size()};
for (size_t i = 0; i < av.size(); ++i) {
    volatile int v = *static_cast<const volatile int*>(av[i]);
    (void)v;
}
\`\`\`

### 29.7 Cache hit measurement (instance methods)

| Interface | Description |
|------|------|
| \`a.measure_hits(base, count, stride)\` | One-stop: generate addresses, measure and classify |
| \`a.measure_hits(address_view)\` | Measure with pre-generated addresses, returns \`cache_report\` |
| \`a.measure_batch(address_view, repeats=10)\` | Take the best of several runs + subtract the baseline, returns \`batch_result\` |

\`\`\`cpp
analyzer a;

// One-stop measurement
auto r = a.measure_hits(buf, 4096, sizeof(int));
// r.l1_hit_rate / r.l2_hit_rate / r.l3_hit_rate / r.miss_rate
// r.avg_cycles / r.p50_cycles / r.p95_cycles / r.p99_cycles

// Reuse pre-generated addresses
auto seq = analyzer::make_sequential_addresses(buf, 4096, sizeof(int));
analyzer::address_view av{seq.data(), seq.size()};
auto r2 = a.measure_hits(av);

// Precise batch measurement
auto b = a.measure_batch(av, 10);
// b.net_cycles_per_access: net cycles per access

// Adaptive configuration
analyzer a_auto(analyzer::detect_config());
auto r3 = a_auto.measure_hits(av);
\`\`\`

### 29.8 Adaptive detection and report printing (static)

| Interface | Description |
|------|------|
| \`analyzer::detect_config()\` | Scan 1KB→16MB working sets to detect cache levels and thresholds |
| \`analyzer::print_report(label, r)\` | Print \`cache_report\` to stdout |
| \`analyzer::print_batch(label, r)\` | Print \`batch_result\` to stdout |
| \`analyzer::print_config(label, c)\` | Print \`config\` to stdout |

\`\`\`cpp
analyzer::config c = analyzer::detect_config();
analyzer::print_config("检测结果", c);

auto r = a.measure_hits(av);
analyzer::print_report("顺序访问", r);
// Output: 顺序访问 | L1: 98.9%  L2:  1.1%  L3:  0.0%  Miss:  0.0% | avg=  2.7  p50=  2.0  ...

auto b = a.measure_batch(av, 10);
analyzer::print_batch("批量测量", b);
\`\`\`

---
`
};

window.DOCS_DATA_EN['id_allocation'] = {
  title: "id_allocation\\<T> — ID allocator",
  content: `## 18. id_allocation\\<T> — ID allocator

Manages a recyclable ID pool to avoid unbounded ID growth. The default template parameter is \`size_t\`.

### Interfaces

| Interface | Description |
|------|------|
| \`get_id()\` | Get an ID (prefers a released one, otherwise increments) |
| \`free_id(T id)\` | Release an ID (returns it to the recycle pool) |
| \`total_number_of_ids()\` | Size of the recycle pool |
| \`maximum_id()\` | Largest ID allocated so far |

### Usage

\`\`\`cpp
id_allocation<uint32_t> alloc;
uint32_t id1 = alloc.get_id();  // 1
uint32_t id2 = alloc.get_id();  // 2
alloc.free_id(id1);             // release 1
uint32_t id3 = alloc.get_id();  // 1 (reused)
\`\`\`

---
`
};

window.DOCS_DATA_EN['string_to_code'] = {
  title: "string_to_code — String numeric code",
  content: `## 36. string_to_code — String numeric code

Namespace \`string_to_code\`. \`noexcept\`.

Reversible, collision-free encoding from strings to numeric codes. Short strings (≤8 bytes) inline a \`uint64_t\`; long strings (>8 bytes) use a \`uint64_t\` array. \`utf8_view\` is the unified input/output interface (\`utf8_view\` can be implicitly constructed from \`const char*\`/\`string_view\`/\`string\`).

### code_value — Numeric code

| Interface | Description |
|------|------|
| \`code_value()\` | Default construction, empty state |
| \`code_value(const utf8_view& s)\` | Encode a \`utf8_view\` into a numeric code (\`const char*\`/\`string_view\`/\`string\` implicitly convert to \`utf8_view\`) |
| \`code_value(code_value&&)\` | Move construction |
| \`operator=(code_value&&)\` | Move assignment |
| \`decode()\` | Decode back to the original string, returns \`utf8_view\` (does not own memory) |
| \`is_inline()\` | Whether it is in inline mode (short string ≤8 bytes) |
| \`inline_value()\` | Inline value (valid only when \`is_inline()=true\`) |
| \`string_size()\` | Byte length of the original string |
| \`empty()\` | Whether it is empty |
| \`equals(const code_value&)\` | Collision-free equivalence comparison |
| \`equals_strict(const code_value&)\` | Strict comparison (same semantics as \`equals\`, for scenarios that need explicit intent) |
| \`encode_equals(const char* data, size_t n)\` | Encode and compare: avoids constructing an intermediate \`code_value\`, compares the string with this directly |
| \`encode_inline(const char* data, size_t n)\` | Lightweight encoding (short strings only): returns \`uint64_t\`, constructs no object (\`n\` must be ≤8) |
| \`encode_inline_n<N>(const char* data)\` | Compile-time fixed-length encoding: \`N\` is a compile-time constant (1~8), returns \`uint64_t\` |

> Note: copying is forbidden; the \`utf8_view\` returned by \`decode()\` does not own memory, so the caller must keep the \`code_value\` alive.

### Free functions

| Interface | Description |
|------|------|
| \`encode(const utf8_view& s)\` | Encode into a \`code_value\` (accepts implicit conversion from \`const char*\`/\`string_view\`/\`string\`) |
| \`equals(const code_value& a, const code_value& b)\` | Equivalence comparison |

### Usage

\`\`\`cpp
using namespace string_to_code;

// Short string: inline uint64_t
code_value v1("player");                 // const char* → utf8_view → code_value
bool inline1 = v1.is_inline();           // true
uint64_t key = v1.inline_value();        // can be used directly as a map key
utf8_view s1 = v1.decode();              // "player" (also implicitly convertible to string_view)

// Long string: uint64_t array
code_value v2("this_is_long_string");
bool inline2 = v2.is_inline();           // false
utf8_view s2 = v2.decode();             // "this_is_long_string"

// Equivalence comparison
code_value a("test");
code_value b("test");
bool same = a.equals(b);                 // true

// encode_equals: avoid constructing an intermediate code_value
code_value c("my_key");
bool match = c.encode_equals("my_key", 6);  // true, no code_value construction needed

// encode_inline: lightweight encoding, constructs no object
uint64_t id = code_value::encode_inline("abc", 3);  // returns uint64_t directly

// encode_inline_n: compile-time fixed length
constexpr uint64_t id2 = code_value::encode_inline_n<3>("abc");

// Free functions
auto code = encode("hello");
bool eq = equals(code, code_value("hello"));  // true

// Various sources implicitly convert to utf8_view
code_value v3(std::string_view("from_sv"));    // string_view → utf8_view
code_value v4{utf8_view(std::string("x"))};   // std::string → utf8_view
// Chinese UTF-8 case
utf8_view cn("你好世界");
code_value v5(cn);                      // 12 bytes, heap allocation
\`\`\`

### What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Copying a \`code_value\` | Copying is forbidden | Use move semantics |
| Destroying the \`code_value\` after \`decode()\` | The \`utf8_view\` dangles | Keep the \`code_value\` alive |
| Using \`inline_value()\` to compare an instance with \`is_inline()=false\` | The returned value is a cache of the first 8 bytes, not the full data | Use \`equals()\` for uniform comparison |
| Passing \`n > 8\` to \`encode_inline\` | Undefined behavior | \`n\` must be ≤8 |
| \`code_value v(utf8_view(s))\` (parentheses) | Most Vexing Parse, parsed as a function declaration | Use list initialization \`code_value v{utf8_view(s)}\` |

---

`
};

window.DOCS_DATA_EN['part_generic'] = {
  title: "part generic facilities — index pool / id table / callable / throttle / bit ops / clock",
  content: `## 58. part generic facilities

The following facilities live in \`part/\` and can be included directly by any module.

### 58.1 index_pool — Index pool

Fixed-capacity index allocator that can be used across threads.

| Interface | Description |
|------|------|
| \`create(capacity)\` | Create with a fixed capacity; returns \`false\` if created twice |
| \`destroy()\` | Release; can be created again after destruction |
| \`allocate(index)\` | Allocate an index; returns \`false\` when exhausted |
| \`release(index)\` | Return an index; out-of-range values are ignored |
| \`available()\` / \`capacity()\` / \`created()\` | State queries |

\`\`\`cpp
index_pool pool;
pool.create(1024);

std::uint32_t slot = index_pool::none;
if (pool.allocate(slot))
{
    // use the slot
    pool.release(slot);
}
\`\`\`

This facility allows cross-thread calls; the other \`part\` containers have single-threaded semantics.

### 58.2 inline_callable — Callable

Unified type-erased callable: bind, invoke and release.

| Interface | Description |
|------|------|
| \`bind(fn)\` | Bind a callable object, returns \`true\` on success |
| \`operator()\` | Invoke the bound object |
| \`reset()\` | Release the binding |
| \`operator bool\` | Whether something is bound |
| \`heap_allocated()\` | Whether the current binding uses heap storage |
| \`callable_bind<Size, Align>(storage, fn, invoke, destroy)\` | Bind into caller-provided storage |

\`\`\`cpp
inline_callable<> callback;
callback.bind([]() noexcept { /* ... */ });
callback();
callback.reset();
\`\`\`

### 58.3 keyed_throttle — Keyed throttle

Counts per \`(key, window)\`; exceeding the limit within the same window returns \`false\`.

| Interface | Description |
|------|------|
| \`keyed_throttle<SlotCount>\` | Slot count template parameter, 64 by default |
| \`allow(key, window, limit)\` | Whether to allow; \`limit == 0\` always rejects |
| \`clear()\` | Clear all slots |

\`\`\`cpp
keyed_throttle<64> throttle;
const bool allowed = throttle.allow(/*key*/ 7, /*window*/ 1, /*limit*/ 8);
\`\`\`

### 58.4 bit_ops — Bit operations and fast random

| Interface | Description |
|------|------|
| \`mix_bits(value)\` | 64-bit mixing hash |
| \`count_trailing_zeros(value)\` / \`count_leading_zeros(value)\` | Trailing / leading zeros |
| \`popcount(value)\` | Set-bit count |
| \`align_up(value, alignment)\` | Round up; returns the original value when \`alignment\` is 0 |
| \`is_power_of_two(value)\` | Whether it is a power of two |
| \`round_up_power_of_two(value)\` | Round up to the nearest power of two |
| \`fast_rand\` | Lightweight random: construct with a seed, get values with \`next()\` |

\`\`\`cpp
const std::uint64_t seed = mix_bits(42);
fast_rand rng{seed};
const std::uint64_t value = rng.next();
\`\`\`

### 58.5 Monotonic clock

| Interface | Description |
|------|------|
| \`monotonic_now_ns()\` | Monotonically increasing nanosecond timestamp |

\`\`\`cpp
const std::uint64_t begin = monotonic_now_ns();
// ...
const std::uint64_t elapsed = monotonic_now_ns() - begin;
\`\`\`

### 58.6 id_table — Dense id table

Stores and accesses entries by integer id.

| Interface | Description |
|------|------|
| \`ensure(id, value)\` | Insert or update by id, returns whether a new entry was created |
| \`find(id)\` | Return the entry pointer, \`nullptr\` if not registered |
| \`index_of(id)\` | Return the index, \`-1\` if not registered |
| \`at(index)\` / \`id_at(index)\` | Get the entry / id by index |
| \`size()\` / \`empty()\` / \`clear()\` | State and clearing |
| \`for_each(f)\` | Iterate with \`f(id, value)\` |

\`\`\`cpp
id_table<field_info> table;
table.ensure(7, field_info{/* ... */});

const field_info* info = table.find(7);
table.for_each([](int id, const field_info& value) { /* ... */ });
\`\`\`

### 58.7 safety — Safety limits and encoding

| Interface | Description |
|------|------|
| \`safety_limits\` | Limit configuration: \`max_file_size\` / \`max_string_length\` / \`max_array_elements\` / \`max_object_fields\` / \`max_depth\` |
| \`base64_encode(data, len)\` | Base64 encoding, returns \`std::string\` |
| \`base64_decode(s)\` | Base64 decoding; returns an empty string for invalid input |
| \`base64_decode_strict(s, out)\` | Strict decoding, returns \`true\` on success |
| \`rle_compress(s)\` / \`rle_decompress(s)\` | RLE compression / decompression |

\`\`\`cpp
safety_limits limits;
limits.max_depth = 32;

const std::string encoded = base64_encode(data, len);
std::string decoded;
if (base64_decode_strict(encoded, decoded))
{
    // decode succeeded
}
\`\`\`

### 58.8 workspace_path — Workspace path

| Interface | Description |
|------|------|
| \`workspace_path()\` | Returns the current working directory (UTF-8); returns \`nullptr\` on failure; the result is written to a static buffer and must be copied immediately |

\`\`\`cpp
char* path = workspace_path();
if (path != nullptr)
{
    const std::string current(path);
}
\`\`\`

### 58.9 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Treating \`index_pool\` as a general memory allocator | It only allocates indices, it does not manage object memory | Index + caller-owned slot array |
| Calling \`release\` again on an already released index | Index state is corrupted | Release each index only once |
| Binding a large object by value with \`inline_callable\` | Every binding incurs extra storage | Bind large objects by reference or pointer |
| Treating \`keyed_throttle\` as exact counting | Keys sharing a slot reset each other on collision | Maintain your own map when exact counting is needed |
| Passing 0 to \`bit_ops\` trailing-zero counting | The result is meaningless | Check for zero first or use the convention that returns 64 |
| Using \`id_table\` as a sparse map | It only suits dense, contiguous id scenarios | Compact the ids into a dense range first |
`
};

window.DOCS_DATA_EN['hash'] = {
  title: "fnv1a / crc32c — Hashing and checksums",
  content: `## 61. fnv1a / crc32c — Hashing and checksums

Global namespace.

### 61.1 fnv1a — String hashing

| Interface | Description |
|------|------|
| \`FNV_OFFSET_BASIS_64\` / \`FNV_PRIME_64\` | 64-bit FNV-1a constants |
| \`fnv1a_consteval(s)\` | Compile-time computation, the argument is a null-terminated string |
| \`fnv1a_runtime(s)\` | Runtime computation (null-terminated) |
| \`fnv1a_runtime(s, len)\` | Runtime computation (explicit length) |

### 61.2 crc32c — CRC32C checksum

| Interface | Description |
|------|------|
| \`compute_crc32c(data, len)\` | Returns a \`uint32_t\` checksum value |

### 61.3 Example

\`\`\`cpp
constexpr uint64_t id = fnv1a_consteval("entity");
const uint64_t runtime = fnv1a_runtime("entity");
const uint32_t crc = compute_crc32c(data.data(), data.size());
\`\`\`

### 61.4 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Treating crc32c as a cryptographic hash | It is only for integrity checks | Use dedicated cryptographic algorithms for security scenarios |
| Using \`fnv1a_runtime(s)\` on a buffer containing null characters | Only computes up to the first null character | Use the overload that takes \`len\` |
`
};
