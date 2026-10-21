window.DOCS_DATA = window.DOCS_DATA || {};

window.DOCS_DATA['thread_manager'] = {
  id: 'thread_manager',
  title: "thread::manager — 工作线程池",
  category: 'thread',
  icon: 'M',
  order: 47,
  content: `## 47. thread::manager — 工作线程池

工作线程池管理器：配置、启动、提交任务、按通道调度、优雅停止，并汇总运行统计。无线程模式（嵌入式）下由 \`pump\` 驱动执行。

### 47.1 配置 manager_config

| 字段 | 默认值 | 说明 |
|------|--------|------|
| \`worker_count\` | \`0\` | 线程预算总数（含原生主线程接入的外部角色）；\`0\` 为嵌入式（无线程，仅 \`pump\` 驱动） |
| \`inject_capacity\` | \`4096\` | 全局注入队列容量 |
| \`local_capacity\` | \`1024\` | 每线程本地双端队列容量 |
| \`steal_batch\` | \`64\` | 单次窃取批量上限 |
| \`pin_workers\` | \`false\` | 是否将工作线程绑定到核心 |
| \`priority_levels\` | \`3\` | 优先级层数 |
| \`aging_threshold\` | \`64\` | 老化阈值：连续弹出该次数后下沉取最低优先级 |
| \`lane_capacity\` | \`64\` | 通道注册上限 |
| \`mailbox_capacity\` | \`4096\` | 每线程邮箱队列容量 |
| \`job_slot_capacity\` | \`8192\` | 任务槽池容量 |
| \`frame_pool_blocks\` | \`1024\` | 每级协程帧块数 |
| \`task_capacity\` | \`4096\` | 持久任务图容量；\`0\` 关闭任务图 |
| \`roles\` / \`role_count\` | \`nullptr\` / \`0\` | 角色划分；未给出时全部线程归入 \`general\` |
| \`task_local_capacity\` | \`1024\` | 每线程本地任务队列容量 |
| \`task_command_capacity\` | \`1024\` | 顺序域命令队列容量 |
| \`task_inject_capacity\` | \`4096\` | 任务注入队列容量 |
| \`task_dependency_capacity\` | \`8192\` | 任务依赖边上限 |

预设配置（\`thread::profile\`）：

| 接口 | 说明 |
|------|------|
| \`profile::thread_budget()\` | 默认线程预算：硬件线程数减一（主线程参与帧循环） |
| \`profile::server(worker_count = thread_budget())\` | 服务端预设，不绑核 |
| \`profile::game(worker_count = thread_budget())\` | 游戏预设，绑核，较小窃取批量 |
| \`profile::embedded()\` | 嵌入式预设，无线程（\`worker_count = 0\`） |
| \`profile::is_embedded(config)\` | 判断是否为嵌入式配置 |

### 47.2 生命周期

| 接口 | 说明 |
|------|------|
| \`configure(manager_config, runtime_context* = nullptr)\` | 创建线程与队列并绑定运行时；已配置返回 \`false\` |
| \`start()\` | 启动工作线程 |
| \`stop()\` / \`stop(stop_mode, destroy_policy)\` | 停止并回收 |
| \`running()\` | 是否已启动 |
| \`phase()\` | 关闭阶段：\`running\` / \`draining\` / \`aborting\` / \`stopped\` |
| \`stopping()\` | 是否进入关闭流程 |

| 枚举 | 取值 |
|------|------|
| \`stop_mode\` | \`drain\`（排空后再停）/ \`abort\`（立即停） |
| \`destroy_policy\` | \`soft\`（软删，跳过内联捕获析构）/ \`hard\`（硬删，析构并销毁挂起帧） |

### 47.3 提交与驱动

| 接口 | 说明 |
|------|------|
| \`submit(job*, preferred_worker = -1)\` | 阻塞式提交，返回槽位索引；失败返回 \`slot_index_none\` |
| \`try_submit(job*, preferred_worker = -1)\` | 非阻塞提交 |
| \`submit_to_lane(lane*, job*)\` | 提交到指定通道 |
| \`pump(budget = 64)\` | 无线程模式驱动一次执行，返回执行数 |
| \`drain(spin_limit = 0, include_new_submissions = false)\` | 等待任务完成，返回 \`drain_status\`；无线程模式会主动驱动执行 |
| \`take_job(slot_index&)\` / \`slot_of(slot_index)\` | 取用 / 查询任务槽 |
| \`release_job_local(job*)\` | 释放任务到本管理器槽池 |

| 枚举 | 取值 |
|------|------|
| \`drain_status\` | \`complete\`（已完成）/ \`timeout\`（达到自旋上限） |

### 47.4 通道与统计

| 接口 | 说明 |
|------|------|
| \`create_lane(lane_config)\` | 创建通道，返回 \`lane*\` |
| \`lane_at(index)\` / \`lane_count()\` | 查询通道 |
| \`worker_count()\` / \`worker_at(index)\` | 查询工作线程（含外部角色） |
| \`spawned_worker_count()\` | 实际创建的工作线程数（预算扣除外部角色） |
| \`inject_queue()\` | 全局注入队列 |
| \`stats()\` | \`manager_stats\`：\`submitted_\` / \`rejected_\` / \`wakes_\` / \`drain_timeouts_\` / \`submit_waits_\` |
| \`completed_count()\` | 已完成任务数 |

### 47.5 示例

\`\`\`cpp

thread::manager mgr;
if (!mgr.configure(thread::profile::server(4)))
{
    return 1;
}
mgr.start();

std::uint32_t slot = thread::slot_index_none;
thread::job* j = mgr.take_job(slot);
thread::fill_inline_job(j, []() noexcept { /* 任务体 */ });
mgr.submit(j);

const thread::drain_status status = mgr.drain();
if (status == thread::drain_status::timeout)
{
    // 自旋上限内未完成
}
mgr.stop();
\`\`\`

### 47.6 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 未 \`configure\` 直接 \`start\` | 无工作线程与队列 | 先 \`configure\` 再 \`start\` |
| 用 \`stop(stop_mode::abort)\` 等待任务执行完 | 未执行任务被丢弃 | 需要执行完用 \`stop(stop_mode::drain)\` 或先 \`drain\` |
| worker 内同步等待 | 阻塞工作线程 | 用协程 \`co_await\` 挂起 |
`
};

window.DOCS_DATA['thread_lane'] = {
  id: 'thread_lane',
  title: "thread::lane — 任务通道",
  category: 'thread',
  icon: 'L',
  order: 48,
  content: `## 48. thread::lane — 任务通道

通道用于将任务分组并按固定顺序执行：独占（由创建时绑定的线程 FIFO 抽取）、绑定（指定线程抽取）、共享（任意线程抢占执行）。

### 48.1 配置 lane_config

| 工厂 | 模式 | 说明 |
|------|------|------|
| \`lane_config::exclusive(capacity)\` | \`exclusive\` | 由创建时绑定的线程按 FIFO 抽取 |
| \`lane_config::pinned(worker, capacity)\` | \`pinned\` | 由指定 worker 抽取 |
| \`lane_config::shared(capacity)\` | \`shared\` | 任意 worker 抢占执行 |

| 字段 | 默认值 | 说明 |
|------|--------|------|
| \`mode\` | \`exclusive\` | 通道模式 |
| \`pinned_worker\` | \`0\` | 绑定模式下的 worker 序号 |
| \`capacity\` | \`4096\` | 队列容量 |
| \`drain_budget\` | \`64\` | 单次抽取预算 |

### 48.2 接口

| 接口 | 说明 |
|------|------|
| \`create(id, lane_config, runtime_context*)\` | 创建通道 |
| \`id()\` / \`mode()\` / \`bound_worker()\` / \`drain_budget()\` | 查询属性 |
| \`submit(slot_index)\` | 提交任务槽，返回槽位索引 |
| \`try_pop(index&)\` | 取出一个任务槽 |
| \`try_acquire(worker_id)\` / \`owns(worker_id)\` / \`release_ownership(worker_id)\` | 批量执行所有权 |
| \`empty()\` / \`size()\` | 队列状态 |
| \`last_executed_seq()\` | 最近执行序号 |
| \`stats()\` | \`lane_stats\`：\`submitted_\` / \`executed_\` / \`owned_batches_\` / \`reclaims_\` / \`seq_breaks_\` |

### 48.3 示例

\`\`\`cpp
thread::lane* l = mgr.create_lane(thread::lane_config::exclusive(1024));

std::uint32_t slot = thread::slot_index_none;
thread::job* j = mgr.take_job(slot);
thread::fill_inline_job(j, []() noexcept { /* 顺序任务 */ });
mgr.submit_to_lane(l, j);
\`\`\`

### 48.4 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 制作多于 \`lane_capacity\` 的通道 | 创建返回 \`nullptr\` | 提高 \`lane_capacity\` |
| 预期独占通道被非绑定线程执行 | 破坏 FIFO 前提 | 独占/绑定通道仅由绑定者抽取 |
`
};

window.DOCS_DATA['thread_job'] = {
  id: 'thread_job',
  title: "thread::job — 任务与优先级",
  category: 'thread',
  icon: 'J',
  order: 49,
  content: `## 49. thread::job — 任务与优先级

任务为固定 64 字节内联槽：大小不超过槽且平凡可拷贝可调用对象内联存储，超出则堆捕获。任务从槽池分配，执行后归还。

### 49.1 优先级

| 值 | 说明 |
|----|------|
| \`priority::background\` | 后台 |
| \`priority::normal\` | 普通（默认） |
| \`priority::critical\` | 关键 |

### 49.2 槽池与生命周期

| 接口 | 说明 |
|------|------|
| \`job_pool::create(capacity)\` / \`destroy()\` | 创建 / 销毁槽池 |
| \`job_pool::allocate(slot_index&)\` | 分配任务槽 |
| \`job_pool::release(slot_index)\` | 归还任务槽 |
| \`job_pool::slot_of(index)\` / \`index_of(job*)\` | 槽与任务的互查 |
| \`job_pool::available()\` / \`capacity()\` | 剩余 / 总容量 |
| \`take_slot_job(slot_index&)\` | 从当前槽池取任务 |
| \`run_job(job*)\` / \`destroy_job(job*)\` / \`release_job(job*)\` | 执行 / 析构 / 释放 |
| \`reset_job_metadata(job*)\` | 复位元数据 |
| \`fill_job(job*, callable)\` / \`fill_inline_job\` / \`fill_overflow_job\` | 填充任务体 |

### 49.3 优先级栈

| 接口 | 说明 |
|------|------|
| \`priority_stack::create(levels, capacity_per_level)\` | 创建分层栈 |
| \`push(job*)\` / \`pop()\` / \`steal(out, max_count)\` | 入 / 出 / 窃取 |
| \`total_size()\` / \`levels()\` / \`drops()\` | 统计 |

### 49.4 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 执行后不归还任务槽 | 槽池最终耗尽 | 用 \`release_job\` 归还 |
| 直接用裸 \`new\` 造任务 | 绕过槽池管理 | 用槽池 \`allocate\` 分配 |
`
};

window.DOCS_DATA['thread_task'] = {
  id: 'thread_task',
  title: "thread::task — 协程任务",
  category: 'thread',
  icon: 'K',
  order: 50,
  content: `## 50. thread::task — 协程任务

协程任务以 \`task<T>\` 表示，帧由运行时的分级帧池分配（128 / 256 / 512 三级，超限或对齐过大回退堆）。挂起一律以 \`co_await\` 表达。

### 50.1 task 接口

| 接口 | 说明 |
|------|------|
| \`valid()\` / \`done()\` | 有效性 / 完成状态 |
| \`start()\` | 启动协程 |
| \`value()\` | 取值（\`task<T>\`） |
| \`handle()\` / \`release()\` | 句柄 / 释放所有权 |
| \`destroy()\` | 销毁帧 |
| \`co_await task\` | 等待子任务，续体在子任务完成后恢复 |

### 50.2 提交与让出

| 接口 | 说明 |
|------|------|
| \`spawn(manager&, task<void>&&, priority, preferred_worker)\` | 提交协程到线程池 |
| \`co_yield_now()\` | 主动让出，由 worker 重新调度；非 worker 线程下为直通 |

### 50.3 等待与取消

| 接口 | 说明 |
|------|------|
| \`cancel_token\` | 取消标志包装：\`cancelled()\` / \`poll()\` |
| \`co_await cancel_token\` | 立即返回是否已取消 |
| \`counter_awaiter{counter*, resume_waiter*}\` | 等待计数器归零 |
| \`event_awaiter{event*, resume_waiter*}\` | 等待事件触发 |
| \`resume_waiter_init(waiter*)\` | 初始化唤起器 |

### 50.4 帧作用域

| 接口 | 说明 |
|------|------|
| \`frame_scope::begin()\` / \`track()\` / \`complete_one()\` | 登记一批任务 |
| \`frame_scope::wait(timeout_ms)\` | 等待全部完成 |
| \`frame_scope::done()\` / \`submitted()\` / \`counter_ref()\` | 状态查询 |

### 50.5 示例

\`\`\`cpp
thread::task<void> work()
{
    co_await thread::co_yield_now();
    co_return;
}

thread::spawn(mgr, work());
\`\`\`

### 50.6 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| worker 内同步阻塞等待 | 违反调度约束 | 用 \`co_await\` 挂起 |
| 对已 \`release\` 的 task 再访问 | 句柄已转移 | 转移后不再使用原对象 |
| 协程帧过大或对齐超基准 | 回退堆分配 | 控制协程帧大小 |
`
};

window.DOCS_DATA['thread_sync'] = {
  id: 'thread_sync',
  title: "thread::sync — 同步原语",
  category: 'thread',
  icon: 'Y',
  order: 51,
  content: `## 51. thread::sync — 同步原语

提供跨线程计数、事件、栅栏与自旋闩，用于工作线程挂起点与外部线程之间的协调。

### 51.1 计数器 counter

| 接口 | 说明 |
|------|------|
| \`init(initial = 0)\` | 复位到初始计数 |
| \`add(delta = 1)\` | 增加计数 |
| \`value()\` / \`ready()\` | 当前值 / 是否归零 |
| \`complete_one()\` | 减一，归零时唤醒等待者 |
| \`complete(delta = 1)\` | 减 delta，归零时唤醒等待者 |
| \`attach_waiter(waiter_node*)\` | 注册等待者，返回是否需挂起 |

### 51.2 事件 event

| 接口 | 说明 |
|------|------|
| \`init()\` | 复位 |
| \`trigger()\` / \`triggered()\` | 触发 / 是否已触发 |
| \`attach_waiter(waiter_node*)\` | 注册等待者，返回是否需挂起 |

### 51.3 栅栏 fence

| 接口 | 说明 |
|------|------|
| \`init(expected)\` | 每 \`expected\` 次到达推进一代 |
| \`arrive()\` | 到达一次 |
| \`generation()\` | 当前代 |
| \`attach_waiter(waiter_node*, observed_generation)\` | 等待换代 |

### 51.4 自旋闩 spin_latch

| 接口 | 说明 |
|------|------|
| \`reset()\` | 复位为未释放 |
| \`release()\` | 释放并唤醒全部等待者 |
| \`ready()\` | 是否已释放 |
| \`wait(timeout_ms)\` | 自旋 + 让出 + 停放等待，返回是否已释放 |

### 51.5 等待者节点

| 接口 | 说明 |
|------|------|
| \`waiter_node\` | 侵入式等待节点：\`next_\` / \`wake_\` / \`signalled_\` |
| \`waiter_node_init(node, wake)\` | 初始化节点与唤起回调 |
| \`waiter_list::push(node)\` / \`drain()\` | 入链 / 摘链 |
| \`waiter_list_wake(list)\` / \`waiter_list_wake_except(list, skip)\` | 唤醒全部 / 除一个外全部 |

### 51.6 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| worker 内调用 \`spin_latch::wait\` | 阻塞工作线程 | 仅非 worker 线程使用 |
| 复用未 \`init\` 的 counter / event | 状态不确定 | 使用前先 \`init\` |
`
};

window.DOCS_DATA['thread_mpmc'] = {
  id: 'thread_mpmc',
  title: "thread::mpmc_queue — 并发队列",
  category: 'thread',
  icon: 'Q',
  order: 52,
  content: `## 52. thread::mpmc_queue — 并发队列

多生产者多消费者环形队列与工作窃取双端队列，用于线程间传递槽位索引与任务指针。

### 52.1 mpmc_queue

| 接口 | 说明 |
|------|------|
| \`create(capacity_hint)\` / \`destroy()\` | 创建（向上取整为 2 的幂）/ 销毁 |
| \`try_push(value)\` | 入队，成功返回 \`true\` |
| \`try_pop(out)\` | 出队 |
| \`try_pop_bulk(out, max_count)\` | 批量出队，返回数量 |
| \`size()\` / \`empty()\` / \`capacity()\` | 状态 |
| \`stats()\` | \`mpmc_stats\`：\`pushes_\` / \`pops_\` / \`push_full_\` / \`contended_\` |

### 52.2 work_deque

| 接口 | 说明 |
|------|------|
| \`create(capacity_hint)\` / \`destroy()\` | 创建 / 销毁 |
| \`push(job*)\` | 从底端入队，满则扩容 |
| \`pop()\` | 从底端出队 |
| \`steal(out, max_count)\` | 从顶端窃取一批，返回数量 |
| \`size()\` / \`empty()\` / \`capacity()\` / \`room()\` | 状态 |
| \`grow()\` | 手动扩容 |
| \`stats()\` | \`work_deque_stats\` |

### 52.3 示例

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

### 52.4 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 传入 \`mpmc_index_none\` 入队 | 被拒绝，返回 \`false\` | 用合法索引值 |
| 期望 \`try_pop\` 失败后输出可用 | 失败时不写出 | 只在返回 \`true\` 时使用结果 |
`
};

window.DOCS_DATA['thread_error'] = {
  id: 'thread_error',
  title: "thread::error — 错误处理与日志",
  category: 'thread',
  icon: 'E',
  order: 53,
  content: `## 53. thread::error — 错误处理与日志

统一错误码、严重级、处置动作与日志出口，全模块以 \`FRAMEWORK_REPORT\` 上报。

### 53.1 错误码 errc

| 值 | 说明 |
|----|------|
| \`ok\` | 无错误 |
| \`no_such_lane\` / \`no_such_worker\` / \`no_such_tag\` | 目标不存在 |
| \`lane_destroyed\` | 通道已销毁 |
| \`inject_queue_full\` / \`lane_queue_full\` | 队列已满 |
| \`job_slot_exhausted\` / \`coroutine_frame_exhausted\` | 槽 / 帧耗尽 |
| \`thread_spawn_failed\` | 线程创建失败 |
| \`not_running\` / \`shutdown_in_progress\` | 未运行 / 正在关闭 |
| \`already_registered\` | 重复注册 |
| \`invalid_argument\` / \`timeout\` | 参数非法 / 超时 |
| \`invariant_broken\` | 不变量被破坏 |
| \`handler_faulted\` | 处理器自身出错 |
| \`cancelled\` | 已取消 |

### 53.2 严重级与动作

| 类型 | 取值 |
|------|------|
| \`error_severity\` | \`info\` / \`warning\` / \`error\` / \`fatal\` |
| \`error_action\` | \`continue_\` / \`abort_frame\` / \`shutdown\` / \`terminate_\` |

### 53.3 处理器与日志

| 接口 | 说明 |
|------|------|
| \`set_error_handler(handler)\` | 设置 \`error\` 级处理器 |
| \`set_fatal_handler(handler)\` | 设置 \`fatal\` 级处理器 |
| \`set_log_sink(sink, user)\` | 设置日志出口 |
| \`report(...)\` | 上报一条错误，返回处置动作 |
| \`reset_error_state()\` | 复位计数器与处理器 |
| \`global_error_counters()\` | 全局错误计数 |
| \`severity_name(severity)\` / \`errc_name(code)\` | 名称查询 |

### 53.4 报告宏

| 宏 | 说明 |
|----|------|
| \`FRAMEWORK_REPORT(severity, code)\` | 上报错误，自动填充站点 |
| \`FRAMEWORK_REPORT_TAGGED(severity, code, tag, lane, flags)\` | 带标签上报 |
| \`FRAMEWORK_INVARIANT(condition, code)\` | 断言，失败即终止 |

### 53.5 示例

\`\`\`cpp
thread::set_log_sink([](thread::log_level level, const char* text, void*) noexcept {
    thread::error_severity sev = static_cast<thread::error_severity>(level);
    std::printf("[%s] %s\n", thread::severity_name(sev), text);
}, nullptr);
\`\`\`

### 53.6 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 在处理器内再次上报 | 触发递归保护 | 处理器只记录或返回动作 |
| 依赖 \`fatal\` 默认行为 | 默认终止进程 | 如需继续，注册 \`fatal\` 处理器返回 \`continue_\` |
`
};

window.DOCS_DATA['thread_metrics'] = {
  id: 'thread_metrics',
  title: "thread::metrics — 运行指标",
  category: 'thread',
  icon: 'N',
  order: 54,
  content: `## 54. thread::metrics — 运行指标

一次性采集管理器、工作线程、通道、帧池与错误计数的运行快照。

### 54.1 采集

| 接口 | 说明 |
|------|------|
| \`collect_metrics(manager&)\` | 返回 \`metrics_snapshot\` |

### 54.2 快照字段

| 字段 | 说明 |
|------|------|
| \`manager\` | \`manager_metrics\`：\`submitted\` / \`rejected\` / \`completed\` / \`wakes\` |
| \`workers[]\` / \`worker_count\` | 每线程 \`worker_metrics\`：\`executed\` / \`local\` / \`injected\` / \`stolen\` / \`steal_attempts\` / \`parks\` / \`drops\` |
| \`lanes[]\` / \`lane_count\` | 每通道 \`lane_metrics\` |
| \`frame_pool_hits\` / \`frame_pool_misses\` / \`frame_pool_returns\` / \`frame_heap_allocs\` | 帧池统计 |
| \`frame_high_water[]\` | 每级帧高水位 |
| \`errors_by_tag[]\` / \`errors_total\` | 错误计数 |
| \`steal_hit_ratio()\` | 窃取命中率 |

### 54.3 示例

\`\`\`cpp
thread::metrics_snapshot snap = thread::collect_metrics(mgr);
std::printf("completed=%llu stolen=%llu ratio=%.3f\\n",
            (unsigned long long)snap.manager.completed,
            (unsigned long long)snap.total_stolen,
            snap.steal_hit_ratio());
\`\`\`

### 54.4 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 高频调用 \`collect_metrics\` | 采集需遍历线程与通道 | 按采样周期调用 |
| 超上限后按全量读取 worker | 数组被截断 | 关注 \`workers_truncated\` |
`
};

window.DOCS_DATA['thread_platform'] = {
  id: 'thread_platform',
  title: "thread::platform — 平台工具",
  category: 'thread',
  icon: 'P',
  order: 55,
  content: `## 55. thread::platform — 平台工具

跨平台停放/唤醒原语与线程属性工具，供上层调度与外部线程协调使用。

### 55.1 平台与停放

| 接口 | 说明 |
|------|------|
| \`current_platform()\` | \`windows\` / \`linux_futex\` / \`posix_condvar\` |
| \`platform_check_required()\` | 停放原语是否可用 |
| \`park_state\` | 停放状态：\`word_\` / \`parks_\` |
| \`park_state_init(state)\` | 初始化停放状态 |
| \`park_once(state, expected, timeout_ms)\` | 停放一次，返回 \`woken\` / \`timed_out\` |
| \`unpark_other(state, new_word)\` / \`unpark_all(state, new_word)\` | 唤醒一个 / 全部 |
| \`park_until_changed(addr, expected, size, timeout_ms)\` | 直到值变化 |
| \`infinite_park_ms\` / \`default_park_ms\` | 无限停放 / 默认停放毫秒 |

### 55.2 线程属性

| 接口 | 说明 |
|------|------|
| \`set_current_thread_name(name)\` | 设置当前线程名 |
| \`pin_current_thread_to_core(core_index)\` | 绑定当前线程到核心 |
| \`process_thread_id()\` | 当前线程系统 ID |

### 55.3 工具

| 接口 | 说明 |
|------|------|
| \`platform_relax_pause()\` / \`platform_yield()\` | 暂停提示 / 让出 |
| \`mix_bits(value)\` | 位混合 |
| \`count_trailing_zeros(value)\` | 尾零计数 |
| \`fast_rand\` | 轻量随机数：\`next()\` |

### 55.4 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| worker 内长时间 \`park_once\` | 阻塞工作线程 | 仅非 worker 线程停放 |
| 假定绑核一定成功 | 受平台与核心数限制 | 检查返回值 |
`
};

window.DOCS_DATA['thread_task_graph'] = {
  id: 'thread_task_graph',
  title: "thread::task_graph — 持久任务图",
  category: 'thread',
  icon: 'G',
  order: 56,
  content: `## 56. thread::task_graph — 持久任务图

任务注册后拥有稳定编号，可反复激活；执行域由角色划分，顺序角色按顺序键单线程执行，通用角色多线程并行。

### 56.1 角色配置 role_config

| 字段 | 默认值 | 说明 |
|------|--------|------|
| \`name_\` | \`nullptr\` | 角色名，最多 15 字节，仅用于展示 |
| \`first_worker_\` | \`0\` | 首个工作线程编号 |
| \`worker_count_\` | \`0\` | 该角色的线程数；顺序角色必须为 \`1\` |
| \`mode_\` | \`parallel\` | \`parallel\` 并行 / \`sequential\` 顺序 |
| \`policy_\` | \`keyed\` | 顺序域策略：\`keyed\` 支持重排序 / \`fifo\` 不支持 |
| \`external_\` | \`false\` | 是否由外部线程接入（只能用于顺序角色） |
| \`frame_auto_\` | \`false\` | 是否自动驱动帧（托管线程自行 \`begin_frame\`/\`end_frame\`） |
| \`frame_rate_\` | \`60\` | 自动帧率 |

\`role\` 的编号即角色表下标，范围 \`[0, role_count)\`；自定义角色用 \`make_role(id)\` 构造。所有角色必须完整覆盖 \`[0, worker_count)\`，不得重叠。

### 56.2 任务图配置 task_graph_config

| 字段 | 默认值 | 说明 |
|------|--------|------|
| \`worker_count\` | \`0\` | 逻辑线程总数；\`0\` 表示仅由调用线程 \`pump_role\` 驱动 |
| \`task_capacity\` | \`4096\` | 任务容量上限；超出返回 \`invalid_task\` |
| \`roles\` | \`nullptr\` | 角色数组指针；\`nullptr\` 时全部线程归入 \`general\` |
| \`role_count\` | \`0\` | 角色数量 |
| \`local_capacity\` | \`1024\` | 每线程本地任务队列容量 |
| \`inject_capacity\` | \`4096\` | 全局投递队列容量 |
| \`command_capacity\` | \`1024\` | 顺序域调整队列容量 |
| \`dependency_capacity\` | \`8192\` | 依赖边总数上限 |
| \`steal_batch\` | \`64\` | 任务再平衡的批量上限 |

### 56.3 生命周期与查询

| 接口 | 说明 |
|------|------|
| \`create(const task_graph_config&)\` | 建表与建域；已创建返回 \`false\` |
| \`destroy()\` | 释放全部任务与队列 |
| \`created()\` | 是否已创建 |
| \`task_capacity()\` / \`task_count()\` | 容量 / 已注册数量 |
| \`role_count()\` / \`worker_count()\` | 角色数 / 逻辑线程数 |
| \`role_at(role)\` | 角色槽指针，越界返回 \`nullptr\` |
| \`role_mode(role)\` / \`role_external(role)\` | 角色顺序性与外部标记 |
| \`role_frame_auto(role)\` / \`role_frame_rate(role)\` | 自动帧与帧率 |
| \`worker_role(worker)\` | 工作线程所属角色 |
| \`view(task_id)\` | 返回 \`task_view\` 快照 |
| \`state_of(task_id)\` | 返回 \`task_state\` |
| \`metrics()\` | \`task_metrics\` 计数快照 |

\`task_view\` 字段：\`id_\` / \`state_\` / \`role_\` / \`mode_\` / \`affinity_\` / \`worker_\` / \`stealable_\` / \`parent_\` / \`key_\` / \`activations_\` / \`name_\`。

| \`task_state\` | 含义 |
|--------------|------|
| \`dormant\` | 已注册，未激活 |
| \`ready\` | 已激活，等待执行 |
| \`running\` | 正在执行 |
| \`waiting\` | 等待依赖、子任务或帧边界 |
| \`completed\` | 本次激活已结束，身份保留 |
| \`disabled\` | 禁止后续激活 |
| \`faulted\` | 任务体报告不可恢复错误 |
| \`cancelled\` | 本次激活被取消 |

### 56.4 注册

| 接口 | 说明 |
|------|------|
| \`declare(name)\` | 注册空任务，返回 \`task_builder\`，由 \`run\` 提交任务体 |
| \`declare(name, task_body)\` | 注册并直接绑定任务体，返回 \`task_id\` |
| \`declare(name, task_body, role)\` | 指定角色注册 |
| \`declare_full(name, body, route_word, parent_word, key, flags)\` | 完整字段注册 |
| \`declare_all(decls, count)\` | 按声明表批量注册，任一非法则全部不生效 |
| \`set_body(id, task_body)\` | 为已注册任务绑定或替换任务体 |

\`task_body\`：

| 字段 | 说明 |
|------|------|
| \`invoke_.plain_\` | \`void (*)(task_context&, void*)\` 任务体 |
| \`invoke_.plain_ctx_\` | \`void (*)(task_context&)\` 任务体 |
| \`invoke_.coro_\` | \`task<void> (*)(task_context&, void*)\` 协程任务体 |
| \`invoke_.coro_ctx_\` | \`task<void> (*)(task_context&)\` 协程任务体 |
| \`object_\` | 传入任务体的用户指针 |
| \`kind_\` | \`task_body_plain\` / \`task_body_plain_ctx\` / \`task_body_coroutine\` / \`task_body_coroutine_ctx\` |

builder 方法：

| 方法 | 作用 |
|------|------|
| \`role(role)\` | 设置角色 |
| \`ordered()\` / \`unordered()\` | 顺序 / 并行 |
| \`pinned(worker)\` | 绑定到指定工作线程，默认不可窃取 |
| \`stealable(bool = true)\` | 声明绑定任务允许同角色窃取 |
| \`priority_of(priority)\` | 设置优先级（仅并行任务） |
| \`order(key)\` | 设置顺序键 |
| \`frames(period)\` | 每 N 帧激活 |
| \`activation_of(policy, overlap)\` | 激活策略与重叠策略 |
| \`detach()\` | 子任务不参与父任务汇合 |
| \`timeout(ns)\` | 设置截止时间 |
| \`after_task(id)\` | 追加前驱依赖 |
| \`then(fn)\` | 追加阶段任务并建立偏序 |
| \`run(body)\` | 应用字段并绑定任务体，返回 \`task_id\` |
| \`commit()\` | 提交阶段链，返回 \`task_chain{head_, tail_}\` |
| \`id()\` | 当前阶段编号 |

管道标签：

| 标签 | 作用 |
|------|------|
| \`on(role)\` | 设置角色 |
| \`ordered\` / \`unordered\` | 顺序 / 并行 |
| \`pinned(worker)\` | 绑定工作线程 |
| \`stealable\` | 绑定任务允许同角色窃取 |
| \`at_priority(p)\` | 设置优先级 |
| \`every_frames(n)\` | 每 N 帧激活 |
| \`once\` / \`on_signal\` | 一次激活 / 信号激活 |
| \`after(id)\` / \`after_any(id)\` | 全部前驱 / 任一前驱完成后就绪 |
| \`deadline(ns)\` | 设置截止时间 |
| \`detached\` | 子任务不参与汇合 |

\`run\` 支持四种任务体指针与带捕获可调用对象：

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

声明表：

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

### 56.5 激活与身份变更

| 接口 | 说明 |
|------|------|
| \`activate(id)\` | 按当前身份激活一次 |
| \`activate(id, activate_options)\` | \`queue_\` 排队、\`delay_frames_\` 延迟帧 |
| \`deactivate(id)\` | 撤销尚未执行的激活 |
| \`migrate(id, role)\` / \`rebind(id, role)\` | 改变角色，下一次激活生效 |
| \`pin(id, worker)\` | 绑定工作线程，并清除可窃取声明 |
| \`set_stealable(id, true/false)\` | 仅对绑定并行任务有效 |
| \`set_order_mode(id, mode)\` | 切换顺序 / 并行 |
| \`set_priority(id, p)\` | 顺序任务拒绝 |
| \`reorder(id, order_key)\` | 顺序域内调整相对位置 |
| \`reparent(id, parent)\` | 变更父任务，禁止成环 |
| \`disable(id)\` / \`enable(id)\` | 禁止 / 恢复后续激活 |
| \`cancel(scope)\` / \`clear_cancel(scope)\` / \`cancelled(id)\` | 取消作用域 |
| \`set_deadline(id, ns)\` | 设置截止时间 |

\`activate_options\`：\`queue_\`（重叠激活排队）、\`delay_frames_\`（延迟帧数）。

失败一律返回 \`false\`，可恢复错误不终止进程。

### 56.6 执行、帧与外部线程

| 接口 | 说明 |
|------|------|
| \`pump_role(role, budget)\` | 在当前线程执行该角色最多 \`budget\` 个就绪任务 |
| \`begin_frame(role)\` | 帧号 +1，激活本帧到期的周期任务 |
| \`end_frame(role)\` | 帧纪元 +1 |
| \`frame_of(role)\` / \`epoch_of(role)\` | 当前帧号 / 帧纪元 |
| \`attach_thread(role, label = nullptr)\` | 把当前线程绑定到外部角色 |
| \`detach_thread(role)\` | 解除绑定 |
| \`thread_attached(role)\` | 是否已接入 |
| \`has_worker_work(worker)\` | 指定线程是否还有可执行任务 |
| \`drain(limit = 1000000)\` | 在当前线程排空任务，返回执行数 |

### 56.7 错误上报

| 接口 | 说明 |
|------|------|
| \`set_error_sink(error_sink, void* user)\` | 安装上报出口；传 \`nullptr\` 恢复默认输出 |
| \`reporter()\` | \`error_reporter\`：\`reported_\` / \`suppressed_\` / \`sink_faults_\` |
| \`last_error()\` | 最近一次失败：\`code_\` / \`severity_\` / \`site_\` |

\`error_report\`：\`severity_\` / \`code_\` / \`site_\` / \`sequence_\` / \`task_\` / \`role_\` / \`worker_\` / \`message_\`。\`message_\` 只在回调期间有效。

\`\`\`cpp
mgr.set_error_sink(+[](const thread::error_report& report, void* user) noexcept
{
    auto* out = static_cast<queue*>(user);
    (void)out->try_push(report);
}, &out);
\`\`\`

### 56.8 指标快照

\`thread::collect_metrics(manager&)\` 在原有 manager/worker/lane 字段之外，新增：

| 字段 | 说明 |
|------|------|
| \`task.declared\` / \`activations\` / \`completions\` | 任务注册、激活、完成计数 |
| \`task.coalesced\` / \`stale_tickets\` | 重叠合并与过期调度丢弃 |
| \`task.migrations\` / \`forwarded\` / \`dependency_waits\` | 迁移、转发、资源等待 |
| \`task.faults\` / \`cancellations\` / \`overruns\` | 故障、取消、顺序域超时 |
| \`watchdog.*\` | 扫描、超期、卡住、心跳、排队超时、最大等待/执行 |
| \`admission.*\` | 准入、拒绝、降级、丢弃、合并、在途 |
| \`blocking_queue_depth\` / \`blocking_completed\` | 阻塞卸载池 |
| \`trace_dropped\` | 追踪环丢弃 |
| \`spawned_workers\` / \`thread_budget\` | 实际派生线程数 / 默认线程预算 |

### 56.9 manager 集成

\`manager_config\` 增加字段：\`task_capacity\`（\`0\` 关闭任务图）、\`roles\`、\`role_count\`、\`task_local_capacity\`、\`task_command_capacity\`、\`task_inject_capacity\`、\`task_dependency_capacity\`。

\`manager\` 转发全部任务图接口：\`declare\` / \`declare_all\` / \`activate\` / \`deactivate\` / \`migrate\` / \`pin\` / \`set_stealable\` / \`set_order_mode\` / \`set_priority\` / \`reorder\` / \`reparent\` / \`disable\` / \`enable\` / \`cancel\` / \`clear_cancel\` / \`cancelled\` / \`set_deadline\` / \`view\` / \`state_of\` / \`attach_thread\` / \`detach_thread\` / \`pump_role\` / \`begin_frame\` / \`end_frame\` / \`frame_of\` / \`set_error_sink\` / \`last_error\` / \`task_stats\` / \`tasks\`。

### 56.10 示例

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
    if (!mgr.configure(config))
    {
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

### 56.11 协程等待原语

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

| 接口 | 返回 | 说明 |
|------|------|------|
| \`switch_to(role)\` | \`errc\` | 下一次在目标角色恢复；硬绑定任务返回 \`affinity_violation\` 且继续当前线程 |
| \`next_frame(role)\` | \`errc\` | 等待该角色下一个 \`end_frame\` |
| \`frame_sync(a, b)\` | \`errc\` | 等待两个角色帧纪元都前进（角色编号需小于 32） |
| \`join(id)\` | \`errc\` | 激活并等待子任务 |
| \`when_all(ids, errs)\` | \`errc\` | 等待全部子任务，错误写入调用方数组 |
| \`when_any(ids)\` | \`task_id\` | 返回第一个完成的子任务编号 |
| \`parallel_for(begin, end, body)\` | \`errc\` | 按工作项分片并行，父任务挂起等待 |
| \`sleep_for(duration)\` / \`sleep_until(time_point)\` | \`errc\` | 定时等待，到期后恢复 |
| \`co_yield_now()\` | — | 让出当前时间片并重新入队 |

等待原语只能在任务体内使用；在任务体外调用返回 \`invariant_broken\`。

### 56.12 类型化结果 task_result

| 成员 | 说明 |
|------|------|
| \`error_\` | \`errc\`，\`ok\` 表示成功 |
| \`ok()\` | \`error_ == errc::ok\` |
| \`has_value()\` | 是否持有值 |
| \`value()\` | 取值引用 |
| \`take()\` | 移动取值并清空 |
| \`set(value)\` / \`set_error(errc)\` | 写入值或错误 |

\`co_await task<T>\` 返回 \`task_result<T>\`；\`co_await task<void>\` 返回 \`task_result<void>\`。\`join\` 只回传 \`errc\`。

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

### 56.13 上下文传播

| 接口 | 说明 |
|------|------|
| \`create_context(request_id, trace_id, tenant_id)\` | 创建上下文编号 |
| \`set_context(id, ctx)\` / \`with_context(ctx)\` | 为任务绑定上下文 |
| \`context_of(id)\` | 查询任务上下文 |
| \`contexts().request_id(ctx)\` / \`trace_id\` / \`tenant_id\` | 查询字段 |
| \`release_context(ctx)\` | 释放引用 |
| \`ctx.context_\` | 任务体内读取上下文编号 |
| \`thread::current_context_id()\` | 线程内查询 |

子任务默认继承父任务的上下文；上下文容量由 \`context_capacity\` 配置。

### 56.14 资源依赖

| 接口 / 标签 | 说明 |
|-------------|------|
| \`resource_id{value}\` | 资源编号 |
| \`access_mode::read\` / \`write\` | 访问模式 |
| \`| thread::reads(id)\` | 声明读访问 |
| \`| thread::writes(id)\` | 声明写访问 |
| \`add_access(id, resource, mode)\` | 注册后追加访问声明 |

读-读可并行；读-写与写-写互斥；写者优先于新读者。单个任务最多声明 4 个读与 4 个写。

### 56.15 看门狗与取消

| 配置 / 接口 | 默认值 | 说明 |
|-------------|--------|------|
| \`watchdog_enabled\` | \`true\` | 是否启动看门狗线程 |
| \`watchdog_period_ms\` | \`100\` | 扫描周期 |
| \`default_deadline_ns\` | \`0\` | 全局默认截止时间；\`0\` 关闭 |
| \`hang_grace_ns\` | \`1s\` | 超期后判定卡住的宽限期 |
| \`heartbeat_timeout_ns\` | \`0\` | 心跳超时；\`0\` 关闭 |
| \`queue_timeout_ns\` | \`0\` | 排队超时；\`0\` 关闭 |

| 接口 | 说明 |
|------|------|
| \`set_deadline(id, ns)\` | 单任务截止时间 |
| \`cancel(scope)\` / \`clear_cancel(scope)\` / \`cancelled(id)\` | 取消作用域 |
| \`ctx.cancelled()\` | 任务体内协作检查 |
| \`ctx.heartbeat()\` | 上报心跳 |
| \`ctx.budget_exceeded()\` | 本次激活是否超过时间预算 |
| \`set_budget(id, ns)\` / \`budget_of(id)\` | 设置 / 查询任务时间预算 |
| \`ctx.fail()\` | 报告不可恢复错误，本次激活置 \`faulted\` |
| \`watchdog_stats()\` | \`deadline_misses_\` / \`hung_tasks_\` / \`max_wait_ns_\` 等 |
| \`watchdog_scan(now_ns)\` | 手动扫描一次 |

任务需在安全点轮询 \`ctx.cancelled()\`。取消作用域会同时撤销该任务在途的 IO 请求，挂起中的 \`read_some/write_all\` 以 \`cancelled\` 返回。

### 56.16 异步 IO

| 类型 / 接口 | 说明 |
|-------------|------|
| \`io_result\` | \`bytes_\` / \`status_\` / \`error_\` / \`ok()\` / \`would_block()\` |
| \`io_status\` | \`ok\` / \`eof\` / \`would_block\` / \`partial\` / \`error\` / \`cancelled\` |
| \`data_source\` | \`read_\` / \`write_\` / \`close_\` 三个函数指针 + \`self_\` |
| \`io_backend\` | \`submit_\` / \`cancel_\` / \`poll_\` 三个函数指针 + \`self_\` |
| \`io_domain\` | \`create(graph, backend, capacity)\` / \`start()\` / \`stop()\` / \`destroy()\` |
| \`read_some(domain, source, buffer)\` | \`co_await\` 返回 \`io_result\` |
| \`write_all(domain, source, buffer)\` | \`co_await\` 返回 \`io_result\`，短写自动推进 |
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

文件、内存与 socket 访问通过 \`data_source\` / \`io_backend\` 适配器提供。

### 56.17 阻塞卸载

| 接口 | 说明 |
|------|------|
| \`manager_config::blocking_threads\` | 阻塞线程数；\`0\` 关闭 |
| \`manager_config::blocking_capacity\` | 队列容量 |
| \`blocking_call(domain, fn, arg)\` | \`co_await\` 返回 \`errc\` |
| \`blocking_domain::queue_depth()\` | 队列深度 |
| \`blocking_domain::completed_\` / \`rejected_\` | 计数 |

用于数据库、文件、DNS 等阻塞调用；队列满返回 \`queue_full\`。

### 56.18 准入控制

| 字段 / 接口 | 说明 |
|-------------|------|
| \`admission_config::max_inflight_\` | 在途上限；\`0\` 关闭 |
| \`admission_config::tenant_slots_\` | 租户槽数 |
| \`admission_config::policy_\` | \`reject\`（拒绝）/ \`drop_oldest\`（丢弃计数）/ \`coalesce\`（合并计数）/ \`degrade\`（仍准入并计数） |
| \`try_admit(tenant)\` | 是否准入 |
| \`on_complete(tenant)\` | 释放配额 |
| \`admission().metrics_\` | \`admitted_\` / \`rejected_\` / \`degraded_\` / \`dropped_\` / \`coalesced_\` / \`completed_\` |

manager 配置项：\`admission_max_inflight\` / \`admission_tenant_slots\` / \`admission_policy\`。

### 56.19 逐任务追踪

| 接口 | 说明 |
|------|------|
| \`manager_config::trace_enabled\` | 是否开启 |
| \`manager_config::trace_capacity\` | 每个工作线程的事件缓冲容量 |
| \`trace_recorder::drain(worker, sink, user)\` | 取走事件 |
| \`trace_recorder::dropped_\` | 缓冲满丢弃计数 |
| \`trace_event\` | \`timestamp_ns_\` / \`duration_ns_\` / \`task_\` / \`worker_\` / \`role_\` / \`kind_\` |

\`trace_kind\`：\`enqueue\` / \`start\` / \`end\` / \`wait_begin\` / \`wait_end\` / \`steal\` / \`migrate\` / \`cancel\` / \`io_submit\` / \`io_complete\`。

### 56.20 多 manager 消息

| 接口 | 说明 |
|------|------|
| \`manager_group::create(managers, count, capacity)\` | 注册多个 manager |
| \`post(manager_id, message)\` | 非阻塞投递 |
| \`poll(manager_id, budget)\` | 消费并转为激活/取消 |
| \`poll_all(budget)\` | 消费全部入站队列 |
| \`group_message\` | \`kind_\`（\`activate\` / \`deactivate\` / \`cancel\`）+ \`task_\` + \`source_manager_\` |

跨 manager 只传消息，不迁移任务；任务编号在各自 manager 内唯一。

### 56.21 时间轮

\`timing_wheel\` 为通用容器，不依赖 thread 模块：

| 接口 | 说明 |
|------|------|
| \`create(capacity, callback, owner, now_ns)\` | 建轮 |
| \`add(node, delay_ns)\` | 加入定时器 |
| \`cancel(node)\` | 取消 |
| \`advance(now_ns)\` | 推进并触发，返回触发数 |
| \`size()\` / \`capacity()\` | 当前与上限 |

\`timer_node\` 由调用方持有，并在创建时间轮的线程内使用。

### 56.22 热重载

| 接口 | 说明 |
|------|------|
| \`replace_body(id, task_body)\` | 替换下一次激活使用的任务体 |
| \`body_version(id)\` | 当前版本号，\`0\` 表示未替换 |

替换后下一次激活使用新任务体；版本号可用 \`body_version\` 查询。单任务版本数受 \`body_version_capacity\`（默认 8）限制，超限返回 \`false\`。

### 56.23 确定性模式

\`role_config::deterministic_ = true\` 后：工作线程按 \`task_id % worker_count\` 固定映射，回放结果可重复。用于回放与帧同步角色。

### 56.24 阶段组合 stage

\`\`\`cpp
auto pipeline = thread::stage(load) | thread::stage(parse) | thread::stage(store);
const thread::task_id io = mgr.declare("io.pipeline")
    | thread::on(thread::role::general)
    | thread::unordered
    | thread::run(pipeline);
\`\`\`

| 接口 | 说明 |
|------|------|
| \`stage(fn)\` | 包装一个普通可调用对象，签名为 \`fn(task_context&)\` |
| \`stage_a \| stage_b \| ...\` | 组合为顺序流水线 |
| \`run(pipeline)\` | 提交组合，每次激活顺序调用全部阶段 |

### 56.25 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 把任务当一次性工作项 | 任务编号不复用，容量有限 | 数据并行叶子用 \`job\` / 工作项 |
| 对顺序任务调用 \`set_priority\` | 顺序域不支持优先级 | 只用并行任务设置优先级 |
| 在通用角色上声明 \`ordered\` | 通用角色不提供顺序执行 | 为顺序语义单独声明顺序角色 |
| 保存 \`task_context\` 引用到下一次激活 | 上下文只在单次激活有效 | 长期状态放 \`ctx.user_\` 指向的对象 |
| 期望 \`disable\` 立刻终止正在运行的任务 | 只阻止后续激活 | 任务体内轮询 \`ctx.cancelled()\` |
| 绑定任务依赖线程局部资源后又声明 \`stealable\` | 可能被同角色其他线程执行 | 需要线程独占时不要声明可窃取 |
| 跨角色共享可变数据不加同步 | 框架不对用户数据加锁 | 用依赖或帧边界建立先后关系 |
| 运行期按事件无限注册任务 | 任务容量有上限 | 预注册固定数量执行器任务 |
`
};

window.DOCS_DATA['thread_app'] = {
  id: 'thread_app',
  title: "thread::app — 开箱即用应用骨架",
  category: 'thread',
  icon: 'A',
  order: 57,
  content: `## 57. thread::app — 开箱即用应用骨架

应用骨架把线程配置、任务注册、帧循环与关闭流程收敛到一处：默认布局下只需三行即可运行，需要更细控制时再下沉到 47–56 章的接口。

### 57.1 配置 app_config

| 字段 | 默认值 | 说明 |
|------|--------|------|
| \`manager\` | \`profile::server(thread_budget())\` | 线程与容量配置；可整体替换 |
| \`frame_rate\` | \`60\` | 帧率；\`0\` 表示不限速 |

未指定 \`manager.roles\` 时使用默认布局：单 \`general\` 并行域覆盖全部工作线程，主线程只驱动帧。自定义角色表必须包含 \`general\`。

### 57.2 生命周期

| 接口 | 说明 |
|------|------|
| \`app(app_config = {})\` | 配置并启动；失败时 \`valid()\` 为 \`false\`，析构安全 |
| \`valid()\` | 是否配置成功 |
| \`running()\` | 是否已启动且未请求退出 |
| \`run()\` | 阻塞运行帧循环，\`quit()\` 后返回；成功返回 \`0\` |
| \`run_for(duration)\` | 运行指定时长后返回 |
| \`tick()\` | 执行一帧，便于嵌入既有主循环 |
| \`quit()\` | 请求退出，可从任务内或其他线程调用 |
| \`stop()\` | 停止并排空；析构自动调用 |
| \`set_frame_rate(rate)\` | 调整帧率，\`0\` 为不限速 |

### 57.3 注册任务与工作项

| 接口 | 说明 |
|------|------|
| \`every_frame(name, fn)\` | 每帧激活一次，返回 \`task_handle\` |
| \`parallel(name, fn)\` | 每帧激活，语义同 \`every_frame\`；用于强调并行域 |
| \`once(name, fn)\` | 注册后立即激活一次 |
| \`post(fn)\` | 提交一次性工作项，不建立任务身份；队列满返回 \`false\` |
| \`spawn(task<void>&&)\` | 提交协程工作项 |
| \`every_frame(name, method, object)\` | 绑定成员函数，接受 \`void()\` 与 \`void(task_context&)\` |

任务体支持四种形态：

| 形态 | 示例 |
|------|------|
| 函数指针 | \`void update(thread::task_context&) noexcept\` |
| 带上下文 lambda | \`[](thread::task_context& ctx) noexcept { ... }\` |
| 无参 lambda | \`[]() noexcept { ... }\` |
| 成员函数 | \`app.every_frame("x", &System::update, &system)\` |

阶段组合可直接作为任务体：

\`\`\`cpp
auto pipeline = thread::stage(load) | thread::stage(parse) | thread::stage(store);
app.every_frame("io.pipeline", pipeline);
\`\`\`

### 57.4 查询与下沉

| 接口 | 说明 |
|------|------|
| \`state_of(handle)\` / \`state_of(task_id)\` | 任务状态 |
| \`stats()\` | \`task_metrics\` 计数快照 |
| \`last_error()\` | 最近一次失败：\`code_\` / \`severity_\` / \`site_\` |
| \`phase()\` | 关闭阶段 |
| \`frame_index()\` | 当前帧号 |
| \`main_role()\` | 默认角色 |
| \`worker_count()\` | 工作线程数量 |
| \`kernel()\` | 底层 \`manager\`，用于调用 47–56 章的接口 |

### 57.5 task_handle

| 接口 | 说明 |
|------|------|
| \`valid()\` | 句柄是否有效 |
| \`id()\` | 任务编号，可直接传给内核接口 |
| \`state()\` | 当前状态 |
| \`after(prev)\` | 登记依赖：\`prev\` 完成后才激活 |
| \`priority(p)\` | 设置优先级（并行任务） |
| \`timeout(ns)\` | 设置截止时间 |
| \`on(role)\` | 改变角色，下一次激活生效 |
| \`activate()\` / \`disable()\` / \`enable()\` | 激活 / 禁止 / 恢复 |

### 57.6 示例

三行入口：

\`\`\`cpp

void update(thread::task_context&) noexcept { /* 每帧逻辑 */ }
void physics(thread::task_context&) noexcept { /* 并行逻辑 */ }

int main()
{
    thread::app app;
    app.every_frame("game.update", update);
    app.parallel("physics.step", physics);
    return app.run();
}
\`\`\`

成员函数、依赖与退出：

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

单步驱动（嵌入既有循环）：

\`\`\`cpp
thread::app app;
app.every_frame("update", update);

while (running)
{
    poll_platform_events();
    app.tick();
}
\`\`\`

### 57.7 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 配置失败仍继续使用 | 任务注册返回无效句柄 | 先检查 \`valid()\` |
| 在任务内长时间阻塞 | 占用工作线程 | 用 \`co_await thread::sleep_for\` 或拆分为工作项 |
| 用 \`post\` 承载需要查询或依赖的任务 | 工作项没有任务身份 | 用 \`every_frame\` / \`once\` 注册任务 |
| 自定义角色表缺少 \`general\` | 构造后 \`valid()\` 为 \`false\` | 至少声明 \`general\`，或交给默认布局 |
| 多个线程同时调用 \`tick()\` | 帧号与周期激活错乱 | 只由一个线程驱动帧 |
`
};
