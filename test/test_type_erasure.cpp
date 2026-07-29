// test_type_erasure.cpp - type_erasure 模块功能测试
#include "test_common.hpp"
#include "include/part/type_erasure.hpp"

// === 测试用类型 ===
class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    int sum5(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
    float to_float(int x) { return static_cast<float>(x); }
    double mix(int a, float b, double c) { return a + b + c; }
};

struct Greeter
{
    std::string greet(const std::string& name) { return "Hello, " + name; }
    int count() const { return 42; }
};

int free_multiply(int a, int b) { return a * b; }
int free_identity(int x) { return x; }
void free_no_op(int x) { (void)x; }

// === 测试辅助: 通过 invoker 调用方法 ===
template<auto Fn>
static int test_invoke_int(void* obj, int a, int b)
{
    using MFnType = decltype(Fn);
    const void* args[] = { &a, &b };
    alignas(alignof(std::max_align_t)) char result_buf[64];
    mfn_invoker_t<Fn, MFnType>::invoke(obj, args, result_buf);
    return *reinterpret_cast<int*>(result_buf);
}

int main()
{
    // === 1. mfn_traits 类型提取 ===
    print_section(1, "mfn_traits 类型提取");
    {
        using T1 = decltype(&Calculator::add);
        using traits1 = mfn_traits<T1>;
        print_item("add 是成员方法", !traits1::is_static);
        print_item("add 非 const", !traits1::is_const);
        print_item("add 参数数 == 2", traits1::arg_count == 2);
        static_assert(std::is_same_v<traits1::class_type, Calculator>);
        static_assert(std::is_same_v<traits1::return_type, int>);
        print_item("add class_type == Calculator", std::is_same_v<traits1::class_type, Calculator>);
        print_item("add return_type == int", std::is_same_v<traits1::return_type, int>);

        using T2 = decltype(&Calculator::is_positive);
        using traits2 = mfn_traits<T2>;
        print_item("is_positive 是 const", traits2::is_const);
        print_item("is_positive 参数数 == 1", traits2::arg_count == 1);

        using T3 = decltype(&free_multiply);
        using traits3 = mfn_traits<T3>;
        print_item("free_multiply 是静态", traits3::is_static);
        print_item("free_multiply 参数数 == 2", traits3::arg_count == 2);
    }

    // === 2. 普通成员方法调用 ===
    print_section(2, "普通成员方法调用");
    {
        Calculator calc;
        using FnType = decltype(&Calculator::add);
        constexpr FnType fn = &Calculator::add;
        int a = 10, b = 20;
        const void* args[] = { &a, &b };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
        int result = *reinterpret_cast<int*>(result_buf);
        print_item("add(10, 20) == 30", result == 30);

        int result2 = test_invoke_int<&Calculator::add>(&calc, 100, 200);
        print_item("add(100, 200) == 300", result2 == 300);
    }

    // === 3. const 成员方法调用 ===
    print_section(3, "const 成员方法调用");
    {
        Calculator calc;
        using FnType = decltype(&Calculator::is_positive);
        constexpr FnType fn = &Calculator::is_positive;
        int x = 5;
        const void* args[] = { &x };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
        bool result = *reinterpret_cast<bool*>(result_buf);
        print_item("is_positive(5) == true", result);

        x = -1;
        mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
        bool result2 = *reinterpret_cast<bool*>(result_buf);
        print_item("is_positive(-1) == false", !result2);
    }

    // === 4. void 返回方法 ===
    print_section(4, "void 返回方法");
    {
        Calculator calc;
        using FnType = decltype(&Calculator::no_return);
        constexpr FnType fn = &Calculator::no_return;
        int x = 42;
        const void* args[] = { &x };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
        print_item("no_return 调用成功", true);
    }

    // === 5. 多参数方法 ===
    print_section(5, "多参数方法");
    {
        Calculator calc;
        using FnType = decltype(&Calculator::sum5);
        constexpr FnType fn = &Calculator::sum5;
        int a = 1, b = 2, c = 3, d = 4, e = 5;
        const void* args[] = { &a, &b, &c, &d, &e };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
        int result = *reinterpret_cast<int*>(result_buf);
        print_item("sum5(1,2,3,4,5) == 15", result == 15);
    }

    // === 6. 浮点返回值 ===
    print_section(6, "浮点返回值");
    {
        Calculator calc;
        using FnType = decltype(&Calculator::to_float);
        constexpr FnType fn = &Calculator::to_float;
        int x = 42;
        const void* args[] = { &x };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
        float result = *reinterpret_cast<float*>(result_buf);
        print_item("to_float(42) == 42.0", result == 42.0f);
    }

    // === 7. 混合参数类型 ===
    print_section(7, "混合参数类型");
    {
        Calculator calc;
        using FnType = decltype(&Calculator::mix);
        constexpr FnType fn = &Calculator::mix;
        int a = 1;
        float b = 2.5f;
        double c = 3.5;
        const void* args[] = { &a, &b, &c };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        mfn_invoker_t<fn, FnType>::invoke(&calc, args, result_buf);
        double result = *reinterpret_cast<double*>(result_buf);
        print_item("mix(1, 2.5, 3.5) == 7.0", result == 7.0);
    }

    // === 8. 静态方法调用 ===
    print_section(8, "静态方法调用");
    {
        using FnType = decltype(&free_multiply);
        constexpr FnType fn = &free_multiply;
        int a = 3, b = 4;
        const void* args[] = { &a, &b };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        sfn_invoker_t<fn, FnType>::invoke(nullptr, args, result_buf);
        int result = *reinterpret_cast<int*>(result_buf);
        print_item("multiply(3, 4) == 12", result == 12);

        using FnType2 = decltype(&free_identity);
        constexpr FnType2 fn2 = &free_identity;
        int x = 99;
        const void* args2[] = { &x };
        sfn_invoker_t<fn2, FnType2>::invoke(nullptr, args2, result_buf);
        int result2 = *reinterpret_cast<int*>(result_buf);
        print_item("identity(99) == 99", result2 == 99);
    }

    // === 9. 静态 void 方法 ===
    print_section(9, "静态 void 方法");
    {
        using FnType = decltype(&free_no_op);
        constexpr FnType fn = &free_no_op;
        int x = 42;
        const void* args[] = { &x };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        sfn_invoker_t<fn, FnType>::invoke(nullptr, args, result_buf);
        print_item("free_no_op 调用成功", true);
    }

    // === 10. 返回 std::string ===
    print_section(10, "返回 std::string");
    {
        Greeter g;
        using FnType = decltype(&Greeter::greet);
        constexpr FnType fn = &Greeter::greet;
        std::string name = "World";
        const void* args[] = { &name };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        mfn_invoker_t<fn, FnType>::invoke(&g, args, result_buf);
        std::string* result = reinterpret_cast<std::string*>(result_buf);
        print_item("greet(World) == Hello, World", result->find("Hello, World") == 0);
        result->~basic_string();
    }

    // === 11. arg_ids_maker ===
    print_section(11, "arg_ids_maker");
    {
        using FnType = decltype(&Calculator::add);
        dense<int> ids = arg_ids_maker<FnType>::make();
        print_item("add arg_ids 数量 == 2", ids.size() == 2);
        print_item("add arg[0] == int id", ids[0] == type_id::get_type_id<int>());
        print_item("add arg[1] == int id", ids[1] == type_id::get_type_id<int>());

        using FnType2 = decltype(&Calculator::mix);
        dense<int> ids2 = arg_ids_maker<FnType2>::make();
        print_item("mix arg_ids 数量 == 3", ids2.size() == 3);
        print_item("mix arg[0] == int id", ids2[0] == type_id::get_type_id<int>());
        print_item("mix arg[1] == float id", ids2[1] == type_id::get_type_id<float>());
        print_item("mix arg[2] == double id", ids2[2] == type_id::get_type_id<double>());
    }

    // === 12. return_type_id ===
    print_section(12, "return_type_id");
    {
        print_item("int return_type_id 匹配", return_type_id<int>() == type_id::get_type_id<int>());
        print_item("float return_type_id 匹配", return_type_id<float>() == type_id::get_type_id<float>());
        print_item("void return_type_id == -1", return_type_id<void>() == -1);
    }

    // === 13. invoker_func 类型 ===
    print_section(13, "invoker_func 类型");
    {
        using FnType = decltype(&Calculator::add);
        constexpr FnType fn = &Calculator::add;
        invoker_func inv = &mfn_invoker_t<fn, FnType>::invoke;
        Calculator calc;
        int a = 5, b = 7;
        const void* args[] = { &a, &b };
        alignas(alignof(std::max_align_t)) char result_buf[64];
        inv(&calc, args, result_buf);
        int result = *reinterpret_cast<int*>(result_buf);
        print_item("通过 invoker_func 调用 add(5,7) == 12", result == 12);
    }

    print_summary("功能测试");
    return 0;
}
