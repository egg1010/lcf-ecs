// binary_writer.hpp - 原生二进制写入器
#pragma once

#include "../safety.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

class binary_writer {
    std::string buf_;

    static constexpr char magic[4] = {'L', 'C', 'E', '1'};
public:
    explicit binary_writer(size_t reserve = 65536) noexcept {
        buf_.reserve(reserve);
        buf_.append(magic, 4);
        buf_.push_back(static_cast<char>(1));
        buf_.push_back(static_cast<char>(1));
        buf_.push_back('\0'); buf_.push_back('\0');
    }

    binary_writer& begin_object() noexcept { return *this; }
    binary_writer& end_object() noexcept { return *this; }
    binary_writer& begin_array(size_t count) noexcept {
        detail::write_le(static_cast<uint32_t>(count), append(4));
        return *this;
    }
    binary_writer& end_array() noexcept { return *this; }
    binary_writer& key(std::string_view k) noexcept {
        write_string(k);
        return *this;
    }

    binary_writer& value(int32_t v) noexcept  { detail::write_le(v, append(4)); return *this; }
    binary_writer& value(uint32_t v) noexcept { detail::write_le(v, append(4)); return *this; }
    binary_writer& value(int64_t v) noexcept  { detail::write_le(v, append(8)); return *this; }
    binary_writer& value(uint64_t v) noexcept{ detail::write_le(v, append(8)); return *this; }
    binary_writer& value(float v) noexcept    { detail::write_le(v, append(4)); return *this; }
    binary_writer& value(double v) noexcept  { detail::write_le(v, append(8)); return *this; }
    binary_writer& value(bool v) noexcept    { buf_.push_back(v ? 1 : 0); return *this; }
    binary_writer& value(std::string_view v) noexcept { write_string(v); return *this; }
    binary_writer& value(const char* v) noexcept { write_string(v); return *this; }
    binary_writer& value(const std::string& v) noexcept { write_string(v); return *this; }
    binary_writer& null() noexcept { return *this; }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    binary_writer& value_trivial(const T& v) noexcept {
        detail::write_le(v, append(sizeof(T)));
        return *this;
    }

    [[nodiscard]] const std::string& data() const noexcept { return buf_; }
    std::string take() noexcept { return std::move(buf_); }

    // 写入原始字节 (无长度前缀)
    void write_raw_bytes(const void* data, size_t len) noexcept {
        buf_.append(static_cast<const char*>(data), len);
    }

private:
    char* append(size_t n) noexcept {
        size_t old = buf_.size();
        buf_.resize(old + n);
        return buf_.data() + old;
    }
    void write_string(std::string_view s) noexcept {
        detail::write_le(static_cast<uint32_t>(s.size()), append(4));
        buf_.append(s.data(), s.size());
    }
};
