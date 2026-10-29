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

    t_section(4, "addc 变参");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.addc(e, position{9.0f, 1.0f}, velocity{1.0f, 1.0f});
        t_check("addc 单实体多组件", mgr.get_ptr<position>(e) != nullptr && mgr.get_ptr<velocity>(e) != nullptr);

        auto a = mgr.create_entity();
        auto b = mgr.create_entity();
        mgr.addc(health{50}, a, b);
        t_check("addc 单组件多实体", mgr.get_ptr<health>(a) != nullptr && mgr.get_ptr<health>(b) != nullptr);
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
        t_check("delete_entity_batch 成功", static_cast<bool>(batch_result));
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
        t_check("has_component_batch 成功",
                mgr.has_component_batch<position>(ent_span, std::span<uint8_t>(results.data(), results.size())));
        t_check("has_component_batch 结果正确", results[0] == 1 && results[1] == 1 && results[2] == 1);
        t_check("结果缓冲过短返回 false",
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

    return t_end("manager ");
}
