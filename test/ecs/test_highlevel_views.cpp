// test_highlevel_views.cpp - 高层封装测试: 视图系统
#include "test_common.hpp"
#include "include/ecs/manager.hpp"
#include "include/ecs/entity/entity.hpp"

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

template <size_t I>
struct filler_component
{
    int value;
};

template <size_t... Is>
static void reserve_filler_components(ecs::manager& mgr, std::index_sequence<Is...>)
{
    (mgr.preallocate_components<filler_component<Is>>(1), ...);
}

int main()
{
    t_begin("test_highlevel_views — 高层封装测试: 视图系统");

    t_section(1, "single_view");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});
        mgr.add(b, position{2.0f, 0.0f});

        int cnt = 0;
        float sum = 0;
        mgr.view<position>().for_each([&](position& p) { ++cnt; sum += p.x; });
        t_check("single_view for_each 计数", cnt == 2);
        t_check("single_view 数值累加", sum == 3.0f);

        int ecnt = 0;
        mgr.view<position>().for_each([&](ecs::entity e, position& p) {
            (void)e;
            (void)p;
            ++ecnt;
        });
        t_check("single_view for_each(entity) 计数", ecnt == 2);
    }

    t_section(2, "multi_view");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        auto c = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});
        mgr.add(a, velocity{10.0f, 0.0f});
        mgr.add(b, position{2.0f, 0.0f});
        mgr.add(b, velocity{20.0f, 0.0f});
        mgr.add(c, position{3.0f, 0.0f});

        int cnt = 0;
        mgr.view<position, velocity>().for_each([&](position& p, velocity& v) {
            (void)p;
            (void)v;
            ++cnt;
        });
        t_check("multi_view 仅命中双组件", cnt == 2);

        int ecnt = 0;
        float acc = 0;
        mgr.view<position, velocity>().for_each([&](ecs::entity e, position& p, velocity& v) {
            (void)e;
            ++ecnt;
            acc += p.x + v.vx;
        });
        t_check("multi_view for_each(entity)", ecnt == 2 && acc == (1.0f + 10.0f) + (2.0f + 20.0f));
    }

    t_section(3, "without 过滤");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});
        mgr.add(b, position{2.0f, 0.0f});
        mgr.add(b, velocity{1.0f, 1.0f});

        int cnt = 0;
        mgr.view<position>(ecs::without<velocity>).for_each([&](position& p) {
            (void)p;
            ++cnt;
        });
        t_check("view(without<V>) 排除含 V", cnt == 1);
    }

    t_section(4, "with 过滤");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});
        mgr.add(b, position{2.0f, 0.0f});
        mgr.add(b, health{100});

        int cnt = 0;
        int with_cnt = 0;
        mgr.view<position>(ecs::with<health>).for_each([&](position& p, health* h) {
            (void)p;
            ++cnt;
            if (h != nullptr) { ++with_cnt; }
        });
        t_check("view(with<H>) 遍历全 T 并附带 H*", cnt == 2 && with_cnt == 1);
    }

    t_section(5, "or_view");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});
        mgr.add(b, velocity{2.0f, 0.0f});

        int cnt = 0;
        mgr.view_or<position, velocity>().for_each([&](ecs::entity e, position* p, velocity* v) {
            (void)e;
            (void)p;
            (void)v;
            ++cnt;
        });
        t_check("view_or 命中并集", cnt == 2);
    }

    t_section(6, "any_of_view");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        auto c = mgr.create_entity();
        mgr.add(a, position{1.0f, 0.0f});
        mgr.add(b, velocity{2.0f, 0.0f});
        mgr.add(b, health{10});
        (void)c;

        int cnt = 0;
        int position_only = 0;
        int velocity_only = 0;
        int invalid = 0;
        float position_sum = 0.0f;
        float velocity_sum = 0.0f;
        mgr.view_any_of<position, velocity>().for_each([&](ecs::entity e, position* p, velocity* v) {
            (void)e;
            ++cnt;
            if (p != nullptr && v == nullptr)
            {
                ++position_only;
                position_sum += p->x;
            }
            else if (p == nullptr && v != nullptr)
            {
                ++velocity_only;
                velocity_sum += v->vx;
            }
            else
            {
                ++invalid;
            }
        });
        t_check("view_any_of 命中并集", cnt == 2);
        t_check("view_any_of 指针配对正确", position_only == 1 && velocity_only == 1
            && invalid == 0);
        t_check("view_any_of 指针取值正确", position_sum == 1.0f && velocity_sum == 2.0f);
    }

    t_section(7, "组件类型扩容后旧视图仍有效");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, position{4.0f, 5.0f});
        auto view = mgr.view<position>();
        auto runtime = mgr.runtime_view_create(std::array<int, 1>{
            type_id::get_type_id<position>()
        });

        reserve_filler_components(mgr, std::make_index_sequence<96>{});

        float sum = 0.0f;
        view.for_each([&](position& p) { sum += p.x; });
        size_t runtime_count = 0;
        runtime.for_each([&](ecs::entity) { ++runtime_count; });
        t_check("扩容后 single_view 仍可遍历", sum == 4.0f);
        t_check("扩容后 runtime_view 仍可遍历", runtime_count == 1);
    }

    return t_end("views ");
}
