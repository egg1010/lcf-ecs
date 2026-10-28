window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['operating_message'] = {
  id: 'operating_message',
  title: "operating_message — 错误对象",
  category: 'tools',
  icon: 'O',
  order: 14,
  content: `## 14. operating_message — 错误对象

一次调用一个对象，只描述最近一次失败。

- 2 字节，可平凡复制，失败路径不分配
- 错误码 14 位，\`code()\` 返回，0 表示成功
- 级别 2 位，\`level()\` 返回 debug / info / warn / error

### msg_level

\`\`\`cpp
enum class msg_level : uint8_t {
    debug = 0,
    info  = 1,
    warn  = 2,
    error = 3
};
\`\`\`

### 接口

| 接口 | 说明 |
|------|------|
| \`ok()\` / \`operator bool()\` | 是否成功 |
| \`code()\` | 错误码，成功为 0 |
| \`is_code(c)\` | 判断错误码 |
| \`level()\` | 本次失败级别 |
| \`write_failure(code, level)\` | 写入失败，保留首个失败 |
| \`write(false, code, level)\` | 带标签写入失败 |
| \`check(cond, ...)\` | 条件不成立时写入并返回 false |
| \`operator+=(other)\` | 合并，保留首个失败 |
| \`reset()\` | 恢复成功 |

### 使用

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

### 错误码

| 常量 | 含义 |
|------|------|
| \`om_err_type_mismatch\` | 类型不匹配 |
| \`om_err_invalid_entity\` | 实体无效 |
| \`om_err_version_mismatch\` | 版本不匹配 |
| \`om_err_out_of_range\` | 索引越界 |
| \`om_err_null_pointer\` | 空指针 |
| \`om_err_capacity_exceeded\` | 容量超限 |
| \`om_err_not_found\` | 未找到 |
| \`om_err_already_exists\` | 已存在 |
| \`om_err_out_of_memory\` | 内存分配失败 |
| \`om_err_invalid_argument\` | 参数非法 |
| \`om_err_unsupported_operation\` | 当前模式不支持 |
| \`om_err_cancelled\` | 已取消 |
| \`om_err_timeout\` | 等待超时 |
| \`om_err_not_running\` | 目标未启动 |
| \`om_err_shutting_down\` | 正在关闭 |
| \`om_err_queue_full\` | 队列已满 |
| \`om_err_decode_failed\` | 解码失败 |
| \`om_err_io_failed\` | IO 失败 |
| \`om_err_checksum_failed\` | 校验失败 |
| \`om_err_spawn_failed\` | 执行体创建失败 |

自定义错误码用 \`om_error_code("my_error")\` 生成，名称由调用方自己保存。

### 首错工具

链式与管道一次执行共用一个错误对象，只保留首个失败：

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

| 接口 | 说明 |
|------|------|
| \`om_write_first_failure(dst, failed, level, code, site)\` | 目标成功时写入首错并返回 true；失败或空目标返回 false |
| \`om_merge_first_failure(dst, src)\` | 目标成功时复制 src 的首错；目标已失败不覆盖 |

### 名称查询

| 接口 | 说明 |
|------|------|
| \`om_error_name(code)\` | 编译期目录查名称，未收录返回 \`"unknown"\` |
| \`om_error_desc(code)\` | 编译期目录查描述 |

### 使用注意

| 注意点 | 说明 |
|--------|------|
| 同一对象不并发写 | 一个对象只在一个线程内使用 |
| 无位置信息 | 排障依赖错误码名称与调用栈 |
| 失败路径不分配 | 全程无堆分配 |
| 无全局状态 | 统计、去重、恢复策略由调用方自建容器实现 |
`
};

window.DOCS_DATA['force_inline'] = {
  id: 'force_inline',
  title: "FORCE_INLINE / NOINLINE — 跨平台内联宏",
  category: 'tools',
  icon: 'H',
  order: 24,
  content: `## 23. FORCE_INLINE / NOINLINE — 跨平台内联宏


| 宏 | 说明 |
|------|------|
| \`FORCE_INLINE\` | 强制函数内联，跨编译器适配 |
| \`NOINLINE\` | 禁止函数内联，跨编译器适配 |

| 编译器 | \`FORCE_INLINE\` 展开为 | \`NOINLINE\` 展开为 |
|--------|----------------------|------------------|
| MSVC | \`__forceinline\` | \`__declspec(noinline)\` |
| GCC / Clang | \`inline __attribute__((always_inline))\` | \`__attribute__((noinline))\` |
| 其他 | \`inline\` | (空) |

### 使用

\`\`\`cpp

FORCE_INLINE int add(int a, int b) noexcept
{
    return a + b;
}

// 标记不内联的函数
NOINLINE void heavy_function() noexcept
{
    // ...
}
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 递归函数标记 \`FORCE_INLINE\` | 编译器可能忽略或导致代码膨胀 | 递归函数不使用 \`FORCE_INLINE\` |
| 大函数标记 \`FORCE_INLINE\` | 代码膨胀，icache 压力增大 | 仅对热路径小函数使用 |
| 对常规函数标记 \`NOINLINE\` | 调用不再内联 | 仅用于调试或隔离测量场景 |

---
`
};

window.DOCS_DATA['analysis'] = {
  id: 'analysis',
  title: "analyzer — 微架构分析",
  category: 'tools',
  icon: 'A',
  order: 29,
  content: `## 29. analyzer — 微架构分析

全局命名空间。\`noexcept\`。

微架构分析器：内存屏障、缓存控制、序列化周期测量、地址生成、缓存命中测量、自适应层级检测。x86/x64 全套支持，其他平台屏障为编译器屏障，缓存测量回退到周期计数。

### 29.1 配置与报告类型

| 类型 | 说明 |
|------|------|
| \`analyzer::config\` | 缓存层级阈值配置（\`l1_max\`/\`l2_max\`/\`l3_max\`/\`l4_max\`/\`cache_levels\`，默认 3 级） |
| \`analyzer::cache_report\` | 命中测量报告（各级命中率 + 百分位周期） |
| \`analyzer::batch_result\` | 批量测量结果（含基线扣除） |
| \`analyzer::address_view\` | 地址视图 POD（持有指针 + 数量） |

\`\`\`cpp
analyzer::config c;            // 默认 3 级阈值
c.cache_levels = 4;            // 切换为 4 级 (含 L4/DRAM)
c.l4_max = 150.0;

analyzer::config auto_c = analyzer::detect_config();  // 自适应检测
\`\`\`

### 29.2 构造与配置

| 接口 | 说明 |
|------|------|
| \`analyzer()\` | 默认构造（3 级阈值） |
| \`analyzer(config)\` | 用指定配置构造 |
| \`get_config()\` / \`set_config(c)\` | 读取 / 修改配置 |

\`\`\`cpp
analyzer a;                    // 默认 3 级
analyzer a_auto(auto_c);       // 自适应配置
a.set_config(c);               // 运行时切换配置
\`\`\`

### 29.3 内存屏障原语（静态）

| 接口 | 说明 |
|------|------|
| \`analyzer::mfence()\` | 全屏障（Store + Load 序列化） |
| \`analyzer::lfence()\` | Load 屏障 |
| \`analyzer::sfence()\` | Store 屏障 |

\`\`\`cpp
store_data();
analyzer::sfence();
load_result();
\`\`\`

### 29.4 缓存控制（静态）

| 接口 | 说明 |
|------|------|
| \`analyzer::cache_flush(p)\` | 刷新单条缓存行 |
| \`analyzer::cache_flush_range(p, bytes)\` | 刷新字节范围（逐行 + 尾部 mfence） |

\`\`\`cpp
analyzer::cache_flush_range(&data, sizeof(data));
\`\`\`

### 29.5 周期测量（静态）

| 接口 | 说明 |
|------|------|
| \`analyzer::now_cycles_fenced()\` | 全屏障周期读取（lfence; rdtsc; lfence） |
| \`analyzer::measure_cycles_fenced(fn)\` | 全屏障配对计时（短代码段精确测量） |
| \`analyzer::measure_loop_cycles(fn)\` | 单次 rdtscp 包裹（返回循环总周期） |

\`\`\`cpp
uint64_t cyc = analyzer::measure_cycles_fenced([]() { /* 短代码 */ });

uint64_t total = analyzer::measure_loop_cycles([]() {
    for (int i = 0; i < 1000; ++i) { work(i); }
});
\`\`\`

### 29.6 地址生成（静态）

| 接口 | 说明 |
|------|------|
| \`analyzer::make_sequential_addresses(base, count, stride)\` | 顺序地址序列 |
| \`analyzer::make_random_addresses(base, count, stride, seed)\` | 随机地址序列（确定性可复现） |

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

### 29.7 缓存命中测量（实例方法）

| 接口 | 说明 |
|------|------|
| \`a.measure_hits(base, count, stride)\` | 一站式：生成地址、测量并分类 |
| \`a.measure_hits(address_view)\` | 用预生成地址测量，返回 \`cache_report\` |
| \`a.measure_batch(address_view, repeats=10)\` | 批量取最优 + 扣除基线，返回 \`batch_result\` |

\`\`\`cpp
analyzer a;

// 一站式测量
auto r = a.measure_hits(buf, 4096, sizeof(int));
// r.l1_hit_rate / r.l2_hit_rate / r.l3_hit_rate / r.miss_rate
// r.avg_cycles / r.p50_cycles / r.p95_cycles / r.p99_cycles

// 预生成地址复用
auto seq = analyzer::make_sequential_addresses(buf, 4096, sizeof(int));
analyzer::address_view av{seq.data(), seq.size()};
auto r2 = a.measure_hits(av);

// 批量精确测量
auto b = a.measure_batch(av, 10);
// b.net_cycles_per_access: 净每次访问周期

// 自适应配置
analyzer a_auto(analyzer::detect_config());
auto r3 = a_auto.measure_hits(av);
\`\`\`

### 29.8 自适应检测与报告打印（静态）

| 接口 | 说明 |
|------|------|
| \`analyzer::detect_config()\` | 扫描 1KB→16MB 工作集检测缓存层级与阈值 |
| \`analyzer::print_report(label, r)\` | 打印 \`cache_report\` 到 stdout |
| \`analyzer::print_batch(label, r)\` | 打印 \`batch_result\` 到 stdout |
| \`analyzer::print_config(label, c)\` | 打印 \`config\` 到 stdout |

\`\`\`cpp
analyzer::config c = analyzer::detect_config();
analyzer::print_config("检测结果", c);

auto r = a.measure_hits(av);
analyzer::print_report("顺序访问", r);
// 输出: 顺序访问 | L1: 98.9%  L2:  1.1%  L3:  0.0%  Miss:  0.0% | avg=  2.7  p50=  2.0  ...

auto b = a.measure_batch(av, 10);
analyzer::print_batch("批量测量", b);
\`\`\`

---
`
};

window.DOCS_DATA['id_allocation'] = {
  id: 'id_allocation',
  title: "id_allocation\\<T> — ID分配器",
  category: 'tools',
  icon: 'D',
  order: 19,
  content: `## 18. id_allocation\\<T> — ID分配器

管理可回收的 ID 池，避免 ID 无限增长。默认模板参数为 \`size_t\`。

### 接口

| 接口 | 说明 |
|------|------|
| \`get_id()\` | 获取一个 ID（优先回收已释放的，否则递增） |
| \`free_id(T id)\` | 释放 ID（放入回收池） |
| \`total_number_of_ids()\` | 回收池大小 |
| \`maximum_id()\` | 已分配的最大 ID |

### 使用

\`\`\`cpp
id_allocation<uint32_t> alloc;
uint32_t id1 = alloc.get_id();  // 1
uint32_t id2 = alloc.get_id();  // 2
alloc.free_id(id1);             // 释放 1
uint32_t id3 = alloc.get_id();  // 1（复用）
\`\`\`

---
`
};

window.DOCS_DATA['string_to_code'] = {
  id: 'string_to_code',
  title: "string_to_code — 字符串数字码",
  category: 'tools',
  icon: 'C',
  order: 36,
  content: `## 36. string_to_code — 字符串数字码

命名空间 \`string_to_code\`。\`noexcept\`。

字符串到数字码的可逆无冲突编码。短串（≤8 字节）内联 \`uint64_t\`，长串（>8 字节）用 \`uint64_t\` 数组。统一以 \`utf8_view\` 为输入输出接口 (\`utf8_view\` 可从 \`const char*\`/\`string_view\`/\`string\` 隐式构造)。

### code_value — 数字码

| 接口 | 说明 |
|------|------|
| \`code_value()\` | 默认构造，空状态 |
| \`code_value(const utf8_view& s)\` | 编码 \`utf8_view\` 为数字码 (\`const char*\`/\`string_view\`/\`string\` 隐式转 \`utf8_view\`) |
| \`code_value(code_value&&)\` | 移动构造 |
| \`operator=(code_value&&)\` | 移动赋值 |
| \`decode()\` | 解码为原字符串，返回 \`utf8_view\` (不持有内存) |
| \`is_inline()\` | 是否为内联模式（短串 ≤8 字节） |
| \`inline_value()\` | 内联值（仅 \`is_inline()=true\` 时有效） |
| \`string_size()\` | 原始字符串字节长度 |
| \`empty()\` | 是否为空 |
| \`equals(const code_value&)\` | 无冲突等价比较 |
| \`equals_strict(const code_value&)\` | 严格比较 (与 \`equals\` 语义相同，供语义明确场景使用) |
| \`encode_equals(const char* data, size_t n)\` | 编码并比较: 避免构造中间 \`code_value\`，直接比较字符串与 this |
| \`encode_inline(const char* data, size_t n)\` | 轻量编码 (短串专用): 返回 \`uint64_t\`，不构造对象 (\`n\` 必须 ≤8) |
| \`encode_inline_n<N>(const char* data)\` | 编译期定长编码: \`N\` 为编译期常量 (1~8)，返回 \`uint64_t\` |

> 注：禁止拷贝；\`decode()\` 返回的 \`utf8_view\` 不持有内存，调用方需保证 \`code_value\` 生命周期。

### 自由函数

| 接口 | 说明 |
|------|------|
| \`encode(const utf8_view& s)\` | 编码为 \`code_value\` (接受 \`const char*\`/\`string_view\`/\`string\` 隐式转换) |
| \`equals(const code_value& a, const code_value& b)\` | 等价比较 |

### 使用

\`\`\`cpp
using namespace string_to_code;

// 短串: 内联 uint64_t
code_value v1("player");                 // const char* → utf8_view → code_value
bool inline1 = v1.is_inline();           // true
uint64_t key = v1.inline_value();        // 可直接做 map key
utf8_view s1 = v1.decode();              // "player" (也可隐式转 string_view)

// 长串: uint64_t 数组
code_value v2("this_is_long_string");
bool inline2 = v2.is_inline();           // false
utf8_view s2 = v2.decode();             // "this_is_long_string"

// 等价比较
code_value a("test");
code_value b("test");
bool same = a.equals(b);                 // true

// encode_equals: 避免构造中间 code_value
code_value c("my_key");
bool match = c.encode_equals("my_key", 6);  // true, 无需构造 code_value

// encode_inline: 轻量编码, 不构造对象
uint64_t id = code_value::encode_inline("abc", 3);  // 直接返回 uint64_t

// encode_inline_n: 编译期定长
constexpr uint64_t id2 = code_value::encode_inline_n<3>("abc");

// 自由函数
auto code = encode("hello");
bool eq = equals(code, code_value("hello"));  // true

// 各种来源隐式转 utf8_view
code_value v3(std::string_view("from_sv"));    // string_view → utf8_view
code_value v4{utf8_view(std::string("x"))};   // std::string → utf8_view
// 中文 UTF-8 场景
utf8_view cn("你好世界");
code_value v5(cn);                      // 12 字节, 走堆分配
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 拷贝 \`code_value\` | 禁止拷贝 | 使用移动语义 |
| \`decode()\` 后销毁 \`code_value\` | \`utf8_view\` 悬空 | 保证 \`code_value\` 生命周期 |
| 用 \`inline_value()\` 比较 \`is_inline()=false\` 的实例 | 返回值为前8字节缓存，非完整数据 | 用 \`equals()\` 统一比较 |
| \`encode_inline\` 传 \`n > 8\` | 未定义行为 | \`n\` 必须 ≤8 |
| \`code_value v(utf8_view(s))\` (圆括号) | Most Vexing Parse，被解析为函数声明 | 用列表初始化 \`code_value v{utf8_view(s)}\` |

---

`
};

window.DOCS_DATA['part_generic'] = {
  id: 'part_generic',
  title: "part 通用设施 — 索引池 / id 表 / 可调用体 / 限流 / 位运算 / 时钟",
  category: 'tools',
  icon: 'U',
  order: 58,
  content: `## 58. part 通用设施

以下设施位于 \`part/\`，可被任意模块直接包含。

### 58.1 index_pool — 索引池

固定容量的索引分配器，可跨线程使用。

| 接口 | 说明 |
|------|------|
| \`create(capacity)\` | 创建，容量固定；重复创建返回 \`false\` |
| \`destroy()\` | 释放；销毁后可重新创建 |
| \`allocate(index)\` | 分配一个索引，耗尽返回 \`false\` |
| \`release(index)\` | 归还索引；越界忽略 |
| \`available()\` / \`capacity()\` / \`created()\` | 状态查询 |

\`\`\`cpp
index_pool pool;
pool.create(1024);

std::uint32_t slot = index_pool::none;
if (pool.allocate(slot))
{
    // 使用 slot
    pool.release(slot);
}
\`\`\`

本设施允许跨线程调用；\`part\` 其余容器为单线程语义。

### 58.2 inline_callable — 可调用体

统一的类型擦除可调用体：绑定、调用与释放。

| 接口 | 说明 |
|------|------|
| \`bind(fn)\` | 绑定可调用对象，成功返回 \`true\` |
| \`operator()\` | 调用已绑定的对象 |
| \`reset()\` | 释放绑定 |
| \`operator bool\` | 是否已绑定 |
| \`heap_allocated()\` | 当前绑定是否使用堆存储 |
| \`callable_bind<Size, Align>(storage, fn, invoke, destroy)\` | 绑定到调用方自备的存储 |

\`\`\`cpp
inline_callable<> callback;
callback.bind([]() noexcept { /* ... */ });
callback();
callback.reset();
\`\`\`

### 58.3 keyed_throttle — 键控限流器

按 \`(key, window)\` 计数；同一窗口内超过上限返回 \`false\`。

| 接口 | 说明 |
|------|------|
| \`keyed_throttle<SlotCount>\` | 槽数模板参数，默认 64 |
| \`allow(key, window, limit)\` | 是否放行；\`limit == 0\` 恒拒绝 |
| \`clear()\` | 清空全部槽位 |

\`\`\`cpp
keyed_throttle<64> throttle;
const bool allowed = throttle.allow(/*key*/ 7, /*window*/ 1, /*limit*/ 8);
\`\`\`

### 58.4 bit_ops — 位运算与快速随机

| 接口 | 说明 |
|------|------|
| \`mix_bits(value)\` | 64 位混合哈希 |
| \`count_trailing_zeros(value)\` / \`count_leading_zeros(value)\` | 尾零 / 前导零 |
| \`popcount(value)\` | 置位计数 |
| \`align_up(value, alignment)\` | 向上对齐；\`alignment\` 为 0 时返回原值 |
| \`is_power_of_two(value)\` | 是否为 2 的幂 |
| \`round_up_power_of_two(value)\` | 向上取最近的 2 的幂 |
| \`fast_rand\` | 轻量随机数：构造种子，\`next()\` 取值 |

\`\`\`cpp
const std::uint64_t seed = mix_bits(42);
fast_rand rng{seed};
const std::uint64_t value = rng.next();
\`\`\`

### 58.5 单调时钟

| 接口 | 说明 |
|------|------|
| \`monotonic_now_ns()\` | 单调递增的纳秒时间戳 |

\`\`\`cpp
const std::uint64_t begin = monotonic_now_ns();
// ...
const std::uint64_t elapsed = monotonic_now_ns() - begin;
\`\`\`

### 58.6 id_table — 稠密 id 表

按整型 id 存取条目。

| 接口 | 说明 |
|------|------|
| \`ensure(id, value)\` | 按 id 插入或更新，返回是否新建 |
| \`find(id)\` | 返回条目指针，未注册返回 \`nullptr\` |
| \`index_of(id)\` | 返回下标，未注册返回 \`-1\` |
| \`at(index)\` / \`id_at(index)\` | 按下标取条目 / id |
| \`size()\` / \`empty()\` / \`clear()\` | 状态与清空 |
| \`for_each(f)\` | 遍历 \`f(id, value)\` |

\`\`\`cpp
id_table<field_info> table;
table.ensure(7, field_info{/* ... */});

const field_info* info = table.find(7);
table.for_each([](int id, const field_info& value) { /* ... */ });
\`\`\`

### 58.7 safety — 安全限制与编码

| 接口 | 说明 |
|------|------|
| \`safety_limits\` | 限制配置：\`max_file_size\` / \`max_string_length\` / \`max_array_elements\` / \`max_object_fields\` / \`max_depth\` |
| \`base64_encode(data, len)\` | Base64 编码，返回 \`std::string\` |
| \`base64_decode(s)\` | Base64 解码；非法输入返回空串 |
| \`base64_decode_strict(s, out)\` | 严格解码，成功返回 \`true\` |
| \`rle_compress(s)\` / \`rle_decompress(s)\` | RLE 压缩 / 解压 |

\`\`\`cpp
safety_limits limits;
limits.max_depth = 32;

const std::string encoded = base64_encode(data, len);
std::string decoded;
if (base64_decode_strict(encoded, decoded))
{
    // 解码成功
}
\`\`\`

### 58.8 workspace_path — 工作区路径

| 接口 | 说明 |
|------|------|
| \`workspace_path()\` | 返回当前工作目录（UTF-8）；失败返回 \`nullptr\`，结果写入静态缓冲，需立即复制 |

\`\`\`cpp
char* path = workspace_path();
if (path != nullptr)
{
    const std::string current(path);
}
\`\`\`

### 58.9 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 把 \`index_pool\` 当通用内存分配器 | 它只分配索引，不管理对象内存 | 索引 + 自备槽数组 |
| 对已释放索引再次 \`release\` | 索引状态被破坏 | 一个索引只归还一次 |
| 用 \`inline_callable\` 按值绑定大对象 | 每次绑定都产生额外存储 | 大对象用引用或指针绑定 |
| 把 \`keyed_throttle\` 当精确计数 | 槽冲突时同槽键会互相重置 | 需要精确计数时自行维护映射 |
| 在 \`bit_ops\` 上传入 0 求尾零 | 结果无意义 | 先判零或使用返回 64 的约定 |
| 把 \`id_table\` 当稀疏映射用 | 它只适合 id 稠密连续的场景 | 先把 id 压缩到稠密范围 |
`
};

window.DOCS_DATA['hash'] = {
  id: 'hash',
  title: "fnv1a / crc32c — 哈希与校验",
  category: 'tools',
  icon: 'H',
  order: 61,
  content: `## 61. fnv1a / crc32c — 哈希与校验

全局命名空间。

### 61.1 fnv1a — 字符串哈希

| 接口 | 说明 |
|------|------|
| \`FNV_OFFSET_BASIS_64\` / \`FNV_PRIME_64\` | 64 位 FNV-1a 常量 |
| \`fnv1a_consteval(s)\` | 编译期计算，参数为以空字符结尾的字符串 |
| \`fnv1a_runtime(s)\` | 运行期计算（以空字符结尾） |
| \`fnv1a_runtime(s, len)\` | 运行期计算（指定长度） |

### 61.2 crc32c — CRC32C 校验

| 接口 | 说明 |
|------|------|
| \`compute_crc32c(data, len)\` | 返回 \`uint32_t\` 校验值 |

### 61.3 示例

\`\`\`cpp
constexpr uint64_t id = fnv1a_consteval("entity");
const uint64_t runtime = fnv1a_runtime("entity");
const uint32_t crc = compute_crc32c(data.data(), data.size());
\`\`\`

### 61.4 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 把 crc32c 当加密哈希 | 它只用于完整性校验 | 安全场景使用专用加密算法 |
| 对含空字符的缓冲区用 \`fnv1a_runtime(s)\` | 只计算到首个空字符 | 改用带 \`len\` 的重载 |
`
};
