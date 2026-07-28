#pragma once
#include "part/dense.hpp"
#include "part/void_any.hpp"
#include "entity.hpp"
#include <cstdint>
#include <utility>

namespace ecs
{

class manager;

// 延迟结构变更缓冲:录制 add/remove/destroy,flush 时按序回放
class command_buffer
{
private:
    using apply_fn_t = void (*)(manager*, entity, void_any&) noexcept;

    // command 布局: entity(8) + void_any(64) + fn(8) = 80B
    // op 字段移除: apply_fn 已能区分操作类型,从未被读取
    struct command
    {
        entity target{};
        void_any payload{};
        apply_fn_t apply_fn{nullptr};
    };

    manager* mgr_;
    dense<command> commands_;

    // apply 回调:由 component.hpp 在 manager 完整后包含,故可内联调用 manager 方法
    template <typename T>
    static void apply_add(manager* mgr, entity e, void_any& payload) noexcept
    {
        // fast_get_ptr 跳过 type_id 检查 (payload 类型在录制时已确定)
        T* p = payload.template fast_get_ptr<T>();
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

    // 预分配容量,避免录制过程中扩容
    void reserve(size_t n) noexcept { commands_.increase_capacity(n); }
    [[nodiscard]] size_t capacity() const noexcept { return commands_.capacity(); }

    template <typename T>
    void add_component(entity e, T&& comp) noexcept
    {
        commands_.emplace_back(command{
            e,
            void_any(std::forward<T>(comp)),
            &apply_add<std::decay_t<T>>});
    }

    template <typename T>
    void remove_component(entity e) noexcept
    {
        commands_.emplace_back(command{
            e,
            void_any{},
            &apply_remove<T>});
    }

    void destroy_entity(entity e) noexcept
    {
        commands_.emplace_back(command{
            e,
            void_any{},
            &apply_destroy});
    }

    void clear() noexcept { commands_.clear(); }
    [[nodiscard]] size_t size() const noexcept { return commands_.size(); }
    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }

    // 按录入顺序回放全部命令,完成后清空
    // for_each 内部 DENSE_FLATTEN + ivdep, 去掉 apply_fn 空检查 (录制时一定设置)
    void flush() noexcept
    {
        commands_.for_each([&](command& cmd) noexcept {
            cmd.apply_fn(mgr_, cmd.target, cmd.payload);
        });
        commands_.clear();
    }

    ~command_buffer() = default;
};

} // namespace ecs
