window.DOCS_DATA_EN = window.DOCS_DATA_EN || {};

window.DOCS_DATA_EN['thread_manager'] = {
  title: "thread::manager — Worker thread pool",
  content: `## 47. thread::manager — Worker thread pool

Worker thread pool manager: configures, starts, submits tasks, schedules by lane, stops gracefully, and aggregates runtime statistics. In threadless (embedded) mode it is driven by \`pump\`.

Entry point \`thread/manager.hpp\`; for frame-loop scenarios use \`thread::app\` (§57), a ready-to-use application skeleton that adds a layer of default settings on top of the manager.

> **Concurrency boundary:** when sharing one manager, operate concurrently only through the provided submission and task interfaces; synchronization of objects captured by task bodies is the caller's responsibility. The thread and task-graph paths are continuously checked by the CI TSan job.

### Quick start

\`\`\`cpp
thread::manager mgr;
const operating_message config_error = mgr.configure(thread::profile::server(4));
if (!config_error)
{
    std::fprintf(stderr, "configure failed: %u\n", config_error.code());
    return 1;
}
mgr.start();

std::uint32_t slot = thread::slot_index_none;
thread::job* j = mgr.take_job(slot);
thread::fill_inline_job(j, []() noexcept { /* task body */ });
mgr.submit(j);

(void)mgr.drain();                          // wait for completion
mgr.stop();
\`\`\`

Embedded (threadless) configuration uses \`thread::profile::embedded()\` and is driven by \`pump\`.

### 47.1 Configuring manager_config

| Field | Default | Description |
|------|--------|------|
| \`worker_count\` | \`0\` | Total thread budget (including external roles attached by the native main thread); \`0\` means embedded (threadless, driven only by \`pump\`) |
| \`inject_capacity\` | \`4096\` | Global injection queue capacity |
| \`local_capacity\` | \`1024\` | Per-thread local deque capacity |
| \`steal_batch\` | \`64\` | Max batch size per steal |
| \`pin_workers\` | \`false\` | Whether to pin worker threads to cores |
| \`priority_levels\` | \`3\` | Number of priority levels |
| \`aging_threshold\` | \`64\` | Aging threshold: after this many consecutive pops, sink to take the lowest priority |
| \`lane_capacity\` | \`64\` | Max number of registered lanes |
| \`mailbox_capacity\` | \`4096\` | Per-thread mailbox queue capacity |
| \`job_slot_capacity\` | \`8192\` | Job slot pool capacity |
| \`frame_pool_blocks\` | \`1024\` | Number of coroutine frame blocks per level |
| \`task_capacity\` | \`4096\` | Persistent task graph capacity; \`0\` disables the task graph |
| \`roles\` / \`role_count\` | \`nullptr\` / \`0\` | Role partition; when not given, all threads go to \`general\` |
| \`task_local_capacity\` | \`1024\` | Per-thread local task queue capacity |
| \`task_command_capacity\` | \`1024\` | Sequential domain command queue capacity |
| \`task_inject_capacity\` | \`4096\` | Task injection queue capacity |
| \`task_dependency_capacity\` | \`8192\` | Max number of task dependency edges |

Preset configurations (\`thread::profile\`):

| Interface | Description |
|------|------|
| \`profile::thread_budget()\` | Default thread budget: hardware thread count minus one (the main thread takes part in the frame loop) |
| \`profile::server(worker_count = thread_budget())\` | Server preset, no core pinning |
| \`profile::game(worker_count = thread_budget())\` | Game preset, core pinning, smaller steal batch |
| \`profile::embedded()\` | Embedded preset, threadless (\`worker_count = 0\`) |
| \`profile::is_embedded(config)\` | Determines whether the configuration is embedded |

### 47.2 Lifecycle

| Interface | Description |
|------|------|
| \`configure(manager_config, runtime_context* = nullptr)\` | Creates threads and queues and binds the runtime, returns \`operating_message\`; repeated configuration returns \`already_exists\` |
| \`start()\` | Starts worker threads |
| \`stop()\` / \`stop(stop_mode, destroy_policy)\` | Stops and reclaims |
| \`running()\` | Whether it has been started |
| \`phase()\` | Shutdown phase: \`running\` / \`draining\` / \`aborting\` / \`stopped\` |
| \`stopping()\` | Whether the shutdown flow has been entered |

| Enum | Values |
|------|------|
| \`stop_mode\` | \`drain\` (stop after draining) / \`abort\` (stop immediately) |
| \`destroy_policy\` | \`soft\` (soft destroy, skips destructing inline captures) / \`hard\` (hard destroy, destructs and destroys pending frames) |

### 47.3 Submission and driving

| Interface | Description |
|------|------|
| \`submit(job*, preferred_worker = -1)\` | Blocking submission, returns the slot index; returns \`slot_index_none\` on failure |
| \`try_submit(job*, preferred_worker = -1)\` | Non-blocking submission |
| \`submit_to_lane(lane*, job*)\` | Submit to the specified lane |
| \`pump(budget = 64)\` | Drives one round of execution in threadless mode, returns the number executed |
| \`drain(spin_limit = 0, include_new_submissions = false)\` | Waits for tasks to complete, returns \`drain_status\`; in threadless mode it actively drives execution |
| \`take_job(slot_index&)\` / \`slot_of(slot_index)\` | Acquire / query a job slot |
| \`release_job_local(job*)\` | Release a job back to this manager's slot pool |

| Enum | Values |
|------|------|
| \`drain_status\` | \`complete\` (completed) / \`timeout\` (spin limit reached) |

### 47.4 Lanes and statistics

| Interface | Description |
|------|------|
| \`create_lane(lane_config)\` | Creates a lane, returns \`lane*\` |
| \`lane_at(index)\` / \`lane_count()\` | Query lanes |
| \`worker_count()\` / \`worker_at(index)\` | Query workers (including external roles) |
| \`spawned_worker_count()\` | Actual number of worker threads created (budget minus external roles) |
| \`inject_queue()\` | Global injection queue |
| \`stats()\` | \`manager_stats\`: \`submitted_\` / \`rejected_\` / \`wakes_\` / \`drain_timeouts_\` / \`submit_waits_\` |
| \`completed_count()\` | Number of completed tasks |

### 47.5 Example

\`\`\`cpp

thread::manager mgr;
const operating_message config_error = mgr.configure(thread::profile::server(4));
if (!config_error)
{
    std::fprintf(stderr, "configure failed: %u\n", config_error.code());
    return 1;
}
mgr.start();

std::uint32_t slot = thread::slot_index_none;
thread::job* j = mgr.take_job(slot);
thread::fill_inline_job(j, []() noexcept { /* task body */ });
mgr.submit(j);

const thread::drain_status status = mgr.drain();
if (status == thread::drain_status::timeout)
{
    // not completed within the spin limit
}
mgr.stop();
\`\`\`

### 47.6 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Calling \`start\` without \`configure\` | No worker threads or queues | Call \`configure\` before \`start\` |
| Using \`stop(stop_mode::abort)\` to wait for tasks to finish | Unexecuted tasks are discarded | To have tasks executed, use \`stop(stop_mode::drain)\` or \`drain\` first |
| Synchronous waiting inside a worker | Blocks the worker thread | Suspend with the coroutine \`co_await\` |
`
};

window.DOCS_DATA_EN['thread_lane'] = {
  title: "thread::lane — Task lanes",
  content: `## 48. thread::lane — Task lanes

Lanes group tasks and execute them in a fixed order: exclusive (extracted FIFO by the thread bound at creation), pinned (extracted by a specified thread), shared (preemptively executed by any thread).

### 48.1 Configuring lane_config

| Factory | Mode | Description |
|------|------|------|
| \`lane_config::exclusive(capacity)\` | \`exclusive\` | Extracted FIFO by the thread bound at creation |
| \`lane_config::pinned(worker, capacity)\` | \`pinned\` | Extracted by the specified worker |
| \`lane_config::shared(capacity)\` | \`shared\` | Preemptively executed by any worker |

| Field | Default | Description |
|------|--------|------|
| \`mode\` | \`exclusive\` | Lane mode |
| \`pinned_worker\` | \`0\` | Worker index in pinned mode |
| \`capacity\` | \`4096\` | Queue capacity |
| \`drain_budget\` | \`64\` | Budget per extraction |

### 48.2 Interfaces

| Interface | Description |
|------|------|
| \`create(id, lane_config, runtime_context*)\` | Creates a lane |
| \`id()\` / \`mode()\` / \`bound_worker()\` / \`drain_budget()\` | Query properties |
| \`submit(slot_index)\` | Submits a job slot, returns the slot index |
| \`try_pop(index&)\` | Pops one job slot |
| \`try_acquire(worker_id)\` / \`owns(worker_id)\` / \`release_ownership(worker_id)\` | Batch execution ownership |
| \`empty()\` / \`size()\` | Queue state |
| \`last_executed_seq()\` | Most recent execution sequence number |
| \`stats()\` | \`lane_stats\`: \`submitted_\` / \`executed_\` / \`owned_batches_\` / \`reclaims_\` / \`seq_breaks_\` |

### 48.3 Example

\`\`\`cpp
thread::lane* l = mgr.create_lane(thread::lane_config::exclusive(1024));

std::uint32_t slot = thread::slot_index_none;
thread::job* j = mgr.take_job(slot);
thread::fill_inline_job(j, []() noexcept { /* ordered task */ });
mgr.submit_to_lane(l, j);
\`\`\`

### 48.4 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Creating more lanes than \`lane_capacity\` | Creation returns \`nullptr\` | Raise \`lane_capacity\` |
| Expecting an exclusive lane to be executed by a thread that is not bound to it | Breaks the FIFO premise | Exclusive/pinned lanes are extracted only by the thread bound to them |
`
};

window.DOCS_DATA_EN['thread_job'] = {
  title: "thread::job — Jobs and priorities",
  content: `## 49. thread::job — Jobs and priorities

A job is a fixed 64-byte inline slot: a callable whose size does not exceed the slot and that is trivially copyable is stored inline; otherwise it is captured on the heap. Jobs are allocated from the slot pool and returned after execution.

### 49.1 Priorities

| Value | Description |
|----|------|
| \`priority::background\` | Background |
| \`priority::normal\` | Normal (default) |
| \`priority::critical\` | Critical |

### 49.2 Slot pool and lifecycle

| Interface | Description |
|------|------|
| \`job_pool::create(capacity)\` / \`destroy()\` | Creates / destroys the slot pool |
| \`job_pool::allocate(slot_index&)\` | Allocates a job slot |
| \`job_pool::release(slot_index)\` | Returns a job slot |
| \`job_pool::slot_of(index)\` / \`index_of(job*)\` | Cross-lookup between slots and jobs |
| \`job_pool::available()\` / \`capacity()\` | Remaining / total capacity |
| \`take_slot_job(slot_index&)\` | Takes a job from the current slot pool |
| \`run_job(job*)\` / \`destroy_job(job*)\` / \`release_job(job*)\` | Execute / destruct / release |
| \`reset_job_metadata(job*)\` | Resets metadata |
| \`fill_job(job*, callable)\` / \`fill_inline_job\` / \`fill_overflow_job\` | Fills the task body |

### 49.3 Priority stack

| Interface | Description |
|------|------|
| \`priority_stack::create(levels, capacity_per_level)\` | Creates a layered stack |
| \`push(job*)\` / \`pop()\` / \`steal(out, max_count)\` | Push / pop / steal |
| \`total_size()\` / \`levels()\` / \`drops()\` | Statistics |

### 49.4 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Not returning the job slot after execution | The slot pool is eventually exhausted | Return it with \`release_job\` |
| Creating jobs directly with raw \`new\` | Bypasses slot pool management | Allocate with the slot pool's \`allocate\` |
`
};

window.DOCS_DATA_EN['thread_task'] = {
  title: "thread::task — Coroutine tasks",
  content: `## 50. thread::task — Coroutine tasks

A coroutine task is represented by \`task<T>\`; frames are allocated from the runtime's tiered frame pool (three tiers 128 / 256 / 512, falling back to the heap when the limit is exceeded or the alignment is too large). All suspension is expressed with \`co_await\`.

### 50.1 task interfaces

| Interface | Description |
|------|------|
| \`valid()\` / \`done()\` | Validity / completion state |
| \`start()\` | Starts the coroutine |
| \`value()\` | Gets the value (\`task<T>\`) |
| \`handle()\` / \`release()\` | Handle / release ownership |
| \`destroy()\` | Destroys the frame |
| \`co_await task\` | Waits for a subtask; the continuation resumes after the subtask completes |

### 50.2 Submission and yielding

| Interface | Description |
|------|------|
| \`spawn(manager&, task<void>&&, priority, preferred_worker)\` | Submits a coroutine to the thread pool |
| \`co_yield_now()\` | Voluntarily yields, to be rescheduled by the worker; pass-through on non-worker threads |

### 50.3 Waiting and cancellation

| Interface | Description |
|------|------|
| \`cancel_token\` | Cancellation flag wrapper: \`cancelled()\` / \`poll()\` |
| \`co_await cancel_token\` | Returns immediately whether it has been cancelled |
| \`counter_awaiter{counter*, resume_waiter*}\` | Waits for the counter to reach zero |
| \`event_awaiter{event*, resume_waiter*}\` | Waits for the event to be triggered |
| \`resume_waiter_init(waiter*)\` | Initializes the resumer |

### 50.4 Frame scope

| Interface | Description |
|------|------|
| \`frame_scope::begin()\` / \`track()\` / \`complete_one()\` | Registers a batch of tasks |
| \`frame_scope::wait(timeout_ms)\` | Waits for all of them to complete |
| \`frame_scope::done()\` / \`submitted()\` / \`counter_ref()\` | State queries |

### 50.5 Example

\`\`\`cpp
thread::task<void> work()
{
    co_await thread::co_yield_now();
    co_return;
}

thread::spawn(mgr, work());
\`\`\`

### 50.6 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Synchronously blocking inside a worker | Violates the scheduling constraint | Suspend with \`co_await\` |
| Accessing a task again after it has been \`release\`d | The handle has been transferred | Do not use the original object after transferring it |
| Coroutine frame too large or alignment beyond the baseline | Falls back to heap allocation | Keep the coroutine frame size under control |
`
};

window.DOCS_DATA_EN['thread_sync'] = {
  title: "thread::sync — Synchronization primitives",
  content: `## 51. thread::sync — Synchronization primitives

Provides cross-thread counters, events, fences and a spin latch, used to coordinate worker suspension points with external threads.

### 51.1 counter

| Interface | Description |
|------|------|
| \`init(initial = 0)\` | Resets to the initial count |
| \`add(delta = 1)\` | Increases the count |
| \`value()\` / \`ready()\` | Current value / whether it has reached zero |
| \`complete_one()\` | Decrements by one, waking waiters when it reaches zero |
| \`complete(delta = 1)\` | Decrements by delta, waking waiters when it reaches zero |
| \`attach_waiter(waiter_node*)\` | Registers a waiter, returns whether suspension is required |

### 51.2 event

| Interface | Description |
|------|------|
| \`init()\` | Resets |
| \`trigger()\` / \`triggered()\` | Trigger / whether it has been triggered |
| \`attach_waiter(waiter_node*)\` | Registers a waiter, returns whether suspension is required |

### 51.3 fence

| Interface | Description |
|------|------|
| \`init(expected)\` | Advances one generation every \`expected\` arrivals |
| \`arrive()\` | Arrives once |
| \`generation()\` | Current generation |
| \`attach_waiter(waiter_node*, observed_generation)\` | Waits for the generation to change |

### 51.4 spin_latch

| Interface | Description |
|------|------|
| \`reset()\` | Resets to unreleased |
| \`release()\` | Releases and wakes all waiters |
| \`ready()\` | Whether it has been released |
| \`wait(timeout_ms)\` | Spin + yield + parked wait, returns whether it has been released |

### 51.5 Waiter nodes

| Interface | Description |
|------|------|
| \`waiter_node\` | Intrusive wait node: \`next_\` / \`wake_\` / \`signalled_\` |
| \`waiter_node_init(node, wake)\` | Initializes the node and the wake callback |
| \`waiter_list::push(node)\` / \`drain()\` | Link / unlink |
| \`waiter_list_wake(list)\` / \`waiter_list_wake_except(list, skip)\` | Wake all / all but one |

### 51.6 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Calling \`spin_latch::wait\` inside a worker | Blocks the worker thread | Use it only from non-worker threads |
| Reusing a counter / event that has not been \`init\`ed | Indeterminate state | \`init\` before use |
`
};

window.DOCS_DATA_EN['thread_mpmc'] = {
  title: "thread::mpmc_queue — Concurrent queues",
  content: `## 52. thread::mpmc_queue — Concurrent queues

Multi-producer multi-consumer ring queue and work-stealing deque, used to pass slot indices and job pointers between threads.

### 52.1 mpmc_queue

| Interface | Description |
|------|------|
| \`create(capacity_hint)\` / \`destroy()\` | Create (rounded up to a power of two) / destroy |
| \`try_push(value)\` | Enqueue, returns \`true\` on success |
| \`try_pop(out)\` | Dequeue |
| \`try_pop_bulk(out, max_count)\` | Bulk dequeue, returns the count |
| \`size()\` / \`empty()\` / \`capacity()\` | State |
| \`stats()\` | \`mpmc_stats\`: \`pushes_\` / \`pops_\` / \`push_full_\` / \`contended_\` |

### 52.2 work_deque

| Interface | Description |
|------|------|
| \`create(capacity_hint)\` / \`destroy()\` | Create / destroy |
| \`push(job*)\` | Enqueues from the bottom, growing when full |
| \`pop()\` | Dequeues from the bottom |
| \`steal(out, max_count)\` | Steals a batch from the top, returns the count |
| \`size()\` / \`empty()\` / \`capacity()\` / \`room()\` | State |
| \`grow()\` | Grows manually |
| \`stats()\` | \`work_deque_stats\` |

### 52.3 Example

\`\`\`cpp
thread::mpmc_queue q;
q.create(1024);
q.try_push(7);

std::uint32_t got = thread::mpmc_index_none;
if (q.try_pop(got))
{
    // got == 7
}
\`\`\`

### 52.4 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Enqueueing \`mpmc_index_none\` | Rejected, returns \`false\` | Use a valid index value |
| Expecting usable output after \`try_pop\` fails | Nothing is written on failure | Use the result only when it returns \`true\` |
`
};

window.DOCS_DATA_EN['thread_error'] = {
  title: "thread — Error delivery",
  content: `## 53. thread — Error delivery

The thread module defines no standalone error codes or error types; all failures are delivered through \`operating_message\`.

### 53.1 Error codes

| Constant | Trigger point |
|------|--------|
| \`om_err_not_running\` | Submission before \`configure\` / before start |
| \`om_err_shutting_down\` | Submission during the shutdown flow |
| \`om_err_queue_full\` | Injection queue or local queue is full |
| \`om_err_capacity_exceeded\` | Insufficient slot, frame pool, or task table capacity |
| \`om_err_out_of_memory\` | Memory allocation failure during graph construction |
| \`om_err_spawn_failed\` | Worker thread creation failed |
| \`om_err_invalid_argument\` | Invalid configuration, task identity, or role partition |
| \`om_err_cancelled\` | Abort caused by a cancellation scope |
| \`om_err_timeout\` | Deadline or watchdog timeout |
| \`om_err_already_exists\` | Repeated configuration |
| \`thread_task_body_faulted\` | Task body called \`ctx.fail()\` |

### 53.2 Levels

\`msg_level\`: \`debug\` / \`info\` / \`warn\` / \`error\`. Thread-side synchronization failures use \`warn\`; configuration and graph-construction failures use \`error\`.

### 53.3 Delivery outlets

| Interface | Description |
|------|------|
| \`configure(config, runtime = nullptr)\` | Returns \`operating_message\`, carrying the error code on failure |
| \`create(config)\` | Returns an object carrying the error code when graph construction fails |
| \`submit(job*, error, preferred)\` / \`try_submit(job*, error, preferred)\` | Submission failure writes the error code |
| \`set_error_sink(sink, user)\` | Synchronous failure outlet, \`void(const operating_message&, void*)\` |
| \`set_completion_error(fn, user)\` | Asynchronous failure outlet, \`void(operating_message, void*)\`, called once per faulted activation |
| \`ctx.fail(code, site)\` | Task body proactively reports failure; the code and site are delivered with the callback |

Success paths write no error object and invoke no callback; the library keeps no last error and performs no global counting.

### 53.4 Example

\`\`\`cpp
thread::manager mgr;
const operating_message config_error = mgr.configure(thread::profile::server(4));
if (!config_error)
{
    std::fprintf(stderr, "configure failed: %u\n", config_error.code());
    return 1;
}

mgr.set_completion_error(+[](operating_message failure, void*) noexcept
{
    std::fprintf(stderr, "task failed: %u level=%d\n",
                 failure.code(), static_cast<int>(failure.level()));
}, nullptr);

thread::task_body body{};
body.kind_ = thread::task_body_plain_ctx;
body.invoke_.plain_ctx_ = +[](thread::task_context& ctx) noexcept
{
    if (!load_resource())
    {
        ctx.fail(om_err_io_failed.code);
    }
};
\`\`\`

### 53.5 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Relying on thread-side global counters to count failures | The library provides no global state | Accumulate into your own container in the sink / callback |
| Blocking inside the callback or submitting a task that would deadlock | The callback runs on a worker thread | Only enqueue or record |
| Ignoring the return value of \`configure\` | Subsequent submissions will inevitably fail | Read the error code and handle it |
`
};

window.DOCS_DATA_EN['thread_metrics'] = {
  title: "thread::metrics — Runtime metrics",
  content: `## 54. thread::metrics — Runtime metrics

Collects a one-shot runtime snapshot of the manager, workers, lanes, frame pool and error counters.

### 54.1 Collection

| Interface | Description |
|------|------|
| \`collect_metrics(manager&)\` | Returns \`metrics_snapshot\` |

### 54.2 Snapshot fields

| Field | Description |
|------|------|
| \`manager\` | \`manager_metrics\`: \`submitted\` / \`rejected\` / \`completed\` / \`wakes\` |
| \`workers[]\` / \`worker_count\` | Per-thread \`worker_metrics\`: \`executed\` / \`local\` / \`injected\` / \`stolen\` / \`steal_attempts\` / \`parks\` / \`drops\` |
| \`lanes[]\` / \`lane_count\` | Per-lane \`lane_metrics\` |
| \`frame_pool_hits\` / \`frame_pool_misses\` / \`frame_pool_returns\` / \`frame_heap_allocs\` | Frame pool statistics |
| \`frame_high_water[]\` | Per-tier frame high-water mark |
| \`errors_by_tag[]\` / \`errors_total\` | Error counts |
| \`steal_hit_ratio()\` | Steal hit ratio |

### 54.3 Example

\`\`\`cpp
thread::metrics_snapshot snap = thread::collect_metrics(mgr);
std::printf("completed=%llu stolen=%llu ratio=%.3f\\n",
            (unsigned long long)snap.manager.completed,
            (unsigned long long)snap.total_stolen,
            snap.steal_hit_ratio());
\`\`\`

### 54.4 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Calling \`collect_metrics\` at high frequency | Collection has to traverse threads and lanes | Call it once per sampling period |
| Reading all workers after the limit is exceeded | The array is truncated | Watch \`workers_truncated\` |
`
};

window.DOCS_DATA_EN['thread_platform'] = {
  title: "thread::platform — Platform utilities",
  content: `## 55. thread::platform — Platform utilities

Cross-platform park/unpark primitives and thread attribute utilities, used by upper-layer scheduling and external thread coordination.

### 55.1 Platform and parking

| Interface | Description |
|------|------|
| \`current_platform()\` | \`windows\` / \`linux_futex\` / \`posix_condvar\` |
| \`platform_check_required()\` | Whether the parking primitives are available |
| \`park_state\` | Park state: \`word_\` / \`parks_\` |
| \`park_state_init(state)\` | Initializes the park state |
| \`park_once(state, expected, timeout_ms)\` | Parks once, returns \`woken\` / \`timed_out\` |
| \`unpark_other(state, new_word)\` / \`unpark_all(state, new_word)\` | Wake one / all |
| \`park_until_changed(addr, expected, size, timeout_ms)\` | Until the value changes |
| \`infinite_park_ms\` / \`default_park_ms\` | Infinite park / default park milliseconds |

### 55.2 Thread attributes

| Interface | Description |
|------|------|
| \`set_current_thread_name(name)\` | Sets the current thread name |
| \`pin_current_thread_to_core(core_index)\` | Pins the current thread to a core |
| \`process_thread_id()\` | Current thread system ID |

### 55.3 Utilities

| Interface | Description |
|------|------|
| \`platform_relax_pause()\` / \`platform_yield()\` | Pause hint / yield |
| \`mix_bits(value)\` | Bit mixing |
| \`count_trailing_zeros(value)\` | Trailing zero count |
| \`fast_rand\` | Lightweight random numbers: \`next()\` |

### 55.4 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Long \`park_once\` inside a worker | Blocks the worker thread | Park only from non-worker threads |
| Assuming core pinning always succeeds | Limited by platform and core count | Check the return value |
`
};

window.DOCS_DATA_EN['thread_task_graph'] = {
  title: "thread::task_graph — Persistent task graph",
  content: `## 56. thread::task_graph — Persistent task graph

Once registered, a task owns a stable number and can be activated repeatedly; the execution domain is partitioned by roles — sequential roles execute single-threaded by order key, while general roles execute in parallel on multiple threads.

### 56.1 Role configuration role_config

| Field | Default | Description |
|------|--------|------|
| \`name_\` | \`nullptr\` | Role name, up to 15 bytes, used for display only |
| \`first_worker_\` | \`0\` | Index of the first worker |
| \`worker_count_\` | \`0\` | Thread count of this role; must be \`1\` for sequential roles |
| \`mode_\` | \`parallel\` | \`parallel\` parallel / \`sequential\` sequential |
| \`policy_\` | \`keyed\` | Sequential domain policy: \`keyed\` supports reordering / \`fifo\` does not |
| \`external_\` | \`false\` | Whether it is attached by an external thread (only for sequential roles) |
| \`frame_auto_\` | \`false\` | Whether frames are driven automatically (the managed thread calls \`begin_frame\`/\`end_frame\` itself) |
| \`frame_rate_\` | \`60\` | Automatic frame rate |

The number of a \`role\` is its index into the role table, in the range \`[0, role_count)\`; build custom roles with \`make_role(id)\`. All roles must completely cover \`[0, worker_count)\` without overlapping.

### 56.2 Task graph configuration task_graph_config

| Field | Default | Description |
|------|--------|------|
| \`worker_count\` | \`0\` | Total logical thread count; \`0\` means driven only by the calling thread's \`pump_role\` |
| \`task_capacity\` | \`4096\` | Max task capacity; exceeding it returns \`invalid_task\` |
| \`roles\` | \`nullptr\` | Pointer to the role array; with \`nullptr\` all threads go to \`general\` |
| \`role_count\` | \`0\` | Number of roles |
| \`local_capacity\` | \`1024\` | Per-thread local task queue capacity |
| \`inject_capacity\` | \`4096\` | Global dispatch queue capacity |
| \`command_capacity\` | \`1024\` | Sequential domain reordering queue capacity |
| \`dependency_capacity\` | \`8192\` | Max total number of dependency edges |
| \`steal_batch\` | \`64\` | Max batch size for task rebalancing |

### 56.3 Lifecycle and queries

| Interface | Description |
|------|------|
| \`create(const task_graph_config&)\` | Builds tables and domains; returns \`false\` if already created |
| \`destroy()\` | Releases all tasks and queues |
| \`created()\` | Whether it has been created |
| \`task_capacity()\` / \`task_count()\` | Capacity / registered count |
| \`role_count()\` / \`worker_count()\` | Role count / logical thread count |
| \`role_at(role)\` | Role slot pointer, \`nullptr\` if out of range |
| \`role_mode(role)\` / \`role_external(role)\` | Role sequentiality and external flag |
| \`role_frame_auto(role)\` / \`role_frame_rate(role)\` | Automatic frames and frame rate |
| \`worker_role(worker)\` | Role that owns the worker |
| \`view(task_id)\` | Returns a \`task_view\` snapshot |
| \`state_of(task_id)\` | Returns \`task_state\` |
| \`metrics()\` | \`task_metrics\` count snapshot |

\`task_view\` fields: \`id_\` / \`state_\` / \`role_\` / \`mode_\` / \`affinity_\` / \`worker_\` / \`stealable_\` / \`parent_\` / \`key_\` / \`activations_\` / \`name_\`.

| \`task_state\` | Meaning |
|--------------|------|
| \`dormant\` | Registered, not activated |
| \`ready\` | Activated, waiting to execute |
| \`running\` | Executing |
| \`waiting\` | Waiting for dependencies, subtasks, or a frame boundary |
| \`completed\` | This activation has ended; the identity is retained |
| \`disabled\` | Further activation is forbidden |
| \`faulted\` | Task body reported an unrecoverable error |
| \`cancelled\` | This activation was cancelled |

### 56.4 Registration

All failable interfaces have a single form: they return \`operating_message\`. On success the object is empty; on failure it carries an error code and level.


| Interface | Description |
|------|------|
| \`declare(name)\` | Registers an empty task, returns \`task_builder\`, and the body is submitted by \`run\` |
| \`declare(name, task_body)\` | Registers and directly binds the task body, returns \`task_id\`; returns an invalid handle on failure |
| \`declare(name, task_body, role)\` | Registers with the specified role |
| \`declare_full(name, body, route_word, parent_word, key, flags)\` | Registers with full fields |
| \`declare_all(decls, count)\` | Bulk registration from a declaration table; if any entry is invalid, none take effect |
| \`set_body(id, task_body)\` | Binds or replaces the task body for an already registered task |

\`task_body\`:

| Field | Description |
|------|------|
| \`invoke_.plain_\` | \`void (*)(task_context&, void*)\` task body |
| \`invoke_.plain_ctx_\` | \`void (*)(task_context&)\` task body |
| \`invoke_.coro_\` | \`task<void> (*)(task_context&, void*)\` coroutine task body |
| \`invoke_.coro_ctx_\` | \`task<void> (*)(task_context&)\` coroutine task body |
| \`object_\` | User pointer passed into the task body |
| \`kind_\` | \`task_body_plain\` / \`task_body_plain_ctx\` / \`task_body_coroutine\` / \`task_body_coroutine_ctx\` |

builder methods:

| Method | Effect |
|------|------|
| \`role(role)\` | Sets the role |
| \`ordered()\` / \`unordered()\` | Sequential / parallel |
| \`pinned(worker)\` | Pins to the specified worker, not stealable by default |
| \`stealable(bool = true)\` | Declares that a pinned task may be stolen by the same role |
| \`priority_of(priority)\` | Sets the priority (parallel tasks only) |
| \`order(key)\` | Sets the order key |
| \`frames(period)\` | Activates every N frames |
| \`activation_of(policy, overlap)\` | Activation policy and overlap policy |
| \`detach()\` | The subtask does not take part in the parent's join |
| \`timeout(ns)\` | Sets the deadline |
| \`after_task(id)\` | Appends a predecessor dependency |
| \`then(fn)\` | Appends a stage task and establishes a partial order |
| \`run(body)\` | Applies the fields and binds the task body, returns \`task_id\` |
| \`commit()\` | Commits the stage chain, returns \`task_chain{head_, tail_}\` |
| \`id()\` | Current stage number |

Pipeline tags:

| Tag | Effect |
|------|------|
| \`on(role)\` | Sets the role |
| \`ordered\` / \`unordered\` | Sequential / parallel |
| \`pinned(worker)\` | Pins the worker |
| \`stealable\` | A pinned task may be stolen by the same role |
| \`at_priority(p)\` | Sets the priority |
| \`every_frames(n)\` | Activates every N frames |
| \`once\` / \`on_signal\` | Activate once / activate on signal |
| \`after(id)\` / \`after_any(id)\` | Ready after all predecessors / any predecessor completes |
| \`deadline(ns)\` | Sets the deadline |
| \`detached\` | The subtask does not take part in the join |

\`run\` supports four kinds of task body pointers and capturing callables:

\`\`\`cpp
void plain(thread::task_context&) noexcept;
void with_user(thread::task_context&, void*) noexcept;
thread::task<void> coro(thread::task_context&) noexcept;

int state = 0;
const thread::task_id a = mgr.declare("a") | thread::run(plain);
const thread::task_id b = mgr.declare("b") | thread::run(with_user);
const thread::task_id c = mgr.declare("c") | thread::run(coro);
const thread::task_id d = mgr.declare("d")
    | thread::run([&state](thread::task_context&) noexcept { (void)state; });
\`\`\`

Declaration table:

\`\`\`cpp
const thread::task_decl decls[] =
{
    {"render.camera", thread::role::renderer, thread::order_mode::sequential,
     thread::affinity_kind::role, thread::task_worker_none, thread::order_key{10}, render_camera},
    {"audio.mix", thread::role::general, thread::order_mode::parallel,
     thread::affinity_kind::worker, 3, thread::order_key{0}, mix_audio, true},
};
mgr.declare_all(decls, 2);
\`\`\`

### 56.5 Activation and identity changes

| Interface | Description |
|------|------|
| \`activate(id)\` | Activates once with the current identity |
| \`activate(id, activate_options)\` | \`queue_\` queues, \`delay_frames_\` delays frames |
| \`deactivate(id)\` | Cancels an activation that has not yet run |
| \`migrate(id, role)\` / \`rebind(id, role)\` | Changes the role, effective on the next activation |
| \`pin(id, worker)\` | Pins the worker and clears the stealable declaration |
| \`set_stealable(id, value)\` | Valid only for pinned parallel tasks |
| \`set_order_mode(id, mode)\` | Switches sequential / parallel |
| \`set_priority(id, p)\` | Rejected for sequential tasks |
| \`reorder(id, order_key)\` | Adjusts the relative position within the sequential domain |
| \`reparent(id, parent)\` | Changes the parent task; cycles are forbidden |
| \`disable(id)\` / \`enable(id)\` | Forbids / restores further activations |
| \`cancel(scope)\` / \`clear_cancel(scope)\` / \`cancelled(id)\` | Cancellation scope |
| \`set_deadline(id, ns)\` | Sets the deadline |

\`activate_options\`: \`queue_\` (queue overlapping activations), \`delay_frames_\` (number of delayed frames).

Failure always returns \`false\`; recoverable errors do not terminate the process.

### 56.6 Execution, frames and external threads

| Interface | Description |
|------|------|
| \`pump_role(role, budget)\` | Executes up to \`budget\` ready tasks of that role on the current thread |
| \`begin_frame(role)\` | Frame number +1, activates periodic tasks due this frame |
| \`end_frame(role)\` | Frame epoch +1 |
| \`frame_of(role)\` / \`epoch_of(role)\` | Current frame number / frame epoch |
| \`attach_thread(role, label = nullptr)\` | Binds the current thread to an external role |
| \`detach_thread(role)\` | Unbinds |
| \`thread_attached(role)\` | Whether it has been attached |
| \`has_worker_work(worker)\` | Whether the specified thread still has executable tasks |
| \`drain(limit = 1000000)\` | Drains tasks on the current thread, returns the number executed |

### 56.7 Error delivery

| Interface | Description |
|------|------|
| \`set_error_sink(error_sink, void* user)\` | Synchronous failure outlet, signature \`void(const operating_message&, void*)\`; pass \`nullptr\` to disable |
| \`set_completion_error(fn, void* user)\` | Asynchronous failure outlet, signature \`void(operating_message, void*)\`, called exactly once per faulted activation |
| \`configure(config)\` | Configuration failure writes into the caller's error object |
| \`submit(job*, error, preferred)\` / \`try_submit(job*, error, preferred)\` | Submission failure writes \`queue_full\` / \`shutting_down\` / \`null_pointer\` |

Success and normal cancellation trigger no callback; the library keeps no copy of the last error, so record history in the caller's own container if needed.

\`\`\`cpp
mgr.set_error_sink(+[](const operating_message& report, void* user) noexcept
{
    auto* out = static_cast<queue*>(user);
    (void)out->try_push(report);
}, &out);

mgr.set_completion_error(+[](operating_message report, void* user) noexcept
{
    auto* out = static_cast<queue*>(user);
    (void)out->try_push(report);
}, &out);
\`\`\`

### 56.8 Metrics snapshot

\`thread::collect_metrics(manager&)\` adds the following fields on top of the existing manager/worker/lane fields:

| Field | Description |
|------|------|
| \`task.declared\` / \`activations\` / \`completions\` | Task registration, activation, and completion counts |
| \`task.coalesced\` / \`stale_tickets\` | Overlap coalescing and stale scheduling drops |
| \`task.migrations\` / \`forwarded\` / \`dependency_waits\` | Migration, forwarding, resource waits |
| \`task.faults\` / \`cancellations\` / \`overruns\` | Faults, cancellations, sequential domain timeouts |
| \`watchdog.*\` | Scans, overdue, hung, heartbeat, queue timeouts, max wait/execution |
| \`admission.*\` | Admitted, rejected, degraded, dropped, coalesced, in flight |
| \`blocking_queue_depth\` / \`blocking_completed\` | Blocking offload pool |
| \`trace_dropped\` | Trace ring drops |
| \`spawned_workers\` / \`thread_budget\` | Actual spawned thread count / default thread budget |

### 56.9 manager integration

\`manager_config\` gains the fields: \`task_capacity\` (\`0\` disables the task graph), \`roles\`, \`role_count\`, \`task_local_capacity\`, \`task_command_capacity\`, \`task_inject_capacity\`, \`task_dependency_capacity\`.

\`manager\` forwards all task graph interfaces: \`declare\` / \`declare_all\` / \`activate\` / \`deactivate\` / \`migrate\` / \`pin\` / \`set_stealable\` / \`set_order_mode\` / \`set_priority\` / \`reorder\` / \`reparent\` / \`disable\` / \`enable\` / \`cancel\` / \`clear_cancel\` / \`cancelled\` / \`set_deadline\` / \`view\` / \`state_of\` / \`attach_thread\` / \`detach_thread\` / \`pump_role\` / \`begin_frame\` / \`end_frame\` / \`frame_of\` / \`set_error_sink\` / \`set_completion_error\` / \`task_stats\` / \`tasks\`.

### 56.10 Example

\`\`\`cpp

void render_camera(thread::task_context& ctx) noexcept { (void)ctx; }
void layout_ui(thread::task_context& ctx) noexcept { (void)ctx; }
void logic_tick(thread::task_context& ctx) noexcept { (void)ctx; }
void physics_step(thread::task_context& ctx) noexcept { (void)ctx; }
void mix_audio(thread::task_context& ctx) noexcept { (void)ctx; }

int main()
{
    thread::role_config roles[] =
    {
        {"general", 2, 2, thread::order_mode::parallel},
        {"renderer", 0, 1, thread::order_mode::sequential},
        {"logic", 1, 1, thread::order_mode::sequential},
    };
    roles[thread::role_index(thread::role::renderer)].external_ = true;
    roles[thread::role_index(thread::role::logic)].frame_auto_ = true;

    thread::manager_config config = thread::profile::game(4);
    config.roles = roles;
    config.role_count = 3;

    thread::manager mgr;
    const operating_message config_error = mgr.configure(config);
    if (!config_error)
    {
        std::fprintf(stderr, "configure failed: %u\n", config_error.code());
        return 1;
    }
    if (!mgr.attach_thread(thread::role::renderer, "render-main"))
    {
        return 1;
    }

    const thread::task_id camera = mgr.declare("render.camera")
        | thread::on(thread::role::renderer) | thread::ordered | thread::every_frames(1)
        | thread::run(render_camera);
    const thread::task_id ui = mgr.declare("render.ui")
        | thread::on(thread::role::renderer) | thread::ordered | thread::after(camera)
        | thread::every_frames(1) | thread::run(layout_ui);
    const thread::task_id tick = mgr.declare("logic.tick")
        | thread::on(thread::role::logic) | thread::ordered | thread::every_frames(1)
        | thread::run(logic_tick);
    const thread::task_id physics = mgr.declare("physics.step")
        | thread::on(thread::role::general) | thread::unordered
        | thread::at_priority(thread::priority::critical) | thread::run(physics_step);
    const thread::task_id audio = mgr.declare("audio.mix")
        | thread::on(thread::role::general) | thread::unordered | thread::pinned(3)
        | thread::run(mix_audio);

    mgr.start();
    for (int frame = 0; frame < 600; ++frame)
    {
        mgr.begin_frame(thread::role::renderer);
        mgr.pump_role(thread::role::renderer, 1024);
        mgr.end_frame(thread::role::renderer);
        (void)mgr.activate(physics);
        (void)mgr.activate(audio);
    }
    mgr.stop(thread::stop_mode::drain);
    mgr.detach_thread(thread::role::renderer);
    (void)ui;
    (void)tick;
    return 0;
}
\`\`\`

### 56.11 Coroutine wait primitives

\`\`\`cpp

thread::task<void> frame_flow(thread::task_context& ctx)
{
    co_await thread::next_frame(thread::role::logic);
    co_await thread::join(other_task);
    co_await thread::switch_to(thread::role::general);
    co_await thread::parallel_for(0, 1024, +[](std::size_t i) noexcept { work(i); });
    co_await thread::sleep_for(std::chrono::milliseconds(5));
}
\`\`\`

| Interface | Returns | Description |
|------|------|------|
| \`switch_to(role)\` | Error code | Resumes on the target role at the next step; hard-bound tasks return \`om_err_invalid_argument\` and continue on the current thread |
| \`next_frame(role)\` | Error code | Waits for that role's next \`end_frame\` |
| \`frame_sync(a, b)\` | Error code | Waits until both roles' frame epochs have advanced (role indices must be less than 32) |
| \`join(id)\` | Error code | Activates and waits for the subtask |
| \`when_all(ids, errs)\` | Error code | Waits for all subtasks; error codes are written into the caller's array |
| \`when_any(ids)\` | \`task_id\` | Returns the number of the first completed subtask |
| \`parallel_for(begin, end, body)\` | Error code | Partitions work items in parallel; the parent task suspends and waits |
| \`sleep_for(duration)\` / \`sleep_until(time_point)\` | Error code | Timed wait, resumes when due |
| \`co_yield_now()\` | — | Yields the current time slice and re-enqueues |

Wait primitives can only be used inside a task body; calling them outside returns \`om_err_not_running\`.

### 56.12 Typed results task_result

| Member | Description |
|------|------|
| \`error_\` | \`uint32_t\` error code, 0 means success |
| \`ok()\` | \`error_ == 0\` |
| \`has_value()\` | Whether it holds a value |
| \`value()\` | Reference to the value |
| \`take()\` | Moves out the value and clears it |
| \`set(value)\` / \`set_error(code)\` | Writes a value or an error code |

\`co_await task<T>\` returns \`task_result<T>\`; \`co_await task<void>\` returns \`task_result<void>\`. \`join\` returns only the error code.

\`\`\`cpp
thread::task<int> compute(thread::task_context&) { co_return 42; }

thread::task<void> use(thread::task_context&)
{
    thread::task_result<int> result = co_await compute_detached();
    if (result.ok())
    {
        const int value = result.take();
        (void)value;
    }
    co_return;
}
\`\`\`

### 56.13 Context propagation

| Interface | Description |
|------|------|
| \`create_context(request_id, trace_id, tenant_id)\` | Creates a context ID |
| \`set_context(id, ctx)\` / \`with_context(ctx)\` | Binds a context to a task |
| \`context_of(id)\` | Queries a task's context |
| \`contexts().request_id(ctx)\` / \`trace_id\` / \`tenant_id\` | Query fields |
| \`release_context(ctx)\` | Releases a reference |
| \`ctx.context_\` | Reads the context ID inside the task body |
| \`thread::current_context_id()\` | Queries within a thread |

Subtasks inherit the parent task's context by default; context capacity is configured by \`context_capacity\`.

### 56.14 Resource dependencies

| Interface / tag | Description |
|-------------|------|
| \`resource_id{value}\` | Resource ID |
| \`access_mode::read\` / \`write\` | Access mode |
| \`| thread::reads(id)\` | Declares read access |
| \`| thread::writes(id)\` | Declares write access |
| \`add_access(id, resource, mode)\` | Appends an access declaration after registration |

Read-read may run in parallel; read-write and write-write are mutually exclusive; writers take precedence over new readers. A single task may declare at most 4 reads and 4 writes.

### 56.15 Watchdog and cancellation

| Config / interface | Default | Description |
|-------------|--------|------|
| \`watchdog_enabled\` | \`true\` | Whether to start the watchdog thread |
| \`watchdog_period_ms\` | \`100\` | Scan period |
| \`default_deadline_ns\` | \`0\` | Global default deadline; \`0\` disables |
| \`hang_grace_ns\` | \`1s\` | Grace period after overdue before deeming the task hung |
| \`heartbeat_timeout_ns\` | \`0\` | Heartbeat timeout; \`0\` disables |
| \`queue_timeout_ns\` | \`0\` | Queue timeout; \`0\` disables |

| Interface | Description |
|------|------|
| \`set_deadline(id, ns)\` | Per-task deadline |
| \`cancel(scope)\` / \`clear_cancel(scope)\` / \`cancelled(id)\` | Cancellation scope |
| \`ctx.cancelled()\` | Cooperative check inside the task body |
| \`ctx.heartbeat()\` | Reports a heartbeat |
| \`ctx.budget_exceeded()\` | Whether this activation exceeded its time budget |
| \`set_budget(id, ns)\` / \`budget_of(id)\` | Sets / queries the task time budget |
| \`ctx.fail()\` | Reports an unrecoverable error, marking this activation \`faulted\`; the default code is \`thread_task_body_faulted\` |
| \`ctx.fail(code, site = current())\` | Reports with an error code and site; the asynchronous callback delivers by that code |
| \`watchdog_stats()\` | \`deadline_misses_\` / \`hung_tasks_\` / \`max_wait_ns_\` and others |
| \`watchdog_scan(now_ns)\` | Scans once manually |
| \`set_completion_error(fn, user)\` | Asynchronous failure callback: called exactly once per faulted activation, not called on success or cancellation |

Tasks must poll \`ctx.cancelled()\` at safe points. A cancellation scope also cancels that task's in-flight IO requests; a suspended \`read_some/write_all\` returns with \`cancelled\`.

### 56.16 Asynchronous IO

| Type / interface | Description |
|-------------|------|
| \`io_result\` | \`bytes_\` / \`status_\` / \`error_\` / \`ok()\` / \`would_block()\` |
| \`io_status\` | \`ok\` / \`eof\` / \`would_block\` / \`partial\` / \`error\` / \`cancelled\` |
| \`data_source\` | Three function pointers \`read_\` / \`write_\` / \`close_\` + \`self_\` |
| \`io_backend\` | Three function pointers \`submit_\` / \`cancel_\` / \`poll_\` + \`self_\` |
| \`io_domain\` | \`create(graph, backend, capacity)\` / \`start()\` / \`stop()\` / \`destroy()\` |
| \`read_some(domain, source, buffer)\` | \`co_await\` returns \`io_result\` |
| \`write_all(domain, source, buffer)\` | \`co_await\` returns \`io_result\`, short writes advance automatically |
| \`io_domain::metrics_\` | \`submitted_\` / \`completed_\` / \`bytes_read_\` / \`bytes_written_\` |

\`\`\`cpp
thread::io_domain domain;
domain.create(mgr.tasks(), backend, 128);
domain.start();

thread::task<void> read_flow(thread::task_context&)
{
    std::byte buffer[1024];
    const thread::io_result result = co_await thread::read_some(domain, source, buffer);
    if (result.ok())
    {
        handle(buffer, static_cast<std::size_t>(result.bytes_));
    }
    co_return;
}
\`\`\`

File, memory and socket access are provided through the \`data_source\` / \`io_backend\` adapters.

### 56.17 Blocking offload

| Interface | Description |
|------|------|
| \`manager_config::blocking_threads\` | Blocking thread count; \`0\` disables |
| \`manager_config::blocking_capacity\` | Queue capacity |
| \`blocking_call(domain, fn, arg)\` | \`co_await\` returns an error code |
| \`blocking_domain::queue_depth()\` | Queue depth |
| \`blocking_domain::completed_\` / \`rejected_\` | Counts |

Used for blocking calls such as databases, files and DNS; a full queue returns \`queue_full\`.

### 56.18 Admission control

| Field / interface | Description |
|-------------|------|
| \`admission_config::max_inflight_\` | In-flight limit; \`0\` disables |
| \`admission_config::tenant_slots_\` | Tenant slot count |
| \`admission_config::policy_\` | \`reject\` (reject) / \`drop_oldest\` (count drops) / \`coalesce\` (count coalesced) / \`degrade\` (still admit and count) |
| \`try_admit(tenant)\` | Whether admitted |
| \`on_complete(tenant)\` | Releases the quota |
| \`admission().metrics_\` | \`admitted_\` / \`rejected_\` / \`degraded_\` / \`dropped_\` / \`coalesced_\` / \`completed_\` |

manager config keys: \`admission_max_inflight\` / \`admission_tenant_slots\` / \`admission_policy\`.

### 56.19 Per-task tracing

| Interface | Description |
|------|------|
| \`manager_config::trace_enabled\` | Whether enabled |
| \`manager_config::trace_capacity\` | Event buffer capacity per worker |
| \`trace_recorder::drain(worker, sink, user)\` | Takes the events away |
| \`trace_recorder::dropped_\` | Drop count when the buffer is full |
| \`trace_event\` | \`timestamp_ns_\` / \`duration_ns_\` / \`task_\` / \`worker_\` / \`role_\` / \`kind_\` |

\`trace_kind\`: \`enqueue\` / \`start\` / \`end\` / \`wait_begin\` / \`wait_end\` / \`steal\` / \`migrate\` / \`cancel\` / \`io_submit\` / \`io_complete\`.

### 56.20 Multi-manager messages

| Interface | Description |
|------|------|
| \`manager_group::create(managers, count, capacity)\` | Registers multiple managers |
| \`post(manager_id, message)\` | Non-blocking delivery |
| \`poll(manager_id, budget)\` | Consumes and turns into activations/cancellations |
| \`poll_all(budget)\` | Consumes all inbound queues |
| \`group_message\` | \`kind_\` (\`activate\` / \`deactivate\` / \`cancel\`) + \`task_\` + \`source_manager_\` |

Across managers only messages are passed, tasks are not migrated; task numbers are unique within each manager.

### 56.21 Timing wheel

\`timing_wheel\` is a general-purpose container and does not depend on the thread module:

| Interface | Description |
|------|------|
| \`create(capacity, callback, owner, now_ns)\` | Builds the wheel |
| \`add(node, delay_ns)\` | Adds a timer |
| \`cancel(node)\` | Cancels |
| \`advance(now_ns)\` | Advances and triggers, returns the number triggered |
| \`size()\` / \`capacity()\` | Current and maximum |

\`timer_node\` is owned by the caller and must be used on the thread that created the timing wheel.

### 56.22 Hot reload

| Interface | Description |
|------|------|
| \`replace_body(id, task_body)\` | Replaces the task body used by the next activation |
| \`body_version(id)\` | Current version number, \`0\` means not replaced |

After replacement the next activation uses the new task body; the version number can be queried with \`body_version\`. The number of versions per task is limited by \`body_version_capacity\` (default 8); exceeding it returns \`false\`.

### 56.23 Deterministic mode

With \`role_config::deterministic_ = true\`: workers are mapped fixedly by \`task_id % worker_count\`, and replay results are reproducible. Used for replay and frame-sync roles.

### 56.24 Stage composition stage

\`\`\`cpp
auto pipeline = thread::stage(load) | thread::stage(parse) | thread::stage(store);
const thread::task_id io = mgr.declare("io.pipeline")
    | thread::on(thread::role::general)
    | thread::unordered
    | thread::run(pipeline);
\`\`\`

| Interface | Description |
|------|------|
| \`stage(fn)\` | Wraps an ordinary callable, with signature \`fn(task_context&)\` |
| \`stage_a | stage_b | ...\` | Composes into a sequential pipeline |
| \`run(pipeline)\` | Submits the composition; each activation calls all stages in order |

### 56.25 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Treating tasks as one-off work items | Task numbers are not reused and capacity is limited | Use \`job\` / work items for data-parallel leaves |
| Calling \`set_priority\` on a sequential task | The sequential domain does not support priorities | Set priorities only on parallel tasks |
| Declaring \`ordered\` on a general role | General roles do not provide sequential execution | Declare a separate sequential role for sequential semantics |
| Saving a \`task_context\` reference for the next activation | The context is only valid for a single activation | Put long-lived state in the object pointed to by \`ctx.user_\` |
| Expecting \`disable\` to terminate a running task immediately | It only prevents further activations | Poll \`ctx.cancelled()\` inside the task body |
| Declaring \`stealable\` on a pinned task that relies on thread-local resources | It may be executed by another thread of the same role | Do not declare stealable when thread exclusivity is required |
| Sharing mutable data across roles without synchronization | The framework does not lock user data | Establish ordering with dependencies or frame boundaries |
| Registering tasks unboundedly at runtime in response to events | Task capacity has a limit | Pre-register a fixed number of executor tasks |
`
};

window.DOCS_DATA_EN['thread_app'] = {
  title: "thread::app — Ready-to-use application skeleton",
  content: `## 57. thread::app — Ready-to-use application skeleton

The application skeleton brings thread configuration, task registration, the frame loop and the shutdown flow together in one place: with the default layout only three lines are needed to run, and when finer control is needed you can drop down to the interfaces in chapters 47–56.

### 57.1 Configuring app_config

| Field | Default | Description |
|------|--------|------|
| \`manager\` | \`profile::server(thread_budget())\` | Thread and capacity configuration; can be replaced wholesale |
| \`frame_rate\` | \`60\` | Frame rate; \`0\` means unlimited |

When \`manager.roles\` is not specified, the default layout is used: a single \`general\` parallel domain covers all worker threads, and the main thread only drives frames. A custom role table must contain \`general\`.

### 57.2 Lifecycle

| Interface | Description |
|------|------|
| \`app(app_config = {})\` | Configures and starts; on failure \`valid()\` is \`false\` and destruction is safe |
| \`valid()\` | Whether configuration succeeded |
| \`running()\` | Whether it has started and exit has not been requested |
| \`run()\` | Runs the frame loop in a blocking manner, returning after \`quit()\`; returns \`0\` on success |
| \`run_for(duration)\` | Runs for the specified duration, then returns |
| \`tick()\` | Executes one frame, convenient for embedding into an existing main loop |
| \`quit()\` | Requests exit, callable from inside a task or from other threads |
| \`stop()\` | Stops and drains; called automatically by the destructor |
| \`set_frame_rate(rate)\` | Adjusts the frame rate, \`0\` means unlimited |

### 57.3 Registering tasks and work items

| Interface | Description |
|------|------|
| \`every_frame(name, fn)\` | Activates once per frame, returns \`task_handle\` |
| \`parallel(name, fn)\` | Activated every frame, same semantics as \`every_frame\`; used to emphasize the parallel domain |
| \`once(name, fn)\` | Activates once immediately after registration |
| \`post(fn)\` | Submits a one-off work item without establishing a task identity; returns \`false\` when the queue is full |
| \`spawn(task<void>&&)\` | Submits a coroutine work item |
| \`every_frame(name, method, object)\` | Binds a member function, accepting \`void()\` and \`void(task_context&)\` |

Task bodies support four forms:

| Form | Example |
|------|------|
| Function pointer | \`void update(thread::task_context&) noexcept\` |
| Lambda with context | \`[](thread::task_context& ctx) noexcept { ... }\` |
| Lambda without parameters | \`[]() noexcept { ... }\` |
| Member function | \`app.every_frame("x", &System::update, &system)\` |

Stage compositions can be used directly as a task body:

\`\`\`cpp
auto pipeline = thread::stage(load) | thread::stage(parse) | thread::stage(store);
app.every_frame("io.pipeline", pipeline);
\`\`\`

### 57.4 Queries and dropping down

| Interface | Description |
|------|------|
| \`state_of(handle)\` / \`state_of(task_id)\` | Task state |
| \`stats()\` | \`task_metrics\` count snapshot |
| \`config_error()\` | The error object from a construction-time configuration failure; read it when \`valid()\` is \`false\` |
| \`phase()\` | Shutdown phase |
| \`frame_index()\` | Current frame number |
| \`main_role()\` | Default role |
| \`worker_count()\` | Number of worker threads |
| \`kernel()\` | The underlying \`manager\`, for calling interfaces from chapters 47–56 |

### 57.5 task_handle

| Interface | Description |
|------|------|
| \`valid()\` | Whether the handle is valid |
| \`id()\` | Task number, can be passed directly to kernel interfaces |
| \`state()\` | Current state |
| \`after(prev)\` | Registers a dependency: activated only after \`prev\` completes |
| \`priority(p)\` | Sets the priority (parallel tasks) |
| \`timeout(ns)\` | Sets the deadline |
| \`on(role)\` | Changes the role, effective on the next activation |
| \`activate()\` / \`disable()\` / \`enable()\` | Activate / forbid / restore |

### 57.6 Example

Three-line entry:

\`\`\`cpp

void update(thread::task_context&) noexcept { /* per-frame logic */ }
void physics(thread::task_context&) noexcept { /* parallel logic */ }

int main()
{
    thread::app app;
    app.every_frame("game.update", update);
    app.parallel("physics.step", physics);
    return app.run();
}
\`\`\`

Member functions, dependencies and exit:

\`\`\`cpp
struct game_system
{
    void update(thread::task_context&) noexcept { /* ... */ }
};

int main()
{
    thread::app_config config;
    config.frame_rate = 120;

    thread::app app{config};
    game_system system;

    const thread::task_handle input = app.every_frame("input.poll", &game_system::update, &system);
    const thread::task_handle logic = app.every_frame("logic.tick", &game_system::update, &system);
    logic.after(input);

    app.once("app.stop", [&app]() noexcept { app.quit(); });
    return app.run();
}
\`\`\`

Single-step driving (embedding into an existing loop):

\`\`\`cpp
thread::app app;
app.every_frame("update", update);

while (running)
{
    poll_platform_events();
    app.tick();
}
\`\`\`

### 57.7 What not to do

| Wrong practice | Problem | Correct practice |
|---------|------|---------|
| Continuing to use it after a configuration failure | Task registration returns invalid handles | Check \`valid()\` first |
| Blocking for a long time inside a task | Occupies a worker thread | Use \`co_await thread::sleep_for\` or split it into work items |
| Using \`post\` for tasks that need querying or dependencies | Work items have no task identity | Register tasks with \`every_frame\` / \`once\` |
| A custom role table missing \`general\` | \`valid()\` is \`false\` after construction | Declare at least \`general\`, or leave it to the default layout |
| Multiple threads calling \`tick()\` at the same time | Frame numbers and periodic activations get scrambled | Drive frames from only one thread |
`
};
