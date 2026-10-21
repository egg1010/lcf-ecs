// test_highlevel_query_consistency.cpp - 高层封装测试: 查询路径一致性不变量
// INV-1 遍历产出句柄必须过点查 / INV-2 跨路径实体集逐句柄相等 / INV-3 点查对失效与墓碑句柄恒 false
#include "test_common.hpp"
#include "include/ecs/component.hpp"
#include "include/ecs/entity/entity.hpp"
#include <algorithm>
#include <array>
#include <vector>

namespace
{
struct pos
{
    float x;
    float y;
    bool operator<(const pos& o) const { return x < o.x; }
};
struct vel
{
    float vx;
    float vy;
};

template <int I>
struct tag
{
    int v{I};
};
using pa = tag<320>;
using pb = tag<321>;

template <int... Is>
void touch_tags_impl(std::integer_sequence<int, Is...>)
{
    ((void)type_id::get_type_id<tag<Is>>(), ...);
}

void touch_tags()
{
    touch_tags_impl(std::make_integer_sequence<int, 320>{});
}

std::vector<ecs::entity> sorted_handles(std::vector<ecs::entity> v)
{
    std::sort(v.begin(), v.end(), [](ecs::entity a, ecs::entity b) {
        return a.handle_ < b.handle_;
    });
    return v;
}

bool same_set(const std::vector<ecs::entity>& a, const std::vector<ecs::entity>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (!(a[i] == b[i])) return false;
    }
    return true;
}

bool has_idx_local(const std::vector<ecs::entity>& v, uint32_t idx)
{
    for (ecs::entity e : v)
    {
        if (e.parts_.index_ == idx) return true;
    }
    return false;
}
} // namespace

int main()
{
    touch_tags();

    t_begin("test_highlevel_query_consistency — 高层封装测试: 查询路径一致性不变量");

    t_section(1, "INV-1/INV-2 无墓碑形态");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(4);
        std::array<ecs::entity, 4> es{mgr.create_entity(), mgr.create_entity(),
                                      mgr.create_entity(), mgr.create_entity()};
        for (size_t i = 0; i < es.size(); ++i)
        {
            mgr.add(es[i], pos{float(i), 0.0f});
            mgr.add(es[i], vel{float(i), 1.0f});
        }

        const int pos_id = type_id::get_type_id<pos>();
        std::vector<ecs::entity> via_iter;
        for (auto e : mgr.view<pos>()) via_iter.push_back(e);
        std::vector<ecs::entity> via_fe;
        mgr.view<pos>().for_each([&](ecs::entity e, pos&) { via_fe.push_back(e); });
        std::vector<ecs::entity> via_multi;
        mgr.view<pos, vel>().for_each([&](ecs::entity e, pos&, vel&) { via_multi.push_back(e); });
        auto grp = mgr.group<pos, vel>();
        std::vector<ecs::entity> via_group;
        grp.for_each([&](ecs::entity e, pos&, vel&) { via_group.push_back(e); });
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, type_id::get_type_id<vel>()});
        std::vector<ecs::entity> via_rt;
        rv.for_each([&](ecs::entity e) { via_rt.push_back(e); });

        t_check("INV-2 无墓碑 跨路径集合相等",
            same_set(sorted_handles(via_iter), sorted_handles(via_fe))
                && same_set(sorted_handles(via_iter), sorted_handles(via_multi))
                && same_set(sorted_handles(via_iter), sorted_handles(via_group))
                && same_set(sorted_handles(via_iter), sorted_handles(via_rt)));

        bool inv1 = true;
        for (ecs::entity e : via_iter)
        {
            if (mgr.get_ptr_fast<pos>(e) == nullptr) inv1 = false;
        }
        t_check("INV-1 无墓碑 遍历句柄过点查", inv1);
    }

    t_section(2, "INV-1/INV-2 有墓碑形态");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(4);
        std::array<ecs::entity, 4> es{mgr.create_entity(), mgr.create_entity(),
                                      mgr.create_entity(), mgr.create_entity()};
        for (size_t i = 0; i < es.size(); ++i)
        {
            mgr.add(es[i], pos{float(i), 0.0f});
            mgr.add(es[i], vel{float(i), 1.0f});
        }
        mgr.soft_remove<pos>(es[1]);
        mgr.soft_remove<vel>(es[2]);

        std::vector<ecs::entity> via_iter;
        for (auto e : mgr.view<pos>()) via_iter.push_back(e);
        std::vector<ecs::entity> via_fe;
        mgr.view<pos>().for_each([&](ecs::entity e, pos&) { via_fe.push_back(e); });
        std::vector<ecs::entity> via_wo;
        mgr.view<pos>(ecs::without<vel>).for_each([&](ecs::entity e, pos&) { via_wo.push_back(e); });
        std::vector<ecs::entity> via_or;
        mgr.view_or<pos, vel>().for_each([&](ecs::entity e, pos*, vel*) { via_or.push_back(e); });

        bool inv1 = true;
        for (ecs::entity e : via_iter)
        {
            if (mgr.get_ptr_fast<pos>(e) == nullptr) inv1 = false;
        }
        for (ecs::entity e : via_or)
        {
            if (!mgr.get_ptr_fast<pos>(e) && !mgr.get_ptr_fast<vel>(e)) inv1 = false;
        }
        t_check("INV-1 有墓碑 遍历句柄过点查", inv1);
        t_check("iterator 与 for_each 一致",
            same_set(sorted_handles(via_iter), sorted_handles(via_fe)));
        t_check("first/at_index 与 for_each 一致",
            mgr.view<pos>().get_first_entity() == via_fe.front()
                && mgr.view<pos>().get_entity_at_index(0) == via_fe.front());

        const int pos_id = type_id::get_type_id<pos>();
        std::vector<ecs::entity> via_multi2;
        mgr.view<pos, vel>().for_each([&](ecs::entity e, pos&, vel&) { via_multi2.push_back(e); });
        auto grp = mgr.group<pos, vel>();
        std::vector<ecs::entity> via_group;
        grp.for_each([&](ecs::entity e, pos&, vel&) { via_group.push_back(e); });
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, type_id::get_type_id<vel>()});
        std::vector<ecs::entity> via_rt;
        rv.for_each([&](ecs::entity e) { via_rt.push_back(e); });
        t_check("INV-2 有墓碑 双组件跨路径集合相等",
            same_set(sorted_handles(via_multi2), sorted_handles(via_group))
                && same_set(sorted_handles(via_multi2), sorted_handles(via_rt)));
    }

    t_section(3, "INV-2 稀疏路径形态 (type id > 320)");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(3);
        std::array<ecs::entity, 3> es{mgr.create_entity(), mgr.create_entity(),
                                      mgr.create_entity()};
        mgr.add(es[0], pa{1});
        mgr.add(es[1], pa{2});
        mgr.add(es[1], pb{3});
        mgr.add(es[2], pa{4});
        mgr.add(es[2], pb{5});
        mgr.soft_remove<pa>(es[0]);

        const int pa_id = type_id::get_type_id<pa>();
        const int pb_id = type_id::get_type_id<pb>();
        std::vector<ecs::entity> via_multi;
        mgr.view<pa, pb>().for_each([&](ecs::entity e, pa&, pb&) { via_multi.push_back(e); });
        auto grp = mgr.group<pa, pb>();
        std::vector<ecs::entity> via_group;
        grp.for_each([&](ecs::entity e, pa&, pb&) { via_group.push_back(e); });
        auto og = mgr.group<pa, pb>(ecs::owned<pa>);
        std::vector<ecs::entity> via_owned;
        og.for_each([&](ecs::entity e, pa&, pb&) { via_owned.push_back(e); });
        auto rg = mgr.group<pa, pb>(ecs::reorder<pa>);
        std::vector<ecs::entity> via_reorder;
        rg.for_each([&](ecs::entity e, pa&, pb&) { via_reorder.push_back(e); });
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pa_id, pb_id});
        std::vector<ecs::entity> via_rt;
        rv.for_each([&](ecs::entity e) { via_rt.push_back(e); });

        t_check("INV-2 稀疏路径 跨路径集合相等",
            same_set(sorted_handles(via_multi), sorted_handles(via_group))
                && same_set(sorted_handles(via_multi), sorted_handles(via_owned))
                && same_set(sorted_handles(via_multi), sorted_handles(via_reorder))
                && same_set(sorted_handles(via_multi), sorted_handles(via_rt)));
        t_check("稀疏路径 主集墓碑被排除",
            via_multi.size() == 2 && !has_idx_local(via_multi, es[0].parts_.index_));
    }

    t_section(4, "INV-3 点查对失效/墓碑句柄恒 false");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(3);
        ecs::entity a = mgr.create_entity();
        mgr.add(a, pos{1.0f, 0.0f});
        mgr.add(a, vel{1.0f, 0.0f});
        const int pos_id = type_id::get_type_id<pos>();

        auto sv = mgr.view<pos>();
        auto mv = mgr.view<pos, vel>();
        auto wo = mgr.view<pos>(ecs::without<vel>);
        auto wt = mgr.view<pos>(ecs::with<vel>);
        auto ov = mgr.view_or<pos, vel>();
        auto grp = mgr.group<pos, vel>();
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, type_id::get_type_id<vel>()});

        ecs::entity stale = a;
        mgr.delete_entity(a);
        ecs::entity recycled = mgr.create_entity();
        mgr.add(recycled, pos{9.0f, 0.0f});
        mgr.add(recycled, vel{9.0f, 0.0f});

        bool inv3 = !sv.contains(stale) && !mv.contains(stale) && !wo.contains(stale)
            && !wt.contains(stale) && !ov.contains(stale) && !grp.contains(stale)
            && !rv.contains(stale);
        t_check("INV-3 失效句柄 全路径 false", inv3);

        mgr.soft_remove<pos>(recycled);
        mgr.soft_remove<vel>(recycled);
        ecs::entity tomb_handle = recycled;
        inv3 = !sv.contains(tomb_handle) && !mv.contains(tomb_handle)
            && !wo.contains(tomb_handle) && !wt.contains(tomb_handle)
            && !ov.contains(tomb_handle) && !grp.contains(tomb_handle)
            && !rv.contains(tomb_handle);
        t_check("INV-3 墓碑句柄 全路径 false", inv3);

        ecs::entity null_e{};
        inv3 = !sv.contains(null_e) && !mv.contains(null_e) && !rv.contains(null_e)
            && !grp.contains(null_e);
        t_check("INV-3 空句柄 全路径 false", inv3);
    }

    t_section(5, "派生视图与基础遍历一致");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(4);
        std::array<ecs::entity, 4> es{mgr.create_entity(), mgr.create_entity(),
                                      mgr.create_entity(), mgr.create_entity()};
        for (size_t i = 0; i < es.size(); ++i)
        {
            mgr.add(es[i], pos{float(i), 0.0f});
        }
        mgr.soft_remove<pos>(es[0]);

        std::vector<ecs::entity> base;
        mgr.view<pos>().for_each([&](ecs::entity e, pos&) { base.push_back(e); });
        auto base_set = sorted_handles(base);

        std::vector<ecs::entity> via_iter;
        for (auto e : mgr.view<pos>()) via_iter.push_back(e);

        std::vector<ecs::entity> via_sorted;
        auto sorted = mgr.view<pos>().sorted_by_component(std::less<pos>{});
        sorted.for_each([&](ecs::entity e, pos&) { via_sorted.push_back(e); });

        std::vector<ecs::entity> via_changed;
        auto changed = mgr.view<pos>().track_changes();
        changed.for_each([&](ecs::entity e, pos&) { via_changed.push_back(e); });

        std::vector<ecs::entity> via_fchanged;
        auto fchanged = mgr.view<pos>().filter_changed();
        fchanged.for_each([&](ecs::entity e, pos&) { via_fchanged.push_back(e); });

        std::vector<ecs::entity> via_fadded;
        auto fadded = mgr.view<pos>().filter_added();
        ecs::entity en = mgr.create_entity();
        mgr.add(en, pos{50.0f, 0.0f});
        mgr.soft_remove<pos>(en);
        fadded.for_each([&](ecs::entity e, pos&) { via_fadded.push_back(e); });

        std::vector<ecs::entity> via_fv;
        auto fv = mgr.view_filtered<pos>([](pos& p) { return p.x > -1000.0f; });
        fv.for_each([&](ecs::entity e, pos&) { via_fv.push_back(e); });
        auto fvd = fv.and_<pos>();
        std::vector<ecs::entity> via_fvd;
        fvd.for_each([&](ecs::entity e, pos&, pos&) { via_fvd.push_back(e); });
        auto fvo = fv.or_<pos>();
        std::vector<ecs::entity> via_fvo;
        fvo.for_each([&](ecs::entity e, pos*, pos*) { via_fvo.push_back(e); });

        t_check("sorted 与基础遍历一致",
            same_set(base_set, sorted_handles(via_iter))
                && same_set(base_set, sorted_handles(via_sorted)));
        t_check("changed/filter_changed 与基础遍历一致",
            same_set(base_set, sorted_handles(via_changed))
                && same_set(base_set, sorted_handles(via_fchanged)));
        t_check("filter_added 排除软删实体",
            via_fadded.empty() || !has_idx_local(via_fadded, en.parts_.index_));
        t_check("filter_view/and/or 与基础遍历一致",
            same_set(base_set, sorted_handles(via_fv))
                && same_set(base_set, sorted_handles(via_fvd))
                && same_set(base_set, sorted_handles(via_fvo)));
    }

    t_section(6, "swap/reorder 后不变量保持");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(3);
        std::array<ecs::entity, 3> es{mgr.create_entity(), mgr.create_entity(),
                                      mgr.create_entity()};
        for (size_t i = 0; i < es.size(); ++i)
        {
            mgr.add(es[i], pos{float(i), 0.0f});
            mgr.add(es[i], vel{float(i), 1.0f});
        }
        mgr.soft_remove<pos>(es[1]);

        mgr.get_single_class_set<pos>()->swap_dense_and_pool(0, 2);

        const int pos_id = type_id::get_type_id<pos>();
        auto grp = mgr.group<pos, vel>();
        std::vector<ecs::entity> via_group;
        grp.for_each([&](ecs::entity e, pos&, vel&) { via_group.push_back(e); });
        std::vector<ecs::entity> via_rt;
        auto rv = mgr.runtime_view_create(std::array<int, 2>{pos_id, type_id::get_type_id<vel>()});
        rv.for_each([&](ecs::entity e) { via_rt.push_back(e); });
        std::vector<ecs::entity> via_multi;
        mgr.view<pos, vel>().for_each([&](ecs::entity e, pos&, vel&) { via_multi.push_back(e); });

        bool pairing = true;
        rv.for_each_typed<pos, vel>([&](ecs::entity e, pos& p, vel&) {
            if (p.x != mgr.get_ptr_fast<pos>(e)->x) pairing = false;
        });
        t_check("swap 后 跨路径一致且配对正确",
            same_set(sorted_handles(via_group), sorted_handles(via_rt))
                && same_set(sorted_handles(via_group), sorted_handles(via_multi)) && pairing);

        auto og = mgr.group<pos, vel>(ecs::owned<pos>);
        std::vector<ecs::entity> via_owned;
        og.for_each([&](ecs::entity e, pos&, vel&) { via_owned.push_back(e); });
        t_check("swap 后 owning_group 一致",
            same_set(sorted_handles(via_group), sorted_handles(via_owned)));
    }

    t_end("test_highlevel_query_consistency");
    return 0;
}
