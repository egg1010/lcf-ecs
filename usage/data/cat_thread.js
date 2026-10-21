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
| \`drain(spin_limit = 0)\` | 等待已提交任务完成 |
| \`take_job(slot_index&)\` / \`slot_of(slot_index)\` | 取用 / 查询任务槽 |
| \`release_job_local(job*)\` | 释放任务到本管理器槽池 |

### 47.4 通道与统计

| 接口 | 说明 |
|------|------|
| \`create_lane(lane_config)\` | 创建通道，返回 \`lane*\` |
| \`lane_at(index)\` / \`lane_count()\` | 查询通道 |
| \`worker_count()\` / \`worker_at(index)\` | 查询工作线程（含外部角色） |
| \`spawned_worker_count()\` | 实际创建的工作线程数（预算扣除外部角色） |
| \`inject_queue()\` | 全局注入队列 |
| \`stats()\` | \`manager_stats\`：\`submitted_\` / \`rejected_\` / \`wakes_\` |
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

mgr.drain();
mgr.stop();
\`\`\`

### 47.6 不要做什么

| 错误做法 | 问题 | 正确做法 |
|---------|------|---------|
| 未 \`configure\` 直接 \`start\` | 无工作线程与队列 | 先 \`configure\` 再 \`start\` |
| 提交后立即 \`stop\` | 任务可能未执行 | 先 \`drain\` 再 \`stop\` |
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
