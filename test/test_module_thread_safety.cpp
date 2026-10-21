// test_module_thread_safety.cpp - 模块功能测试: thread 并发安全回归
// 覆盖: 批量窃取完整性 / Lane 就绪位 / 优先级扫描 / 资源互斥 / 停止与排空
#include "test_common.hpp"
#include "include/thread/thread_all.hpp"

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <thread>
#include <type_traits>

namespace
{
std::atomic<int> g_inside{0};
std::atomic<int> g_max_inside{0};

void write_body(thread::task_context& ctx) noexcept
{
    (void)ctx;
    const int now = g_inside.fetch_add(1) + 1;
    int seen = g_max_inside.load();
    while (now > seen && !g_max_inside.compare_exchange_weak(seen, now))
    {
    }
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    g_inside.fetch_sub(1);
}

bool deque_integrity_work(std::size_t capacity, long total)
{
    thread::work_deque deque;
    if (!deque.create(capacity))
    {
        return false;
    }
    std::atomic<bool> producing{true};
    dense<long> collected;

    std::thread thief([&deque, &producing, &collected]() {
        thread::job* batch[8];
        for (;;)
        {
            const bool finishing = !producing.load(std::memory_order_acquire);
            const std::size_t got = deque.steal(batch, 8);
            for (std::size_t i = 0; i < got; ++i)
            {
                collected.push_back(static_cast<long>(reinterpret_cast<std::uintptr_t>(batch[i])));
            }
            if (finishing && got == 0 && deque.size() <= 0)
            {
                break;
            }
        }
    });

    for (long i = 1; i <= total; ++i)
    {
        thread::job* item = reinterpret_cast<thread::job*>(static_cast<std::uintptr_t>(i));
        while (!deque.push(item))
        {
        }
    }
    producing.store(false, std::memory_order_release);
    thief.join();

    while (thread::job* item = deque.pop())
    {
        collected.push_back(static_cast<long>(reinterpret_cast<std::uintptr_t>(item)));
    }

    dense<std::uint8_t> seen;
    seen.resize_fill(static_cast<std::size_t>(total) + 1, 0);
    bool ok = collected.size() == static_cast<std::size_t>(total);
    for (std::size_t i = 0; i < collected.size(); ++i)
    {
        const long value = collected[i];
        if (value < 1 || value > total || seen[static_cast<std::size_t>(value)] != 0)
        {
            ok = false;
            break;
        }
        seen[static_cast<std::size_t>(value)] = 1;
    }
    return ok;
}

bool deque_integrity_ticket(std::size_t capacity, long total)
{
    thread::ticket_deque deque;
    if (!deque.create(capacity))
    {
        return false;
    }
    std::atomic<bool> producing{true};
    dense<long> collected;

    std::thread thief([&deque, &producing, &collected]() {
        thread::ticket batch[8];
        for (;;)
        {
            const bool finishing = !producing.load(std::memory_order_acquire);
            const std::size_t got = deque.steal(batch, 8);
            for (std::size_t i = 0; i < got; ++i)
            {
                collected.push_back(static_cast<long>(batch[i].value_));
            }
            if (finishing && got == 0 && deque.size() <= 0)
            {
                break;
            }
        }
    });

    for (long i = 1; i <= total; ++i)
    {
        while (!deque.push(thread::ticket{static_cast<std::uint64_t>(i)}))
        {
        }
    }
    producing.store(false, std::memory_order_release);
    thief.join();

    for (;;)
    {
        const thread::ticket value = deque.pop();
        if (value.empty())
        {
            break;
        }
        collected.push_back(static_cast<long>(value.value_));
    }

    dense<std::uint8_t> seen;
    seen.resize_fill(static_cast<std::size_t>(total) + 1, 0);
    bool ok = collected.size() == static_cast<std::size_t>(total);
    for (std::size_t i = 0; i < collected.size(); ++i)
    {
        const long value = collected[i];
        if (value < 1 || value > total || seen[static_cast<std::size_t>(value)] != 0)
        {
            ok = false;
            break;
        }
        seen[static_cast<std::size_t>(value)] = 1;
    }
    return ok;
}
} // namespace

int main()
{
    t_begin("test_module_thread_safety");

    t_section(1, "批量窃取完整性");
    {
        t_check("work_deque 容量 4 无重复无丢失", deque_integrity_work(4, 200000));
        t_check("work_deque 容量 16 无重复无丢失", deque_integrity_work(16, 200000));
        t_check("ticket_deque 容量 4 无重复无丢失", deque_integrity_ticket(4, 200000));
        t_check("ticket_deque 容量 16 无重复无丢失", deque_integrity_ticket(16, 200000));
    }

    t_section(2, "Lane 就绪位清位复查");
    {
        thread::mpmc_queue queue;
        queue.create(64);
        std::atomic<std::uint64_t> summary{0};
        const std::uint32_t lane_id = 3;
        std::uint32_t index = thread::mpmc_index_none;

        queue.try_push(101);
        thread::lane_mark_ready(summary, lane_id);
        const bool popped = queue.try_pop(index);
        const bool empty_snapshot = queue.size() == 0;

        queue.try_push(202);
        thread::lane_mark_ready(summary, lane_id);
        if (empty_snapshot)
        {
            thread::lane_clear_ready(summary, lane_id);
            if (queue.size() != 0)
            {
                thread::lane_mark_ready(summary, lane_id);
            }
        }

        const bool bit = (summary.load() >> lane_id) & 1u;
        t_check("弹出成功", popped);
        t_check("判空快照成立", empty_snapshot);
        t_check("复查后位已补回", bit && queue.size() == 1);
    }

    t_section(3, "优先级扫描起点");
    {
        thread::priority_stack stack;
        t_check("创建三层", stack.create(3, 8));
        thread::job background{};
        background.priority_ = static_cast<std::uint8_t>(thread::priority::background);
        thread::job normal{};
        normal.priority_ = static_cast<std::uint8_t>(thread::priority::normal);
        thread::job critical{};
        critical.priority_ = static_cast<std::uint8_t>(thread::priority::critical);

        stack.push(&background);
        t_check("先取低优先级", stack.pop() == &background);
        stack.push(&critical);
        t_check("随后仍取高优先级", stack.pop() == &critical);
        stack.push(&normal);
        t_check("再取普通优先级", stack.pop() == &normal);
        t_check("空栈返回空", stack.pop() == nullptr);
    }

    t_section(4, "资源写写互斥");
    {
        thread::manager mgr;
        thread::manager_config config = thread::profile::server(4);
        config.task_capacity = 16;
        t_check("配置管理器", mgr.configure(config));

        thread::task_body body{};
        body.kind_ = thread::task_body_plain_ctx;
        body.invoke_.plain_ctx_ = &write_body;
        const thread::resource_id resource{0};
        const thread::task_id first =
            mgr.declare("s.w0") | thread::writes(resource) | thread::run(body);
        const thread::task_id second =
            mgr.declare("s.w1") | thread::writes(resource) | thread::run(body);
        const thread::task_id mixed = mgr.declare("s.mixed") | thread::reads(resource) |
                                      thread::writes(resource) | thread::run(body);
        t_check("声明两个写任务", first.valid() && second.valid());
        t_check("声明读写重复任务", mixed.valid());
        mgr.start();

        bool overlap = false;
        bool completed = true;
        for (int round = 0; round < 100; ++round)
        {
            g_inside.store(0);
            g_max_inside.store(0);
            std::barrier sync{3};
            auto fire = [&mgr, &sync](thread::task_id id) {
                sync.arrive_and_wait();
                (void)mgr.activate(id);
            };
            std::thread left(fire, first);
            std::thread right(fire, second);
            sync.arrive_and_wait();
            left.join();
            right.join();
            (void)mgr.drain(2000000);
            if (g_max_inside.load() != 1)
            {
                overlap = true;
            }
            if (mgr.state_of(first) != thread::task_state::completed ||
                mgr.state_of(second) != thread::task_state::completed)
            {
                completed = false;
            }
        }
        t_check("并发激活未出现写写重叠", !overlap);
        t_check("两个任务每轮都完成", completed);

        (void)mgr.activate(mixed);
        const thread::drain_status mixed_status = mgr.drain(2000000);
        t_check("读写重复声明可完成",
                mixed_status == thread::drain_status::complete &&
                    mgr.state_of(mixed) == thread::task_state::completed);
        mgr.stop(thread::stop_mode::drain);
    }

    t_section(5, "零 Worker 排空");
    {
        thread::manager mgr;
        thread::manager_config config = thread::profile::embedded();
        config.job_slot_capacity = 64;
        t_check("配置嵌入式管理器", mgr.configure(config));
        mgr.start();

        std::atomic<int> ran{0};
        std::uint32_t slot = thread::slot_index_none;
        thread::job* item = mgr.take_job(slot);
        t_check("取用任务槽", item != nullptr);
        if (item != nullptr)
        {
            thread::fill_inline_job(item, [&ran]() noexcept { ran.fetch_add(1); });
            t_check("提交成功", mgr.submit(item) != thread::slot_index_none);
            const thread::drain_status status = mgr.drain(1000000);
            t_check("排空完成", status == thread::drain_status::complete);
            t_check("任务已执行", ran.load() == 1);
        }
        mgr.stop(thread::stop_mode::drain);
    }

    t_section(6, "停止与提交计数");
    {
        thread::manager mgr;
        thread::manager_config config = thread::profile::server(2);
        config.job_slot_capacity = 1024;
        t_check("配置管理器", mgr.configure(config));
        mgr.start();

        std::atomic<bool> stop_flag{false};
        std::atomic<std::uint64_t> accepted{0};
        std::atomic<int> producer_count{3};
        auto producer = [&mgr, &stop_flag, &accepted, &producer_count]() {
            while (!stop_flag.load(std::memory_order_acquire))
            {
                std::uint32_t slot = thread::slot_index_none;
                thread::job* item = mgr.take_job(slot);
                if (item == nullptr)
                {
                    continue;
                }
                thread::fill_inline_job(item, []() noexcept {});
                if (mgr.try_submit(item) != thread::slot_index_none)
                {
                    accepted.fetch_add(1);
                }
                else
                {
                    mgr.release_job_local(item);
                }
            }
            producer_count.fetch_sub(1);
        };

        std::thread first(producer);
        std::thread second(producer);
        std::thread third(producer);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        stop_flag.store(true, std::memory_order_release);
        mgr.stop(thread::stop_mode::drain);
        first.join();
        second.join();
        third.join();
        t_check("生产者退出", producer_count.load() == 0);
        t_check("完成数等于接收数", mgr.completed_count() == accepted.load());
    }

    static_assert(std::is_same_v<decltype(std::declval<thread::manager&>().drain(1)),
                                 thread::drain_status>,
                  "drain 必须返回 drain_status");

    return t_end("test_module_thread_safety");
}
