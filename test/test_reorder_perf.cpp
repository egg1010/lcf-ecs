// test_reorder_perf.cpp - reorder_group<T...> 独立性能测试
#include "perf_common.hpp"
#include "include/component.hpp"

using namespace std;
using ecs::manager;
using ecs::entity;

struct Pos { float x, y, z; };
struct Vel { float vx, vy, vz; };
struct Hp  { int v; };

// 构建测试用 manager: 创建 n 个实体, 每个都拥有 Pos+Vel, 其中一半拥有 Hp
static void build_manager(manager& mgr, size_t n, mt19937& rng)
{
    uniform_real_distribution<float> rf(-1000, 1000);
    for (size_t i = 0; i < n; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add(e, Pos{rf(rng), rf(rng), rf(rng)});
        mgr.add(e, Vel{rf(rng), rf(rng), rf(rng)});
        if ((i & 1) == 0) mgr.add(e, Hp{static_cast<int>(i)});
    }
}

// === Section 1: 基本查询 ===
template <typename... Ts>
static void test_basic(manager& mgr, size_t n, const char* types_str)
{
    using First = tuple_element_t<0, tuple<Ts...>>;
    auto rg = mgr.group<Ts...>(ecs::reorder_t<First>{});

    print_header((string("Section 1: basic (") + types_str + ", N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    {
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += rg.size();
                s += rg.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("size/empty", OPS, ns / static_cast<double>(OPS));
    }

    {
        entity e0 = rg.front();
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < n; ++i) sink = rg.contains(e0);
            (void)sink;
        });
        print_ns("contains (hit)", n, ns / static_cast<double>(n));
    }

    {
        entity miss_e(static_cast<uint32_t>(n + 1000), 1);
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < OPS; ++i) sink = rg.contains(miss_e);
            (void)sink;
        });
        print_ns("contains (miss)", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            entity f{}, b{};
            for (size_t i = 0; i < n; ++i)
            {
                f = rg.front();
                b = rg.back();
            }
            compiler_barrier();
            (void)f; (void)b;
        });
        print_ns("front/back", 2 * n, ns / static_cast<double>(2 * n));
    }

    {
        entity e0 = rg.front();
        double ns = best_ns(REPEAT, [&]() {
            Pos* p = nullptr;
            for (size_t i = 0; i < n; ++i) p = rg.template get<Pos>(e0);
            touch_ptr(p);
            return p;
        });
        print_ns("get<Pos>", n, ns / static_cast<double>(n));
    }

    {
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += decay_t<decltype(rg)>::template find_type_index<Pos>();
                s += decay_t<decltype(rg)>::template find_type_index<Vel>();
            }
            (void)s;
        });
        print_ns("find_type_index (static)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 2: rebuild (重建 owned 数据) ===
template <typename... Ts>
static void test_rebuild(manager& mgr, size_t n, const char* types_str)
{
    using First = tuple_element_t<0, tuple<Ts...>>;
    auto rg = mgr.group<Ts...>(ecs::reorder_t<First>{});

    print_header((string("Section 2: rebuild (") + types_str + ", N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    {
        double ns = best_ns(REPEAT, [&]() {
            auto tmp = mgr.group<Ts...>(ecs::reorder_t<First>{});
            tmp.rebuild();
            compiler_barrier();
            return tmp.size();
        });
        print_ns("rebuild (cold)", 1, ns);
    }

    {
        rg.rebuild();
        double ns = best_ns(REPEAT, [&]() {
            rg.rebuild();
            compiler_barrier();
            return rg.size();
        });
        print_ns("rebuild (warm)", 1, ns);
    }

    {
        rg.rebuild();
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i) s += rg.size();
            (void)s;
        });
        print_ns("size() (ensure_fresh, no rebuild)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 3: for_each 遍历 ===
template <typename... Ts>
static void test_for_each(manager& mgr, size_t n, const char* types_str)
{
    using First = tuple_element_t<0, tuple<Ts...>>;
    auto rg = mgr.group<Ts...>(ecs::reorder_t<First>{});

    print_header((string("Section 3: for_each (") + types_str + ", N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float sink = 0;
            rg.for_each([&](auto&... comps) {
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
            volatile uint32_t sink = 0;
            rg.for_each([&](entity e, auto&...) { sink = e.parts_.index_; });
            (void)sink;
        });
        print_ns("for_each (entity, comps&...)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 4: share_with (共享 reorder_state) ===
template <typename... Ts>
static void test_share_with(manager& mgr, size_t n, const char* types_str)
{
    using First = tuple_element_t<0, tuple<Ts...>>;
    auto rg1 = mgr.group<Ts...>(ecs::reorder_t<First>{});
    auto rg2 = mgr.group<Ts...>(ecs::reorder_t<First>{});

    print_header((string("Section 4: share_with (") + types_str + ", N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    {
        rg1.rebuild();
        rg2.share_with(rg1);

        double ns = best_ns(REPEAT, [&]() {
            volatile float sink = 0;
            rg1.for_each([&](auto&... comps) {
                float tmp = 0;
                ((tmp += *reinterpret_cast<float*>(&comps)), ...);
                sink = tmp;
            });
            rg2.for_each([&](auto&... comps) {
                float tmp = 0;
                ((tmp += *reinterpret_cast<float*>(&comps)), ...);
                sink = tmp;
            });
            (void)sink;
        });
        print_ns("share_with (2-view iter)", 2 * n, ns / static_cast<double>(2 * n));
    }

    {
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += rg1.size();
                s += rg2.size();
            }
            (void)s;
        });
        print_ns("share_with size (shared)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: manager::reorder_by_component ===
static void test_reorder_by_component(manager& mgr, size_t n)
{
    print_header(("Section 5: reorder_by_component (N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    {
        double ns = best_ns(REPEAT, [&]() {
            mgr.reorder_by_component<Pos, Vel>([&](const Vel& a, const Vel& b) {
                return a.vx < b.vx;
            });
            compiler_barrier();
            return 0;
        });
        print_ns("reorder_by_component<Pos,Vel>", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            mgr.reorder_by_component<Pos, Hp>([&](const Hp& a, const Hp& b) {
                return a.v < b.v;
            });
            compiler_barrier();
            return 0;
        });
        print_ns("reorder_by_component<Pos,Hp> (partial)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

int main()
{
    cout << "============================================================\n";
    cout << "  reorder_group<T...> 独立性能测试\n";
    cout << "  编译: MinGW GCC 15.2.0 -O3 -std=c++20 -mavx2 -mbmi -mbmi2\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K
    mt19937 rng(42);
    manager mgr;
    build_manager(mgr, N, rng);

    // 2-组件查询
    test_basic<Pos, Vel>(mgr, N, "2-comp");
    test_rebuild<Pos, Vel>(mgr, N, "2-comp");
    test_for_each<Pos, Vel>(mgr, N, "2-comp");
    test_share_with<Pos, Vel>(mgr, N, "2-comp");

    // 3-组件查询 (Pos+Vel+Hp, 仅一半实体有 Hp)
    test_basic<Pos, Vel, Hp>(mgr, N / 2, "3-comp partial");
    test_rebuild<Pos, Vel, Hp>(mgr, N / 2, "3-comp partial");
    test_for_each<Pos, Vel, Hp>(mgr, N / 2, "3-comp partial");

    // reorder_by_component
    test_reorder_by_component(mgr, N);

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
