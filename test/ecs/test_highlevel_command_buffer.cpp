// test_highlevel_command_buffer.cpp - 高层封装测试: command_buffer
#include "test_common.hpp"
#include "include/ecs/manager.hpp"
#include "include/ecs/entity/entity.hpp"
#include "include/ecs/command/command_buffer.hpp"
#include <span>
#include <vector>

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

struct unknown_component
{
    int value;
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
        cb2.delete_entity(e);
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

    t_section(4, "flush 返回值与失败上报");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        ecs::command_buffer cb(&mgr);
        cb.add_component(e, position{1.0f, 1.0f});
        t_check("flush 返回成功", static_cast<bool>(cb.flush()));
        t_check("flush 后清空", cb.empty());

        ecs::command_buffer partial(&mgr);
        partial.add_component(e, position{2.0f, 2.0f});
        partial.remove_component<unknown_component>(e);
        t_check("flush 部分失败上报", !partial.flush());
        t_check("flush 成功命令保留",
                mgr.get_ptr<position>(e) != nullptr && mgr.get_ptr<position>(e)->x == 2.0f);
    }

    t_section(5, "批量录制");
    {
        ecs::manager mgr;
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        std::vector<ecs::entity> ents{e1, e2};
        std::vector<position> comps{position{1.0f, 2.0f}, position{3.0f, 4.0f}};
        ecs::command_buffer cb(&mgr);
        t_check("add_component_batch 录制成功",
                static_cast<bool>(cb.add_component_batch<position>(
                    std::span<const ecs::entity>(ents.data(), ents.size()),
                    std::span<const position>(comps.data(), comps.size()))));
        t_check("批量 add flush 成功", static_cast<bool>(cb.flush()));
        t_check("批量 add 生效",
                mgr.has_all_components<position>(e1) && mgr.has_all_components<position>(e2));

        t_check("remove_component_batch 录制成功",
                static_cast<bool>(cb.remove_component_batch<position>(
                    std::span<const ecs::entity>(ents.data(), ents.size()))));
        t_check("批量 remove flush 成功", static_cast<bool>(cb.flush()));
        t_check("批量 remove 生效",
                !mgr.has_component<position>(e1) && !mgr.has_component<position>(e2));
    }

    t_section(6, "def 组件与延迟实体");
    {
        type_def def{};
        def.size = sizeof(int) * 2;
        def.alignment = alignof(int);
        def.trivially_copyable = true;
        const int did = type_id::register_type_def("test_command_buffer_def", def);
        t_check("def 注册成功", did > 0);

        ecs::manager mgr;
        auto e = mgr.create_entity();
        int payload[2] = {7, 8};
        ecs::command_buffer cb(&mgr);
        t_check("add_def_component 录制成功",
                static_cast<bool>(cb.add_def_component(e, did, payload)));
        t_check("def flush 成功", static_cast<bool>(cb.flush()));
        const int* stored = static_cast<const int*>(mgr.get_def_ptr(e, did));
        t_check("def 录制值生效", stored != nullptr && stored[0] == 7 && stored[1] == 8);

        auto e2 = mgr.create_entity();
        t_check("按名录制 def 成功",
                static_cast<bool>(cb.add_def_component(e2, "test_command_buffer_def", payload)));
        t_check("按名 def flush 成功", static_cast<bool>(cb.flush()));

        const size_t alive_before = mgr.alive_count();
        ecs::deferred_entity pending = cb.create_entity();
        t_check("延迟实体未回放时 resolve 无效", !cb.resolve(pending).is_valid());
        t_check("延迟实体录制后不立即修改实体数", mgr.alive_count() == alive_before);

        t_check("延迟实体绑定组件成功",
                [&] {
                    cb.add_component(pending, position{3.0f, 4.0f});
                    return true;
                }());
        t_check("延迟实体绑定 def 成功",
                static_cast<bool>(cb.add_def_component(pending, did, payload)));
        t_check("延迟实体 flush 成功", static_cast<bool>(cb.flush()));

        const ecs::entity created = cb.resolve(pending);
        t_check("延迟实体 resolve 有效", mgr.is_entity_valid(created));
        t_check("延迟实体组件生效",
                mgr.get_ptr<position>(created) != nullptr
                && mgr.get_ptr<position>(created)->x == 3.0f);
        const int* created_def = static_cast<const int*>(mgr.get_def_ptr(created, did));
        t_check("延迟实体 def 生效", created_def != nullptr && created_def[0] == 7);

        t_check("重复 flush 不重复创建",
                static_cast<bool>(cb.flush())
                && mgr.alive_count() == alive_before + 1
                && mgr.is_entity_valid(cb.resolve(pending)));

        ecs::command_buffer other(&mgr);
        t_check("跨缓冲占位 id 被拒绝", !other.resolve(pending).is_valid());

        ecs::deferred_entity removed = cb.create_entity();
        cb.add_component(removed, position{5.0f, 6.0f});
        cb.remove_component<position>(removed);
        cb.delete_entity(removed);
        t_check("创建后删除 flush 成功", static_cast<bool>(cb.flush()));
        t_check("创建后删除 resolve 无效", !cb.resolve(removed).is_valid());

        ecs::deferred_entity stale = cb.create_entity();
        cb.clear();
        t_check("clear 后占位 id 失效", !cb.resolve(stale).is_valid());
        ecs::deferred_entity fresh = cb.create_entity();
        t_check("clear 后新占位 id 可用", fresh.is_valid() && fresh != stale);
        t_check("clear 后新实体 flush 成功", static_cast<bool>(cb.flush()));
        t_check("clear 后新实体 resolve 有效", mgr.is_entity_valid(cb.resolve(fresh)));
    }

    return t_end("command_buffer ");
}
