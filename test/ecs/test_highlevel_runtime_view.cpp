// test_highlevel_runtime_view.cpp - 高层封装测试: runtime_view
#include "test_common.hpp"
#include "include/ecs/manager.hpp"
#include "include/ecs/entity/entity.hpp"
#include <array>

struct position
{
    float x;
    float y;
};

struct velocity
{
    float vx;
    float vy;
};

struct health
{
    int hp;
};

int main()
{
    t_begin("test_highlevel_runtime_view — 高层封装测试: runtime_view");

    t_section(1, "按类型 id 构造与查询");
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

        const int pos_id = type_id::get_type_id<position>();
        const int vel_id = type_id::get_type_id<velocity>();
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, vel_id});

        t_check("runtime_view count == 2", rv.count() == 2);
        t_check("runtime_view size == 2", rv.size() == 2);
        t_check("runtime_view !empty", !rv.empty());
        t_check("runtime_view contains(a)", rv.contains(a));
        t_check("runtime_view !contains(c)", !rv.contains(c));

        size_t cnt = 0;
        rv.for_each([&](ecs::entity e) {
            (void)e;
            ++cnt;
        });
        t_check("runtime_view for_each 计数", cnt == 2);

        auto first = rv.get_first_entity();
        t_check("runtime_view get_first_entity 有效", first.is_valid());
    }

    t_section(2, "缓存失效与重建");
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

        const int pos_id = type_id::get_type_id<position>();
        const int vel_id = type_id::get_type_id<velocity>();
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, vel_id});

        size_t before = rv.count();
        mgr.soft_remove<velocity>(a);
        size_t after = rv.count();
        t_check("运行时缓存失效感知删除", before == 2 && after == 1);

        size_t cnt = 0;
        rv.for_each([&](ecs::entity) { ++cnt; });
        t_check("失效后 for_each 一致", cnt == 1);
    }

    t_section(3, "排除类型");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});
        mgr.add(a, health{10});
        mgr.add(b, position{2.0f, 0.0f});

        const int pos_id = type_id::get_type_id<position>();
        const int hp_id = type_id::get_type_id<health>();
        auto rv = mgr.runtime_view_create(std::array<int, 1>{pos_id},
                                          std::array<int, 1>{hp_id});
        t_check("排除 health 后 count == 1", rv.count() == 1);
    }

    t_section(4, "缓存随非主集合刷新");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});

        const int pos_id = type_id::get_type_id<position>();
        const int vel_id = type_id::get_type_id<velocity>();
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, vel_id});
        t_check("初始 count == 0", rv.count() == 0);

        mgr.add(a, velocity{1.0f, 1.0f});
        t_check("补非主组件后 count 刷新", rv.count() == 1);
        t_check("补非主组件后 size 刷新", rv.size() == 1);
        t_check("补非主组件后 !empty", !rv.empty());

        (void)mgr.soft_remove<velocity>(a);
        t_check("删非主组件后 count 刷新", rv.count() == 0);
        t_check("删非主组件后 empty", rv.empty());

        size_t cnt = 0;
        rv.for_each([&](ecs::entity e) {
            (void)e;
            ++cnt;
        });
        t_check("删非主组件后 for_each 一致", cnt == 0);
    }

    t_section(5, "纯 OR 查询各接口一致");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});

        ecs::runtime_term term{};
        term.type_id = type_id::get_type_id<velocity>();
        term.op = 1;
        auto rv = mgr.runtime_view_create_from_terms(&term, 1);
        t_check("后注册 OR 类型前 count == 0", rv.count() == 0);

        mgr.add(a, velocity{2.0f, 0.0f});
        t_check("后注册 OR 类型后 count == 1", rv.count() == 1);
        t_check("纯 OR size 一致", rv.size() == 1 && !rv.empty());
        t_check("纯 OR get_first_entity 有效", rv.get_first_entity().is_valid());
        t_check("纯 OR contains 命中", rv.contains(a));

        size_t visited = 0;
        for (ecs::entity e : rv)
        {
            (void)e;
            ++visited;
        }
        t_check("纯 OR begin/end 遍历一致", visited == 1);

        size_t paged = 0;
        rv.for_each_paged(0, 4, [&](ecs::entity) { ++paged; });
        t_check("纯 OR 分页遍历一致", paged == 1);
    }

    t_section(6, "后注册 NOT 类型触发刷新");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});

        ecs::runtime_term terms[2]{};
        terms[0].type_id = type_id::get_type_id<position>();
        terms[0].op = 0;
        terms[1].type_id = type_id::get_type_id<health>();
        terms[1].op = 2;
        auto rv = mgr.runtime_view_create_from_terms(terms, 2);
        t_check("NOT 类型注册前命中", rv.count() == 1);

        mgr.add(a, health{5});
        t_check("NOT 类型注册后排除", rv.count() == 0);
        t_check("NOT 类型注册后 empty", rv.empty());
    }

    return t_end("runtime_view ");
}
