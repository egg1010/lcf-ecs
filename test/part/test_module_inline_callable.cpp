// test_module_inline_callable.cpp - 模块功能测试: 类型擦除可调用体
#include "test_common.hpp"
#include "include/part/type/inline_callable.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
struct tracked
{
    int* counter_{nullptr};
    bool alive_{true};

    explicit tracked(int* counter) noexcept : counter_(counter) {}

    tracked(tracked&& other) noexcept : counter_(other.counter_), alive_(other.alive_)
    {
        other.alive_ = false;
    }

    tracked(const tracked&) = delete;
    tracked& operator=(const tracked&) = delete;

    ~tracked()
    {
        if (alive_ && counter_ != nullptr)
        {
            ++*counter_;
        }
    }

    void operator()() noexcept
    {
        if (counter_ != nullptr)
        {
            ++*counter_;
        }
    }
};

struct large_capture
{
    unsigned char data[256]{};
    int* value_{nullptr};

    void operator()() noexcept
    {
        ++*value_;
    }
};
} // namespace

int main()
{
    t_begin("test_module_inline_callable — 模块功能测试: inline_callable");

    t_section(1, "小对象内联");
    {
        int value = 0;
        inline_callable<> callable;
        t_check("绑定成功", callable.bind([&value]() noexcept { value = 42; }));
        t_check("内联存储", !callable.heap_allocated());
        t_check("可调用", static_cast<bool>(callable));
        callable();
        t_check("执行结果正确", value == 42);
    }

    t_section(2, "大对象溢出到堆");
    {
        int value = 0;
        large_capture capture;
        capture.value_ = &value;
        inline_callable<> callable;
        t_check("绑定成功", callable.bind(capture));
        t_check("堆存储", callable.heap_allocated());
        callable();
        t_check("执行结果正确", value == 1);
    }

    t_section(3, "析构一次");
    {
        int destroyed = 0;
        inline_callable<> callable;
        t_check("绑定带析构对象", callable.bind(tracked{&destroyed}));
        t_check("堆存储", callable.heap_allocated());
        t_check("绑定期未析构", destroyed == 0);
        callable.reset();
        t_check("reset 析构一次", destroyed == 1);
        t_check("reset 后为空", !static_cast<bool>(callable));
        callable();
        t_check("空调用安全", true);
    }

    t_section(4, "移动");
    {
        int value = 0;
        inline_callable<> first;
        (void)first.bind([&value]() noexcept { ++value; });
        inline_callable<> second{std::move(first)};
        t_check("源已清空", !static_cast<bool>(first));
        t_check("目标可调用", static_cast<bool>(second));
        second();
        t_check("移动后执行", value == 1);
    }

    t_section(5, "调用方存储");
    {
        alignas(16) unsigned char storage[64]{};
        callable_invoke_fn invoke = nullptr;
        callable_destroy_fn destroy = nullptr;
        int value = 0;
        const callable_placement placement = callable_bind<64, 16>(
            storage, [&value]() noexcept { ++value; }, invoke, destroy);
        t_check("自动选择内联", placement == callable_placement::inline_storage);
        t_check("thunk 可用", invoke != nullptr && destroy != nullptr);
        invoke(storage);
        t_check("执行结果正确", value == 1);
        destroy(storage);
    }

    return t_end("test_module_inline_callable");
}
