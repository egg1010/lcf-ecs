// test_perf_dense.cpp - 性能测试: dense<T>
#include "test_common.hpp"
#include "include/part/dense.hpp"

static volatile uint64_t g_sink = 0;

int main()
{
    t_begin("test_perf_dense — 性能测试: dense<T> (百万为单位)");

    constexpr uint64_t N = 1'000'000;

    t_section(1, "追加 (1e6)");
    {
        double ms = t_bench_best_ms([&]() {
            dense<int> d;
            d.increase_capacity(N);
            for (uint64_t i = 0; i < N; ++i)
            {
                d.push_back(static_cast<int>(i));
            }
            g_sink += d.size();
        }, 5);
        t_perf("push_back (预分配)", N, ms);

        ms = t_bench_best_ms([&]() {
            dense<int> d;
            for (uint64_t i = 0; i < N; ++i)
            {
                d.push_back(static_cast<int>(i));
            }
            g_sink += d.size();
        }, 5);
        t_perf("push_back (自动扩容)", N, ms);

        ms = t_bench_best_ms([&]() {
            dense<int> d;
            d.increase_capacity(N);
            for (uint64_t i = 0; i < N; ++i)
            {
                d.emplace_back(static_cast<int>(i));
            }
            g_sink += d.size();
        }, 5);
        t_perf("emplace_back (预分配)", N, ms);
    }

    t_section(2, "访问 (1e6)");
    {
        dense<int> d(N, 7);

        double ms = t_bench_best_ms([&]() {
            int s = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                s += d[i];
            }
            g_sink += static_cast<uint64_t>(s);
        }, 5);
        t_perf("operator[] 顺序读", N, ms);

        ms = t_bench_best_ms([&]() {
            int s = 0;
            d.for_each([&s](int v) { s += v; });
            g_sink += static_cast<uint64_t>(s);
        }, 5);
        t_perf("for_each 顺序读", N, ms);

        ms = t_bench_best_ms([&]() {
            int s = 0;
            const int* p = d.data();
            for (uint64_t i = 0; i < N; ++i)
            {
                s += p[i];
            }
            g_sink += static_cast<uint64_t>(s);
        }, 5);
        t_perf("data() 裸指针读", N, ms);

        ms = t_bench_best_ms([&]() {
            uint64_t state = 0x9E3779B97F4A7C15ULL;
            int s = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                s += d[state % N];
            }
            g_sink += static_cast<uint64_t>(s);
        }, 5);
        t_perf("operator[] 随机读", N, ms);
    }

    t_section(3, "批量与修改 (1e6)");
    {
        double ms = t_bench_best_ms([&]() {
            dense<int> d;
            d.append_n(N, 1);
            g_sink += d.size();
        }, 5);
        t_perf("append_n", N, ms);

        dense<int> src;
        src.append_n(N, 5);
        ms = t_bench_best_ms([&]() {
            dense<int> d;
            d.append_bulk(src.data(), src.size());
            g_sink += d.size();
        }, 5);
        t_perf("append_bulk", N, ms);

        dense<int> back_dst(N, 1);
        ms = t_bench_best_ms([&]() {
            dense<int> d(N, 1);
            for (uint64_t i = 0; i < 1'000'000; ++i)
            {
                d.push_back(static_cast<int>(i));
                d.pop_back();
            }
            g_sink += d.size();
        }, 5);
        t_perf("push_back+pop_back", 1'000'000, ms);
    }

    t_section(4, "插入与删除 (1e5)");
    {
        constexpr uint64_t M = 100'000;

        double ms = t_bench_best_ms([&]() {
            dense<int> d;
            d.increase_capacity(M + 1);
            for (uint64_t i = 0; i < M; ++i)
            {
                d.push_back(static_cast<int>(i));
            }
            for (uint64_t i = 0; i < 1000; ++i)
            {
                d.insert(d.begin() + (d.size() / 2), 0);
            }
            g_sink += d.size();
        }, 3);
        t_perf("insert 中间位置", 1000, ms);

        ms = t_bench_best_ms([&]() {
            dense<int> d;
            d.increase_capacity(M);
            for (uint64_t i = 0; i < M; ++i)
            {
                d.push_back(static_cast<int>(i));
            }
            for (uint64_t i = 0; i < M / 2; ++i)
            {
                d.erase(d.begin() + (d.size() / 2));
            }
            g_sink += d.size();
        }, 3);
        t_perf("erase 中间位置", M / 2, ms);
    }

    t_section(5, "缓存行为");
    {
        dense<int> d(N, 1);
        t_cache_scan("顺序扫描 1e6×4B (4MB)", d.data(), 1'000'000, sizeof(int));

        dense<int> small(16'384, 1);
        t_cache_scan("顺序扫描 16K×4B (64KB)", small.data(), 16'384, sizeof(int));

        dense<float> vec(262'144, 1.0f);
        t_cache_scan("顺序扫描 256K×4B (1MB)", vec.data(), 262'144, sizeof(float));
    }

    return t_end("dense 性能 ");
}
