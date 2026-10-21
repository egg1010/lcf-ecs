// test_highlevel_function_storage.cpp - 高层封装测试: 函数存储 (回调作为组件)
#include "test_common.hpp"
#include "include/ecs/component.hpp"
#include "include/part/type/t_fun.hpp"
#include "include/ecs/entity/entity.hpp"

static int add_ints(int a, int b) { return a + b; }
static int mul_ints(int a, int b) { return a * b; }
static int g_call_count = 0;
static int count_up(int x) { ++g_call_count; return x + 1; }

int main()
{
    t_begin("test_highlevel_function_storage — 高层封装测试: 函数存储");

    t_section(1, "t_fun 作为组件存储");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, t_fun<int(int, int)>{add_ints, 2, 3});

        auto* f = mgr.get_ptr<t_fun<int(int, int)>>(e);
        t_check("t_fun 存为组件", f != nullptr);
        t_check("t_fun 绑定参数调用", f != nullptr && (*f)() == 5);
    }

    t_section(2, "重新绑定参数调用");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, t_fun<int(int, int)>{mul_ints, 2, 3});
        auto* f = mgr.get_ptr<t_fun<int(int, int)>>(e);
        t_check("首次调用结果", f != nullptr && (*f)() == 6);
        t_check("传入参数调用覆盖", f != nullptr && (*f)(4, 5) == 20);
    }

    t_section(3, "result_ptr / result_reset");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, t_fun<int(int)>{count_up, 10});
        auto* f = mgr.get_ptr<t_fun<int(int)>>(e);
        t_check("调用并返回", f != nullptr && (*f)() == 11);

        int* rp = f ? f->result_ptr() : nullptr;
        t_check("result_ptr 指向结果", rp != nullptr && *rp == 11);

        if (f) { f->result_reset(); }
        rp = f ? f->result_ptr() : nullptr;
        t_check("result_reset 归零", rp != nullptr && *rp == 0);
    }

    t_section(4, "多实体独立回调");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        mgr.add(a, t_fun<int(int, int)>{add_ints, 1, 1});
        mgr.add(b, t_fun<int(int, int)>{mul_ints, 3, 3});

        auto* fa = mgr.get_ptr<t_fun<int(int, int)>>(a);
        auto* fb = mgr.get_ptr<t_fun<int(int, int)>>(b);
        t_check("实体 a 回调独立", fa != nullptr && (*fa)() == 2);
        t_check("实体 b 回调独立", fb != nullptr && (*fb)() == 9);
    }

    return t_end("function_storage ");
}
