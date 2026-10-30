#pragma once
#include "part/dense.hpp"
#include "part/type/void_any.hpp"
#include "part/operating_message.hpp"
#include "ecs/entity/entity.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace ecs
{

class manager;

// 命令缓冲内的占位实体, 仅在所属 command_buffer 内有效
struct deferred_entity
{
    uint64_t buffer_id{0};
    uint32_t local_id{0};

    [[nodiscard]] constexpr bool is_valid() const noexcept
    {
        return buffer_id != 0 && local_id != 0;
    }

    constexpr bool operator==(const deferred_entity&) const noexcept = default;
};

// 延迟结构变更缓冲: 录制 add/remove/destroy/def/create, flush 时按序回放
class command_buffer
{
private:
    struct def_command_payload
    {
        int def_id{0};
        dense<uint8_t> bytes;
    };

    struct command;

    using apply_fn_t = operating_message (*)(command_buffer&, command&) noexcept;

    struct command
    {
        entity target{};
        deferred_entity deferred_target{};
        void_any payload{};
        apply_fn_t apply_fn{nullptr};
    };

    manager* mgr_;
    dense<command> commands_;
    dense<entity> resolved_;
    uint64_t buffer_id_;
    uint32_t next_local_id_{0};

    static uint64_t next_buffer_id_() noexcept
    {
        static std::atomic<uint64_t> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    static operating_message make_error_(uint32_t code) noexcept
    {
        operating_message error;
        error.write_failure(code, msg_level::warn, msg::here());
        return error;
    }

    [[nodiscard]] entity resolve_target_(const command& cmd) const noexcept
    {
        if (!mgr_) [[unlikely]] return entity{};
        if (cmd.deferred_target.is_valid()) return resolve(cmd.deferred_target);
        return cmd.target;
    }

    void store_resolved_(deferred_entity id, entity e) noexcept
    {
        if (id.local_id >= resolved_.size()) [[likely]]
        {
            resolved_.increase_capacity(id.local_id + 1);
            while (resolved_.size() <= id.local_id)
            {
                resolved_.emplace_back(entity{});
            }
        }
        resolved_[id.local_id] = e;
    }

    template <typename T>
    static operating_message apply_add(command_buffer& cb, command& cmd) noexcept
    {
        entity target = cb.resolve_target_(cmd);
        if (!target.is_valid()) [[unlikely]]
        {
            return make_error_(om_err_invalid_entity.code);
        }
        T* p = cmd.payload.template fast_get_ptr<T>();
        if (!p) [[unlikely]] return make_error_(om_err_invalid_argument.code);
        operating_message error;
        (void)cb.mgr_->template add_impl<T>(target, std::move(*p), error);
        return error;
    }

    template <typename T>
    static operating_message apply_remove(command_buffer& cb, command& cmd) noexcept
    {
        entity target = cb.resolve_target_(cmd);
        if (!target.is_valid()) [[unlikely]]
        {
            return make_error_(om_err_invalid_entity.code);
        }
        operating_message error;
        (void)cb.mgr_->template soft_remove_impl<T>(target, error);
        return error;
    }

    static operating_message apply_destroy(command_buffer& cb, command& cmd) noexcept
    {
        entity target = cb.resolve_target_(cmd);
        if (!target.is_valid()) [[unlikely]]
        {
            return make_error_(om_err_invalid_entity.code);
        }
        return cb.mgr_->delete_entity(target);
    }

    static operating_message apply_add_def(command_buffer& cb, command& cmd) noexcept
    {
        entity target = cb.resolve_target_(cmd);
        if (!target.is_valid()) [[unlikely]]
        {
            return make_error_(om_err_invalid_entity.code);
        }
        def_command_payload* p = cmd.payload.template fast_get_ptr<def_command_payload>();
        if (!p) [[unlikely]] return make_error_(om_err_invalid_argument.code);
        operating_message error;
        (void)cb.mgr_->add_def_impl(target, p->def_id, p->bytes.data(), error);
        return error;
    }

    static operating_message apply_create(command_buffer& cb, command& cmd) noexcept
    {
        if (!cb.mgr_) [[unlikely]] return make_error_(om_err_invalid_argument.code);
        entity created = cb.mgr_->create_entity();
        if (!created.is_valid()) [[unlikely]]
        {
            return make_error_(om_err_capacity_exceeded.code);
        }
        cb.store_resolved_(cmd.deferred_target, created);
        return {};
    }

public:
    explicit command_buffer(manager* mgr) noexcept
        : mgr_(mgr)
        , buffer_id_(next_buffer_id_())
    {
    }

    command_buffer(const command_buffer&) = delete;
    command_buffer& operator=(const command_buffer&) = delete;
    command_buffer(command_buffer&&) noexcept = default;
    command_buffer& operator=(command_buffer&&) noexcept = default;

    void preallocate(size_t n) noexcept { commands_.increase_capacity(n); }
    [[nodiscard]] size_t capacity() const noexcept { return commands_.capacity(); }

    [[nodiscard]] entity resolve(deferred_entity id) const noexcept
    {
        if (!id.is_valid() || id.buffer_id != buffer_id_
            || id.local_id >= resolved_.size()) [[unlikely]]
        {
            return entity{};
        }
        const entity e = resolved_[id.local_id];
        if (!e.is_valid() || !mgr_ || !mgr_->is_entity_valid(e)) [[unlikely]]
        {
            return entity{};
        }
        return e;
    }

    deferred_entity create_entity() noexcept
    {
        const uint32_t local_id = ++next_local_id_;
        const deferred_entity id{buffer_id_, local_id};
        command cmd{};
        cmd.deferred_target = id;
        cmd.apply_fn = &apply_create;
        commands_.emplace_back(cmd);
        return id;
    }

    template <typename T>
    void add_component(entity e, T&& comp) noexcept
    {
        commands_.emplace_back(command{
            e,
            deferred_entity{},
            void_any(std::forward<T>(comp)),
            &apply_add<std::decay_t<T>>});
    }

    template <typename T>
    void add_component(deferred_entity e, T&& comp) noexcept
    {
        commands_.emplace_back(command{
            entity{},
            e,
            void_any(std::forward<T>(comp)),
            &apply_add<std::decay_t<T>>});
    }

    template <typename T>
    operating_message add_component_batch(std::span<const entity> entities,
                                          std::span<const T> components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            return make_error_(om_err_invalid_argument.code);
        }
        for (size_t i = 0; i < entities.size(); ++i)
        {
            add_component(entities[i], components[i]);
        }
        return {};
    }

    template <typename T>
    operating_message add_component_batch(std::span<const deferred_entity> entities,
                                          std::span<const T> components) noexcept
    {
        if (entities.size() != components.size()) [[unlikely]]
        {
            return make_error_(om_err_invalid_argument.code);
        }
        for (size_t i = 0; i < entities.size(); ++i)
        {
            add_component(entities[i], components[i]);
        }
        return {};
    }

    template <typename T>
    void remove_component(entity e) noexcept
    {
        commands_.emplace_back(command{
            e,
            deferred_entity{},
            void_any{},
            &apply_remove<T>});
    }

    template <typename T>
    void remove_component(deferred_entity e) noexcept
    {
        commands_.emplace_back(command{
            entity{},
            e,
            void_any{},
            &apply_remove<T>});
    }

    template <typename T>
    operating_message remove_component_batch(std::span<const entity> entities) noexcept
    {
        for (const entity e : entities)
        {
            remove_component<T>(e);
        }
        return {};
    }

    template <typename T>
    operating_message remove_component_batch(std::span<const deferred_entity> entities) noexcept
    {
        for (const deferred_entity e : entities)
        {
            remove_component<T>(e);
        }
        return {};
    }

    operating_message add_def_component(entity e, int def_id, const void* data) noexcept
    {
        const type_def* def = type_id::get_type_def(def_id);
        if (!def || !data) [[unlikely]]
        {
            return make_error_(om_err_not_found.code);
        }
        def_command_payload payload;
        payload.def_id = def_id;
        payload.bytes.append_bulk(static_cast<const uint8_t*>(data), def->size);
        commands_.emplace_back(command{e, deferred_entity{}, void_any(std::move(payload)), &apply_add_def});
        return {};
    }

    operating_message add_def_component(deferred_entity e, int def_id, const void* data) noexcept
    {
        const type_def* def = type_id::get_type_def(def_id);
        if (!def || !data) [[unlikely]]
        {
            return make_error_(om_err_not_found.code);
        }
        def_command_payload payload;
        payload.def_id = def_id;
        payload.bytes.append_bulk(static_cast<const uint8_t*>(data), def->size);
        commands_.emplace_back(command{entity{}, e, void_any(std::move(payload)), &apply_add_def});
        return {};
    }

    operating_message add_def_component(entity e, std::string_view name, const void* data) noexcept
    {
        const int def_id = type_id::get_def_type_id(name);
        if (def_id < 0) [[unlikely]]
        {
            return make_error_(om_err_not_found.code);
        }
        return add_def_component(e, def_id, data);
    }

    operating_message add_def_component(deferred_entity e, std::string_view name, const void* data) noexcept
    {
        const int def_id = type_id::get_def_type_id(name);
        if (def_id < 0) [[unlikely]]
        {
            return make_error_(om_err_not_found.code);
        }
        return add_def_component(e, def_id, data);
    }

    void delete_entity(entity e) noexcept
    {
        commands_.emplace_back(command{
            e,
            deferred_entity{},
            void_any{},
            &apply_destroy});
    }

    void delete_entity(deferred_entity e) noexcept
    {
        commands_.emplace_back(command{
            entity{},
            e,
            void_any{},
            &apply_destroy});
    }

    // 清空未回放命令与占位映射, 旧 deferred_entity 全部失效
    void clear() noexcept
    {
        commands_.clear();
        resolved_.clear();
        next_local_id_ = 0;
        buffer_id_ = next_buffer_id_();
    }

    [[nodiscard]] size_t size() const noexcept { return commands_.size(); }
    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }

    // 按录入顺序回放全部命令, 完成后清空; 返回首个失败
    operating_message flush() noexcept
    {
        operating_message first_error;
        bool has_error = false;
        commands_.for_each([&](command& cmd) noexcept {
            operating_message result = cmd.apply_fn(*this, cmd);
            if (!result && !has_error)
            {
                first_error = result;
                has_error = true;
            }
        });
        commands_.clear();
        return first_error;
    }

    ~command_buffer() = default;
};

} // namespace ecs
