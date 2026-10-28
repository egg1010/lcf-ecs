// test_perf_sort.cpp - 性能测试: 分级排序
#include "test_common.hpp"
#include "include/part/tiered_sort.hpp"
#include "include/part/dense.hpp"

static volatile uint64_t g_sink = 0;

static void fill_random(dense<int>& v, uint64_t n, uint64_t seed)
{
    v.clear();
    v.increase_capacity(n);
    uint64_t state = seed;
    for (uint64_t i = 0; i < n; ++i)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        v.emplace_back(static_cast<int>(state % 1000000));
    }
}

int main()
{
    t_begin("test_perf_sort — 性能测试: 分级排序 (百万为单位)");

    constexpr uint64_t N = 1'000'000;

    t_section(1, "随机数据");
    {
        dense<int> v;
        double ms = t_bench_best_ms([&]() {
            fill_random(v, N, 0x12345);
            sort(v.data(), v.size());
            g_sink += static_cast<uint64_t>(v.front());
        }, 3);
        t_perf("sort 随机 1e6", N, ms);

        ms = t_bench_best_ms([&]() {
            fill_random(v, N, 0x12345);
            pdqsort(v.data(), v.size(), [](int a, int b) { return a < b; });
            g_sink += static_cast<uint64_t>(v.front());
        }, 3);
        t_perf("pdqsort 随机 1e6", N, ms);
    }

    t_section(2, "有序数据");
    {
        dense<int> v;
        v.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            v.emplace_back(static_cast<int>(i));
        }
        double ms = t_bench_best_ms([&]() {
            dense<int> copy = v;
            sort(copy.data(), copy.size());
            g_sink += static_cast<uint64_t>(copy.front());
        }, 3);
        t_perf("sort 已有序 1e6", N, ms);
    }

    t_section(3, "小规模");
    {
        dense<int> v;
        constexpr uint64_t M = 10'000;
        double ms = t_bench_best_ms([&]() {
            v.clear();
            uint64_t state = 777;
            for (uint64_t i = 0; i < M; ++i)
            {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                v.emplace_back(static_cast<int>(state % 1000));
            }
            sort(v.data(), v.size());
            g_sink += static_cast<uint64_t>(v.front());
        }, 20);
        t_perf("sort 1e4 (多轮)", M, ms);
    }

    t_section(4, "间接排序");
    {
        dense<int> vals;
        dense<size_t> idx;
        double ms = t_bench_best_ms([&]() {
            fill_random(vals, N, 0xABC);
            idx.clear();
            idx.increase_capacity(N);
            for (uint64_t i = 0; i < N; ++i) { idx.emplace_back(static_cast<size_t>(i)); }
            sort_indices(idx.data(), vals.data(), N);
            g_sink += static_cast<uint64_t>(vals[idx[0]]);
        }, 3);
        t_perf("sort_indices 1e6", N, ms);
    }

    return t_end("sort 性能 ");
}
