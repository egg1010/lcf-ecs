// test_highlevel_command_buffer.cpp - 高层封装测试: command_buffer
#include "test_common.hpp"
#include "include/ecs/manager.hpp"
#include "include/ecs/entity/entity.hpp"
#include "include/ecs/command/command_buffer.hpp"

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

int main()
{
    t_begin("test_highlevel_command_buffer — 高层封装测试: command_buffer");

    t_section(1, "录制与回放 add");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        ecs::command_buffer cb(&mgr);
        cb.preallocate(8);
        t_check("preallocate 容量", cb.capacity() >= 8);
        t_check("初始为空", cb.empty());

        cb.add_component(e, position{1.0f, 2.0f});
        cb.add_component(e, velocity{3.0f, 4.0f});
        t_check("录制 2 条", cb.size() == 2);
        t_check("flush 前未应用", mgr.get_ptr<position>(e) == nullptr);

        cb.flush();
        t_check("flush 后应用 position", mgr.get_ptr<position>(e) != nullptr);
        t_check("flush 后应用 velocity", mgr.get_ptr<velocity>(e) != nullptr);
        t_check("flush 后清空", cb.empty());
    }

    t_section(2, "remove 与 destroy");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, position{1.0f, 1.0f});
        mgr.add(e, velocity{1.0f, 1.0f});

        ecs::command_buffer cb(&mgr);
        cb.remove_component<velocity>(e);
        cb.flush();
        t_check("flush 移除 velocity", mgr.get_ptr<velocity>(e) == nullptr);
        t_check("position 保留", mgr.get_ptr<position>(e) != nullptr);

        ecs::command_buffer cb2(&mgr);
        cb2.destroy_entity(e);
        cb2.flush();
        t_check("flush 销毁实体", !mgr.is_entity_valid(e));
    }

    t_section(3, "clear");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        ecs::command_buffer cb(&mgr);
        cb.add_component(e, position{1.0f, 1.0f});
        cb.clear();
        t_check("clear 丢弃命令", cb.empty() && cb.size() == 0);
        cb.flush();
        t_check("clear 后 flush 无效果", mgr.get_ptr<position>(e) == nullptr);
    }

    return t_end("command_buffer ");
}
