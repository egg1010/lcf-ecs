window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['operating_message'] = {
  id: 'operating_message',
  title: "operating_message — 操作消息",
  category: 'tools',
  icon: 'O',
  order: 14,
  content: `## 14. operating_message — 操作消息

记录操作结果（成功/失败）和调试信息。核心特性：

- **值语义返回**：\`single_class_set\` / \`manager\` 的 \`add\` / \`add_batch\` / \`hard_remove\` / \`soft_remove\` 均按值返回 \`operating_message\`。容器自身不持有 \`operating_message\` 成员，每次操作返回独立结果
- **粘性 false 语义**：单个返回值对象一旦失败就保持 false，只有 \`reset()\` 能恢复
- **全局开关**：\`ecs_debug_messages()\` 运行时控制是否写入字符串
- **日志级别过滤**：\`msg_level\` 枚举 + \`min_level_\` 表驱动过滤，被过滤的级别跳过全部格式化
- **类型特化写入**：\`write_message\` 对整型/浮点走 \`std::to_chars\`，对字符串走 \`append\`，其他类型走 \`std::format_to\`

### msg_level 日志级别

\`\`\`cpp
enum class msg_level : uint8_t {
    debug = 0,  // 最低
    info  = 1,  // 默认 min_level
    warn  = 2,
    error = 3   // 最高
};
\`\`\`

级别用 \`uint8_t\` 存储，过滤仅做一次整数比较（\`lv < min_level_\` 返回），前缀通过 \`k_level_prefix[]\` 字符串数组索引写入，无 switch-case 分支。

### 接口

| 接口 | 说明 |
|------|------|
| \`ecs_debug_messages()\` | 全局开关引用（控制是否写入字符串） |
| \`msg_level\` | 日志级别枚举（debug/info/warn/error） |
| \`operating_message()\` | 默认构造，\`switch_=true\`，\`min_level_=info\` |
| \`operator bool()\` | 是否成功（返回 \`switch_\`） |
| \`reset()\` | 重置为成功并清空消息 |
| \`clear_message()\` | 仅清空消息字符串 |
| \`set_switch_bool(bool)\` | 直接设置开关值 |
| \`get_switch_bool()\` | 获取开关引用 |
| \`get_switch_bool() const\` | 获取开关 const 引用 |
| \`set_min_level(msg_level)\` | 设置最低记录级别（默认 info） |
| \`get_min_level()\` | 获取当前最低记录级别 |
| \`reserve(size_t)\` | 预分配消息缓冲区（避免循环内重分配） |
| \`capacity()\` | 当前缓冲区容量 |
| \`message_size()\` | 当前消息长度 |
| \`write_message(bool sw, Args... args)\` | 写入消息（\`sw=false\` 标记失败，粘性）；整型/浮点走 to_chars |
| \`write_message_level(lv, sw, Args...)\` | 带级别的写入（级别不足则跳过，自动加前缀） |
| \`write_message_fmt(bool sw, fmt, Args...)\` | 格式化写入消息（\`std::format_to\`） |
| \`write_message_fmt_level(lv, sw, fmt, Args...)\` | 带级别的格式化写入 |
| \`read_message()\` | 读取消息字符串（返回 \`string_view\`） |
| \`operator+=(string_view)\` | 追加字符串到消息 |
| \`operator+=(operating_message&&)\` | 合并右值消息（\`switch_ = switch_ && other.switch_\`） |
| \`operator+=(const operating_message&)\` | 合并左值消息 |
| \`operator<<(ostream, operating_message)\` | 输出到 \`ostream\` |
| \`operating_message(operating_message&&)\` | 移动构造 |
| \`operator=(operating_message&&)\` | 移动赋值 |
| \`operating_message(const operating_message&)\` | 拷贝构造 |
| \`operator=(const operating_message&)\` | 拷贝赋值 |

### 使用

\`\`\`cpp
operating_message msg;
msg.write_message(false, "Error: ", "file not found");
if (!msg) { /* 处理错误 */ }

// 粘性 false：write_message(true) 不会恢复
msg.write_message(true, "This won't recover");
// msg 仍为 false

msg.reset();  // 恢复为 true

// 全局开关
ecs_debug_messages() = false;  // 禁用字符串写入
ecs_debug_messages() = true;   // 启用字符串写入

// 格式化
msg.write_message_fmt(true, "x={}, y={}", 10, 20);

// 合并
operating_message msg2;
msg2.write_message(false, "Another error");
msg += msg2;  // switch_ = msg.switch_ && msg2.switch_

// 日志级别过滤
msg.set_min_level(msg_level::warn);       // 只记 warn 及以上
msg.write_message_level(msg_level::info, true, "这条被过滤");  // 不写入
msg.write_message_level(msg_level::error, true, "严重错误: ", 42);
// message_ = "[ERROR] 严重错误: 42\\n"

// 级别格式化
msg.write_message_fmt_level(msg_level::warn, true, "v={} k={}", 1, "x");
// message_ += "[WARN]  v=1 k=x\\n"

// 整型/浮点使用 to_chars
msg.write_message(true, "i=", 100, " d=", 3.14);

// 预分配缓冲区（循环场景避免首次分配）
msg.reserve(4096);
for (int i = 0; i < 1000; ++i) {
    msg.reset();
    msg.write_message(true, "iter ", i);
}
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 依赖 \`write_message(true)\` 恢复失败状态 | 粘性 false 语义，成功后不会恢复 | 调用 \`reset()\` 显式恢复 |
| 在 Release 构建中依赖 \`read_message()\` | 全局开关关闭时字符串为空 | 使用 \`operator bool()\` 判断成败，而非消息内容 |
| 忘记检查 \`operator bool()\` | 操作失败被静默忽略 | 每次关键操作后检查 \`if (!msg) { ... }\` |
| 高频日志用 \`{:08x}\` 等复杂格式 | 走 \`std::format_to\` 通用路径 | 高频路径仅用 \`{}\` 简单占位符以走 fast path |
| 默认级别记录所有 debug 日志 | Release 中 debug 日志拖累性能 | \`set_min_level(msg_level::warn)\` 过滤低级别 |

---
`
};

window.DOCS_DATA['type_id'] = {
  id: 'type_id',
  title: "type_id — 类型ID",
  category: 'tools',
  icon: 'I',
  order: 18,
  content: `## 17. type_id — 类型ID

为每种类型分配唯一整数 ID（编译时确定，线程安全）。

### 接口

| 接口 | 说明 |
|------|------|
| \`type_id::get_type_id<T>()\` | 获取类型 T 的唯一 ID（静态函数，线程安全） |
| \`type_id::current_max_id()\` | 返回当前已分配的最大 type_id（静态函数） |

### 使用

\`\`\`cpp
int id1 = type_id::get_type_id<int>();
int id2 = type_id::get_type_id<double>();
assert(type_id::get_type_id<int>() == id1);  // 同类型 ID 相同

int max_id = type_id::current_max_id();  // 已分配的最大 ID
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

window.DOCS_DATA['force_inline'] = {
  id: 'force_inline',
  title: "FORCE_INLINE / NOINLINE — 跨平台内联宏",
  category: 'tools',
  icon: 'H',
  order: 24,
  content: `## 23. FORCE_INLINE / NOINLINE — 跨平台内联宏

\`#include "part/force_inline.hpp"\`

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
#include "part/force_inline.hpp"

FORCE_INLINE int add(int a, int b) noexcept
{
    return a + b;
}

// 标记不内联的函数 (例如避免代码膨胀, 或用于隔离性能测量)
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
| 对热路径函数标记 \`NOINLINE\` | 性能下降 | \`NOINLINE\` 仅用于调试或隔离测量场景 |

---
`
};

window.DOCS_DATA['time'] = {
  id: 'time',
  title: "time — 计时与基准测量",
  category: 'tools',
  icon: 'T',
  order: 29,
  content: `## 28. time — 计时与基准测量

\`#include "part/time.hpp"\`，全局命名空间。\`noexcept\`。

计时与基准测量工具：墙钟计时、CPU 周期计数、缓存屏障、统计分布、在线分位数、缓存延迟测量。x86/x64 提供 \`rdtsc\`/\`rdtscp\` / \`clflush\` / \`mfence\` / \`lfence\`，其他平台返回 0 或空操作。

### 29.1 timer — 墙钟计时器

| 接口 | 说明 |
|------|------|
| \`timer()\` | 构造并记录起始时间点 |
| \`reset()\` | 重置起始时间点 |
| \`elapsed_ns()\` | 纳秒数 |
| \`elapsed_us()\` | 微秒数 |
| \`elapsed_ms()\` | 毫秒数 |
| \`elapsed_s()\` | 秒数 |

\`\`\`cpp
timer t;
// ... 执行操作 ...
double ns = t.elapsed_ns();
\`\`\`

### 29.2 cycle_timer — CPU 周期计时器

| 接口 | 说明 |
|------|------|
| \`cycle_timer()\` | 构造并记录起始周期 |
| \`reset()\` | 重置起始周期 |
| \`elapsed_cycles()\` | CPU 周期数 |
| \`elapsed_ns_estimated(cpu_ghz)\` | 按 CPU 频率估算纳秒 |
| \`rdtsc()\` | 读取 TSC 周期计数（自由函数，x86/x64；其他平台返回 0） |
| \`rdtscp()\` | 序列化读取 TSC 周期计数（自由函数，x86/x64；其他平台返回 0） |

\`\`\`cpp
cycle_timer ct;
// ... 执行操作 ...
uint64_t cycles = ct.elapsed_cycles();
double ns = ct.elapsed_ns_estimated(3.5);  // 3.5 GHz

// 独立读取 TSC
uint64_t tsc = rdtsc();
uint64_t tsc_serialized = rdtscp();
\`\`\`

### 29.3 stats — 统计分布

| 字段/接口 | 说明 |
|------|------|
| \`min\` / \`max\` / \`mean\` / \`median\` | 基本统计量（字段） |
| \`p50\` / \`p90\` / \`p95\` / \`p99\` | 百分位（字段） |
| \`stddev\` | 标准差（字段） |
| \`count\` | 样本数（字段） |
| \`compute_stats(dense<double> samples)\` | 从样本计算统计量（自由函数，会排序样本，空样本返回全 0） |

\`\`\`cpp
class_pool<double> samples;
samples.emplace_back(1.0);
samples.emplace_back(2.0);
samples.emplace_back(3.0);
stats s = compute_stats(std::move(samples));
// s.mean == 2.0, s.median == 2.0
\`\`\`

> 内部使用 \`tiered_sort\` 分级排序：n≤16 排序网络，n<1024 pdqsort，n≥1024 radix sort（O(n)）。

### 29.4 benchmark — 基准测量

| 接口 | 说明 |
|------|------|
| \`benchmark_ns(iterations, warmup, fn)\` | 纳秒级基准，运行 fn iterations 次 |
| \`benchmark_cycles(iterations, warmup, fn)\` | 周期级基准，精度更高（无 RDTSC 平台回退到 ns） |
| \`benchmark_p2(iterations, warmup, fn)\` | 流式基准，P² 在线估计，O(1) 空间，返回 \`p2_benchmark_result\` |

\`\`\`cpp
auto s = benchmark_ns(1000, 10, []() {
    // 被测代码
});
// s.p99 为 99 百分位延迟

// 流式基准：不存储样本，适合超大迭代次数
p2_benchmark_result r = benchmark_p2(1000000, 100, []() {
    // 被测代码
});
// r.p50, r.p90, r.p95, r.p99
\`\`\`

### 29.5 缓存命中测量

| 接口 | 说明 |
|------|------|
| \`measure_cache_hits(addresses, thresholds)\` | 测量一组地址访问的缓存命中情况 |
| \`measure_cache_batch(addresses, repeats)\` | 批量测量，取 3 次最优值，扣除基线（适合 L1/L2 精确测量） |
| \`measure_loop_cycles(fn)\` | 单次 rdtscp 包裹循环，测量总周期 |
| \`detect_cache_latency_thresholds()\` | 自适应检测缓存层级和阈值（1KB→16MB 步进扫描） |
| \`latency_thresholds\` | 阈值：l1_max/l2_max/l3_max + \`cache_levels\`（1/2/3） |
| \`cache_report\` | 报告：l1/l2/l3 命中数与率、miss 数与率、p50/p95/p99、\`active_levels\` |
| \`batch_cache_result\` | 批量结果：总周期、平均/净周期、基线周期 |
| \`make_sequential_addresses(base, count, stride)\` | 顺序访问地址序列（缓存友好） |
| \`make_random_addresses(base, count, stride, seed)\` | 随机访问地址序列（缓存不友好，确定性可复现） |

\`\`\`cpp
class_pool<const void*> addrs;
// 填充地址列表
addrs.emplace_back(&some_data);

cache_report r = measure_cache_hits(addrs);
// r.l1_hit_rate, r.miss_rate, r.avg_cycles 等

// 批量模式（精确 L1/L2 延迟）
batch_cache_result bcr = measure_cache_batch(addrs, 10);
// bcr.net_cycles_per_access 为净每次访问周期

// 自适应检测缓存层级 (不同 CPU 缓存层级不同)
latency_thresholds auto_th = detect_cache_latency_thresholds();
// auto_th.cache_levels == 1/2/3, auto_th.l1_max/l2_max/l3_max 自动标定

// 模拟 1 级缓存场景
latency_thresholds th_l1 = auto_th;
th_l1.cache_levels = 1;
cache_report r_l1 = measure_cache_hits(addrs, th_l1);
// r_l1.active_levels == 1, 仅 L1 命中分类有效
\`\`\`

> 不同 CPU 缓存层级不同（嵌入式可能仅 1-2 级），默认 \`cache_levels=3\`。可通过 \`detect_cache_latency_thresholds()\` 自动检测实际层级，或手动设置 \`cache_levels\`。

### 29.6 x86 缓存屏障

| 接口 | 说明 |
|------|------|
| \`cache_flush(p)\` | 刷新单条缓存行（\`clflush\`） |
| \`cache_flush_range(p, bytes)\` | 逐缓存行刷新范围 + \`mfence\` 尾屏障 |
| \`mfence()\` | 全屏障（Store/Load 序列化） |
| \`lfence()\` | Load 屏障 |
| \`rdtsc_fenced()\` | Intel 推荐 \`lfence; rdtsc; lfence\` 全屏障周期测量 |

\`\`\`cpp
// 冷缓存测量：先刷出缓存，再测访问延迟
cache_flush_range(&data, sizeof(data));
// 现在 data 不在任何缓存层级中
\`\`\`

> 非 x86 平台以上均为空操作。

### 29.7 P² 在线分位数

| 接口 | 说明 |
|------|------|
| \`p2_quantile(quantile)\` | 构造指定分位数估计器（0.0~1.0） |
| \`add(x)\` | 添加观测值，O(1) |
| \`estimate()\` | 当前分位数估计值 |
| \`count()\` | 已观测样本数 |
| \`reset()\` | 重置估计器 |

\`\`\`cpp
p2_quantile est(0.99);  // p99 估计器
for (int i = 0; i < 1000000; ++i)
{
    est.add(measure_something());
}
double p99 = est.estimate();
// 无需存储 100 万个样本，内存 O(1)
\`\`\`

### 29.8 CPU 频率

| 接口 | 说明 |
|------|------|
| \`estimate_cpu_ghz(calibration_ms=100)\` | 忙等校准估算 TSC 频率（GHz），非核心频率 |
| \`cpu_ghz_cached()\` | 首次调用校准，后续返回缓存值 |

\`\`\`cpp
double ghz = cpu_ghz_cached();
// 首次调用耗时 ~100ms，后续 O(1)
// 注: 测量的是 invariant TSC 频率 (恒定), 非核心频率 (受 Turbo Boost 影响)
//      rdtsc 计数速率 = TSC 频率, 不随核心频率变化
//      cycle_timer::elapsed_ns_estimated() 基于 TSC 频率, 适合相对比较
\`\`\`

> 若需精确墙钟时间，优先使用 \`timer\`（\`high_resolution_clock\`），而非 \`cycle_timer\` + 频率估算。

### 29.9 延迟异常检测

| 接口 | 说明 |
|------|------|
| \`latency_anomaly_detector\` | 结构体，内置 p50/p99 P² 估计器 |
| \`add(latency_ns)\` | 添加延迟样本，建立基线 |
| \`is_anomaly(latency_ns)\` | 判断当前延迟是否异常（超过 p99 × multiplier） |
| \`anomaly_threshold()\` | 当前异常阈值（p99 × multiplier） |

\`\`\`cpp
latency_anomaly_detector detector;
detector.multiplier = 3.0;  // 超过 3x p99 视为异常

for (auto& e : entities)
{
    timer t;
    process(e);
    double ns = t.elapsed_ns();
    detector.add(ns);
    if (detector.is_anomaly(ns))
    {
        // 延迟异常，记录或告警
    }
}
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 非 x86 平台依赖 \`rdtsc\` 精度 | \`TIME_HAS_RDTSC=0\`，返回 0 | 非 x86 平台用 \`benchmark_ns\` 而非 \`benchmark_cycles\` |
| \`compute_stats\` 传入空 samples | count=0，所有统计量为 0 | 先检查 samples 非空 |
| \`measure_cache_hits\` 地址列表含无效指针 | 访问野指针崩溃 | 确保所有地址有效 |
| \`cycle_timer\` 跨 CPU 频率变化测量 | 频率动态调整导致估算不准 | 短时间测量或锁定频率 |
| 大样本用 \`benchmark_ns\` 存储全部样本 | 内存占用 O(n) | 大样本（>10M）用 \`benchmark_p2\`，O(1) 空间 |

---
`
};

window.DOCS_DATA['multi_block_bitmask'] = {
  id: 'multi_block_bitmask',
  title: "multi_block_bitmask — 多块位掩码存储",
  category: 'tools',
  icon: 'B',
  order: 30,
  content: `## 29. multi_block_bitmask — 多块位掩码存储

\`include/part/multi_block_bitmask.hpp\`，无命名空间。每槽 1+ 个 64 位块的多块位掩码容器，块 0 内嵌，块 1+ 按需分配。通用位掩码场景（布隆过滤、稀疏集合、组件标签、哈希位图等）均适用。

### 接口

#### 静态辅助

| 接口 | 说明 |
|------|------|
| \`static constexpr uint32_t bits_per_block\` | 每块位数（64） |
| \`static constexpr uint32_t block_count_for_bits(size_t bit_count)\` | 给定位数算所需块数 |

#### 块管理

| 接口 | 说明 |
|------|------|
| \`reserve_blocks(uint32_t num_blocks)\` | 预分配掩码块数（仅扩容，不缩容） |
| \`num_blocks() const\` | 当前每槽掩码块数（= 1 + overflow_block_count_） |
| \`overflow_entity_count() const\` | 已分配 overflow 的槽位数 |

#### 容量管理

| 接口 | 说明 |
|------|------|
| \`ensure_entity(uint32_t slot)\` | 确保槽位容量（必要时自动扩容） |
| \`resize_entities(uint32_t new_count)\` | 批量扩容到 \`new_count\` 个槽位 |
| \`increase_capacity(size_t new_slot_capacity)\` | 扩容到指定槽位容量（只增不减，不改变 \`size\`） |
| \`reserve_exact(size_t new_slot_capacity)\` | 精确预留容量（不改变 \`size\`） |
| \`shrink_to_fit()\` | 缩容到实际槽位数 |
| \`reduce_capacity(size_t new_slot_capacity)\` | 缩容到指定容量（超出部分截断） |
| \`clear()\` | 清空所有数据（\`size=0\`，\`capacity\`/\`num_blocks\` 保留） |
| \`size() const\` | 当前槽位数 |
| \`capacity() const\` | 槽位容量 |
| \`empty() const\` | 是否为空 |
| \`size_bytes() const\` | 已用内存（字节） |
| \`capacity_bytes() const\` | 容量内存（字节） |

#### 单位写入（带边界检查）

| 接口 | 说明 |
|------|------|
| \`set_bit(slot, block_idx, bit_offset)\` | 设置位（必要时自动扩容 slot） |
| \`clear_bit(slot, block_idx, bit_offset)\` | 清除位 |
| \`clear_entity(slot)\` | 清零该槽位所有块 |

#### 单位写入（无边界检查）

调用方保证 \`slot < size()\` 且 \`block_idx < num_blocks()\`。

| 接口 | 说明 |
|------|------|
| \`set_bit_no_check(slot, block_idx, bit_offset)\` | 设置位 |
| \`clear_bit_no_check(slot, block_idx, bit_offset)\` | 清除位 |

#### 整块写入

替代读-改-写，直接写入整块值。

| 接口 | 说明 |
|------|------|
| \`set_block_value(slot, block_idx, uint64_t value)\` | 整块赋值 |
| \`or_block_value(slot, block_idx, uint64_t mask)\` | 原地或 |
| \`and_block_value(slot, block_idx, uint64_t mask)\` | 原地与 |
| \`xor_block_value(slot, block_idx, uint64_t mask)\` | 原地异或 |

#### 批量位操作（同块多位）

| 接口 | 说明 |
|------|------|
| \`set_bits_at(slot, block_idx, std::span<const uint32_t> offsets)\` | 批量设置多位 |
| \`clear_bits_at(slot, block_idx, std::span<const uint32_t> offsets)\` | 批量清除多位 |
| \`toggle_bits_at(slot, block_idx, std::span<const uint32_t> offsets)\` | 批量翻转多位 |

#### 整槽多块读写

| 接口 | 说明 |
|------|------|
| \`assign_slot(slot, std::span<const uint64_t> data)\` | 写入整槽所有块（\`data[0]\`→block 0，\`data[1]\`→block 1，…；若 \`data.size() > num_blocks()\` 自动 \`reserve_blocks\`） |
| \`copy_slot_to(slot, std::span<uint64_t> dst) const\` | 读取整槽所有块到 \`dst\`（\`dst.size()\` 决定读取块数，不足部分补零） |

#### 查询接口

| 接口 | 说明 |
|------|------|
| \`get_block(slot, block_idx) const\` | 获取块值（越界返回 0） |
| \`test_bit(slot, block_idx, bit_offset) const\` | 测试位是否置位 |
| \`any_set_in_block(slot, block_idx) const\` | 块是否非零 |
| \`any_set(slot) const\` | 槽位是否有任意位置位（含 overflow） |
| \`is_zero(slot) const\` | 槽位是否全零 |
| \`count_set_bits(slot) const\` | 槽位置位数（含 overflow，基于 \`std::popcount\`） |
| \`find_first_set(slot, out_block, out_offset) const\` | 找首个置位，写入 \`out_block\`/\`out_offset\`，返回是否找到 |
| \`find_last_set(slot, out_block, out_offset) const\` | 找末个置位 |
| \`find_next_set(slot, after_block, after_offset, out_block, out_offset) const\` | 从指定位置之后找下一个置位 |

#### 遍历接口

回调受 \`std::invocable\` concepts 约束，编译期捕获签名错误。

| 接口 | 回调签名 | 说明 |
|------|---------|------|
| \`for_each_set_bit(slot, func) const\` | \`func(uint32_t block_idx, uint32_t bit_offset)\` | 遍历该槽位所有置位 |
| \`for_each_set_slot(func) const\` | \`func(uint32_t slot)\` | 遍历所有非空槽位 |
| \`for_each_set_bit_global(func) const\` | \`func(uint32_t slot, uint32_t block_idx, uint32_t bit_offset)\` | 全局遍历所有置位 |
| \`count_set_bits_global() const\` | — | 全局置位总数 |

#### 视图接口

| 接口 | 说明 |
|------|------|
| \`inline_span() noexcept\` / \`inline_span() const noexcept\` | 返回 block 0 全局视图 \`std::span<uint64_t>\` / \`std::span<const uint64_t>\` |
| \`overflow_span(slot) noexcept\` / \`overflow_span(slot) const noexcept\` | 返回某槽位的 overflow 块视图（block 1+），未分配则返回空 span |

#### 复制与交换

| 接口 | 说明 |
|------|------|
| \`multi_block_bitmask(const multi_block_bitmask&)\` | 深拷贝构造 |
| \`operator=(const multi_block_bitmask&)\` | 深拷贝赋值 |
| \`multi_block_bitmask(multi_block_bitmask&&)\` | 移动构造 |
| \`operator=(multi_block_bitmask&&)\` | 移动赋值 |
| \`swap(multi_block_bitmask&) noexcept\` | 成员交换 |
| \`clone() const\` | 显式深拷贝工厂 |
| 自由函数 \`swap(a, b) noexcept\` | 自由交换 |

#### 集合运算（原地，处理共同 slot 与共同块）

| 接口 | 说明 |
|------|------|
| \`and_with(const multi_block_bitmask& o)\` | \`this &= o\`（\`this\` 中 \`o\` 不存在的 slot 清零） |
| \`or_with(const multi_block_bitmask& o)\` | \`this \\|= o\`（仅处理共同 slot，不扩容） |
| \`xor_with(const multi_block_bitmask& o)\` | \`this ^= o\` |
| \`subtract(const multi_block_bitmask& o)\` | \`this &= ~o\`（差集） |
| \`overlaps(const multi_block_bitmask& o) const\` | 是否与 \`o\` 有任意共同置位 |
| \`contains_all(const multi_block_bitmask& o) const\` | \`this\` 是否包含 \`o\` 的所有置位（超集） |
| \`equals(const multi_block_bitmask& o) const\` | 是否相等 |

#### 内存压缩

| 接口 | 说明 |
|------|------|
| \`compact_slot(slot)\` | 若该槽位 overflow 全零则释放 |
| \`compact_all()\` | 全局压缩所有全零 overflow |

### 使用

\`\`\`cpp
multi_block_bitmask masks;
masks.reserve_blocks(2);                    // 预分配 2 块 = 支持 128 位
masks.ensure_entity(0);
masks.set_bit(0, 0, 5);                     // 槽 0 块 0 位 5
masks.set_bit(0, 1, 10);                    // 槽 0 块 1 位 10
uint64_t b0 = masks.get_block(0, 0);
uint64_t b1 = masks.get_block(0, 1);
masks.clear_entity(0);                      // 清零整槽

// 查询
bool has = masks.test_bit(0, 0, 5);
bool any = masks.any_set(0);
uint32_t cnt = masks.count_set_bits(0);
uint32_t blk, off;
if (masks.find_first_set(0, blk, off)) { /* ... */ }

// 批量位操作
uint32_t offsets[] = {1, 3, 5, 7};
masks.set_bits_at(0, 0, offsets);

// 整块写入
masks.set_block_value(0, 0, 0xFFFFFFFFULL);
masks.or_block_value(0, 0, 0x1ULL << 10);

// 整槽读写
uint64_t data[] = {0xFF, 0xAA};
masks.assign_slot(0, data);
uint64_t out[2];
masks.copy_slot_to(0, out);

// 视图
std::span<const uint64_t> inline_view = masks.inline_span();
std::span<const uint64_t> ovf_view = masks.overflow_span(0);

// 遍历
masks.for_each_set_bit(0, [](uint32_t blk, uint32_t off) { /* ... */ });
masks.for_each_set_slot([](uint32_t slot) { /* ... */ });
masks.for_each_set_bit_global([](uint32_t slot, uint32_t blk, uint32_t off) { /* ... */ });
size_t total = masks.count_set_bits_global();

// 集合运算
multi_block_bitmask a, b;
a.reserve_blocks(2); b.reserve_blocks(2);
a.ensure_entity(0); b.ensure_entity(0);
a.set_bit(0, 0, 1); b.set_bit(0, 0, 2);
a.or_with(b);                             // a 现在 = {1, 2}
bool inter = a.overlaps(b);
bool sup = a.contains_all(b);
bool eq = a.equals(b);

// 复制与交换
multi_block_bitmask c = a.clone();
multi_block_bitmask d = std::move(c);
d.swap(a);
swap(d, a);                               // 自由 swap

// 内存压缩
masks.clear_bit(0, 1, 10);
masks.compact_slot(0);                     // overflow 全零则释放
masks.compact_all();                       // 全局压缩

// 扩容 / 缩容 / 状态查询 (与 class_pool / dense 命名一致)
masks.increase_capacity(10000);
masks.reserve_exact(50000);
size_t cap = masks.capacity();
size_t n   = masks.size();
masks.shrink_to_fit();
masks.reduce_capacity(1000);
masks.clear();
bool isEmpty = masks.empty();
size_t used  = masks.size_bytes();
size_t total_cap = masks.capacity_bytes();
size_t ovf_count = masks.overflow_entity_count();

// 静态辅助
constexpr uint32_t blk = multi_block_bitmask::block_count_for_bits(200);  // = 4
\`\`\`

### 批量注册推荐用法

逐个 \`ensure_entity\` 每次都做边界检查 + size 读取 + 可能的扩容。批量注册时用三步法减少重复检查:

\`\`\`cpp
multi_block_bitmask masks;
masks.reserve_blocks(2);                   // 步骤 0: 预分配块数 (必须先于步骤 1)

// 三步法批量注册 N 个槽位
masks.increase_capacity(N);                // 步骤 1: 扩 capacity
masks.resize_entities(N);                  // 步骤 2: 撑 size
for (uint32_t i = 0; i < N; ++i)
{
    masks.set_bit_no_check(i, 0, i & 63);  // 步骤 3: 跳过边界检查写入
}
\`\`\`

| 步骤 | 作用 | 跳过后果 |
|------|------|----------|
| \`increase_capacity(N)\` | 预留容量,避免 \`resize_entities\` 内部触发 realloc | 性能回退到逐次扩容路径 |
| \`resize_entities(N)\` | 把 \`size()\` 撑到 N,使 \`set_bit_no_check\` 访问合法 | \`set_bit_no_check\` 越界写崩 |
| \`set_bit_no_check(...)\` | 跳过边界检查的写入 | 退回 \`set_bit\` 的带检查路径 |

> 注:\`increase_capacity\` 只扩 capacity 不动 size,\`set_bit_no_check\` 要求 \`slot < size()\`。
> 因此步骤 1 和步骤 2 **都不可省略**,否则 \`set_bit_no_check\` 越界。

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 注册组件后再调 \`reserve_blocks\` | 触发现有溢出槽位扩容 | 启动时预估类型数，注册前调 \`reserve_blocks\` |
| 对未 \`ensure_entity\` 的槽位调 \`set_bit_no_check\` | 越界写崩 | 先 \`ensure_entity\`,或批量场景用 \`increase_capacity\` + \`resize_entities\` 三步法 |
| \`increase_capacity(N)\` 后直接 \`set_bit_no_check\` | capacity 扩了但 size 未撑,越界写崩 | 必须再调 \`resize_entities(N)\` 撑 size |
| 对 \`block_idx >= num_blocks()\` 调 \`set_bit\` | 静默丢弃（不会扩容块数） | 先 \`reserve_blocks\` 扩容 |
| 假设 \`get_block(slot, b)\` 跨进程稳定 | 布局可能变化 | 仅当前进程内有效 |
| \`reduce_capacity(n)\` 传小于当前 \`size\` 的值 | 超出部分被截断丢失 | 缩容前确认 \`n >= size()\`，或先 \`shrink_to_fit\` |
| \`clear()\` 后假设 \`num_blocks()\` 归 1 | \`clear\` 只清数据不重置块数 | 重置块数需重新构造实例 |
| 集合运算后假设 \`this\` 的 \`num_blocks()\` 与 \`o\` 一致 | 集合运算不扩容块数，仅处理共同块 | 需要扩容先调 \`reserve_blocks\` |
| \`overflow_span(slot)\` 返回的 span 跨 \`reserve_blocks\` 使用 | \`reserve_blocks\` 会重分配 overflow 内存，span 失效 | 视图即时使用，不跨写操作持有 |

---
`
};

window.DOCS_DATA['reflection_meta'] = {
  id: 'reflection_meta',
  title: "reflection — 反射元数据与存储",
  category: 'tools',
  icon: 'R',
  order: 31,
  content: `## 30. reflection — 反射元数据与存储

\`#include "reflection/reflection.hpp"\`，命名空间 \`reflect\`。\`noexcept\`。

反射模块元数据结构与存储接口。高层使用接口见 [§ 11. reflection — 反射模块使用](#11-reflection--反射模块使用)。

### 30.1 元数据结构

| 结构 | 字段 |
|------|------|
| \`field_meta\` | \`name\`, \`offset\`, \`type_id\`, \`is_const\`, \`is_private\`, \`array_rank\`, \`reserved\`, \`total_elements\`, \`extents[4]\`, \`element_stride\` |
| \`method_meta\` | \`name\`, \`arg_count\`, \`return_type_id\`, \`invoker\`, \`is_const\`, \`is_static\` |
| \`type_meta\` | \`name\`, \`registered\`, \`field_count\`, \`method_count\`, \`size\`, \`align\`, \`type_id\`, \`fields\`, \`methods\` |
| \`MAX_TYPE_ID\` | 类型槽位上限（65536） |
| \`MAX_FIELDS_PER_TYPE\` | 单类型字段上限（256） |
| \`MAX_METHODS_PER_TYPE\` | 单类型方法上限（256） |

\`field_meta\` 数组字段：\`array_rank=1~4\`、\`extents[0..rank-1]\` 为各维元素数、\`total_elements\` 为总元素数、\`element_stride\` 为元素步长（字节）。标量字段 \`array_rank=0\`。

### 30.2 storage 接口

| 接口 | 说明 |
|------|------|
| \`storage::register_type<T>(name)\` | 注册类型，聚合类型自动遍历公有字段 |
| \`storage::register_type_only<T>(name)\` | 只注册类型元信息，不自动遍历字段。用于无字段类型或配合 \`register_array_field\` 手动注册 |
| \`storage::register_private_offsets<T>(descs, count)\` | 手填偏移量注册私有成员 |
| \`storage::register_field<T, M, Ptr>(name)\` | 成员指针注册字段，需先 \`register_type_only\` |
| \`storage::register_member_auto<T, M, Ptr>(type_name, field_name)\` | 注册成员（标量/数组统一入口），自动判断字段类别 |
| \`storage::register_array_field<T, M, Ptr>(name, rank, extents, element_type_id)\` | 注册数组字段（C 数组/std::array） |
| \`storage::register_array_field_auto<T, M, Ptr>(name)\` | 注册数组字段，自动推导 rank/extents/element_type |
| \`storage::register_method<Fn>(name)\` | 注册成员方法 |
| \`storage::register_static_method<C, Fn>(name)\` | 注册静态方法 |
| \`storage::get_type(tid)\` | 按类型 id 查询 \`type_meta*\` |
| \`storage::find_type(name)\` | 按类型名查询 \`type_meta*\` |
| \`reflect::global()\` | 全局 storage 对象 |

---
`
};

window.DOCS_DATA['aggregate_reflect'] = {
  id: 'aggregate_reflect',
  title: "aggregate_reflect — 聚合类型字段遍历",
  category: 'tools',
  icon: 'G',
  order: 32,
  content: `## 31. aggregate_reflect — 聚合类型字段遍历

\`#include "part/aggregate_reflect.hpp"\`，全局命名空间。\`noexcept\`。编译期常量。

编译期探测聚合类型字段数并遍历字段。仅支持聚合类型（无自定义构造、无私有/受保护非静态数据成员、无虚函数、无基类）。非聚合类型字段数为 0。

### 字段上限配置

通过宏 \`REFLECT_MAX_FIELDS\` 配置自动反射字段上限，支持 16/32/64/128/256。默认 64。配置统一在 \`include/config/reflect_config.hpp\`，修改该文件或在 include 前定义对应宏即可调整：

\`\`\`cpp
// 默认 64 字段
#include "part/aggregate_reflect.hpp"

// 扩展到 128 字段: 编辑 config/reflect_config.hpp 或在 include 前定义
#define REFLECT_MAX_FIELDS 128
#include "part/aggregate_reflect.hpp"
\`\`\`

字段数超过 \`REFLECT_MAX_FIELDS\` 时触发编译期 \`static_assert\` 错误。

### 接口

| 接口 | 说明 |
|------|------|
| \`aggregate_field_count_v<T>\` | 编译期常量，类型 T 的字段数（非聚合为 0，上限由 \`REFLECT_MAX_FIELDS\` 决定） |
| \`for_each_aggregate_member(obj, f)\` | 遍历对象字段，\`f(member, idx)\` 接收引用和索引 |
| \`for_each_aggregate_member(obj, f)\` (const) | const 对象版本，\`f\` 接收 const 引用 |
| \`member_offset(M T::*member)\` | 通过成员指针计算偏移量（需访问权限） |

### 使用

\`\`\`cpp
#include "part/aggregate_reflect.hpp"

struct Vec3 { float x, y, z; };
struct Pod4 { int a, b, c, d; };
struct Empty {};

// 编译期字段计数
static_assert(aggregate_field_count_v<Vec3> == 3);
static_assert(aggregate_field_count_v<Pod4> == 4);
static_assert(aggregate_field_count_v<Empty> == 0);

// 遍历字段
Vec3 v{1.0f, 2.0f, 3.0f};
for_each_aggregate_member(v, [&](auto& member, size_t idx) {
    std::cout << "field " << idx << " = " << member << "\\n";
});

// 修改字段
for_each_aggregate_member(v, [&](auto& member, size_t idx) {
    (void)idx;
    member = 0.0f;
});

// const 对象遍历
const Vec3 cv{5.0f, 6.0f, 7.0f};
for_each_aggregate_member(cv, [&](const auto& member, size_t idx) {
    std::cout << idx << ": " << member << "\\n";
});

// 成员偏移量
size_t off_y = member_offset(&Vec3::y);  // 4
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 用于非聚合类型 | 字段数为 0，遍历不执行 | 仅用于聚合类型 |
| 字段含 C 数组 | 数组被识别为多字段聚合 | 避免在含数组字段类型上使用 |
| 字段数超过 \`REFLECT_MAX_FIELDS\` | 编译期 \`static_assert\` 错误 | include 前增大 \`REFLECT_MAX_FIELDS\` |
| \`for_each_aggregate_member\` 修改 const 对象 | 编译错误 | 用非 const 对象修改字段 |

---
`
};

window.DOCS_DATA['type_erasure'] = {
  id: 'type_erasure',
  title: "type_erasure — 类型擦除方法调用器",
  category: 'tools',
  icon: 'E',
  order: 33,
  content: `## 32. type_erasure — 类型擦除方法调用器

\`#include "part/type_erasure.hpp"\`，全局命名空间。\`noexcept\`。

将任意成员函数指针、const 成员函数指针、静态函数指针包装为统一签名的 \`invoker_func\` 函数指针。支持任意参数数量。

### 接口

| 接口 | 说明 |
|------|------|
| \`mfn_traits<MFnType>\` | 方法签名 traits，提取 \`class_type\`/\`return_type\`/\`arg_count\`/\`is_const\`/\`is_static\` |
| \`mfn_invoker_t<Fn, MFnType>::invoke(obj, args, result)\` | 成员方法 invoker（普通/const 统一） |
| \`sfn_invoker_t<Fn, MFnType>::invoke(obj, args, result)\` | 静态方法 invoker（\`obj\` 忽略） |
| \`arg_ids_maker<MFnType>::make()\` | 生成参数类型 id 的 \`dense<int>\` |
| \`return_type_id<R>()\` | 返回类型 id（void 为 -1） |
| \`invoker_func\` | invoker 函数指针类型 |

### invoker 签名

\`\`\`cpp
using invoker_func = void(*)(void* obj, const void* const* args, void* result);
\`\`\`

- \`obj\`：对象指针（静态方法传 \`nullptr\`）
- \`args\`：参数指针数组，每个元素指向一个参数
- \`result\`：返回值缓冲区（void 方法不写）

### 使用

\`\`\`cpp
#include "part/type_erasure.hpp"

class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
};

int free_multiply(int a, int b) { return a * b; }

// === 普通/const 成员方法 ===
Calculator calc;
using FnType = decltype(&Calculator::add);
constexpr FnType fn = &Calculator::add;

int a = 10, b = 20;
const void* args[] = { &a, &b };
alignas(alignof(std::max_align_t)) char result_buf[64];

mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
int result = *reinterpret_cast<int*>(result_buf);  // 30

// === 静态方法 ===
using FnType2 = decltype(&free_multiply);
constexpr FnType2 fn2 = &free_multiply;
sfn_invoker_t<fn2, FnType2>::invoke(nullptr, args, result_buf);
int product = *reinterpret_cast<int*>(result_buf);  // 200

// === 通过 invoker_func 调用 ===
invoker_func inv = &mfn_invoker_t<fn, FnType>::invoke;
inv(&calc, args, result_buf);

// === traits 与元数据 ===
using Traits = mfn_traits<decltype(&Calculator::add)>;
static_assert(Traits::arg_count == 2);
static_assert(!Traits::is_const);
static_assert(!Traits::is_static);

dense<int> arg_ids = arg_ids_maker<decltype(&Calculator::add)>::make();
// arg_ids.size() == 2, arg_ids[0] == type_id::get_type_id<int>()

int ret_id = return_type_id<int>();      // 返回 int 的 type_id
int void_id = return_type_id<void>();    // -1
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 返回值缓冲区未对齐 | placement new 未对齐访问崩溃 | \`alignas(alignof(std::max_align_t))\` |
| 参数数量与签名不符 | 未定义行为 | 调用前确认 \`arg_count\` |
| 返回 std::string 未析构 | 内存泄漏 | 手动调用 \`~basic_string()\` |
| 静态方法传非空 \`obj\` | 被忽略 | 静态方法传 \`nullptr\` |

---
`
};

window.DOCS_DATA['member_offset'] = {
  id: 'member_offset',
  title: "member_offset — 成员偏移量访问",
  category: 'tools',
  icon: 'O',
  order: 34,
  content: `## 33. member_offset — 成员偏移量访问

\`#include "part/member_offset.hpp"\`，全局命名空间。\`noexcept\`。

通过偏移量访问对象成员，支持突破私有访问限制。提供 \`offset_desc\` 结构用于批量描述字段。

### 接口

| 接口 | 说明 |
|------|------|
| \`offset_of(M T::*member)\` | 通过成员指针计算偏移量（需访问权限） |
| \`offset_access<M>(obj, offset)\` | 按偏移量访问成员引用 |
| \`offset_access<M>(obj, offset)\` (const) | const 对象版本 |
| \`ub_access<T, M>(obj, offset)\` | UB 反向构造成员指针，突破私有访问 |
| \`offset_desc\` | 字段描述结构 \`{name, offset, type_id}\` |

### 使用

\`\`\`cpp
#include "part/member_offset.hpp"

struct Pod3 { float x, y, z; };
struct Mixed { char c; int i; double d; };

class Account
{
    int balance_;
public:
    Account(int b) : balance_(b) {}
    int get_balance() const { return balance_; }
};

// === offset_of 成员指针偏移 ===
size_t off_x = offset_of(&Pod3::x);  // 0
size_t off_y = offset_of(&Pod3::y);  // 4
size_t off_z = offset_of(&Pod3::z);  // 8

// === offset_access 直接指针访问 ===
Pod3 v{1.0f, 2.0f, 3.0f};
float y = offset_access<float>(&v, 4);   // 2.0
offset_access<float>(&v, 8) = 100.0f;    // 修改 z

// const 对象
const Pod3 cv{5.0f, 6.0f, 7.0f};
float cx = offset_access<float>(&cv, 0);  // 5.0

// 混合类型
Mixed m{'A', 42, 3.14};
char c = offset_access<char>(&m, 0);      // 'A'
int i = offset_access<int>(&m, 4);        // 42
double d = offset_access<double>(&m, 8);  // 3.14

// === ub_access 突破私有访问 ===
Account acc(100);
// 假设 balance_ 在偏移 0 (首成员)
int bal = ub_access<Account, int>(acc, 0);  // 100
ub_access<Account, int>(acc, 0) = 999;      // 修改私有成员
acc.get_balance();  // 999

// === offset_desc 批量描述 ===
offset_desc descs[] = {
    {"name_",    0,  type_id::get_type_id<std::string>()},
    {"balance_", 32, type_id::get_type_id<int>()}
};
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| \`ub_access\` 偏移量错误 | 访问错误内存 | 用 \`offsetof\` 或编译期推导确认偏移量 |
| \`ub_access\` 用于虚函数类 | vptr 干扰偏移布局 | 仅用于无非虚函数的类 |
| 假设 \`ub_access\` 可移植 | 严格 UB，依赖编译器实现 | 仅在受控环境使用 |
| 对齐错误的偏移量访问 | 未对齐访问崩溃 | 确保偏移量与类型对齐匹配 |
`
};

window.DOCS_DATA['t_fun'] = {
  id: 't_fun',
  title: "t_fun — 函数类型延迟调用器",
  category: 'tools',
  icon: 'F',
  order: 35,
  content: `## 34. t_fun — 函数类型延迟调用器

\`#include "part/t_fun.hpp"\`，全局命名空间。\`noexcept\`。

编译期推导函数类型的延迟调用器，支持函数指针、成员函数指针（const/非 const）。用户无需输入模板参数，通过 CTAD 自动推导。void 返回值特化，\`result_ptr()\` 返回 \`nullptr\`。

### 接口

| 接口 | 说明 |
|------|------|
| \`t_fun v{f, args...}\` | 构造，绑定函数与参数（CTAD 推导） |
| \`v()\` | 用绑定参数调用 |
| \`v(a, b)\` | 用传入参数调用，不修改绑定参数 |
| \`v.fun()\` / \`v.fun(a, b)\` | 等价 operator() |
| \`v.result_ptr()\` | 非 void 返回 \`R*\`；void 返回 \`nullptr\` |
| \`v.result_reset()\` | 非 void 重置为默认值；void 无副作用 |
| \`v.target()\` | 原始可调用对象（函数指针 / 成员函数指针） |
| \`v.bound_arg<I>()\` | 第 I 个绑定参数引用（const/非 const 重载） |
| \`v.set_arg<I>(v)\` | 修改第 I 个绑定参数 |
| \`v.object()\` | 成员函数指针版本，对象指针 |
| \`v.arity\` | 编译期常量，参数数量 |
| \`v.return_type\` | 编译期类型别名，返回值类型 |
| \`v.args_tuple\` | 编译期类型别名，参数 tuple |
| \`v.then_call(g)\` | 链式调用，\`g(v())\`；void 版本 \`v(); g();\` |
| \`v.compose(g, more...)\` | 多级组合，依次调用 g, more... |
| \`v.bind_front(args...)\` | 设置前 N 个绑定参数 |
| \`v.apply_n(count)\` | 重复调用 N 次，返回最后结果 |
| \`v.apply_range(data, n)\` | 对数组批量调用（arity==1） |
| \`v.swap(other)\` | O(1) 交换 |
| \`v == other\` / \`v != other\` | 比较 target 与绑定参数 |
| \`v.hash()\` | 哈希值 |
| \`v.empty()\` | 是否已释放 |
| \`v.release()\` | 释放所有权 |
| \`v.reset(args...)\` | 重置所有绑定参数 |
| \`os << v\` | 流输出诊断信息 |

### 使用

\`\`\`cpp
#include "part/t_fun.hpp"
#include <string>

int  free_add(int a, int b) { return a + b; }
void free_void(int x) { (void)x; }
void free_noop() {}
std::string free_make_str(const char* s) { return std::string(s); }

class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
};

// === 函数指针: 绑定参数调用 ===
t_fun v1{free_add, 10, 20};
v1();                  // 30, 用绑定的 10,20
v1.fun();              // 30, 等价
v1(3, 4);              // 7,  用传入参数, 不修改绑定参数
v1();                  // 仍 30, 绑定参数未被覆盖

// === result_ptr / result_reset ===
int* p = v1.result_ptr();   // &result_, *p == 30
v1.result_reset();          // *p 变为 0
v1.result_ptr();            // &result_, *p == 0

// === target / bound_arg / set_arg ===
v1.target()(10, 20);        // 30, 直接调用原始函数指针
v1.bound_arg<0>();          // 10
v1.set_arg<0>(100);
v1.set_arg<1>(200);
v1();                       // 300

// === 编译期元信息 ===
static_assert(decltype(v1)::arity == 2);
static_assert(std::is_same_v<decltype(v1)::return_type, int>);

// === void 返回值 ===
t_fun v2{free_void, 42};
v2();                       // 调用, 无返回值
void* p2 = v2.result_ptr(); // nullptr
v2.result_reset();          // 无副作用

t_fun v3{free_noop};        // 无参函数
v3();

// === std::string 返回值 ===
t_fun v4{free_make_str, "abc"};
std::string s = v4();       // "abc"
std::string* p4 = v4.result_ptr();  // &result_, *p4 == "abc"
v4.result_reset();          // 空字符串

// === 成员函数指针 ===
Calculator calc;
t_fun m1{&Calculator::add, &calc, 10, 20};
m1();                       // 30, 绑定对象与参数
m1(3, 4);                   // 7,  带参调用
m1.object();                // &calc
m1.target();                // &Calculator::add

// const 成员函数
t_fun m2{&Calculator::is_positive, &calc, 5};
m2();                       // true

// void 成员函数
t_fun m3{&Calculator::no_return, &calc, 0};
m3();

// === 移动与拷贝 ===
t_fun v5{std::move(v1)};    // 移动构造
t_fun v6{v5};                // 拷贝构造
v5 = std::move(v6);          // 移动赋值

// === then_call 链式调用 ===
t_fun v7{free_add, 10, 20};
int doubled = v7.then_call([](int x){ return x * 2; });  // 60
// void 版本: v(); g();

// === compose 多级组合 ===
int composed = v7.compose(
    [](int x){ return x * 2; },
    [](int x){ return x + 1; }
);  // (30*2)+1 = 61

// === bind_front 设置前 N 个参数 ===
t_fun v8{free_add, 0, 0};
v8.bind_front(100);          // 设置第 0 个参数为 100
v8.set_arg<1>(200);
v8();                        // 300

// === apply_n 批量调用 ===
t_fun v9{free_add, 1, 2};
int last = v9.apply_n(1000); // 调用 1000 次, 返回最后结果 (3)

// === apply_range 范围应用 (arity==1) ===
t_fun v10{free_identity};
int data[] = {1, 2, 3, 4, 5};
int last_r = v10.apply_range(data, 5);  // 对每个元素调用, 返回最后 (5)

// === swap 交换 ===
t_fun a{free_add, 1, 2};
t_fun b{free_add, 10, 20};
a.swap(b);
a();                         // 30 (交换后)
b();                         // 3

// === operator== / != ===
t_fun c1{free_add, 10, 20};
t_fun c2{free_add, 10, 20};
t_fun c3{free_add, 30, 40};
c1 == c2;                    // true (target 与绑定参数相同)
c1 != c3;                    // true

// === hash ===
size_t h = c1.hash();        // 哈希值

// === empty / release ===
t_fun v11{free_add, 10, 20};
v11.empty();                 // false
v11.release();               // 释放所有权
v11.empty();                 // true

// === reset 重置所有绑定参数 ===
t_fun v12{free_add, 0, 0};
v12.reset(5, 6);
v12();                       // 11

// === operator<< 流输出 ===
t_fun v13{free_add, 10, 20};
std::cout << v13;            // [t_fun arity=2 target=set]
v13.release();
std::cout << v13;            // [t_fun arity=2 target=null]
\`\`\`

### 注意事项

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 返回值类型不可默认构造 | 编译期报错 | 确保 \`R\` 可默认构造 |
| \`set_arg<I>\` 越界 | 编译期报错 | I < arity |
| \`bound_arg<I>\` 越界 | 编译期报错 | I < arity |
| 带参调用参数数与签名不符 | 编译期报错 | 参数数量与函数签名一致 |
| 成员函数指针版本不传对象 | 编译期报错 | 构造时第二参数传 \`C*\` |
| const 成员函数传非 const 对象指针 | 编译期报错 | 传 \`const C*\` |
| \`result_ptr()\` 未调用就解引用 | 读到默认构造值 | 先调用 \`v()\` |
| 使用后忘记 \`result_reset()\` | 读到上次调用结果 | 需要重置时调用 |
| \`apply_range\` 用于 arity != 1 | 编译期报错 | 仅 arity == 1 时可用 |
| \`release\` 后再调用 | 解引用空指针 | 调用前检查 \`empty()\` |
| \`bind_front\` 参数数超过 arity | 编译期报错 | 参数数 <= arity |
| \`reset\` 参数数与 arity 不符 | 编译期报错 | 参数数 == arity |
| \`then_call\` 的 g 参数类型不匹配 | 编译期报错 | g 接受 R 类型参数 |
`
};

