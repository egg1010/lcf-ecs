// test_group_perf.cpp - group<T...> / owning_group<T...> 独立性能测试
#include "perf_common.hpp"
#include "include/component.hpp"

using namespace std;
using ecs::manager;
using ecs::entity;

struct Pos { float x, y, z; };           // 12B
struct Vel { float vx, vy, vz; };        // 12B
struct Hp  { int v; };                   // 4B
struct Dmg { int v; };                   // 4B
struct Armor { int v; };                 // 4B
struct Spd { float v; };                 // 4B
struct Rot { float x, y, z, w; };        // 16B
struct Scl { float x, y, z; };           // 12B
struct Name { char data[32]; };          // 32B
struct Mass { float v; };                // 4B

static void build_manager(manager& mgr, size_t n, mt19937& rng)
{
    uniform_real_distribution<float> rf(-1000, 1000);
    uniform_int_distribution<int> ri(0, 100);
    for (size_t i = 0; i < n; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add(Pos{rf(rng), rf(rng), rf(rng)}, e);
        mgr.add(Vel{rf(rng), rf(rng), rf(rng)}, e);
        mgr.add(Hp{ri(rng)}, e);
        mgr.add(Dmg{ri(rng)}, e);
        mgr.add(Armor{ri(rng)}, e);
    }
}

// 10 组件版 build: total_size = 12+12+4+4+4+4+16+12+32+4 = 104B
static void build_manager_10(manager& mgr, size_t n, mt19937& rng)
{
    uniform_real_distribution<float> rf(-1000, 1000);
    uniform_int_distribution<int> ri(0, 100);
    for (size_t i = 0; i < n; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add(Pos{rf(rng), rf(rng), rf(rng)}, e);
        mgr.add(Vel{rf(rng), rf(rng), rf(rng)}, e);
        mgr.add(Hp{ri(rng)}, e);
        mgr.add(Dmg{ri(rng)}, e);
        mgr.add(Armor{ri(rng)}, e);
        mgr.add(Spd{rf(rng)}, e);
        mgr.add(Rot{rf(rng), rf(rng), rf(rng), 1.0f}, e);
        mgr.add(Scl{rf(rng), rf(rng), rf(rng)}, e);
        Name nm; memset(nm.data, static_cast<int>(i & 0xFF), 32);
        mgr.add(nm, e);
        mgr.add(Mass{rf(rng) * 10.0f}, e);
    }
}

// === Section 1: group 基本查询 ===
template <typename... Ts>
static void test_group_basic(manager& mgr, size_t n, const char* types_str)
{
    print_header((string("Section 1: group basic (") + types_str + ", N=" + to_string(n) + ")").c_str());
    auto g = mgr.group<Ts...>();

    constexpr int REPEAT = 5;

    {
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += g.size();
                s += g.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("size/empty", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < n; ++i) s = g.contains(entity(static_cast<uint32_t>(opaque(i)), 1));
            (void)s;
        });
        print_ns("contains (hit)", n, ns / static_cast<double>(n));
    }

    {
        using First = tuple_element_t<0, tuple<Ts...>>;
        double ns = best_ns(REPEAT, [&]() {
            First* p = nullptr;
            for (size_t i = 0; i < n; ++i)
                p = g.template get<First>(entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get<T>", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            entity f{}, b{};
            for (size_t i = 0; i < n; ++i)
            {
                f = g.front();
                b = g.back();
            }
            compiler_barrier();
            (void)f; (void)b;
        });
        print_ns("front/back", 2 * n, ns / static_cast<double>(2 * n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            g.rebuild();
            compiler_barrier();
            return g.size();
        });
        print_ns("rebuild", 1, ns);
    }

    print_footer();
}

// === Section 2: group for_each ===
template <typename... Ts>
static void test_group_for_each(manager& mgr, size_t n, const char* types_str)
{
    print_header((string("Section 2: group for_each (") + types_str + ", N=" + to_string(n) + ")").c_str());
    auto g = mgr.group<Ts...>();

    constexpr int REPEAT = 5;

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float sink = 0;
            g.for_each([&](auto&... comps) {
                float tmp = 0;
                ((tmp += *reinterpret_cast<float*>(&comps)), ...);
                sink = tmp;
            });
            (void)sink;
        });
        print_ns("for_each (comps&...)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            entity sink{};
            g.for_each([&](entity e, auto&...) { sink = e; });
            compiler_barrier();
            (void)sink;
        });
        print_ns("for_each (entity, comps&...)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 3: owning_group ===
template <typename... Ts>
static void test_owning_group(manager& mgr, size_t n, const char* types_str)
{
    print_header((string("Section 3: owning_group (") + types_str + ", N=" + to_string(n) + ")").c_str());
    // group(owned_t<First>) 工厂: owned_t 仅需第一个类型 (First), 其余由 Rest... 推导
    using First = tuple_element_t<0, tuple<Ts...>>;
    auto og = mgr.group<Ts...>(ecs::owned_t<First>{});

    constexpr int REPEAT = 5;

    {
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += og.size();
                s += og.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("size/empty", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < n; ++i) s = og.contains(entity(static_cast<uint32_t>(opaque(i)), 1));
            (void)s;
        });
        print_ns("contains (hit)", n, ns / static_cast<double>(n));
    }

    {
        using First = tuple_element_t<0, tuple<Ts...>>;
        double ns = best_ns(REPEAT, [&]() {
            First* p = nullptr;
            for (size_t i = 0; i < n; ++i)
                p = og.template get<First>(entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get<T>", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            entity f{}, b{};
            for (size_t i = 0; i < n; ++i)
            {
                f = og.front();
                b = og.back();
            }
            compiler_barrier();
            (void)f; (void)b;
        });
        print_ns("front/back", 2 * n, ns / static_cast<double>(2 * n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            og.rebuild();
            compiler_barrier();
            return og.size();
        });
        print_ns("rebuild", 1, ns);
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float sink = 0;
            og.for_each([&](auto&... comps) {
                float tmp = 0;
                ((tmp += *reinterpret_cast<float*>(&comps)), ...);
                sink = tmp;
            });
            (void)sink;
        });
        print_ns("for_each (comps&...)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

int main()
{
    cout << "============================================================\n";
    cout << "  group / owning_group 独立性能测试\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K
    mt19937 rng(42);
    manager mgr;
    build_manager(mgr, N, rng);

    test_group_basic<Pos, Vel>(mgr, N, "2-comp");
    test_group_for_each<Pos, Vel>(mgr, N, "2-comp");

    test_group_basic<Pos, Vel, Hp>(mgr, N, "3-comp");
    test_group_for_each<Pos, Vel, Hp>(mgr, N, "3-comp");

    test_group_for_each<Pos, Vel, Hp, Dmg, Armor>(mgr, N, "5-comp");

    test_owning_group<Pos, Vel>(mgr, N, "2-comp");
    test_owning_group<Pos, Vel, Hp>(mgr, N, "3-comp");

    // 7~10 组件寄存器压力测试 (total_size: 7=60B, 8=72B, 9=104B, 10=108B)
    {
        cout << "\n============================================================\n";
        cout << "  7~10 组件寄存器压力测试 (10 组件 build)\n";
        cout << "============================================================\n";
        manager mgr10;
        build_manager_10(mgr10, N, rng);

        test_group_for_each<Pos, Vel, Hp, Dmg, Armor, Spd, Rot>(mgr10, N, "7-comp (60B)");
        test_group_for_each<Pos, Vel, Hp, Dmg, Armor, Spd, Rot, Scl>(mgr10, N, "8-comp (72B)");
        test_group_for_each<Pos, Vel, Hp, Dmg, Armor, Spd, Rot, Scl, Name>(mgr10, N, "9-comp (104B)");
        test_group_for_each<Pos, Vel, Hp, Dmg, Armor, Spd, Rot, Scl, Name, Mass>(mgr10, N, "10-comp (108B)");
    }

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
