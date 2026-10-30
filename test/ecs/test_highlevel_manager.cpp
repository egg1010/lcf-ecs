// test_highlevel_manager.cpp - 高层封装测试: ecs::manager
#include "test_common.hpp"
#include "include/ecs/manager.hpp"
#include "include/ecs/entity/entity.hpp"
#include "include/part/type/type_id.hpp"
#include <functional>
#include <memory>
#include <span>
#include <vector>




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

struct health
{
    int hp;
};

// 非可复制组件: 用于验证 clone 拒绝复制
struct move_only_component
{
    std::unique_ptr<int> value;
};

namespace
{
int g_def_add_calls = 0;
int g_def_remove_calls = 0;
int g_system_order[4]{};
int g_system_count = 0;

void created_probe(ecs::entity, void* data) noexcept
{
    ++*static_cast<int*>(data);
}

void destroyed_probe(ecs::entity, void* data) noexcept
{
    ++*static_cast<int*>(data);
}

void def_add_probe(ecs::entity, void*, void* data) noexcept
{
    ++*static_cast<int*>(data);
}

void def_remove_probe(ecs::entity, void*, void* data) noexcept
{
    ++*static_cast<int*>(data);
}

void system_probe(ecs::manager&, void* data) noexcept
{
    if (g_system_count < 4)
    {
        g_system_order[g_system_count++] = *static_cast<int*>(data);
    }
}
} // namespace

int main()
{


    t_begin("test_highlevel_manager — 高层封装测试: ecs::manager");

    t_section(1, "实体创建与有效性");
    {
        ecs::manager mgr;
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        t_check("create_entity 有效", e1.is_valid() && e2.is_valid());
        t_check("两个实体不同", e1 != e2);
        t_check("is_entity_valid 命中", mgr.is_entity_valid(e1));

        mgr.delete_entity(e1);
        t_check("delete 后失效", !mgr.is_entity_valid(e1));
        t_check("e2 仍有效", mgr.is_entity_valid(e2));
    }

    t_section(2, "组件增删查");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        operating_message add_err = mgr.add(e, position{1.0f, 2.0f});
        t_check("add 成功", static_cast<bool>(add_err));

        position* p = mgr.get_ptr<position>(e);
        t_check("get_ptr 命中", p != nullptr && p->x == 1.0f && p->y == 2.0f);

        position* pf = mgr.get_ptr_fast<position>(e);
        t_check("get_ptr_fast 命中", pf != nullptr);

        mgr.add(e, velocity{3.0f, 4.0f});
        velocity* v = mgr.get_ptr<velocity>(e);
        t_check("多组件共存", v != nullptr && v->vx == 3.0f);

        operating_message rs_err = mgr.soft_remove<velocity>(e);
        t_check("soft_remove 成功", static_cast<bool>(rs_err));
        t_check("soft 后 get_ptr null", mgr.get_ptr<velocity>(e) == nullptr);

        operating_message rh_err = mgr.hard_remove<position>(e);
        t_check("hard_remove 成功", static_cast<bool>(rh_err));
        t_check("hard 后 get_ptr null", mgr.get_ptr<position>(e) == nullptr);
    }

    t_section(3, "批量添加");
    {
        ecs::manager mgr;
        dense<ecs::entity> ents;
        dense<position> comps;
        for (int i = 0; i < 5; ++i)
        {
            auto e = mgr.create_entity();
            ents.emplace_back(e);
            comps.emplace_back(position{static_cast<float>(i), 0.0f});
        }
        operating_message batch_err = mgr.add_batch<position>(ents, comps);
        t_check("add_batch 成功", static_cast<bool>(batch_err));
        bool ok = true;
        for (int i = 0; i < 5; ++i)
        {
            position* p = mgr.get_ptr<position>(ents[i]);
            if (!p || p->x != static_cast<float>(i)) { ok = false; }
        }
        t_check("add_batch 数值正确", ok);
    }

    t_section(4, "多组件添加");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        operating_message r1 = mgr.add(e, position{9.0f, 1.0f});
        operating_message r2 = mgr.add(e, velocity{1.0f, 1.0f});
        t_check("单实体多组件",
                static_cast<bool>(r1) && static_cast<bool>(r2)
                && mgr.get_ptr<position>(e) != nullptr && mgr.get_ptr<velocity>(e) != nullptr);

        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        operating_message r3 = mgr.add(a, health{50});
        operating_message r4 = mgr.add(b, health{50});
        t_check("单组件多实体",
                static_cast<bool>(r3) && static_cast<bool>(r4)
                && mgr.get_ptr<health>(a) != nullptr && mgr.get_ptr<health>(b) != nullptr);
    }

    t_section(5, "实体销毁清空组件");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, position{1.0f, 1.0f});
        mgr.add(e, velocity{2.0f, 2.0f});
        mgr.delete_entity(e);
        t_check("销毁后 position 失效", mgr.get_ptr<position>(e) == nullptr || !mgr.is_entity_valid(e));
        t_check("销毁后实体无效", !mgr.is_entity_valid(e));
    }

    t_section(6, "clear 兜底清空");
    {
        ecs::manager mgr;
        for (int i = 0; i < 4; ++i)
        {
            auto e = mgr.create_entity();
            mgr.add(e, position{static_cast<float>(i), 0.0f});
        }
        mgr.clear();
        auto* set = mgr.get_single_class_set<position>();
        t_check("clear 后组件集空", set == nullptr || set->size() == 0);
    }

    t_section(7, "preallocate 与 组件排序");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(64);
        auto e0 = mgr.create_entity();
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        mgr.add(e0, position{3.0f, 0.0f});
        mgr.add(e1, position{1.0f, 0.0f});
        mgr.add(e2, position{2.0f, 0.0f});

        mgr.sort_entities_by_component<position>(std::less<position>{});
        auto* set = mgr.get_single_class_set<position>();
        bool sorted = set != nullptr;
        if (set)
        {
            auto* pool = set->get_typed_pool_ptr<position>();
            for (size_t i = 1; i < set->size(); ++i)
            {
                if ((*pool)[i - 1].x > (*pool)[i].x) { sorted = false; }
            }
        }
        t_check("sort_entities_by_component 有序", sorted);
    }

    t_section(8, "组件类型注册查询");
    {
        ecs::manager mgr;
        t_check("未注册类型 is_component_type_registered 为假", !mgr.is_component_type_registered<position>());
        auto e = mgr.create_entity();
        mgr.add(e, position{1.0f, 2.0f});
        t_check("添加后 is_component_type_registered 为真", mgr.is_component_type_registered<position>());
        t_check("其他类型仍为假", !mgr.is_component_type_registered<velocity>());

        mgr.hard_remove<position>(e);
        t_check("移除实例后仍视为已注册", mgr.is_component_type_registered<position>());
    }

    t_section(9, "统一错误对象");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();

        const operating_message added = mgr.add(e, position{1.0f, 2.0f});
        t_check("成功添加返回成功对象", static_cast<bool>(added) && added.code() == 0);

        ecs::entity dead{};
        const operating_message bad_add = mgr.add(dead, position{3.0f, 4.0f});
        t_check("无效实体添加失败", !bad_add);
        t_check("失败码为 invalid_entity", bad_add.is_code(om_err_invalid_entity));
        t_check("失败级别为 warn", bad_add.level() == msg_level::warn);
        t_check("失败码非零", bad_add.code() != 0);

        const operating_message bad_remove = mgr.soft_remove<velocity>(e);
        t_check("移除不存在的组件集失败", !bad_remove);
        t_check("失败码为 not_found", bad_remove.is_code(om_err_not_found));

        const operating_message removed = mgr.soft_remove<position>(e);
        t_check("成功移除返回成功对象", static_cast<bool>(removed));
    }

    t_section(10, "组件存在性查询");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, position{1.0f, 2.0f});

        t_check("has_component 命中", mgr.has_component<position>(e));
        t_check("has_component 未命中", !mgr.has_component<velocity>(e));
        t_check("has_all_components 命中", mgr.has_all_components<position>(e));
        t_check("has_all_components 缺项为假", !mgr.has_all_components<position, velocity>(e));
        t_check("has_any_component 任一项命中", mgr.has_any_component<position, velocity>(e));
        t_check("has_any_component 全不命中为假", !mgr.has_any_component<velocity, health>(e));

        ecs::entity dead{};
        t_check("无效实体查询为假", !mgr.has_component<position>(dead));

        mgr.add(e, velocity{3.0f, 4.0f});
        t_check("多组件全部命中", mgr.has_all_components<position, velocity>(e));
        t_check("component_count 统计活组件", mgr.component_count<position>() == 1);
        t_check("未注册类型计数为 0", mgr.component_count<health>() == 0);

        mgr.add(e, position{9.0f, 9.0f});
        t_check("同类型覆盖后计数不变", mgr.component_count<position>() == 1);

        mgr.soft_remove<position>(e);
        t_check("软删除后计数归零", mgr.component_count<position>() == 0);
        t_check("软删除后 has_component 为假", !mgr.has_component<position>(e));

        mgr.add(e, position{4.0f, 4.0f});
        t_check("软删除后重加恢复计数", mgr.component_count<position>() == 1);

        mgr.hard_remove<position>(e);
        t_check("硬删除后计数归零", mgr.component_count<position>() == 0);
    }

    t_section(11, "create_with 创建并附加组件");
    {
        ecs::manager mgr;
        ecs::entity e{};
        const operating_message result = mgr.create_with(e, position{5.0f, 6.0f}, velocity{7.0f, 8.0f});
        t_check("create_with 成功", static_cast<bool>(result));
        t_check("实体有效", mgr.is_entity_valid(e));
        t_check("组件全部写入", mgr.has_all_components<position, velocity>(e));
        t_check("组件值正确", mgr.get_ptr<position>(e)->x == 5.0f && mgr.get_ptr<velocity>(e)->vy == 8.0f);

        ecs::entity bare{};
        t_check("无组件 create_with 成功", static_cast<bool>(mgr.create_with(bare)) && mgr.is_entity_valid(bare));
    }

    t_section(12, "实体删除与清空");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, position{1.0f, 2.0f});
        mgr.add(e, velocity{3.0f, 4.0f});

        t_check("alive_count 计入实体", mgr.alive_count() == 1);
        t_check("empty 为假", !mgr.empty());

        const operating_message cleared = mgr.clear_entity(e);
        t_check("clear_entity 成功", static_cast<bool>(cleared));
        t_check("清空后实体仍有效", mgr.is_entity_valid(e));
        t_check("清空后组件移除", !mgr.has_component<position>(e) && !mgr.has_component<velocity>(e));

        const operating_message deleted = mgr.delete_entity(e);
        t_check("delete_entity 成功", static_cast<bool>(deleted));
        t_check("删除后实体失效", !mgr.is_entity_valid(e));
        t_check("删除后 alive_count 归零", mgr.alive_count() == 0 && mgr.empty());

        const operating_message again = mgr.delete_entity(e);
        t_check("重复删除失败", !again && again.is_code(om_err_invalid_entity));

        std::vector<ecs::entity> alive;
        for (int i = 0; i < 4; ++i) alive.push_back(mgr.create_entity());
        std::vector<ecs::entity> mixed{alive[0], ecs::entity{}, alive[1], alive[2]};
        size_t deleted_count = 0;
        const operating_message batch_result = mgr.delete_entity_batch(
            std::span<const ecs::entity>(mixed.data(), mixed.size()), deleted_count);
        t_check("delete_entity_batch 上报无效句柄", !batch_result);
        t_check("批量删除跳过无效实体", deleted_count == 3);
        t_check("批量删除后仅剩 1 个实体", mgr.alive_count() == 1);
    }

    t_section(13, "批量组件操作");
    {
        ecs::manager mgr;
        std::vector<ecs::entity> ents;
        for (int i = 0; i < 3; ++i) ents.push_back(mgr.create_entity());
        for (size_t i = 0; i < ents.size(); ++i) mgr.add(ents[i], position{static_cast<float>(i), 0.0f});

        const std::span<const ecs::entity> ent_span(ents.data(), ents.size());
        std::vector<uint8_t> results(ents.size(), 0);
        operating_message query_result = mgr.has_component_batch<position>(
            ent_span, std::span<uint8_t>(results.data(), results.size()));
        t_check("has_component_batch 成功", static_cast<bool>(query_result));
        t_check("has_component_batch 结果正确", results[0] == 1 && results[1] == 1 && results[2] == 1);
        t_check("结果缓冲过短返回错误",
                !mgr.has_component_batch<position>(ent_span, std::span<uint8_t>(results.data(), 1)));

        size_t removed = 0;
        t_check("soft_remove_batch 成功", static_cast<bool>(mgr.soft_remove_batch<position>(ent_span, removed)));
        t_check("soft_remove_batch 统计正确", removed == 3);

        for (size_t i = 0; i < ents.size(); ++i) mgr.add(ents[i], position{static_cast<float>(i), 1.0f});
        t_check("hard_remove_batch 成功", static_cast<bool>(mgr.hard_remove_batch<position>(ent_span, removed)));
        t_check("hard_remove_batch 统计正确", removed == 3);
        t_check("批量移除后无残留", mgr.component_count<position>() == 0);
    }

    t_section(14, "实体状态安全重载");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        ecs::entity_state state{};
        t_check("get_entity_state 成功", static_cast<bool>(mgr.get_entity_state(e, state)));
        t_check("默认含 active 标志",
                (state.flags & static_cast<uint32_t>(ecs::entity_flag::active)) != 0);

        t_check("set_tag 成功", static_cast<bool>(mgr.set_tag(e, 42)));
        t_check("set_layer 成功", static_cast<bool>(mgr.set_layer(e, 3)));
        t_check("set_group 成功", static_cast<bool>(mgr.set_group(e, 7)));

        uint32_t out = 0;
        t_check("get_tag 读到写入值", static_cast<bool>(mgr.get_tag(e, out)) && out == 42);
        t_check("get_layer 读到写入值", static_cast<bool>(mgr.get_layer(e, out)) && out == 3);
        t_check("get_group 读到写入值", static_cast<bool>(mgr.get_group(e, out)) && out == 7);

        t_check("set_entity_flag 成功",
                static_cast<bool>(mgr.set_entity_flag(e, ecs::entity_flag::static_entity)));
        t_check("has_entity_flag 命中", mgr.has_entity_flag(e, ecs::entity_flag::static_entity));
        t_check("clear_entity_flag 成功",
                static_cast<bool>(mgr.clear_entity_flag(e, ecs::entity_flag::static_entity)));
        t_check("清除后不再命中", !mgr.has_entity_flag(e, ecs::entity_flag::static_entity));

        ecs::entity dead{};
        t_check("无效实体读状态失败", !mgr.get_entity_state(dead, state));
        t_check("无效实体写标志失败", !mgr.set_entity_flag(dead, ecs::entity_flag::active));
        t_check("无效实体查标志为假", !mgr.has_entity_flag(dead, ecs::entity_flag::active));
        t_check("无效实体写 tag 失败", !mgr.set_tag(dead, 1));
    }

    t_section(15, "clone 复制组件与用户状态");
    {
        ecs::manager mgr;
        auto source = mgr.create_entity();
        mgr.add(source, position{1.5f, 2.5f});
        mgr.add(source, velocity{3.5f, 4.5f});
        (void)mgr.set_tag(source, 11);
        (void)mgr.set_layer(source, 5);
        (void)mgr.set_group(source, 9);
        (void)mgr.set_entity_flag(source, ecs::entity_flag::static_entity);
        (void)mgr.set_entity_flag(source, ecs::entity_flag::pending_destroy);

        ecs::entity copy{};
        const operating_message result = mgr.clone(source, copy);
        t_check("clone 成功", static_cast<bool>(result));
        t_check("克隆实体有效且不同", mgr.is_entity_valid(copy) && copy != source);
        t_check("克隆组件齐全", mgr.has_all_components<position, velocity>(copy));
        t_check("克隆组件值一致",
                mgr.get_ptr<position>(copy)->y == 2.5f && mgr.get_ptr<velocity>(copy)->vx == 3.5f);

        uint32_t out = 0;
        t_check("克隆 tag", static_cast<bool>(mgr.get_tag(copy, out)) && out == 11);
        t_check("克隆 layer", static_cast<bool>(mgr.get_layer(copy, out)) && out == 5);
        t_check("克隆 group", static_cast<bool>(mgr.get_group(copy, out)) && out == 9);
        t_check("克隆 static_entity 标志", mgr.has_entity_flag(copy, ecs::entity_flag::static_entity));
        t_check("不复制 pending_destroy 标志", !mgr.has_entity_flag(copy, ecs::entity_flag::pending_destroy));
        t_check("源实体保持不变", mgr.has_all_components<position, velocity>(source));
        t_check("克隆新增一个实体", mgr.alive_count() == 2);

        ecs::entity none{};
        const operating_message bad = mgr.clone(ecs::entity{}, none);
        t_check("克隆无效实体失败", !bad && bad.is_code(om_err_invalid_entity));
        t_check("失败时输出实体无效", !none.is_valid());
    }

    t_section(16, "clone 拒绝不可复制组件");
    {
        ecs::manager mgr;
        auto source = mgr.create_entity();
        mgr.add(source, move_only_component{std::make_unique<int>(7)});

        ecs::entity copy{};
        const operating_message result = mgr.clone(source, copy);
        t_check("不可复制组件克隆失败", !result && result.is_code(om_err_unsupported_operation));
        t_check("失败回滚新建实体", !copy.is_valid() && mgr.alive_count() == 1);
    }

    t_section(17, "clone 复制 def 组件");
    {
        type_def def{};
        def.size = sizeof(int) * 2;
        def.alignment = alignof(int);
        def.trivially_copyable = true;
        const int did = type_id::register_type_def("test_highlevel_manager_clone_def", def);
        t_check("def 注册成功", did > 0);

        ecs::manager mgr;
        auto source = mgr.create_entity();
        int payload[2] = {21, 22};
        (void)mgr.add_def(source, did, payload);

        ecs::entity copy{};
        const operating_message result = mgr.clone(source, copy);
        t_check("含 def 组件克隆成功", static_cast<bool>(result));
        const int* cloned = static_cast<const int*>(mgr.get_def_ptr(copy, did));
        t_check("def 组件值一致", cloned != nullptr && cloned[0] == 21 && cloned[1] == 22);
    }

    t_section(18, "存储状态与维护");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        (void)mgr.add(a, position{1.0f, 0.0f});
        (void)mgr.add(b, position{2.0f, 0.0f});

        t_check("contains_entity 命中", mgr.contains_entity<position>(b));
        (void)mgr.soft_remove<position>(a);
        t_check("contains_entity 软删除后不命中", !mgr.contains_entity<position>(a));
        t_check("has_tombstones 命中", mgr.has_tombstones<position>());
        t_check("tombstone_count 可查询", mgr.tombstone_count<position>() >= 1);
        t_check("compact 成功", static_cast<bool>(mgr.compact<position>()));
        t_check("compact 后存活数不变", mgr.component_count<position>() == 1);
        t_check("compact_all 成功", static_cast<bool>(mgr.compact_all()));

        (void)mgr.add(a, position{3.0f, 0.0f});
        const auto* entity_indices = mgr.get_entity_indices<position>();
        const auto* entity_versions = mgr.get_entity_versions<position>();
        t_check("get_entity_indices 可访问", entity_indices != nullptr && entity_indices->size() == 2);
        t_check("get_entity_versions 长度一致", entity_versions != nullptr && entity_versions->size() == 2);
        t_check("未注册类型返回空指针",
                mgr.get_entity_indices<velocity>() == nullptr
                && mgr.get_entity_versions<velocity>() == nullptr);
        t_check("查询不隐式注册类型", !mgr.is_component_type_registered<velocity>());
        const ecs::manager& const_mgr = mgr;
        t_check("const 查询可访问",
                const_mgr.get_entity_indices<position>() != nullptr
                && const_mgr.get_entity_versions<position>() != nullptr);
        const uint64_t pool_version = mgr.get_pool_version<position>();
        (void)mgr.add(a, position{4.0f, 0.0f});
        t_check("pool version 变化", mgr.get_pool_version<position>() != pool_version);
        t_check("get_version 可查询", mgr.get_version<position>(a.parts_.index_) > 0);

        t_check("设置 hot set 容量", static_cast<bool>(mgr.set_hot_set_capacity<position>(4096)));
        t_check("读取 hot set 容量", mgr.hot_set_capacity<position>() == 4096);
        mgr.clear_hot_set<position>();
        t_check("清理 hot set 后容量保留", mgr.hot_set_capacity<position>() == 4096);
        t_check("无效容量被拒绝", !mgr.set_hot_set_capacity<position>(3));

        t_check("raw pool 可访问", mgr.get_raw_pool_data<position>() != nullptr);
        t_check("type id 可访问", mgr.get_type_id<position>() == type_id::get_type_id<position>());

        (void)mgr.preallocate_entities(5);
        int alive_visited = 0;
        mgr.for_each_alive_entity([&](ecs::entity) noexcept { ++alive_visited; });
        t_check("for_each_alive_entity 数量", static_cast<size_t>(alive_visited) == mgr.alive_count());
        t_check("capacity 可查询", mgr.capacity<position>() >= 2);
        t_check("reserved_entities 可查询", mgr.reserved_entities() == 5);
        t_check("memory_usage 可查询", mgr.memory_usage() > 0);

        mgr.reset();
        t_check("reset 清空实体", mgr.alive_count() == 0);
        t_check("reset 清空组件", mgr.component_count<position>() == 0);
        t_check("reset 旧句柄失效", !mgr.is_entity_valid(a));
        auto fresh = mgr.create_entity();
        t_check("reset 后可继续创建", mgr.is_entity_valid(fresh));
    }

    t_section(19, "批量创建、克隆与组件便利接口");
    {
        ecs::manager mgr;
        dense<ecs::entity> created;
        t_check("create_entities 成功", static_cast<bool>(mgr.create_entities(3, created)));
        t_check("create_entities 数量", created.size() == 3 && mgr.alive_count() == 3);
        for (size_t i = 0; i < created.size(); ++i)
        {
            (void)mgr.add(created[i], position{static_cast<float>(i), 0.0f});
        }

        dense<ecs::entity> clones;
        t_check("clone_batch 成功",
                static_cast<bool>(mgr.clone_batch(
                    std::span<const ecs::entity>(created.data(), created.size()), clones)));
        t_check("clone_batch 数量", clones.size() == 3);
        t_check("clone_batch 组件复制", mgr.has_all_components<position>(clones[0]));

        auto dst = mgr.create_entity();
        (void)mgr.add(dst, velocity{9.0f, 9.0f});
        t_check("clone_into 成功", static_cast<bool>(mgr.clone_into(dst, created[0])));
        t_check("clone_into 覆盖旧组件",
                mgr.has_component<position>(dst) && !mgr.has_component<velocity>(dst));
        t_check("clone_into 自身克隆失败", !mgr.clone_into(dst, dst));

        auto fresh = mgr.create_entity();
        position* out = nullptr;
        t_check("get_or_add 新组件",
                static_cast<bool>(mgr.get_or_add(fresh, position{5.0f, 6.0f}, out))
                && out != nullptr);
        t_check("get_or_add 已存在不覆盖",
                static_cast<bool>(mgr.get_or_add(fresh, position{7.0f, 8.0f}, out))
                && out->x == 5.0f);

        velocity* vout = nullptr;
        t_check("emplace 成功",
                static_cast<bool>(mgr.emplace<velocity>(created[2], vout, 1.0f, 2.0f))
                && vout->vx == 1.0f);

        t_check("replace 成功", static_cast<bool>(mgr.replace(created[0], position{3.0f, 4.0f})));
        t_check("replace 值生效", mgr.get_ptr<position>(created[0])->x == 3.0f);
        t_check("replace 缺失类型失败", !mgr.replace(created[0], health{1}));

        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        std::vector<ecs::entity> two{e1, e2};
        t_check("add_batch 单值成功",
                static_cast<bool>(mgr.add_batch(
                    health{10}, std::span<const ecs::entity>(two.data(), two.size()))));
        t_check("add_batch 单值生效",
                mgr.has_all_components<health>(e1) && mgr.has_all_components<health>(e2));
    }

    t_section(20, "def 组件批量接口");
    {
        type_def def{};
        def.size = sizeof(int) * 2;
        def.alignment = alignof(int);
        def.trivially_copyable = true;
        const int did = type_id::register_type_def("test_highlevel_manager_def_batch", def);
        t_check("def 注册成功", did > 0);

        ecs::manager mgr;
        std::vector<ecs::entity> ents;
        for (int i = 0; i < 3; ++i) ents.push_back(mgr.create_entity());
        int p0[2] = {1, 2};
        int p1[2] = {3, 4};
        int p2[2] = {5, 6};
        const void* ptrs[3] = {p0, p1, p2};
        size_t added = 0;
        t_check("add_def_batch 成功",
                static_cast<bool>(mgr.add_def_batch(
                    did, std::span<const ecs::entity>(ents.data(), ents.size()),
                    std::span<const void*>(ptrs, 3), added)) && added == 3);
        t_check("has_def 命中", mgr.has_def(ents[0], did));
        t_check("has_def 未命中", !mgr.has_def(ents[0], std::string_view("missing_def")));

        std::vector<uint8_t> def_results(3, 0);
        t_check("has_def_batch 成功",
                static_cast<bool>(mgr.has_def_batch(
                    did, std::span<const ecs::entity>(ents.data(), ents.size()),
                    std::span<uint8_t>(def_results.data(), def_results.size())))
                && def_results[0] == 1);

        size_t removed = 0;
        t_check("soft_remove_def_batch 成功",
                static_cast<bool>(mgr.soft_remove_def_batch(
                    did, std::span<const ecs::entity>(ents.data(), ents.size()), removed))
                && removed == 3);
        t_check("soft 后 has_def 不命中", !mgr.has_def(ents[0], did));

        added = 0;
        t_check("重新 add_def_batch 成功",
                static_cast<bool>(mgr.add_def_batch(
                    did, std::span<const ecs::entity>(ents.data(), ents.size()),
                    std::span<const void*>(ptrs, 3), added)) && added == 3);
        removed = 0;
        t_check("hard_remove_def_batch 成功",
                static_cast<bool>(mgr.hard_remove_def_batch(
                    did, std::span<const ecs::entity>(ents.data(), ents.size()), removed))
                && removed == 3);
    }

    t_section(21, "实体状态整体写入与按索引写入");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        ecs::entity_state state{};
        state.flags = static_cast<uint32_t>(ecs::entity_flag::active)
                    | static_cast<uint32_t>(ecs::entity_flag::static_entity);
        state.tag = 17;
        state.layer = 4;
        state.group_id = 6;
        t_check("set_entity_state 成功", static_cast<bool>(mgr.set_entity_state(e, state)));

        ecs::entity_state got{};
        t_check("set_entity_state 生效",
                static_cast<bool>(mgr.get_entity_state(e, got)) && got.tag == 17
                && got.layer == 4 && got.group_id == 6);

        const uint32_t idx = e.parts_.index_;
        t_check("set_tag_by_index 成功", static_cast<bool>(mgr.set_tag_by_index(idx, 8)));
        t_check("set_layer_by_index 成功", static_cast<bool>(mgr.set_layer_by_index(idx, 5)));
        t_check("set_group_by_index 成功", static_cast<bool>(mgr.set_group_by_index(idx, 7)));
        uint32_t out = 0;
        t_check("按索引写入 tag 生效", static_cast<bool>(mgr.get_tag(e, out)) && out == 8);
        t_check("按索引写入 layer 生效", static_cast<bool>(mgr.get_layer(e, out)) && out == 5);
        t_check("按索引写入 group 生效", static_cast<bool>(mgr.get_group(e, out)) && out == 7);
        t_check("越界索引写入失败", !mgr.set_tag_by_index(idx + 1000, 1));
    }

    t_section(22, "const 视图与分组");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        (void)mgr.add(e, position{1.0f, 2.0f});
        (void)mgr.add(e, velocity{3.0f, 4.0f});
        const ecs::manager& cmgr = mgr;

        int visited = 0;
        cmgr.view<position>().for_each([&](position&) noexcept { ++visited; });
        t_check("const view 可用", visited == 1);

        visited = 0;
        cmgr.group<position, velocity>().for_each(
            [&](position&, velocity&) noexcept { ++visited; });
        t_check("const group 可用", visited == 1);
    }

    t_section(23, "实体事件监听器");
    {
        ecs::manager mgr;
        int created_a = 0;
        int created_b = 0;
        int destroyed = 0;
        int single_created = 0;
        uint32_t id_a = 0;
        uint32_t id_b = 0;
        uint32_t id_d = 0;
        t_check("注册 created 监听器 A",
                static_cast<bool>(mgr.add_entity_listener(
                    ecs::entity_event::created, &created_probe, &created_a, id_a)));
        t_check("注册 created 监听器 B",
                static_cast<bool>(mgr.add_entity_listener(
                    ecs::entity_event::created, &created_probe, &created_b, id_b)));
        t_check("注册 destroyed 监听器",
                static_cast<bool>(mgr.add_entity_listener(
                    ecs::entity_event::destroyed, &destroyed_probe, &destroyed, id_d)));
        mgr.set_on_entity_created(&created_probe, &single_created);

        auto e = mgr.create_entity();
        t_check("创建触发全部 created 监听器",
                created_a == 1 && created_b == 1 && single_created == 1);
        (void)mgr.delete_entity(e);
        t_check("销毁触发 destroyed 监听器", destroyed == 1);

        t_check("移除监听器成功", mgr.remove_entity_listener(id_a));
        t_check("移除不存在监听器失败", !mgr.remove_entity_listener(999999));
        (void)mgr.create_entity();
        t_check("移除后仅触发剩余监听器",
                created_a == 1 && created_b == 2 && single_created == 2);
    }

    t_section(24, "def 类型回调");
    {
        type_def def{};
        def.size = sizeof(int);
        def.alignment = alignof(int);
        def.trivially_copyable = true;
        const int did = type_id::register_type_def("test_highlevel_manager_def_callback", def);
        t_check("def 注册成功", did > 0);

        ecs::manager mgr;
        g_def_add_calls = 0;
        g_def_remove_calls = 0;
        t_check("注册 def add 回调",
                static_cast<bool>(mgr.set_on_add_def(did, &def_add_probe, &g_def_add_calls)));
        t_check("注册 def remove 回调",
                static_cast<bool>(mgr.set_on_remove_def(did, &def_remove_probe, &g_def_remove_calls)));
        t_check("未知 def 注册回调失败",
                !mgr.set_on_add_def(999999, &def_add_probe, &g_def_add_calls));

        auto e = mgr.create_entity();
        int payload = 11;
        t_check("add_def 触发 add 回调",
                static_cast<bool>(mgr.add_def(e, did, &payload)) && g_def_add_calls == 1);
        t_check("hard_remove_def 触发 remove 回调",
                static_cast<bool>(mgr.hard_remove_def(e, did)) && g_def_remove_calls == 1);
    }

    t_section(25, "系统上下文查询与执行");
    {
        ecs::manager mgr;
        int system_a = 1;
        int system_b = 2;
        int system_c = 3;
        ecs::system_context ctx_a{};
        ctx_a.order = 20;
        ctx_a.phase = 1;
        ctx_a.run = &system_probe;
        ctx_a.user_data = &system_a;
        ecs::system_context ctx_b{};
        ctx_b.order = 10;
        ctx_b.phase = 1;
        ctx_b.run = &system_probe;
        ctx_b.user_data = &system_b;
        ecs::system_context ctx_c{};
        ctx_c.order = 5;
        ctx_c.phase = 0;
        ctx_c.run = &system_probe;
        ctx_c.user_data = &system_c;
        mgr.register_system(ctx_a);
        mgr.register_system(ctx_b);
        mgr.register_system(ctx_c);

        g_system_count = 0;
        t_check("run_systems(phase) 成功", static_cast<bool>(mgr.run_systems(1)));
        t_check("按 order 执行",
                g_system_count == 2 && g_system_order[0] == 2 && g_system_order[1] == 1);

        dense<ecs::system_context> queried;
        t_check("query_systems 成功",
                static_cast<bool>(mgr.query_systems(1, queried)) && queried.size() == 2);
        t_check("remove_system 成功", static_cast<bool>(mgr.remove_system(0)));
        g_system_count = 0;
        t_check("remove 后执行剩余系统",
                static_cast<bool>(mgr.run_systems(1)) && g_system_count == 1
                && g_system_order[0] == 2);
        t_check("run_systems 全阶段成功", static_cast<bool>(mgr.run_systems()));
        t_check("非法 phase 被拒绝", !mgr.run_systems(4));
    }

    t_section(26, "swap 与 merge");
    {
        ecs::manager a;
        ecs::manager b;
        auto ea = a.create_entity();
        (void)a.add(ea, position{1.0f, 2.0f});
        auto eb = b.create_entity();
        (void)b.add(eb, velocity{3.0f, 4.0f});
        a.swap(b);
        t_check("swap 后实体随存储交换", b.is_entity_valid(ea) && a.is_entity_valid(eb));
        t_check("swap 后组件随存储交换",
                b.get_ptr<position>(ea) != nullptr && a.get_ptr<velocity>(eb) != nullptr);
        t_check("swap 后计数不变", a.alive_count() == 1 && b.alive_count() == 1);

        ecs::manager dst;
        ecs::manager src;
        type_def merge_def{};
        merge_def.size = sizeof(int);
        merge_def.alignment = alignof(int);
        merge_def.trivially_copyable = true;
        const int merge_def_id = type_id::register_type_def("test_highlevel_manager_merge_def", merge_def);
        auto se = src.create_entity();
        (void)src.add(se, position{8.0f, 9.0f});
        (void)src.add(se, velocity{1.0f, 2.0f});
        int merge_payload = 42;
        (void)src.add_def(se, merge_def_id, &merge_payload);
        (void)src.set_tag(se, 3);
        const operating_message merge_result = dst.merge(std::move(src));
        t_check("merge 成功", static_cast<bool>(merge_result));
        t_check("merge 后目标实体数", dst.alive_count() == 1);

        ecs::entity merged{};
        dst.for_each_alive_entity([&](ecs::entity e) noexcept { merged = e; });
        t_check("merge 组件齐全", dst.has_all_components<position, velocity>(merged));
        t_check("merge 组件值", dst.get_ptr<position>(merged)->x == 8.0f);
        const int* merged_def = static_cast<const int*>(dst.get_def_ptr(merged, merge_def_id));
        t_check("merge def 组件复制", merged_def != nullptr && *merged_def == 42);
        uint32_t tag = 0;
        t_check("merge 状态复制", static_cast<bool>(dst.get_tag(merged, tag)) && tag == 3);
        t_check("merge 清空源", src.alive_count() == 0);
    }

    t_section(27, "部分失败语义");
    {
        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        std::vector<ecs::entity> ents{a, ecs::entity{}, b};
        std::vector<position> comps{position{1.0f, 2.0f}, position{3.0f, 4.0f}, position{5.0f, 6.0f}};
        const operating_message add_result = mgr.add_batch<position>(
            std::span<const ecs::entity>(ents.data(), ents.size()),
            std::span<const position>(comps.data(), comps.size()));
        t_check("add_batch 部分失败返回错误",
                !add_result && add_result.is_code(om_err_invalid_entity));
        t_check("add_batch 成功项保留",
                mgr.has_component<position>(a) && mgr.has_component<position>(b));
        t_check("add_batch 成功计数", mgr.component_count<position>() == 2);

        auto c = mgr.create_entity();
        auto d = mgr.create_entity();
        std::vector<ecs::entity> ents2{c, ecs::entity{}, d};
        const operating_message add_one_result = mgr.add_batch(
            health{10}, std::span<const ecs::entity>(ents2.data(), ents2.size()));
        t_check("单值 add_batch 部分失败",
                !add_one_result && mgr.has_component<health>(c) && mgr.has_component<health>(d));

        size_t removed = 0;
        const operating_message remove_result = mgr.hard_remove_batch<position>(
            std::span<const ecs::entity>(ents.data(), ents.size()), removed);
        t_check("remove_batch 部分失败返回错误",
                !remove_result && remove_result.is_code(om_err_invalid_entity));
        t_check("remove_batch 成功项保留",
                removed == 2 && !mgr.has_component<position>(a)
                && !mgr.has_component<position>(b));
    }

    {
        ecs::manager mgr;
        auto ok = mgr.create_entity();
        (void)mgr.add(ok, position{1.0f, 2.0f});
        auto bad = mgr.create_entity();
        (void)mgr.add(bad, move_only_component{std::make_unique<int>(1)});
        std::vector<ecs::entity> sources{bad, ok};
        dense<ecs::entity> clones;
        const operating_message result = mgr.clone_batch(
            std::span<const ecs::entity>(sources.data(), sources.size()), clones);
        t_check("clone_batch 部分失败返回错误",
                !result && result.is_code(om_err_unsupported_operation));
        t_check("clone_batch 成功项保留",
                clones.size() == 1 && mgr.has_component<position>(clones[0]));
    }

    {
        ecs::manager dst;
        ecs::manager src;
        auto ok = src.create_entity();
        (void)src.add(ok, position{7.0f, 8.0f});
        auto bad = src.create_entity();
        (void)src.add(bad, move_only_component{std::make_unique<int>(2)});
        const operating_message result = dst.merge(std::move(src));
        t_check("merge 部分失败返回错误",
                !result && result.is_code(om_err_unsupported_operation));
        t_check("merge 成功实体保留",
                dst.alive_count() == 1 && dst.component_count<position>() == 1);
        t_check("merge 失败实体留在源",
                src.is_entity_valid(bad) && !src.is_entity_valid(ok) && src.alive_count() == 1);
    }

    {
        type_def def{};
        def.size = sizeof(int);
        def.alignment = alignof(int);
        def.trivially_copyable = true;
        const int did = type_id::register_type_def("test_highlevel_manager_partial_def", def);
        t_check("def 注册成功", did > 0);

        ecs::manager mgr;
        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        std::vector<ecs::entity> ents{a, ecs::entity{}, b};
        int values[3] = {1, 2, 3};
        const void* ptrs[3] = {&values[0], &values[1], &values[2]};
        size_t added = 0;
        const operating_message result = mgr.add_def_batch(
            did, std::span<const ecs::entity>(ents.data(), ents.size()),
            std::span<const void*>(ptrs, 3), added);
        t_check("add_def_batch 部分失败返回错误",
                !result && result.is_code(om_err_invalid_entity) && added == 2);
        t_check("add_def_batch 成功项保留",
                mgr.has_def(a, did) && mgr.has_def(b, did));
    }

    return t_end("manager ");
}
