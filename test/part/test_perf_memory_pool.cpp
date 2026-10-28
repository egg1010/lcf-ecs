// test_perf_memory_pool.cpp - 性能测试: memory_pool / list_memory_pool
#include "test_common.hpp"
#include "include/part/memory/memory_pool.hpp"
#include "include/part/memory/list_memory_pool.hpp"
#include "include/part/memory/layered_allocator.hpp"

using memory::memory_pool;
using memory::list_memory_pool;
using memory::layered_allocator;

static volatile uint64_t g_sink = 0;

namespace {

struct node
{
    int a;
    double b;
};

void bench_list_pool() noexcept
{
    constexpr uint64_t N = 1'000'000;

    t_sub("list_memory_pool");

    t_section(1, "分配与软删除");
    {
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.acquire(64);
                pool.soft_delete(p);
            }
        }, 5);
        t_perf("acquire+soft_delete 64B", N, ms);
    }

    t_section(2, "编译期大小接口");
    {
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.acquire_sized<64>();
                pool.soft_delete_sized<64>(p);
            }
        }, 5);
        t_perf("acquire_sized<64> + sized_delete", N, ms);
    }

    t_section(3, "硬删除");
    {
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.acquire(128);
                pool.hard_delete(p);
            }
        }, 5);
        t_perf("acquire+hard_delete 128B", N, ms);
    }

    t_section(4, "清零分配");
    {
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < 200'000; ++i)
            {
                void* p = pool.acquire_zeroed(64);
                acc += p ? 1 : 0;
                pool.soft_delete(p);
            }
            g_sink = acc;
        }, 5);
        t_perf("acquire_zeroed 64B", 200'000, ms);
    }

    t_section(5, "大块软删除复用");
    {
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < 200'000; ++i)
            {
                void* p = pool.acquire(8192);
                pool.soft_delete(p);
            }
        }, 5);
        t_perf("acquire+soft_delete 8192B", 200'000, ms);
    }

    t_section(6, "大批量持有");
    {
        constexpr uint64_t M = 100'000;
        static void* ptrs[M];
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < M; ++i)
            {
                ptrs[i] = pool.acquire(32);
            }
            for (uint64_t i = 0; i < M; ++i)
            {
                pool.soft_delete(ptrs[i]);
            }
        }, 5);
        t_perf("批量 acquire/soft_delete 32B", M, ms);
    }

    t_section(7, "对象构造/析构");
    {
        list_memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < 500'000; ++i)
            {
                node* n = pool.create<node>();
                pool.dispose(n);
                pool.soft_delete(n);
            }
        }, 5);
        t_perf("create+dispose+soft_delete", 500'000, ms);
    }
}

void bench_memory_pool() noexcept
{
    constexpr uint64_t N = 1'000'000;

    t_sub("memory_pool");

    t_section(8, "分配与软删除");
    {
        memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.acquire(64);
                pool.soft_delete(p);
            }
        }, 5);
        t_perf("acquire+soft_delete 64B", N, ms);
    }

    t_section(9, "编译期大小接口");
    {
        memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.acquire_sized<64>();
                pool.soft_delete_sized<64>(p);
            }
        }, 5);
        t_perf("acquire_sized<64> + sized_delete", N, ms);
    }

    t_section(10, "硬删除");
    {
        memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.acquire(64);
                pool.hard_delete(p);
            }
        }, 5);
        t_perf("acquire+hard_delete 64B", N, ms);
    }

    t_section(11, "清零分配");
    {
        memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < 200'000; ++i)
            {
                void* p = pool.acquire_zeroed(64);
                acc += p ? 1 : 0;
                pool.soft_delete(p);
            }
            g_sink = acc;
        }, 5);
        t_perf("acquire_zeroed 64B", 200'000, ms);
    }

    t_section(12, "对齐分配");
    {
        memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < 200'000; ++i)
            {
                void* p = pool.acquire_aligned(64, 64);
                pool.soft_delete(p);
            }
        }, 5);
        t_perf("acquire_aligned 64B/64", 200'000, ms);
    }

    t_section(13, "大批量持有");
    {
        constexpr uint64_t M = 100'000;
        static void* ptrs[M];
        memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < M; ++i)
            {
                ptrs[i] = pool.acquire(32);
            }
            for (uint64_t i = 0; i < M; ++i)
            {
                pool.soft_delete(ptrs[i]);
            }
        }, 5);
        t_perf("批量 acquire/soft_delete 32B", M, ms);
    }

    t_section(14, "对象构造/析构");
    {
        memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < 500'000; ++i)
            {
                node* n = pool.create<node>();
                pool.dispose(n);
                pool.soft_delete(n);
            }
        }, 5);
        t_perf("create+dispose+soft_delete", 500'000, ms);
    }

    t_section(15, "多档批量软删除");
    {
        constexpr uint64_t M = 100'000;
        constexpr size_t sizes[] = { 16, 32, 64, 128, 256 };
        static void* ptrs[M];
        memory_pool pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < M; ++i)
            {
                ptrs[i] = pool.acquire(sizes[i % 5]);
            }
            for (uint64_t i = 0; i < M; ++i)
            {
                pool.soft_delete(ptrs[i]);
            }
        }, 5);
        t_perf("批量多档 acquire/soft_delete", M, ms);
    }

    t_section(16, "批量软删除");
    {
        constexpr uint64_t M = 100'000;
        static void* ptrs[M];
        memory_pool pool;
        double best = 1e18;
        for (size_t repeat = 0; repeat < 5; ++repeat)
        {
            for (uint64_t i = 0; i < M; ++i)
            {
                ptrs[i] = pool.acquire(32);
            }
            timer t;
            for (uint64_t i = 0; i < M; ++i)
            {
                pool.soft_delete(ptrs[i]);
            }
            const double ms = t.elapsed_milliseconds();
            if (ms < best)
            {
                best = ms;
            }
        }
        t_perf("soft_delete 32B", M, best);
    }
}

void bench_layered_allocator() noexcept
{
    constexpr uint64_t N = 1'000'000;

    t_sub("layered_allocator");

    t_section(17, "分配与软删除");
    {
        layered_allocator pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.acquire(64);
                pool.soft_delete(p);
            }
        }, 5);
        t_perf("acquire+soft_delete 64B", N, ms);
    }

    t_section(18, "编译期大小接口");
    {
        layered_allocator pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.acquire_sized<64>();
                pool.soft_delete_sized<64>(p);
            }
        }, 5);
        t_perf("acquire_sized<64> + sized_delete", N, ms);
    }

    t_section(19, "对齐分配");
    {
        layered_allocator pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < 200'000; ++i)
            {
                void* p = pool.acquire_aligned(64, 64);
                pool.soft_delete(p);
            }
        }, 5);
        t_perf("acquire_aligned 64B/64", 200'000, ms);
    }

    t_section(20, "大块软删除复用");
    {
        layered_allocator pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < 200'000; ++i)
            {
                void* p = pool.acquire(8192);
                pool.soft_delete(p);
            }
        }, 5);
        t_perf("acquire+soft_delete 8192B", 200'000, ms);
    }

    t_section(21, "归属与大小查询");
    {
        constexpr uint64_t M = 100'000;
        static void* ptrs[M];
        layered_allocator pool;
        for (uint64_t i = 0; i < M; ++i)
        {
            ptrs[i] = pool.acquire(64);
        }
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = ptrs[i % M];
                acc += pool.owns(p) ? 1 : 0;
                acc += pool.size_of(p);
            }
            g_sink = acc;
        }, 5);
        t_perf("owns+size_of 64B", N, ms);
        for (uint64_t i = 0; i < M; ++i)
        {
            pool.hard_delete(ptrs[i]);
        }
    }

    t_section(22, "硬删除");
    {
        layered_allocator pool;
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                void* p = pool.acquire(64);
                pool.hard_delete(p);
            }
        }, 5);
        t_perf("acquire+hard_delete 64B", N, ms);
    }
}

} // namespace

int main()
{
    t_begin("test_perf_memory_pool — 性能测试: memory_pool / list_memory_pool / layered_allocator (百万为单位)");

    bench_list_pool();
    bench_memory_pool();
    bench_layered_allocator();

    return t_end("memory_pool 性能 ");
}
