window.DOCS_DATA = window.DOCS_DATA || {};

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
