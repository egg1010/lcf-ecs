#pragma once
#include "part/dense.hpp"
#include "part/void_any.hpp"
#include "entity.hpp"
#include <cstdint>
#include <utility>

namespace ecs
{

class manager;

enum class command_op : uint8_t
{
    add_component,
    remove_component,
    destroy_entity
};

// 延迟结构变更缓冲:录制 add/remove/destroy,flush 时按序回放
class command_buffer
{
private:
    struct command
    {
        command_op op{command_op::add_component};
        entity target{};
        void_any payload{};
        void (*apply_fn)(manager*, entity, void_any&) noexcept{nullptr};
    };

    manager* mgr_;
    dense<command> commands_;

    // apply 回调:由 component.hpp 在 manager 完整后包含,故可内联调用 manager 方法
    template <typename T>
    static void apply_add(manager* mgr, entity e, void_any& payload) noexcept
    {
        T* p = payload.template get_ptr<T>();
        if (!p) [[unlikely]] return;
        mgr->template add<T>(e, std::move(*p));
    }

    template <typename T>
    static void apply_remove(manager* mgr, entity e, void_any&) noexcept
    {
        mgr->template soft_remove<T>(e);
    }

    static void apply_destroy(manager* mgr, entity e, void_any&) noexcept
    {
        mgr->delete_entity(e);
    }

public:
    explicit command_buffer(manager* mgr) noexcept : mgr_(mgr) {}

    command_buffer(const command_buffer&) = delete;
    command_buffer& operator=(const command_buffer&) = delete;
    command_buffer(command_buffer&&) noexcept = default;
    command_buffer& operator=(command_buffer&&) noexcept = default;

    template <typename T>
    void add_component(entity e, T&& comp) noexcept
    {
        commands_.emplace_back(command{
            command_op::add_component, e,
            void_any(std::forward<T>(comp)),
            &apply_add<std::decay_t<T>>});
    }

    template <typename T>
    void remove_component(entity e) noexcept
    {
        commands_.emplace_back(command{
            command_op::remove_component, e,
            void_any{},
            &apply_remove<T>});
    }

    void destroy_entity(entity e) noexcept
    {
        commands_.emplace_back(command{
            command_op::destroy_entity, e,
            void_any{},
            &apply_destroy});
    }

    void clear() noexcept { commands_.clear(); }
    [[nodiscard]] size_t size() const noexcept { return commands_.size(); }
    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }

    // 按录入顺序回放全部命令,完成后清空
    void flush() noexcept
    {
        for (size_t i = 0; i < commands_.size(); ++i)
        {
            auto& cmd = commands_[i];
            if (cmd.apply_fn) [[likely]]
            {
                cmd.apply_fn(mgr_, cmd.target, cmd.payload);
            }
        }
        commands_.clear();
    }

    ~command_buffer() = default;
};

} // namespace ecs
