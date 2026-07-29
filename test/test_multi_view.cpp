// test_multi_view.cpp - multi_view<T...> 功能测试
// 覆盖: 基本查询/for_each/without/page/exactly_one/find_one/
//       track_changes/filter_changed/filter_added/sorted_by_component/view_any_of
#include "test_common.hpp"

using ecs::entity;
using ecs::manager;

int main()
{
    // === 1. 基本查询 ===
    print_section(1, "基本查询 (size/empty/contains)");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();

        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});

        auto mv = mgr.view<Position, Velocity>();
        print_item("2-comp size == 2", mv.size() == 2);
        print_item("2-comp pool_size == 2", mv.pool_size() == 2);
        print_item("2-comp empty false", !mv.empty());
        print_item("contains e1", mv.contains(e1));
        print_item("contains e3 false", !mv.contains(e3));

        auto mv3 = mgr.view<Position, Velocity, Health>();
        print_item("3-comp (无 Health) empty", mv3.empty());
        print_item("3-comp (无 Health) size == 0", mv3.size() == 0);
        int mv3_cnt = 0;
        mv3.for_each([&](Position&, Velocity&, Health&) { ++mv3_cnt; });
        print_item("3-comp (无 Health) for_each == 0", mv3_cnt == 0);

        mgr.add(e1, Health{100, 100});
        mgr.add(e2, Health{80, 100});
        auto mv3b = mgr.view<Position, Velocity, Health>();
        print_item("3-comp (补 Health) size == 2", mv3b.size() == 2);
        print_item("3-comp (补 Health) pool_size == 2", mv3b.pool_size() == 2);
        print_item("3-comp contains e1", mv3b.contains(e1));
    }

    // === 2. for_each ===
    print_section(2, "for_each (comp / entity+comp)");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();

        mgr.add(e1, Position{1, 2, 3});
        mgr.add(e2, Position{4, 5, 6});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});

        auto mv = mgr.view<Position, Velocity>();

        // 仅组件
        int comp_cnt = 0;
        float sum_px = 0;
        mv.for_each([&](Position& p, Velocity&) { ++comp_cnt; sum_px += p.x; });
        print_item("for_each [comp] 计数 == 2", comp_cnt == 2);
        print_item("for_each [comp] px 之和 == 5", sum_px == 5.0f);

        // entity + 组件
        int ent_cnt = 0;
        bool found_e1 = false;
        mv.for_each([&](entity e, Position&, Velocity&) {
            ++ent_cnt;
            if (e == e1) found_e1 = true;
        });
        print_item("for_each [ent+comp] 计数 == 2", ent_cnt == 2);
        print_item("for_each [ent+comp] 找到 e1", found_e1);

        // for_each 修改组件
        mv.for_each([](Position& p, Velocity&) { p.x *= 10; });
        float modified = 0;
        mv.for_each([&](Position& p, Velocity&) { modified += p.x; });
        print_item("for_each 修改后 px 之和 == 50", modified == 50.0f);
    }

    // === 3. without 排除 ===
    print_section(3, "without<T> 排除");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();

        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});   // e1 有 Velocity

        auto ev = mgr.view<Position>(ecs::without<Velocity>);
        print_item("without<Vel> 不含 e1", !ev.contains(e1));
        print_item("without<Vel> 含 e2", ev.contains(e2));

        int cnt = 0;
        ev.for_each([&](Position&) { ++cnt; });
        print_item("without<Vel> for_each 计数 == 2", cnt == 2);
    }

    // === 4. page 分页 ===
    print_section(4, "page 分页");
    {
        manager mgr;
        mgr.append_preallocated_entities(20);
        dense<entity> ents;
        for (int i = 0; i < 10; ++i)
        {
            auto e = mgr.create_entity();
            ents.emplace_back(e);
            mgr.add(e, Position{static_cast<float>(i), 0, 0});
            mgr.add(e, Velocity{static_cast<float>(i * 10), 0, 0});
        }

        auto mv = mgr.view<Position, Velocity>();
        print_item("全量 size == 10", mv.size() == 10);
        print_item("全量 pool_size == 10", mv.pool_size() == 10);

        // 第一页 (offset=0, limit=3)
        auto p1 = mv.page(0, 3);
        int cnt1 = 0;
        p1.for_each([&](Position&, Velocity&) { ++cnt1; });
        print_item("page(0,3) 计数 == 3", cnt1 == 3);

        // 中间页 (offset=4, limit=3)
        auto p2 = mv.page(4, 3);
        int cnt2 = 0;
        p2.for_each([&](Position&, Velocity&) { ++cnt2; });
        print_item("page(4,3) 计数 == 3", cnt2 == 3);

        // 超出范围的页 (offset=8, limit=5)
        auto p3 = mv.page(8, 5);
        int cnt3 = 0;
        p3.for_each([&](Position&, Velocity&) { ++cnt3; });
        print_item("page(8,5) 计数 == 2 (尾部截断)", cnt3 == 2);

        // 完全超出
        auto p4 = mv.page(20, 5);
        int cnt4 = 0;
        p4.for_each([&](Position&, Velocity&) { ++cnt4; });
        print_item("page(20,5) 计数 == 0 (完全超出)", cnt4 == 0);
    }

    // === 5. exactly_one / find_one ===
    print_section(5, "exactly_one / find_one");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        mgr.add(e1, Position{42, 0, 0});
        mgr.add(e1, Velocity{100, 0, 0});

        // exactly_one (唯一匹配)
        auto [p, v] = mgr.view<Position, Velocity>().exactly_one();
        print_item("exactly_one px == 42", p.x == 42);
        print_item("exactly_one vx == 100", v.vx == 100);

        // find_one 匹配
        auto [fp, fv] = mgr.view<Position, Velocity>().find_one(e1);
        print_item("find_one(e1) 非空", fp != nullptr && fv != nullptr);
        print_item("find_one(e1) px == 42", fp && fp->x == 42);

        // find_one 不匹配
        auto e2 = mgr.create_entity();
        mgr.add(e2, Position{99, 0, 0});   // e2 无 Velocity
        auto [fp2, fv2] = mgr.view<Position, Velocity>().find_one(e2);
        print_item("find_one(e2) 不匹配返回 nullptr", fp2 == nullptr && fv2 == nullptr);
    }

    // === 6. track_changes ===
    print_section(6, "track_changes");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});

        auto cv = mgr.view<Position, Velocity>().track_changes();
        print_item("track_changes 初始 size == 2", cv.size() == 2);

        // 首次遍历消费所有变更
        int first_cnt = 0;
        cv.for_each([&](Position&, Velocity&) { ++first_cnt; });
        print_item("track_changes 首次 for_each == 2", first_cnt == 2);

        // 无变更时为空
        int second_cnt = 0;
        cv.for_each([&](Position&, Velocity&) { ++second_cnt; });
        print_item("track_changes 无变更 == 0", second_cnt == 0);

        // 修改后再次有变更 (track_changes 脏标记语义: 有变更返回全部匹配)
        mgr.add(e1, Position{11, 0, 0});
        int third_cnt = 0;
        cv.for_each([&](Position&, Velocity&) { ++third_cnt; });
        print_item("track_changes 修改后 == 2 (全量)", third_cnt == 2);
    }

    // === 7. filter_changed / filter_added ===
    print_section(7, "filter_changed / filter_added");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();

        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});
        mgr.add(e3, Velocity{30, 0, 0});

        // filter_changed<Position>
        auto fcv = mgr.view<Position, Velocity>().filter_changed<Position>();
        int cnt1 = 0;
        fcv.for_each([&](Position&, Velocity&) { ++cnt1; });
        print_item("filter_changed 首次全量 == 3", cnt1 == 3);

        int cnt2 = 0;
        fcv.for_each([&](Position&, Velocity&) { ++cnt2; });
        print_item("filter_changed 无变更 == 0", cnt2 == 0);

        mgr.add(e2, Position{22, 0, 0});
        int cnt3 = 0;
        fcv.for_each([&](Position&, Velocity&) { ++cnt3; });
        print_item("filter_changed 仅变更实体 == 1", cnt3 == 1);

        // filter_added<Position>
        auto fav = mgr.view<Position, Velocity>().filter_added<Position>();
        int cnt4 = 0;
        fav.for_each([&](Position&, Velocity&) { ++cnt4; });
        print_item("filter_added 无新添加 == 0", cnt4 == 0);

        auto e4 = mgr.create_entity();
        mgr.add(e4, Position{4, 0, 0});
        mgr.add(e4, Velocity{40, 0, 0});
        int cnt5 = 0;
        fav.for_each([&](Position&, Velocity&) { ++cnt5; });
        print_item("filter_added 新实体触发 == 1", cnt5 == 1);
    }

    // === 8. sorted_by_component ===
    print_section(8, "sorted_by_component");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        auto c = mgr.create_entity();
        mgr.add(a, int{30});
        mgr.add(b, int{10});
        mgr.add(c, int{20});
        mgr.add(a, Position{3, 0, 0});
        mgr.add(b, Position{1, 0, 0});
        mgr.add(c, Position{2, 0, 0});

        auto mv = mgr.view<int, Position>();
        auto sv = mv.sorted_by_component<int>(std::less<int>{});

        dense<int> xs;
        sv.for_each([&](int& v, Position&) { xs.emplace_back(v); });
        print_item("sorted 升序 [0]==10", xs.size() >= 1 && xs[0] == 10);
        print_item("sorted 升序 [1]==20", xs.size() >= 2 && xs[1] == 20);
        print_item("sorted 升序 [2]==30", xs.size() >= 3 && xs[2] == 30);
        print_item("sorted 总数 == 3", xs.size() == 3);
    }

    // === 9. view_any_of ===
    print_section(9, "view_any_of (N元 OR)");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto o1 = mgr.create_entity();
        auto o2 = mgr.create_entity();
        auto o3 = mgr.create_entity();
        auto o4 = mgr.create_entity();
        (void)o4;

        mgr.add(o1, Position{1, 0, 0});                 // 仅 Position
        mgr.add(o2, Velocity{20, 0, 0});                // 仅 Velocity
        mgr.add(o3, Position{3, 0, 0});
        mgr.add(o3, Velocity{30, 0, 0});                // 两者都有
        // o4 无任何组件

        // 2 元 OR
        auto av2 = mgr.view_any_of<Position, Velocity>();
        size_t cnt2 = 0;
        av2.for_each([&](Position* p, Velocity* v) {
            ++cnt2;
            if (p) lcf_sink(p->x);
            if (v) lcf_sink(v->vx);
        });
        print_item("view_any_of<Pos,Vel> 总数 == 3", cnt2 == 3);

        // 验证指针可为 null (o1 无 Velocity, o2 无 Position)
        bool has_null = false;
        av2.for_each([&](Position* p, Velocity* v) {
            if (p == nullptr || v == nullptr) has_null = true;
        });
        print_item("view_any_of 存在 nullptr 指针", has_null);

        // 3 元 OR
        auto av3 = mgr.view_any_of<Position, Velocity, Health>();
        size_t cnt3 = 0;
        av3.for_each([&](Position*, Velocity*, Health*) { ++cnt3; });
        print_item("view_any_of<Pos,Vel,Hp> 总数 == 3", cnt3 == 3);
    }

    // === 10. 边界情况 ===
    print_section(10, "边界情况");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);

        // 空 manager 上的 view
        auto mv = mgr.view<Position, Velocity>();
        print_item("空 manager view size == 0", mv.size() == 0);
        print_item("空 manager view pool_size == 0", mv.pool_size() == 0);
        print_item("空 manager view empty", mv.empty());

        auto e = mgr.create_entity();
        mgr.add(e, Position{1, 0, 0});   // 仅 1 个组件
        auto mv2 = mgr.view<Position, Velocity>();
        print_item("单组件实体 2-comp view empty", mv2.empty());
        print_item("单组件实体 2-comp size == 0", mv2.size() == 0);
        print_item("单组件实体 2-comp pool_size == 1", mv2.pool_size() == 1);
        int mv2_cnt = 0;
        mv2.for_each([&](Position&, Velocity&) { ++mv2_cnt; });
        print_item("单组件实体 2-comp for_each == 0", mv2_cnt == 0);

        // page 在空 view 上
        auto pv = mv.page(0, 10);
        int cnt = 0;
        pv.for_each([&](Position&, Velocity&) { ++cnt; });
        print_item("空 view page 计数 == 0", cnt == 0);

        // track_changes 在空 view 上
        auto cv = mv.track_changes();
        print_item("空 view track_changes size == 0", cv.size() == 0);
    }

    print_summary("功能测试");
    return 0;
}
