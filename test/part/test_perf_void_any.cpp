// test_perf_void_any.cpp - 性能测试: void_any
#include "test_common.hpp"
#include "include/part/type/void_any.hpp"
#include <string>

struct small_pod
{
    int a;
    double b;
};

struct big_pod
{
    char data[128];
};

static volatile uint64_t g_sink = 0;

int main()
{
    t_begin("test_perf_void_any — 性能测试: void_any (百万为单位)");

    constexpr uint64_t N = 1'000'000;

    t_section(1, "构造与析构");
    {
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                void_any a(small_pod{static_cast<int>(i), 1.0});
                acc += a.has_value() ? 1 : 0;
            }
            g_sink = acc;
        }, 5);
        t_perf("构造+析构 (小对象)", N, ms);

        ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                void_any a(big_pod{});
                acc += a.has_value() ? 1 : 0;
            }
            g_sink = acc;
        }, 5);
        t_perf("构造+析构 (大对象)", N, ms);
    }

    t_section(2, "取值");
    {
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                void_any a(small_pod{static_cast<int>(i), 1.0});
                small_pod* p = a.get_ptr<small_pod>();
                if (p) { acc += static_cast<uint64_t>(p->a); }
            }
            g_sink = acc;
        }, 5);
        t_perf("get_ptr", N, ms);

        ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                void_any a(small_pod{static_cast<int>(i), 1.0});
                small_pod* p = a.fast_get_ptr<small_pod>();
                if (p) { acc += static_cast<uint64_t>(p->a); }
            }
            g_sink = acc;
        }, 5);
        t_perf("fast_get_ptr", N, ms);
    }

    t_section(3, "移动与拷贝");
    {
        constexpr uint64_t M = 200'000;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < M; ++i)
            {
                void_any a(small_pod{1, 1.0});
                void_any b(std::move(a));
                acc += b.has_value() ? 1 : 0;
            }
            g_sink = acc;
        }, 5);
        t_perf("移动构造", M, ms);

        ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < M; ++i)
            {
                void_any a(small_pod{1, 1.0});
                void_any b(a);
                acc += b.has_value() ? 1 : 0;
            }
            g_sink = acc;
        }, 5);
        t_perf("拷贝构造", M, ms);
    }

    t_section(4, "非平凡类型");
    {
        constexpr uint64_t M = 500'000;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < M; ++i)
            {
                void_any a(std::string("payload"));
                auto* s = a.get_ptr<std::string>();
                if (s) { acc += s->size(); }
            }
            g_sink = acc;
        }, 5);
        t_perf("std::string 存取", M, ms);
    }

    return t_end("void_any 性能 ");
}
