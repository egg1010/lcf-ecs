// test_highlevel_manager.cpp - 高层封装测试: ecs::manager
#include "test_common.hpp"
#include "include/ecs/manager.hpp"
#include "include/ecs/entity/entity.hpp"
#include <functional>




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
        t_check("未注册类型 has_component 为假", !mgr.has_component<position>());
        auto e = mgr.create_entity();
        mgr.add(e, position{1.0f, 2.0f});
        t_check("添加后 has_component 为真", mgr.has_component<position>());
        t_check("其他类型仍为假", !mgr.has_component<velocity>());

        mgr.hard_remove<position>(e);
        t_check("移除实例后仍视为已注册", mgr.has_component<position>());
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

    return t_end("manager ");
}
