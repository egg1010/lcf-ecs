// test_single_view.cpp - single_view<T> 功能测试
// 覆盖: 基本查询/for_each/迭代器/组件访问/实体访问/
//       without/with/page/sorted_by_component/sorted_by_component_value/
//       track_changes/filter_changed/filter_added/exactly_one
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

        auto sv = mgr.view<Position>();
        print_item("size == 3", sv.size() == 3);
        print_item("empty false", !sv.empty());
        print_item("contains e1", sv.contains(e1));
        print_item("contains e2", sv.contains(e2));
        print_item("contains e3", sv.contains(e3));

        // 未添加组件的实体
        auto e4 = mgr.create_entity();
        print_item("contains e4 false", !sv.contains(e4));

        // 无效实体
        print_item("contains invalid false", !sv.contains(entity{}));
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

        auto sv = mgr.view<Position>();

        // 仅组件
        int comp_cnt = 0;
        float sum_px = 0;
        sv.for_each([&](Position& p) { ++comp_cnt; sum_px += p.x; });
        print_item("for_each [comp] 计数 == 2", comp_cnt == 2);
        print_item("for_each [comp] px 之和 == 5", sum_px == 5.0f);

        // entity + 组件
        int ent_cnt = 0;
        bool found_e1 = false;
        sv.for_each([&](entity e, Position&) {
            ++ent_cnt;
            if (e == e1) found_e1 = true;
        });
        print_item("for_each [ent+comp] 计数 == 2", ent_cnt == 2);
        print_item("for_each [ent+comp] 找到 e1", found_e1);

        // for_each 修改组件
        sv.for_each([](Position& p) { p.x *= 10; });
        float modified = 0;
        sv.for_each([&](Position& p) { modified += p.x; });
        print_item("for_each 修改后 px 之和 == 50", modified == 50.0f);
    }

    // === 3. 迭代器 ===
    print_section(3, "迭代器 (begin/end / component_begin/end)");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});

        auto sv = mgr.view<Position>();

        // begin/end (entity 迭代器)
        int iter_cnt = 0;
        for (auto it = sv.begin(); it != sv.end(); ++it) ++iter_cnt;
        print_item("begin/end 计数 == 3", iter_cnt == 3);

        // 解引用获取 entity
        auto it = sv.begin();
        entity first_e = *it;
        print_item("begin() 解引用有效", first_e.is_valid());

        // component_begin/component_end
        int comp_cnt = 0;
        for (auto it2 = sv.component_begin(); it2 != sv.component_end(); ++it2) ++comp_cnt;
        print_item("component_begin/end 计数 == 3", comp_cnt == 3);

        // component 迭代器解引用
        auto cit = sv.component_begin();
        print_item("component_begin() x == 1", cit->x == 1.0f);
    }

    // === 4. 组件访问 ===
    print_section(4, "组件访问 (get_component_for_entity / get_component_at_index)");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        mgr.add(e1, Position{10, 0, 0});
        mgr.add(e2, Position{20, 0, 0});
        mgr.add(e3, Position{30, 0, 0});

        auto sv = mgr.view<Position>();

        // get_component_for_entity
        Position* p1 = sv.get_component_for_entity(e1);
        print_item("get_component_for_entity(e1) 非空", p1 != nullptr);
        print_item("get_component_for_entity(e1) x == 10", p1 && p1->x == 10.0f);

        Position* p2 = sv.get_component_for_entity(e2);
        print_item("get_component_for_entity(e2) x == 20", p2 && p2->x == 20.0f);

        // 未添加组件的实体返回 nullptr
        auto e4 = mgr.create_entity();
        Position* p4 = sv.get_component_for_entity(e4);
        print_item("get_component_for_entity(e4) nullptr", p4 == nullptr);

        // get_component_at_index
        Position* pi0 = sv.get_component_at_index(0);
        Position* pi1 = sv.get_component_at_index(1);
        Position* pi2 = sv.get_component_at_index(2);
        print_item("get_component_at_index(0) x == 10", pi0 && pi0->x == 10.0f);
        print_item("get_component_at_index(1) x == 20", pi1 && pi1->x == 20.0f);
        print_item("get_component_at_index(2) x == 30", pi2 && pi2->x == 30.0f);

        // 越界索引返回 nullptr
        Position* pi_oob = sv.get_component_at_index(100);
        print_item("get_component_at_index(越界) nullptr", pi_oob == nullptr);
    }

    // === 5. 实体访问 ===
    print_section(5, "实体访问 (get_first/last/at_index)");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});

        auto sv = mgr.view<Position>();

        // get_first_entity
        entity first = sv.get_first_entity();
        print_item("get_first_entity 有效", first.is_valid());
        print_item("get_first_entity == e1", first == e1);

        // get_last_entity
        entity last = sv.get_last_entity();
        print_item("get_last_entity 有效", last.is_valid());
        print_item("get_last_entity == e3", last == e3);

        // get_entity_at_index
        entity at0 = sv.get_entity_at_index(0);
        entity at1 = sv.get_entity_at_index(1);
        entity at2 = sv.get_entity_at_index(2);
        print_item("get_entity_at_index(0) == e1", at0 == e1);
        print_item("get_entity_at_index(1) == e2", at1 == e2);
        print_item("get_entity_at_index(2) == e3", at2 == e3);

        // 越界索引返回无效实体
        entity at_oob = sv.get_entity_at_index(100);
        print_item("get_entity_at_index(越界) 无效", !at_oob.is_valid());
    }

    // === 6. without<T> 排除 ===
    print_section(6, "without<T> 排除");
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
        print_item("without<Vel> 含 e3", ev.contains(e3));

        int cnt = 0;
        ev.for_each([&](Position&) { ++cnt; });
        print_item("without<Vel> for_each 计数 == 2", cnt == 2);

        // entity + comp
        int ent_cnt = 0;
        ev.for_each([&](entity, Position&) { ++ent_cnt; });
        print_item("without<Vel> for_each [ent+comp] == 2", ent_cnt == 2);

        // 多类型排除
        auto ev2 = mgr.view<Position>(ecs::without<Velocity, Health>);
        int cnt2 = 0;
        ev2.for_each([&](Position&) { ++cnt2; });
        print_item("without<Vel,Hp> for_each == 2", cnt2 == 2);
    }

    // === 7. with<T> 获取 ===
    print_section(7, "with<T> 获取");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();

        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e1, Health{80, 100});
        mgr.add(e2, Health{90, 100});
        // e3 无 Health

        auto gv = mgr.view<Position>(ecs::with<Health>);
        print_item("with<Hp> 含 e1", gv.contains(e1));
        print_item("with<Hp> 含 e2", gv.contains(e2));
        print_item("with<Hp> 含 e3", gv.contains(e3));

        // with 回调: Position& + Health*
        int cnt = 0;
        int hp_sum = 0;
        gv.for_each([&](Position&, Health* h) {
            ++cnt;
            if (h) hp_sum += h->current;
        });
        print_item("with<Hp> for_each 计数 == 3", cnt == 3);
        print_item("with<Hp> hp 之和 == 170", hp_sum == 170);

        // entity + comp + optional
        int ent_cnt = 0;
        gv.for_each([&](entity, Position&, Health*) { ++ent_cnt; });
        print_item("with<Hp> for_each [ent+comp] == 3", ent_cnt == 3);

        // 多类型 with
        mgr.add(e1, Velocity{10, 0, 0});
        auto gv2 = mgr.view<Position>(ecs::with<Health, Velocity>);
        int cnt2 = 0;
        gv2.for_each([&](Position&, Health*, Velocity*) { ++cnt2; });
        print_item("with<Hp,Vel> for_each == 3", cnt2 == 3);
    }

    // === 8. page 分页 ===
    print_section(8, "page 分页");
    {
        manager mgr;
        mgr.append_preallocated_entities(20);
        dense<entity> ents;
        for (int i = 0; i < 10; ++i)
        {
            auto e = mgr.create_entity();
            ents.emplace_back(e);
            mgr.add(e, Position{static_cast<float>(i), 0, 0});
        }

        auto sv = mgr.view<Position>();
        print_item("全量 size == 10", sv.size() == 10);

        // 第一页 (offset=0, limit=3)
        auto p1 = sv.page(0, 3);
        int cnt1 = 0;
        p1.for_each([&](Position&) { ++cnt1; });
        print_item("page(0,3) 计数 == 3", cnt1 == 3);
        print_item("page(0,3) size == 3", p1.size() == 3);
        print_item("page(0,3) !empty", !p1.empty());

        // 中间页 (offset=4, limit=3)
        auto p2 = sv.page(4, 3);
        int cnt2 = 0;
        p2.for_each([&](Position&) { ++cnt2; });
        print_item("page(4,3) 计数 == 3", cnt2 == 3);

        // 超出范围的页 (offset=8, limit=5)
        auto p3 = sv.page(8, 5);
        int cnt3 = 0;
        p3.for_each([&](Position&) { ++cnt3; });
        print_item("page(8,5) 计数 == 2 (尾部截断)", cnt3 == 2);

        // 完全超出
        auto p4 = sv.page(20, 5);
        int cnt4 = 0;
        p4.for_each([&](Position&) { ++cnt4; });
        print_item("page(20,5) 计数 == 0 (完全超出)", cnt4 == 0);
        print_item("page(20,5) empty", p4.empty());

        // paged_view size
        auto p5 = sv.page(0, 2);
        print_item("page(0,2) size == 2", p5.size() == 2);
    }

    // === 9. sorted_by_component ===
    print_section(9, "sorted_by_component");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        auto c = mgr.create_entity();
        auto d = mgr.create_entity();
        auto e = mgr.create_entity();
        mgr.add(a, Position{30, 0, 0});
        mgr.add(b, Position{10, 0, 0});
        mgr.add(c, Position{20, 0, 0});
        mgr.add(d, Position{50, 0, 0});
        mgr.add(e, Position{40, 0, 0});

        auto sv = mgr.view<Position>();
        auto sorted = sv.sorted_by_component(
            [](const Position& a, const Position& b) { return a.x < b.x; });

        dense<float> xs;
        sorted.for_each([&](Position& p) { xs.emplace_back(p.x); });

        bool sorted_ok = xs.size() == 5
            && xs[0] == 10 && xs[1] == 20 && xs[2] == 30 && xs[3] == 40 && xs[4] == 50;
        print_item("sorted_by_component 升序", sorted_ok);
        print_item("sorted_by_component size == 5", sorted.size() == 5);
        print_item("sorted_by_component !empty", !sorted.empty());

        // entity + comp 回调
        dense<entity> sorted_ents;
        sorted.for_each([&](entity e, Position&) { sorted_ents.emplace_back(e); });
        print_item("sorted [ent+comp] 计数 == 5", sorted_ents.size() == 5);

        // 降序
        auto sorted_desc = sv.sorted_by_component(
            [](const Position& a, const Position& b) { return a.x > b.x; });
        dense<float> xs_desc;
        sorted_desc.for_each([&](Position& p) { xs_desc.emplace_back(p.x); });
        bool desc_ok = xs_desc.size() == 5
            && xs_desc[0] == 50 && xs_desc[1] == 40 && xs_desc[2] == 30;
        print_item("sorted_by_component 降序", desc_ok);
    }

    // === 10. sorted_by_component_value ===
    print_section(10, "sorted_by_component_value (分组视图)");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        auto c = mgr.create_entity();
        auto d = mgr.create_entity();
        auto e = mgr.create_entity();
        mgr.add(a, Position{10, 0, 0});
        mgr.add(b, Position{30, 0, 0});
        mgr.add(c, Position{20, 0, 0});
        mgr.add(d, Position{50, 0, 0});
        mgr.add(e, Position{40, 0, 0});

        auto sv = mgr.view<Position>();
        auto gv = sv.sorted_by_component_value(
            [](Position& p) -> int { return static_cast<int>(p.x) / 20; });

        print_item("sorted_by_component_value size == 5", gv.size() == 5);
        // 10/20=0, 30/20=1, 20/20=1, 50/20=2, 40/20=2 → 3 组
        print_item("sorted_by_component_value group_count == 3", gv.group_count() == 3);

        // for_each_group
        size_t groups = 0;
        dense<int> keys;
        gv.for_each_group([&](int key, size_t start, size_t end) {
            ++groups;
            keys.emplace_back(key);
            (void)start; (void)end;
        });
        print_item("for_each_group 组数 == 3", groups == 3);
        print_item("for_each_group keys[0]==0", keys.size() >= 1 && keys[0] == 0);
        print_item("for_each_group keys[1]==1", keys.size() >= 2 && keys[1] == 1);
        print_item("for_each_group keys[2]==2", keys.size() >= 3 && keys[2] == 2);

        // for_each 遍历所有元素
        int cnt = 0;
        gv.for_each([&](Position&) { ++cnt; });
        print_item("for_each 计数 == 5", cnt == 5);
    }

    // === 11. track_changes ===
    print_section(11, "track_changes");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});

        auto cv = mgr.view<Position>().track_changes();
        print_item("track_changes 初始 size == 3", cv.size() == 3);

        // 首次遍历消费所有变更
        int first_cnt = 0;
        cv.for_each([&](Position&) { ++first_cnt; });
        print_item("track_changes 首次 for_each == 3", first_cnt == 3);

        // 无变更时为空
        int second_cnt = 0;
        cv.for_each([&](Position&) { ++second_cnt; });
        print_item("track_changes 无变更 == 0", second_cnt == 0);

        // 修改后再次有变更
        mgr.add(e1, Position{11, 0, 0});
        int third_cnt = 0;
        cv.for_each([&](Position&) { ++third_cnt; });
        print_item("track_changes 修改后 == 3 (全量)", third_cnt == 3);

        // reset_tracking
        cv.reset_tracking();
        int reset_cnt = 0;
        cv.for_each([&](Position&) { ++reset_cnt; });
        print_item("track_changes reset后 == 3", reset_cnt == 3);

        // entity + comp 回调
        int ent_cnt = 0;
        cv.for_each([&](entity, Position&) { ++ent_cnt; });
        print_item("track_changes [ent+comp] == 0 (已消费)", ent_cnt == 0);
    }

    // === 12. filter_changed / filter_added ===
    print_section(12, "filter_changed / filter_added");
    {
        // filter_changed
        {
            manager mgr;
            mgr.append_preallocated_entities(10);
            auto e1 = mgr.create_entity();
            auto e2 = mgr.create_entity();
            auto e3 = mgr.create_entity();

            mgr.add(e1, Position{1, 0, 0});
            mgr.add(e2, Position{2, 0, 0});
            mgr.add(e3, Position{3, 0, 0});

            auto fcv = mgr.view<Position>().filter_changed();

            // 首次查询：所有实体都是"变更的"
            size_t cnt1 = 0;
            fcv.for_each([&](Position&) { ++cnt1; });
            print_item("filter_changed 首次全量 == 3", cnt1 == 3);

            // 无变更时返回空
            size_t cnt2 = 0;
            fcv.for_each([&](Position&) { ++cnt2; });
            print_item("filter_changed 无变更 == 0", cnt2 == 0);

            // 修改一个实体后仅返回该实体
            mgr.add(e1, Position{10, 0, 0});
            size_t cnt3 = 0;
            fcv.for_each([&](Position&) { ++cnt3; });
            print_item("filter_changed 修改1个 == 1", cnt3 == 1);

            // reset 后重新全量
            fcv.reset_tracking();
            size_t cnt4 = 0;
            fcv.for_each([&](Position&) { ++cnt4; });
            print_item("filter_changed reset后 == 3", cnt4 == 3);

            // entity + comp
            size_t ent_cnt = 0;
            fcv.for_each([&](entity, Position&) { ++ent_cnt; });
            print_item("filter_changed [ent+comp] == 0 (已消费)", ent_cnt == 0);
        }

        // filter_added
        {
            manager mgr;
            mgr.append_preallocated_entities(10);
            auto a1 = mgr.create_entity();
            auto a2 = mgr.create_entity();
            auto a3 = mgr.create_entity();

            auto fav = mgr.view<Position>().filter_added();

            // 添加组件后首次查询应返回所有
            mgr.add(a1, Position{1, 0, 0});
            mgr.add(a2, Position{2, 0, 0});
            mgr.add(a3, Position{3, 0, 0});

            size_t cnt1 = 0;
            fav.for_each([&](Position&) { ++cnt1; });
            print_item("filter_added 首次添加 == 3", cnt1 == 3);

            // 无新添加时返回空
            size_t cnt2 = 0;
            fav.for_each([&](Position&) { ++cnt2; });
            print_item("filter_added 无新添加 == 0", cnt2 == 0);

            // 更新已有组件不触发 added
            mgr.add(a1, Position{10, 0, 0});
            size_t cnt3 = 0;
            fav.for_each([&](Position&) { ++cnt3; });
            print_item("filter_added 更新不触发 == 0", cnt3 == 0);

            // 新实体添加组件触发 added
            auto a4 = mgr.create_entity();
            mgr.add(a4, Position{4, 0, 0});
            size_t cnt4 = 0;
            fav.for_each([&](Position&) { ++cnt4; });
            print_item("filter_added 新实体触发 == 1", cnt4 == 1);

            // reset 后重新全量
            fav.reset_tracking();
            size_t cnt5 = 0;
            fav.for_each([&](Position&) { ++cnt5; });
            print_item("filter_added reset后 == 4", cnt5 == 4);
        }
    }

    // === 13. exactly_one ===
    print_section(13, "exactly_one");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        mgr.add(e1, Position{42, 0, 0});

        auto sv = mgr.view<Position>();
        Position& p = sv.exactly_one();
        print_item("exactly_one x == 42", p.x == 42.0f);

        // const 版本
        const auto& csv = sv;
        const Position& cp = csv.exactly_one();
        print_item("exactly_one (const) x == 42", cp.x == 42.0f);
    }

    // === 14. 边界情况 ===
    print_section(14, "边界情况");
    {
        manager mgr;
        mgr.append_preallocated_entities(10);

        // 空 manager 上的 view
        auto sv = mgr.view<Position>();
        print_item("空 manager view size == 0", sv.size() == 0);
        print_item("空 manager view empty", sv.empty());

        // 空 view 上的接口
        auto e = mgr.create_entity();
        print_item("空 view contains == false", !sv.contains(e));
        print_item("空 view get_component_for_entity nullptr",
            sv.get_component_for_entity(e) == nullptr);
        print_item("空 view get_component_at_index nullptr",
            sv.get_component_at_index(0) == nullptr);
        print_item("空 view get_first_entity 无效",
            !sv.get_first_entity().is_valid());
        print_item("空 view get_last_entity 无效",
            !sv.get_last_entity().is_valid());
        print_item("空 view get_entity_at_index 无效",
            !sv.get_entity_at_index(0).is_valid());

        // 空 view 迭代器
        int iter_cnt = 0;
        for (auto it = sv.begin(); it != sv.end(); ++it) ++iter_cnt;
        print_item("空 view begin/end == 0", iter_cnt == 0);

        int comp_cnt = 0;
        for (auto it = sv.component_begin(); it != sv.component_end(); ++it) ++comp_cnt;
        print_item("空 view component_begin/end == 0", comp_cnt == 0);

        // 空 view for_each
        int fe_cnt = 0;
        sv.for_each([&](Position&) { ++fe_cnt; });
        print_item("空 view for_each == 0", fe_cnt == 0);

        // page 在空 view 上
        auto pv = sv.page(0, 10);
        int cnt = 0;
        pv.for_each([&](Position&) { ++cnt; });
        print_item("空 view page 计数 == 0", cnt == 0);
        print_item("空 view page empty", pv.empty());

        // track_changes 在空 view 上
        auto cv = sv.track_changes();
        print_item("空 view track_changes size == 0", cv.size() == 0);
        int tc_cnt = 0;
        cv.for_each([&](Position&) { ++tc_cnt; });
        print_item("空 view track_changes for_each == 0", tc_cnt == 0);

        // filter_changed 在空 view 上
        auto fcv = sv.filter_changed();
        print_item("空 view filter_changed size == 0", fcv.size() == 0);

        // filter_added 在空 view 上
        auto fav = sv.filter_added();
        print_item("空 view filter_added size == 0", fav.size() == 0);

        // sorted_by_component 在空 view 上
        auto sorted = sv.sorted_by_component(
            [](const Position& a, const Position& b) { return a.x < b.x; });
        print_item("空 view sorted size == 0", sorted.size() == 0);
        print_item("空 view sorted empty", sorted.empty());

        // 添加一个组件后 view 应更新
        mgr.add(e, Position{1, 0, 0});
        auto sv2 = mgr.view<Position>();
        print_item("添加后 view size == 1", sv2.size() == 1);
        print_item("添加后 view !empty", !sv2.empty());
        print_item("添加后 contains == true", sv2.contains(e));
    }

    print_summary("功能测试");
    return 0;
}
