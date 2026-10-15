// test_highlevel_group.cpp - 高层封装测试: group / owning_group / reorder_group
#include "test_common.hpp"
#include "include/component.hpp"
#include "include/entity.hpp"

struct position
{
    float x;
    float y;
    bool operator<(const position& o) const { return x < o.x; }
};

struct velocity
{
    float vx;
    float vy;
};

int main()
{
    t_begin("test_highlevel_group — 高层封装测试: group 系统");

    t_section(1, "group 基础");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        auto c = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});
        mgr.add(a, velocity{1.0f, 1.0f});
        mgr.add(b, position{2.0f, 0.0f});
        mgr.add(b, velocity{2.0f, 2.0f});
        mgr.add(c, position{3.0f, 0.0f});

        auto g = mgr.group<position, velocity>();
        t_check("group size == 2", g.size() == 2);
        t_check("group !empty", !g.empty());
        t_check("group contains(a)", g.contains(a));
        t_check("group !contains(c)", !g.contains(c));

        int cnt = 0;
        g.for_each([&](position& p, velocity& v) {
            (void)p;
            (void)v;
            ++cnt;
        });
        t_check("group for_each 计数", cnt == 2);

        auto e1 = g.front();
        auto e2 = g.back();
        t_check("group front/back 有效", e1.is_valid() && e2.is_valid());

        auto* pp = g.get<position>(a);
        t_check("group get<position>", pp != nullptr && pp->x == 1.0f);
    }

    t_section(2, "group 空集守卫");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});

        auto g = mgr.group<position, velocity>();
        t_check("空 group size == 0", g.size() == 0 && g.empty());
        t_check("空 group front 无效", !g.front().is_valid());
        t_check("空 group back 无效", !g.back().is_valid());
    }

    t_section(3, "owning_group");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        auto c = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});
        mgr.add(b, position{2.0f, 0.0f});
        mgr.add(c, position{3.0f, 0.0f});

        auto og = mgr.group(ecs::owned<position>);
        t_check("owning_group size == 3", og.size() == 3);
        t_check("owning_group contains(a)", og.contains(a));
    }

    t_section(4, "reorder_group");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        mgr.add(a, position{2.0f, 0.0f});
        mgr.add(a, velocity{1.0f, 0.0f});
        mgr.add(b, position{1.0f, 0.0f});
        mgr.add(b, velocity{2.0f, 0.0f});

        auto rg = mgr.group(ecs::reorder<position>);
        t_check("reorder_group size == 2", rg.size() == 2);
        t_check("reorder_group contains(a)", rg.contains(a));
    }

    return t_end("group ");
}
