// test_perf_class_pool.cpp - 性能测试: class_pool<T>
#include "test_common.hpp"
#include "include/part/class_pool.hpp"

static volatile uint64_t g_sink = 0;

int main()
{
    t_begin("test_perf_class_pool — 性能测试: class_pool<T> (百万为单位)");

    constexpr uint64_t N = 1'000'000;

    t_section(1, "追加");
    {
        class_pool<int> p;
        p.increase_capacity(N);
        double ms = t_bench_best_ms([&]() {
            p.clear();
            for (uint64_t i = 0; i < N; ++i)
            {
                p.push_back(static_cast<int>(i));
            }
            g_sink += p.size();
        }, 5);
        t_perf("push_back (预分配)", N, ms);
    }

    t_section(2, "访问");
    {
        class_pool<int> p;
        p.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            p.push_back(static_cast<int>(i));
        }

        double ms = t_bench_best_ms([&]() {
            int s = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                s += p[static_cast<size_t>(i)];
            }
            g_sink += static_cast<uint64_t>(s);
        }, 5);
        t_perf("operator[] 顺序读", N, ms);

        ms = t_bench_best_ms([&]() {
            int s = 0;
            p.for_each([&s](int v) { s += v; });
            g_sink += static_cast<uint64_t>(s);
        }, 5);
        t_perf("for_each", N, ms);
    }

    t_section(3, "迭代器");
    {
        class_pool<int> p;
        p.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            p.push_back(static_cast<int>(i));
        }
        double ms = t_bench_best_ms([&]() {
            int s = 0;
            for (auto it = p.begin(); it != p.end(); ++it)
            {
                s += *it;
            }
            g_sink += static_cast<uint64_t>(s);
        }, 5);
        t_perf("迭代器遍历", N, ms);

        for (uint64_t i = 0; i < N; i += 2)
        {
            p.soft_sparse_delete(static_cast<size_t>(i));
        }
        ms = t_bench_best_ms([&]() {
            int s = 0;
            for (auto it = p.begin(); it != p.end(); ++it)
            {
                s += *it;
            }
            g_sink += static_cast<uint64_t>(s);
        }, 5);
        t_perf("迭代器遍历 (稀疏 50% 洞)", N / 2, ms);
    }

    t_section(4, "缓存命中率");
    {
        class_pool<int> p;
        p.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            p.push_back(static_cast<int>(i));
        }
        t_cache_scan("池数据 1e6×4B (4MB)", p.data(), 1'000'000, sizeof(int));
    }

    t_section(5, "填充");
    {
        struct S1 { char c; };
        struct S3 { char c[3]; };

        {
            class_pool<int> p;
            p.increase_capacity(N);
            double ms = t_bench_best_ms([&]() {
                p.clear();
                p.resize_fill(N, 7);
                g_sink += p.size();
            }, 5);
            t_throughput("resize_fill 4B", N * sizeof(int), N, ms);
        }
        {
            class_pool<S1> p;
            p.increase_capacity(N);
            double ms = t_bench_best_ms([&]() {
                p.clear();
                p.resize_fill(N, S1{'x'});
                g_sink += p.size();
            }, 5);
            t_throughput("resize_fill 1B", N * sizeof(S1), N, ms);
        }
        {
            class_pool<S3> p;
            p.increase_capacity(N);
            double ms = t_bench_best_ms([&]() {
                p.clear();
                p.resize_fill(N, S3{'a', 'b', 'c'});
                g_sink += p.size();
            }, 5);
            t_throughput("resize_fill 3B", N * sizeof(S3), N, ms);
        }
        {
            class_pool<int> p;
            p.increase_capacity(N);
            for (uint64_t i = 0; i < N; ++i)
            {
                p.push_back(0);
            }
            double ms = t_bench_best_ms([&]() {
                p.fill_bulk(7, 0, N);
                g_sink += p[0];
            }, 5);
            t_throughput("fill_bulk 4B", N * sizeof(int), N, ms);
        }
        {
            class_pool<int> p;
            p.increase_capacity(N);
            double ms = t_bench_best_ms([&]() {
                p.clear();
                p.append_n(N, 7);
                g_sink += p.size();
            }, 5);
            t_throughput("append_n 4B 对照", N * sizeof(int), N, ms);
        }
    }

    return t_end("class_pool 性能 ");
}
