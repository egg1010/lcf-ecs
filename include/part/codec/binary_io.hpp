// binary_io.hpp - 原生二进制读写器（合并自 binary_writer + binary_reader）
#pragma once

#include "../safety.hpp"
#include <cstdint>
#include <cstring>
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

class binary_reader
{
    const char* p_;
    const char* end_;
    bool err_ = false;

public:
    binary_reader(std::string_view data) noexcept
        : p_(data.data()), end_(data.data() + data.size()) {
        if (data.size() < 8)
        {
            err_ = true;
            return;
        }
        if (std::memcmp(p_, "LCE1", 4) != 0)
        {
            err_ = true;
            return;
        }
        p_ += 4;
        // endianness (仅支持 LE=1)
        if (static_cast<unsigned char>(*p_) != 1)
        {
            err_ = true;
            return;
        }
        p_ += 1;
        // version
        p_ += 1;
        // reserved
        p_ += 2;
    }

    [[nodiscard]] bool has_error() const noexcept { return err_; }

    binary_reader& skip(size_t n) noexcept {
        if (p_ + n > end_)
        {
            err_ = true;
            return *this;
        }
        p_ += n;
        return *this;
    }

    [[nodiscard]] uint32_t read_u32() noexcept {
        if (p_ + 4 > end_)
        {
            err_ = true;
            return 0;
        }
        uint32_t v = detail::read_le<uint32_t>(p_);
        p_ += 4;
        return v;
    }

    [[nodiscard]] uint64_t read_u64() noexcept {
        if (p_ + 8 > end_)
        {
            err_ = true;
            return 0;
        }
        uint64_t v = detail::read_le<uint64_t>(p_);
        p_ += 8;
        return v;
    }

    [[nodiscard]] int32_t read_i32() noexcept {
        return static_cast<int32_t>(read_u32());
    }

    [[nodiscard]] int64_t read_i64() noexcept {
        return static_cast<int64_t>(read_u64());
    }

    [[nodiscard]] float read_f32() noexcept {
        if (p_ + 4 > end_)
        {
            err_ = true;
            return 0.0f;
        }
        float v;
        uint32_t raw = detail::read_le<uint32_t>(p_);
        std::memcpy(&v, &raw, sizeof(float));
        p_ += 4;
        return v;
    }

    [[nodiscard]] double read_f64() noexcept {
        if (p_ + 8 > end_)
        {
            err_ = true;
            return 0.0;
        }
        double v;
        uint64_t raw = detail::read_le<uint64_t>(p_);
        std::memcpy(&v, &raw, sizeof(double));
        p_ += 8;
        return v;
    }

    [[nodiscard]] bool read_bool() noexcept {
        if (p_ + 1 > end_)
        {
            err_ = true;
            return false;
        }
        bool v = (*p_ != 0);
        p_ += 1;
        return v;
    }

    [[nodiscard]] std::string_view read_string_view() noexcept {
        uint32_t len = read_u32();
        if (err_ || p_ + len > end_)
        {
            err_ = true;
            return {};
        }
        std::string_view s(p_, len);
        p_ += len;
        return s;
    }

    [[nodiscard]] std::string read_string() noexcept {
        return std::string(read_string_view());
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    void read_trivial(T& out) noexcept {
        if (p_ + sizeof(T) > end_)
        {
            err_ = true;
            return;
        }
        out = detail::read_le<T>(p_);
        p_ += sizeof(T);
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] T read_trivial() noexcept {
        T v{};
        read_trivial(v);
        return v;
    }

    [[nodiscard]] size_t remaining() const noexcept {
        return static_cast<size_t>(end_ - p_);
    }

    // 零拷贝窥视: 返回当前读取位置指针, 不前进
    [[nodiscard]] const char* peek() const noexcept { return p_; }
};
