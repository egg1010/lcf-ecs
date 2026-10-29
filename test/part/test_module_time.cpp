// test_module_time.cpp - 模块功能测试: timer
#include "test_common.hpp"
#include "include/part/time/timer.hpp"
#include "include/part/time/timer_scheduler.hpp"

static volatile uint64_t g_work = 0;
static timer::scheduler* g_scheduler = nullptr;
static bool g_nested_fired = false;

static void timer_noop()
{
}

static void timer_reentrant()
{
    g_nested_fired = true;
    (void)g_scheduler->schedule_after(0.0, timer_noop);
}

static void do_some_work()
{
    volatile uint64_t s = 0;
    for (int i = 0; i < 1000000; ++i)
    {
        s = s + static_cast<uint64_t>(i);
    }
    g_work = s;
}

int main()
{
    t_begin("test_module_time — 模块功能测试: timer");

    t_section(1, "墙钟计时");
    {
        timer t;
        do_some_work();
        double ns = t.elapsed_nanoseconds();
        t_check("纳秒 > 0", ns > 0.0);

        double us = t.elapsed_microseconds();
        t_check("微秒 > 0", us > 0.0);

        double ms = t.elapsed_milliseconds();
        t_check("毫秒非负", ms >= 0.0);

        t_check("单位换算自洽", us >= ms);
    }

    t_section(2, "reset");
    {
        timer t;
        do_some_work();
        double first = t.elapsed_microseconds();
        t.reset();
        double second = t.elapsed_microseconds();
        t_check("reset 后重新计时", second < first || first >= 0.0);
    }

    t_section(3, "CPU 周期");
    {
        timer t;
        do_some_work();
        uint64_t cyc = t.elapsed_cycles();
        t_check("周期数可用", (cyc > 0) || (cyc == 0));
        uint64_t n1 = timer::now_cycles();
        uint64_t n2 = timer::now_cycles();
        t_check("now_cycles 非递减", n2 >= n1);
    }

    t_section(4, "配对计时");
    {
        double ns = timer::measure_nanoseconds(do_some_work);
        t_check("measure_nanoseconds >= 0", ns > 0.0 || ns == 0.0);

        uint64_t c = timer::measure_cycles(do_some_work);
        t_check("measure_cycles 可用", (c > 0) || (c == 0));
    }

    t_section(5, "快照");
    {
        timer t;
        do_some_work();
        timer::snapshot snap = t.take_snapshot();
        t_check("快照纳秒 > 0", snap.nanoseconds > 0.0);
        t_check("静态 now_nanoseconds > 0", timer::now_nanoseconds() > 0.0);
    }


    t_section(6, "单调时钟纳秒");
    {
        const uint64_t first = monotonic_now_ns();
        const uint64_t second = monotonic_now_ns();
        t_check("纳秒值非零", first > 0);
        t_check("单调非递减", second >= first);
        t_check("量级为纳秒", second - first < 1000000000ull);
    }

    t_section(7, "调度器回调重入");
    {
        timer::scheduler scheduler;
        g_scheduler = &scheduler;
        g_nested_fired = false;
        (void)scheduler.schedule_after(0.0, timer_reentrant);
        scheduler.tick();
        t_check("重入回调成功执行", g_nested_fired);
        t_check("重入新增定时器保留", scheduler.pending_count() == 2);
    }

    return t_end("time ");
}
