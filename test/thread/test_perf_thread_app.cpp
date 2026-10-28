// test_perf_thread_app.cpp - 性能测试: 应用骨架 thread::app
#include "test_common.hpp"
#include "include/thread/manager.hpp"

#include <atomic>
#include <cstdint>

namespace
{
std::atomic<std::uint64_t> g_hits{0};

void hit(thread::task_context&) noexcept
{
    g_hits.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

int main()
{
    t_begin("test_perf_thread_app — 性能测试: 应用骨架 (百万为单位)");

    thread::app_config config;
    config.frame_rate = 0;
    config.manager.worker_count = 4;

    thread::app application{config};
    application.every_frame("perf.hit", hit);

    t_section(1, "帧驱动");
    {
        constexpr std::uint64_t ticks = 200000;
        t_bench("app_tick", ticks, [&application]()
        {
            for (std::uint64_t i = 0; i < ticks; ++i)
            {
                (void)application.tick();
            }
        }, 3);
    }

    t_section(2, "工作项提交");
    {
        constexpr std::uint64_t jobs = 2000;
        t_bench("post+drain", jobs, [&application]()
        {
            for (std::uint64_t i = 0; i < jobs; ++i)
            {
                (void)application.post([]() noexcept {});
            }
            application.kernel().drain();
        }, 3);
        // 停止前一实例，避免其积压任务干扰注册测量
        application.stop();
    }

    t_section(3, "任务注册");
    {
        thread::app registration{config};
        constexpr std::uint64_t count = 2000;
        timer watch;
        std::uint64_t made = 0;
        for (std::uint64_t i = 0; i < count; ++i)
        {
            if (registration.every_frame(nullptr, hit).valid())
            {
                ++made;
            }
        }
        t_perf("every_frame", made, watch.elapsed_milliseconds());
    }

    return t_end("test_perf_thread_app");
}
