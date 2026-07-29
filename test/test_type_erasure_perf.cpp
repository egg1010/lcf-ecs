// test_type_erasure_perf.cpp - type_erasure 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/type_erasure.hpp"

using namespace std;

// === 测试用类型 ===
class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    int sum5(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
    float to_float(int x) { return static_cast<float>(x); }
};

int free_multiply(int a, int b) { return a * b; }
int free_identity(int x) { return x; }

// === Section 1: 普通成员方法调用 ===
static void test_member_invoke()
{
    print_header("Section 1: member method invoke");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    Calculator calc;
    using FnType = decltype(&Calculator::add);
    constexpr FnType fn = &Calculator::add;

    // 1.1 2 参数方法
    {
        int a = opaque(10);
        int b = opaque(20);
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                const void* args[] = { &a, &b };
                alignas(alignof(std::max_align_t)) char result_buf[64];
                mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
                s += *reinterpret_cast<int*>(result_buf);
            }
            (void)s;
        });
        print_ns("invoke add (2 args)", OPS, ns / static_cast<double>(OPS));
    }

    // 1.2 5 参数方法
    {
        using FnType5 = decltype(&Calculator::sum5);
        constexpr FnType5 fn5 = &Calculator::sum5;
        int a = opaque(1), b = opaque(2), c = opaque(3), d = opaque(4), e = opaque(5);
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                const void* args[] = { &a, &b, &c, &d, &e };
                alignas(alignof(std::max_align_t)) char result_buf[64];
                mfn_invoker_t<fn5, FnType5>::invoke(&calc, args, result_buf);
                s += *reinterpret_cast<int*>(result_buf);
            }
            (void)s;
        });
        print_ns("invoke sum5 (5 args)", OPS, ns / static_cast<double>(OPS));
    }

    // 1.3 直接调用基线
    {
        int a = opaque(10);
        int b = opaque(20);
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += calc.add(a, b);
            }
            (void)s;
        });
        print_ns("direct call add (baseline)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 2: const 方法调用 ===
static void test_const_invoke()
{
    print_header("Section 2: const method invoke");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    Calculator calc;
    using FnType = decltype(&Calculator::is_positive);
    constexpr FnType fn = &Calculator::is_positive;

    {
        int x = opaque(5);
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i)
            {
                const void* args[] = { &x };
                alignas(alignof(std::max_align_t)) char result_buf[64];
                mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
                s = *reinterpret_cast<bool*>(result_buf);
            }
            (void)s;
        });
        print_ns("invoke is_positive (const)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 3: void 返回方法 ===
static void test_void_invoke()
{
    print_header("Section 3: void method invoke");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    Calculator calc;
    using FnType = decltype(&Calculator::no_return);
    constexpr FnType fn = &Calculator::no_return;

    {
        int x = opaque(42);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                const void* args[] = { &x };
                alignas(alignof(std::max_align_t)) char result_buf[64];
                mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
            }
        });
        print_ns("invoke no_return (void)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 4: 静态方法调用 ===
static void test_static_invoke()
{
    print_header("Section 4: static method invoke");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    using FnType = decltype(&free_multiply);
    constexpr FnType fn = &free_multiply;

    {
        int a = opaque(3);
        int b = opaque(4);
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                const void* args[] = { &a, &b };
                alignas(alignof(std::max_align_t)) char result_buf[64];
                sfn_invoker_t<fn, FnType>::invoke(nullptr, args, result_buf);
                s += *reinterpret_cast<int*>(result_buf);
            }
            (void)s;
        });
        print_ns("invoke multiply (static)", OPS, ns / static_cast<double>(OPS));
    }

    // 静态方法直接调用基线
    {
        int a = opaque(3);
        int b = opaque(4);
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += free_multiply(a, b);
            }
            (void)s;
        });
        print_ns("direct call multiply (baseline)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: 浮点返回值 ===
static void test_float_return()
{
    print_header("Section 5: float return value");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    Calculator calc;
    using FnType = decltype(&Calculator::to_float);
    constexpr FnType fn = &Calculator::to_float;

    {
        int x = opaque(42);
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                const void* args[] = { &x };
                alignas(alignof(std::max_align_t)) char result_buf[64];
                mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
                s += *reinterpret_cast<float*>(result_buf);
            }
            (void)s;
        });
        print_ns("invoke to_float (float return)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 6: traits 与元数据生成 ===
static void test_traits_meta()
{
    print_header("Section 6: traits and metadata");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 6.1 traits 查询
    {
        using FnType = decltype(&Calculator::add);
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += mfn_traits<FnType>::arg_count;
                s += mfn_traits<FnType>::is_const ? 1 : 0;
                s += mfn_traits<FnType>::is_static ? 1 : 0;
            }
            (void)s;
        });
        print_ns("mfn_traits query (3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 6.2 arg_ids_maker 生成
    {
        using FnType = decltype(&Calculator::sum5);
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                dense<int> ids = arg_ids_maker<FnType>::make();
                s += ids.size();
            }
            (void)s;
        });
        print_ns("arg_ids_maker make (5 args)", OPS, ns / static_cast<double>(OPS));
    }

    // 6.3 return_type_id 查询
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += return_type_id<int>();
                s += return_type_id<float>();
                s += return_type_id<void>();
            }
            (void)s;
        });
        print_ns("return_type_id (3 types)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  type_erasure 独立性能测试\n";
    cout << "============================================================\n";

    test_member_invoke();
    test_const_invoke();
    test_void_invoke();
    test_static_invoke();
    test_float_return();
    test_traits_meta();

    cout << "\n============================================================\n";
    cout << "  type_erasure 性能测试完成\n";
    cout << "============================================================\n";
    return 0;
}
