// test_aggregate_reflect_perf.cpp - aggregate_reflect 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/aggregate_reflect.hpp"

using namespace std;

// === 测试用类型 ===
struct Pod1 { int a; };
struct Pod3 { float x, y, z; };
struct Pod4 { int a, b, c, d; };
struct Pod8 { int a, b, c, d, e, f, g, h; };
struct Pod16 { int a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p; };

// === Section 1: 编译期字段计数 ===
static void test_field_count()
{
    print_header("Section 1: field_count (compile-time)");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 1.1 单类型字段计数 (稳态)
    {
        volatile size_t sink = aggregate_field_count_v<Pod3>;
        (void)sink;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += aggregate_field_count_v<Pod3>;
            }
            (void)s;
        });
        print_ns("field_count_v<Pod3>", OPS, ns / static_cast<double>(OPS));
    }

    // 1.2 多类型字段计数
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += aggregate_field_count_v<Pod1>;
                s += aggregate_field_count_v<Pod3>;
                s += aggregate_field_count_v<Pod4>;
                s += aggregate_field_count_v<Pod8>;
                s += aggregate_field_count_v<Pod16>;
            }
            (void)s;
        });
        print_ns("field_count_v (5 types)", 5 * OPS, ns / static_cast<double>(5 * OPS));
    }

    print_footer();
}

// === Section 2: for_each_aggregate_member 遍历 ===
static void test_for_each_member()
{
    print_header("Section 2: for_each_aggregate_member");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 2.1 Pod3 遍历
    {
        Pod3 v{1.0f, 2.0f, 3.0f};
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                for_each_aggregate_member(v, [&](auto& member, size_t idx) {
                    s += static_cast<float>(member);
                    (void)idx;
                });
            }
            (void)s;
        });
        print_ns("for_each_member (Pod3, 3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 2.2 Pod8 遍历
    {
        Pod8 v{1, 2, 3, 4, 5, 6, 7, 8};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                for_each_aggregate_member(v, [&](auto& member, size_t idx) {
                    s += member;
                    (void)idx;
                });
            }
            (void)s;
        });
        print_ns("for_each_member (Pod8, 8 fields)", 8 * OPS, ns / static_cast<double>(8 * OPS));
    }

    // 2.3 Pod16 遍历
    {
        Pod16 v{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                for_each_aggregate_member(v, [&](auto& member, size_t idx) {
                    s += member;
                    (void)idx;
                });
            }
            (void)s;
        });
        print_ns("for_each_member (Pod16, 16 fields)", 16 * OPS, ns / static_cast<double>(16 * OPS));
    }

    // 2.4 空类型遍历
    {
        struct Empty {};
        Empty e{};
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                for_each_aggregate_member(e, [&](auto& member, size_t idx) {
                    (void)member; (void)idx;
                    s += 1;
                });
            }
            (void)s;
        });
        print_ns("for_each_member (Empty)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 3: member_offset 计算 ===
static void test_member_offset_perf()
{
    print_header("Section 3: member_offset");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 3.1 单成员偏移
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += member_offset(&Pod3::x);
            }
            (void)s;
        });
        print_ns("member_offset (1 field)", OPS, ns / static_cast<double>(OPS));
    }

    // 3.2 多成员偏移
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += member_offset(&Pod3::x);
                s += member_offset(&Pod3::y);
                s += member_offset(&Pod3::z);
            }
            (void)s;
        });
        print_ns("member_offset (3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 3.3 直接访问基线对照
    {
        Pod3 v{1.0f, 2.0f, 3.0f};
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v.x;
                s += v.y;
                s += v.z;
            }
            (void)s;
        });
        print_ns("direct access (baseline)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    print_footer();
}

// === Section 4: 索引访问遍历 ===
static void test_index_access()
{
    print_header("Section 4: index access via for_each");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 4.1 索引访问字段值
    {
        Pod4 v{10, 20, 30, 40};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                for_each_aggregate_member(v, [&](auto& member, size_t idx) {
                    if (idx == 0) s += member;
                    else if (idx == 1) s += member;
                    else if (idx == 2) s += member;
                    else if (idx == 3) s += member;
                });
            }
            (void)s;
        });
        print_ns("index access (Pod4)", 4 * OPS, ns / static_cast<double>(4 * OPS));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  aggregate_reflect 独立性能测试\n";
    cout << "============================================================\n";

    test_field_count();
    test_for_each_member();
    test_member_offset_perf();
    test_index_access();

    cout << "\n============================================================\n";
    cout << "  aggregate_reflect 性能测试完成\n";
    cout << "============================================================\n";
    return 0;
}
