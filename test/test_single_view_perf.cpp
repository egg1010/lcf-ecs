// test_single_view_perf.cpp - single_view<T> 独立性能测试
#include "perf_common.hpp"
#include "include/component.hpp"

using namespace std;
using ecs::manager;
using ecs::entity;
// single_view 是 manager 的嵌套类, 直接用 manager::single_view 引用
template <typename T>
using single_view = manager::single_view<T>;

struct Pos { float x, y, z; };
struct Vel { float vx, vy, vz; };
struct Hp  { int v; };
struct Name { char buf[32]; };

// 构建测试用 manager
template <typename T>
static void build_manager(manager& mgr, size_t n, mt19937& rng)
{
    // 预分配实体, 确保 entity_manager 的 masks_ 有足够容量
    for (size_t i = 0; i < n; ++i) mgr.create_entity();

    if constexpr (is_same_v<T, Pos>)
    {
        uniform_real_distribution<float> d(-1000, 1000);
        for (size_t i = 0; i < n; ++i) mgr.add(Pos{d(rng), d(rng), d(rng)}, entity(static_cast<uint32_t>(i), 1));
    }
    else if constexpr (is_same_v<T, Vel>)
    {
        uniform_real_distribution<float> d(-100, 100);
        for (size_t i = 0; i < n; ++i) mgr.add(Vel{d(rng), d(rng), d(rng)}, entity(static_cast<uint32_t>(i), 1));
    }
    else if constexpr (is_same_v<T, Hp>)
    {
        uniform_int_distribution<int> d(0, 100);
        for (size_t i = 0; i < n; ++i) mgr.add(Hp{d(rng)}, entity(static_cast<uint32_t>(i), 1));
    }
    else if constexpr (is_same_v<T, Name>)
    {
        for (size_t i = 0; i < n; ++i)
        {
            Name nm; memset(nm.buf, 0, 32); snprintf(nm.buf, 32, "E%zu", i);
            mgr.add(nm, entity(static_cast<uint32_t>(i), 1));
        }
    }
}

// === Section 1: 基本查询接口 ===
template <typename T>
static void test_basic(size_t n)
{
    print_header(("Section 1: basic (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    mt19937 rng(42);
    manager mgr;
    build_manager<T>(mgr, n, rng);
    auto sv = mgr.view<T>();

    constexpr int REPEAT = 5;

    {
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += sv.size();
                s += sv.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("size/empty", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < n; ++i) s = sv.contains(entity(static_cast<uint32_t>(opaque(i)), 1));
            (void)s;
        });
        print_ns("contains (hit)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < n; ++i) s = sv.contains(entity(static_cast<uint32_t>(opaque(i + n)), 1));
            (void)s;
        });
        print_ns("contains (miss)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = nullptr;
            for (size_t i = 0; i < n; ++i) p = sv.get_component_for_entity(entity(static_cast<uint32_t>(opaque(i)), 1));
            touch_ptr(p);
            return p;
        });
        print_ns("get_component_for_entity", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            entity f{}, l{}, a{};
            for (size_t i = 0; i < n; ++i)
            {
                f = sv.get_first_entity();
                l = sv.get_last_entity();
                a = sv.get_entity_at_index(opaque(i % n));
            }
            compiler_barrier();
            (void)f; (void)l; (void)a;
        });
        print_ns("first/last/at_index", 3 * n, ns / static_cast<double>(3 * n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = nullptr;
            for (size_t i = 0; i < n; ++i) p = sv.get_component_at_index(opaque(i % n));
            touch_ptr(p);
            return p;
        });
        print_ns("get_component_at_index", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 2: 迭代器 ===
template <typename T>
static void test_iterator(size_t n)
{
    print_header(("Section 2: iterator (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    mt19937 rng(42);
    manager mgr;
    build_manager<T>(mgr, n, rng);
    auto sv = mgr.view<T>();

    constexpr int REPEAT = 5;

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t cnt = 0;
            for (auto it = sv.begin(); it != sv.end(); ++it)
            {
                entity e = *it;
                touch_ptr(&e);
                ++cnt;
            }
            compiler_barrier();
            return cnt;
        });
        print_ns("begin/end (entity iter)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t cnt = 0;
            for (auto it = sv.component_begin(); it != sv.component_end(); ++it)
            {
                touch_ptr(&*it);
                ++cnt;
            }
            compiler_barrier();
            return cnt;
        });
        print_ns("component_begin/end", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 3: for_each ===
template <typename T>
static void test_for_each(size_t n)
{
    print_header(("Section 3: for_each (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    mt19937 rng(42);
    manager mgr;
    build_manager<T>(mgr, n, rng);
    auto sv = mgr.view<T>();

    constexpr int REPEAT = 5;

    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            sv.for_each([&](T& v) { sink = v; });
            compiler_barrier();
            (void)sink;
        });
        print_ns("for_each (T&)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            entity sink{};
            sv.for_each([&](entity e, T&) { sink = e; });
            compiler_barrier();
            (void)sink;
        });
        print_ns("for_each (entity, T&)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 4: 嵌套视图 ===
template <typename T>
static void test_nested_views(size_t n)
{
    print_header(("Section 4: nested views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    mt19937 rng(42);
    manager mgr;
    build_manager<T>(mgr, n, rng);
    auto sv = mgr.view<T>();

    constexpr int REPEAT = 3;

    {
        auto pv = sv.page(n / 4, n / 2);
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            pv.for_each([&](T&) { ++s; });
            compiler_barrier();
            return s;
        });
        print_ns("paged_view.for_each", n / 2, ns / static_cast<double>(n / 2));

        double ns2 = best_ns(REPEAT, [&]() {
            volatile size_t s = pv.size();
            volatile bool e = pv.empty();
            (void)s; (void)e;
            return s;
        });
        print_ns("paged_view.size/empty", 1, ns2);
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            auto sorted = sv.sorted_by_component([](const T& a, const T& b) { return &a < &b; });
            volatile size_t s = sorted.size();
            sorted.for_each([](T&) {});
            compiler_barrier();
            return s;
        });
        print_ns("sorted_by_component (build+iter)", n, ns / static_cast<double>(n));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            auto cv = sv.track_changes();
            volatile size_t s = cv.size();
            cv.for_each([](T&) {});
            compiler_barrier();
            return s;
        });
        print_ns("track_changes (build+iter)", n, ns / static_cast<double>(n));

        ns = best_ns(REPEAT, [&]() {
            auto fv = sv.filter_changed();
            volatile size_t s = fv.size();
            fv.for_each([](T&) {});
            compiler_barrier();
            return s;
        });
        print_ns("filter_changed (build+iter)", n, ns / static_cast<double>(n));

        ns = best_ns(REPEAT, [&]() {
            auto fa = sv.filter_added();
            volatile size_t s = fa.size();
            fa.for_each([](T&) {});
            compiler_barrier();
            return s;
        });
        print_ns("filter_added (build+iter)", n, ns / static_cast<double>(n));
    }

    {
        manager mgr2;
        auto e2 = mgr2.create_entity();
        mgr2.add(T{}, e2);
        auto sv2 = mgr2.view<T>();
        double ns = best_ns(REPEAT, [&]() {
            volatile const T& r = sv2.exactly_one();
            (void)r;
            compiler_barrier();
            return 0;
        });
        print_ns("exactly_one", 1, ns);
    }

    print_footer();
}

int main()
{
    cout << "============================================================\n";
    cout << "  single_view<T> 独立性能测试\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K

    cout << "\n=== Pos (12B) ===\n";
    test_basic<Pos>(N);
    test_iterator<Pos>(N);
    test_for_each<Pos>(N);
    test_nested_views<Pos>(N);

    cout << "\n=== Hp (4B) ===\n";
    test_basic<Hp>(N);
    test_for_each<Hp>(N);

    cout << "\n=== Name (32B) ===\n";
    test_basic<Name>(N);
    test_for_each<Name>(N);

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
