// test_perf_entity.cpp - 性能测试: entity_manager (创造/销毁/校验)
#include "test_common.hpp"
#include "include/ecs/entity/entity_manager.hpp"
#include <cstdint>

static volatile uint64_t g_sink_dummy = 0;

int main()
{
    t_begin("test_perf_entity — 性能测试: entity_manager (百万为单位)");

    constexpr uint64_t N = 1'000'000;

    t_section(1, "创造与销毁");
    {
        ecs::entity_manager em;
        em.preallocate_entities(N);
        dense<ecs::entity> ents;
        ents.increase_capacity(N);

        double ms = t_bench_best_ms([&]() {
            ents.clear();
            for (uint64_t i = 0; i < N; ++i)
            {
                ents.emplace_back(em.get_entity());
            }
        }, 5);
        t_perf("get_entity (create)", N, ms);

        ms = t_bench_best_ms(
            [&]() {
                for (uint64_t i = 0; i < N; ++i)
                {
                    ents[i] = em.get_entity();
                }
            },
            [&]() {
                for (uint64_t i = 0; i < N; ++i)
                {
                    em.destroy_entity(ents[i]);
                }
            }, 5);
        t_perf("destroy_entity", N, ms);
    }

    t_section(2, "版本校验");
    {
        ecs::entity_manager em;
        em.preallocate_entities(N);
        dense<ecs::entity> ents;
        ents.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            ents.emplace_back(em.get_entity());
        }

        double ms = t_bench_best_ms([&]() {
            uint64_t ok = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                if (em.is_version_valid(ents[i])) { ++ok; }
            }
            g_sink_dummy = ok;
        }, 5);
        t_perf("is_version_valid", N, ms);
    }

    t_section(3, "索引与掩码");
    {
        ecs::entity_manager em;
        em.preallocate_mask_blocks(16);
        double ms = t_bench_best_ms([&]() {
            for (uint64_t i = 0; i < N; ++i)
            {
                uint32_t slot = static_cast<uint32_t>(i & 0xFFFF);
                uint32_t block = static_cast<uint32_t>(i & 0xF);
                em.set_mask_bit(slot, block, static_cast<uint32_t>(i & 63));
            }
        }, 5);
        t_perf("set_mask_bit", N, ms);

        ms = t_bench_best_ms([&]() {
            uint64_t cnt = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                cnt += em.get_block(static_cast<uint32_t>(i & 0xFFFF), static_cast<uint32_t>(i & 0xF));
            }
            g_sink_dummy = cnt;
        }, 5);
        t_perf("get_block", N, ms);
    }

    t_section(4, "entity_state 访问");
    {
        ecs::entity_manager em;
        em.preallocate_entities(N);
        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                acc += em.get_entity_state(static_cast<uint32_t>(i & 0xFFFF)).flags;
            }
            g_sink_dummy = acc;
        }, 5);
        t_perf("get_entity_state", N, ms);
    }

    return t_end("entity_manager 性能 ");
}
