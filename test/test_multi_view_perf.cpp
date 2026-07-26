// test_multi_view_perf.cpp - multi_view<T...> 独立性能测试
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
struct Speed { float v; };
struct Rot { float x, y, z, w; };
struct Scale { float x, y, z; };

// 构建测试 manager (所有实体都有所有组件, pools_aligned)
static void build_full_manager(manager& mgr, size_t n, mt19937& rng)
{
    uniform_real_distribution<float> rf(-1000, 1000);
    uniform_int_distribution<int> ri(0, 100);
    for (size_t i = 0; i < n; ++i)
    {
        entity e(static_cast<uint32_t>(i), 1);
        mgr.add(Pos{rf(rng), rf(rng), rf(rng)}, e);
        mgr.add(Vel{rf(rng), rf(rng), rf(rng)}, e);
        mgr.add(Hp{ri(rng)}, e);
        mgr.add(Dmg{ri(rng)}, e);
        mgr.add(Armor{ri(rng)}, e);
        mgr.add(Speed{rf(rng)}, e);
        mgr.add(Rot{rf(rng), rf(rng), rf(rng), rf(rng)}, e);
        mgr.add(Scale{rf(rng), rf(rng), rf(rng)}, e);
    }
}

// 构建部分匹配 manager (50% 实体有全部组件)
static void build_partial_manager(manager& mgr, size_t n, mt19937& rng)
{
    uniform_real_distribution<float> rf(-1000, 1000);
    uniform_int_distribution<int> ri(0, 100);
    for (size_t i = 0; i < n; ++i)
    {
        entity e(static_cast<uint32_t>(i), 1);
        mgr.add(Pos{rf(rng), rf(rng), rf(rng)}, e);
        if ((i & 1) == 0)
        {
            mgr.add(Vel{rf(rng), rf(rng), rf(rng)}, e);
            mgr.add(Hp{ri(rng)}, e);
        }
    }
}

// === Section 1: 基本查询接口 ===
template <typename... Ts>
static void test_basic_impl(manager& mgr, size_t n, const char* types_str)
{
    print_header((string("Section 1: basic (") + types_str + ", N=" + to_string(n) + ")").c_str());
    auto mv = mgr.view<Ts...>();

    constexpr int REPEAT = 5;

    {
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += mv.size();
                s += mv.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("size/empty", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < n; ++i) s = mv.contains(entity(static_cast<uint32_t>(opaque(i)), 1));
            (void)s;
        });
        print_ns("contains (hit)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            entity f{}, l{}, a{};
            for (size_t i = 0; i < n; ++i)
            {
                f = mv.get_first_entity();
                l = mv.get_last_entity();
                a = mv.get_entity_at_index(opaque(i % n));
            }
            compiler_barrier();
            (void)f; (void)l; (void)a;
        });
        print_ns("first/last/at_index", 3 * n, ns / static_cast<double>(3 * n));
    }

    {
        using First = tuple_element_t<0, tuple<Ts...>>;
        double ns = best_ns(REPEAT, [&]() {
            First* p = nullptr;
            for (size_t i = 0; i < n; ++i)
                p = mv.template get_component_for_entity<First>(entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get_component_for_entity", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 2: for_each ===
template <typename... Ts>
static void test_for_each_impl(manager& mgr, size_t n, const char* types_str)
{
    print_header((string("Section 2: for_each (") + types_str + ", N=" + to_string(n) + ")").c_str());
    auto mv = mgr.view<Ts...>();

    constexpr int REPEAT = 5;

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float sink = 0;
            mv.for_each([&](auto&... comps) {
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
            mv.for_each([&](entity e, auto&...) { sink = e; });
            compiler_barrier();
            (void)sink;
        });
        print_ns("for_each (entity, comps&...)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 3: 嵌套视图 ===
template <typename... Ts>
static void test_nested_impl(manager& mgr, size_t n, const char* types_str)
{
    print_header((string("Section 3: nested views (") + types_str + ", N=" + to_string(n) + ")").c_str());
    auto mv = mgr.view<Ts...>();

    constexpr int REPEAT = 3;

    {
        auto pv = mv.page(n / 4, n / 2);
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            pv.for_each([&](auto&...) { ++s; });
            compiler_barrier();
            return s;
        });
        print_ns("paged_view.for_each", n / 2, ns / static_cast<double>(n / 2));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            auto cv = mv.track_changes();
            volatile size_t s = cv.size();
            cv.for_each([](auto&...) {});
            compiler_barrier();
            return s;
        });
        print_ns("track_changes (build+iter)", n, ns / static_cast<double>(n));
    }

    {
        using First = tuple_element_t<0, tuple<Ts...>>;
        double ns = best_ns(REPEAT, [&]() {
            auto fc = mv.template filter_changed<First>();
            volatile size_t s = fc.size();
            fc.for_each([](auto&...) {});
            compiler_barrier();
            return s;
        });
        print_ns("filter_changed<T> (build+iter)", n, ns / static_cast<double>(n));

        ns = best_ns(REPEAT, [&]() {
            auto fa = mv.filter_any_changed();
            volatile size_t s = fa.size();
            fa.for_each([](auto&...) {});
            compiler_barrier();
            return s;
        });
        print_ns("filter_any_changed (build+iter)", n, ns / static_cast<double>(n));
    }

    {
        using First = tuple_element_t<0, tuple<Ts...>>;
        double ns = best_ns(REPEAT, [&]() {
            auto fa = mv.template filter_added<First>();
            volatile size_t s = fa.size();
            fa.for_each([](auto&...) {});
            compiler_barrier();
            return s;
        });
        print_ns("filter_added<T> (build+iter)", n, ns / static_cast<double>(n));
    }

    {
        // 构建只有一个匹配实体的 manager
        manager mgr2;
        entity e0(0, 1);
        mgr2.add(Pos{1, 2, 3}, e0);
        mgr2.add(Vel{4, 5, 6}, e0);
        auto mv2 = mgr2.view<Pos, Vel>();

        double ns = best_ns(REPEAT, [&]() {
            auto [p, v] = mv2.exactly_one();
            touch_ptr(&p);
            touch_ptr(&v);
            return 0;
        });
        print_ns("exactly_one", 1, ns);

        ns = best_ns(REPEAT, [&]() {
            auto [p, v] = mv2.find_one(e0);
            touch_ptr(p);
            touch_ptr(v);
            return p;
        });
        print_ns("find_one", 1, ns);
    }

    print_footer();
}

int main()
{
    cout << "============================================================\n";
    cout << "  multi_view<T...> 独立性能测试\n";
    cout << "  编译: MinGW GCC 15.2.0 -O3 -std=c++20 -mavx2 -mbmi -mbmi2\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K

    // pools_aligned 路径 (所有实体都有所有组件)
    cout << "\n=== pools_aligned (full match) ===\n";
    {
        mt19937 rng(42);
        manager mgr;
        build_full_manager(mgr, N, rng);

        test_basic_impl<Pos, Vel>(mgr, N, "2-comp aligned");
        test_for_each_impl<Pos, Vel>(mgr, N, "2-comp aligned");
        test_nested_impl<Pos, Vel>(mgr, N, "2-comp aligned");

        test_basic_impl<Pos, Vel, Hp>(mgr, N, "3-comp aligned");
        test_for_each_impl<Pos, Vel, Hp>(mgr, N, "3-comp aligned");

        test_for_each_impl<Pos, Vel, Hp, Dmg, Armor>(mgr, N, "5-comp aligned");
        test_for_each_impl<Pos, Vel, Hp, Dmg, Armor, Speed, Rot, Scale>(mgr, N, "8-comp aligned");
    }

    // 部分匹配路径 (pools_aligned_ = false)
    cout << "\n=== partial match (50% entities) ===\n";
    {
        mt19937 rng(42);
        manager mgr;
        build_partial_manager(mgr, N, rng);

        test_basic_impl<Pos, Vel, Hp>(mgr, N, "3-comp partial");
        test_for_each_impl<Pos, Vel, Hp>(mgr, N, "3-comp partial");
        test_nested_impl<Pos, Vel, Hp>(mgr, N, "3-comp partial");
    }

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
