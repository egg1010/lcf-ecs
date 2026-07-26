// test_type_id_perf.cpp - type_id 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/type_id.hpp"

using namespace std;

// 测试用类型
struct TypeA { int a; };
struct TypeB { int b; };
struct TypeC { int c; };
struct TypeD { int d; };
struct TypeE { int e; };

// === Section 1: get_type_id ===
static void test_get_type_id()
{
    print_header("Section: get_type_id");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 1.1 首次分配 (cold)
    {
        // 用新类型确保 cold (每次新类型)
        struct ColdType1 { int x; };
        struct ColdType2 { int x; };
        struct ColdType3 { int x; };
        double ns = best_ns(REPEAT, [&]() {
            int a = type_id::get_type_id<ColdType1>();
            int b = type_id::get_type_id<ColdType2>();
            int c = type_id::get_type_id<ColdType3>();
            compiler_barrier();
            return a + b + c;
        });
        print_ns("get_type_id (cold, new type)", 3, ns / 3.0);
    }

    // 1.2 稳态查询 (已分配类型)
    {
        // 预热
        volatile int a = type_id::get_type_id<TypeA>();
        volatile int b = type_id::get_type_id<TypeB>();
        volatile int c = type_id::get_type_id<TypeC>();
        (void)a; (void)b; (void)c;

        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += type_id::get_type_id<TypeA>();
                s += type_id::get_type_id<TypeB>();
                s += type_id::get_type_id<TypeC>();
            }
            (void)s;
        });
        print_ns("get_type_id (steady, 3 types)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 1.3 多类型混合查询
    {
        volatile int a = type_id::get_type_id<TypeA>();
        volatile int b = type_id::get_type_id<TypeB>();
        volatile int c = type_id::get_type_id<TypeC>();
        volatile int d = type_id::get_type_id<TypeD>();
        volatile int e = type_id::get_type_id<TypeE>();
        (void)a; (void)b; (void)c; (void)d; (void)e;

        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += type_id::get_type_id<TypeA>();
                s += type_id::get_type_id<TypeB>();
                s += type_id::get_type_id<TypeC>();
                s += type_id::get_type_id<TypeD>();
                s += type_id::get_type_id<TypeE>();
            }
            (void)s;
        });
        print_ns("get_type_id (5 types mixed)", 5 * OPS, ns / static_cast<double>(5 * OPS));
    }

    print_footer();
}

// === Section 2: current_max_id ===
static void test_current_max_id()
{
    print_header("Section: current_max_id");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 预热 (分配一些类型)
    volatile int a = type_id::get_type_id<TypeA>();
    (void)a;

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i) s += type_id::current_max_id();
            (void)s;
        });
        print_ns("current_max_id", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 3: 默认构造 ===
static void test_construct()
{
    print_header("Section: construct");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile type_id t;
            (void)t;
            compiler_barrier();
            return 0;
        });
        print_ns("default ctor", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  type_id 独立性能测试\n";
    cout << "  编译: MinGW GCC 15.2.0 -O3 -std=c++20 -mavx2 -mbmi -mbmi2\n";
    cout << "============================================================\n";

    test_get_type_id();
    test_current_max_id();
    test_construct();

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
