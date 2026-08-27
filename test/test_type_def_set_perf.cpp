// test_type_def_set_perf.cpp - single_class_set def 池性能测试
// 分组: def vs 模板路径对比 (同尺寸组件) / 模板路径回归 (def 特性共存时)
// 方法: opaque() + 轮转阻止常量折叠与 CSE; best_ns 多次取最小
#include "perf_common.hpp"
#include "include/component.hpp"

#include <vector>

using namespace ecs;

// 全局 volatile sink
static volatile uintptr_t g_ptr_sink = 0;
static volatile int g_int_sink = 0;

// 对比输出: 模板路径为基线
static void print_compare(const char* label,
                          double tpl_ns, double def_ns) noexcept
{
    double ratio = (tpl_ns > 0) ? def_ns / tpl_ns : 0;
    const char* verdict;
    if (def_ns <= tpl_ns * 1.10)      verdict = "[PAR]";
    else if (def_ns <= tpl_ns * 1.50) verdict = "[+10~50%]";
    else                             verdict = "[SLOW]";
    std::cout << "  " << std::left << std::setw(30) << label
              << " | 模板 " << std::fixed << std::setprecision(3) << std::setw(8) << tpl_ns << " ns"
              << " | def " << std::setw(8) << def_ns << " ns"
              << " | 比 " << std::setprecision(2) << std::setw(5) << ratio << " " << verdict << "\n";
}

// 测试组件 (12B, 同 def 注册尺寸)
struct Comp12 { float x, y, z; };

static type_def make_def12() noexcept
{
    type_def d;
    d.size = 12;
    d.alignment = 4;
    d.trivially_copyable = true;
    return d;
}

// === Section 1: 添加 (append fast path) ===
static void test_add_compare()
{
    print_header("Section: 添加 (末尾追加, 12B)");
    constexpr size_t N = 100000;

    // 模板基线
    {
        manager mgr;
        std::vector<entity> es;
        es.reserve(N);
        for (size_t i = 0; i < N; ++i) es.push_back(mgr.create_entity());
        Comp12 c{1.0f, 2.0f, 3.0f};
        for (size_t i = 0; i < 10; ++i) mgr.add(es[i], c);  // 预热

        double ns = best_ns(5, [&]() {
            for (size_t i = 10; i < N; ++i)
            {
                mgr.add(es[i], c);
            }
            compiler_barrier();
            return 0;
        });
        // 每轮重建会重复添加 → 用覆盖语义, 仍为合法路径
        print_ns("模板 add<T> (覆盖追加)", N - 10, ns / static_cast<double>(N - 10));
    }

    // def 路径
    {
        manager mgr;
        const int id = type_id::register_type_def("PerfDef12", make_def12());
        std::vector<entity> es;
        es.reserve(N);
        for (size_t i = 0; i < N; ++i) es.push_back(mgr.create_entity());
        Comp12 c{1.0f, 2.0f, 3.0f};
        for (size_t i = 0; i < 10; ++i) mgr.add_def(es[i], id, &c);

        double ns = best_ns(5, [&]() {
            for (size_t i = 10; i < N; ++i)
            {
                mgr.add_def(es[i], id, &c);
            }
            compiler_barrier();
            return 0;
        });
        print_ns("def add_def (覆盖追加)", N - 10, ns / static_cast<double>(N - 10));
    }

    print_footer();
}

// === Section 2: 顺序查询 (hot set 命中) ===
static void test_get_sequential()
{
    print_header("Section: 顺序查询 (hot set, 12B)");
    constexpr size_t N = 100000;
    constexpr size_t OPS = 1000000;

    double tpl_ns = 0, def_ns = 0;

    // 模板基线
    {
        manager mgr;
        std::vector<entity> es;
        es.reserve(N);
        for (size_t i = 0; i < N; ++i)
        {
            es.push_back(mgr.create_entity());
            Comp12 c{float(i), 2, 3};
            mgr.add(es[i], c);
        }
        volatile Comp12* sink = nullptr;
        tpl_ns = best_ns(5, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                const size_t idx = (i * 31) & (N - 1);
                sink = mgr.get_ptr_fast<Comp12>(es[idx]);
            }
            compiler_barrier();
            return 0;
        });
        (void)sink;
        print_ns("模板 get_ptr_fast<T>", OPS, tpl_ns / static_cast<double>(OPS));
    }

    // def 路径
    {
        manager mgr;
        const int id = type_id::register_type_def("PerfGet12", make_def12());
        std::vector<entity> es;
        es.reserve(N);
        for (size_t i = 0; i < N; ++i)
        {
            es.push_back(mgr.create_entity());
            Comp12 c{float(i), 2, 3};
            mgr.add_def(es[i], id, &c);
        }
        volatile void* sink = nullptr;
        def_ns = best_ns(5, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                const size_t idx = (i * 31) & (N - 1);
                sink = mgr.get_def_ptr(es[idx], id);
            }
            compiler_barrier();
            return 0;
        });
        (void)sink;
        print_ns("def get_def_ptr", OPS, def_ns / static_cast<double>(OPS));
    }

    print_compare("顺序查询对比",
                  tpl_ns / static_cast<double>(OPS), def_ns / static_cast<double>(OPS));
    print_footer();
}

// === Section 3: 随机查询 ===
static void test_get_random()
{
    print_header("Section: 随机查询 (sparse 慢路径, 12B)");
    constexpr size_t N = 100000;
    constexpr size_t OPS = 1000000;

    std::vector<size_t> order;
    order.reserve(OPS);
    std::mt19937_64 rng(12345);
    for (size_t i = 0; i < OPS; ++i) order.push_back(rng() % N);

    double tpl_ns = 0, def_ns = 0;

    {
        manager mgr;
        std::vector<entity> es;
        es.reserve(N);
        for (size_t i = 0; i < N; ++i)
        {
            es.push_back(mgr.create_entity());
            Comp12 c{float(i), 2, 3};
            mgr.add(es[i], c);
        }
        volatile Comp12* sink = nullptr;
        tpl_ns = best_ns(5, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                sink = mgr.get_ptr_fast<Comp12>(es[order[i]]);
            }
            compiler_barrier();
            return 0;
        });
        (void)sink;
        print_ns("模板 get_ptr_fast<T> (随机)", OPS, tpl_ns / static_cast<double>(OPS));
    }

    {
        manager mgr;
        const int id = type_id::register_type_def("PerfRnd12", make_def12());
        std::vector<entity> es;
        es.reserve(N);
        for (size_t i = 0; i < N; ++i)
        {
            es.push_back(mgr.create_entity());
            Comp12 c{float(i), 2, 3};
            mgr.add_def(es[i], id, &c);
        }
        volatile void* sink = nullptr;
        def_ns = best_ns(5, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                sink = mgr.get_def_ptr(es[order[i]], id);
            }
            compiler_barrier();
            return 0;
        });
        (void)sink;
        print_ns("def get_def_ptr (随机)", OPS, def_ns / static_cast<double>(OPS));
    }

    print_compare("随机查询对比",
                  tpl_ns / static_cast<double>(OPS), def_ns / static_cast<double>(OPS));
    print_footer();
}

// === Section 4: 移除 ===
static void test_remove_compare()
{
    print_header("Section: hard_remove (尾部搬运, 12B)");
    constexpr size_t N = 100000;
    constexpr int REPEAT = 5;

    double tpl_ns = 0, def_ns = 0;

    {
        for (int r = 0; r < REPEAT; ++r)
        {
            manager mgr;
            std::vector<entity> es;
            es.reserve(N);
            for (size_t i = 0; i < N; ++i)
            {
                es.push_back(mgr.create_entity());
                Comp12 c{float(i), 2, 3};
                mgr.add(es[i], c);
            }
            timer t;
            for (size_t i = 0; i < N; i += 2)
            {
                mgr.hard_remove<Comp12>(es[i]);
            }
            const double ns = t.elapsed_nanoseconds();
            if (ns < tpl_ns || r == 0) tpl_ns = ns;
        }
        print_ns("模板 hard_remove<T> (隔一删一)", N / 2, tpl_ns / static_cast<double>(N / 2));
    }

    {
        for (int r = 0; r < REPEAT; ++r)
        {
            manager mgr;
            const int id = type_id::register_type_def("PerfRm12", make_def12());
            std::vector<entity> es;
            es.reserve(N);
            for (size_t i = 0; i < N; ++i)
            {
                es.push_back(mgr.create_entity());
                Comp12 c{float(i), 2, 3};
                mgr.add_def(es[i], id, &c);
            }
            timer t;
            for (size_t i = 0; i < N; i += 2)
            {
                mgr.hard_remove_def(es[i], id);
            }
            const double ns = t.elapsed_nanoseconds();
            if (ns < def_ns || r == 0) def_ns = ns;
        }
        print_ns("def hard_remove_def (隔一删一)", N / 2, def_ns / static_cast<double>(N / 2));
    }

    print_compare("移除对比",
                  tpl_ns / static_cast<double>(N / 2), def_ns / static_cast<double>(N / 2));
    print_footer();
}

// === Section 5: 模板路径回归 (def 组件共存于同一 manager) ===
static void test_template_regression()
{
    print_header("Section: 模板路径回归 (def 共存)");
    constexpr size_t N = 100000;
    constexpr size_t OPS = 1000000;

    manager mgr;
    // def 组件存在且活跃 (触发 ops 表填充与掩码多块)
    const int id = type_id::register_type_def("PerfCoex", make_def12());
    entity ed = mgr.create_entity();
    Comp12 cd{7, 8, 9};
    mgr.add_def(ed, id, &cd);

    std::vector<entity> es;
    es.reserve(N);
    for (size_t i = 0; i < N; ++i)
    {
        es.push_back(mgr.create_entity());
        Comp12 c{float(i), 2, 3};
        mgr.add(es[i], c);
    }

    // 添加
    {
        Comp12 c{1.0f, 2.0f, 3.0f};
        double ns = best_ns(5, [&]() {
            for (size_t i = 0; i < N; ++i)
            {
                mgr.add(es[i], c);   // 覆盖
            }
            compiler_barrier();
            return 0;
        });
        print_ns("模板 add<T> (def 共存, 覆盖)", N, ns / static_cast<double>(N));
    }

    // 查询
    {
        volatile Comp12* sink = nullptr;
        double ns = best_ns(5, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                const size_t idx = (i * 31) & (N - 1);
                sink = mgr.get_ptr_fast<Comp12>(es[idx]);
            }
            compiler_barrier();
            return 0;
        });
        (void)sink;
        print_ns("模板 get_ptr_fast<T> (def 共存)", OPS, ns / static_cast<double>(OPS));
    }

    // 移除
    {
        double best = 1e18;
        for (int r = 0; r < 5; ++r)
        {
            manager m2;
            std::vector<entity> es2;
            es2.reserve(N);
            for (size_t i = 0; i < N; ++i)
            {
                es2.push_back(m2.create_entity());
                Comp12 c{float(i), 2, 3};
                m2.add(es2[i], c);
            }
            entity ed2 = m2.create_entity();
            Comp12 cd2{7, 8, 9};
            m2.add_def(ed2, type_id::register_type_def("PerfCoex2", make_def12()), &cd2);
            timer t;
            for (size_t i = 0; i < N; i += 2)
            {
                m2.hard_remove<Comp12>(es2[i]);
            }
            const double ns = t.elapsed_nanoseconds();
            if (ns < best) best = ns;
        }
        print_ns("模板 hard_remove<T> (def 共存)", N / 2, best / static_cast<double>(N / 2));
    }

    print_footer();
}

int main()
{
    std::cout << "============================================================\n";
    std::cout << "  single_class_set def 池 性能测试\n";
    std::cout << "============================================================\n";

    test_add_compare();
    test_get_sequential();
    test_get_random();
    test_remove_compare();
    test_template_regression();

    std::cout << "\n============================================================\n";
    std::cout << "  测试完成\n";
    std::cout << "============================================================\n";
    return 0;
}
