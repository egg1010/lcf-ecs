// test_highlevel_signals.cpp - 高层封装测试: 生命周期信号
#include "test_common.hpp"
#include "include/ecs/manager.hpp"
#include "include/ecs/entity/entity.hpp"

struct position
{
    float x;
    float y;
};

int main()
{
    t_begin("test_highlevel_signals — 高层封装测试: 生命周期信号");

    t_section(1, "组件信号");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.flush_component_signals([](uint32_t, uint32_t, uint32_t) {});

        mgr.add(e, position{1.0f, 2.0f});
        t_check("add 后有待处理组件信号", mgr.has_pending_component_signals());

        uint32_t comp_events = 0;
        mgr.flush_component_signals([&](uint32_t type, uint32_t idx, uint32_t cid) {
            (void)type;
            (void)idx;
            (void)cid;
            ++comp_events;
        });
        t_check("组件信号计数 >= 1", comp_events >= 1);
        t_check("flush 后清空", !mgr.has_pending_component_signals());
    }

    t_section(2, "组件信号禁用开关");
    {
        ecs::manager mgr;
        mgr.disable_comp_signals();
        auto e = mgr.create_entity();
        mgr.add(e, position{1.0f, 2.0f});
        t_check("禁用后无组件信号", !mgr.has_pending_component_signals());

        mgr.enable_comp_signals();
        auto e2 = mgr.create_entity();
        mgr.add(e2, position{3.0f, 4.0f});
        t_check("启用后有组件信号", mgr.has_pending_component_signals());
    }

    t_section(3, "实体信号");
    {
        ecs::manager mgr;
        mgr.enable_entity_signals();
        mgr.flush_entity_signals([](uint32_t, uint32_t) {});
        auto e = mgr.create_entity();
        t_check("创建实体后有信号", mgr.has_pending_entity_signals() && e.is_valid());

        uint32_t ent_events = 0;
        mgr.flush_entity_signals([&](uint32_t type, uint32_t idx) {
            (void)type;
            (void)idx;
            ++ent_events;
        });
        t_check("实体信号计数 >= 1", ent_events >= 1);
        t_check("flush 后清空", !mgr.has_pending_entity_signals());
    }

    t_section(4, "实体信号禁用开关");
    {
        ecs::manager mgr;
        mgr.disable_entity_signals();
        mgr.flush_entity_signals([](uint32_t, uint32_t) {});
        auto e = mgr.create_entity();
        t_check("禁用后无实体信号", !mgr.has_pending_entity_signals() && e.is_valid());
    }

    return t_end("signals ");
}
