// test_highlevel_single_class_set.cpp - 高层封装测试: single_class_set
#include "test_common.hpp"
#include "include/ecs/storage/single_class_set.hpp"
#include "include/ecs/entity/entity.hpp"

using ecs::single_class_set;

struct pos
{
    float x;
    float y;
};

int main()
{
    t_begin("test_highlevel_single_class_set — 高层封装测试: single_class_set");

    t_section(1, "添加与访问");
    {
        single_class_set set;
        ecs::entity e1(0, 1);
        ecs::entity e2(1, 1);
        t_check("初始为空", set.empty() && set.size() == 0);

        auto r1 = set.add(e1, pos{1.0f, 2.0f});
        t_check("add e1 成功", static_cast<bool>(r1));
        auto r2 = set.add(e2, pos{3.0f, 4.0f});
        t_check("add e2 成功", static_cast<bool>(r2));
        t_check("size == 2", set.size() == 2);

        pos* p1 = set.get_ptr<pos>(e1);
        t_check("get_ptr e1 命中", p1 != nullptr && p1->x == 1.0f);
        pos* p2 = set.get_ptr_fast<pos>(e2);
        t_check("get_ptr_fast e2 命中", p2 != nullptr && p2->y == 4.0f);
    }

    t_section(2, "contains / 索引视图");
    {
        single_class_set set;
        ecs::entity e1(0, 1);
        set.add(e1, pos{5.0f, 6.0f});

        t_check("contains_entity 命中", set.contains_entity(e1));
        ecs::entity other(7, 1);
        t_check("contains_entity 未命中", !set.contains_entity(other));

        auto& idx = set.get_entity_indices();
        auto& ver = set.get_entity_versions();
        t_check("entity_indices 大小", idx.size() == 1 && idx[0] == 0);
        t_check("entity_versions 大小", ver.size() == 1 && ver[0] == 1);
    }

    t_section(3, "软删除与墓碑");
    {
        single_class_set set;
        ecs::entity e1(0, 1);
        ecs::entity e2(1, 1);
        set.add(e1, pos{1.0f, 1.0f});
        set.add(e2, pos{2.0f, 2.0f});

        auto rs = set.soft_remove(e1);
        t_check("soft_remove 成功", static_cast<bool>(rs));
        t_check("soft 后 size 保留槽位", set.size() == 2);
        t_check("soft 后 get_ptr 为 null", set.get_ptr<pos>(e1) == nullptr);
        t_check("soft 后 live_count == 1", set.live_count() == 1);
        t_check("soft 后 tombstone > 0", set.tombstone_count() >= 1);
        t_check("has_tombstones", set.has_tombstones());

        auto rh = set.hard_remove(e2);
        t_check("hard_remove 成功", static_cast<bool>(rh));
    }

    t_section(4, "capacity / pool version");
    {
        single_class_set set;
        set.increase_capacity(256);
        uint64_t v0 = set.get_pool_version();
        ecs::entity e1(0, 1);
        set.add(e1, pos{1.0f, 1.0f});
        uint64_t v1 = set.get_pool_version();
        t_check("add 后 pool_version 变化", v1 != v0);
        t_check("get_version 可读", set.get_version(0) == 1);
    }

    t_section(5, "clear");
    {
        single_class_set set;
        ecs::entity e1(0, 1);
        ecs::entity e2(1, 1);
        set.add(e1, pos{1.0f, 1.0f});
        set.add(e2, pos{2.0f, 2.0f});
        set.clear();
        t_check("clear 后为空", set.empty() && set.size() == 0);
    }

    return t_end("single_class_set ");
}
