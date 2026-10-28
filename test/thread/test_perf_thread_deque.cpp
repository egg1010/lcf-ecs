// test_perf_thread_deque.cpp - 模块性能测试: thread 队列与优先级
// 输出单位: 百万次/秒 (Mops/s)
#include "test_common.hpp"
#include "include/thread/manager.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace
{
constexpr std::uint64_t perf_items = 1000000;

thread::job* item_of(std::uint64_t value) noexcept
{
    return reinterpret_cast<thread::job*>(static_cast<std::uintptr_t>(value));
}

double push_pop_work_deque()
{
    thread::work_deque deque;
    deque.create(1u << 21);
    timer t;
    for (std::uint64_t i = 1; i <= perf_items; ++i)
    {
        while (!deque.push(item_of(i)))
        {
        }
    }
    for (std::uint64_t i = 0; i < perf_items; ++i)
    {
        while (deque.pop() == nullptr)
        {
        }
    }
    return t.elapsed_milliseconds();
}

double push_pop_ticket_deque()
{
    thread::ticket_deque deque;
    deque.create(1u << 21);
    timer t;
    for (std::uint64_t i = 1; i <= perf_items; ++i)
    {
        while (!deque.push(thread::ticket{i}))
        {
        }
    }
    for (std::uint64_t i = 0; i < perf_items; ++i)
    {
        while (deque.pop().empty())
        {
        }
    }
    return t.elapsed_milliseconds();
}

double steal_concurrent()
{
    thread::work_deque deque;
    deque.create(1024);
    std::atomic<bool> producing{true};
    std::atomic<std::uint64_t> stolen{0};

    timer t;
    std::thread thief([&deque, &producing, &stolen]() {
        thread::job* batch[64];
        for (;;)
        {
            const bool finishing = !producing.load(std::memory_order_acquire);
            const std::size_t got = deque.steal(batch, 64);
            stolen.fetch_add(got, std::memory_order_relaxed);
            if (finishing && got == 0 && deque.size() <= 0)
            {
                break;
            }
        }
    });
    for (std::uint64_t i = 1; i <= perf_items; ++i)
    {
        while (!deque.push(item_of(i)))
        {
        }
    }
    producing.store(false, std::memory_order_release);
    thief.join();
    const double ms = t.elapsed_milliseconds();
    return ms;
}

double push_pop_priority()
{
    thread::priority_stack stack;
    stack.create(3, 1024);
    thread::job jobs[3]{};
    jobs[0].priority_ = static_cast<std::uint8_t>(thread::priority::background);
    jobs[1].priority_ = static_cast<std::uint8_t>(thread::priority::normal);
    jobs[2].priority_ = static_cast<std::uint8_t>(thread::priority::critical);

    timer t;
    for (std::uint64_t i = 0; i < perf_items; ++i)
    {
        while (!stack.push(&jobs[i % 3]))
        {
        }
        while (stack.pop() == nullptr)
        {
        }
    }
    return t.elapsed_milliseconds();
}
} // namespace

int main()
{
    t_begin("test_perf_thread_deque");

    t_section(1, "work_deque 单线程");
    {
        const double ms = t_bench_best_ms(push_pop_work_deque, 3);
        t_perf("push+pop", perf_items * 2, ms);
    }

    t_section(2, "work_deque 并发窃取");
    {
        const double ms = t_bench_best_ms(steal_concurrent, 3);
        t_perf("push+steal", perf_items * 2, ms);
    }

    t_section(3, "ticket_deque 单线程");
    {
        const double ms = t_bench_best_ms(push_pop_ticket_deque, 3);
        t_perf("push+pop", perf_items * 2, ms);
    }

    t_section(4, "priority_stack 三层");
    {
        const double ms = t_bench_best_ms(push_pop_priority, 3);
        t_perf("push+pop", perf_items * 2, ms);
    }

    return t_end("test_perf_thread_deque");
}
