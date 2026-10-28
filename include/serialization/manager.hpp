#pragma once

// manager.hpp - 序列化门面: 对象 / 数组 / 整存档
// 命名空间: serialize
// 可失败接口返回 operating_message, 成功为空对象

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "serialization/archive/full_archive.hpp"
#include "serialization/archive/payload_codec.hpp"

namespace serialize {

class manager
{
public:
    manager() noexcept = default;

    explicit manager(ecs::manager& world) noexcept
        : world_(std::make_unique<full_archive>(world))
    {
    }

    manager(manager&&) noexcept = default;
    manager& operator=(manager&&) noexcept = default;
    manager(const manager&) = delete;
    manager& operator=(const manager&) = delete;
    ~manager() = default;

    // === 配置 ===
    manager& set_format(archive_format fmt) noexcept
    {
        format_ = fmt;
        return *this;
    }

    manager& set_checksum(bool on) noexcept
    {
        objects_.bridge.enable_checksum = on;
        if (world_ != nullptr)
        {
            world_->set_checksum_enabled(on);
        }
        return *this;
    }

    manager& set_compression(compress_fn c, decompress_fn d) noexcept
    {
        objects_.bridge.compress_cb = c;
        objects_.bridge.decompress_cb = d;
        if (world_ != nullptr)
        {
            world_->set_compression(c, d);
        }
        return *this;
    }

    manager& set_encryption(encrypt_fn e, decrypt_fn d) noexcept
    {
        objects_.bridge.encrypt_cb = e;
        objects_.bridge.decrypt_cb = d;
        if (world_ != nullptr)
        {
            world_->set_encryption(e, d);
        }
        return *this;
    }

    manager& set_save_transform(transform_fn cb) noexcept
    {
        objects_.bridge.on_save_cb = cb;
        if (world_ != nullptr)
        {
            world_->on_save(cb);
        }
        return *this;
    }

    manager& set_load_transform(transform_fn cb) noexcept
    {
        objects_.bridge.on_load_cb = cb;
        if (world_ != nullptr)
        {
            world_->on_load(cb);
        }
        return *this;
    }

    // === 单对象 ===
    template<typename T>
    operating_message save_object(std::string& out, const T& obj) noexcept
    {
        return objects_.save_object(out, format_, obj);
    }

    template<typename T>
    operating_message load_object(std::string_view data, T& obj) noexcept
    {
        return objects_.load_object(data, obj);
    }

    template<typename T>
    operating_message save_object_to_file(const std::string& path, const T& obj) noexcept
    {
        return objects_.save_object_to_file(path, format_, obj);
    }

    template<typename T>
    operating_message load_object_from_file(const std::string& path, T& obj) noexcept
    {
        return objects_.load_object_from_file(path, obj);
    }

    // === 对象数组 ===
    template<typename T>
    operating_message save_array(std::string& out, const T* items, size_t count) noexcept
    {
        return objects_.save_array(out, format_, items, count);
    }

    template<typename T>
    operating_message load_array(std::string_view data, dense<T>& out) noexcept
    {
        return objects_.load_array(data, out);
    }

    template<typename T>
    operating_message save_array_to_file(const std::string& path, const T* items, size_t count) noexcept
    {
        return objects_.save_array_to_file(path, format_, items, count);
    }

    template<typename T>
    operating_message load_array_from_file(const std::string& path, dense<T>& out) noexcept
    {
        return objects_.load_array_from_file(path, out);
    }

    // === 整存档 ===
    [[nodiscard]] bool has_world() const noexcept { return world_ != nullptr; }

    operating_message save_world(std::string& out) noexcept
    {
        if (world_ == nullptr)
        {
            return detail_failure();
        }
        return world_->save_to_string(out, format_);
    }

    operating_message save_world_to_file(const std::string& path) noexcept
    {
        if (world_ == nullptr)
        {
            return detail_failure();
        }
        return world_->save_to_file(path, format_);
    }

    operating_message load_world(std::string_view data) noexcept
    {
        if (world_ == nullptr)
        {
            return detail_failure();
        }
        return world_->load_from_string(data);
    }

    operating_message load_world_from_file(const std::string& path) noexcept
    {
        if (world_ == nullptr)
        {
            return detail_failure();
        }
        return world_->load_from_file(path);
    }

    operating_message set_world_metadata(const char* key, const char* value) noexcept
    {
        if (world_ == nullptr)
        {
            return detail_failure();
        }
        world_->set_metadata(key, value);
        return operating_message{};
    }

    // === 查询 ===
    [[nodiscard]] archive_format format() const noexcept { return format_; }

    [[nodiscard]] const serialize_stats* last_stats() const noexcept
    {
        return world_ != nullptr ? &world_->last_stats() : nullptr;
    }

    [[nodiscard]] std::string world_schema_proto() noexcept
    {
        return world_ != nullptr ? world_->schema_proto() : std::string{};
    }

    [[nodiscard]] std::string world_schema_fbs() noexcept
    {
        return world_ != nullptr ? world_->schema_fbs() : std::string{};
    }

    // === 高级配置访问 ===
    [[nodiscard]] full_archive* world() noexcept { return world_.get(); }
    [[nodiscard]] const full_archive* world() const noexcept { return world_.get(); }
    [[nodiscard]] object_archive& objects() noexcept { return objects_; }
    [[nodiscard]] const object_archive& objects() const noexcept { return objects_; }

private:
    [[nodiscard]] static operating_message detail_failure() noexcept
    {
        operating_message fallback;
        fallback.write_failure(om_err_not_running.code, msg_level::warn);
        return fallback;
    }

    archive_format format_{archive_format::json};
    object_archive objects_;
    std::unique_ptr<full_archive> world_;
};

} // namespace serialize
