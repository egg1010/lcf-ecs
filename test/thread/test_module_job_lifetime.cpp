// test_module_job_lifetime.cpp - 模块功能测试: Job 槽位并发生命周期
#include "test_common.hpp"
#include "include/thread/manager.hpp"

#include <atomic>
#include <thread>

namespace
{
std::atomic<int> g_executed{0};

constexpr int producer_count = 4;
constexpr int jobs_per_producer = 1500;
} // namespace

int main()
{
    t_begin("test_module_job_lifetime — 模块功能测试: Job 槽位并发生命周期");

    t_section(1, "多生产者提交与槽位复用");
    {
        thread::manager mgr;
        thread::manager_config config = thread::profile::server(4);
        config.task_capacity = 32;
        config.job_slot_capacity = 256;
        t_check("配置管理器", static_cast<bool>(mgr.configure(config)));
        mgr.start();
        t_check("启动管理器", true);

        g_executed.store(0, std::memory_order_relaxed);
        std::thread producers[producer_count];
        for (int p = 0; p < producer_count; ++p)
        {
            producers[p] = std::thread([&mgr]() {
                int submitted = 0;
                while (submitted < jobs_per_producer)
                {
                    std::uint32_t slot = thread::slot_index_none;
                    thread::job* target = mgr.take_job(slot);
                    if (target == nullptr)
                    {
                        std::this_thread::yield();
                        continue;
                    }
                    (void)thread::fill_inline_job(target, []() noexcept {
                        g_executed.fetch_add(1, std::memory_order_relaxed);
                    });
                    if (mgr.submit(target) != thread::slot_index_none)
                    {
                        ++submitted;
                    }
                    else
                    {
                        mgr.release_job_local(target);
                        std::this_thread::yield();
                    }
                }
            });
        }
        for (int p = 0; p < producer_count; ++p)
        {
            producers[p].join();
        }

        (void)mgr.drain(5'000'000);
        mgr.stop(thread::stop_mode::drain);
        t_check("所有 Job 恰好执行一次",
                g_executed.load(std::memory_order_relaxed) ==
                    producer_count * jobs_per_producer);
    }

    t_section(2, "重复释放保护");
    {
        thread::manager mgr;
        thread::manager_config config = thread::profile::embedded();
        config.task_capacity = 8;
        config.job_slot_capacity = 8;
        t_check("配置嵌入式管理器", static_cast<bool>(mgr.configure(config)));

        std::uint32_t slot = thread::slot_index_none;
        thread::job* target = mgr.take_job(slot);
        t_check("取得 Job 槽位", target != nullptr);
        (void)thread::fill_inline_job(target, []() noexcept {});
        mgr.release_job_local(target);
        mgr.release_job_local(target);

        std::uint32_t reused = thread::slot_index_none;
        thread::job* next = mgr.take_job(reused);
        t_check("重复释放后槽位仍可分配", next == target && reused == slot);
        mgr.release_job_local(next);
    }

    return t_end("test_module_job_lifetime");
}
