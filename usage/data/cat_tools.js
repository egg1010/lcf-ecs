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
- **默认高性能**：Release 构建（\`NDEBUG\`）默认零开销，\`OM_MSG\` 宏不求值消息参数；Debug 构建默认写消息
- **全局开关**：\`message_recording_enabled()\` 运行时控制是否写入字符串（仅影响 \`write_message*\` 函数，不影响 \`OM_MSG\` 宏的编译期展开）
- **错误码**：\`code()\` 返回错误码（0=成功），可用 \`is_code()\` 程序化判断错误类型
- **位置追踪**：\`write_message_loc()\` 自动附加 \`[文件名:行号]\` 前缀
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

### 错误码常量

\`\`\`cpp
constexpr uint16_t om_err_none              = 0;
constexpr uint16_t om_err_type_mismatch     = /* fnv1a("type_mismatch") */;
constexpr uint16_t om_err_invalid_entity    = /* fnv1a("invalid_entity") */;
constexpr uint16_t om_err_version_mismatch  = /* fnv1a("version_mismatch") */;
constexpr uint16_t om_err_out_of_range      = /* fnv1a("out_of_range") */;
constexpr uint16_t om_err_null_pointer      = /* fnv1a("null_pointer") */;
constexpr uint16_t om_err_capacity_exceeded = /* fnv1a("capacity_exceeded") */;
constexpr uint16_t om_err_not_found         = /* fnv1a("not_found") */;
constexpr uint16_t om_err_already_exists    = /* fnv1a("already_exists") */;
\`\`\`

错误码基于 \`fnv1a_consteval\` 哈希低 16 位，跨进程稳定。可自定义错误码：\`static_cast<uint16_t>(fnv1a_consteval("my_error"))\`。

### 接口

| 接口 | 说明 |
|------|------|
| \`message_recording_enabled()\` | 全局开关引用（运行时控制是否写入字符串，仅影响 \`write_message*\` 函数） |
| \`msg_level\` | 日志级别枚举（debug/info/warn/error） |
| \`om_err_*\` | 预定义错误码常量 |
| \`operating_message()\` | 默认构造，\`switch_=true\`，\`code_=0\`，\`min_level_=info\` |
| \`operator bool()\` | 是否成功（返回 \`switch_\`） |
| \`reset()\` | 重置为成功、清空消息和错误码 |
| \`clear_message()\` | 仅清空消息字符串（保留 switch/code/capacity） |
| \`set_switch_bool(bool)\` | 直接设置开关值 |
| \`get_switch_bool()\` | 获取开关引用 |
| \`get_switch_bool() const\` | 获取开关 const 引用 |
| \`set_min_level(msg_level)\` | 设置最低记录级别（默认 info） |
| \`get_min_level()\` | 获取当前最低记录级别 |
| \`set_code(uint16_t)\` | 设置错误码 |
| \`code()\` | 获取错误码（0=成功） |
| \`is_code(uint16_t)\` | 判断错误码是否匹配 |
| \`reserve(size_t)\` | 预分配消息缓冲区 |
| \`capacity()\` | 当前缓冲区容量 |
| \`message_size()\` | 当前消息长度 |
| \`write_message(bool sw, Args... args)\` | 写入消息（\`sw=false\` 标记失败，粘性） |
| \`write_message_level(lv, sw, Args...)\` | 带级别的写入（级别不足则跳过，自动加前缀） |
| \`write_message_fmt(bool sw, fmt, Args...)\` | 格式化写入（\`fmt\` 为编译期字面量，\`std::format_string\` 编译期校验） |
| \`write_message_fmt_level(lv, sw, fmt, Args...)\` | 带级别的格式化写入（编译期校验） |
| \`write_message_code(code, sw, Args...)\` | 带错误码的写入（\`sw=false\` 时设置 code） |
| \`write_message_code_level(code, lv, sw, Args...)\` | 带错误码和级别的写入 |
| \`write_message_loc(sw, std::source_location, Args...)\` | 带源码位置前缀 \`[文件:行]\` 的写入（\`loc\` 需显式传 \`current()\`） |
| \`write_message_code_loc(code, sw, std::source_location, Args...)\` | 带错误码和源码位置的写入 |
| \`write_message_fmt_runtime(sw, std::string_view fmt, Args...)\` | 运行时格式化写入（\`fmt\` 运行时构造，\`validate_format\` 校验，完整 \`std::format\` 语法） |
| \`write_message_fmt_runtime_level(lv, sw, fmt, Args...)\` | 带级别的运行时格式化写入 |
| \`write_message_fmt_runtime_code(code, sw, fmt, Args...)\` | 带错误码的运行时格式化写入 |
| \`write_message_fmt_runtime_code_level(code, lv, sw, fmt, Args...)\` | 带错误码和级别的运行时格式化写入 |
| \`write_message_fmt_runtime_loc(sw, std::source_location, fmt, Args...)\` | 带源码位置的运行时格式化写入 |
| \`write_message_fmt_runtime_code_loc(code, sw, std::source_location, fmt, Args...)\` | 带错误码和源码位置的运行时格式化写入 |
| \`validate_format(fmt, expected_count)\` | 运行时格式串校验（占位符数量 + 语法，\`noexcept\`，不校验类型匹配） |
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

msg.reset();  // 恢复为 true, 清空 code 和消息

// 全局开关 (运行时控制, 仅影响 write_message* 函数)
// Release 构建 (NDEBUG) 默认 false (高性能); Debug 构建默认 true
message_recording_enabled() = false;  // 运行时禁用字符串写入
message_recording_enabled() = true;   // 运行时启用字符串写入

// 构建类型选择 (VSCode CMake Tools 状态栏 / 命令行)
//   cmake -B build -DCMAKE_BUILD_TYPE=Release  # 高性能: OM_MSG 宏不求值参数
//   cmake -B build -DCMAKE_BUILD_TYPE=Debug    # 调试: OM_MSG 宏写完整消息

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

// === 错误码 ===
msg.write_message_code(om_err_invalid_entity, false, "实体无效: ", entity_id);
if (msg.is_code(om_err_invalid_entity)) {
    // 程序化判断错误类型
}
if (msg.is_code(om_err_version_mismatch)) {
    // 版本不匹配的专门处理
}

// 自定义错误码
constexpr uint16_t my_err = static_cast<uint16_t>(fnv1a_consteval("my_custom_error"));
msg.set_code(my_err);

// 错误码 + 级别
msg.write_message_code_level(om_err_capacity_exceeded, msg_level::error, false, "容量超限");

// === source_location ===
msg.write_message_loc(false, std::source_location::current(), "操作失败: ", reason);
// message_ = "[test.cpp:42] 操作失败: <reason>\\n"

// 错误码 + 位置
msg.write_message_code_loc(om_err_null_pointer, false, std::source_location::current(), "空指针: ", ptr);

// === 运行时格式化 (fmt 为运行时 string_view, 支持完整 std::format 语法) ===
msg.write_message_fmt_runtime(true, "值: {}", 42);
msg.write_message_fmt_runtime(true, "{}+{}={}", 1, 2, 3);
msg.write_message_fmt_runtime_level(msg_level::warn, true, "[{}] {}", "WARN", "告警");

// 复杂格式 (slow path: validate_format + std::vformat_to)
msg.write_message_fmt_runtime(true, "hex={:08x}", 0xAB);   // "hex=000000ab"

// 运行时拼接 fmt (配置/语言包/动态宽度)
std::string dyn_fmt = load_template("entry");
msg.write_message_fmt_runtime(true, dyn_fmt, id, name);

// 运行时 + code / loc 组合
msg.write_message_fmt_runtime_code(om_err_out_of_range, false, "idx={} max={}", 9, 8);
msg.write_message_fmt_runtime_loc(true, std::source_location::current(), "n={}", 5);

// 预校验运行时格式串 (可选, 不校验类型匹配)
if (!validate_format(dyn_fmt, 2)) { /* 格式非法 */ }
\`\`\`

### 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 依赖 \`write_message(true)\` 恢复失败状态 | 粘性 false 语义，成功后不会恢复 | 调用 \`reset()\` 显式恢复 |
| 在 Release 构建中依赖 \`read_message()\` | 全局开关关闭时字符串为空 | 使用 \`operator bool()\` 判断成败，或用 Debug 构建开启消息 |
| 忘记检查 \`operator bool()\` | 操作失败被静默忽略 | 每次关键操作后检查 \`if (!msg) { ... }\` |
| 字符串匹配判断错误类型 | 脆弱且 Release 中消息为空 | 使用 \`is_code()\` 程序化判断错误码 |
| \`write_message_code\` 传 \`sw=true\` 时期望设置 code | \`sw=true\` 时不设 code | 仅 \`sw=false\` 时 code 被设置 |
| \`reset()\` 后期望保留错误码 | \`reset()\` 清零 code | 如需保留 code，用 \`clear_message()\` 仅清消息 |
| 高频日志用 \`{:08x}\` 等复杂格式 | 走通用路径 | 高频路径仅用 \`{}\` 简单占位符 |
| 默认级别记录所有 debug 日志 | Release 中 debug 日志拖累性能 | \`set_min_level(msg_level::warn)\` 过滤低级别 |
| \`write_message_loc\` 省略 \`loc\` 参数 | 编译错误（\`loc\` 非默认参数） | 显式传 \`std::source_location::current()\` |

### 高级功能与调试辅助

基于 \`operating_message\` 现有接口组合的零侵入工具集（全部 \`noexcept\`，无异常，复用 \`dense\`/\`ring_buffer\`/\`time\`/\`fnv1a\`）。

依赖 \`dense\`/\`ring_buffer\`/\`time\`/\`fnv1a\` 的接口（\`om_prefix\`/\`om_write_prefixed\`/\`om_scope_timer\`/\`om_scope_cycles\`/\`om_error_table\`/\`om_once\`/\`om_stats\`/\`om_history\`/\`om_logger\`/\`om_try_recover\` 等）位于 \`part/om_extensions.hpp\`，需额外包含；其余在 \`part/operating_message.hpp\`。

#### RAII 守卫

| 类型 | 说明 |
|------|------|
| \`message_recording_guard(enable)\` | 作用域内临时开启/关闭 \`message_recording_enabled()\`，析构自动恢复 |
| \`min_level_guard(om, lv)\` | 作用域内临时调整 \`om\` 的 \`min_level_\`，析构自动恢复 |
| \`om_prefix(prefix)\` | 推入模块前缀到前缀栈，析构弹出（配合 \`om_write_prefixed\`；\`om_extensions.hpp\`） |
| \`om_indent\` | 推入一层缩进，析构弹出（配合 \`om_write_indented\`） |
| \`om_scope_timer(om, name)\` | RAII 作用域计时器（墙钟 us，析构写入消息；\`om_extensions.hpp\`） |
| \`om_scope_cycles(om, name)\` | RAII 作用域计时器（CPU 周期，析构写入消息；\`om_extensions.hpp\`） |

\`\`\`cpp
{
    message_recording_guard g(true);        // Release 模式局部开启消息
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
om.write_message_fmt_runtime(true, "二次失败诊断: {}", detail);
std::string_view extra = appended_since(om, snap);  // 仅追加部分
\`\`\`

#### 级别便捷方法与 KV 日志

| 接口 | 说明 |
|------|------|
| \`om_debug/om_info/om_warn/om_error(om, sw, args...)\` | 薄封装 \`write_message_level\` |
| \`om_kv(om, key, val)\` | 写入 \`key=value\\n\` 形式 |
| \`om_progress(om, cur, total, task)\` | 写入 \`[cur/total] task\\n\` 进度 |

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
if (om_once("warn_overflow")) {           // 同一警告只输出一次
    om.write_message_level(msg_level::warn, true, "buffer overflow");
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
| \`om_history\` | \`ring_buffer<om_record, 1024>\`，固定 256B 消息缓冲，静态池复用 |
| \`om_history_push(hist, om, lv)\` | 推入一条消息（截断到 256B） |
| \`om_history_flush(hist, handler)\` | 排空历史，逐条调用 \`handler(lv, code, msg_view)\` |

#### 消息订阅/回调（包装器模式）

| 类型/接口 | 说明 |
|----------|------|
| \`om_sink_fn\` | 回调签名 \`void(*)(msg_level, uint16_t, string_view)\`（函数指针，无 \`std::function\`） |
| \`om_logger(sink, cb)\` | 包装 \`operating_message\`，每次 \`write/write_fmt\` 后触发回调 + 推入历史 |
| \`om_logger::write / write_fmt\` | 代理 \`write_message_level\` / \`write_message_fmt_runtime_level\` |

\`\`\`cpp
om_history hist;
auto cb = [](msg_level lv, uint16_t code, std::string_view msg) {
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
for (int i = 0; i < 1000; ++i) {
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
if (!msg && om_try_recover(msg)) {
    msg = manager.add<T>(e);  // 恢复后重试
}
\`\`\`

#### 调试与测试辅助宏

| 宏 | 说明 |
|----|------|
| \`LCF_DEBUG_MSG(om, fmt, ...)\` | \`om\` 失败时追加诊断消息并触发调试器中断 |
| \`LCF_CHECK(om, cond, err_code, ...)\` | 前置条件检查，\`cond=false\` 时写入错误码消息并 \`return om\` |
| \`LCF_EXPECT_OK(om)\` | 测试断言：期望成功，失败则 \`printf\` + \`abort\` |
| \`LCF_EXPECT_CODE(om, expected)\` | 测试断言：期望指定错误码 |
| \`LCF_EXPECT_FAIL(om)\` | 测试断言：期望失败 |

\`\`\`cpp
operating_message do_add(manager& m, entity_t e) {
    operating_message result;
    LCF_CHECK(result, e != 0, om_err_invalid_entity, "实体不能为 0");
    return m.add<T>(e);
}

// 测试中
LCF_EXPECT_OK(m.add<T>(e));
LCF_EXPECT_CODE(m.add<T>(invalid), om_err_invalid_entity);
\`\`\`

### 高级功能使用注意

| 注意点 | 说明 |
|--------|------|
| 全局静态状态 | \`om_dedup_set\`/\`global_om_stats\`/\`om_prefix_stack\`/\`om_indent_level\`/\`om_anomaly_detector\` 为全局静态，非线程安全（项目排除多线程） |
| 历史消息截断 | \`om_history_push\` 单条消息截断到 256B，超长部分丢失 |
| \`om_logger\` 不影响 \`OM_MSG\` | 包装器仅代理 \`write_message*\` 函数，\`OM_MSG\` 宏仍走编译期展开 |
| \`om_latency_tracker::compute\` 会排序 | 内部调用 \`compute_stats\` 按值传入并排序样本，非 \`const\` |
| \`om_anomaly_detector\` 单位为纳秒 | 与 \`time.hpp\` 一致，\`om_measure_and_check\` 内部用 \`stopwatch::ns()\` |
| Release 模式 \`OM_MSG\` 不写消息 | 守卫/\`om_logger\` 只影响 \`write_message*\` 函数，无法让 \`OM_MSG\` 在 Release 写消息 |

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
另支持运行期按名字注册自定义类型，注册 ID 与模板类型 ID 互不冲突。

### 接口

| 接口 | 说明 |
|------|------|
| \`type_id::get_type_id<T>()\` | 获取类型 T 的唯一 ID（静态函数，线程安全） |
| \`type_id::current_max_id()\` | 返回当前已分配的最大 type_id（静态函数） |
| \`type_id::mask_block_of(tid)\` | type_id 所在实体掩码块索引 \`(tid-1)/64\`（constexpr） |
| \`type_id::mask_offset_of(tid)\` | type_id 在掩码块内的位偏移 \`(tid-1)%64\`（constexpr） |
| \`type_id::mask_type_of(block, offset)\` | 掩码块/偏移反查 type_id（上两者的逆映射，constexpr） |
| \`type_id::register_type_def(name, def)\` | 按名字注册自定义类型，返回类型 ID；同名重复注册返回已有 ID |
| \`type_id::get_def_type_id(name)\` | 按名字查询类型 ID（含注册类型与绑定名）；未注册返回 -1 |
| \`type_id::bind_def_name(name, id)\` | 将名字绑定到既有类型 ID（模板类型的稳定名/别名）；同名同 ID 幂等，同名异 ID 返回 false |
| \`type_id::get_type_def(id)\` | 按 ID 查询自定义类型的存储语义；非注册 ID 或纯绑定名返回 nullptr |
| \`type_id::get_def_type_name(id)\` | 按 ID 反查名字；非注册 ID 返回空串 |

### type_def 字段

| 字段 | 说明 |
|------|------|
| \`size\` | 元素字节大小（必须 > 0） |
| \`alignment\` | 对齐要求（必须为 2 的幂） |
| \`trivially_copyable\` | true 时按 memcpy 方式搬运数据 |
| \`construct\` / \`destruct\` | 非平凡类型的构造/析构函数指针（\`void(*)(void*) noexcept\`） |

### 使用

\`\`\`cpp
// 模板类型 ID
int id1 = type_id::get_type_id<int>();
int id2 = type_id::get_type_id<double>();
assert(type_id::get_type_id<int>() == id1);  // 同类型 ID 相同

int max_id = type_id::current_max_id();  // 已分配的最大 ID
\`\`\`

\`\`\`cpp
// 注册 trivially copyable 自定义类型
type_def def;
def.size = 12;
def.alignment = 4;
def.trivially_copyable = true;

int id = type_id::register_type_def("Velocity", def);
assert(type_id::get_def_type_id("Velocity") == id);        // 按名查询
assert(type_id::register_type_def("Velocity", def) == id); // 重复注册幂等

// 非平凡类型需提供构造/析构
type_def nontrivial;
nontrivial.size = 16;
nontrivial.alignment = 8;
nontrivial.trivially_copyable = false;
nontrivial.construct = my_construct;  // void(*)(void*) noexcept
nontrivial.destruct = my_destruct;    // void(*)(void*) noexcept
int id2x = type_id::register_type_def("Matrix", nontrivial);

// 按 ID 反查
const type_def* q = type_id::get_type_def(id);
std::string_view name = type_id::get_def_type_name(id);  // "Velocity"
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
  title: "timer — 计时与定时触发",
  category: 'tools',
  icon: 'T',
  order: 28,
  content: `## 28. timer — 计时与定时触发

\`#include "part/time.hpp"\`（计时器本体）与 \`#include "part/timer_scheduler.hpp"\`（调度器与时间字面量），全局命名空间。\`noexcept\`。

时间测量与定时触发：墙钟时间（纳秒/微秒/毫秒/秒）与 CPU 周期测量，配对计时，定时调度器。x86/x64 提供 \`rdtsc\`/\`rdtscp\`，其他平台返回 0。

### 28.1 rdtsc / rdtscp — CPU 周期读取原语

| 接口 | 说明 |
|------|------|
| \`rdtsc()\` | 读取 TSC 周期计数（x86/x64；其他平台返回 0） |
| \`rdtscp()\` | 序列化读取 TSC 周期计数（x86/x64；其他平台返回 0） |

\`\`\`cpp
uint64_t tsc = rdtsc();
uint64_t tsc_serialized = rdtscp();
\`\`\`

### 28.2 timer — 统一计时器

| 接口 | 说明 |
|------|------|
| \`timer()\` | 构造并记录起点 |
| \`reset()\` | 重置起点 |
| \`elapsed_nanoseconds()\` | 墙钟已逝纳秒数 |
| \`elapsed_microseconds()\` | 墙钟已逝微秒数 |
| \`elapsed_milliseconds()\` | 墙钟已逝毫秒数 |
| \`elapsed_seconds()\` | 墙钟已逝秒数 |
| \`elapsed_cycles()\` | CPU 周期数（非 x86 返回 0） |
| \`take_snapshot()\` | 一次性返回 \`snapshot{nanoseconds, cycles}\` |

\`\`\`cpp
timer t;
// ... 执行操作 ...
double ns = t.elapsed_nanoseconds();
uint64_t cycles = t.elapsed_cycles();

// 一次性快照
timer::snapshot snap = t.take_snapshot();
// snap.nanoseconds, snap.cycles
\`\`\`

### 28.3 timer 静态原语 — 当前时间与配对计时

| 接口 | 说明 |
|------|------|
| \`timer::now_cycles()\` | 当前 CPU 周期计数（绝对值，非 x86 返回 0） |
| \`timer::now_nanoseconds()\` | 当前墙钟纳秒（绝对值，自 epoch 起） |
| \`timer::measure_nanoseconds(fn)\` | 配对计时，返回 fn 执行的墙钟纳秒 |
| \`timer::measure_cycles(fn)\` | 配对计时，返回 fn 执行的 CPU 周期数（非 x86 返回 0） |

\`\`\`cpp
uint64_t c = timer::now_cycles();
double ns = timer::now_nanoseconds();

double elapsed_ns = timer::measure_nanoseconds([]() { /* 被测代码 */ });
uint64_t elapsed_cyc = timer::measure_cycles([]() { /* 被测代码 */ });
\`\`\`

### 28.4 时间字面量 — 配合 scheduler 使用（timer_scheduler.hpp）

| 字面量 | 说明 |
|------|------|
| \`100_ns\` | 100 纳秒 |
| \`100_us\` | 100 微秒（= 100,000 纳秒） |
| \`100_ms\` | 100 毫秒（= 100,000,000 纳秒） |
| \`1_sec\` | 1 秒（= 1,000,000,000 纳秒） |

\`\`\`cpp
double a = 100_ms;   // 100000000.0
double b = 16_ms;    // 16000000.0
double c = 1_sec;    // 1000000000.0
\`\`\`

### 28.5 timer::scheduler — 定时触发器（timer_scheduler.hpp）

| 接口 | 说明 |
|------|------|
| \`timer::scheduler::invalid_id\` | 无效定时器 ID 常量 |
| \`schedule_after(delay_ns, task)\` | 一次性：delay_ns 纳秒后触发，返回 \`timer_id\`（可用字面量） |
| \`schedule_every(period_ns, task)\` | 周期性：每 period_ns 纳秒触发，返回 \`timer_id\`（可用字面量） |
| \`cancel(id)\` | 取消定时器（惰性标记，ID 不失效） |
| \`tick()\` | 推进时间，触发所有到期回调 |
| \`compact()\` | 清理已取消的槽位（调用后 ID 可能失效） |
| \`pending_count()\` | 挂起定时器数量 |
| \`clear()\` | 清空所有定时器 |

\`task\` 参数类型为 \`t_fun<void()>\`，可传入函数指针、lambda 或成员函数指针（CTAD 自动推导）。

\`\`\`cpp
timer::scheduler sched;

// 一次性：100ms 后触发
auto id1 = sched.schedule_after(100_ms, []() {
    // 延迟回调
});

// 周期性：每 16ms 触发（约 60 FPS）
auto id2 = sched.schedule_every(16_ms, []() {
    // 帧调度
});

// 主循环推进
while (running)
{
    sched.tick();
}

// 取消单个
sched.cancel(id1);

// 清理已取消槽位
sched.compact();

// 清空全部
sched.clear();
\`\`\`

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

\`#include "part/analysis.hpp"\`，全局命名空间。\`noexcept\`。

微架构分析器：内存屏障、缓存控制、序列化周期测量、地址生成、缓存命中测量、自适应层级检测。x86/x64 全套支持，其他平台屏障为编译器屏障，缓存测量回退到周期计数。

### 29.1 配置与报告类型

| 类型 | 说明 |
|------|------|
| \`analyzer::config\` | 缓存层级阈值配置（\`l1_max\`/\`l2_max\`/\`l3_max\`/\`l4_max\`/\`cache_levels\`，默认 3 级） |
| \`analyzer::cache_report\` | 命中测量报告（各级命中率 + 百分位周期） |
| \`analyzer::batch_result\` | 批量测量结果（含基线扣除） |
| \`analyzer::address_view\` | 地址视图 POD（零分配，持有指针 + 数量） |

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
| \`analyzer::measure_loop_cycles(fn)\` | 单次 rdtscp 包裹（循环总开销，低开销） |

\`\`\`cpp
uint64_t cyc = analyzer::measure_cycles_fenced([]() { /* 短代码 */ });

uint64_t total = analyzer::measure_loop_cycles([]() {
    for (int i = 0; i < 1000; ++i) { work(i); }
});
\`\`\`

### 29.6 地址生成（静态）

| 接口 | 说明 |
|------|------|
| \`analyzer::make_sequential_addresses(base, count, stride)\` | 顺序地址序列（缓存友好） |
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
| \`a.measure_hits(base, count, stride)\` | 一站式：内部生成地址 + 测量 + 分类 |
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
| \`field_meta\` | \`name\`, \`offset\`, \`type_id\`, \`is_const\`, \`is_private\`, \`array_rank\`, \`reserved\`, \`total_elements\`, \`extents[4]\`, \`element_stride\`, \`attrs\`（\`dense<attr_entry>\`，#4 属性列表） |
| \`method_meta\` | \`name\`, \`arg_count\`, \`return_type_id\`, \`invoker\`, \`is_const\`, \`is_static\`, \`arg_type_ids\`（\`const int*\`，#12 参数类型 id 数组，nullptr=未设置）, \`vtable_offset\`（#6 虚函数偏移，-1=非虚） |
| \`type_meta\` | \`name\`, \`name_hash\`（#10 FNV-1a(name)）, \`registered\`, \`field_count\`, \`method_count\`, \`size\`, \`align\`, \`type_id\`, \`fields\`, \`methods\`, \`default_construct_\`/\`destruct_\`/\`has_default_construct\`（#1）, \`base_offsets\`（#2）, \`derived_type_ids\`（#2）, \`converters\`（#7）, \`container_ops\`（#5） |
| \`attr_entry\` | \`key_hash\`（FNV-1a(key)）, \`value\`（\`void_any\` 类型擦除值） |
| \`base_offset_entry\` | \`base_type_id\`, \`offset\`（派生类→基类指针调整量） |
| \`convert_entry\` | \`target_type_id\`, \`convert_fn\`（\`void(*)(const void* src, void* dst)\`） |
| \`enum_value_entry\` | \`value\`（\`uint64_t\`，underlying type 存储）, \`name\`, \`name_hash\`（FNV-1a(name)） |
| \`enum_meta\` | \`name\`, \`underlying_type_id\`, \`type_id\`, \`values\`（\`dense<enum_value_entry>\`）, \`registered\` |
| \`MAX_TYPE_ID\` | 类型槽位上限（65536） |
| \`MAX_FIELDS_PER_TYPE\` | 单类型字段上限（256） |
| \`MAX_METHODS_PER_TYPE\` | 单类型方法上限（256） |

\`field_meta\` 数组字段：\`array_rank=1~4\`、\`extents[0..rank-1]\` 为各维元素数、\`total_elements\` 为总元素数、\`element_stride\` 为元素步长（字节）。标量字段 \`array_rank=0\`。

\`type_meta\` 扩展字段说明：
- \`name_hash\`（#10）：类型名的 FNV-1a 哈希，作为跨编译器/DLL 的稳定标识，配合 \`find_type_by_hash\` 使用。
- \`default_construct_\`/\`destruct_\`（#1）：默认构造与析构函数指针，\`has_default_construct\` 标记是否可默认构造。注册类型时若 \`is_default_constructible_v<T>\` 自动填充。
- \`base_offsets\`/\`derived_type_ids\`（#2）：直接基类与派生类列表，通过 \`register_base\` 填充。
- \`converters\`（#7）：已注册的目标类型转换表，通过 \`register_convert\` 填充。
- \`container_ops\`（#5）：指向全局 \`container_ops_registry\` 的容器操作表指针，顺序容器类型注册时自动填充。

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
| \`storage::find_type_by_hash(hash)\` | 按 \`name_hash\` 查询 \`type_meta*\`（#10，跨编译器/DLL 稳定标识） |
| \`storage::register_enum<E>(name, values)\` | 注册枚举类型（#3），\`values\` 为 \`std::initializer_list<std::pair<E, const char*>>\` |
| \`storage::find_enum(tid)\` | 按类型 id 查询 \`enum_meta*\` |
| \`storage::find_enum<E>()\` | 按枚举类型查询 \`enum_meta*\` |
| \`storage::register_base<Derived, Base>(offset)\` | 注册继承关系（#2），\`offset\` 为派生类→基类指针调整量（单继承为 0） |
| \`storage::register_field_attr<T, M, Ptr, V>(field_name, attr_key, value)\` | 注册字段属性（#4），\`value\` 任意类型（\`void_any\` 存储） |
| \`storage::register_convert<T, U>()\` | 注册类型转换（#7），要求 \`is_convertible_v<T, U>\` |
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

window.DOCS_DATA['string_to_code'] = {
  id: 'string_to_code',
  title: "string_to_code — 字符串数字码",
  category: 'tools',
  icon: 'C',
  order: 36,
  content: `## 36. string_to_code — 字符串数字码

\`#include "part/string_to_code.hpp"\`，命名空间 \`string_to_code\`。\`noexcept\`。

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
#include "part/string_to_code.hpp"
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

