// test_module_invoke.cpp - 模块功能测试: invoke
#include "test_common.hpp"
#include "include/reflection/base/invoke.hpp"
#include <type_traits>

struct calc
{
    int add(int x, int y) { return x + y; }
    int scaled(int x) const { return x * 2; }
    static int mul(int x, int y) { return x * y; }
};

int main()
{
    t_begin("test_module_invoke — 模块功能测试: invoke");

    t_section(1, "mfn_traits 成员推导");
    {
        using mfn = decltype(&calc::add);
        using tr = reflect::detail::mfn_traits<mfn>;
        t_check("class_type 推导", std::is_same_v<typename tr::class_type, calc>);
        t_check("return_type 推导", std::is_same_v<typename tr::return_type, int>);
        t_check("arg_count == 2", tr::arg_count == 2);
        t_check("非静态", !tr::is_static);
    }

    t_section(2, "成员方法类型擦除调用");
    {
        using mfn = decltype(&calc::add);
        calc c;
        int x = 3;
        int y = 4;
        const void* args[2] = {&x, &y};
        int result = 0;
        reflect::detail::mfn_invoker_t<&calc::add, mfn>::invoke(&c, args, &result);
        t_check("成员调用结果", result == 7);
    }

    t_section(3, "const 成员方法");
    {
        using mfn = decltype(&calc::scaled);
        using tr = reflect::detail::mfn_traits<mfn>;
        t_check("const 标记", tr::is_const);

        calc c;
        int x = 21;
        const void* args[1] = {&x};
        int result = 0;
        reflect::detail::mfn_invoker_t<&calc::scaled, mfn>::invoke(&c, args, &result);
        t_check("const 调用结果", result == 42);
    }

    t_section(4, "静态方法");
    {
        using sfn = decltype(&calc::mul);
        using tr = reflect::detail::mfn_traits<sfn>;
        t_check("静态 is_static", tr::is_static);
        t_check("静态 arg_count == 2", tr::arg_count == 2);

        int x = 3;
        int y = 4;
        const void* args[2] = {&x, &y};
        int result = 0;
        reflect::detail::sfn_invoker_t<&calc::mul, sfn>::invoke(nullptr, args, &result);
        t_check("静态调用结果", result == 12);
    }

    t_section(5, "arg_ids_maker");
    {
        using mfn = decltype(&calc::add);
        dense<int> ids = reflect::detail::arg_ids_maker<mfn>::make();
        t_check("参数 id 数量 == 2", ids.size() == 2);
        t_check("参数 id 与 int 一致", ids[0] == type_id::get_type_id<int>());
    }

    return t_end("invoke ");
}
