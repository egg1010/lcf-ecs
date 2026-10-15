// test_perf_type_id.cpp - 性能测试: type_id
#include "test_common.hpp"
#include "include/part/type_id.hpp"

struct t1 { int a; };
struct t2 { int a; };
struct t3 { int a; };
struct t4 { int a; };

static volatile uint64_t g_sink = 0;

int main()
{
    t_begin("test_perf_type_id — 性能测试: type_id (百万为单位)");

    constexpr uint64_t N = 1'000'000;

    t_section(1, "单一类型查询");
    {
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                acc += static_cast<uint64_t>(type_id::get_type_id<t1>());
            }
            g_sink = acc;
        }, 5);
        t_perf("get_type_id<t1>", N, ms);
    }

    t_section(2, "多类型轮换");
    {
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                switch (i & 3)
                {
                    case 0: acc += static_cast<uint64_t>(type_id::get_type_id<t1>()); break;
                    case 1: acc += static_cast<uint64_t>(type_id::get_type_id<t2>()); break;
                    case 2: acc += static_cast<uint64_t>(type_id::get_type_id<t3>()); break;
                    default: acc += static_cast<uint64_t>(type_id::get_type_id<t4>()); break;
                }
            }
            g_sink = acc;
        }, 5);
        t_perf("get_type_id (4 类型轮换)", N, ms);
    }

    t_section(3, "掩码位映射");
    {
        int id = type_id::get_type_id<t1>();
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                acc += type_id::mask_block_of(id);
                acc += type_id::mask_offset_of(id);
            }
            g_sink = acc;
        }, 5);
        t_perf("mask_block/offset_of", N * 2, ms);

        uint32_t blk = type_id::mask_block_of(id);
        uint32_t off = type_id::mask_offset_of(id);
        ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                acc += static_cast<uint64_t>(type_id::mask_type_of(blk, off));
            }
            g_sink = acc;
        }, 5);
        t_perf("mask_type_of", N, ms);
    }

    t_section(4, "名绑定查询");
    {
        int id = type_id::get_type_id<t1>();
        type_id::bind_def_name("PerfT1", id);

        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                acc += static_cast<uint64_t>(type_id::get_def_type_id("PerfT1"));
            }
            g_sink = acc;
        }, 5);
        t_perf("get_def_type_id (按名)", N, ms);
    }

    return t_end("type_id 性能 ");
}
