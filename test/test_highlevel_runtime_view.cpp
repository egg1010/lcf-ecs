// test_highlevel_runtime_view.cpp - 高层封装测试: runtime_view
#include "test_common.hpp"
#include "include/component.hpp"
#include "include/entity.hpp"
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

    return t_end("runtime_view ");
}
