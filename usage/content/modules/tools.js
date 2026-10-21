window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['operating_message'] = {
  id: 'operating_message',
  title: "operating_message — 操作消息",
  category: 'tools',
  icon: 'O',
  order: 14,
  content: `## 14. operating_message — 操作消息

记录操作结果（成功/失败）和调试信息。核心特性：

- **单一 write 接口**：所有写入经由 \`write\`，级别/错误码/位置/格式化以标签形式混入参数包，任意组合
- **值语义返回**：\`single_class_set\` / \`manager\` 的 \`add\` / \`add_batch\` / \`hard_remove\` / \`soft_remove\` 均按值返回 \`operating_message\`
- **粘性 false 语义**：单个返回值对象一旦失败就保持 false，只有 \`reset()\` 能恢复
- **零业务宏**：全部能力由模板与标签表达；消息写入由运行时开关 \`message_recording_enabled()\` 控制
- **全局开关**：\`message_recording_enabled()\` 运行时控制是否写入字符串
- **错误码**：32 位，\`code()\` 返回（0=成功），\`is_code()\` 程序化判断错误类型
- **日志级别过滤**：\`msg_level\` 枚举 + \`min_level\` 过滤，被过滤的级别跳过全部写入

### msg_level 日志级别

\`\`\`cpp
enum class msg_level : uint8_t {
    debug = 0,  // 最低
    info  = 1,  // 默认 min_level
    warn  = 2,
    error = 3   // 最高
};
\`\`\`

### 错误码常量

| 常量 | 含义 |
|------|------|
| \`om_err_none\` | 无错误（成功） |
| \`om_err_type_mismatch\` | 类型不匹配 |
| \`om_err_invalid_entity\` | 实体无效 |
| \`om_err_version_mismatch\` | 版本不匹配 |
| \`om_err_out_of_range\` | 索引越界 |
| \`om_err_null_pointer\` | 空指针 |
| \`om_err_capacity_exceeded\` | 容量超限 |
| \`om_err_not_found\` | 未找到 |
| \`om_err_already_exists\` | 已存在 |
| \`om_err_out_of_memory\` | 内存分配失败 |

\`\`\`cpp
operating_message om;
om.write(false, om_err_not_found);

if (om.is_code(om_err_not_found))
{
    // 处理失败
}
\`\`\`

错误码跨进程稳定，取值始终落在正 \`int32\` 范围。自定义错误码用 \`om_error_code("my_error")\` 生成，也可用 \`msg::errc(value)\` 包装已有数值。

### 接口

| 接口 | 说明 |
|------|------|
| \`message_recording_enabled()\` | 全局开关引用（运行时控制是否写入字符串） |
| \`msg_level\` | 日志级别枚举（debug/info/warn/error） |
| \`om_err_*\` | 预定义错误码常量标签 |
| \`om_error_code(name)\` | 由名字生成自定义错误码（编译期常量，正 \`int32\`） |
| \`msg::errc(value)\` | 用已有数值包装错误码标签 |
| \`operating_message()\` | 默认构造，\`switch_=true\`，\`code_=0\`，\`min_level_=info\` |
| \`operator bool()\` | 是否成功（返回 \`switch_\`） |
| \`reset()\` | 重置为成功、清空消息和错误码 |
| \`clear_message()\` | 仅清空消息字符串（保留 switch/code/capacity） |
| \`set_switch_bool(bool)\` | 直接设置开关值 |
| \`get_switch_bool()\` | 获取开关引用 |
| \`get_switch_bool() const\` | 获取开关 const 引用 |
| \`set_min_level(msg_level)\` | 设置最低记录级别（默认 info） |
| \`get_min_level()\` | 获取当前最低记录级别 |
| \`set_code(uint32_t)\` | 设置错误码 |
| \`code()\` | 获取错误码（0=成功） |
| \`is_code(uint32_t)\` | 判断错误码是否匹配 |
| \`reserve(size_t)\` | 预分配消息缓冲区 |
| \`capacity()\` | 当前缓冲区容量 |
| \`message_size()\` | 当前消息长度 |
| \`write(bool sw, Args... args)\` | 写入消息，\`sw\` 调整成败（粘性） |
| \`write(Args... args)\` | 写入消息（省略开关 = 成功，不改变现有状态） |
| \`check(cond, Args... args)\` | 前置条件检查：成功零写入返回 true，失败写入返回 false |
| \`validate_format(fmt, expected_count)\` | 运行时格式串校验（占位符数量 + 语法，不校验类型匹配） |
| \`read_message()\` | 读取消息字符串（返回 \`string_view\`） |
| \`operator+=(string_view)\` | 追加字符串到消息 |
| \`operator+=(operating_message&&)\` | 合并右值消息（\`switch_ = switch_ && other.switch_\`） |
| \`operator+=(const operating_message&)\` | 合并左值消息 |
| \`operator<<(ostream, operating_message)\` | 输出到 \`ostream\` |
| \`operating_message(operating_message&&)\` | 移动构造 |
| \`operator=(operating_message&&)\` | 移动赋值 |
| \`operating_message(const operating_message&)\` | 拷贝构造 |
| \`operator=(const operating_message&)\` | 拷贝赋值 |

### 标签

| 标签 | 说明 |
|------|------|
| \`msg::debug\` / \`msg::info\` / \`msg::warn\` / \`msg::error\` | 级别常量标签 |
| \`om_err_*\` | 预定义错误码标签（失败时设置 code） |
| \`msg::errc(c)\` | 动态错误码标签 |
| \`msg::here()\` | 位置标签，写入 \`[文件名:行号]\` 前缀 |
| \`msg::fmt("{}", x)\` | 编译期格式化标签（\`std::format\` 语法，编译期校验） |
| \`msg::fmt_rt(sv, x)\` | 运行时格式串标签（\`fmt\` 为运行时 string_view） |
| \`msg::defer(fn)\` | 惰性求值标签（写入时才调用） |

标签与普通参数可任意组合、任意顺序混入 \`write\` 参数包：

\`\`\`cpp
om.write(false, msg::here(), om_err_type_mismatch,
         "add(): type mismatch, got ", msg::fmt("{}(0x{:X})", id, id));
\`\`\`

### 断言与调试辅助

| 接口 | 说明 |
|------|------|
| \`msg::expect_ok(om)\` | 测试断言：期望成功，失败则打印位置并中止 |
| \`msg::expect_code(om, expected)\` | 测试断言：期望指定错误码 |
| \`msg::expect_fail(om)\` | 测试断言：期望失败 |
| \`msg::debug_break()\` | 触发调试器中断 |

### 使用

\`\`\`cpp
operating_message msg;
msg.write(false, "Error: ", "file not found");
if (!msg) { /* 处理错误 */ }

// 粘性 false：write(true) 不会恢复
msg.write(true, "This won't recover");
// msg 仍为 false

msg.reset();  // 恢复为 true, 清空 code 和消息

// 省略开关 = 成功
msg.write("created: id=", 42);

// 纯状态调整（无消息）
msg.write(false);

// 全局开关 (运行时控制)
message_recording_enabled() = false;  // 禁用字符串写入
message_recording_enabled() = true;   // 启用字符串写入

// 格式化
msg.write(true, msg::fmt("x={}, y={}", 10, 20));

// 运行时格式串 (配置/语言包/动态模板)
std::string dyn_fmt = load_template("entry");
msg.write(true, msg::fmt_rt(dyn_fmt, id, name));

// 合并
operating_message msg2;
msg2.write(false, "Another error");
msg += msg2;  // switch_ = msg.switch_ && msg2.switch_

// 日志级别过滤
msg.set_min_level(msg_level::warn);       // 只记 warn 及以上
msg.write(true, msg::info, "这条被过滤");  // 不写入
msg.write(true, msg::error, "严重错误: ", 42);
// message_ = "[ERROR] 严重错误: 42\\n"

// 级别 + 格式化组合
msg.write(true, msg::warn, msg::fmt("v={} k={}", 1, "x"));
// message_ += "[WARN]  v=1 k=x\\n"

// 整型/浮点直接写入
msg.write(true, "i=", 100, " d=", 3.14);

// 惰性求值
msg.write(false, "state=", msg::defer([&]{ return dump_state(); }));

// 错误码
msg.write(false, om_err_invalid_entity, "实体无效: ", entity_id);
if (msg.is_code(om_err_invalid_entity))
{
    // 程序化判断错误类型
}

// 动态错误码
msg.write(false, msg::errc(custom_code), "自定义错误");

// 位置
msg.write(false, msg::here(), "操作失败: ", reason);
// message_ = "[test.cpp:42] 操作失败: <reason>\\n"

// 前置条件检查
if (!msg.check(ptr != nullptr, om_err_null_pointer, "空指针: ", ptr))
{
    return msg;
}

// 预分配缓冲区（循环场景）
msg.reserve(4096);
for (int i = 0; i < 1000; ++i)
{
    msg.reset();
    msg.write(true, "iter ", i);
}

// 测试断言
msg::expect_ok(manager.add<Position>(e, pos));
msg::expect_code(manager.add<Position>(invalid_e, pos), om_err_invalid_entity);
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 依赖 \`write(true)\` 恢复失败状态 | 粘性 false 语义，成功后不会恢复 | 调用 \`reset()\` 显式恢复 |
| 在记录开关关闭时依赖 \`read_message()\` | 字符串为空，但 switch/code 仍更新 | 先 \`message_recording_enabled()\` 确认开启，或用 \`operator bool()\` 判断成败 |
| 忘记检查 \`operator bool()\` | 操作失败被静默忽略 | 每次关键操作后检查 \`if (!msg) { ... }\` |
| 字符串匹配判断错误类型 | 脆弱且记录关闭时消息为空 | 使用 \`is_code()\` 程序化判断错误码 |
| \`write\` 传 \`sw=true\` 时期望设置错误码 | 成功路径错误码清零 | 仅 \`sw=false\` 时 code 被设置 |
| \`reset()\` 后期望保留错误码 | \`reset()\` 清零 code | 如需保留 code，用 \`clear_message()\` 仅清消息 |
| 首参数传 bool 内容 | 首参数为 bool 类型一律视为开关 | 用 \`msg::fmt("{}", flag)\` 写入 bool 内容 |
| \`msg::fmt\` 内传昂贵表达式 | 参数在构造时求值 | 用 \`msg::defer\` 包裹昂贵计算 |

### 高级功能与调试辅助

在 \`operating_message\` 基础上组合出的扩展工具集，全部 \`noexcept\`。

扩展接口（\`om_prefix\`/\`om_write_prefixed\`/\`om_scope_timer\`/\`om_scope_cycles\`/\`om_error_table\`/\`om_once\`/\`om_stats\`/\`om_history\`/\`om_logger\`/\`om_try_recover\` 等）属于 \`om_extensions\` 扩展；其余属于 \`operating_message\` 本体。

#### RAII 守卫

| 类型 | 说明 |
|------|------|
| \`message_recording_guard(enable)\` | 作用域内临时开启/关闭 \`message_recording_enabled()\`，析构自动恢复 |
| \`min_level_guard(om, lv)\` | 作用域内临时调整 \`om\` 的 \`min_level\`，析构自动恢复 |
| \`om_prefix(prefix)\` | 推入模块前缀到前缀栈，析构弹出（配合 \`om_write_prefixed\`） |
| \`om_indent\` | 推入一层缩进，析构弹出（配合 \`om_write_indented\`） |
| \`om_scope_timer(om, name)\` | RAII 作用域计时器（墙钟 us，析构写入消息） |
| \`om_scope_cycles(om, name)\` | RAII 作用域计时器（CPU 周期，析构写入消息） |

\`\`\`cpp
{
    message_recording_guard g(true);        // 局部开启消息
    min_level_guard lg(om, msg_level::warn);// 作用域内只看 warn 及以上
    om_prefix pfx("[ECS]");                 // 模块前缀
    om_indent ind;                          // 缩进一层
    om_scope_timer t(om, "batch_add");      // 计时
    // ... 操作 ...
}  // 全部 RAII 自动恢复
\`\`\`

#### 快照与差异提取

| 接口 | 说明 |
|------|------|
| \`snapshot_of(om)\` | 记录当前 \`size/switch/code\` 为快照 |
| \`appended_since(om, snap)\` | 提取快照之后追加的消息内容（零拷贝 \`string_view\`） |

\`\`\`cpp
auto snap = snapshot_of(om);
om.write(true, msg::fmt_rt("二次失败诊断: {}", detail));
std::string_view extra = appended_since(om, snap);  // 仅追加部分
\`\`\`

#### 前缀与缩进写入

| 接口 | 说明 |
|------|------|
| \`om_write_prefixed(om, sw, args...)\` | 追加前缀栈所有前缀后写入 |
| \`om_write_indented(om, sw, args...)\` | 追加 \`2*level\` 个空格后写入 |

#### 体积监控

| 接口 | 说明 |
|------|------|
| \`om_clear_if_over(om, threshold)\` | 消息体积超阈值则 \`clear_message()\` |

#### 错误码映射表

| 接口 | 说明 |
|------|------|
| \`om_error_table()\` | 内置错误码 → (\`name\`, \`description\`) 表（\`dense<om_error_entry>\`） |
| \`om_error_name(code)\` | 错误码 → 可读名（如 \`"out_of_range"\`） |
| \`om_error_desc(code)\` | 错误码 → 中文描述（如 \`"索引越界"\`） |

#### 一次性消息去重

| 接口 | 说明 |
|------|------|
| \`om_once(key)\` | 同 \`key\` 仅首次返回 \`true\`（应输出），重复返回 \`false\`（跳过） |
| \`om_dedup_clear()\` | 清空去重集合，允许同 \`key\` 再次输出 |

\`\`\`cpp
if (om_once("warn_overflow"))
{
    om.write(true, msg::warn, "buffer overflow");
}
\`\`\`

#### 消息计数统计

| 接口 | 说明 |
|------|------|
| \`global_om_stats()\` | 全局统计引用（\`std::array\` 按级别计数 + 成败计数） |
| \`om_stats_record(om, lv)\` | 记录一条操作结果到全局统计 |
| \`om_stats::reset()\` | 清零统计 |

#### 消息历史记录

| 接口 | 说明 |
|------|------|
| \`om_history\` | \`ring_buffer<om_record, 1024>\`，保留最近 1024 条消息（单条截断到 256B），满则覆盖最旧 |
| \`om_history_push(hist, om, lv)\` | 推入一条消息（截断到 256B，满则覆盖最旧） |
| \`om_history_visit(hist, handler)\` | 只读查看历史，逐条调用 \`handler(lv, code, msg_view)\`，不消费 |
| \`om_history_flush(hist, handler)\` | 排空历史（批量消费），逐条调用 \`handler(lv, code, msg_view)\` |
| \`om_history_dropped(hist)\` | 历史中被覆盖淘汰的累计条数 |

#### 消息订阅/回调（包装器模式）

| 类型/接口 | 说明 |
|----------|------|
| \`om_sink_fn\` | 回调签名 \`void(*)(msg_level, uint32_t, string_view)\`（函数指针，无 \`std::function\`） |
| \`om_logger(sink, cb)\` | 包装 \`operating_message\`，每次 \`write/write_fmt\` 后触发回调 + 推入历史 |
| \`om_logger::write / write_fmt\` | 代理 \`write\` + level 标签（\`write_fmt\` 附运行时格式串） |

\`\`\`cpp
om_history hist;
auto cb = [](msg_level lv, uint32_t code, std::string_view msg) {
    std::printf("[%u] code=%u %.*s\\n", (unsigned)lv, code, (int)msg.size(), msg.data());
};
om_logger logger(&hist, cb);
logger.write(msg_level::error, false, "操作失败");
\`\`\`

#### 耗时统计与延迟监控

| 类型 | 说明 |
|------|------|
| \`om_latency_tracker\` | 离线统计，存全部样本（\`dense<double>\`），\`compute()\` 返回 \`stats\`（min/max/mean/p50/p90/p95/p99/stddev） |
| \`om_latency_monitor\` | 在线 P² 分位数，O(1) 空间，\`p50()/p95()/p99()\` 实时查询 |
| \`om_anomaly_detector()\` | 全局延迟异常检测器（基于 P99 动态阈值） |
| \`om_measure_and_check(fn)\` | 测量 \`fn\` 耗时（纳秒），喂入检测器，返回是否异常 |

\`\`\`cpp
om_latency_tracker tracker;
tracker.reserve(1000);
for (int i = 0; i < 1000; ++i)
{
    tracker.measure([&]{ /* 待测操作 */ });
}
stats s = tracker.compute();   // 一次性计算分位数

om_latency_monitor mon;        // 长跑服务在线监控
mon.measure([&]{ /* 操作 */ });
double p99 = mon.p99();
\`\`\`

#### 错误恢复策略表

| 接口 | 说明 |
|------|------|
| \`om_recovery_fn\` | 恢复函数签名 \`bool(*)(const operating_message&)\` |
| \`om_register_recovery(code, fn)\` | 注册 \`code\` 对应的恢复函数 |
| \`om_try_recover(err)\` | 查表调用匹配 \`code\` 的恢复函数，返回是否恢复成功 |

\`\`\`cpp
bool handle_oor(const operating_message&) { expand_capacity(); return true; }
om_register_recovery(om_err_out_of_range, handle_oor);

auto msg = manager.add<T>(e);
if (!msg && om_try_recover(msg))
{
    msg = manager.add<T>(e);  // 恢复后重试
}
\`\`\`

### 高级功能使用注意

| 注意点 | 说明 |
|--------|------|
| 全局静态状态 | \`om_dedup_set\`/\`global_om_stats\`/\`om_prefix_stack\`/\`om_indent_level\`/\`om_anomaly_detector\` 为全局静态，非线程安全 |
| 历史消息截断 | \`om_history_push\` 单条消息截断到 256B，超长部分丢失 |
| \`om_logger\` 代理范围 | 包装器代理 \`write\` 函数，记录开关关闭时同样不产生字符串 |
| \`om_latency_tracker::compute\` 会排序 | 按值接收样本并排序，非 \`const\` |
| \`om_anomaly_detector\` 单位为纳秒 | 与其他计时接口一致 |
| 记录开关关闭时 \`write\` 不写字符串 | 仅跳过字符串写入；switch 累积与 code 设置不受影响 |

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
