// test_perf_ecs_architecture.cpp - ECS 架构综合性能测试
//   规模单位: 100 万组件; 输出单位: MB/s 吞吐, ns/组件 延迟, MB 内存; 含缓存命中率
#include "test_common.hpp"
#include "include/ecs/component.hpp"
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
    t_begin("test_perf_ecs_architecture — ECS 架构综合性能 (100 万组件, MB / ns)");

    constexpr uint64_t N = 1'000'000;                       // 100 万组件单位
    constexpr size_t P_SZ = sizeof(position);               // 12 B
    constexpr size_t V_SZ = sizeof(velocity);               // 12 B
    constexpr size_t E_SZ = sizeof(ecs::entity);            // 8 B

    // ========================================================================
    t_section(1, "构建: 100 万实体 + 200 万组件");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(N);
        dense<ecs::entity> ents;
        ents.increase_capacity(N);

        double ms = t_bench_best_ms([&]() {
            mgr.clear();
            ents.clear();
            for (uint64_t i = 0; i < N; ++i)
            {
                auto e = mgr.create_entity();
                ents.push_back(e);
                mgr.add(e, position{static_cast<float>(i), 0.0f, 0.0f});
                mgr.add(e, velocity{1.0f, 0.0f, 0.0f});
            }
            g_sink = static_cast<double>(ents.size());
        }, 3);
        // 数据量 = 2e6 组件 × 12B
        t_throughput("构建 (create + add×2)", N * (P_SZ + V_SZ), N * 2, ms);

        ms = t_bench_best_ms([&]() {
            mgr.clear();
            ents.clear();
            for (uint64_t i = 0; i < N; ++i)
            {
                ents.push_back(mgr.create_entity());
            }
            g_sink = static_cast<double>(ents.size());
        }, 3);
        t_throughput("仅创建实体", N * E_SZ, N, ms);
    }

    // ========================================================================
    ecs::manager mgr;
    {
        mgr.preallocate_entities(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            auto e = mgr.create_entity();
            mgr.add(e, position{static_cast<float>(i), 1.0f, 2.0f});
            if ((i & 1) == 0)
            {
                mgr.add(e, velocity{1.0f, 1.0f, 1.0f});
            }
        }
    }

    t_section(2, "遍历 (单组件视图)");
    {
        double ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            mgr.view<position>().for_each([&acc](position& p) { acc += p.x; });
            g_sink = acc;
        }, 5);
        t_throughput("single_view<position>", N * P_SZ, N, ms);

        ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            mgr.view<position>().for_each([&acc](ecs::entity, position& p) { acc += p.x; });
            g_sink = acc;
        }, 5);
        t_throughput("single_view<position> (带 entity)", (N * P_SZ) + (N * E_SZ), N, ms);
    }

    t_section(3, "遍历 (多组件视图 / 分组)");
    {
        double ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            mgr.view<position, velocity>().for_each([&acc](position& p, velocity& v) {
                acc += p.x + v.x;
            });
            g_sink = acc;
        }, 5);
        t_throughput("multi_view<position,velocity>", (N / 2) * (P_SZ + V_SZ), N / 2, ms);

        auto g = mgr.group<position, velocity>();
        t_check("group 命中 50 万组件对", g.size() == N / 2);
        ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            g.for_each([&acc](position& p, velocity& v) { acc += p.x + v.x; });
            g_sink = acc;
        }, 5);
        t_throughput("group<position,velocity>", (N / 2) * (P_SZ + V_SZ), N / 2, ms);
    }

    t_section(4, "遍历 (运行时视图)");
    {
        const int pos_id = type_id::get_type_id<position>();
        const int vel_id = type_id::get_type_id<velocity>();
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, vel_id});
        t_check("runtime_view 命中 50 万", rv.count() == N / 2);

        double ms = t_bench_best_ms([&]() {
            uint64_t cnt = 0;
            rv.for_each([&cnt](ecs::entity) { ++cnt; });
            g_sink = static_cast<double>(cnt);
        }, 5);
        t_throughput("runtime_view<position,velocity>", (N / 2) * (P_SZ + V_SZ), N / 2, ms);

        ms = t_bench_best_ms([&]() {
            double acc = 0.0;
            auto* pset = mgr.get_single_class_set<position>();
            auto* pool = pset->get_typed_pool_ptr<position>();
            auto& idx = pset->get_entity_indices();
            for (size_t i = 0; i < idx.size(); ++i)
            {
                acc += (*pool)[i].x;
            }
            g_sink = acc;
        }, 5);
        t_throughput("裸组件池顺序遍历", N * P_SZ, N, ms);
    }

    t_section(5, "架构操作 (组件增删 / 指针解析)");
    {
        double ms = t_bench_best_ms([&]() {
            mgr.view<position>().for_each([](position&) {});
        }, 1);
        (void)ms;

        // 指针解析 (批量)
        dense<position*> ptrs;
        ptrs.increase_capacity(N);
        for (uint64_t i = 0; i < N; ++i) { ptrs.push_back(nullptr); }
        auto* pset = mgr.get_single_class_set<position>();
        auto& eidx = pset->get_entity_indices();
        dense<ecs::entity> ents;
        ents.increase_capacity(N);
        for (size_t i = 0; i < eidx.size(); ++i)
        {
            ents.push_back(ecs::entity(eidx[i], pset->get_version_unchecked(eidx[i])));
        }

        ms = t_bench_best_ms([&]() {
            mgr.get_ptr_batch<position>(ents.data(), ptrs.data(), ents.size());
            g_sink = ptrs[0] ? 1.0 : 0.0;
        }, 5);
        t_throughput("get_ptr_batch 批量解析", N * E_SZ, N, ms);
    }

    t_section(6, "内存占用 (MB)");
    {
        auto* pset = mgr.get_single_class_set<position>();
        auto* vset = mgr.get_single_class_set<velocity>();
        size_t p_mem = pset ? pset->get_typed_pool_ptr<position>()->capacity() * P_SZ : 0;
        size_t v_mem = vset ? vset->get_typed_pool_ptr<velocity>()->capacity() * V_SZ : 0;
        t_mem("position 组件池 (12B × capacity)", p_mem);
        t_mem("velocity 组件池 (12B × capacity)", v_mem);
        t_mem("实体句柄表 (8B × 1e6)", N * E_SZ);
        t_mem("组件池合计", p_mem + v_mem);
    }

    t_section(7, "缓存命中率");
    {
        auto* pset = mgr.get_single_class_set<position>();
        auto* vset = mgr.get_single_class_set<velocity>();
        if (pset && vset)
        {
            const position* pdata = pset->get_typed_pool_ptr<position>()->data();
            const velocity* vdata = vset->get_typed_pool_ptr<velocity>()->data();
            // 顺序扫描: 反映顺序遍历 ECS 密集存储的缓存行为
            t_cache_scan("position 池 100 万 × 12B", pdata, N, P_SZ);
            t_cache_scan("velocity 池 50 万 × 12B", vdata, N / 2, V_SZ);
        }
        else
        {
            t_check("组件池可用", false);
        }
    }

    return t_end("ECS 架构综合 ");
}
