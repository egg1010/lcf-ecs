// test_perf_manager.cpp - 性能测试: ecs::manager 组件接口
#include "test_common.hpp"
#include "include/component.hpp"

struct position
{
    float x;
    float y;
    float z;
};

struct velocity
{
    float x;
    float y;
    float z;
};

static volatile uint64_t g_sink = 0;

int main()
{
    t_begin("test_perf_manager — 性能测试: manager 组件接口 (百万为单位)");

    constexpr uint64_t N = 1'000'000;

    t_section(1, "实体创建");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(N);
        dense<ecs::entity> ents;
        ents.increase_capacity(N);

        double ms = t_bench_best_ms([&]() {
            ents.clear();
            for (uint64_t i = 0; i < N; ++i)
            {
                ents.push_back(mgr.create_entity());
            }
        }, 5);
        t_perf("create_entity", N, ms);

        g_sink += ents.size();
    }

    t_section(2, "组件添加");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(N);
        dense<ecs::entity> ents;
        ents.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            ents.push_back(mgr.create_entity());
        }
        mgr.delete_type_container<position>();

        double ms = t_bench_best_ms([&]() {
            mgr.delete_type_container<position>();
            for (uint64_t i = 0; i < N; ++i)
            {
                mgr.add(ents[i], position{static_cast<float>(i), 0.0f, 0.0f});
            }
        }, 5);
        t_perf("add<position>", N, ms);

        dense<velocity> comps;
        comps.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            comps.push_back(velocity{static_cast<float>(i), 1.0f, 2.0f});
        }
        ms = t_bench_best_ms([&]() {
            mgr.delete_type_container<velocity>();
            mgr.add_batch<velocity>(ents, comps);
        }, 5);
        t_perf("add_batch<velocity>", N, ms);
    }

    t_section(3, "组件查询");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(N);
        dense<ecs::entity> ents;
        ents.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            auto e = mgr.create_entity();
            ents.push_back(e);
            mgr.add(e, position{static_cast<float>(i), 0.0f, 0.0f});
        }

        double ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                position* p = mgr.get_ptr<position>(ents[i]);
                if (p) { acc += static_cast<uint64_t>(p->x); }
            }
            g_sink = acc;
        }, 5);
        t_perf("get_ptr<position>", N, ms);

        ms = t_bench_best_ms([&]() {
            uint64_t acc = 0;
            for (uint64_t i = 0; i < N; ++i)
            {
                position* p = mgr.get_ptr_fast<position>(ents[i]);
                if (p) { acc += static_cast<uint64_t>(p->x); }
            }
            g_sink = acc;
        }, 5);
        t_perf("get_ptr_fast<position>", N, ms);

        // 批量指针解析
        dense<position*> results;
        results.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i) { results.push_back(nullptr); }
        ms = t_bench_best_ms([&]() {
            mgr.get_ptr_batch<position>(ents.data(), results.data(), N);
        }, 5);
        t_perf("get_ptr_batch<position>", N, ms);
    }

    t_section(4, "缓存命中率 (组件池)");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            auto e = mgr.create_entity();
            mgr.add(e, position{static_cast<float>(i), 0.0f, 0.0f});
        }
        auto* set = mgr.get_single_class_set<position>();
        const position* data = set ? set->get_typed_pool_ptr<position>()->data() : nullptr;
        if (data)
        {
            t_cache_scan("position 池 1e6×12B (12MB)", data, 1'000'000, sizeof(position));
        }
        else
        {
            t_check("position 池可用", false);
        }
    }

    return t_end("manager 性能 ");
}
