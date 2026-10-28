// test_perf_multi_block_bitmask.cpp - 性能测试: multi_block_bitmask
#include "test_common.hpp"
#include "include/part/multi_block_bitmask.hpp"

static volatile uint64_t g_sink = 0;

int main()
{
    t_begin("test_perf_multi_block_bitmask — 性能测试: multi_block_bitmask (百万为单位)");

    constexpr uint64_t N = 1'000'000;
    constexpr uint32_t SLOTS = 4096;

    t_section(1, "写入");
    {
        multi_block_bitmask bm;
        bm.resize_entities(SLOTS);
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                bm.set_bit(static_cast<uint32_t>(i % SLOTS), 0, static_cast<uint32_t>(i & 63));
            }
            g_sink += bm.get_block(0, 0);
        }, 5);
        t_perf("set_bit", N, ms);

        ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                bm.clear_bit(static_cast<uint32_t>(i % SLOTS), 0, static_cast<uint32_t>(i & 63));
            }
        }, 5);
        t_perf("clear_bit", N, ms);
    }

    t_section(2, "读取");
    {
        multi_block_bitmask bm;
        bm.resize_entities(SLOTS);
        for (uint32_t s = 0; s < SLOTS; ++s)
        {
            bm.set_bit(s, 0, s & 63);
        }

        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                acc += bm.test_bit(static_cast<uint32_t>(i % SLOTS), 0, static_cast<uint32_t>(i & 63)) ? 1 : 0;
            }
            g_sink = acc;
        }, 5);
        t_perf("test_bit", N, ms);

        ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                acc += bm.get_block(static_cast<uint32_t>(i % SLOTS), 0);
            }
            g_sink = acc;
        }, 5);
        t_perf("get_block", N, ms);
    }

    t_section(3, "遍历置位");
    {
        multi_block_bitmask bm;
        bm.resize_entities(256);
        for (uint32_t s = 0; s < 256; ++s)
        {
            for (uint32_t b = 0; b < 8; ++b)
            {
                bm.set_bit(s, 0, b);
            }
        }

        constexpr uint64_t M = 100'000;
        double ms = t_bench_best_ms([&]() {
            uint64_t cnt = 0;
            for (uint64_t i = 0; i < M; ++i)
            {
                uint32_t slot = static_cast<uint32_t>(i & 255);
                bm.for_each_set_bit(slot, [&cnt](uint32_t, uint32_t) { ++cnt; });
            }
            g_sink = cnt;
        }, 5);
        t_perf("for_each_set_bit", M * 8, ms);
    }

    t_section(4, "整体统计");
    {
        multi_block_bitmask bm;
        bm.resize_entities(1024);
        for (uint32_t s = 0; s < 1024; ++s)
        {
            bm.set_bit(s, 0, s & 63);
        }

        constexpr uint64_t M = 20'000;
        double ms = t_bench_best_ms([&]() {
            uint64_t slots = 0;
            for (uint64_t i = 0; i < M; ++i)
            {
                slots = 0;
                bm.for_each_set_slot([&slots](uint32_t) { ++slots; });
            }
            g_sink = slots;
        }, 5);
        t_perf("for_each_set_slot (1024 slot)", M, ms);
    }

    return t_end("multi_block_bitmask 性能 ");
}
