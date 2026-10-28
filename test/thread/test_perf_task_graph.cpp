// test_perf_task_graph.cpp - 性能测试: 持久任务图
#include "test_common.hpp"
#include "include/thread/manager.hpp"

#include <atomic>
#include <cstdio>
#include <cstdint>

namespace
{
volatile std::uint64_t g_sink = 0;
std::atomic<std::uint64_t> g_counter{0};

void empty_body(thread::task_context&) noexcept
{
    g_counter.fetch_add(1, std::memory_order_relaxed);
}

void empty_ctx_body(thread::task_context&) noexcept
{
}

thread::task<void> resume_body(thread::task_context&)
{
    co_return;
}

thread::io_domain* g_perf_domain = nullptr;
thread::data_source g_perf_source{};
int g_perf_io_done = 0;

struct perf_backend_state
{
    thread::mpmc_ring<thread::io_event>* queue_{nullptr};
};

bool perf_submit(void* self, thread::io_request* request) noexcept
{
    auto* state = static_cast<perf_backend_state*>(self);
    thread::io_result result{};
    result.bytes_ = static_cast<std::ptrdiff_t>(request->capacity_);
    result.status_ = thread::io_status::ok;
    thread::io_event event{request, result};
    return state->queue_->try_push(event);
}

void perf_cancel(void*, thread::io_request*) noexcept
{
}

int perf_poll(void* self, thread::io_event* out, int max_events, int) noexcept
{
    auto* state = static_cast<perf_backend_state*>(self);
    int count = 0;
    while (count < max_events && state->queue_->try_pop(out[count]))
    {
        ++count;
    }
    return count;
}

thread::io_result perf_read(void*, std::span<std::byte> buffer) noexcept
{
    for (std::size_t i = 0; i < buffer.size(); ++i)
    {
        buffer[i] = std::byte{1};
    }
    return thread::io_result{static_cast<std::ptrdiff_t>(buffer.size()), thread::io_status::ok,
                             0};
}

thread::io_result perf_write(void*, std::span<const std::byte> buffer) noexcept
{
    return thread::io_result{static_cast<std::ptrdiff_t>(buffer.size()), thread::io_status::ok,
                             0};
}

thread::task<void> perf_io_body(thread::task_context&)
{
    std::byte buffer[64]{};
    const thread::io_result result =
        co_await thread::read_some(*g_perf_domain, g_perf_source, std::span<std::byte>(buffer, 64));
    if (result.ok())
    {
        ++g_perf_io_done;
    }
    co_return;
}

void perf_wheel_callback(void*, timer_node*) noexcept
{
}
} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    t_begin("test_perf_task_graph — 性能测试: 持久任务图 (百万为单位)");

    constexpr std::uint64_t N = 1'000'000;

    t_section(1, "注册与查询");
    {
        constexpr std::uint64_t count = 65536;
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = count + 16;
        config.dependency_capacity = 1024;
        if (!graph.create(config))
        {
            t_check("创建任务图", false);
            return t_end("test_perf_task_graph");
        }

        double ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < count; ++i)
            {
                const thread::task_id id = graph.declare("perf", thread::task_body{});
                g_sink += id.index_;
            }
        }, 1);
        t_perf("declare", count, ms);

        const thread::task_id probe = graph.declare("perf.view", thread::task_body{});
        g_sink += probe.index_;
        ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < N; ++i)
            {
                const thread::task_view view = graph.view(probe);
                g_sink += static_cast<std::uint64_t>(view.state_);
            }
        }, 3);
        t_perf("view", N, ms);
    }

    t_section(2, "顺序域");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 64;
        if (!graph.create(config))
        {
            t_check("创建任务图", false);
            return t_end("test_perf_task_graph");
        }
        thread::task_body body{};
        body.invoke_.plain_ctx_ = &empty_body;
        body.kind_ = thread::task_body_plain_ctx;
        const thread::task_id tick = graph.declare("perf.seq", body);

        const double ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < N; ++i)
            {
                (void)graph.activate(tick);
                (void)graph.pump_role(thread::role::general, 4);
            }
        }, 3);
        t_perf("activate+pump", N, ms);
    }

    t_section(3, "通用域派发");
    {
        constexpr std::uint32_t tasks = 512;
        constexpr std::uint64_t rounds = 1024;

        for (std::uint32_t workers : {1u, 4u})
        {
            thread::manager_config config = thread::profile::server(workers);
            config.task_capacity = tasks + 16;
            config.task_local_capacity = 1024;
            config.pin_workers = false;
            thread::manager mgr;
            if (!mgr.configure(config))
            {
                t_check("配置管理器", false);
                return t_end("test_perf_task_graph");
            }
            thread::task_body body{};
            body.invoke_.plain_ctx_ = &empty_ctx_body;
            body.kind_ = thread::task_body_plain_ctx;
            thread::task_id ids[tasks]{};
            for (std::uint32_t i = 0; i < tasks; ++i)
            {
                ids[i] = mgr.declare_task("perf.par", body);
            }
            mgr.start();

            const std::uint64_t ops = static_cast<std::uint64_t>(tasks) * rounds;
            const double ms = t_bench_best_ms([&]()
            {
                for (std::uint64_t round = 0; round < rounds; ++round)
                {
                    for (std::uint32_t i = 0; i < tasks; ++i)
                    {
                        (void)mgr.activate(ids[i]);
                    }
                    mgr.drain(2000000);
                }
            }, 3);
            const char* label = workers == 1 ? "dispatch 1t" : "dispatch 4t";
            t_perf(label, ops, ms);
            mgr.stop(thread::stop_mode::drain);
        }
    }

    t_section(4, "身份变更");
    {
        thread::role_config roles[] =
        {
            {"general", 0, 1, thread::order_mode::parallel},
            {"alt", 1, 1, thread::order_mode::parallel},
        };
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        config.roles = roles;
        config.role_count = 2;
        if (!graph.create(config))
        {
            t_check("创建任务图", false);
            return t_end("test_perf_task_graph");
        }

        thread::task_body body{};
        body.invoke_.plain_ctx_ = &empty_ctx_body;
        body.kind_ = thread::task_body_plain_ctx;
        const thread::task_id task = graph.declare("perf.migrate", body);
        const thread::role other = thread::make_role(1);

        const double ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < N; ++i)
            {
                (void)graph.migrate(task, (i & 1u) != 0 ? other : thread::role::general);
            }
        }, 3);
        t_perf("migrate", N, ms);
    }

    t_section(5, "重排序与绑定");
    {
        thread::role_config roles[] =
        {
            {"general", 0, 1, thread::order_mode::parallel},
            {"seq", 1, 1, thread::order_mode::sequential},
        };
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 2;
        config.task_capacity = 32;
        config.command_capacity = 131072;
        config.roles = roles;
        config.role_count = 2;
        if (!graph.create(config))
        {
            t_check("创建任务图", false);
            return t_end("test_perf_task_graph");
        }

        thread::task_body body{};
        body.invoke_.plain_ctx_ = &empty_ctx_body;
        body.kind_ = thread::task_body_plain_ctx;
        const thread::role seq_role = thread::make_role(1);
        const thread::task_id ordered = thread::task_builder{&graph, graph.declare("perf.seq.key", thread::task_body{})}
            | thread::on(seq_role) | thread::ordered | thread::run(body);

        constexpr std::uint64_t reorders = 131072;
        const double ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < reorders; ++i)
            {
                (void)graph.reorder(ordered, thread::order_key{(i + 1ull) << 32});
            }
            (void)graph.pump_role(seq_role, 1);
        }, 3);
        t_perf("reorder", reorders, ms);

        const thread::task_id pinned = thread::task_builder{&graph, graph.declare("perf.pin", thread::task_body{})}
            | thread::on(thread::role::general) | thread::pinned(1) | thread::run(body);
        const double pin_ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < N; ++i)
            {
                (void)graph.pin(pinned, static_cast<std::uint32_t>(i & 1u));
                (void)graph.set_stealable(pinned, (i & 2u) != 0);
            }
        }, 3);
        t_perf("pin+stealable", N, pin_ms);
    }

    t_section(6, "协程恢复");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 32;
        if (!graph.create(config))
        {
            t_check("创建任务图", false);
            return t_end("test_perf_task_graph");
        }
        thread::task_body body{};
        body.kind_ = thread::task_body_coroutine_ctx;
        body.invoke_.coro_ctx_ = &resume_body;
        const thread::task_id id = graph.declare("perf.coro", body);

        const double ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < N; ++i)
            {
                (void)graph.activate(id);
                (void)graph.pump_role(thread::role::general, 4);
            }
        }, 3);
        t_perf("coroutine_resume", N, ms);
    }

    t_section(7, "异步 IO");
    {
        thread::manager_config config = thread::profile::server(2);
        config.task_capacity = 32;
        thread::manager mgr;
        if (!mgr.configure(config))
        {
            t_check("配置管理器", false);
            return t_end("test_perf_task_graph");
        }
        thread::mpmc_ring<thread::io_event> queue;
        queue.create(1024);
        perf_backend_state state{&queue};
        thread::io_backend backend{&state, &perf_submit, &perf_cancel, &perf_poll};
        static thread::io_domain domain;
        (void)domain.create(mgr.tasks(), backend, 64);
        (void)domain.start();
        g_perf_domain = &domain;
        g_perf_source.self_ = nullptr;
        g_perf_source.read_ = &perf_read;
        g_perf_source.write_ = &perf_write;

        thread::task_body body{};
        body.kind_ = thread::task_body_coroutine_ctx;
        body.invoke_.coro_ctx_ = &perf_io_body;
        const thread::task_id id = mgr.declare_task("perf.io", body);
        mgr.start();

        constexpr std::uint64_t io_ops = 200;
        const double ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < io_ops; ++i)
            {
                (void)mgr.activate(id);
                mgr.drain(2000000);
            }
        }, 3);
        t_perf("io_read_roundtrip", io_ops, ms);
        domain.stop();
        domain.destroy();
        mgr.stop(thread::stop_mode::drain);
    }

    t_section(8, "看门狗扫描");
    {
        constexpr std::uint32_t tasks = 8192;
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = tasks;
        if (!graph.create(config))
        {
            t_check("创建任务图", false);
            return t_end("test_perf_task_graph");
        }
        for (std::uint32_t i = 0; i < tasks; ++i)
        {
            (void)graph.declare("perf.wd", thread::task_body{});
        }
        thread::watchdog_config wd{};
        wd.period_ms_ = 1000;
        thread::task_watchdog watchdog;
        (void)watchdog.start(graph, wd);
        const std::uint64_t now = thread::monotonic_now_ns();
        const double ms = t_bench_best_ms([&]()
        {
            for (int i = 0; i < 1000; ++i)
            {
                (void)watchdog.scan(now + static_cast<std::uint64_t>(i) * 1'000'000ull);
            }
        }, 3);
        t_perf("scan_8k", 1000ull * tasks, ms);
        watchdog.stop();
    }

    t_section(9, "时间轮 100k");
    {
        constexpr std::uint32_t count = 100000;
        static timer_node nodes[count];
        timing_wheel wheel;
        (void)wheel.create(count, &perf_wheel_callback, nullptr, 0);

        double ms = t_bench_best_ms([&]()
        {
            for (std::uint32_t i = 0; i < count; ++i)
            {
                (void)wheel.add(nodes[i],
                                (static_cast<std::uint64_t>(i % 5000) + 1) * 1'000'000ull);
            }
        }, 1);
        t_perf("wheel_add", count, ms);

        ms = t_bench_best_ms([&]()
        {
            for (std::uint32_t i = 0; i < count; ++i)
            {
                (void)wheel.cancel(nodes[i]);
            }
        }, 1);
        t_perf("wheel_cancel", count, ms);

        for (std::uint32_t i = 0; i < count; ++i)
        {
            (void)wheel.add(nodes[i], (static_cast<std::uint64_t>(i % 5000) + 1) * 1'000'000ull);
        }
        ms = t_bench_best_ms([&]()
        {
            for (std::uint32_t step = 1; step <= 5000; ++step)
            {
                (void)wheel.advance(static_cast<std::uint64_t>(step) * 1'000'000ull);
            }
        }, 1);
        t_perf("wheel_advance", count, ms);
    }

    t_section(10, "资源依赖");
    {
        thread::task_graph graph;
        thread::task_graph_config config{};
        config.worker_count = 0;
        config.task_capacity = 16;
        if (!graph.create(config))
        {
            t_check("创建任务图", false);
            return t_end("test_perf_task_graph");
        }
        thread::task_body body{};
        body.invoke_.plain_ctx_ = &empty_ctx_body;
        body.kind_ = thread::task_body_plain_ctx;
        const thread::resource_id res{0};
        const thread::task_id id = thread::task_builder{&graph, graph.declare("perf.res", thread::task_body{})}
            | thread::writes(res) | thread::run(body);
        const double ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < N; ++i)
            {
                (void)graph.activate(id);
                (void)graph.pump_role(thread::role::general, 4);
            }
        }, 3);
        t_perf("resource_acquire", N, ms);
    }

    t_section(11, "追踪开销");
    {
        thread::trace_recorder recorder;
        (void)recorder.create(1, 256);
        thread::trace_event event{};
        event.task_ = thread::task_id{0};

        double ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < N; ++i)
            {
                recorder.record(thread::trace_kind::start, event.task_, 0, thread::role::general, i);
            }
        }, 3);
        t_perf("trace_disabled", N, ms);

        recorder.set_enabled(true);
        ms = t_bench_best_ms([&]()
        {
            for (std::uint64_t i = 0; i < N; ++i)
            {
                recorder.record(thread::trace_kind::start, event.task_, 0, thread::role::general, i);
            }
        }, 3);
        t_perf("trace_enabled", N, ms);
        t_check("ring 溢出只计数", recorder.dropped_.load() > 0);
    }

    return t_end("test_perf_task_graph");
}
