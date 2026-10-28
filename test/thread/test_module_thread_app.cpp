// test_module_thread_app.cpp - 模块功能测试: 应用骨架 thread::app
#include "test_common.hpp"
#include "include/thread/manager.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace
{
std::atomic<int> g_free_body{0};
std::atomic<int> g_plain{0};
std::atomic<int> g_context{0};
std::atomic<int> g_stage{0};
std::atomic<int> g_stage_order{1};
std::atomic<int> g_posted{0};
std::atomic<int> g_spawned{0};
std::atomic<int> g_once{0};

struct sample_system
{
    std::atomic<int> plain{0};
    std::atomic<int> context{0};

    void tick() noexcept
    {
        plain.fetch_add(1, std::memory_order_relaxed);
    }

    void tick_context(thread::task_context&) noexcept
    {
        context.fetch_add(1, std::memory_order_relaxed);
    }
};

struct large_payload
{
    unsigned char data[128]{};
};

void free_body(thread::task_context&) noexcept
{
    g_free_body.fetch_add(1, std::memory_order_relaxed);
}

void stage_load(thread::task_context&) noexcept
{
    g_stage.store(1);
}

void stage_store(thread::task_context&) noexcept
{
    if (g_stage.load() != 1)
    {
        g_stage_order.store(0);
    }
    g_stage.store(2);
}

thread::task<void> spawned_body()
{
    g_spawned.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

thread::app_config unlimited_config()
{
    thread::app_config config;
    config.frame_rate = 0;
    return config;
}
} // namespace

int main()
{
    t_begin("test_module_thread_app — 模块功能测试: 应用骨架");

    t_section(1, "默认布局与三行入口");
    {
        thread::app application{unlimited_config()};
        t_check("配置有效", application.valid());
        t_check("已启动", application.running());
        t_check("工作线程非零", application.worker_count() >= 1);
        t_check("主角色为 general", application.main_role() == thread::role::general);
        t_check("初始帧号为零", application.frame_index() == 0);
        t_check("相位 running", application.phase() == thread::shutdown_phase::running);
    }

    t_section(2, "任务体形态");
    {
        thread::app application{unlimited_config()};
        sample_system system;

        thread::task_handle by_function = application.every_frame("body.function", free_body);
        thread::task_handle by_plain_lambda = application.every_frame(
            "body.plain", []() noexcept { g_plain.fetch_add(1, std::memory_order_relaxed); });
        thread::task_handle by_context_lambda = application.every_frame(
            "body.context", [](thread::task_context&) noexcept
            { g_context.fetch_add(1, std::memory_order_relaxed); });
        thread::task_handle by_member =
            application.every_frame("body.member", &sample_system::tick, &system);
        thread::task_handle by_member_context =
            application.every_frame("body.member_context", &sample_system::tick_context, &system);
        auto pipeline = thread::stage(stage_load) | thread::stage(stage_store);
        thread::task_handle by_stage = application.every_frame("body.stage", pipeline);

        t_check("函数指针注册", by_function.valid());
        t_check("无参 lambda 注册", by_plain_lambda.valid());
        t_check("上下文 lambda 注册", by_context_lambda.valid());
        t_check("成员函数注册", by_member.valid());
        t_check("带上下文成员函数注册", by_member_context.valid());
        t_check("阶段组合注册", by_stage.valid());

        (void)application.run_for(std::chrono::milliseconds(40));

        t_check("函数指针执行", g_free_body.load() > 0);
        t_check("无参 lambda 执行", g_plain.load() > 0);
        t_check("上下文 lambda 执行", g_context.load() > 0);
        t_check("成员函数执行", system.plain.load() > 0);
        t_check("带上下文成员函数执行", system.context.load() > 0);
        t_check("阶段按序执行", g_stage.load() == 2 && g_stage_order.load() == 1);
        t_check("任务计数一致", application.stats().declared_.load() == 6);
    }

    t_section(3, "一次性任务与退出");
    {
        thread::app application{unlimited_config()};
        g_once.store(0);
        thread::task_handle once = application.once("app.once", [&application]() noexcept
        {
            g_once.fetch_add(1, std::memory_order_relaxed);
            application.quit();
        });
        const int rc = application.run();
        t_check("run 正常返回", rc == 0);
        t_check("一次性任务执行一次", g_once.load() == 1);
        t_check("任务状态完成", application.state_of(once) == thread::task_state::completed);
    }

    t_section(4, "工作项提交");
    {
        thread::app application{unlimited_config()};
        g_posted.store(0);
        g_spawned.store(0);

        t_check("小捕获提交", application.post([]() noexcept
        { g_posted.fetch_add(1, std::memory_order_relaxed); }));
        large_payload payload;
        t_check("大捕获提交", application.post([payload]() noexcept
        {
            (void)payload;
            g_posted.fetch_add(1, std::memory_order_relaxed);
        }));

        int submitted = 0;
        for (int i = 0; i < 32; ++i)
        {
            if (application.spawn(spawned_body()))
            {
                ++submitted;
            }
        }
        t_check("协程提交成功", submitted == 32);

        (void)application.tick();
        application.kernel().drain();

        t_check("工作项全部执行", g_posted.load() == 2);
        t_check("协程全部执行", g_spawned.load() == submitted);
    }

    t_section(5, "任务句柄");
    {
        thread::app application{unlimited_config()};
        thread::task_handle first =
            application.every_frame("handle.first", []() noexcept {});
        thread::task_handle second =
            application.every_frame("handle.second", []() noexcept {});

        t_check("句柄有效", first.valid() && second.valid());
        t_check("句柄编号有效", first.id().valid() && second.id().valid());
        t_check("依赖登记", second.after(first).valid());

        (void)application.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        t_check("首任务完成", application.state_of(first) == thread::task_state::completed);

        t_check("禁用成功", second.disable());
        t_check("禁用后状态", application.state_of(second) == thread::task_state::disabled);
        t_check("禁用后拒绝激活", !second.activate());
        t_check("启用成功", second.enable());
        t_check("启用后可激活", second.activate());
    }

    t_section(6, "单步驱动");
    {
        thread::app application{unlimited_config()};
        const std::uint64_t before = application.frame_index();
        t_check("单步返回 true", application.tick());
        t_check("单步返回 true 二次", application.tick());
        t_check("帧号推进两次", application.frame_index() == before + 2);
    }

    t_section(7, "配置失败安全");
    {
        thread::role_config roles[1] = {{"general", 0, 1, thread::order_mode::parallel}};
        thread::app_config config;
        config.frame_rate = 0;
        config.manager.worker_count = 3;
        config.manager.roles = roles;
        config.manager.role_count = 1;

        thread::app application{config};
        t_check("无效配置被拒绝", !application.valid());
        t_check("无效配置不启动", !application.running());
        t_check("无效配置单步返回 false", !application.tick());
        t_check("无效配置 run 返回 1", application.run() == 1);
    }

    t_section(8, "嵌入式驱动");
    {
        thread::app_config config;
        config.frame_rate = 0;
        config.manager.worker_count = 0;
        config.manager.task_capacity = 64;

        thread::app application{config};
        t_check("嵌入式配置有效", application.valid());
        t_check("嵌入式无工作线程", application.worker_count() == 0);

        g_plain.store(0);
        thread::task_handle task = application.every_frame("embed.tick", []() noexcept
        { g_plain.fetch_add(1, std::memory_order_relaxed); });
        t_check("嵌入式注册", task.valid());
        (void)application.tick();
        (void)application.tick();
        t_check("嵌入式执行", g_plain.load() >= 1);
    }

    t_section(9, "未配置内核安全");
    {
        thread::manager mgr;
        mgr.stop();
        t_check("相位 stopped", mgr.phase() == thread::shutdown_phase::stopped);
        t_check("未运行", !mgr.running());
        t_check("pump 返回 0", mgr.pump() == 0);
    }

    return t_end("test_module_thread_app");
}
