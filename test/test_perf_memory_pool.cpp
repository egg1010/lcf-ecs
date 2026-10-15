// test_perf_memory_pool.cpp - 性能测试: list_memory_pool
#include "test_common.hpp"
#include "include/part/memory/list_memory_pool.hpp"

using memory::list_memory_pool;

static volatile uint64_t g_sink = 0;

int main()
{
    t_begin("test_perf_memory_pool — 性能测试: list_memory_pool (百万为单位)");

    constexpr uint64_t N = 1'000'000;

    t_section(1, "分配与释放");
    {
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.allocate(64);
                pool.deallocate(p);
            }
        }, 5);
        t_perf("allocate+deallocate 64B", N, ms);
    }

    t_section(2, "小对象");
    {
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.allocate(16);
                pool.deallocate(p);
            }
        }, 5);
        t_perf("allocate+deallocate 16B", N, ms);
    }

    t_section(3, "清零分配");
    {
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < 200'000; ++i)
            {
                void* p = pool.allocate_zeroed(64);
                acc += p ? 1 : 0;
                pool.deallocate(p);
            }
            g_sink = acc;
        }, 5);
        t_perf("allocate_zeroed 64B", 200'000, ms);
    }

    t_section(4, "大批量持有");
    {
        constexpr uint64_t M = 100'000;
        list_memory_pool pool;
        static void* ptrs[M];
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < M; ++i)
            {
                ptrs[i] = pool.allocate(32);
            }
            for (uint64_t i = 0; i < M; ++i)
            {
                pool.deallocate(ptrs[i]);
            }
        }, 5);
        t_perf("批量 alloc/free 32B", M, ms);
    }

    t_section(5, "构造/析构");
    {
        struct node
        {
            int a;
            double b;
        };
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < 500'000; ++i)
            {
                node* n = pool.construct<node>();
                pool.destroy(n);
            }
        }, 5);
        t_perf("construct+destroy", 500'000, ms);
    }

    return t_end("list_memory_pool 性能 ");
}
