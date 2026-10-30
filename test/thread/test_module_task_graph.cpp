// test_module_task_graph.cpp - 模块功能测试: 持久任务图
#include "test_common.hpp"
#include "include/thread/manager.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

namespace
{
struct probe
{
    std::atomic<int> total{0};
    std::atomic<int> worker{0x7FFFFFFF};
    std::atomic<int> role{0x7FFFFFFF};
    std::atomic<int> activation{0};
    std::atomic<int> order[8]{};
    std::atomic<int> cursor{0};
};

std::atomic<int> g_sink_count{0};
std::atomic<int> g_sink_code{0};

void test_sink(const operating_message& report, void*) noexcept
{
    g_sink_count.fetch_add(1, std::memory_order_relaxed);
    g_sink_code.store(static_cast<int>(report.code()), std::memory_order_relaxed);
}

std::atomic<int> g_completion_count{0};
std::atomic<unsigned> g_completion_codes[4] = {};
std::atomic<unsigned> g_completion_lines[4] = {};

void completion_sink(operating_message message, void*) noexcept
{
    const int index = g_completion_count.fetch_add(1, std::memory_order_relaxed);
    if (index >= 0 && index < 4)
    {
        g_completion_codes[index].store(message.code(), std::memory_order_relaxed);
        g_completion_lines[index].store(message.code(), std::memory_order_relaxed);
    }
}

void record(probe& p) noexcept
{
    p.total.fetch_add(1, std::memory_order_relaxed);
}

inline thread::task_builder builder_of(thread::task_graph& graph, const char* name) noexcept
{
    return thread::task_builder{&graph, graph.declare(name, thread::task_body{})};
}
} // namespace


namespace
{
std::atomic<int> g_switch_hits{0};
std::atomic<int> g_frame_hits{0};
std::atomic<int> g_join_child{0};
std::atomic<int> g_join_parent{0};
std::atomic<long long> g_parallel_sum{0};
std::atomic<int> g_parallel_done{0};
std::atomic<int> g_inside{0};
std::atomic<int> g_max_inside{0};
std::atomic<int> g_ctx_request{0};
std::atomic<int> g_ctx_trace{0};
std::atomic<int> g_sleep_done{0};
std::atomic<int> g_fault_seen{0};
std::atomic<int> g_join_fault_handled{0};
std::atomic<int> g_io_done{0};
std::atomic<int> g_blocking_done{0};
std::atomic<int> g_det_done{0};
std::atomic<int> g_group_done{0};
std::atomic<int> g_any_done{0};
std::atomic<thread::task_id> g_any_first{thread::invalid_task};
std::atomic<int> g_body_a{0};
std::atomic<int> g_body_b{0};
std::atomic<int> g_det_worker{0x7FFFFFFF};
std::atomic<int> g_timeout_seen{0};
std::atomic<int> g_det_workers[4] = {-1, -1, -1, -1};

thread::task<void> switch_body(thread::task_context& ctx)
{
    const uint32_t r = co_await thread::switch_to(thread::make_role(1));
    if (r == 0 && ctx.role_ == thread::make_role(1))
    {
        g_switch_hits.fetch_add(1);
    }
    co_return;
}

thread::task<void> frame_body(thread::task_context&)
{
    g_frame_hits.fetch_add(1);
    const uint32_t r = co_await thread::next_frame(thread::role::general);
    if (r == 0)
    {
        g_frame_hits.fetch_add(1);
    }
    co_return;
}

thread::task<void> child_body(thread::task_context&)
{
    g_join_child.fetch_add(1);
    co_return;
}

thread::task<void> parent_body(thread::task_context&)
{
    co_await thread::join(thread::task_id{0});
    g_join_parent.fetch_add(1);
    co_return;
}

thread::task<void> join_fault_parent_body(thread::task_context&)
{
    const uint32_t result = co_await thread::join(thread::task_id{0});
    if (result != 0)
    {
        g_join_fault_handled.fetch_add(1);
    }
    co_return;
}

thread::task<void> when_body(thread::task_context&)
{
    const thread::task_id ids[2] = {thread::task_id{0}, thread::task_id{1}};
    uint32_t errors[2]{};
    const uint32_t r = co_await thread::when_all(ids, errors);
    if (r == 0 && errors[0] == 0 && errors[1] == 0)
    {
        g_join_parent.fetch_add(1);
    }
    co_return;
}

thread::task<void> parallel_body(thread::task_context&)
{
    const uint32_t r = co_await thread::parallel_for(
        0, 1000, [](std::size_t index) noexcept
        { g_parallel_sum.fetch_add(static_cast<long long>(index)); });
    if (r == 0)
    {
        g_parallel_done.fetch_add(1);
    }
    co_return;
}

void resource_body(thread::task_context& ctx) noexcept
{
    (void)ctx;
    const int now = g_inside.fetch_add(1) + 1;
    int seen = g_max_inside.load();
    while (now > seen && !g_max_inside.compare_exchange_weak(seen, now))
    {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    g_inside.fetch_sub(1);
}

void context_body(thread::task_context& ctx) noexcept
{
    thread::task_graph* graph = ctx.graph_;
    if (graph != nullptr && ctx.context_.valid())
    {
        g_ctx_request.store(static_cast<int>(graph->contexts().request_id(ctx.context_)));
        g_ctx_trace.store(static_cast<int>(graph->contexts().trace_id(ctx.context_)));
    }
}

void fault_body(thread::task_context& ctx) noexcept
{
    ctx.fail();
    g_fault_seen.fetch_add(1);
}

void fault_with_code_body(thread::task_context& ctx) noexcept
{
    ctx.fail(om_err_timeout.code);
}

void ok_body(thread::task_context&) noexcept
{
}

thread::task<void> any_wait_body(thread::task_context&)
{
    co_await thread::next_frame(thread::role::general);
    co_return;
}

thread::task<void> any_parent_body(thread::task_context&)
{
    const thread::task_id ids[2] = {thread::task_id{0}, thread::task_id{1}};
    g_any_first.store(co_await thread::when_any(ids), std::memory_order_relaxed);
    g_any_done.fetch_add(1);
    co_return;
}

thread::task<void> sleep_body(thread::task_context&)
{
    const uint32_t r = co_await thread::sleep_for(std::chrono::milliseconds(5));
    if (r == 0)
    {
        g_sleep_done.fetch_add(1);
    }
    co_return;
}

void body_a(thread::task_context&) noexcept
{
    g_body_a.fetch_add(1);
}

void body_b(thread::task_context&) noexcept
{
    g_body_b.fetch_add(1);
}

void group_body(thread::task_context&) noexcept
{
    g_group_done.fetch_add(1);
}
} // namespace


namespace
{
void timeout_sink(const operating_message& report, void*) noexcept
{
    if (report.code() == om_err_timeout.code)
    {
        g_timeout_seen.fetch_add(1);
    }
}

void slow_body(thread::task_context&) noexcept
{
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
}

void det_record_body(thread::task_context& ctx) noexcept
{
    if (ctx.id_.index_ < 4)
    {
        g_det_workers[ctx.id_.index_].store(static_cast<int>(ctx.worker_), std::memory_order_relaxed);
    }
    g_det_done.fetch_add(1);
}

struct memory_backend_state
{
    thread::mpmc_ring<thread::io_event>* queue_{nullptr};
};

bool io_submit(void* self, thread::io_request* request) noexcept
{
    auto* state = static_cast<memory_backend_state*>(self);
    thread::io_result result{};
    if (request->write_)
    {
        result = request->source_->write(
            std::span<const std::byte>(request->buffer_, request->capacity_));
    }
    else
    {
        result = request->source_->read(std::span<std::byte>(request->buffer_, request->capacity_));
    }
    thread::io_event event{request, result};
    return state->queue_->try_push(event);
}

void io_cancel(void*, thread::io_request*) noexcept
{
}

int io_poll(void* self, thread::io_event* out, int max_events, int) noexcept
{
    auto* state = static_cast<memory_backend_state*>(self);
    int count = 0;
    while (count < max_events && state->queue_->try_pop(out[count]))
    {
        ++count;
    }
    return count;
}

std::uint8_t g_io_buffer[8] = {1, 2, 3, 4, 5, 6, 7, 8};

thread::io_result io_read(void* self, std::span<std::byte> buffer) noexcept
{
    const auto* source = static_cast<const std::uint8_t*>(self);
    for (std::size_t i = 0; i < buffer.size(); ++i)
    {
        buffer[i] = static_cast<std::byte>(source[i]);
    }
    return thread::io_result{static_cast<std::ptrdiff_t>(buffer.size()), thread::io_status::ok,
                             0};
}

thread::io_result io_write(void* self, std::span<const std::byte> buffer) noexcept
{
    auto* target = static_cast<std::uint8_t*>(self);
    for (std::size_t i = 0; i < buffer.size(); ++i)
    {
        target[i] = static_cast<std::uint8_t>(buffer[i]);
    }
    return thread::io_result{static_cast<std::ptrdiff_t>(buffer.size()), thread::io_status::ok,
                             0};
}

thread::io_domain* g_io_domain = nullptr;
thread::data_source g_io_source{};

thread::task<void> io_body(thread::task_context&)
{
    std::byte buffer[4]{};
    const thread::io_result read =
        co_await thread::read_some(*g_io_domain, g_io_source, std::span<std::byte>(buffer, 4));
    std::byte out[2]{std::byte{9}, std::byte{7}};
    const thread::io_result written = co_await thread::write_all(
        *g_io_domain, g_io_source, std::span<const std::byte>(out, 2));
    if (read.ok() && read.bytes_ == 4 && written.ok() && written.bytes_ == 2)
    {
        g_io_done.fetch_add(1);
    }
    co_return;
}

void blocking_work(void* arg) noexcept
{
    *static_cast<int*>(arg) += 41;
}

std::atomic<int> g_trace_events{0};

void trace_count_sink(const thread::trace_event*, std::size_t count, void*) noexcept
{
    g_trace_events.fetch_add(static_cast<int>(count), std::memory_order_relaxed);
}

std::atomic<int> g_wheel_fired{0};

void wheel_callback(void*, timer_node*) noexcept
{
    g_wheel_fired.fetch_add(1, std::memory_order_relaxed);
}

thread::blocking_domain* g_blocking_domain = nullptr;

thread::task<void> blocking_body(thread::task_context&)
{
    int value = 1;
    const uint32_t r = co_await thread::blocking_call(*g_blocking_domain, &blocking_work, &value);
    if (r == 0 && value == 42)
    {
        g_blocking_done.fetch_add(1);
    }
    co_return;
}
} // namespace


namespace
{
std::atomic<int> g_trace_kinds[16]{};

void trace_kind_sink(const thread::trace_event* events, std::size_t count, void*) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t kind = static_cast<std::size_t>(events[i].kind_);
        if (kind < 16)
        {
            g_trace_kinds[kind].fetch_add(1, std::memory_order_relaxed);
        }
    }
}

std::atomic<int> g_budget_hits{0};

void budget_body(thread::task_context& ctx) noexcept
{
    const auto start = std::chrono::steady_clock::now();
    while (!ctx.budget_exceeded())
    {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(50))
        {
            break;
        }
        std::this_thread::yield();
    }
    if (ctx.budget_exceeded())
    {
        g_budget_hits.fetch_add(1, std::memory_order_relaxed);
    }
}

bool never_submit(void*, thread::io_request*) noexcept
{
    return true;
}

void never_cancel(void*, thread::io_request*) noexcept
{
}

int never_poll(void*, thread::io_event*, int, int) noexcept
{
    return 0;
}

thread::io_result never_read(void*, std::span<std::byte> buffer) noexcept
{
    return thread::io_result{static_cast<std::ptrdiff_t>(buffer.size()), thread::io_status::ok,
                             0};
}

thread::io_domain* g_never_domain = nullptr;
thread::data_source g_never_source{};
std::atomic<int> g_cancel_io_done{0};

thread::task<void> cancel_io_body(thread::task_context&)
{
    std::byte buffer[8]{};
    const thread::io_result result =
        co_await thread::read_some(*g_never_domain, g_never_source, std::span<std::byte>(buffer, 8));
    if (result.error_ == om_err_cancelled.code || result.status_ == thread::io_status::cancelled)
    {
        g_cancel_io_done.fetch_add(1, std::memory_order_relaxed);
    }
    co_return;
}
} // namespace

namespace
{
std::atomic<int> g_route_spawned{0};
std::atomic<int> g_route_parallel{0};
std::atomic<int> g_route_child{0};
std::atomic<int> g_route_flow_done{0};
thread::task_id g_route_child_task{};

thread::task<void> route_spawn_body()
{
    g_route_spawned.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

thread::task<void> route_child_body(thread::task_context&)
{
    g_route_child.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

thread::task<void> route_flow_body(thread::task_context&)
{
    const uint32_t r = co_await thread::parallel_for(
        0, 4096, [](std::size_t) noexcept { g_route_parallel.fetch_add(1, std::memory_order_relaxed); });
    if (r == 0)
    {
        (void)co_await thread::join(g_route_child_task);
        (void)co_await thread::sleep_for(std::chrono::milliseconds(1));
        g_route_flow_done.fetch_add(1, std::memory_order_relaxed);
    }
    co_return;
}
} // namespace

int main()
{
    t_begin("test_module_task_graph — 模块功能测试: 持久任务图");

    // ------------------------------------------------------------------
    t_section(1, "注册与身份稳定");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        t_check("创建任务图", graph.create(config));
        t_check("默认角色数 1", graph.role_count() == 1);
        t_check("工作线程数 2", graph.worker_count() == 2);
        t_check("任务容量 32", graph.task_capacity() == 32);

        probe p{};
        auto body = [&p](thread::task_context& ctx) noexcept
        {
            record(p);
            p.worker.store(static_cast<int>(ctx.worker_), std::memory_order_relaxed);
            p.role.store(static_cast<int>(ctx.role_), std::memory_order_relaxed);
            p.activation.store(static_cast<int>(ctx.activation_), std::memory_order_relaxed);
        };

        const thread::task_id alpha =
            builder_of(graph, "alpha") | thread::on(thread::role::general) | thread::run(body);
        t_check("编号有效且从 0 开始", alpha.valid() && alpha.index_ == 0);
        t_check("注册后为休眠", graph.state_of(alpha) == thread::task_state::dormant);

        thread::task_view view = graph.view(alpha);
        t_check("名称截断保留", std::strcmp(view.name_, "alpha") == 0);
        t_check("未绑定默认可窃取", view.stealable_);
        t_check("默认亲和为 none", view.affinity_ == thread::affinity_kind::none);

        t_check("激活成功", graph.activate(alpha));
        t_check("排空执行", graph.drain(64) >= 1);
        t_check("执行一次", p.total.load() == 1);
        t_check("状态完成", graph.state_of(alpha) == thread::task_state::completed);
        t_check("编号不变", graph.view(alpha).id_ == alpha);
        t_check("激活计数为 1", graph.view(alpha).activations_ == 1);
        t_check("执行在角色 0", p.role.load() == 0);

        t_check("再次激活可重复执行", graph.activate(alpha) && graph.drain(64) >= 1);
        t_check("执行两次", p.total.load() == 2);
        t_check("第 2 次激活序号", p.activation.load() == 2);
    }

    // ------------------------------------------------------------------
    t_section(2, "容量耗尽与错误对象");
    {
        g_sink_count.store(0);
        g_sink_code.store(0);
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 4;
        t_check("创建任务图", graph.create(config));
        graph.set_error_sink(&test_sink, nullptr);

        int created = 0;
        for (int i = 0; i < 4; ++i)
        {
            const thread::task_id id = graph.declare("cap", thread::task_body{});
            created += id.valid() ? 1 : 0;
        }
        t_check("容量内全部注册", created == 4);

        const thread::task_id overflow = graph.declare("cap.overflow", thread::task_body{});
        t_check("超容量返回 invalid_task", !overflow.valid());
        const thread::task_id overflow_again = graph.declare("cap.again", thread::task_body{});
        t_check("超容量再次返回 invalid_task", !overflow_again.valid());
        t_check("注册计数不变", graph.task_count() == 4);
    }

    // ------------------------------------------------------------------
    t_section(3, "顺序域全序");
    {
        thread::role_config roles[] =
        {
            {"general", 0, 1, thread::order_mode::parallel},
            {"seq", 1, 1, thread::order_mode::sequential},
        };
        const thread::role seq_role = thread::make_role(1);

        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        config.roles = roles;
        config.role_count = 2;
        t_check("创建两角色任务图", graph.create(config));
        t_check("顺序角色模式", graph.role_mode(seq_role) == thread::order_mode::sequential);

        probe p{};
        auto make_stage = [&p](int tag)
        {
            return [&p, tag](thread::task_context&) noexcept
            {
                const int slot = p.cursor.fetch_add(1, std::memory_order_relaxed);
                if (slot < 8)
                {
                    p.order[slot].store(tag, std::memory_order_relaxed);
                }
            };
        };

        const thread::task_id a = builder_of(graph, "s.a") | thread::on(seq_role) | thread::ordered | thread::run(make_stage(1));
        const thread::task_id b = builder_of(graph, "s.b") | thread::on(seq_role) | thread::ordered | thread::run(make_stage(2));
        const thread::task_id c = builder_of(graph, "s.c") | thread::on(seq_role) | thread::ordered | thread::run(make_stage(3));
        t_check("三个阶段有效", a.valid() && b.valid() && c.valid());

        t_check("激活 a", graph.activate(a));
        t_check("激活 b", graph.activate(b));
        t_check("激活 c", graph.activate(c));
        (void)graph.drain(256);
        t_check("执行顺序为注册顺序",
                p.order[0].load() == 1 && p.order[1].load() == 2 && p.order[2].load() == 3);
        t_check("顺序域累计执行 3 次", p.cursor.load() == 3);
    }

    // ------------------------------------------------------------------
    t_section(4, "顺序键重排序");
    {
        thread::role_config roles[] =
        {
            {"general", 0, 1, thread::order_mode::parallel},
            {"seq", 1, 1, thread::order_mode::sequential},
        };
        const thread::role seq_role = thread::make_role(1);

        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        config.roles = roles;
        config.role_count = 2;
        t_check("创建任务图", graph.create(config));

        probe p{};
        auto make_stage = [&p](int tag)
        {
            return [&p, tag](thread::task_context&) noexcept
            {
                const int slot = p.cursor.fetch_add(1, std::memory_order_relaxed);
                if (slot < 8)
                {
                    p.order[slot].store(tag, std::memory_order_relaxed);
                }
            };
        };

        const thread::task_id a = builder_of(graph, "r.a") | thread::on(seq_role) | thread::ordered | thread::run(make_stage(1));
        const thread::task_id b = builder_of(graph, "r.b") | thread::on(seq_role) | thread::ordered | thread::run(make_stage(2));
        t_check("重排序成功", graph.reorder(a, thread::order_key{6ull << 32}));
        t_check("重排后键值更新", graph.view(a).key_.value_ == (6ull << 32));

        t_check("激活 a", graph.activate(a));
        t_check("激活 b", graph.activate(b));
        (void)graph.drain(256);
        t_check("b 先于 a 执行", p.order[0].load() == 2 && p.order[1].load() == 1);
    }

    // ------------------------------------------------------------------
    t_section(5, "迁移与票据转发");
    {
        thread::role_config roles[] =
        {
            {"general", 0, 2, thread::order_mode::parallel},
            {"", 0, 0, thread::order_mode::parallel},
            {"logic", 2, 1, thread::order_mode::sequential},
        };
        roles[1].first_worker_ = 0;
        roles[1].worker_count_ = 0;
        roles[1].mode_ = thread::order_mode::parallel;

        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 3;
        config.task_capacity = 32;
        config.roles = roles;
        config.role_count = 3;
        // 角色 1 留空会与角色 2 冲突，改为两角色拓扑
        t_check("非法角色划分被拒绝", !graph.create(config));

        thread::role_config valid_roles[] =
        {
            {"general", 0, 2, thread::order_mode::parallel},
            {"logic", 2, 1, thread::order_mode::sequential},
        };
        thread::task_graph graph2;
        thread::task_graph_config config2{};
        config2.worker_count = 3;
        config2.task_capacity = 32;
        config2.roles = valid_roles;
        config2.role_count = 2;
        t_check("创建三角色任务图", graph2.create(config2));
        const thread::role logic_role = thread::make_role(1);

        probe p{};
        auto body = [&p](thread::task_context& ctx) noexcept
        {
            record(p);
            p.role.store(static_cast<int>(ctx.role_), std::memory_order_relaxed);
            p.worker.store(static_cast<int>(ctx.worker_), std::memory_order_relaxed);
        };
        const thread::task_id task = builder_of(graph2, "m.task") | thread::on(thread::role::general) | thread::run(body);
        t_check("激活到通用域", graph2.activate(task));
        t_check("迁移到顺序角色", graph2.migrate(task, logic_role));
        (void)graph2.drain(256);
        t_check("迁移后只执行一次", p.total.load() == 1);
        t_check("执行角色为 logic", p.role.load() == static_cast<int>(logic_role));
        t_check("执行 worker 为 2", p.worker.load() == 2);
        t_check("发生票据转发", graph2.metrics().forwarded_.load() >= 1);
    }

    // ------------------------------------------------------------------
    t_section(6, "绑定与窃取声明");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        t_check("创建任务图", graph.create(config));

        probe p{};
        auto body = [&p](thread::task_context& ctx) noexcept
        {
            record(p);
            p.worker.store(static_cast<int>(ctx.worker_), std::memory_order_relaxed);
        };
        const thread::task_id bound = builder_of(graph, "pin.task") | thread::on(thread::role::general) | thread::pinned(1) | thread::run(body);
        t_check("绑定成功", bound.valid());
        t_check("视图亲和为 worker",
                graph.view(bound).affinity_ == thread::affinity_kind::worker);
        t_check("绑定默认不可窃取", !graph.view(bound).stealable_);

        t_check("激活绑定任务", graph.activate(bound));
        (void)graph.drain(64);
        t_check("绑定任务在 worker 1 执行", p.worker.load() == 1);
        t_check("绑定任务执行一次", p.total.load() == 1);

        t_check("声明可窃取", graph.set_stealable(bound, true));
        t_check("视图变为可窃取", graph.view(bound).stealable_);

        const thread::task_id loose = builder_of(graph, "loose.task") | thread::on(thread::role::general) | thread::run(body);
        t_check("未绑定任务拒绝 set_stealable(false)",
                !graph.set_stealable(loose, false));
    }

    // ------------------------------------------------------------------
    t_section(7, "依赖 after");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        t_check("创建任务图", graph.create(config));

        probe p{};
        auto make_stage = [&p](int tag)
        {
            return [&p, tag](thread::task_context&) noexcept
            {
                const int slot = p.cursor.fetch_add(1, std::memory_order_relaxed);
                if (slot < 8)
                {
                    p.order[slot].store(tag, std::memory_order_relaxed);
                }
            };
        };

        const thread::task_id first = builder_of(graph, "dep.first") | thread::on(thread::role::general) | thread::run(make_stage(1));
        const thread::task_id second = builder_of(graph, "dep.second") | thread::on(thread::role::general) | thread::after(first) | thread::run(make_stage(2));
        t_check("依赖声明成功", first.valid() && second.valid());
        t_check("前驱未完成时激活被拒绝", !graph.activate(second));
        t_check("激活前驱", graph.activate(first));
        (void)graph.drain(64);
        t_check("依赖链执行两次", p.cursor.load() == 2);
        t_check("前驱先执行", p.order[0].load() == 1 && p.order[1].load() == 2);
        t_check("后继状态完成", graph.state_of(second) == thread::task_state::completed);
    }

    // ------------------------------------------------------------------
    t_section(8, "父子自动启动与汇合");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        t_check("创建任务图", graph.create(config));

        probe p{};
        auto body = [&p](thread::task_context& ctx) noexcept
        {
            record(p);
            (void)ctx;
        };

        const thread::task_id parent = builder_of(graph, "p.parent") | thread::on(thread::role::general) | thread::run(body);
        thread::task_node* parent_node = graph.node_at(parent);
        t_check("父任务节点有效", parent_node != nullptr);
        parent_node->flags_.fetch_or(thread::flag_auto_start_children | thread::flag_join_children,
                                     std::memory_order_acq_rel);

        thread::route_word route = thread::route_word::make(
            thread::role::general, thread::order_mode::parallel, thread::affinity_kind::none,
            thread::priority::normal, thread::task_worker_none);
        thread::parent_word parent_word =
            thread::parent_word::make(parent.index_, 1, thread::parent_word::flag_join_counted);
        operating_message child_error;
        const thread::task_id child =
            graph.declare_full("p.child", thread::task_body{}, route, parent_word,
                               thread::invalid_order_key, 0);
        graph.set_body(child, thread::task_body{});
        t_check("子任务注册成功", child.valid());

        t_check("激活父任务", graph.activate(parent));
        (void)graph.drain(256);
        t_check("父任务执行一次", p.total.load() == 1);
        t_check("父任务最终完成", graph.state_of(parent) == thread::task_state::completed);
        t_check("子任务完成", graph.state_of(child) == thread::task_state::completed);
    }

    // ------------------------------------------------------------------
    t_section(9, "禁用、启用与取消作用域");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        t_check("创建任务图", graph.create(config));

        probe p{};
        auto body = [&p](thread::task_context&) noexcept { record(p); };
        const thread::task_id task = builder_of(graph, "ctl.task") | thread::on(thread::role::general) | thread::run(body);

        t_check("禁用成功", graph.disable(task));
        t_check("禁用后激活失败", !graph.activate(task));
        t_check("启用成功", graph.enable(task));
        t_check("启用后可激活", graph.activate(task));
        (void)graph.drain(64);
        t_check("启用后执行一次", p.total.load() == 1);

        t_check("取消作用域", graph.cancel(task));
        t_check("取消后激活失败", !graph.activate(task));
        t_check("取消状态可见", graph.cancelled(task));
        t_check("清除取消", graph.clear_cancel(task));
        t_check("清除后可再次激活", graph.activate(task));
        (void)graph.drain(64);
        t_check("再次执行", p.total.load() == 2);
    }

    // ------------------------------------------------------------------
    t_section(10, "声明表 declare_all");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 4;
        t_check("创建任务图", graph.create(config));
        t_check("任务图启用", graph.created());

        const thread::task_decl decls[] =
        {
            {"d.a", thread::role::general, thread::order_mode::parallel, thread::affinity_kind::none,
             thread::task_worker_none, thread::order_key{0}, nullptr, false},
        };
        thread::task_decl valid[1] = {decls[0]};
        t_check("空任务体被拒绝", !thread::declare_all(graph, valid, 1));

        thread::task_decl filled[2] = {decls[0], decls[0]};
        filled[0].name_ = "d.ok0";
        filled[0].body_ = [](thread::task_context&) noexcept {};
        filled[1].name_ = "d.ok1";
        filled[1].body_ = [](thread::task_context&) noexcept {};
        t_check("合法声明表注册成功", thread::declare_all(graph, filled, 2));
        t_check("注册数量为 2", graph.task_count() == 2);

        thread::task_decl bad[1] = {filled[0]};
        bad[0].role_ = thread::make_role(7);
        const std::size_t before = graph.task_count();
        t_check("非法角色声明表被拒绝", !thread::declare_all(graph, bad, 1));
        t_check("拒绝时不追加节点", graph.task_count() == before);

        thread::task_decl many[3] = {filled[0], filled[1], filled[0]};
        many[0].name_ = "d.x0";
        many[1].name_ = "d.x1";
        many[2].name_ = "d.x2";
        t_check("超出容量回滚", !thread::declare_all(graph, many, 3));
    }

    // ------------------------------------------------------------------
    t_section(11, "帧模型与周期激活");
    {
        thread::role_config roles[] =
        {
            {"general", 0, 1, thread::order_mode::parallel},
            {"frame", 1, 1, thread::order_mode::sequential},
        };
        const thread::role frame_role = thread::make_role(1);

        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        config.roles = roles;
        config.role_count = 2;
        t_check("创建任务图", graph.create(config));

        probe p{};
        auto body = [&p](thread::task_context& ctx) noexcept
        {
            record(p);
            (void)ctx;
        };
        const thread::task_id tick = builder_of(graph, "frame.tick") | thread::on(frame_role) | thread::ordered | thread::every_frames(2) | thread::run(body);
        t_check("周期任务注册", tick.valid());

        for (int frame = 0; frame < 4; ++frame)
        {
            (void)graph.begin_frame(frame_role);
            (void)graph.pump_role(frame_role, 16);
            graph.end_frame(frame_role);
        }
        t_check("每 2 帧执行一次", p.total.load() == 2);
        t_check("帧号推进到 4", graph.frame_of(frame_role) == 4);
        t_check("帧纪元推进到 4", graph.epoch_of(frame_role) == 4);
    }

    // ------------------------------------------------------------------
    t_section(12, "外部线程接入");
    {
        thread::role_config roles[] =
        {
            {"general", 1, 1, thread::order_mode::parallel},
            {"renderer", 0, 1, thread::order_mode::sequential},
        };
        roles[1].external_ = true;
        const thread::role render_role = thread::make_role(1);

        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        config.roles = roles;
        config.role_count = 2;
        t_check("创建任务图", graph.create(config));
        t_check("外部角色标记", graph.role_external(render_role));

        t_check("未接入时 pump 返回 0", graph.pump_role(render_role, 8) == 0);
        t_check("接入当前线程", graph.attach_thread(render_role, "render-main"));
        t_check("重复接入失败", !graph.attach_thread(render_role, "render-main"));
        t_check("接入状态可见", graph.thread_attached(render_role));

        probe p{};
        auto body = [&p](thread::task_context&) noexcept { record(p); };
        const thread::task_id draw = builder_of(graph, "render.draw") | thread::on(render_role) | thread::ordered | thread::every_frames(1)
            | thread::run(body);
        t_check("渲染任务注册", draw.valid());

        for (int frame = 0; frame < 3; ++frame)
        {
            (void)graph.begin_frame(render_role);
            (void)graph.pump_role(render_role, 8);
            graph.end_frame(render_role);
        }
        t_check("外部线程执行 3 次", p.total.load() == 3);

        graph.detach_thread(render_role);
        t_check("解除接入", !graph.thread_attached(render_role));
    }

    // ------------------------------------------------------------------
    t_section(13, "并发注册编号唯一");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 512;
        t_check("创建任务图", graph.create(config));

        constexpr int threads = 4;
        constexpr int per_thread = 64;
        thread::task_id ids[threads][per_thread]{};
        std::thread workers[threads];
        for (int t = 0; t < threads; ++t)
        {
            workers[t] = std::thread([&graph, &ids, t]()
            {
                for (int i = 0; i < per_thread; ++i)
                {
                    ids[t][i] = graph.declare("conc", thread::task_body{});
                }
            });
        }
        for (int t = 0; t < threads; ++t)
        {
            workers[t].join();
        }

        bool all_valid = true;
        bool unique = true;
        for (int t = 0; t < threads; ++t)
        {
            for (int i = 0; i < per_thread; ++i)
            {
                if (!ids[t][i].valid())
                {
                    all_valid = false;
                    continue;
                }
                for (int s = 0; s < threads && unique; ++s)
                {
                    for (int j = 0; j < per_thread; ++j)
                    {
                        if ((s != t || j != i) && ids[s][j].index_ == ids[t][i].index_)
                        {
                            unique = false;
                            break;
                        }
                    }
                }
            }
        }
        t_check("全部编号有效", all_valid);
        t_check("编号无重复", unique);
        t_check("注册总数匹配", graph.task_count() == threads * per_thread);
    }

    // ------------------------------------------------------------------
    t_section(14, "注册容量耗尽");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 1;
        t_check("创建任务图", graph.create(config));

        const thread::task_id only = graph.declare("only", thread::task_body{});
        t_check("首个任务注册成功", only.valid());

        const thread::task_id overflow = graph.declare("overflow", thread::task_body{});
        t_check("超容量注册失败", !overflow.valid());
    }

    // ------------------------------------------------------------------
    t_section(15, "manager 四线程端到端");
    {
        thread::role_config roles[] =
        {
            {"general", 2, 2, thread::order_mode::parallel},
            {"renderer", 0, 1, thread::order_mode::sequential},
            {"logic", 1, 1, thread::order_mode::sequential},
        };
        roles[thread::role_index(thread::role::renderer)].external_ = true;
        roles[thread::role_index(thread::role::logic)].frame_auto_ = true;
        roles[thread::role_index(thread::role::logic)].frame_rate_ = 240;

        thread::manager_config config = thread::profile::game(4);
        config.task_capacity = 128;
        config.roles = roles;
        config.role_count = 3;

        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        t_check("任务图启用", mgr.tasks_enabled());
        t_check("工作线程数 4", mgr.worker_count() == 4);
        t_check("接入渲染主线程", mgr.attach_thread(thread::role::renderer, "render-main"));

        probe camera{};
        probe ui{};
        probe logic{};
        probe physics{};
        probe audio{};

        const thread::task_id camera_id = mgr.declare("render.camera")
            | thread::on(thread::role::renderer) | thread::ordered | thread::every_frames(1)
            | thread::run([&camera](thread::task_context&) noexcept { record(camera); });
        const thread::task_id ui_id = mgr.declare("render.ui")
            | thread::on(thread::role::renderer) | thread::ordered | thread::after(camera_id)
            | thread::every_frames(1)
            | thread::run([&ui](thread::task_context&) noexcept { record(ui); });
        const thread::task_id logic_id = mgr.declare("logic.tick")
            | thread::on(thread::role::logic) | thread::ordered | thread::every_frames(1)
            | thread::run([&logic](thread::task_context&) noexcept { record(logic); });
        const thread::task_id physics_id = mgr.declare("physics.step")
            | thread::on(thread::role::general) | thread::unordered
            | thread::at_priority(thread::priority::critical)
            | thread::run([&physics](thread::task_context& ctx) noexcept
                          {
                              record(physics);
                              physics.worker.store(static_cast<int>(ctx.worker_));
                          });
        const thread::task_id audio_id = mgr.declare("audio.mix")
            | thread::on(thread::role::general) | thread::unordered | thread::pinned(3)
            | thread::run([&audio](thread::task_context& ctx) noexcept
                          {
                              record(audio);
                              audio.worker.store(static_cast<int>(ctx.worker_));
                          });

        t_check("渲染任务注册", camera_id.valid() && ui_id.valid());
        t_check("逻辑任务注册", logic_id.valid());
        t_check("通用任务注册", physics_id.valid() && audio_id.valid());

        mgr.start();
        t_check("已启动", mgr.running());
        // 给自动帧角色一个启动窗口，避免与首帧断言竞态
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        for (int frame = 0; frame < 30; ++frame)
        {
            (void)mgr.begin_frame(thread::role::renderer);
            (void)mgr.pump_role(thread::role::renderer, 64);
            mgr.end_frame(thread::role::renderer);
            (void)mgr.activate(physics_id);
            (void)mgr.activate(audio_id);
        }
        mgr.drain(2000000);

        t_check("渲染阶段按帧执行", camera.total.load() == 30);
        t_check("UI 阶段按帧执行", ui.total.load() == 30);
        t_check("逻辑帧自动驱动", logic.total.load() >= 1);
        t_check("通用任务执行", physics.total.load() >= 1);
        t_check("绑定任务执行", audio.total.load() >= 1);
        t_check("绑定任务只在 worker 3", audio.worker.load() == 3);
        t_check("物理任务在通用线程", physics.worker.load() >= 2);

        mgr.stop(thread::stop_mode::drain);
        t_check("已停止", !mgr.running());
        t_check("关闭阶段为 stopped", mgr.phase() == thread::shutdown_phase::stopped);
        mgr.detach_thread(thread::role::renderer);
    }

    // ------------------------------------------------------------------
    t_section(16, "延迟激活与一次性激活");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 16;
        t_check("创建任务图", graph.create(config));

        probe p{};
        auto body = [&p](thread::task_context&) noexcept { record(p); };

        const thread::task_id delayed = graph.declare("late.task", thread::task_body{});
        t_check("延迟激活接受", graph.activate(delayed, thread::activate_options{false, 2}));
        (void)graph.begin_frame(thread::role::general);
        (void)graph.pump_role(thread::role::general, 8);
        t_check("第 1 帧不执行", graph.state_of(delayed) == thread::task_state::dormant);
        (void)graph.begin_frame(thread::role::general);
        (void)graph.pump_role(thread::role::general, 8);
        t_check("第 2 帧转入完成", graph.state_of(delayed) == thread::task_state::completed);

        const thread::task_id one = builder_of(graph, "once.task")
            | thread::on(thread::role::general) | thread::ordered | thread::once | thread::run(body);
        t_check("一次性任务注册即就绪", graph.state_of(one) == thread::task_state::ready);
        (void)graph.pump_role(thread::role::general, 8);
        t_check("一次性任务执行", p.total.load() == 1);
        t_check("一次性任务完成", graph.state_of(one) == thread::task_state::completed);
    }

    // ------------------------------------------------------------------
    t_section(17, "任务节点布局");
    {
        t_check("task_node 128 字节", sizeof(thread::task_node) == 128);
        t_check("task_node 64 字节对齐", alignof(thread::task_node) == 64);
        t_check("ticket 8 字节", sizeof(thread::ticket) == 8);
        t_check("64 位原子无锁", std::atomic<std::uint64_t>::is_always_lock_free);
    }


    // ------------------------------------------------------------------
    t_section(18, "协程跨域 switch_to (F12)");
    {
        thread::role_config roles[] =
        {
            {"general", 0, 1, thread::order_mode::parallel},
            {"seq", 1, 1, thread::order_mode::sequential},
        };
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 16;
        config.roles = roles;
        config.role_count = 2;
        t_check("创建任务图", graph.create(config));

        thread::task_body body{};
        body.kind_ = thread::task_body_coroutine_ctx;
        body.invoke_.coro_ctx_ = &switch_body;
        const thread::task_id id = graph.declare("c.switch", body);
        g_switch_hits.store(0);
        t_check("激活", graph.activate(id));
        (void)graph.drain(256);
        t_check("在目标角色恢复", g_switch_hits.load() == 1);
        t_check("任务完成", graph.state_of(id) == thread::task_state::completed);
    }

    // ------------------------------------------------------------------
    t_section(19, "协程帧等待 next_frame (F13)");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 16;
        t_check("创建任务图", graph.create(config));

        thread::task_body body{};
        body.kind_ = thread::task_body_coroutine_ctx;
        body.invoke_.coro_ctx_ = &frame_body;
        const thread::task_id id = graph.declare("c.frame", body);
        g_frame_hits.store(0);
        t_check("激活", graph.activate(id));
        (void)graph.pump_role(thread::role::general, 8);
        t_check("首段执行一次", g_frame_hits.load() == 1);
        t_check("等待中", graph.state_of(id) == thread::task_state::waiting);
        (void)graph.begin_frame(thread::role::general);
        graph.end_frame(thread::role::general);
        (void)graph.pump_role(thread::role::general, 8);
        t_check("帧边界后恢复", g_frame_hits.load() == 2);
        t_check("任务完成", graph.state_of(id) == thread::task_state::completed);
    }

    // ------------------------------------------------------------------
    t_section(20, "协程 join 与 when_all (F32)");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 16;
        t_check("创建任务图", graph.create(config));

        thread::task_body child{};
        child.kind_ = thread::task_body_coroutine_ctx;
        child.invoke_.coro_ctx_ = &child_body;
        const thread::task_id first = graph.declare("c.child0", child);
        const thread::task_id second = graph.declare("c.child1", child);
        thread::task_body when{};
        when.kind_ = thread::task_body_coroutine_ctx;
        when.invoke_.coro_ctx_ = &when_body;
        const thread::task_id parent = graph.declare("c.when", when);
        (void)second;

        g_join_child.store(0);
        g_join_parent.store(0);
        t_check("激活父任务", graph.activate(parent));
        (void)graph.drain(512);
        t_check("两个子任务各执行一次", g_join_child.load() == 2);
        t_check("父任务恢复一次", g_join_parent.load() == 1);
        t_check("父任务完成", graph.state_of(parent) == thread::task_state::completed);
        (void)first;
    }

    // ------------------------------------------------------------------
    t_section(21, "并行分片 parallel_for");
    {
        thread::manager_config config = thread::profile::server(4);
        config.task_capacity = 16;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        thread::task_body body{};
        body.kind_ = thread::task_body_coroutine_ctx;
        body.invoke_.coro_ctx_ = &parallel_body;
        const thread::task_id id = mgr.declare_task("c.parallel", body);
        g_parallel_sum.store(0);
        g_parallel_done.store(0);
        mgr.start();
        t_check("激活", mgr.activate(id));
        mgr.drain(4000000);
        t_check("分片总和正确", g_parallel_sum.load() == 499500);
        t_check("分片完成一次", g_parallel_done.load() == 1);
        mgr.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(22, "资源依赖 (F37)");
    {
        thread::manager_config config = thread::profile::server(4);
        config.task_capacity = 16;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        thread::task_body body{};
        body.kind_ = thread::task_body_plain_ctx;
        body.invoke_.plain_ctx_ = &resource_body;
        const thread::resource_id res{0};
        const thread::task_id w0 = mgr.declare("r.w0") | thread::writes(res) | thread::run(body);
        const thread::task_id w1 = mgr.declare("r.w1") | thread::writes(res) | thread::run(body);
        g_inside.store(0);
        g_max_inside.store(0);
        mgr.start();
        (void)mgr.activate(w0);
        (void)mgr.activate(w1);
        mgr.drain(4000000);
        t_check("写写串行", g_max_inside.load() == 1);
        t_check("两次都完成", mgr.state_of(w0) == thread::task_state::completed &&
                                  mgr.state_of(w1) == thread::task_state::completed);
        t_check("产生资源等待", mgr.task_stats().dependency_waits_.load() >= 1);
        mgr.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(23, "上下文传播 (F38)");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 16;
        t_check("创建任务图", graph.create(config));
        const thread::context_id ctx = graph.create_context(7001, 8002, 9003);
        t_check("上下文编号有效", ctx.valid());
        thread::task_body body{};
        body.kind_ = thread::task_body_plain_ctx;
        body.invoke_.plain_ctx_ = &context_body;
        const thread::task_id id = builder_of(graph, "c.ctx") | thread::with_context(ctx)
            | thread::run(body);
        t_check("带上下文注册", id.valid());
        t_check("上下文编号一致", graph.context_of(id) == ctx);
        t_check("请求号可查", graph.contexts().request_id(ctx) == 7001);
        t_check("追踪号可查", graph.contexts().trace_id(ctx) == 8002);
        g_ctx_request.store(0);
        g_ctx_trace.store(0);
        (void)graph.activate(id);
        (void)graph.drain(64);
        t_check("任务读取请求号", g_ctx_request.load() == 7001);
        t_check("任务读取追踪号", g_ctx_trace.load() == 8002);
    }

    // ------------------------------------------------------------------
    t_section(24, "看门狗 deadline 与任务体故障 (F31/F34/F23)");
    {
        thread::manager_config config = thread::profile::server(2);
        config.task_capacity = 16;
        config.watchdog_period_ms = 5;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        mgr.set_error_sink(&timeout_sink, nullptr);

        thread::task_body slow{};
        slow.kind_ = thread::task_body_plain_ctx;
        slow.invoke_.plain_ctx_ = &slow_body;
        const thread::task_id slow_id = mgr.declare_task("w.slow", slow);
        (void)mgr.set_deadline(slow_id, 20'000'000ull);

        thread::task_body fault{};
        fault.kind_ = thread::task_body_plain_ctx;
        fault.invoke_.plain_ctx_ = &fault_body;
        const thread::task_id fault_id = mgr.declare_task("w.fault", fault);

        g_timeout_seen.store(0);
        g_fault_seen.store(0);
        mgr.start();
        (void)mgr.activate(slow_id);
        (void)mgr.activate(fault_id);
        for (int i = 0; i < 500 && mgr.state_of(slow_id) != thread::task_state::completed; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        mgr.drain(2000000);
        t_check("超时被上报", g_timeout_seen.load() >= 1);
        t_check("超时任务最终完成", mgr.state_of(slow_id) == thread::task_state::completed);
        t_check("故障任务置 faulted", mgr.state_of(fault_id) == thread::task_state::faulted);
        t_check("故障任务体执行过", g_fault_seen.load() == 1);
        mgr.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(25, "异步 IO 读写 (F33)");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 16;
        t_check("创建任务图", graph.create(config));

        thread::mpmc_ring<thread::io_event> queue;
        t_check("创建完成队列", queue.create(64));
        memory_backend_state state{&queue};
        thread::io_backend backend{&state, &io_submit, &io_cancel, &io_poll};

        static thread::io_domain domain;
        t_check("创建 IO 域", domain.create(graph, backend, 8));
        t_check("启动 IO 域", domain.start());
        g_io_domain = &domain;
        g_io_source.self_ = g_io_buffer;
        g_io_source.read_ = &io_read;
        g_io_source.write_ = &io_write;

        thread::task_body body{};
        body.kind_ = thread::task_body_coroutine_ctx;
        body.invoke_.coro_ctx_ = &io_body;
        const thread::task_id id = graph.declare("c.io", body);
        g_io_done.store(0);
        t_check("激活 IO 任务", graph.activate(id));
        for (int i = 0; i < 2000 && graph.state_of(id) != thread::task_state::completed; ++i)
        {
            (void)graph.pump_role(thread::role::general, 8);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (g_io_done.load() != 1)
        {
            std::printf("    [dbg] state=%s done=%d submitted=%llu completed=%llu pending=%u\n",
                        thread::task_state_name(graph.state_of(id)), g_io_done.load(),
                        static_cast<unsigned long long>(domain.metrics_.submitted_.load()),
                        static_cast<unsigned long long>(domain.metrics_.completed_.load()),
                        static_cast<unsigned>(domain.pending_.load()));
        }
        t_check("读写完成", g_io_done.load() == 1);
        t_check("IO 提交计数", domain.metrics_.submitted_.load() == 2);
        t_check("IO 字节统计", domain.metrics_.bytes_read_.load() == 4 &&
                                   domain.metrics_.bytes_written_.load() == 2);
        domain.stop();
        domain.destroy();
    }

    // ------------------------------------------------------------------
    t_section(26, "阻塞卸载池 (F44)");
    {
        thread::manager_config config = thread::profile::server(2);
        config.task_capacity = 16;
        config.blocking_threads = 2;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        thread::task_body body{};
        body.kind_ = thread::task_body_coroutine_ctx;
        body.invoke_.coro_ctx_ = &blocking_body;
        const thread::task_id id = mgr.declare_task("c.blocking", body);
        g_blocking_done.store(0);
        mgr.start();
        g_blocking_domain = &mgr.blocking();
        t_check("激活", mgr.activate(id));
        mgr.drain(4000000);
        t_check("阻塞调用完成", g_blocking_done.load() == 1);
        t_check("池队列已清空", mgr.blocking().queue_depth() == 0);
        mgr.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(27, "热重载 body 版本 (F41)");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 16;
        t_check("创建任务图", graph.create(config));
        thread::task_body first{};
        first.kind_ = thread::task_body_plain_ctx;
        first.invoke_.plain_ctx_ = &body_a;
        const thread::task_id id = graph.declare("c.hot", first);
        g_body_a.store(0);
        g_body_b.store(0);
        (void)graph.activate(id);
        (void)graph.drain(64);
        t_check("旧版本执行", g_body_a.load() == 1 && g_body_b.load() == 0);
        thread::task_body second{};
        second.kind_ = thread::task_body_plain_ctx;
        second.invoke_.plain_ctx_ = &body_b;
        t_check("替换任务体", graph.replace_body(id, second));
        t_check("版本号递增", graph.body_version(id) == 1);
        (void)graph.activate(id);
        (void)graph.drain(64);
        t_check("新版本执行", g_body_b.load() == 1);
        t_check("旧版本不再执行", g_body_a.load() == 1);
    }

    // ------------------------------------------------------------------
    t_section(28, "确定性模式 (F40)");
    {
        thread::role_config roles[] =
        {
            {"det", 0, 4, thread::order_mode::parallel},
        };
        roles[0].deterministic_ = true;
        thread::manager_config config = thread::profile::server(4);
        config.task_capacity = 16;
        config.roles = roles;
        config.role_count = 1;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        thread::task_body body{};
        body.kind_ = thread::task_body_plain_ctx;
        body.invoke_.plain_ctx_ = &det_record_body;
        thread::task_id ids[4]{};
        for (int i = 0; i < 4; ++i)
        {
            ids[i] = mgr.declare_task("d.task", body);
        }
        for (int i = 0; i < 4; ++i)
        {
            g_det_workers[i].store(-1, std::memory_order_relaxed);
        }
        g_det_done.store(0);
        mgr.start();
        for (int i = 0; i < 4; ++i)
        {
            (void)mgr.activate(ids[i]);
        }
        mgr.drain(2000000);
        bool mapped = true;
        for (int i = 0; i < 4; ++i)
        {
            mapped = mapped && g_det_workers[i].load(std::memory_order_relaxed) == (i % 4);
        }
        t_check("固定映射 task_id % worker_count", mapped);
        t_check("全部执行", g_det_done.load() == 4);
        mgr.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(29, "多 manager 消息 (F42)");
    {
        thread::manager_config config_a = thread::profile::server(1);
        config_a.task_capacity = 8;
        thread::manager_config config_b = thread::profile::server(1);
        config_b.task_capacity = 8;
        thread::manager first;
        thread::manager second;
        t_check("配置两个管理器", first.configure(config_a) && second.configure(config_b));
        thread::task_body body{};
        body.kind_ = thread::task_body_plain_ctx;
        body.invoke_.plain_ctx_ = &group_body;
        const thread::task_id id = second.declare_task("g.task", body);
        thread::manager* managers[2] = {&first, &second};
        thread::manager_group group;
        t_check("创建 manager_group", group.create(managers, 2, 16));
        first.start();
        second.start();
        g_group_done.store(0);
        thread::group_message message{};
        message.kind_ = thread::group_message_kind::activate;
        message.task_ = id;
        t_check("投递消息", group.post(1, message));
        t_check("消费消息", group.poll(1, 8) == 1);
        second.drain(2000000);
        t_check("跨管理器激活执行", g_group_done.load() == 1);
        group.destroy();
        first.stop(thread::stop_mode::drain);
        second.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(30, "追踪环 (F43)");
    {
        thread::manager_config config = thread::profile::server(2);
        config.task_capacity = 16;
        config.trace_enabled = true;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        thread::task_body body{};
        body.kind_ = thread::task_body_plain_ctx;
        body.invoke_.plain_ctx_ = &group_body;
        const thread::task_id id = mgr.declare_task("t.task", body);
        g_group_done.store(0);
        mgr.start();
        (void)mgr.activate(id);
        mgr.drain(2000000);
        g_trace_events.store(0, std::memory_order_relaxed);
        thread::trace_recorder& recorder = mgr.trace();
        for (std::uint32_t worker = 0; worker < 2; ++worker)
        {
            (void)recorder.drain(worker, &trace_count_sink, nullptr);
        }
        t_check("产生追踪事件", g_trace_events.load(std::memory_order_relaxed) > 0);
        t_check("无丢弃", recorder.dropped_.load() == 0);
        mgr.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(31, "时间轮 (F36)");
    {
        constexpr std::uint32_t count = 1000;
        static timer_node nodes[count];
        timing_wheel wheel;
        t_check("创建时间轮", wheel.create(count, &wheel_callback, nullptr, 0));
        bool added = true;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            added = added && wheel.add(nodes[i], (static_cast<std::uint64_t>(i % 100) + 1) * 1'000'000ull);
        }
        t_check("全部加入", added && wheel.size() == count);
        std::size_t fired = 0;
        for (std::uint32_t step = 1; step <= 200; ++step)
        {
            fired += wheel.advance(static_cast<std::uint64_t>(step) * 1'000'000ull);
        }
        t_check("全部触发", fired == count && g_wheel_fired.load(std::memory_order_relaxed) == static_cast<int>(count));
        t_check("剩余为空", wheel.size() == 0);
    }

    // ------------------------------------------------------------------
    t_section(32, "协程 join 单任务与 sleep_for (F13)");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 16;
        t_check("创建任务图", graph.create(config));
        thread::task_body child{};
        child.kind_ = thread::task_body_coroutine_ctx;
        child.invoke_.coro_ctx_ = &child_body;
        const thread::task_id child_id = graph.declare("c.join.child", child);
        thread::task_body parent{};
        parent.kind_ = thread::task_body_coroutine_ctx;
        parent.invoke_.coro_ctx_ = &parent_body;
        const thread::task_id parent_id = graph.declare("c.join.parent", parent);
        g_join_child.store(0);
        g_join_parent.store(0);
        t_check("子任务编号为 0", child_id.index_ == 0);
        t_check("激活父任务", graph.activate(parent_id));
        (void)graph.drain(512);
        t_check("单任务 join 恢复", g_join_parent.load() == 1);
        t_check("子任务执行", g_join_child.load() == 1);

        thread::manager_config config_async = thread::profile::server(2);
        config_async.task_capacity = 16;
        config_async.watchdog_period_ms = 5;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config_async));
        thread::task_body sleeper{};
        sleeper.kind_ = thread::task_body_coroutine_ctx;
        sleeper.invoke_.coro_ctx_ = &sleep_body;
        const thread::task_id sleep_id = mgr.declare_task("c.sleep", sleeper);
        g_sleep_done.store(0);
        mgr.start();
        t_check("激活睡眠任务", mgr.activate(sleep_id));
        for (int i = 0; i < 1000 && mgr.state_of(sleep_id) != thread::task_state::completed; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        mgr.drain(2000000);
        t_check("定时等待恢复", g_sleep_done.load() == 1);
        mgr.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(33, "when_any 首个完成");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 16;
        t_check("创建任务图", graph.create(config));

        thread::task_body fast{};
        fast.kind_ = thread::task_body_plain_ctx;
        fast.invoke_.plain_ctx_ = &body_a;
        const thread::task_id child_fast = graph.declare("any.fast", fast);

        thread::task_body slow{};
        slow.kind_ = thread::task_body_coroutine_ctx;
        slow.invoke_.coro_ctx_ = &any_wait_body;
        const thread::task_id child_slow = graph.declare("any.slow", slow);
        (void)child_slow;

        thread::task_body parent{};
        parent.kind_ = thread::task_body_coroutine_ctx;
        parent.invoke_.coro_ctx_ = &any_parent_body;
        const thread::task_id parent_id = graph.declare("any.parent", parent);

        g_body_a.store(0);
        g_any_done.store(0);
        g_any_first.store(thread::invalid_task, std::memory_order_relaxed);
        t_check("激活父任务", graph.activate(parent_id));
        (void)graph.drain(512);
        t_check("父任务恢复一次", g_any_done.load() == 1);
        t_check("返回已完成的子任务", g_any_first.load(std::memory_order_relaxed) == child_fast);
        t_check("父任务完成", graph.state_of(parent_id) == thread::task_state::completed);
    }


    // ------------------------------------------------------------------
    t_section(34, "线程预算与实际派生线程");
    {
        const std::size_t budget = thread::profile::thread_budget();
        t_check("默认预算至少 1", budget >= 1);
        t_check("server 默认使用预算", thread::profile::server().worker_count == budget);
        t_check("game 默认使用预算", thread::profile::game().worker_count == budget);
        t_check("embedded 仍为 0", thread::profile::embedded().worker_count == 0);

        thread::role_config roles[] =
        {
            {"general", 1, 2, thread::order_mode::parallel},
            {"renderer", 0, 1, thread::order_mode::sequential},
        };
        roles[1].external_ = true;
        thread::manager_config config = thread::profile::server(3);
        config.task_capacity = 16;
        config.roles = roles;
        config.role_count = 2;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        t_check("线程预算含主线程槽位", mgr.worker_count() == 3);
        t_check("实际派生扣除外部角色", mgr.spawned_worker_count() == 2);
    }

    // ------------------------------------------------------------------
    t_section(35, "时间预算 budget_exceeded");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 8;
        t_check("创建任务图", graph.create(config));
        thread::task_body body{};
        body.kind_ = thread::task_body_plain_ctx;
        body.invoke_.plain_ctx_ = &budget_body;
        const thread::task_id id = graph.declare("b.task", body);
        t_check("设置预算", graph.set_budget(id, 1'000'000ull));
        t_check("预算可查", graph.budget_of(id) == 1'000'000ull);
        g_budget_hits.store(0, std::memory_order_relaxed);
        (void)graph.activate(id);
        (void)graph.drain(256);
        t_check("任务体观察到预算超时", g_budget_hits.load(std::memory_order_relaxed) == 1);
    }

    // ------------------------------------------------------------------
    t_section(36, "追踪等待事件");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 8;
        t_check("创建任务图", graph.create(config));
        thread::trace_recorder recorder;
        t_check("创建追踪环", recorder.create(1, 256));
        recorder.set_enabled(true);
        graph.set_trace(&recorder);

        thread::task_body body{};
        body.kind_ = thread::task_body_coroutine_ctx;
        body.invoke_.coro_ctx_ = &frame_body;
        const thread::task_id id = graph.declare("t.frame", body);
        g_frame_hits.store(0);
        for (int i = 0; i < 16; ++i)
        {
            g_trace_kinds[i].store(0, std::memory_order_relaxed);
        }
        (void)graph.activate(id);
        (void)graph.pump_role(thread::role::general, 8);
        (void)graph.begin_frame(thread::role::general);
        graph.end_frame(thread::role::general);
        (void)graph.pump_role(thread::role::general, 8);
        (void)recorder.drain(0, &trace_kind_sink, nullptr);
        t_check("记录入队事件",
                g_trace_kinds[static_cast<int>(thread::trace_kind::enqueue)].load(std::memory_order_relaxed) >= 1);
        t_check("记录开始事件", g_trace_kinds[static_cast<int>(thread::trace_kind::start)].load(std::memory_order_relaxed) >= 1);
        t_check("记录等待开始",
                g_trace_kinds[static_cast<int>(thread::trace_kind::wait_begin)].load(std::memory_order_relaxed) >= 1);
        t_check("记录等待结束",
                g_trace_kinds[static_cast<int>(thread::trace_kind::wait_end)].load(std::memory_order_relaxed) >= 1);
    }

    // ------------------------------------------------------------------
    t_section(37, "准入策略");
    {
        thread::admission_config reject_config{};
        reject_config.max_inflight_ = 1;
        reject_config.policy_ = thread::overload_policy::reject;
        thread::admission_controller reject;
        t_check("创建拒绝策略", reject.create(reject_config));
        t_check("首个准入", reject.try_admit(0));
        t_check("第二个拒绝", !reject.try_admit(0));
        t_check("拒绝计数", reject.metrics_.rejected_.load() == 1);
        reject.destroy();

        thread::admission_config drop_config{};
        drop_config.max_inflight_ = 1;
        drop_config.policy_ = thread::overload_policy::drop_oldest;
        thread::admission_controller drop;
        t_check("创建丢弃策略", drop.create(drop_config));
        (void)drop.try_admit(0);
        t_check("丢弃返回失败", !drop.try_admit(0));
        t_check("丢弃计数", drop.metrics_.dropped_.load() == 1);
        drop.destroy();

        thread::admission_config coalesce_config{};
        coalesce_config.max_inflight_ = 1;
        coalesce_config.policy_ = thread::overload_policy::coalesce;
        thread::admission_controller coalesce;
        t_check("创建合并策略", coalesce.create(coalesce_config));
        (void)coalesce.try_admit(0);
        t_check("合并返回失败", !coalesce.try_admit(0));
        t_check("合并计数", coalesce.metrics_.coalesced_.load() == 1);
        coalesce.destroy();

        thread::admission_config degrade_config{};
        degrade_config.max_inflight_ = 1;
        degrade_config.policy_ = thread::overload_policy::degrade;
        thread::admission_controller degrade;
        t_check("创建降级策略", degrade.create(degrade_config));
        (void)degrade.try_admit(0);
        t_check("降级仍准入", degrade.try_admit(0));
        t_check("降级计数", degrade.metrics_.degraded_.load() == 1);
        degrade.destroy();
    }

    // ------------------------------------------------------------------
    t_section(38, "热重载版本上限");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 8;
        config.body_version_capacity = 2;
        t_check("创建任务图", graph.create(config));
        thread::task_body first{};
        first.kind_ = thread::task_body_plain_ctx;
        first.invoke_.plain_ctx_ = &body_a;
        const thread::task_id id = graph.declare("h.task", first);
        thread::task_body second{};
        second.kind_ = thread::task_body_plain_ctx;
        second.invoke_.plain_ctx_ = &body_b;
        t_check("第 1 次替换", graph.replace_body(id, second));
        t_check("第 2 次替换", graph.replace_body(id, second));
        t_check("超上限被拒绝", !graph.replace_body(id, second));
        t_check("版本号停在 2", graph.body_version(id) == 2);
    }

    // ------------------------------------------------------------------
    t_section(39, "指标快照");
    {
        thread::manager_config config = thread::profile::server(2);
        config.task_capacity = 16;
        config.trace_enabled = true;
        config.blocking_threads = 1;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        thread::task_body body{};
        body.kind_ = thread::task_body_plain_ctx;
        body.invoke_.plain_ctx_ = &group_body;
        const thread::task_id id = mgr.declare_task("m.task", body);
        g_group_done.store(0);
        mgr.start();
        (void)mgr.activate(id);
        mgr.drain(2000000);
        thread::metrics_snapshot snapshot = thread::collect_metrics(mgr);
        t_check("快照含任务计数", snapshot.task.declared >= 1 && snapshot.task.activations >= 1);
        t_check("快照含派生线程", snapshot.spawned_workers == 2);
        t_check("快照含线程预算", snapshot.thread_budget >= 1);
        t_check("快照含准入字段", snapshot.admission.completed == 0);
        t_check("快照含阻塞队列深度", snapshot.blocking_queue_depth == 0);
        t_check("快照含追踪丢弃计数", snapshot.trace_dropped == 0);
        mgr.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(40, "IO 取消在途请求");
    {
        thread::manager_config config = thread::profile::server(1);
        config.task_capacity = 8;
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));

        thread::io_backend backend{nullptr, &never_submit, &never_cancel, &never_poll};
        static thread::io_domain domain;
        t_check("创建 IO 域", domain.create(mgr.tasks(), backend, 4));
        t_check("启动 IO 域", domain.start());
        g_never_domain = &domain;
        g_never_source.self_ = nullptr;
        g_never_source.read_ = &never_read;

        thread::task_body body{};
        body.kind_ = thread::task_body_coroutine_ctx;
        body.invoke_.coro_ctx_ = &cancel_io_body;
        const thread::task_id id = mgr.declare_task("io.cancel", body);
        g_cancel_io_done.store(0, std::memory_order_relaxed);
        mgr.start();
        t_check("激活", mgr.activate(id));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        t_check("取消作用域", mgr.cancel(id));
        for (int i = 0; i < 2000 && mgr.state_of(id) != thread::task_state::completed; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        t_check("取消后任务恢复", g_cancel_io_done.load(std::memory_order_relaxed) == 1);
        t_check("任务完成", mgr.state_of(id) == thread::task_state::completed);
        t_check("IO 取消计数", domain.metrics_.cancelled_.load() >= 1);
        domain.stop();
        domain.destroy();
        mgr.stop(thread::stop_mode::drain);
    }

    // ------------------------------------------------------------------
    t_section(41, "工作项路由剔除外部与自动帧槽位");
    {
        thread::role_config roles[3] =
        {
            {"general", 0, 2, thread::order_mode::parallel},
            {"renderer", 2, 1, thread::order_mode::sequential},
            {"logic", 3, 1, thread::order_mode::sequential},
        };
        roles[1].external_ = true;
        roles[2].frame_auto_ = true;

        thread::manager_config config = thread::profile::server(4);
        config.roles = roles;
        config.role_count = 3;

        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));
        t_check("外部线程接入", mgr.attach_thread(thread::role::renderer, "test-main"));
        mgr.start();

        g_route_spawned.store(0);
        int submitted = 0;
        for (int i = 0; i < 200; ++i)
        {
            if (thread::spawn(mgr, route_spawn_body()))
            {
                ++submitted;
            }
        }
        mgr.drain(4000000);
        t_check("提交全部成功", submitted == 200);
        t_check("工作项全部执行", g_route_spawned.load() == submitted);

        mgr.stop(thread::stop_mode::abort);
        mgr.detach_thread(thread::role::renderer);
    }

    // ------------------------------------------------------------------
    t_section(42, "parallel_for 之后的协程等待");
    {
        thread::manager_config config = thread::profile::server(4);
        thread::manager mgr;
        t_check("配置管理器", mgr.configure(config));

        g_route_parallel.store(0);
        g_route_child.store(0);
        g_route_flow_done.store(0);

        thread::task_body child_body{};
        child_body.kind_ = thread::task_body_coroutine_ctx;
        child_body.invoke_.coro_ctx_ = &route_child_body;
        g_route_child_task = mgr.declare_task("route.child", child_body);

        thread::task_body flow_body{};
        flow_body.kind_ = thread::task_body_coroutine_ctx;
        flow_body.invoke_.coro_ctx_ = &route_flow_body;
        const thread::task_id flow = mgr.declare_task("route.flow", flow_body);

        mgr.start();
        t_check("激活流程", mgr.activate(flow));
        for (int i = 0; i < 4000 && mgr.state_of(flow) != thread::task_state::completed; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        t_check("分片全部执行", g_route_parallel.load() == 4096);
        t_check("后续 join 生效", g_route_child.load() == 1);
        t_check("后续 sleep 生效", g_route_flow_done.load() == 1);
        t_check("流程状态完成", mgr.state_of(flow) == thread::task_state::completed);

        mgr.stop(thread::stop_mode::abort);
    }

    // ------------------------------------------------------------------
    t_section(43, "未配置管理器安全停止");
    {
        thread::manager mgr;
        mgr.stop();
        t_check("相位为 stopped", mgr.phase() == thread::shutdown_phase::stopped);
        t_check("未运行", !mgr.running());
        t_check("无工作线程", mgr.worker_count() == 0);
        t_check("pump 返回 0", mgr.pump() == 0);
    }

    t_section(42, "统一错误对象交付");
    {
        thread::manager_config config = thread::profile::server(2);
        config.task_capacity = 8;
        thread::manager mgr;
        const operating_message configured = mgr.configure(config);
        t_check("配置成功返回成功对象", static_cast<bool>(configured));

        const operating_message again = mgr.configure(config);
        t_check("重复配置失败", !again);
        t_check("重复配置写 already_exists", again.is_code(om_err_already_exists));
        t_check("重复配置级别为 warn", again.level() == msg_level::warn);

        thread::task_graph bad_graph;
        thread::task_graph_config bad{};
        bad.task_capacity = 0;
        const operating_message create_error = bad_graph.create(bad);
        t_check("非法容量创建失败", !create_error);
        t_check("创建失败码为 invalid_argument",
                create_error.is_code(om_err_invalid_argument));
        thread::task_graph good_graph;
        thread::task_graph_config good_config{};
        good_config.worker_count = 1;
        good_config.task_capacity = 4;
        const operating_message good_error = good_graph.create(good_config);
        t_check("合法配置创建成功", static_cast<bool>(good_error));
    }

    t_section(43, "异步失败回调");
    {
        g_completion_count.store(0);
        for (int i = 0; i < 4; ++i)
        {
            g_completion_codes[i].store(0);
            g_completion_lines[i].store(0);
        }

        thread::manager mgr;
        thread::manager_config config = thread::profile::server(1);
        config.task_capacity = 16;
        t_check("配置异步回调管理器", static_cast<bool>(mgr.configure(config)));
        mgr.set_completion_error(&completion_sink, nullptr);

        thread::task_body default_fault{};
        default_fault.kind_ = thread::task_body_plain_ctx;
        default_fault.invoke_.plain_ctx_ = &fault_body;
        const thread::task_id default_id = mgr.declare_task("e.fault.default", default_fault);

        thread::task_body coded_fault{};
        coded_fault.kind_ = thread::task_body_plain_ctx;
        coded_fault.invoke_.plain_ctx_ = &fault_with_code_body;
        const thread::task_id coded_id = mgr.declare_task("e.fault.coded", coded_fault);

        thread::task_body good{};
        good.kind_ = thread::task_body_plain_ctx;
        good.invoke_.plain_ctx_ = &ok_body;
        const thread::task_id good_id = mgr.declare_task("e.ok", good);

        mgr.start();
        (void)mgr.activate(default_id);
        (void)mgr.activate(coded_id);
        (void)mgr.activate(good_id);
        for (int i = 0; i < 500; ++i)
        {
            if (mgr.state_of(default_id) == thread::task_state::faulted &&
                mgr.state_of(coded_id) == thread::task_state::faulted &&
                mgr.state_of(good_id) == thread::task_state::completed)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        mgr.drain(2000000);

        t_check("故障回调恰好两次", g_completion_count.load(std::memory_order_relaxed) == 2);
        bool saw_default = false;
        bool saw_timeout = false;
        for (int i = 0; i < 2; ++i)
        {
            const unsigned code = g_completion_codes[i].load(std::memory_order_relaxed);
            if (code == om_err_task_body_faulted.code)
            {
                saw_default = true;
            }
            if (code == om_err_timeout.code && g_completion_lines[i].load() != 0)
            {
                saw_timeout = true;
            }
        }
        t_check("默认故障码为 task_body_faulted", saw_default);
        t_check("自定义故障码与站点透传", saw_timeout);
        t_check("成功任务不触发回调", g_completion_count.load() == 2);
        mgr.stop(thread::stop_mode::drain);
    }

    t_section(45, "配置期错误对象");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 8;
        t_check("创建任务图", graph.create(config));

        const thread::task_id id = graph.declare("cfg.task", thread::task_body{});
        const operating_message moved = graph.migrate(id, thread::role::general);
        t_check("迁移到自身角色成功", static_cast<bool>(moved));

        const operating_message bad_move = graph.migrate(id, thread::make_role(7));
        t_check("迁移到未绑定角色失败", !bad_move);
        t_check("迁移失败码为 invalid_argument",
                bad_move.is_code(om_err_invalid_argument));
        t_check("迁移失败码非零", bad_move.code() != 0);

        const thread::task_id bad{};
        const operating_message bad_dep = graph.add_dependency(bad, id);
        t_check("非法任务加依赖失败", !bad_dep);
        t_check("依赖失败码为 invalid_argument",
                bad_dep.is_code(om_err_invalid_argument));

        const operating_message bad_activate = graph.activate(bad);
        t_check("非法任务激活失败", !bad_activate);
        t_check("激活失败码非零", bad_activate.code() != 0);

        const operating_message bad_budget = graph.set_budget(bad, 100);
        t_check("非法任务设置预算失败", !bad_budget);
        t_check("预算失败码非零", bad_budget.code() != 0);
    }

    t_section(46, "子任务失败传播到父任务");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 16;
        t_check("创建故障传播任务图", graph.create(config));

        probe p{};
        auto parent_body_fn = [&p](thread::task_context&) noexcept { record(p); };
        const thread::task_id parent =
            builder_of(graph, "fault.parent") | thread::on(thread::role::general)
            | thread::run(parent_body_fn);
        thread::task_node* parent_node = graph.node_at(parent);
        if (parent_node != nullptr)
        {
            parent_node->flags_.fetch_or(thread::flag_auto_start_children |
                                         thread::flag_join_children,
                                         std::memory_order_acq_rel);
        }

        const thread::route_word route = thread::route_word::make(
            thread::role::general, thread::order_mode::parallel,
            thread::affinity_kind::none, thread::priority::normal,
            thread::task_worker_none);
        const thread::parent_word parent_word =
            thread::parent_word::make(parent.index_, 1, thread::parent_word::flag_join_counted);
        const thread::task_id child =
            graph.declare_full("fault.child", thread::task_body{}, route, parent_word,
                               thread::invalid_order_key, 0);
        thread::task_body child_body{};
        child_body.kind_ = thread::task_body_plain_ctx;
        child_body.invoke_.plain_ctx_ = &fault_body;
        (void)graph.set_body(child, child_body);

        g_fault_seen.store(0, std::memory_order_relaxed);
        t_check("激活故障父任务", graph.activate(parent));
        for (int i = 0; i < 16; ++i)
        {
            (void)graph.pump_role(thread::role::general, 16);
        }
        t_check("故障子任务执行", g_fault_seen.load(std::memory_order_relaxed) == 1);
        t_check("子任务状态为 faulted",
                graph.state_of(child) == thread::task_state::faulted);
        t_check("父任务传播为 faulted",
                graph.state_of(parent) == thread::task_state::faulted);
    }

    t_section(47, "join 子任务失败唤醒父协程");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 16;
        t_check("创建 join 故障任务图", graph.create(config));

        thread::task_body child_body{};
        child_body.kind_ = thread::task_body_plain_ctx;
        child_body.invoke_.plain_ctx_ = &fault_body;
        const thread::task_id child = graph.declare("join.fault.child", child_body);

        thread::task_body parent_body_value{};
        parent_body_value.kind_ = thread::task_body_coroutine_ctx;
        parent_body_value.invoke_.coro_ctx_ = &join_fault_parent_body;
        const thread::task_id parent = graph.declare("join.fault.parent", parent_body_value);

        g_fault_seen.store(0, std::memory_order_relaxed);
        g_join_fault_handled.store(0, std::memory_order_relaxed);
        t_check("激活 join 父协程", graph.activate(parent));
        for (int i = 0; i < 16; ++i)
        {
            (void)graph.pump_role(thread::role::general, 16);
        }
        t_check("join 子任务 faulted",
                graph.state_of(child) == thread::task_state::faulted);
        t_check("join 父协程被唤醒并取得错误",
                g_join_fault_handled.load(std::memory_order_relaxed) == 1);
        t_check("join 父协程完成",
                graph.state_of(parent) == thread::task_state::completed);
    }

    t_section(48, "延迟激活重复登记");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 8;
        t_check("创建延迟任务图", graph.create(config));

        probe p{};
        auto body = [&p](thread::task_context&) noexcept { record(p); };
        const thread::task_id delayed =
            builder_of(graph, "delay.rearm") | thread::on(thread::role::general)
            | thread::run(body);
        thread::activate_options options{};
        options.delay_frames_ = 2;
        t_check("首次延迟登记", graph.activate(delayed, options));
        t_check("重复延迟登记合并", graph.activate(delayed, options));
        (void)graph.begin_frame(thread::role::general);
        (void)graph.pump_role(thread::role::general, 8);
        t_check("延迟第 1 帧不执行", p.total.load() == 0);
        (void)graph.begin_frame(thread::role::general);
        (void)graph.pump_role(thread::role::general, 8);
        t_check("延迟到期只执行一次", p.total.load() == 1);
    }

    return t_end("test_module_task_graph");

}
