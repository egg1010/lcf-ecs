// string_to_code.hpp - 字符串到数字码的可逆无冲突编码
// 字节级编码, 支持任意字符; 统一以 utf8_view 为输入输出接口
// (utf8_view 可从 const char*/string_view/string 隐式构造)
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string_view>
#include "force_inline.hpp"
#include "utf8pp/utf8_view.hpp"
#include "memory/layered_allocator.hpp"

namespace string_to_code {

// 线程局部分层分配器
[[nodiscard]] inline memory::layered_allocator& allocator() noexcept
{
    thread_local memory::layered_allocator alloc;
    return alloc;
}

// 数字码: 短串(<=8字节)内联 uint64_t, 长串(>8字节)用 uint64_t 数组
class code_value
{
public:
    FORCE_INLINE code_value() noexcept
        : inline_value_(0)
        , heap_array_(nullptr)
        , string_length_(0)
    {
    }

    // 接受 utf8_view (可从 const char*/string_view/string 隐式构造)
    FORCE_INLINE explicit code_value(const utf8_view& s) noexcept
        : inline_value_(0)
        , heap_array_(nullptr)
        , string_length_(s.byte_size())
    {
        size_t n = string_length_;
        if (n == 0)
        {
            return;
        }

        if (n <= 8)
        {
            // 短串: inline_value_ 即完整数据
            std::memcpy(&inline_value_, s.data(), n);
        }
        else
        {
            // 长串: 堆分配, 缓存前8字节供快速失败
            size_t array_length = (n + 7) / 8;
            size_t byte_size = array_length * sizeof(uint64_t);
            heap_array_ = static_cast<uint64_t*>(allocator().allocate(byte_size));
            if (!heap_array_) [[unlikely]]
            {
                std::abort();
            }
            std::memcpy(heap_array_, s.data(), n);
            // 清零尾块未填充部分, 确保 equals 可直接 memcmp
            size_t tail = byte_size - n;
            if (tail != 0)
            {
                std::memset(reinterpret_cast<char*>(heap_array_) + n, 0, tail);
            }
            std::memcpy(&inline_value_, s.data(), 8);
        }
    }

    FORCE_INLINE code_value(code_value&& other) noexcept
        : inline_value_(other.inline_value_)
        , heap_array_(other.heap_array_)
        , string_length_(other.string_length_)
    {
        other.inline_value_ = 0;
        other.heap_array_ = nullptr;
        other.string_length_ = 0;
    }

    FORCE_INLINE code_value& operator=(code_value&& other) noexcept
    {
        if (this != &other)
        {
            if (heap_array_)
            {
                allocator().deallocate(heap_array_, allocator_byte_size_());
            }
            inline_value_ = other.inline_value_;
            heap_array_ = other.heap_array_;
            string_length_ = other.string_length_;
            other.inline_value_ = 0;
            other.heap_array_ = nullptr;
            other.string_length_ = 0;
        }
        return *this;
    }

    code_value(const code_value&) = delete;
    code_value& operator=(const code_value&) = delete;

    FORCE_INLINE ~code_value() noexcept
    {
        if (heap_array_)
        {
            allocator().deallocate(heap_array_, allocator_byte_size_());
        }
    }

    // 返回的 utf8_view 不持有内存, 调用方需保证 code_value 生命周期
    [[nodiscard]] FORCE_INLINE utf8_view decode() const noexcept
    {
        if (string_length_ == 0)
        {
            return {};
        }
        const char* data = heap_array_
            ? reinterpret_cast<const char*>(heap_array_)
            : reinterpret_cast<const char*>(&inline_value_);
        return utf8_view(data, string_length_);
    }

    [[nodiscard]] FORCE_INLINE bool is_inline() const noexcept
    {
        return heap_array_ == nullptr;
    }

    // 仅 is_inline()=true 时有效, 用于 map key 直接比较
    [[nodiscard]] FORCE_INLINE uint64_t inline_value() const noexcept
    {
        return inline_value_;
    }

    [[nodiscard]] FORCE_INLINE size_t string_size() const noexcept
    {
        return string_length_;
    }

    [[nodiscard]] FORCE_INLINE bool empty() const noexcept
    {
        return string_length_ == 0;
    }

    // 模板编码: N 为编译期常量, memcpy 长度成为模板参数, GCC 必内联
    // 陷阱: N 非2的幂(如6)时 GCC 用栈拼接(store-to-load forwarding stall);
    //   N>=5 时拆分 lo(4B) + hi(N-4 B) 两次定长 memcpy, 寄存器拼接无栈操作
    template <size_t N>
    [[nodiscard]] FORCE_INLINE static uint64_t encode_inline_n(const char* data) noexcept
    {
        static_assert(N <= 8, "encode_inline_n 仅用于短串(<=8B)");
        if constexpr (N == 0)
        {
            return 0;
        }
        else if constexpr (N <= 4)
        {
            uint64_t v = 0;
            std::memcpy(&v, data, N);
            return v;
        }
        else
        {
            uint32_t lo = 0;
            std::memcpy(&lo, data, 4);
            using hi_t = std::conditional_t<(N - 4) <= 2, uint16_t, uint32_t>;
            hi_t hi = 0;
            std::memcpy(&hi, data + 4, N - 4);
            return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        }
    }

    // 轻量编码 (短串专用): 返回 inline 值, 不构造对象
    // n 必须 <= 8
    [[nodiscard]] FORCE_INLINE static uint64_t encode_inline(const char* data, size_t n) noexcept
    {
        switch (n)
        {
            case 1: return encode_inline_n<1>(data);
            case 2: return encode_inline_n<2>(data);
            case 3: return encode_inline_n<3>(data);
            case 4: return encode_inline_n<4>(data);
            case 5: return encode_inline_n<5>(data);
            case 6: return encode_inline_n<6>(data);
            case 7: return encode_inline_n<7>(data);
            case 8: return encode_inline_n<8>(data);
            default: break;
        }
        uint64_t v = 0;
        std::memcpy(&v, data, n);
        return v;
    }

    // 编码并比较: 避免构造中间 code_value, 直接比较字符串与 this
    [[nodiscard]] FORCE_INLINE bool encode_equals(const char* data, size_t n) const noexcept
    {
        if (n != string_length_)
        {
            return false;
        }
        if (n == 0)
        {
            return true;
        }
        if (n <= 8)
        {
            return encode_inline(data, n) == inline_value_;
        }
        // 长串: 先比较前8字节缓存, 不同则快速失败; 相同再 memcmp 余下
        uint64_t prefix = 0;
        std::memcpy(&prefix, data, 8);
        if (prefix != inline_value_)
        {
            return false;
        }
        return std::memcmp(
            reinterpret_cast<const char*>(heap_array_) + 8,
            data + 8,
            n - 8) == 0;
    }

    // 无冲突比较: 短串比较 inline_value_, 长串先比较前8字节再 memcmp 余下
    [[nodiscard]] FORCE_INLINE bool equals(const code_value& other) const noexcept
    {
        if (string_length_ != other.string_length_)
        {
            return false;
        }
        if (string_length_ == 0)
        {
            return true;
        }
        if (!heap_array_)
        {
            return inline_value_ == other.inline_value_;
        }
        if (inline_value_ != other.inline_value_)
        {
            return false;
        }
        return std::memcmp(
            reinterpret_cast<const char*>(heap_array_) + 8,
            reinterpret_cast<const char*>(other.heap_array_) + 8,
            string_length_ - 8) == 0;
    }

    // 严格比较: 与 equals 语义相同 (均已无碰撞), 保留供语义明确场景使用
    [[nodiscard]] FORCE_INLINE bool equals_strict(const code_value& other) const noexcept
    {
        return equals(other);
    }

private:
    // 计算 heap_array_ 占用的字节数 (用于 deallocate 大小提示)
    [[nodiscard]] FORCE_INLINE size_t allocator_byte_size_() const noexcept
    {
        return (string_length_ + 7) & ~static_cast<size_t>(7);
    }

    uint64_t inline_value_;   // 短串: 数据(<=8B); 长串: 前8字节缓存
    uint64_t* heap_array_;    // nullptr=短串, 非空=长串
    size_t string_length_;    // 字符串字节长度
};

// 统一入口: 接受 utf8_view (可从 const char*/string_view/string 隐式构造)
[[nodiscard]] FORCE_INLINE code_value encode(const utf8_view& s) noexcept
{
    return code_value(s);
}

[[nodiscard]] FORCE_INLINE bool equals(const code_value& a, const code_value& b) noexcept
{
    return a.equals(b);
}

} // namespace string_to_code
