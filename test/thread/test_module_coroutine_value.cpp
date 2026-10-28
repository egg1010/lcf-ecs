// test_module_coroutine_value.cpp - 模块测试: 带值返回的 coroutine
//   GCC PR97433: await_resume / return_value / yield_value 可能被重排,
//   带 co_return 值的 Task 在高风险模式下可能读到未写入的值
#include "test_common.hpp"
#include "include/thread/execution/coroutine.hpp"

#include <coroutine>

namespace
{

thread::task<int> produce(int v)
{
    co_return v;
}

thread::task<int> produce_after_suspend(int v)
{
    co_await std::suspend_never{};
    co_return v;
}

thread::task<int> produce_after_two_suspends(int v)
{
    co_await std::suspend_never{};
    co_await std::suspend_never{};
    co_return v;
}

} // namespace

int main()
{
    t_begin("test_module_coroutine_value — 带值 coroutine 的正确性");

    t_section(1, "直接 co_return");
    {
        auto t = produce(42);
        t.start();
        t_check("已完成", t.done());
        t_check("value == 42", t.value() == 42);
    }

    t_section(2, "一次挂起后 co_return");
    {
        auto t = produce_after_suspend(7);
        t.start();
        t_check("已完成", t.done());
        t_check("value == 7", t.value() == 7);
    }

    t_section(3, "两次挂起后 co_return");
    {
        auto t = produce_after_two_suspends(-3);
        t.start();
        t_check("已完成", t.done());
        t_check("value == -3", t.value() == -3);
    }

    t_section(4, "多次实例化取值一致");
    {
        bool all_ok = true;
        for (int i = 0; i < 64; ++i)
        {
            auto t = produce(i * 3);
            t.start();
            if (!t.done() || t.value() != i * 3)
            {
                all_ok = false;
                break;
            }
        }
        t_check("64 次 co_return 取值一致", all_ok);
    }

    return t_end("coroutine_value ");
}
