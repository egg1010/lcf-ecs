// test_perf_views.cpp - 性能测试: 视图系统遍历与缓存
#include "test_common.hpp"
#include "include/ecs/manager.hpp"
#include <array>

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

static volatile double g_sink = 0.0;

int main()
{
    t_begin("test_perf_views — 性能测试: 视图系统 (百万为单位)");

    constexpr uint64_t N = 1'000'000;

    ecs::manager mgr;
    mgr.preallocate_entities(N);
    for (uint64_t i = 0; i < N; ++i)
    {
        auto e = mgr.create_entity();
        mgr.add(e, position{static_cast<float>(i), 0.0f, 0.0f});
        if ((i & 1) == 0)
        {
            mgr.add(e, velocity{1.0f, 0.0f, 0.0f});
        }
    }

    t_section(1, "single_view 遍历");
    {
        double ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            mgr.view<position>().for_each([&acc](position& p) { acc += p.x; });
            g_sink = acc;
        }, 5);
        t_perf("view<position>().for_each", N, ms);

        ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            mgr.view<position>().for_each([&acc](ecs::entity, position& p) { acc += p.x; });
            g_sink = acc;
        }, 5);
        t_perf("view<position>().for_each(entity)", N, ms);
    }

    t_section(2, "multi_view 遍历");
    {
        double ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            mgr.view<position, velocity>().for_each([&acc](position& p, velocity& v) {
                acc += p.x + v.x;
            });
            g_sink = acc;
        }, 5);
        t_perf("view<position,velocity>().for_each", N, ms);

        ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            mgr.view<position, velocity>().for_each([&acc](ecs::entity, position& p, velocity& v) {
                acc += p.x + v.x;
            });
            g_sink = acc;
        }, 5);
        t_perf("view<position,velocity>().for_each(entity)", N, ms);
    }

    t_section(3, "group 遍历");
    {
        auto g = mgr.group<position, velocity>();
        t_check("group 命中数 == 5e5", g.size() == N / 2);

        double ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            g.for_each([&acc](position& p, velocity& v) { acc += p.x + v.x; });
            g_sink = acc;
        }, 5);
        t_perf("group<position,velocity>().for_each", N / 2, ms);
    }

    t_section(4, "runtime_view 遍历");
    {
        const int pos_id = type_id::get_type_id<position>();
        const int vel_id = type_id::get_type_id<velocity>();
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, vel_id});
        t_check("runtime_view 命中数 == 5e5", rv.count() == N / 2);

        double ms = t_bench_best_ms([&]() {
            uint64_t cnt = 0;
            rv.for_each([&cnt](ecs::entity) { ++cnt; });
            g_sink = static_cast<double>(cnt);
        }, 5);
        t_perf("runtime_view.for_each", N / 2, ms);
    }

    t_section(5, "缓存命中率 (组件池)");
    {
        auto* pset = mgr.get_single_class_set<position>();
        auto* vset = mgr.get_single_class_set<velocity>();
        if (pset && vset)
        {
            const position* pdata = pset->get_typed_pool_ptr<position>()->data();
            const velocity* vdata = vset->get_typed_pool_ptr<velocity>()->data();
            t_cache_scan("position 池 1e6×12B", pdata, 1'000'000, sizeof(position));
            t_cache_scan("velocity 池 5e5×12B", vdata, 500'000, sizeof(velocity));
        }
        else
        {
            t_check("组件池可用", false);
        }
    }

    return t_end("views 性能 ");
}
