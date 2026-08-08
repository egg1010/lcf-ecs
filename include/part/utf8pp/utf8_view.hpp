#pragma once

// 字符串视图: 非拥有, 字节级 O(1), 码点级 O(n), 不分配内存

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <compare>
#include <string_view>
#include <string>
#include <ostream>
#include "../force_inline.hpp"
#include "utf8_codec.hpp"
#include "unicode_data.hpp"

class utf8_view
{
public:
    static constexpr size_t npos = static_cast<size_t>(-1);

    class const_iterator
    {
    public:
        using value_type = char32_t;
        using reference = char32_t;
        using pointer = const char32_t*;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        const_iterator() noexcept = default;
        const_iterator(const char* p, const char* end) noexcept : p_(p), end_(end) {}

        const_iterator& operator++() noexcept
        {
            p_ = reinterpret_cast<const char*>(
                detail_utf8::advance_codepoint(
                    reinterpret_cast<const uint8_t*>(p_),
                    reinterpret_cast<const uint8_t*>(end_)));
            return *this;
        }

        const_iterator operator++(int) noexcept
        {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        [[nodiscard]] char32_t operator*() const noexcept
        {
            // 无校验: 构造/递增保证 p_ 已对齐到码点边界
            return detail_utf8::utf8_decode_unchecked(reinterpret_cast<const uint8_t*>(p_));
        }

        [[nodiscard]] bool operator==(const const_iterator& o) const noexcept { return p_ == o.p_; }
        [[nodiscard]] bool operator!=(const const_iterator& o) const noexcept { return p_ != o.p_; }

        const char* ptr() const noexcept { return p_; }

    private:
        friend class utf8_view;
        const char* p_{nullptr};
        const char* end_{nullptr};
    };

    class const_reverse_iterator
    {
    public:
        using value_type = char32_t;
        using reference = char32_t;
        using pointer = const char32_t*;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;

        const_reverse_iterator() noexcept = default;
        const_reverse_iterator(const char* p, const char* begin, const char* end) noexcept
            : p_(p), begin_(begin), end_(end) {}

        const_reverse_iterator& operator++() noexcept
        {
            if (p_ > begin_)
            {
                p_ = reinterpret_cast<const char*>(
                    detail_utf8::retreat_codepoint(
                        reinterpret_cast<const uint8_t*>(begin_),
                        reinterpret_cast<const uint8_t*>(p_)));
            }
            return *this;
        }

        const_reverse_iterator operator++(int) noexcept
        {
            const_reverse_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        const_reverse_iterator& operator--() noexcept
        {
            if (p_ < end_)
            {
                p_ = reinterpret_cast<const char*>(
                    detail_utf8::advance_codepoint(
                        reinterpret_cast<const uint8_t*>(p_),
                        reinterpret_cast<const uint8_t*>(end_)));
            }
            return *this;
        }

        const_reverse_iterator operator--(int) noexcept
        {
            const_reverse_iterator tmp = *this;
            --(*this);
            return tmp;
        }

        [[nodiscard]] char32_t operator*() const noexcept
        {
            // 反向回退到码点起始后无校验解码
            const uint8_t* q = reinterpret_cast<const uint8_t*>(p_);
            const uint8_t* b = reinterpret_cast<const uint8_t*>(begin_);
            --q;
            while (q > b && (*q & 0xC0) == 0x80) --q;
            return detail_utf8::utf8_decode_unchecked(q);
        }

        [[nodiscard]] bool operator==(const const_reverse_iterator& o) const noexcept { return p_ == o.p_; }
        [[nodiscard]] bool operator!=(const const_reverse_iterator& o) const noexcept { return p_ != o.p_; }

    private:
        friend class utf8_view;
        const char* p_{nullptr};
        const char* begin_{nullptr};
        const char* end_{nullptr};
    };

    using iterator = const_iterator;
    using reverse_iterator = const_reverse_iterator;

    // === 构造 ===
    constexpr utf8_view() noexcept = default;
    constexpr utf8_view(const char* s, size_t byte_len) noexcept : data_(s), byte_size_(byte_len) {}
    constexpr utf8_view(const char* s) noexcept : data_(s), byte_size_(s ? std::strlen(s) : 0) {}
    utf8_view(const char8_t* s) noexcept
        : data_(reinterpret_cast<const char*>(s)), byte_size_(s ? std::strlen(reinterpret_cast<const char*>(s)) : 0) {}
    utf8_view(const char8_t* s, size_t byte_len) noexcept
        : data_(reinterpret_cast<const char*>(s)), byte_size_(byte_len) {}
    constexpr utf8_view(std::string_view sv) noexcept : data_(sv.data()), byte_size_(sv.size()) {}
    utf8_view(std::u8string_view sv) noexcept
        : data_(reinterpret_cast<const char*>(sv.data())), byte_size_(sv.size()) {}
    utf8_view(const std::string& s) noexcept : data_(s.data()), byte_size_(s.size()) {}
    utf8_view(const std::u8string& s) noexcept
        : data_(reinterpret_cast<const char*>(s.data())), byte_size_(s.size()) {}

    // 注: utf8pp 转换在 utf8pp.hpp 隐式构造提供, 避免循环依赖

    // === 赋值 ===
    constexpr utf8_view& operator=(const char* s) noexcept
    {
        data_ = s;
        byte_size_ = s ? std::strlen(s) : 0;
        return *this;
    }
    constexpr utf8_view& operator=(std::string_view sv) noexcept
    {
        data_ = sv.data();
        byte_size_ = sv.size();
        return *this;
    }

    // === 容量 (字节级 O(1)) ===
    [[nodiscard]] constexpr size_t byte_size() const noexcept { return byte_size_; }
    [[nodiscard]] constexpr size_t size_bytes() const noexcept { return byte_size_; }
    [[nodiscard]] constexpr size_t length_bytes() const noexcept { return byte_size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return byte_size_ == 0; }
    [[nodiscard]] constexpr size_t max_size() const noexcept { return static_cast<size_t>(-1); }

    // === 容量 (码点级 O(n)) ===
    [[nodiscard]] size_t size() const noexcept
    {
        return detail_utf8::count_codepoints(
            reinterpret_cast<const uint8_t*>(data_),
            reinterpret_cast<const uint8_t*>(data_) + byte_size_);
    }
    [[nodiscard]] size_t length() const noexcept { return size(); }

    // === 数据访问 (字节级 O(1)) ===
    // 注: data()/c_str() 缓冲区不保证 '\0' 结尾 (同 string_view 语义), 传 C API 前需确认
    [[nodiscard]] constexpr const char* data() const noexcept { return data_; }
    // 空视图返回 "", 仅保证非 null 契约
    [[nodiscard]] constexpr const char* c_str() const noexcept { return data_ ? data_ : ""; }
    [[nodiscard]] constexpr std::string_view byte_view() const noexcept { return std::string_view(data_, byte_size_); }
    [[nodiscard]] constexpr operator std::string_view() const noexcept { return byte_view(); }

    // === 码点访问 (O(n), 需要遍历) ===
    [[nodiscard]] char32_t at(size_t cp_idx) const noexcept
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        p = detail_utf8::advance_codepoints(p, end, cp_idx);
        if (p >= end) return U'\uFFFD';
        uint32_t cp = 0;
        size_t len = 0;
        (void)detail_utf8::utf8_decode_one(p, end, &cp, &len);
        return static_cast<char32_t>(cp);
    }

    [[nodiscard]] char32_t operator[](size_t cp_idx) const noexcept { return at(cp_idx); }
    [[nodiscard]] char32_t front() const noexcept { return at(0); }
    [[nodiscard]] char32_t back() const noexcept
    {
        if (byte_size_ == 0) return U'\uFFFD';
        const uint8_t* begin = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* p = begin + byte_size_;
        p = detail_utf8::retreat_codepoint(begin, p);
        uint32_t cp = 0;
        size_t len = 0;
        (void)detail_utf8::utf8_decode_one(p, begin + byte_size_, &cp, &len);
        return static_cast<char32_t>(cp);
    }

    // === 字节访问 (O(1)) ===
    [[nodiscard]] constexpr char byte_at(size_t i) const noexcept
    {
        return i < byte_size_ ? data_[i] : '\0';
    }

    // === 迭代器 (码点级) ===
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(data_, data_ + byte_size_); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(data_ + byte_size_, data_ + byte_size_); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    [[nodiscard]] const_reverse_iterator rbegin() const noexcept
    {
        return const_reverse_iterator(data_ + byte_size_, data_, data_ + byte_size_);
    }
    [[nodiscard]] const_reverse_iterator rend() const noexcept
    {
        return const_reverse_iterator(data_, data_, data_ + byte_size_);
    }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return rend(); }

    // === 迭代器 (字节级, O(1) 随机访问) ===
    using const_byte_iterator = const char*;
    using byte_iterator = const_byte_iterator;

    [[nodiscard]] const_byte_iterator byte_begin() const noexcept { return data_; }
    [[nodiscard]] const_byte_iterator byte_end() const noexcept { return data_ + byte_size_; }
    [[nodiscard]] const_byte_iterator byte_cbegin() const noexcept { return byte_begin(); }
    [[nodiscard]] const_byte_iterator byte_cend() const noexcept { return byte_end(); }
    [[nodiscard]] const_byte_iterator rbyte_begin() const noexcept { return data_ + byte_size_; }
    [[nodiscard]] const_byte_iterator rbyte_end() const noexcept { return data_; }
    [[nodiscard]] const_byte_iterator byte_crbegin() const noexcept { return rbyte_begin(); }
    [[nodiscard]] const_byte_iterator byte_crend() const noexcept { return rbyte_end(); }

    // === 子串 (字节级 O(1)) ===
    [[nodiscard]] constexpr utf8_view substr_bytes(size_t byte_pos, size_t byte_len = npos) const noexcept
    {
        if (byte_pos >= byte_size_) return utf8_view();
        if (byte_len > byte_size_ - byte_pos) byte_len = byte_size_ - byte_pos;
        return utf8_view(data_ + byte_pos, byte_len);
    }

    // === 子串 (码点级 O(n)) ===
    [[nodiscard]] utf8_view substr(size_t cp_pos, size_t cp_count = npos) const noexcept
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        p = detail_utf8::advance_codepoints(p, end, cp_pos);
        if (p >= end) return utf8_view();
        const char* start = reinterpret_cast<const char*>(p);
        p = detail_utf8::advance_codepoints(p, end, cp_count);
        return utf8_view(start, static_cast<size_t>(reinterpret_cast<const char*>(p) - start));
    }

    // === remove_prefix/suffix (字节级, STL 语义) ===
    constexpr void remove_prefix(size_t byte_n) noexcept
    {
        if (byte_n > byte_size_) byte_n = byte_size_;
        data_ += byte_n;
        byte_size_ -= byte_n;
    }
    constexpr void remove_suffix(size_t byte_n) noexcept
    {
        if (byte_n > byte_size_) byte_n = byte_size_;
        byte_size_ -= byte_n;
    }

    // === copy (字节级) ===
    size_t copy(char* buf, size_t byte_n, size_t byte_pos = 0) const noexcept
    {
        if (byte_pos >= byte_size_ || !buf) return 0;
        if (byte_n > byte_size_ - byte_pos) byte_n = byte_size_ - byte_pos;
        std::memcpy(buf, data_ + byte_pos, byte_n);
        return byte_n;
    }

    // === 比较 (字节级 memcmp, 与 std::string_view 一致) ===
    [[nodiscard]] int compare(const utf8_view& other) const noexcept
    {
        size_t mn = byte_size_ < other.byte_size_ ? byte_size_ : other.byte_size_;
        int c = mn ? std::memcmp(data_, other.data_, mn) : 0;
        if (c != 0) return c;
        if (byte_size_ != other.byte_size_) return byte_size_ < other.byte_size_ ? -1 : 1;
        return 0;
    }
    [[nodiscard]] int compare(std::string_view other) const noexcept
    {
        size_t mn = byte_size_ < other.size() ? byte_size_ : other.size();
        int c = mn ? std::memcmp(data_, other.data(), mn) : 0;
        if (c != 0) return c;
        if (byte_size_ != other.size()) return byte_size_ < other.size() ? -1 : 1;
        return 0;
    }
    [[nodiscard]] int compare(const char* s) const noexcept
    {
        return compare(utf8_view(s));
    }

    // === 字节查找 (O(n), 高效) ===
    [[nodiscard]] size_t find_byte(char c, size_t byte_pos = 0) const noexcept
    {
        if (byte_pos >= byte_size_) return npos;
        const void* found = std::memchr(data_ + byte_pos, static_cast<unsigned char>(c), byte_size_ - byte_pos);
        return found ? static_cast<size_t>(static_cast<const char*>(found) - data_) : npos;
    }
    [[nodiscard]] size_t rfind_byte(char c, size_t byte_pos = npos) const noexcept
    {
        if (byte_size_ == 0) return npos;
        if (byte_pos >= byte_size_) byte_pos = byte_size_ - 1;
        for (size_t i = byte_pos + 1; i > 0; --i)
        {
            if (data_[i - 1] == c) return i - 1;
        }
        return npos;
    }

    // === 字节子串查找 ===
    [[nodiscard]] size_t find_bytes(std::string_view str, size_t byte_pos = 0) const noexcept
    {
        if (str.empty()) return byte_pos <= byte_size_ ? byte_pos : npos;
        if (str.size() > byte_size_ || byte_pos > byte_size_ - str.size()) return npos;
        for (size_t i = byte_pos; i <= byte_size_ - str.size(); ++i)
        {
            if (std::memcmp(data_ + i, str.data(), str.size()) == 0) return i;
        }
        return npos;
    }
    [[nodiscard]] size_t rfind_bytes(std::string_view str, size_t byte_pos = npos) const noexcept
    {
        if (str.empty()) return byte_pos <= byte_size_ ? byte_pos : byte_size_;
        if (str.size() > byte_size_) return npos;
        if (byte_pos > byte_size_ - str.size()) byte_pos = byte_size_ - str.size();
        size_t i = byte_pos + 1;
        while (i > 0)
        {
            --i;
            if (std::memcmp(data_ + i, str.data(), str.size()) == 0) return i;
        }
        return npos;
    }

    // === 码点查找 (O(n), ASCII 用 memchr 快速路径) ===
    [[nodiscard]] size_t find(char32_t cp, size_t cp_pos = 0) const noexcept
    {
        // 纯 ASCII 快速路径: 直接 memchr
        if (static_cast<uint32_t>(cp) < 0x80) [[likely]]
        {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
            const uint8_t* end = p + byte_size_;
            p = detail_utf8::advance_codepoints(p, end, cp_pos);
            if (p >= end) return npos;
            size_t byte_off = static_cast<size_t>(p - reinterpret_cast<const uint8_t*>(data_));
            size_t found = find_byte(static_cast<char>(cp), byte_off);
            if (found == npos) return npos;
            // 字节位置 → 码点索引 (SWAR 计数)
            return detail_utf8::count_codepoints(
                reinterpret_cast<const uint8_t*>(data_),
                reinterpret_cast<const uint8_t*>(data_) + found);
        }
        // 多字节: 预编码 cp 后字节级搜索, memchr 加速首字节
        // 自同步保证: lead 字节 (0xC0-0xFF) 不匹配 continuation, 字节级匹配起始必为码点边界
        uint8_t enc[4];
        size_t enc_len = 0;
        if (!detail_utf8::utf8_encode_one(static_cast<uint32_t>(cp), enc, &enc_len))
        {
            (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &enc_len);
        }
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = base + byte_size_;
        const uint8_t* p = detail_utf8::advance_codepoints(base, end, cp_pos);
        size_t start_byte = static_cast<size_t>(p - base);
        // 首字节 memchr + 完整序列 memcmp 验证
        while (start_byte + enc_len <= byte_size_)
        {
            const void* found = std::memchr(data_ + start_byte, static_cast<int>(enc[0]),
                                             byte_size_ - start_byte);
            if (!found) return npos;
            size_t found_byte = static_cast<size_t>(static_cast<const char*>(found) - data_);
            if (found_byte + enc_len > byte_size_) return npos;
            if (std::memcmp(data_ + found_byte, enc, enc_len) == 0)
            {
                return detail_utf8::count_codepoints(base, base + found_byte);
            }
            start_byte = found_byte + 1;
        }
        return npos;
    }
    [[nodiscard]] size_t find(const utf8_view& str, size_t cp_pos = 0) const noexcept
    {
        if (str.byte_size_ == 0) return cp_pos <= size() ? cp_pos : npos;
        if (str.byte_size_ > byte_size_) return npos;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        for (size_t i = 0; i < cp_pos && p < end; ++i)
        {
            p = detail_utf8::advance_codepoint(p, end);
        }
        size_t idx = cp_pos;
        while (p + str.byte_size_ <= end)
        {
            if (std::memcmp(p, str.data_, str.byte_size_) == 0) return idx;
            p = detail_utf8::advance_codepoint(p, end);
            ++idx;
        }
        return npos;
    }
    [[nodiscard]] size_t rfind(char32_t cp, size_t cp_pos = npos) const noexcept
    {
        // 反向字节遍历, 避免 O(n²)
        if (byte_size_ == 0) return npos;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        // 先正向遍历到 cp_pos, 记录位置; 再反向查找
        size_t total = 0;
        const uint8_t* it = p;
        while (it < end)
        {
            if (total == cp_pos) break;
            it = detail_utf8::advance_codepoint(it, end);
            ++total;
        }
        // 反向遍历 (从 it 回退到 p)
        size_t idx = total;
        const uint8_t* cur = it;
        while (cur > p)
        {
            const uint8_t* prev = detail_utf8::retreat_codepoint(p, cur);
            --idx;
            uint32_t c = 0;
            size_t len = 0;
            if (detail_utf8::utf8_decode_one(prev, cur, &c, &len) && c == cp) return idx;
            cur = prev;
        }
        return npos;
    }
    [[nodiscard]] size_t rfind(const utf8_view& str, size_t cp_pos = npos) const noexcept
    {
        size_t total = size();
        if (str.byte_size_ == 0) return cp_pos < total ? cp_pos : total;
        size_t str_cp = str.size();
        if (str_cp > total) return npos;
        if (cp_pos > total - str_cp) cp_pos = total - str_cp;
        size_t i = cp_pos + 1;
        while (i > 0)
        {
            --i;
            utf8_view candidate = substr(i, str_cp);
            if (candidate.byte_size_ == str.byte_size_ &&
                std::memcmp(candidate.data_, str.data_, str.byte_size_) == 0)
            {
                return i;
            }
        }
        return npos;
    }

    // === find_first_of 系列 (码点级) ===
    [[nodiscard]] size_t find_first_of(char32_t cp, size_t cp_pos = 0) const noexcept
    {
        return find(cp, cp_pos);
    }
    [[nodiscard]] size_t find_first_of(const utf8_view& str, size_t cp_pos = 0) const noexcept
    {
        size_t idx = 0;
        for (char32_t cur : *this)
        {
            if (idx >= cp_pos)
            {
                for (char32_t c : str) if (c == cur) return idx;
            }
            ++idx;
        }
        return npos;
    }
    [[nodiscard]] size_t find_last_of(char32_t cp, size_t cp_pos = npos) const noexcept
    {
        return rfind(cp, cp_pos);
    }
    [[nodiscard]] size_t find_last_of(const utf8_view& str, size_t cp_pos = npos) const noexcept
    {
        size_t total = size();
        if (total == 0) return npos;
        if (cp_pos >= total) cp_pos = total - 1;
        size_t i = cp_pos + 1;
        while (i > 0)
        {
            --i;
            char32_t cur = at(i);
            for (char32_t c : str) if (c == cur) return i;
        }
        return npos;
    }
    [[nodiscard]] size_t find_first_not_of(char32_t cp, size_t cp_pos = 0) const noexcept
    {
        size_t idx = 0;
        for (char32_t cur : *this)
        {
            if (idx >= cp_pos && cur != cp) return idx;
            ++idx;
        }
        return npos;
    }
    [[nodiscard]] size_t find_first_not_of(const utf8_view& str, size_t cp_pos = 0) const noexcept
    {
        size_t idx = 0;
        for (char32_t cur : *this)
        {
            if (idx >= cp_pos)
            {
                bool found = false;
                for (char32_t c : str) if (c == cur) { found = true; break; }
                if (!found) return idx;
            }
            ++idx;
        }
        return npos;
    }
    [[nodiscard]] size_t find_last_not_of(char32_t cp, size_t cp_pos = npos) const noexcept
    {
        size_t total = size();
        if (total == 0) return npos;
        if (cp_pos >= total) cp_pos = total - 1;
        size_t i = cp_pos + 1;
        while (i > 0)
        {
            --i;
            if (at(i) != cp) return i;
        }
        return npos;
    }
    [[nodiscard]] size_t find_last_not_of(const utf8_view& str, size_t cp_pos = npos) const noexcept
    {
        size_t total = size();
        if (total == 0) return npos;
        if (cp_pos >= total) cp_pos = total - 1;
        size_t i = cp_pos + 1;
        while (i > 0)
        {
            --i;
            char32_t cur = at(i);
            bool found = false;
            for (char32_t c : str) if (c == cur) { found = true; break; }
            if (!found) return i;
        }
        return npos;
    }

    // === contains / starts_with / ends_with (码点级) ===
    [[nodiscard]] bool contains(char32_t cp) const noexcept { return find(cp) != npos; }
    [[nodiscard]] bool contains(const utf8_view& str) const noexcept { return find(str) != npos; }
    [[nodiscard]] bool starts_with(char32_t cp) const noexcept
    {
        if (byte_size_ == 0) return false;
        return front() == cp;
    }
    [[nodiscard]] bool starts_with(const utf8_view& prefix) const noexcept
    {
        if (prefix.byte_size_ > byte_size_) return false;
        return std::memcmp(data_, prefix.data_, prefix.byte_size_) == 0;
    }
    [[nodiscard]] bool ends_with(char32_t cp) const noexcept
    {
        if (byte_size_ == 0) return false;
        return back() == cp;
    }
    [[nodiscard]] bool ends_with(const utf8_view& suffix) const noexcept
    {
        if (suffix.byte_size_ > byte_size_) return false;
        return std::memcmp(data_ + byte_size_ - suffix.byte_size_, suffix.data_, suffix.byte_size_) == 0;
    }

    // === 比较运算符 ===
    [[nodiscard]] bool operator==(const utf8_view& other) const noexcept { return compare(other) == 0; }
    [[nodiscard]] bool operator!=(const utf8_view& other) const noexcept { return compare(other) != 0; }
    [[nodiscard]] bool operator<(const utf8_view& other) const noexcept { return compare(other) < 0; }
    [[nodiscard]] bool operator>(const utf8_view& other) const noexcept { return compare(other) > 0; }
    [[nodiscard]] bool operator<=(const utf8_view& other) const noexcept { return compare(other) <= 0; }
    [[nodiscard]] bool operator>=(const utf8_view& other) const noexcept { return compare(other) >= 0; }

    [[nodiscard]] bool operator==(std::string_view other) const noexcept { return compare(other) == 0; }
    [[nodiscard]] bool operator!=(std::string_view other) const noexcept { return compare(other) != 0; }
    [[nodiscard]] bool operator<(std::string_view other) const noexcept { return compare(other) < 0; }
    [[nodiscard]] bool operator>(std::string_view other) const noexcept { return compare(other) > 0; }
    [[nodiscard]] bool operator<=(std::string_view other) const noexcept { return compare(other) <= 0; }
    [[nodiscard]] bool operator>=(std::string_view other) const noexcept { return compare(other) >= 0; }

    [[nodiscard]] bool operator==(const char* s) const noexcept { return compare(s) == 0; }
    [[nodiscard]] bool operator!=(const char* s) const noexcept { return compare(s) != 0; }
    [[nodiscard]] bool operator<(const char* s) const noexcept { return compare(s) < 0; }
    [[nodiscard]] bool operator>(const char* s) const noexcept { return compare(s) > 0; }
    [[nodiscard]] bool operator<=(const char* s) const noexcept { return compare(s) <= 0; }
    [[nodiscard]] bool operator>=(const char* s) const noexcept { return compare(s) >= 0; }

    [[nodiscard]] auto operator<=>(const utf8_view& other) const noexcept { return compare(other) <=> 0; }
    [[nodiscard]] auto operator<=>(std::string_view other) const noexcept { return compare(other) <=> 0; }
    [[nodiscard]] auto operator<=>(const char* s) const noexcept { return compare(s) <=> 0; }

    // === swap ===
    constexpr void swap(utf8_view& other) noexcept
    {
        std::swap(data_, other.data_);
        std::swap(byte_size_, other.byte_size_);
    }

    // ===== 显示宽度 (East Asian Width: 全角/CJK=2, 零宽=0, 其他=1) =====
    [[nodiscard]] size_t display_width() const noexcept
    {
        size_t w = 0;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        while (p < end)
        {
            // 无校验: 遍历前已确认合法 UTF-8
            char32_t cp = detail_utf8::utf8_decode_unchecked(p);
            w += static_cast<size_t>(unicode_data::cp_display_width(static_cast<uint32_t>(cp)));
            p += detail_utf8::k_utf8_seq_len[*p];
        }
        return w;
    }

    // ===== 大小写转换 (零分配, 写入外部缓冲) =====
    // 返回写入字节数; cap 不足时返回所需字节数, out 内容未定义
    size_t to_lower_into(char* out, size_t cap) const noexcept
    {
        return case_convert_into_(out, cap, false);
    }
    size_t to_upper_into(char* out, size_t cap) const noexcept
    {
        return case_convert_into_(out, cap, true);
    }

    // ===== trim (返回子 view, 仅指针移动, 零分配) =====
    [[nodiscard]] utf8_view trimmed_left() const noexcept
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        while (p < end)
        {
            uint32_t cp = 0; size_t len = 0;
            (void)detail_utf8::utf8_decode_one(p, end, &cp, &len);
            if (!unicode_data::is_unicode_space(cp)) break;
            p += len;
        }
        return utf8_view(reinterpret_cast<const char*>(p),
                         static_cast<size_t>(end - p));
    }
    [[nodiscard]] utf8_view trimmed_right() const noexcept
    {
        const uint8_t* begin = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = begin + byte_size_;
        const uint8_t* p = end;
        while (p > begin)
        {
            const uint8_t* q = p - 1;
            while (q > begin && (*q & 0xC0) == 0x80) --q;
            uint32_t cp = 0; size_t len = 0;
            (void)detail_utf8::utf8_decode_one(q, end, &cp, &len);
            if (!unicode_data::is_unicode_space(cp)) break;
            p = q;
        }
        return utf8_view(data_, static_cast<size_t>(p - begin));
    }
    [[nodiscard]] utf8_view trimmed() const noexcept
    {
        return trimmed_left().trimmed_right();
    }

    // ===== 码点分类聚合 (零分配) =====
    [[nodiscard]] bool is_ascii() const noexcept
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        // 字节分组 SWAR 检测高位 bit
        while (p + 8 <= end)
        {
            uint64_t chunk;
            std::memcpy(&chunk, p, 8);
            if (chunk & 0x8080808080808080ULL) return false;
            p += 8;
        }
        while (p < end)
        {
            if (*p >= 0x80) return false;
            ++p;
        }
        return true;
    }
    [[nodiscard]] bool is_valid() const noexcept
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        while (p < end)
        {
            uint32_t cp = 0; size_t len = 0;
            if (!detail_utf8::utf8_decode_one(p, end, &cp, &len)) return false;
            if (!detail_utf8::is_valid_codepoint(cp)) return false;
            if (!detail_utf8::is_shortest_form(cp, static_cast<uint8_t>(len))) return false;
            p += len;
        }
        return true;
    }
    [[nodiscard]] size_t count(char32_t cp) const noexcept
    {
        size_t n = 0;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        while (p < end)
        {
            uint32_t cur = 0; size_t len = 0;
            (void)detail_utf8::utf8_decode_one(p, end, &cur, &len);
            if (cur == static_cast<uint32_t>(cp)) ++n;
            p += len;
        }
        return n;
    }

private:
    // 大小写转换实现: upper=true 大写, false 小写
    size_t case_convert_into_(char* out, size_t cap, bool upper) const noexcept
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        size_t written = 0;
        while (p < end)
        {
            uint32_t cp = 0; size_t len = 0;
            (void)detail_utf8::utf8_decode_one(p, end, &cp, &len);
            uint32_t nc = upper ? unicode_data::to_upper_cp(cp)
                                : unicode_data::to_lower_cp(cp);
            uint8_t enc[4] = {};
            size_t nlen = 0;
            (void)detail_utf8::utf8_encode_one(nc, enc, &nlen);
            if (written + nlen <= cap)
            {
                std::memcpy(out + written, enc, nlen);
            }
            written += nlen;
            p += len;
        }
        return written;
    }

    const char* data_{nullptr};
    size_t      byte_size_{0};
};

// === 非成员 swap ===
inline void swap(utf8_view& a, utf8_view& b) noexcept { a.swap(b); }

// === 流输出 ===
inline std::ostream& operator<<(std::ostream& os, const utf8_view& s)
{
    os.write(s.data(), static_cast<std::streamsize>(s.byte_size()));
    return os;
}

// === std::hash 特化 ===
namespace std {
template <>
struct hash<utf8_view>
{
    size_t operator()(const utf8_view& s) const noexcept
    {
        std::string_view sv = s.byte_view();
        return std::hash<std::string_view>{}(sv);
    }
};
} // namespace std

// =============================================================================
// 自由函数: 直接对 const char* / string_view 操作, 零分配, 委托 utf8_view
// =============================================================================

// 显示宽度
[[nodiscard]] inline size_t utf8_display_width(const char* s) noexcept
{
    return s ? utf8_view(s).display_width() : 0;
}
[[nodiscard]] inline size_t utf8_display_width(std::string_view sv) noexcept
{
    return utf8_view(sv).display_width();
}

// 码点计数
[[nodiscard]] inline size_t utf8_cp_count(const char* s) noexcept
{
    return s ? utf8_view(s).size() : 0;
}
[[nodiscard]] inline size_t utf8_cp_count(std::string_view sv) noexcept
{
    return utf8_view(sv).size();
}

// 第 cp_idx 个码点的字节偏移 (越界返回 utf8_view::npos)
[[nodiscard]] inline size_t utf8_byte_offset(const char* s, size_t cp_idx) noexcept
{
    if (!s) return utf8_view::npos;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    const uint8_t* start = p;
    // 无 end 约束, 以 NUL 为界
    while (*p && cp_idx > 0)
    {
        uint32_t cp = 0; size_t len = 0;
        (void)detail_utf8::utf8_decode_one(p, p + 4, &cp, &len);
        p += len;
        --cp_idx;
    }
    return *p == 0 ? utf8_view::npos : static_cast<size_t>(p - start);
}
[[nodiscard]] inline size_t utf8_byte_offset(std::string_view sv, size_t cp_idx) noexcept
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(sv.data());
    const uint8_t* start = p;
    const uint8_t* end = p + sv.size();
    while (p < end && cp_idx > 0)
    {
        uint32_t cp = 0; size_t len = 0;
        (void)detail_utf8::utf8_decode_one(p, end, &cp, &len);
        p += len;
        --cp_idx;
    }
    return (p > end) ? utf8_view::npos : static_cast<size_t>(p - start);
}

// 合法 UTF-8 校验
[[nodiscard]] inline bool utf8_is_valid(const char* s) noexcept
{
    return s ? utf8_view(s).is_valid() : true;
}
[[nodiscard]] inline bool utf8_is_valid(std::string_view sv) noexcept
{
    return utf8_view(sv).is_valid();
}

// 纯 ASCII 判断
[[nodiscard]] inline bool utf8_is_ascii(const char* s) noexcept
{
    if (!s) return true;
    while (*s) { if (static_cast<uint8_t>(*s) >= 0x80) return false; ++s; }
    return true;
}
[[nodiscard]] inline bool utf8_is_ascii(std::string_view sv) noexcept
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(sv.data());
    const uint8_t* end = p + sv.size();
    while (p < end) { if (*p >= 0x80) return false; ++p; }
    return true;
}

// 游标式解码: 从 p 解码一个码点, 返回码点值; *consumed 写入消耗字节数
[[nodiscard]] inline char32_t utf8_next_cp(const char* p, const char* end, size_t* consumed = nullptr) noexcept
{
    uint32_t cp = 0; size_t len = 0;
    (void)detail_utf8::utf8_decode_one(
        reinterpret_cast<const uint8_t*>(p),
        reinterpret_cast<const uint8_t*>(end), &cp, &len);
    if (consumed) *consumed = len;
    return static_cast<char32_t>(cp);
}

// 游标式回溯: 从 p 往前解码一个码点, 返回码点值; *consumed 写入回退字节数
[[nodiscard]] inline char32_t utf8_prev_cp(const char* begin, const char* p, size_t* consumed = nullptr) noexcept
{
    if (p <= begin)
    {
        if (consumed) *consumed = 0;
        return 0;
    }
    const uint8_t* q = reinterpret_cast<const uint8_t*>(p) - 1;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(begin);
    while (q > b && (*q & 0xC0) == 0x80) --q;
    uint32_t cp = 0; size_t len = 0;
    (void)detail_utf8::utf8_decode_one(q, reinterpret_cast<const uint8_t*>(p), &cp, &len);
    if (consumed) *consumed = static_cast<size_t>(p - reinterpret_cast<const char*>(q));
    return static_cast<char32_t>(cp);
}
