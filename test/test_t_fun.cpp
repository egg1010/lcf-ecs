// test_t_fun.cpp - t_fun 功能测试
#include "test_common.hpp"
#include "include/part/t_fun.hpp"

#include <string>
#include <cstdio>
#include <sstream>

// === 测试用类型 ===
int free_add(int a, int b) { return a + b; }
int free_identity(int x) { return x; }
void free_void(int x) { (void)x; }
void free_noop() {}
std::string free_make_str(const char* s) { return std::string(s); }
double free_pi_mul(double x) { return 3.14159 * x; }

// void 版本 apply_n / apply_range 测试用全局计数器
static int g_void_counter = 0;
void free_count_inc(int) { ++g_void_counter; }
int free_count_add(int x) { g_void_counter += x; return x; }

class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    int sum5(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
    static int version() { return 42; }
};

int main()
{
    // === 1. 函数指针 CTAD 推导 ===
    print_section(1, "函数指针 CTAD 推导");
    {
        t_fun v1{free_add, 10, 20};
        print_item("v1 绑定调用 == 30", v1() == 30);
        print_item("v1.fun() == 30", v1.fun() == 30);

        t_fun v2{free_identity, 42};
        print_item("v2 单参 == 42", v2() == 42);

        t_fun v3{free_noop};
        print_item("v3 无参 void", true);
        v3();
    }

    // === 2. 带参调用 (不覆盖绑定参数) ===
    print_section(2, "带参调用不覆盖");
    {
        t_fun v{free_add, 10, 20};
        print_item("v(3,4) == 7", v(3, 4) == 7);
        print_item("v() 仍 == 30 (绑定参数未改)", v() == 30);
        print_item("v.fun(5,6) == 11", v.fun(5, 6) == 11);
    }

    // === 3. result_ptr ===
    print_section(3, "result_ptr");
    {
        t_fun v{free_add, 10, 20};
        v();
        int* p = v.result_ptr();
        print_item("result_ptr 非空", p != nullptr);
        print_item("*result_ptr == 30", *p == 30);

        // void 版本
        t_fun vv{free_void, 0};
        void* vp = vv.result_ptr();
        print_item("void result_ptr == nullptr", vp == nullptr);
    }

    // === 4. result_reset ===
    print_section(4, "result_reset");
    {
        t_fun v{free_add, 10, 20};
        v();
        print_item("调用后 *result_ptr == 30", *v.result_ptr() == 30);
        v.result_reset();
        print_item("reset 后 *result_ptr == 0", *v.result_ptr() == 0);

        // void 版本
        t_fun vv{free_void, 0};
        vv.result_reset();
        print_item("void result_reset 无副作用", true);
    }

    // === 5. target ===
    print_section(5, "target");
    {
        t_fun v{free_add, 10, 20};
        auto t = v.target();
        print_item("target 非空", t != nullptr);
        print_item("target()(10,20) == 30", t(10, 20) == 30);
        print_item("target == free_add", t == free_add);
    }

    // === 6. set_arg / bound_arg ===
    print_section(6, "set_arg / bound_arg");
    {
        t_fun v{free_add, 0, 0};
        v.set_arg<0>(100);
        v.set_arg<1>(200);
        print_item("set_arg<0>==100", v.bound_arg<0>() == 100);
        print_item("set_arg<1>==200", v.bound_arg<1>() == 200);
        print_item("set_arg 后调用 == 300", v() == 300);
    }

    // === 7. 编译期元信息 ===
    print_section(7, "编译期元信息");
    {
        t_fun v{free_add, 10, 20};
        print_item("arity == 2", v.arity == 2);
        print_item("return_type 是 int", std::is_same_v<decltype(v)::return_type, int>);

        t_fun v2{free_noop};
        print_item("void arity == 0", v2.arity == 0);
        print_item("void return_type 是 void", std::is_same_v<decltype(v2)::return_type, void>);
    }

    // === 8. 成员函数指针 ===
    print_section(8, "成员函数指针");
    {
        Calculator calc;
        t_fun v{&Calculator::add, &calc, 10, 20};
        print_item("成员函数绑定调用 == 30", v() == 30);
        print_item("成员函数带参调用 == 7", v(3, 4) == 7);

        t_fun v2{&Calculator::sum5, &calc, 1, 2, 3, 4, 5};
        print_item("sum5 绑定 == 15", v2() == 15);

        t_fun v3{&Calculator::is_positive, &calc, 5};
        print_item("const 成员函数 == true", v3() == true);

        t_fun v4{&Calculator::no_return, &calc, 0};
        v4();
        print_item("void 成员函数", true);
    }

    // === 9. 成员函数 object/target ===
    print_section(9, "成员函数 object/target");
    {
        Calculator calc;
        t_fun v{&Calculator::add, &calc, 10, 20};
        print_item("object == &calc", v.object() == &calc);
        print_item("target == &Calculator::add", v.target() == &Calculator::add);
    }

    // === 10. 移动构造 ===
    print_section(10, "移动构造");
    {
        t_fun v{free_add, 10, 20};
        t_fun v2{std::move(v)};
        print_item("移动后 v2() == 30", v2() == 30);

        t_fun v3{free_add, 0, 0};
        v3 = std::move(v2);
        print_item("移动赋值后 v3() == 30", v3() == 30);
    }

    // === 11. 拷贝构造 ===
    print_section(11, "拷贝构造");
    {
        t_fun v{free_add, 10, 20};
        v();
        t_fun v2{v};
        print_item("拷贝后 v2() == 30", v2() == 30);
        print_item("拷贝后 *v2.result_ptr() == 30", *v2.result_ptr() == 30);
    }

    // === 12. std::string 返回值 ===
    print_section(12, "std::string 返回值");
    {
        t_fun v{free_make_str, "abc"};
        std::string s = v();
        print_item("string 返回 == abc", s == "abc");
        std::string* p = v.result_ptr();
        print_item("string result_ptr == abc", *p == "abc");

        v.result_reset();
        print_item("string reset 后为空", v.result_ptr()->empty());
    }

    // === 13. double 返回值 ===
    print_section(13, "double 返回值");
    {
        t_fun v{free_pi_mul, 2.0};
        double d = v();
        print_item("double 返回 ~6.28318", d > 6.28 && d < 6.29);
    }

    // === 14. then_call 链式调用 ===
    print_section(14, "then_call 链式调用");
    {
        t_fun v{free_add, 10, 20};
        auto r1 = v.then_call([](int x) { return x * 2; });
        print_item("then_call (30)*2 == 60", r1 == 60);

        t_fun v2{free_identity, 5};
        auto r2 = v2.then_call([](int x) { return x + 100; });
        print_item("then_call (5)+100 == 105", r2 == 105);

        // void 版本 then_call
        int counter = 0;
        t_fun vv{free_void, 0};
        vv.then_call([&]() { counter++; });
        print_item("void then_call 触发 g", counter == 1);
    }

    // === 15. compose 组合 ===
    print_section(15, "compose 组合");
    {
        t_fun v{free_identity, 5};
        auto r1 = v.compose([](int x) { return x + 1; }, [](int x) { return x * 2; });
        print_item("compose (5+1)*2 == 12", r1 == 12);

        t_fun v2{free_identity, 10};
        auto r2 = v2.compose([](int x) { return x - 3; }, [](int x) { return x * 10; }, [](int x) { return x + 1; });
        print_item("compose 3 级 ((10-3)*10)+1 == 71", r2 == 71);

        // void 版本 compose
        int counter = 0;
        t_fun vv{free_void, 0};
        vv.compose([&]() { counter++; }, [&]() { counter += 10; });
        print_item("void compose 触发 g+more (==11)", counter == 11);
    }

    // === 16. bind_front 部分应用 ===
    print_section(16, "bind_front 部分应用");
    {
        t_fun v{free_add, 0, 0};
        v.bind_front(100);
        v.set_arg<1>(200);
        print_item("bind_front(100) 后调用 == 300", v() == 300);

        t_fun v2{free_add, 0, 0};
        v2.bind_front(1, 2);
        print_item("bind_front(1,2) 后调用 == 3", v2() == 3);

        // 成员函数 bind_front
        Calculator calc;
        t_fun v3{&Calculator::sum5, &calc, 0, 0, 0, 0, 0};
        v3.bind_front(1, 2, 3);
        v3.set_arg<3>(4);
        v3.set_arg<4>(5);
        print_item("成员 bind_front(1,2,3) 后 sum5 == 15", v3() == 15);
    }

    // === 17. apply_n 批量调用 ===
    print_section(17, "apply_n 批量调用");
    {
        t_fun v{free_add, 1, 2};
        int r = v.apply_n(5);
        print_item("apply_n(5) 返回最后结果 == 3", r == 3);

        t_fun v2{free_identity, 42};
        int r2 = v2.apply_n(10);
        print_item("apply_n(10) identity == 42", r2 == 42);

        // void 版本 apply_n
        g_void_counter = 0;
        t_fun vv{free_count_inc, 0};
        vv.apply_n(7);
        print_item("void apply_n(7) 触发 7 次", g_void_counter == 7);
    }

    // === 18. apply_range 范围应用 ===
    print_section(18, "apply_range 范围应用");
    {
        int data[] = {1, 2, 3, 4, 5};
        t_fun v{free_identity, 0};
        int r = v.apply_range(data, 5);
        print_item("apply_range 返回最后元素 == 5", r == 5);

        int data2[] = {10, 20, 30};
        t_fun v2{free_identity, 0};
        int r2 = v2.apply_range(data2, 3);
        print_item("apply_range 3 元素 == 30", r2 == 30);

        // void 版本
        g_void_counter = 0;
        t_fun vv{free_count_inc, 0};
        vv.apply_range(data, 5);
        print_item("void apply_range 触发 5 次", g_void_counter == 5);
    }

    // === 19. swap 交换 ===
    print_section(19, "swap 交换");
    {
        t_fun v1{free_add, 10, 20};
        t_fun v2{free_add, 1, 2};
        v1.swap(v2);
        print_item("swap 后 v1() == 3", v1() == 3);
        print_item("swap 后 v2() == 30", v2() == 30);

        // 成员函数 swap
        Calculator calc;
        t_fun m1{&Calculator::add, &calc, 100, 200};
        t_fun m2{&Calculator::add, &calc, 1, 2};
        m1.swap(m2);
        print_item("成员 swap 后 m1() == 3", m1() == 3);
        print_item("成员 swap 后 m2() == 300", m2() == 300);
    }

    // === 20. operator== / != ===
    print_section(20, "operator== / !=");
    {
        t_fun v1{free_add, 10, 20};
        t_fun v2{free_add, 10, 20};
        t_fun v3{free_add, 1, 2};
        print_item("相同 f+args ==", v1 == v2);
        print_item("不同 args !=", v1 != v3);

        // 成员函数 ==
        Calculator calc;
        t_fun m1{&Calculator::add, &calc, 10, 20};
        t_fun m2{&Calculator::add, &calc, 10, 20};
        print_item("成员相同 ==", m1 == m2);

        // void 版本
        t_fun vv1{free_void, 0};
        t_fun vv2{free_void, 0};
        print_item("void 相同 ==", vv1 == vv2);
    }

    // === 21. hash ===
    print_section(21, "hash");
    {
        t_fun v1{free_add, 10, 20};
        t_fun v2{free_add, 10, 20};
        t_fun v3{free_identity, 5};
        print_item("相同对象 hash 相同", v1.hash() == v2.hash());
        print_item("不同 target hash 不同", v1.hash() != v3.hash());

        // 成员函数 hash
        Calculator calc;
        t_fun m1{&Calculator::add, &calc, 1, 2};
        t_fun m2{&Calculator::add, &calc, 1, 2};
        print_item("成员 hash 相同", m1.hash() == m2.hash());

        // void 版本 hash
        t_fun vv1{free_void, 0};
        t_fun vv2{free_void, 0};
        print_item("void 相同 hash 相同", vv1.hash() == vv2.hash());
    }

    // === 22. empty / release ===
    print_section(22, "empty / release");
    {
        t_fun v{free_add, 10, 20};
        print_item("构造后非空", !v.empty());

        v.release();
        print_item("release 后为空", v.empty());
        print_item("release 后 target == nullptr", v.target() == nullptr);

        // 成员函数版本
        Calculator calc;
        t_fun m{&Calculator::add, &calc, 1, 2};
        m.release();
        print_item("成员 release 后为空", m.empty());
        print_item("成员 release 后 object == nullptr", m.object() == nullptr);

        // void 版本
        t_fun vv{free_void, 0};
        vv.release();
        print_item("void release 后为空", vv.empty());
    }

    // === 23. reset 重置参数 ===
    print_section(23, "reset 重置参数");
    {
        t_fun v{free_add, 0, 0};
        v.reset(5, 6);
        print_item("reset(5,6) 后调用 == 11", v() == 11);

        v.reset(100, 200);
        print_item("reset(100,200) 后调用 == 300", v() == 300);

        // 成员函数版本
        Calculator calc;
        t_fun m{&Calculator::add, &calc, 0, 0};
        m.reset(7, 8);
        print_item("成员 reset(7,8) 后 == 15", m() == 15);
    }

    // === 24. operator<< 流输出 ===
    print_section(24, "operator<< 流输出");
    {
        t_fun v{free_add, 10, 20};
        std::ostringstream oss;
        oss << v;
        std::string s = oss.str();
        print_item("输出包含 arity=2", s.find("arity=2") != std::string::npos);
        print_item("输出包含 target=set", s.find("target=set") != std::string::npos);

        // 成员函数版本
        Calculator calc;
        t_fun m{&Calculator::add, &calc, 1, 2};
        std::ostringstream oss2;
        oss2 << m;
        print_item("成员输出包含 arity=2", oss2.str().find("arity=2") != std::string::npos);

        // 释放后输出
        t_fun vv{free_void, 0};
        vv.release();
        std::ostringstream oss3;
        oss3 << vv;
        print_item("release 后输出 target=null", oss3.str().find("target=null") != std::string::npos);
    }

    print_summary("t_fun 功能测试");
    return 0;
}
