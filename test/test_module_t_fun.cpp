// test_module_t_fun.cpp - 模块功能测试: t_fun
#include "test_common.hpp"
#include "include/part/t_fun.hpp"

static int add_ints(int a, int b) { return a + b; }
static int g_accum = 0;
static void accumulate(int x) { g_accum += x; }

int main()
{
    t_begin("test_module_t_fun — 模块功能测试: t_fun");

    t_section(1, "返回值版本");
    {
        t_fun<int(int, int)> f{add_ints, 2, 3};
        t_check("arity == 2", t_fun<int(int, int)>::arity == 2);
        t_check("绑定参数调用", f() == 5);
        t_check("重复调用稳定", f() == 5);
        t_check("传入参数调用", f(10, 20) == 30);
    }

    t_section(2, "result_ptr / result_reset");
    {
        t_fun<int(int, int)> f{add_ints, 6, 7};
        int* rp = f.result_ptr();
        t_check("result_ptr 非空", rp != nullptr);
        f();
        t_check("result_ptr 反映结果", rp != nullptr && *rp == 13);
        f.result_reset();
        t_check("result_reset 归零", rp != nullptr && *rp == 0);
    }

    t_section(3, "void 版本");
    {
        g_accum = 0;
        t_fun<void(int)> vf{accumulate, 0};
        vf();
        t_check("void 绑定调用", g_accum == 0);
        vf(5);
        t_check("void 传入参数调用", g_accum == 5);
        vf(3);
        t_check("void 累加", g_accum == 8);
    }

    t_section(4, "类型属性");
    {
        using tf = t_fun<int(int, int)>;
        t_check("return_type", std::is_same_v<typename tf::return_type, int>);
        t_check("args_tuple", std::is_same_v<typename tf::args_tuple, std::tuple<int, int>>);
    }

    return t_end("t_fun ");
}
