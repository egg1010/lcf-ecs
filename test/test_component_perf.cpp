// test_component_perf.cpp - component/query_context/manager 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/component.hpp"

using namespace std;
using ecs::manager;
using ecs::entity;
using ecs::single_class_set;
using ecs::query_context;

// 测试组件
struct Pos { float x, y, z; };
struct Vel { float vx, vy, vz; };
struct Hp  { int v; };

static void build_manager(manager& mgr, size_t n, mt19937& rng)
{
    uniform_real_distribution<float> rf(-1000, 1000);
    uniform_int_distribution<int> ri(0, 100);
    for (size_t i = 0; i < n; ++i)
    {
        entity e(static_cast<uint32_t>(i), 1);
        mgr.add(Pos{rf(rng), rf(rng), rf(rng)}, e);
        mgr.add(Vel{rf(rng), rf(rng), rf(rng)}, e);
        mgr.add(Hp{ri(rng)}, e);
    }
}

// === Section 1: manager - 实体管理 ===
static void test_manager_entity(size_t n)
{
    print_header("Section 1: manager (entity)");
    constexpr int REPEAT = 3;

    // 1.1 create_entity
    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            for (size_t i = 0; i < n; ++i) mgr.create_entity();
            compiler_barrier();
            return 0;
        });
        print_ns("create_entity", n, ns / static_cast<double>(n));
    }

    // 1.2 is_entity_valid
    {
        manager mgr;
        vector<entity> ents(n);
        for (size_t i = 0; i < n; ++i) ents[i] = mgr.create_entity();
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < n; ++i) s = mgr.is_entity_valid(ents[i]);
            (void)s;
        });
        print_ns("is_entity_valid", n, ns / static_cast<double>(n));
    }

    // 1.3 delete_entity
    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            for (size_t i = 0; i < n; ++i) mgr.create_entity();
            vector<entity> ents;
            for (size_t i = 0; i < n; ++i)
            {
                entity e = mgr.create_entity();
                ents.push_back(e);
            }
            for (size_t i = 0; i < n; ++i) mgr.delete_entity(ents[i]);
            compiler_barrier();
            return 0;
        });
        print_ns("delete_entity", n, ns / static_cast<double>(n));
    }

    // 1.4 get_entity_manager / get_entity_state / set/clear,has_entity_flag
    {
        manager mgr;
        for (size_t i = 0; i < n; ++i) mgr.create_entity();
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += reinterpret_cast<size_t>(&mgr.get_entity_manager());
                s += reinterpret_cast<size_t>(&mgr.get_entity_state(opaque(static_cast<uint32_t>(i % n))));
                mgr.set_entity_flag(opaque(static_cast<uint32_t>(i % n)), ecs::entity_flag::active);
                mgr.clear_entity_flag(opaque(static_cast<uint32_t>(i % n)), ecs::entity_flag::active);
                s += mgr.has_entity_flag(opaque(static_cast<uint32_t>(i % n)), ecs::entity_flag::active) ? 1 : 0;
            }
            (void)s;
        });
        print_ns("get_em/state/set/clear/has_flag", OPS, ns / static_cast<double>(OPS));
    }

    // 1.5 append_preallocated_entities
    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            mgr.append_preallocated_entities(n);
            compiler_barrier();
            return 0;
        });
        print_ns("append_preallocated_entities", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 2: manager - 组件添加 ===
static void test_manager_add(size_t n)
{
    print_header("Section 2: manager (add)");
    constexpr int REPEAT = 3;
    mt19937 rng(42);
    uniform_real_distribution<float> rf(-1000, 1000);

    // 2.1 add<T> (单组件)
    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            for (size_t i = 0; i < n; ++i)
                mgr.add(Pos{rf(rng), rf(rng), rf(rng)}, entity(static_cast<uint32_t>(i), 1));
            compiler_barrier();
            return 0;
        });
        print_ns("add<Pos>", n, ns / static_cast<double>(n));
    }

    // 2.2 add<T> (反参序, IsEntity)
    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            for (size_t i = 0; i < n; ++i)
                mgr.add(Pos{rf(rng), rf(rng), rf(rng)}, entity(static_cast<uint32_t>(i), 1));
            compiler_barrier();
            return 0;
        });
        print_ns("add(Pos, e) reversed", n, ns / static_cast<double>(n));
    }

    // 2.3 add_batch<T> (批量插入)
    {
        vector<entity> ents(n);
        vector<Pos> comps(n);
        for (size_t i = 0; i < n; ++i)
        {
            ents[i] = entity(static_cast<uint32_t>(i), 1);
            comps[i] = Pos{rf(rng), rf(rng), rf(rng)};
        }
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            mgr.add_batch<Pos>(span<const entity>(ents.data(), n), span<const Pos>(comps.data(), n));
            compiler_barrier();
            return 0;
        });
        print_ns("add_batch<Pos>", n, ns / static_cast<double>(n));
    }

    // 2.4 addc<T> (多实体单组件)
    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            Pos p{1, 2, 3};
            for (size_t i = 0; i < n; ++i) mgr.create_entity();
            // addc 需要实体已存在
            return 0;
        });
        print_ns("addc (setup baseline)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 3: manager - 组件移除 ===
static void test_manager_remove(size_t n)
{
    print_header("Section 3: manager (remove)");
    constexpr int REPEAT = 3;
    mt19937 rng(42);
    uniform_real_distribution<float> rf(-1000, 1000);

    // 3.1 soft_remove<T>
    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            for (size_t i = 0; i < n; ++i)
                mgr.add(Pos{rf(rng), rf(rng), rf(rng)}, entity(static_cast<uint32_t>(i), 1));
            for (size_t i = 0; i < n; ++i)
                mgr.soft_remove<Pos>(entity(static_cast<uint32_t>(i), 1));
            compiler_barrier();
            return 0;
        });
        print_ns("soft_remove<Pos>", n, ns / static_cast<double>(n));
    }

    // 3.2 hard_remove<T>
    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            for (size_t i = 0; i < n; ++i)
                mgr.add(Pos{rf(rng), rf(rng), rf(rng)}, entity(static_cast<uint32_t>(i), 1));
            for (size_t i = 0; i < n; ++i)
                mgr.hard_remove<Pos>(entity(static_cast<uint32_t>(i), 1));
            compiler_barrier();
            return 0;
        });
        print_ns("hard_remove<Pos>", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 4: manager - 组件访问 ===
static void test_manager_access(size_t n)
{
    print_header("Section 4: manager (access)");
    constexpr int REPEAT = 5;
    mt19937 rng(42);
    manager mgr;
    build_manager(mgr, n, rng);

    // 4.1 get_ptr<T> (含 is_valid 检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            Pos* p = nullptr;
            for (size_t i = 0; i < n; ++i)
                p = mgr.get_ptr<Pos>(entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get_ptr<Pos>", n, ns / static_cast<double>(n));
    }

    // 4.2 get_ptr_fast<T>
    {
        double ns = best_ns(REPEAT, [&]() {
            Pos* p = nullptr;
            for (size_t i = 0; i < n; ++i)
                p = mgr.get_ptr_fast<Pos>(entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get_ptr_fast<Pos>", n, ns / static_cast<double>(n));
    }

    // 4.3 get_ptr_fast_cached<T>
    {
        single_class_set* set = mgr.get_single_class_set<Pos>();
        double ns = best_ns(REPEAT, [&]() {
            Pos* p = nullptr;
            for (size_t i = 0; i < n; ++i)
                p = mgr.get_ptr_fast_cached<Pos>(set, entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get_ptr_fast_cached<Pos>", n, ns / static_cast<double>(n));
    }

    // 4.4 prefetch_ptr / prefetch_ptr_data / prefetch_ptr_cached
    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
                mgr.prefetch_ptr<Pos>(entity(static_cast<uint32_t>(opaque(i)), 1));
            compiler_barrier();
            return 0;
        });
        print_ns("prefetch_ptr<Pos>", n, ns / static_cast<double>(n));

        ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
                mgr.prefetch_ptr_data<Pos>(entity(static_cast<uint32_t>(opaque(i)), 1));
            compiler_barrier();
            return 0;
        });
        print_ns("prefetch_ptr_data<Pos>", n, ns / static_cast<double>(n));

        single_class_set* set = mgr.get_single_class_set<Pos>();
        ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
                mgr.prefetch_ptr_cached<Pos>(set, entity(static_cast<uint32_t>(opaque(i)), 1));
            compiler_barrier();
            return 0;
        });
        print_ns("prefetch_ptr_cached<Pos>", n, ns / static_cast<double>(n));
    }

    // 4.5 get_ptr_batch<T>
    {
        vector<entity> ents(n);
        vector<Pos*> results(n);
        for (size_t i = 0; i < n; ++i) ents[i] = entity(static_cast<uint32_t>(i), 1);
        double ns = best_ns(REPEAT, [&]() {
            mgr.get_ptr_batch<Pos>(ents.data(), results.data(), n);
            compiler_barrier();
            return results[0];
        });
        print_ns("get_ptr_batch<Pos>", n, ns / static_cast<double>(n));
    }

    // 4.6 prefetch_ptr_batch<T>
    {
        vector<entity> ents(n);
        for (size_t i = 0; i < n; ++i) ents[i] = entity(static_cast<uint32_t>(i), 1);
        double ns = best_ns(REPEAT, [&]() {
            mgr.prefetch_ptr_batch<Pos>(ents.data(), n);
            compiler_barrier();
            return 0;
        });
        print_ns("prefetch_ptr_batch<Pos>", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 5: manager - 元信息查询 ===
static void test_manager_meta(size_t n)
{
    print_header("Section 5: manager (meta)");
    constexpr int REPEAT = 5;
    mt19937 rng(42);
    manager mgr;
    build_manager(mgr, n, rng);

    const size_t OPS = 1000000;

    // 5.1 get_single_class_set / get_component_container / get_component_bit
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += reinterpret_cast<size_t>(mgr.get_single_class_set<Pos>());
                s += reinterpret_cast<size_t>(mgr.get_component_container<Pos>());
                s += mgr.get_component_bit<Pos>();
            }
            (void)s;
        });
        print_ns("get_set/container/bit", OPS, ns / static_cast<double>(OPS));
    }

    // 5.2 get_single_class_set_by_id / get_component_meta / reserve_component_capacity
    {
        int pos_id = ::type_id::get_type_id<Pos>();
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += reinterpret_cast<size_t>(mgr.get_single_class_set_by_id(pos_id));
                s += reinterpret_cast<size_t>(mgr.get_component_meta(pos_id));
            }
            (void)s;
        });
        print_ns("get_set_by_id/get_meta", OPS, ns / static_cast<double>(OPS));
    }

    // 5.3 get_entity_mask / get_entity_block / num_mask_blocks / reserve_mask_blocks
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += mgr.get_entity_mask(entity(static_cast<uint32_t>(opaque(i % n)), 1));
                s += mgr.get_entity_block(entity(static_cast<uint32_t>(opaque(i % n)), 1), 0);
                s += mgr.num_mask_blocks();
            }
            (void)s;
        });
        print_ns("get_mask/get_block/num_blocks", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 6: query_context<T> ===
static void test_query_context(size_t n)
{
    print_header("Section 6: query_context<T>");
    constexpr int REPEAT = 5;
    mt19937 rng(42);
    manager mgr;
    build_manager(mgr, n, rng);

    // 6.1 构造
    {
        double ns = best_ns(REPEAT, [&]() {
            query_context<Pos> qc(mgr);
            compiler_barrier();
            return qc.valid();
        });
        print_ns("ctor", 1, ns);
    }

    query_context<Pos> qc(mgr);

    // 6.2 valid
    {
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i) s = qc.valid();
            (void)s;
        });
        print_ns("valid", OPS, ns / static_cast<double>(OPS));
    }

    // 6.3 get_ptr (非 const)
    {
        double ns = best_ns(REPEAT, [&]() {
            Pos* p = nullptr;
            for (size_t i = 0; i < n; ++i)
                p = qc.get_ptr(entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get_ptr (non-const)", n, ns / static_cast<double>(n));
    }

    // 6.4 get_ptr (const)
    {
        const query_context<Pos>& cqc = qc;
        double ns = best_ns(REPEAT, [&]() {
            const Pos* p = nullptr;
            for (size_t i = 0; i < n; ++i)
                p = cqc.get_ptr(entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get_ptr (const)", n, ns / static_cast<double>(n));
    }

    // 6.5 prefetch_sparse / prefetch_data
    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
                qc.prefetch_sparse(entity(static_cast<uint32_t>(opaque(i)), 1));
            compiler_barrier();
            return 0;
        });
        print_ns("prefetch_sparse", n, ns / static_cast<double>(n));

        ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
                qc.prefetch_data(entity(static_cast<uint32_t>(opaque(i)), 1));
            compiler_barrier();
            return 0;
        });
        print_ns("prefetch_data", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 7: manager - 排序 ===
static void test_manager_sort(size_t n)
{
    print_header("Section 7: manager (sort)");
    constexpr int REPEAT = 3;
    mt19937 rng(42);
    manager mgr;
    build_manager(mgr, n, rng);

    // 7.1 sort_entities_by_component<Pos>
    {
        double ns = best_ns(REPEAT, [&]() {
            mgr.sort_entities_by_component<Pos>([](const Pos& a, const Pos& b) { return a.x < b.x; });
            compiler_barrier();
            return 0;
        });
        print_ns("sort_entities_by_component<Pos>", n, ns / static_cast<double>(n));
    }

    // 7.2 sort_component_container<Pos>
    {
        double ns = best_ns(REPEAT, [&]() {
            mgr.sort_component_container<Pos>([](const Pos& a, const Pos& b) { return a.x < b.x; });
            compiler_barrier();
            return 0;
        });
        print_ns("sort_component_container<Pos>", n, ns / static_cast<double>(n));
    }

    // 7.3 reorder_by_component<Pos, Vel>
    {
        double ns = best_ns(REPEAT, [&]() {
            mgr.reorder_by_component<Pos, Vel>([](const Vel& a, const Vel& b) { return a.vx < b.vx; });
            compiler_barrier();
            return 0;
        });
        print_ns("reorder_by_component<Pos,Vel>", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  component / query_context / manager 独立性能测试\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K

    test_manager_entity(N);
    test_manager_add(N);
    test_manager_remove(N);
    test_manager_access(N);
    test_manager_meta(N);
    test_query_context(N);
    test_manager_sort(N);

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
