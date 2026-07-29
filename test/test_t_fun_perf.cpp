// test_t_fun_perf.cpp - t_fun 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/t_fun.hpp"

using namespace std;

// === 测试用类型 ===
class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    int sum5(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
};

int free_add(int a, int b) { return a + b; }
void free_void(int x) { (void)x; }
int free_identity(int x) { return x; }

// === Section 1: 函数指针 绑定参数调用 ===
static void test_free_bound()
{
    print_header("Section 1: free function bound args");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    int a = opaque(10);
    int b = opaque(20);

    // 1.1 t_fun 绑定参数调用
    {
        t_fun v{free_add, a, b};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v();
            }
            (void)s;
        });
        print_ns("t_fun free bound ()", OPS, ns / static_cast<double>(OPS));
    }

    // 1.2 直接调用基线
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += free_add(a, b);
            }
            (void)s;
        });
        print_ns("direct free_add", OPS, ns / static_cast<double>(OPS));
    }

    // 1.3 fun() 接口
    {
        t_fun v{free_add, a, b};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v.fun();
            }
            (void)s;
        });
        print_ns("t_fun free fun()", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 2: 函数指针 带参覆盖调用 ===
static void test_free_override()
{
    print_header("Section 2: free function override args");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    int a = opaque(10);
    int b = opaque(20);

    // 2.1 t_fun 带参覆盖
    {
        t_fun v{free_add, 0, 0};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v(a, b);
            }
            (void)s;
        });
        print_ns("t_fun free override (a,b)", OPS, ns / static_cast<double>(OPS));
    }

    // 2.2 直接调用基线
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += free_add(a, b);
            }
            (void)s;
        });
        print_ns("direct free_add", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 3: 成员函数指针 绑定参数调用 ===
static void test_member_bound()
{
    print_header("Section 3: member function bound args");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    Calculator calc;
    int a = opaque(10);
    int b = opaque(20);

    // 3.1 t_fun 成员函数绑定
    {
        t_fun v{&Calculator::add, &calc, a, b};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v();
            }
            (void)s;
        });
        print_ns("t_fun member bound ()", OPS, ns / static_cast<double>(OPS));
    }

    // 3.2 直接调用基线
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += calc.add(a, b);
            }
            (void)s;
        });
        print_ns("direct calc.add", OPS, ns / static_cast<double>(OPS));
    }

    // 3.3 const 成员函数
    {
        t_fun v{&Calculator::is_positive, &calc, 5};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("t_fun const member ()", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 4: 成员函数指针 带参覆盖 ===
static void test_member_override()
{
    print_header("Section 4: member function override args");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    Calculator calc;
    int a = opaque(10);
    int b = opaque(20);

    // 4.1 t_fun 成员函数带参覆盖
    {
        t_fun v{&Calculator::add, &calc, 0, 0};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v(a, b);
            }
            (void)s;
        });
        print_ns("t_fun member override (a,b)", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 直接调用基线
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += calc.add(a, b);
            }
            (void)s;
        });
        print_ns("direct calc.add", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: void 返回值 ===
static void test_void_return()
{
    print_header("Section 5: void return");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    int a = opaque(10);

    // 5.1 void 函数指针绑定调用
    {
        t_fun v{free_void, a};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                v();
            }
        });
        print_ns("t_fun void free ()", OPS, ns / static_cast<double>(OPS));
    }

    // 5.2 void 直接调用基线
    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                free_void(a);
            }
        });
        print_ns("direct free_void", OPS, ns / static_cast<double>(OPS));
    }

    // 5.3 void 成员函数
    {
        Calculator calc;
        t_fun v{&Calculator::no_return, &calc, a};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                v();
            }
        });
        print_ns("t_fun void member ()", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 6: result_ptr / result_reset / set_arg ===
static void test_aux_interfaces()
{
    print_header("Section 6: aux interfaces");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    int a = opaque(10);
    int b = opaque(20);

    // 6.1 result_ptr 访问
    {
        t_fun v{free_add, a, b};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                v();
                s += *v.result_ptr();
            }
            (void)s;
        });
        print_ns("t_fun result_ptr read", OPS, ns / static_cast<double>(OPS));
    }

    // 6.2 result_reset 重置
    {
        t_fun v{free_add, a, b};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                v();
                v.result_reset();
            }
        });
        print_ns("t_fun result_reset", OPS, ns / static_cast<double>(OPS));
    }

    // 6.3 set_arg 修改绑定参数
    {
        t_fun v{free_add, 0, 0};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                v.set_arg<0>(a);
                v.set_arg<1>(b);
                v();
                s += *v.result_ptr();
            }
            (void)s;
        });
        print_ns("t_fun set_arg + ()", OPS, ns / static_cast<double>(OPS));
    }

    // 6.4 bound_arg 读取
    {
        t_fun v{free_add, a, b};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v.bound_arg<0>() + v.bound_arg<1>();
            }
            (void)s;
        });
        print_ns("t_fun bound_arg read", OPS, ns / static_cast<double>(OPS));
    }

    // 6.5 target 访问
    {
        t_fun v{free_add, a, b};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v.target()(a, b);
            }
            (void)s;
        });
        print_ns("t_fun target() direct", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 7: 移动构造 / 拷贝 ===
static void test_move_copy()
{
    print_header("Section 7: move / copy");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    int a = opaque(10);
    int b = opaque(20);

    // 7.1 移动构造
    {
        t_fun v{free_add, a, b};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                t_fun v2{std::move(v)};
                v2();
                s += *v2.result_ptr();
                v = std::move(v2);
            }
            (void)s;
        });
        print_ns("t_fun move construct", OPS, ns / static_cast<double>(OPS));
    }

    // 7.2 拷贝构造
    {
        t_fun v{free_add, a, b};
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                t_fun v2{v};
                v2();
                s += *v2.result_ptr();
            }
            (void)s;
        });
        print_ns("t_fun copy construct", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

int main()
{
    std::cout << "=== t_fun 性能测试 ===\n";

    test_free_bound();
    test_free_override();
    test_member_bound();
    test_member_override();
    test_void_return();
    test_aux_interfaces();
    test_move_copy();

    std::cout << "\n=== 测试结束 ===\n";
    return 0;
}
