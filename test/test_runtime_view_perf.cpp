// test_runtime_view_perf.cpp - runtime_view 独立性能测试
#include "perf_common.hpp"
#include "include/component.hpp"

using namespace std;
using ecs::manager;
using ecs::entity;

struct Pos { float x, y, z; };
struct Vel { float vx, vy, vz; };
struct Hp  { int v; };
struct Dmg { int v; };
struct Armor { int v; };

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
        if (i % 2 == 0) mgr.add(Dmg{ri(rng)}, e);
        if (i % 3 == 0) mgr.add(Armor{ri(rng)}, e);
    }
}

// === Section 1: 基本查询接口 ===
static void test_basic(manager& mgr, size_t n, const vector<int>& req_ids, const char* label)
{
    print_header((string("Section 1: basic (") + label + ", N=" + to_string(n) + ")").c_str());
    auto rv = mgr.runtime_view_create(req_ids);

    constexpr int REPEAT = 5;

    {
        const size_t OPS = 100000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += rv.size();
                s += rv.count();
                s += rv.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("size/count/empty", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < n; ++i) s = rv.contains(entity(static_cast<uint32_t>(opaque(i)), 1));
            (void)s;
        });
        print_ns("contains", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile entity e = rv.get_first_entity();
            (void)e;
            compiler_barrier();
            return 0;
        });
        print_ns("get_first_entity", 1, ns);
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            Pos* p = nullptr;
            for (size_t i = 0; i < n; ++i)
                p = rv.get_ptr<Pos>(entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get_ptr<T>", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            rv.rebuild();
            compiler_barrier();
            return rv.size();
        });
        print_ns("rebuild", 1, ns);
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool c = rv.changed();
            (void)c;
            compiler_barrier();
            return 0;
        });
        print_ns("changed", 1, ns);

        rv.reset_change_tracking();
    }

    print_footer();
}

// === Section 2: for_each 变体 ===
static void test_for_each(manager& mgr, size_t n, const vector<int>& req_ids, const char* label)
{
    print_header((string("Section 2: for_each (") + label + ", N=" + to_string(n) + ")").c_str());
    auto rv = mgr.runtime_view_create(req_ids);

    constexpr int REPEAT = 5;

    {
        double ns = best_ns(REPEAT, [&]() {
            size_t cnt = 0;
            rv.for_each([&](entity) { ++cnt; });
            compiler_barrier();
            return cnt;
        });
        print_ns("for_each", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            float sink = 0;
            rv.for_each_typed<Pos, Vel>([&](auto&... comps) {
                float tmp = 0;
                ((tmp += *reinterpret_cast<float*>(&comps)), ...);
                sink = tmp;
            });
            compiler_barrier();
            (void)sink;
        });
        print_ns("for_each_typed", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            size_t cnt = 0;
            rv.for_each_parallel(0, 1, [&](entity) { ++cnt; });
            compiler_barrier();
            return cnt;
        });
        print_ns("for_each_parallel (1 worker)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            size_t cnt = 0;
            rv.for_each_paged(0, n, [&](entity) { ++cnt; });
            compiler_barrier();
            return cnt;
        });
        print_ns("for_each_paged", n, ns / static_cast<double>(n));
    }

    {
        rv.reset_change_tracking();
        double ns = best_ns(REPEAT, [&]() {
            size_t cnt = 0;
            rv.for_each_changed([&](entity) { ++cnt; });
            compiler_barrier();
            return cnt;
        });
        print_ns("for_each_changed", 1, ns);
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t cnt = 0;
            for (auto it = rv.begin(); it != rv.end(); ++it)
            {
                entity e = *it;
                touch_ptr(&e);
                ++cnt;
            }
            compiler_barrier();
            return cnt;
        });
        print_ns("begin/end iter", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 3: sort_by_component ===
static void test_sort(manager& mgr, size_t n, const vector<int>& req_ids)
{
    print_header("Section 3: sort_by_component");
    auto rv = mgr.runtime_view_create(req_ids);

    constexpr int REPEAT = 3;

    {
        double ns = best_ns(REPEAT, [&]() {
            rv.sort_by_component<Pos>([](const Pos& a, const Pos& b) { return a.x < b.x; });
            compiler_barrier();
            return rv.get_sorted_entities().size();
        });
        print_ns("sort_by_component<Pos>", n, ns / static_cast<double>(n));
    }

    {
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = rv.get_sorted_entities().size();
            (void)s;
            compiler_barrier();
            return s;
        });
        print_ns("get_sorted_entities", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

int main()
{
    cout << "============================================================\n";
    cout << "  runtime_view 独立性能测试\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K
    mt19937 rng(42);
    manager mgr;
    build_manager(mgr, N, rng);

    // 获取 type_id (type_id 在全局命名空间, 非 ecs)
    int pos_id = ::type_id::get_type_id<Pos>();
    int vel_id = ::type_id::get_type_id<Vel>();
    int hp_id  = ::type_id::get_type_id<Hp>();
    int dmg_id = ::type_id::get_type_id<Dmg>();
    int arm_id = ::type_id::get_type_id<Armor>();

    // 2-comp 查询
    {
        vector<int> req = {pos_id, vel_id};
        test_basic(mgr, N, req, "2-comp");
        test_for_each(mgr, N, req, "2-comp");
    }

    // 3-comp 查询
    {
        vector<int> req = {pos_id, vel_id, hp_id};
        test_basic(mgr, N, req, "3-comp");
        test_for_each(mgr, N, req, "3-comp");
    }

    // 5-comp 查询 (部分匹配)
    {
        vector<int> req = {pos_id, vel_id, hp_id, dmg_id, arm_id};
        test_basic(mgr, N, req, "5-comp partial");
        test_for_each(mgr, N, req, "5-comp partial");
    }

    // 排序测试
    {
        vector<int> req = {pos_id, vel_id};
        test_sort(mgr, N, req);
    }

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
