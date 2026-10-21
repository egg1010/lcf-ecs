window.DOCS_DATA = window.DOCS_DATA || {};

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
