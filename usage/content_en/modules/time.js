window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['time'] = {
  title: "time — Timers and scheduling",
  content: `## 28. timer — Timing and timed triggering

The timer itself and the scheduler (including time literals), global namespace. \`noexcept\`.

Time measurement and timed triggering: wall-clock time (nanoseconds/microseconds/milliseconds/seconds) and CPU cycle measurement, paired timing, and a timed scheduler. x86/x64 provide \`rdtsc\`/\`rdtscp\`; other platforms return 0.

### 28.1 rdtsc / rdtscp — CPU cycle read primitives

| Interface | Description |
|------|------|
| \`rdtsc()\` | Read the TSC cycle count (x86/x64; other platforms return 0) |
| \`rdtscp()\` | Serialized read of the TSC cycle count (x86/x64; other platforms return 0) |

\`\`\`cpp
uint64_t tsc = rdtsc();
uint64_t tsc_serialized = rdtscp();
\`\`\`

### 28.2 timer — Unified timer

| Interface | Description |
|------|------|
| \`timer()\` | Construct and record the starting point |
| \`reset()\` | Reset the starting point |
| \`elapsed_nanoseconds()\` | Elapsed wall-clock nanoseconds |
| \`elapsed_microseconds()\` | Elapsed wall-clock microseconds |
| \`elapsed_milliseconds()\` | Elapsed wall-clock milliseconds |
| \`elapsed_seconds()\` | Elapsed wall-clock seconds |
| \`elapsed_cycles()\` | CPU cycle count (0 on non-x86) |
| \`take_snapshot()\` | Return \`snapshot{nanoseconds, cycles}\` in one call |

\`\`\`cpp
timer t;
// ... perform work ...
double ns = t.elapsed_nanoseconds();
uint64_t cycles = t.elapsed_cycles();

// One-shot snapshot
timer::snapshot snap = t.take_snapshot();
// snap.nanoseconds, snap.cycles
\`\`\`

### 28.3 timer static primitives — current time and paired timing

| Interface | Description |
|------|------|
| \`timer::now_cycles()\` | Current CPU cycle count (absolute value, 0 on non-x86) |
| \`timer::now_nanoseconds()\` | Current wall-clock nanoseconds (absolute value, since epoch) |
| \`timer::measure_nanoseconds(fn)\` | Paired timing, returns the wall-clock nanoseconds of fn execution |
| \`timer::measure_cycles(fn)\` | Paired timing, returns the CPU cycle count of fn execution (0 on non-x86) |

\`\`\`cpp
uint64_t c = timer::now_cycles();
double ns = timer::now_nanoseconds();

double elapsed_ns = timer::measure_nanoseconds([]() { /* code under test */ });
uint64_t elapsed_cyc = timer::measure_cycles([]() { /* code under test */ });
\`\`\`

### 28.4 Time literals — for use with scheduler

| Literal | Description |
|------|------|
| \`100_ns\` | 100 nanoseconds |
| \`100_us\` | 100 microseconds (= 100,000 nanoseconds) |
| \`100_ms\` | 100 milliseconds (= 100,000,000 nanoseconds) |
| \`1_sec\` | 1 second (= 1,000,000,000 nanoseconds) |

\`\`\`cpp
double a = 100_ms;   // 100000000.0
double b = 16_ms;    // 16000000.0
double c = 1_sec;    // 1000000000.0
\`\`\`

### 28.5 timer::scheduler — Timed triggers

| Interface | Description |
|------|------|
| \`timer::scheduler::invalid_id\` | Invalid timer ID constant |
| \`schedule_after(delay_ns, task)\` | One-shot: fires after delay_ns nanoseconds, returns \`timer_id\` (literals accepted) |
| \`schedule_every(period_ns, task)\` | Periodic: fires every period_ns nanoseconds, returns \`timer_id\` (literals accepted) |
| \`cancel(id)\` | Cancel a timer (lazy marking, the ID stays valid) |
| \`tick()\` | Advance time, firing all due callbacks |
| \`compact()\` | Clean up cancelled slots (IDs may become invalid afterwards) |
| \`pending_count()\` | Number of pending timers |
| \`clear()\` | Clear all timers |

The \`task\` parameter is of type \`t_fun<void()>\`; a function pointer, lambda, or member function pointer may be passed (deduced automatically by CTAD).

\`\`\`cpp
timer::scheduler sched;

// One-shot: fires after 100ms
auto id1 = sched.schedule_after(100_ms, []() {
    // deferred callback
});

// Periodic: fires every 16ms (about 60 FPS)
auto id2 = sched.schedule_every(16_ms, []() {
    // frame scheduling
});

// Advance in the main loop
while (running)
{
    sched.tick();
}

// Cancel a single timer
sched.cancel(id1);

// Clean up cancelled slots
sched.compact();

// Clear everything
sched.clear();
\`\`\`

---
`
};

window.DOCS_DATA_EN['timing_wheel'] = {
  title: "timing_wheel — Timing wheel",
  content: `## 60. timing_wheel — Hierarchical timing wheel

Intrusive timers: a \`timer_node\` is embedded in the business object, and registering or cancelling does not allocate memory. Single-threaded use.

### 60.1 Constants

| Constant | Value | Description |
|------|-----|------|
| \`timing_wheel_levels\` | \`4\` | Number of levels |
| \`timing_wheel_slots\` | \`64\` | Number of slots per level |
| \`timing_wheel_tick_ns\` | \`1000000\` | Minimum tick (1 ms) |

### 60.2 timer_node

| Type | Description |
|------|------|
| \`timer_node\` | Timing node embedded in a business object, maintained by the timing wheel |

The node fields are managed by the timing wheel; the caller only needs to embed the node as a member and recover the host object from \`node\` inside the callback.

### 60.3 Interface

| Interface | Description |
|------|------|
| \`create(capacity, callback, owner, now_ns)\` | Create; \`capacity\` is the maximum number of nodes, \`now_ns\` is the start time |
| \`destroy()\` | Destroy |
| \`add(node, delay_ns)\` | Register a node, fires after \`delay_ns\` nanoseconds; returns whether it succeeded |
| \`cancel(node)\` | Cancel a node, returns whether it succeeded |
| \`advance(now_ns)\` | Advance to \`now_ns\`, returns the number of firings in this call |
| \`size()\` / \`capacity()\` | Current / maximum node count |

Callback type: \`void (*)(void* owner, timer_node* node) noexcept\`.

### 60.4 Usage example

\`\`\`cpp
struct connection
{
    timer_node timeout{};
};

void on_timeout(void* owner, timer_node* node) noexcept
{
    // node belongs to some connection; the host is recovered on the business side
}

timing_wheel wheel;
wheel.create(1024, &on_timeout, nullptr, monotonic_now_ns());

connection conn;
wheel.add(conn.timeout, 100_ms);
wheel.advance(monotonic_now_ns());
\`\`\`

### 60.5 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Multiple threads operating on the same timing wheel | The structure is not thread-safe | Give each thread its own timing wheel, or call it serially from the owning thread |
| Not calling \`cancel\` before destroying the node | The callback accesses a freed object | Cancel the node before the host is destructed |
`
};
