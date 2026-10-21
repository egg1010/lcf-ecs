window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['time'] = {
  id: 'time',
  title: "timer — 计时与定时触发",
  category: 'time',
  icon: 'T',
  order: 28,
  content: `## 28. timer — 计时与定时触发

计时器本体与调度器（含时间字面量），全局命名空间。\`noexcept\`。

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

### 28.4 时间字面量 — 配合 scheduler 使用

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

### 28.5 timer::scheduler — 定时触发器

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

window.DOCS_DATA['timing_wheel'] = {
  id: 'timing_wheel',
  title: "timing_wheel — 分层时间轮",
  category: 'time',
  icon: 'W',
  order: 60,
  content: `## 60. timing_wheel — 分层时间轮

侵入式定时器：\`timer_node\` 内嵌在业务对象中，注册与取消不分配内存。单线程使用。

### 60.1 常量

| 常量 | 值 | 说明 |
|------|-----|------|
| \`timing_wheel_levels\` | \`4\` | 层级数 |
| \`timing_wheel_slots\` | \`64\` | 每级槽数 |
| \`timing_wheel_tick_ns\` | \`1000000\` | 最小刻度（1 ms） |

### 60.2 timer_node

| 类型 | 说明 |
|------|------|
| \`timer_node\` | 内嵌到业务对象中的定时节点，由时间轮维护 |

节点字段由时间轮管理，调用方只需将节点作为成员嵌入，并在回调中通过 \`node\` 找回宿主对象。

### 60.3 接口

| 接口 | 说明 |
|------|------|
| \`create(capacity, callback, owner, now_ns)\` | 创建；\`capacity\` 为最大节点数，\`now_ns\` 为起始时间 |
| \`destroy()\` | 销毁 |
| \`add(node, delay_ns)\` | 注册节点，\`delay_ns\` 纳秒后触发；返回是否成功 |
| \`cancel(node)\` | 取消节点，返回是否成功 |
| \`advance(now_ns)\` | 推进到 \`now_ns\`，返回本次触发数量 |
| \`size()\` / \`capacity()\` | 当前 / 最大节点数 |

回调类型：\`void (*)(void* owner, timer_node* node) noexcept\`。

### 60.4 使用示例

\`\`\`cpp
struct connection
{
    timer_node timeout{};
};

void on_timeout(void* owner, timer_node* node) noexcept
{
    // node 属于某个 connection，由业务侧找回宿主
}

timing_wheel wheel;
wheel.create(1024, &on_timeout, nullptr, monotonic_now_ns());

connection conn;
wheel.add(conn.timeout, 100_ms);
wheel.advance(monotonic_now_ns());
\`\`\`

### 60.5 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 多个线程同时操作同一时间轮 | 结构非线程安全 | 每个线程独立时间轮，或由所属线程串行调用 |
| 节点销毁前不 \`cancel\` | 回调访问已释放对象 | 宿主析构前取消节点 |
`
};
