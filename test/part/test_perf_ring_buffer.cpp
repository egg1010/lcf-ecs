// test_perf_ring_buffer.cpp - 性能测试: ring_buffer (百万为单位)
#include "test_common.hpp"
#include "include/part/ring_buffer.hpp"

#include <atomic>

static volatile uint64_t g_sink = 0;

// 编译器屏障: GCC 用内联汇编, MSVC 不支持该语法, 统一走标准原子栅栏
#define BENCH_FENCE(acc) std::atomic_signal_fence(std::memory_order_seq_cst)

int main()
{
    t_begin("test_perf_ring_buffer — 性能测试: ring_buffer (百万为单位)");

    constexpr uint64_t N = 1'000'000;
    constexpr uint64_t CAP_N = 4096;

    t_section(1, "入队+出队交替 (队列近空)");
    {
        ring_buffer<uint64_t, CAP_N> rb;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                acc += rb.try_push(i) ? 1 : 0;
                uint64_t out = 0;
                if (rb.try_pop(out)) { acc += out; }
                BENCH_FENCE(acc);
            }
            g_sink += acc;
        }, 5);
        t_perf("try_push + try_pop(值)", N, ms);
    }

    t_section(2, "批量 push + drain");
    {
        ring_buffer<uint64_t, CAP_N> rb;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                acc += rb.try_push(i) ? 1 : 0;
                if (rb.size() == CAP_N)
                {
                    rb.drain([&acc](uint64_t& v) { acc += v; });
                }
            }
            rb.drain([&acc](uint64_t& v) { acc += v; });
            g_sink += acc;
        }, 5);
        t_perf("push + drain 循环", N, ms);
    }

    t_section(3, "满容量 push + drain 循环");
    {
        ring_buffer<uint64_t, CAP_N> rb;
        constexpr uint64_t R = N / CAP_N;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t r = 0; r < R; ++r)
            {
                for (uint64_t i = 0; i < CAP_N; ++i) { acc += rb.try_push(i) ? 1 : 0; }
                rb.drain([&acc](uint64_t& v) { acc += v; });
                BENCH_FENCE(acc);
            }
            g_sink += acc;
        }, 5);
        t_perf("满容量 push + drain", N, ms);
    }

    t_section(4, "emplace_overwrite + front + pop");
    {
        ring_buffer<uint64_t, CAP_N> rb;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                (void)rb.emplace_overwrite(i);
                const uint64_t* p = rb.front();
                if (p) { acc += *p; }
                acc += rb.pop() ? 1 : 0;
                BENCH_FENCE(acc);
            }
            g_sink += acc;
        }, 5);
        t_perf("emplace_overwrite + front + pop", N, ms);
    }

    t_section(5, "front_run 批量段");
    {
        ring_buffer<uint64_t, CAP_N> rb;
        constexpr uint64_t R = N / CAP_N;
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t r = 0; r < R; ++r)
            {
                for (uint64_t i = 0; i < CAP_N; ++i) { (void)rb.try_push(i); }
                uint64_t* p = nullptr;
                size_t len = 0;
                while (rb.front_run(p, len) > 0)
                {
                    for (size_t i = 0; i < len; ++i) { acc += p[i]; }
                    acc += rb.pop_run(len);
                }
                BENCH_FENCE(acc);
            }
            g_sink += acc;
        }, 5);
        t_perf("front_run + pop_run", N, ms);
    }

    t_section(6, "缓存命中率");
    {
        ring_buffer<uint64_t, CAP_N> rb;
        for (uint64_t i = 0; i < CAP_N; ++i)
        {
            (void)rb.try_push(i);
        }
        uint64_t* p = nullptr;
        size_t len = 0;
        (void)rb.front_run(p, len);
        t_cache_scan("环形缓冲 4K×8B (32KB)", p, len, sizeof(uint64_t));
    }

    return t_end("ring_buffer 性能 ");
}
