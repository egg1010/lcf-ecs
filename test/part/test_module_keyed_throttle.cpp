// test_module_keyed_throttle.cpp - 模块功能测试: 键控限流器
#include "test_common.hpp"
#include "include/part/keyed_throttle.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

int main()
{
    t_begin("test_module_keyed_throttle — 模块功能测试: keyed_throttle");

    t_section(1, "窗口内限流");
    {
        keyed_throttle<8> throttle;
        t_check("第 1 次允许", throttle.allow(100, 1, 2));
        t_check("第 2 次允许", throttle.allow(100, 1, 2));
        t_check("第 3 次拒绝", !throttle.allow(100, 1, 2));
    }

    t_section(2, "键隔离");
    {
        keyed_throttle<8> throttle;
        t_check("键 A 允许", throttle.allow(1, 5, 1));
        t_check("键 B 独立允许", throttle.allow(2, 5, 1));
        t_check("键 A 第二次拒绝", !throttle.allow(1, 5, 1));
        t_check("键 B 第二次拒绝", !throttle.allow(2, 5, 1));
    }

    t_section(3, "窗口切换");
    {
        keyed_throttle<8> throttle;
        t_check("窗口 1 允许", throttle.allow(7, 1, 1));
        t_check("窗口 1 拒绝", !throttle.allow(7, 1, 1));
        t_check("窗口 2 重新允许", throttle.allow(7, 2, 1));
    }

    t_section(4, "上限为零");
    {
        keyed_throttle<4> throttle;
        t_check("上限 0 恒拒绝", !throttle.allow(1, 1, 0));
        t_check("上限 0 不计数", !throttle.allow(1, 1, 0));
    }

    t_section(5, "清零");
    {
        keyed_throttle<4> throttle;
        (void)throttle.allow(9, 1, 1);
        t_check("清零前拒绝", !throttle.allow(9, 1, 1));
        throttle.clear();
        t_check("清零后允许", throttle.allow(9, 1, 1));
    }

    t_section(6, "并发限流不超限");
    {
        keyed_throttle<8> throttle;
        std::atomic<int> allowed{0};
        auto worker = [&]() {
            for (int i = 0; i < 100; ++i)
            {
                if (throttle.allow(42, 1, 10))
                {
                    allowed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        };
        std::thread t1(worker);
        std::thread t2(worker);
        std::thread t3(worker);
        std::thread t4(worker);
        t1.join();
        t2.join();
        t3.join();
        t4.join();
        t_check("并发窗口允许次数等于上限", allowed.load(std::memory_order_relaxed) == 10);
    }

    return t_end("test_module_keyed_throttle");
}
