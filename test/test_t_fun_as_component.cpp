// test_t_fun_as_component.cpp - 验证 t_fun 直接作为 ECS 组件
#include "test_common.hpp"
#include "include/component.hpp"
#include "include/part/t_fun.hpp"

// 回调目标函数
int  cb_add(int a, int b) { return a + b; }
int  cb_inc(int x) { return x + 1; }
void cb_void(int x) { (void)x; }
void cb_noop() {}

int g_void_call_count = 0;
void cb_count_void(int x) { (void)x; ++g_void_call_count; }

int main()
{
    // === 1. 非 void t_fun 作组件: 单实体 ===
    print_section(1, "非 void t_fun 作组件");
    {
        ecs::manager mgr;
        entity e = mgr.create_entity();

        t_fun v{cb_add, 10, 20};
        mgr.add(e, v);

        auto* p = mgr.get_ptr<t_fun<int(int,int)>>(e);
        print_item("get_ptr 非空", p != nullptr);
        print_item("调用结果 == 30", p && (*p)() == 30);
        print_item("fun() == 30", p && p->fun() == 30);

        // 修改绑定参数
        p->set_arg<0>(100);
        p->set_arg<1>(200);
        print_item("set_arg 后 == 300", p && (*p)() == 300);

        // 带参覆盖
        print_item("带参覆盖 (1,2) == 3", p && (*p)(1, 2) == 3);
    }

    // === 2. void t_fun 作组件 ===
    print_section(2, "void t_fun 作组件");
    {
        ecs::manager mgr;
        entity e = mgr.create_entity();

        g_void_call_count = 0;
        t_fun v{cb_count_void, 42};
        mgr.add(e, v);

        auto* p = mgr.get_ptr<t_fun<void(int)>>(e);
        print_item("void get_ptr 非空", p != nullptr);
        (*p)();
        print_item("void 调用后计数 == 1", g_void_call_count == 1);
        print_item("void result_ptr == nullptr", p && p->result_ptr() == nullptr);
    }

    // === 3. 无参 t_fun 作组件 ===
    print_section(3, "无参 t_fun 作组件");
    {
        ecs::manager mgr;
        entity e = mgr.create_entity();

        t_fun v{cb_noop};
        mgr.add(e, v);

        auto* p = mgr.get_ptr<t_fun<void()>>(e);
        print_item("无参 get_ptr 非空", p != nullptr);
        (*p)();
        print_item("无参调用成功", true);
    }

    // === 4. View 批量遍历 t_fun 组件 ===
    print_section(4, "View 批量遍历");
    {
        ecs::manager mgr;
        entity e1 = mgr.create_entity();
        entity e2 = mgr.create_entity();
        entity e3 = mgr.create_entity();

        mgr.add(e1, t_fun{cb_add, 1, 2});
        mgr.add(e2, t_fun{cb_add, 10, 20});
        mgr.add(e3, t_fun{cb_add, 100, 200});

        size_t count = 0;
        int sum = 0;
        mgr.view<t_fun<int(int,int)>>().for_each([&](entity, t_fun<int(int,int)>& c) {
            sum += c();
            ++count;
        });
        print_item("遍历 3 个组件", count == 3);
        print_item("结果之和 == 333 (3+30+300)", sum == 333);
    }

    // === 5. 单参 t_fun 作组件 ===
    print_section(5, "单参 t_fun 作组件");
    {
        ecs::manager mgr;
        entity e = mgr.create_entity();

        t_fun v{cb_inc, 41};
        mgr.add(e, v);

        auto* p = mgr.get_ptr<t_fun<int(int)>>(e);
        print_item("单参 get_ptr 非空", p != nullptr);
        print_item("调用 == 42", p && (*p)() == 42);
        print_item("带参覆盖 (99) == 100", p && (*p)(99) == 100);
    }

    // === 6. t_fun 高级接口在组件中可用 ===
    print_section(6, "组件中 t_fun 高级接口");
    {
        ecs::manager mgr;
        entity e = mgr.create_entity();

        t_fun v{cb_add, 10, 20};
        mgr.add(e, v);

        auto* p = mgr.get_ptr<t_fun<int(int,int)>>(e);
        print_item("target 可用", p && p->target() == cb_add);
        print_item("bound_arg<0> == 10", p && p->bound_arg<0>() == 10);
        print_item("empty == false", p && !p->empty());

        // then_call 链式
        int doubled = p->then_call([](int x){ return x * 2; });
        print_item("then_call == 60", doubled == 60);

        // reset
        p->reset(5, 6);
        print_item("reset 后 == 11", p && (*p)() == 11);

        // apply_n
        int last = p->apply_n(100);
        print_item("apply_n(100) == 11", last == 11);
    }

    // === 7. 修改组件内 t_fun 后持久化 ===
    print_section(7, "修改后持久化");
    {
        ecs::manager mgr;
        entity e = mgr.create_entity();

        mgr.add(e, t_fun{cb_add, 1, 2});

        // 第一次查询修改
        auto* p1 = mgr.get_ptr<t_fun<int(int,int)>>(e);
        p1->set_arg<0>(100);
        p1->set_arg<1>(200);

        // 第二次查询验证持久化
        auto* p2 = mgr.get_ptr<t_fun<int(int,int)>>(e);
        print_item("第二次查询仍非空", p2 != nullptr);
        print_item("修改持久化 == 300", p2 && (*p2)() == 300);
    }

    print_summary("t_fun 作为组件测试");
    return 0;
}
