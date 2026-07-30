#pragma once

// utf8pp.hpp - utf8pp 字符串类 (拥有内存, SSO + 码点偏移缓存)

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <compare>
#include <string_view>
#include <string>
#include <array>
#include <vector>
#include <span>
#include <iterator>
#include <iostream>
#include <limits>
#include "force_inline.hpp"
#include "dense.hpp"
#include "utf8_codec.hpp"
#include "utf8_view.hpp"
#include "unicode_data.hpp"
#include "../config/utf8pp_config.hpp"

#if UTF8PP_ENABLE_ALLOCATOR
#include "memory_pool.hpp"
#if UTF8PP_ALLOCATOR_TYPE == UTF8PP_ALLOC_LAYERED
#include "layered_allocator.hpp"
#endif
#endif

// === utf8pp 内存分配抽象层 ===
// 默认 (UTF8PP_ENABLE_ALLOCATOR=0): std::malloc/std::free
// 启用 (=1): 自研分配器 (全局实例)
#if UTF8PP_ENABLE_ALLOCATOR
#if UTF8PP_ALLOCATOR_TYPE == UTF8PP_ALLOC_LAYERED
inline layered_allocator utf8pp_pool_{};
#elif UTF8PP_ALLOCATOR_TYPE == UTF8PP_ALLOC_MEMORY_POOL
inline memory_pool utf8pp_pool_{};
#endif
[[nodiscard]] FORCE_INLINE void* utf8pp_alloc(size_t n) noexcept { return utf8pp_pool_.allocate(n); }
FORCE_INLINE void utf8pp_free(void* p) noexcept { utf8pp_pool_.deallocate(p); }
FORCE_INLINE void utf8pp_free(void* p, size_t n) noexcept { utf8pp_pool_.deallocate(p, n); }
#else
[[nodiscard]] FORCE_INLINE void* utf8pp_alloc(size_t n) noexcept { return std::malloc(n); }
FORCE_INLINE void utf8pp_free(void* p) noexcept { std::free(p); }
FORCE_INLINE void utf8pp_free(void* p, size_t /*n*/) noexcept { std::free(p); }
#endif

// === utf8pp 字符串类 ===

class utf8pp
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
        using iterator_category = std::random_access_iterator_tag;

        friend class utf8pp;

        const_iterator() noexcept = default;
        const_iterator(const char* p, const char* end) noexcept : p_(p), end_(end) {}

        const_iterator& operator++() noexcept
        {
            if (p_ < end_)
            {
                uint8_t lead = static_cast<uint8_t>(*p_);
                uint8_t seq = detail_utf8::k_utf8_seq_len[lead];
                if (seq == 0) seq = 1;
                p_ += seq;
                if (p_ > end_) p_ = end_;
            }
            return *this;
        }

        const_iterator operator++(int) noexcept
        {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        const_iterator& operator--() noexcept
        {
            if (p_ > begin_)
            {
                const uint8_t* q = reinterpret_cast<const uint8_t*>(p_);
                const uint8_t* b = reinterpret_cast<const uint8_t*>(begin_);
                --q;
                while (q > b && (*q & 0xC0) == 0x80) --q;
                p_ = reinterpret_cast<const char*>(q);
            }
            return *this;
        }

        const_iterator operator--(int) noexcept
        {
            const_iterator tmp = *this;
            --(*this);
            return tmp;
        }

        const_iterator& operator+=(difference_type n) noexcept
        {
            if (n >= 0)
            {
                for (difference_type i = 0; i < n && p_ < end_; ++i) ++(*this);
            }
            else
            {
                for (difference_type i = 0; i > n && p_ > begin_; --i) --(*this);
            }
            return *this;
        }

        const_iterator& operator-=(difference_type n) noexcept { return *this += -n; }

        [[nodiscard]] const_iterator operator+(difference_type n) const noexcept
        {
            const_iterator tmp = *this;
            tmp += n;
            return tmp;
        }

        [[nodiscard]] friend const_iterator operator+(difference_type n, const const_iterator& it) noexcept
        {
            return it + n;
        }

        [[nodiscard]] const_iterator operator-(difference_type n) const noexcept
        {
            const_iterator tmp = *this;
            tmp -= n;
            return tmp;
        }

        [[nodiscard]] difference_type operator-(const const_iterator& o) const noexcept
        {
            // 码点距离: 遍历计数 (O(n))
            if (p_ == o.p_) return 0;
            if (p_ < o.p_)
            {
                const_iterator tmp = o;
                difference_type n = 0;
                while (tmp.p_ > p_) { --tmp; ++n; }
                return -n;
            }
            else
            {
                difference_type n = 0;
                const_iterator tmp = *this;
                while (tmp.p_ > o.p_) { --tmp; ++n; }
                return n;
            }
        }

        [[nodiscard]] char32_t operator*() const noexcept
        {
            uint32_t cp = 0;
            size_t len = 0;
            (void)detail_utf8::utf8_decode_one(
                reinterpret_cast<const uint8_t*>(p_),
                reinterpret_cast<const uint8_t*>(end_), &cp, &len);
            return static_cast<char32_t>(cp);
        }

        [[nodiscard]] char32_t operator[](difference_type n) const noexcept
        {
            return *(*this + n);
        }

        [[nodiscard]] bool operator==(const const_iterator& o) const noexcept { return p_ == o.p_; }
        [[nodiscard]] bool operator!=(const const_iterator& o) const noexcept { return p_ != o.p_; }
        [[nodiscard]] bool operator<(const const_iterator& o) const noexcept { return p_ < o.p_; }
        [[nodiscard]] bool operator>(const const_iterator& o) const noexcept { return p_ > o.p_; }
        [[nodiscard]] bool operator<=(const const_iterator& o) const noexcept { return p_ <= o.p_; }
        [[nodiscard]] bool operator>=(const const_iterator& o) const noexcept { return p_ >= o.p_; }

        const char* ptr() const noexcept { return p_; }

        void set_begin(const char* b) noexcept { begin_ = b; }

    private:
        const char* p_{nullptr};
        const char* begin_{nullptr};
        const char* end_{nullptr};
    };

    class const_reverse_iterator
    {
    public:
        using value_type = char32_t;
        using reference = char32_t;
        using pointer = const char32_t*;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::random_access_iterator_tag;

        const_reverse_iterator() noexcept = default;
        const_reverse_iterator(const char* p, const char* begin, const char* end) noexcept
            : p_(p), begin_(begin), end_(end) {}

        const_reverse_iterator& operator++() noexcept
        {
            if (p_ > begin_)
            {
                const uint8_t* q = reinterpret_cast<const uint8_t*>(p_);
                const uint8_t* b = reinterpret_cast<const uint8_t*>(begin_);
                --q;
                while (q > b && (*q & 0xC0) == 0x80) --q;
                p_ = reinterpret_cast<const char*>(q);
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
                const uint8_t* q = reinterpret_cast<const uint8_t*>(p_);
                uint8_t lead = *q;
                uint8_t seq = detail_utf8::k_utf8_seq_len[lead];
                if (seq == 0) seq = 1;
                q += seq;
                if (q > reinterpret_cast<const uint8_t*>(end_)) q = reinterpret_cast<const uint8_t*>(end_);
                p_ = reinterpret_cast<const char*>(q);
            }
            return *this;
        }

        const_reverse_iterator operator--(int) noexcept
        {
            const_reverse_iterator tmp = *this;
            --(*this);
            return tmp;
        }

        const_reverse_iterator& operator+=(difference_type n) noexcept
        {
            if (n >= 0) { for (difference_type i = 0; i < n; ++i) ++(*this); }
            else { for (difference_type i = 0; i > n; --i) --(*this); }
            return *this;
        }

        const_reverse_iterator& operator-=(difference_type n) noexcept { return *this += -n; }

        [[nodiscard]] const_reverse_iterator operator+(difference_type n) const noexcept
        {
            const_reverse_iterator tmp = *this;
            tmp += n;
            return tmp;
        }

        [[nodiscard]] const_reverse_iterator operator-(difference_type n) const noexcept
        {
            const_reverse_iterator tmp = *this;
            tmp -= n;
            return tmp;
        }

        [[nodiscard]] difference_type operator-(const const_reverse_iterator& o) const noexcept
        {
            if (p_ == o.p_) return 0;
            if (p_ < o.p_)
            {
                difference_type n = 0;
                const_reverse_iterator tmp = o;
                while (tmp.p_ > p_) { ++tmp; ++n; }
                return n;
            }
            else
            {
                difference_type n = 0;
                const_reverse_iterator tmp = *this;
                while (tmp.p_ > o.p_) { ++tmp; ++n; }
                return -n;
            }
        }

        [[nodiscard]] char32_t operator*() const noexcept
        {
            const uint8_t* q = reinterpret_cast<const uint8_t*>(p_);
            const uint8_t* b = reinterpret_cast<const uint8_t*>(begin_);
            const uint8_t* qend = reinterpret_cast<const uint8_t*>(end_);
            --q;
            while (q > b && (*q & 0xC0) == 0x80) --q;
            uint32_t cp = 0;
            size_t len = 0;
            (void)detail_utf8::utf8_decode_one(q, qend, &cp, &len);
            return static_cast<char32_t>(cp);
        }

        [[nodiscard]] char32_t operator[](difference_type n) const noexcept
        {
            return *(*this + n);
        }

        [[nodiscard]] bool operator==(const const_reverse_iterator& o) const noexcept { return p_ == o.p_; }
        [[nodiscard]] bool operator!=(const const_reverse_iterator& o) const noexcept { return p_ != o.p_; }
        [[nodiscard]] bool operator<(const const_reverse_iterator& o) const noexcept { return p_ > o.p_; }
        [[nodiscard]] bool operator>(const const_reverse_iterator& o) const noexcept { return p_ < o.p_; }
        [[nodiscard]] bool operator<=(const const_reverse_iterator& o) const noexcept { return p_ >= o.p_; }
        [[nodiscard]] bool operator>=(const const_reverse_iterator& o) const noexcept { return p_ <= o.p_; }

    private:
        friend class utf8pp;
        const char* p_{nullptr};
        const char* begin_{nullptr};
        const char* end_{nullptr};
    };

    using reverse_iterator = const_reverse_iterator;

    // === 字节迭代器 (只读 contiguous, O(1) 字节访问) ===
    class const_byte_iterator
    {
    public:
        using value_type        = char;
        using reference         = const char&;
        using pointer           = const char*;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::contiguous_iterator_tag;

        const_byte_iterator() noexcept = default;
        explicit const_byte_iterator(const char* p) noexcept : p_(p) {}

        const char& operator*() const noexcept { return *p_; }
        const char* operator->() const noexcept { return p_; }
        const char& operator[](difference_type n) const noexcept { return p_[n]; }

        const_byte_iterator& operator++() noexcept { ++p_; return *this; }
        const_byte_iterator  operator++(int) noexcept { auto t = *this; ++p_; return t; }
        const_byte_iterator& operator--() noexcept { --p_; return *this; }
        const_byte_iterator  operator--(int) noexcept { auto t = *this; --p_; return t; }
        const_byte_iterator& operator+=(difference_type n) noexcept { p_ += n; return *this; }
        const_byte_iterator& operator-=(difference_type n) noexcept { p_ -= n; return *this; }

        [[nodiscard]] const_byte_iterator operator+(difference_type n) const noexcept
        { return const_byte_iterator(p_ + n); }
        [[nodiscard]] friend const_byte_iterator operator+(difference_type n, const const_byte_iterator& it) noexcept
        { return const_byte_iterator(it.p_ + n); }
        [[nodiscard]] const_byte_iterator operator-(difference_type n) const noexcept
        { return const_byte_iterator(p_ - n); }
        [[nodiscard]] difference_type operator-(const const_byte_iterator& o) const noexcept
        { return p_ - o.p_; }

        [[nodiscard]] bool operator==(const const_byte_iterator& o) const noexcept { return p_ == o.p_; }
        [[nodiscard]] bool operator!=(const const_byte_iterator& o) const noexcept { return p_ != o.p_; }
        [[nodiscard]] bool operator<(const const_byte_iterator& o) const noexcept { return p_ < o.p_; }
        [[nodiscard]] bool operator>(const const_byte_iterator& o) const noexcept { return p_ > o.p_; }
        [[nodiscard]] bool operator<=(const const_byte_iterator& o) const noexcept { return p_ <= o.p_; }
        [[nodiscard]] bool operator>=(const const_byte_iterator& o) const noexcept { return p_ >= o.p_; }

        const char* ptr() const noexcept { return p_; }

    private:
        const char* p_{nullptr};
    };

    using byte_iterator = const_byte_iterator;

    // === 字形簇迭代器 (Grapheme Cluster Iterator, UAX #29 简化版) ===
    // 字形簇 = 用户感知的单个字符 (如 'e' + 组合重音 = 1 个字形簇, emoji ZWJ 序列 = 1 个)
    // 简化规则: CR+LF / Hangul syllable / 组合标记延续 / Emoji+ZWJ+Emoji 不分割
    class const_grapheme_iterator
    {
    public:
        using value_type        = utf8_view;
        using reference         = utf8_view;
        using pointer           = const utf8_view*;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        const_grapheme_iterator() noexcept = default;
        const_grapheme_iterator(const char* p, const char* end) noexcept
            : current_(p), next_(p), end_(end)
        {
            if (current_ < end_) advance_next();
        }

        [[nodiscard]] utf8_view operator*() const noexcept
        {
            return utf8_view(current_, static_cast<size_t>(next_ - current_));
        }

        const_grapheme_iterator& operator++() noexcept
        {
            if (next_ >= end_) { current_ = next_; return *this; }
            current_ = next_;
            advance_next();
            return *this;
        }

        const_grapheme_iterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        [[nodiscard]] bool operator==(const const_grapheme_iterator& o) const noexcept { return current_ == o.current_; }
        [[nodiscard]] bool operator!=(const const_grapheme_iterator& o) const noexcept { return current_ != o.current_; }

        const char* ptr() const noexcept { return next_; }
        const char* start() const noexcept { return current_; }

    private:
        // 解码一个码点, 返回 cp 和字节长度
        static void decode_one(const char* p, const char* end, uint32_t& cp, size_t& len) noexcept
        {
            cp = 0; len = 0;
            if (p >= end) return;
            (void)detail_utf8::utf8_decode_one(
                reinterpret_cast<const uint8_t*>(p),
                reinterpret_cast<const uint8_t*>(end), &cp, &len);
        }

        // 推进 next_ 到下一个字形簇边界 (current_ 保持不变)
        void advance_next() noexcept
        {
            if (next_ >= end_) return;
            uint32_t cp = 0; size_t len = 0;
            decode_one(next_, end_, cp, len);
            if (len == 0) { ++next_; return; }
            const char* cur = next_;
            const char* next = next_ + len;
            uint32_t prev_cp = cp;

            // GB3: CR × LF
            if (prev_cp == 0x000D && next < end_)
            {
                uint32_t cp2 = 0; size_t len2 = 0;
                decode_one(next, end_, cp2, len2);
                if (cp2 == 0x000A) { next_ = next + (len2 ? len2 : 1); return; }
            }

            // GB6/GB7/GB8: Hangul LVT 序列
            // L = 0x1100-0x115F, 0xA960-0xA97F
            // V = 0x1160-0x11A7, 0xD7B0-0xD7FF
            // T = 0x11A8-0x11FF
            // LV = Hangul Syllable (AC00-D7A3 中 LV 形)
            // LVT = Hangul Syllable (AC00-D7A3 中 LVT 形)
            auto is_hangul_l = [](uint32_t c) {
                return (c >= 0x1100 && c <= 0x115F) || (c >= 0xA960 && c <= 0xA97F);
            };
            auto is_hangul_v = [](uint32_t c) {
                return (c >= 0x1160 && c <= 0x11A7) || (c >= 0xD7B0 && c <= 0xD7FF);
            };
            auto is_hangul_t = [](uint32_t c) { return c >= 0x11A8 && c <= 0x11FF; };
            auto is_hangul_lv_lvt = [](uint32_t c) { return c >= 0xAC00 && c <= 0xD7A3; };
            auto is_hangul_lvt_only = [](uint32_t c) {
                return c >= 0xAC00 && c <= 0xD7A3 && ((c - 0xAC00) % 28 != 0);
            };

            bool in_hangul = false;
            if (is_hangul_l(prev_cp)) in_hangul = true;
            else if (is_hangul_lv_lvt(prev_cp)) in_hangul = true;

            while (next < end_)
            {
                uint32_t cp2 = 0; size_t len2 = 0;
                decode_one(next, end_, cp2, len2);
                if (len2 == 0) break;

                // GB4: Control (含 CR/LF) 后断开 (除 CR LF 已处理)
                if (cp2 == 0x000D || cp2 == 0x000A ||
                    (cp2 < 0x20 && cp2 != 0x09 && cp2 != 0x0A && cp2 != 0x0D) ||
                    (cp2 >= 0x7F && cp2 <= 0x9F)) break;

                // GB6: L × (L|V|LV|LVT)
                if (is_hangul_l(prev_cp) &&
                    (is_hangul_l(cp2) || is_hangul_v(cp2) || is_hangul_lv_lvt(cp2)))
                { prev_cp = cp2; cur = next; next += len2; in_hangul = true; continue; }

                // GB7: (LV|V) × (V|T)
                if ((is_hangul_lv_lvt(prev_cp) || is_hangul_v(prev_cp)) &&
                    (is_hangul_v(cp2) || is_hangul_t(cp2)))
                {
                    // LV/LVT only when previous is LV/LVT
                    if (is_hangul_v(prev_cp) && is_hangul_v(cp2))
                    { prev_cp = cp2; cur = next; next += len2; continue; }
                    if (is_hangul_lv_lvt(prev_cp) && !is_hangul_lvt_only(prev_cp))
                    { prev_cp = cp2; cur = next; next += len2; continue; }
                    // 通用情况
                    { prev_cp = cp2; cur = next; next += len2; continue; }
                }

                // GB8: (LVT|T) × T
                if ((is_hangul_lvt_only(prev_cp) || is_hangul_t(prev_cp)) && is_hangul_t(cp2))
                { prev_cp = cp2; cur = next; next += len2; continue; }

                // GB9: × (Extend | ZWJ) - 不在组合标记/ZWJ 前断开
                if (unicode_data::is_combining_mark(cp2) || cp2 == 0x200D)
                { prev_cp = cp2; cur = next; next += len2; continue; }

                // GB9a: × SpacingMark (部分 Indic spacing marks)
                if ((cp2 >= 0x0903 && cp2 <= 0x0939) ||  // Devanagari sign/spacing
                    (cp2 >= 0x093E && cp2 <= 0x094D) ||
                    (cp2 >= 0x0951 && cp2 <= 0x0954))
                { prev_cp = cp2; cur = next; next += len2; continue; }

                // GB11: \p{Extended_Pictographic} Extend* ZWJ × \p{Extended_Pictographic}
                if (cp2 == 0x200D && unicode_data::is_extended_pictographic(prev_cp))
                {
                    // 找 ZWJ 后的下一个
                    const char* after_zwj = next + len2;
                    if (after_zwj < end_)
                    {
                        uint32_t cp3 = 0; size_t len3 = 0;
                        decode_one(after_zwj, end_, cp3, len3);
                        if (len3 > 0 && unicode_data::is_extended_pictographic(cp3))
                        {
                            // 跨过 ZWJ 和下一个 emoji
                            prev_cp = cp3;
                            cur = after_zwj + len3;
                            next = after_zwj + len3;
                            continue;
                        }
                    }
                }

                // GB999: 其他情况断开
                break;
            }

            next_ = next;
            (void)in_hangul;
            (void)cur;
        }

        const char* current_{nullptr};
        const char* next_{nullptr};
        const char* end_{nullptr};
    };

    using grapheme_iterator = const_grapheme_iterator;

    [[nodiscard]] const_grapheme_iterator grapheme_begin() const noexcept
    {
        return const_grapheme_iterator(data_, data_ + byte_size_);
    }
    [[nodiscard]] const_grapheme_iterator grapheme_end() const noexcept
    {
        const char* end = data_ + byte_size_;
        return const_grapheme_iterator(end, end);
    }
    [[nodiscard]] const_grapheme_iterator grapheme_cbegin() const noexcept { return grapheme_begin(); }
    [[nodiscard]] const_grapheme_iterator grapheme_cend() const noexcept { return grapheme_end(); }

    // 字形簇数量
    [[nodiscard]] size_t grapheme_count() const noexcept
    {
        size_t n = 0;
        for (auto it = grapheme_begin(); it != grapheme_end(); ++it) ++n;
        return n;
    }

    // 按字形簇分割, 返回 dense<utf8_view>
    [[nodiscard]] dense<utf8_view> grapheme_clusters() const
    {
        dense<utf8_view> result;
        for (auto it = grapheme_begin(); it != grapheme_end(); ++it)
            result.push_back(*it);
        return result;
    }

    // === SSO 常量 ===
    static constexpr size_t SSO_CAPACITY = 22;

    // === 构造/析构 ===
    utf8pp() noexcept
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        cp_offsets_ = sso_cp_offsets_;
        cp_offsets_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
    }

    utf8pp(const char* s) : utf8pp(s, s ? std::strlen(s) : 0) {}

    utf8pp(const char8_t* s)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        cp_offsets_ = sso_cp_offsets_;
        cp_offsets_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        const char* p = reinterpret_cast<const char*>(s);
        if (p) init_from_utf8(p, std::strlen(p));
    }

    utf8pp(const char8_t* s, size_t byte_len)
        : utf8pp(reinterpret_cast<const char*>(s), byte_len) {}

    utf8pp(const char* s, size_t byte_len)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        cp_offsets_ = sso_cp_offsets_;
        cp_offsets_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        if (byte_len == 0) return;
        init_from_utf8(s, byte_len);
    }

    utf8pp(const char32_t* s, size_t cp_count)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        cp_offsets_ = sso_cp_offsets_;
        cp_offsets_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        if (cp_count == 0) return;
        init_from_char32(s, cp_count);
    }

    utf8pp(size_t n, char32_t cp)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        cp_offsets_ = sso_cp_offsets_;
        cp_offsets_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        if (n == 0) return;
        append_cp(n, cp);
    }

    utf8pp(std::string_view sv) : utf8pp(sv.data(), sv.size()) {}

    // std::string / u8string / u32string 适配构造 (与 std::string 互操作)
    utf8pp(const std::string& s) : utf8pp(s.data(), s.size()) {}
    utf8pp(const std::u8string& s) : utf8pp(reinterpret_cast<const char*>(s.data()), s.size()) {}
    utf8pp(const std::u32string& s) : utf8pp(s.data(), s.size()) {}

    // utf8_view 构造 (零拷贝视图 → 拥有内存拷贝)
    utf8pp(const utf8_view& v) : utf8pp(v.data(), v.byte_size()) {}

    // initializer_list<char32_t> 构造 (与 std::string 的 initializer_list<char> 对齐)
    utf8pp(std::initializer_list<char32_t> il)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        cp_offsets_ = sso_cp_offsets_;
        cp_offsets_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        for (char32_t cp : il) push_back(cp);
    }

    // 迭代器范围构造 (与 std::string(InputIt, InputIt) 对齐)
    // 约束: InputIt 解引用结果可转换为 char32_t
    template <typename InputIt, typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    utf8pp(InputIt first, InputIt last)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        cp_offsets_ = sso_cp_offsets_;
        cp_offsets_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        for (InputIt it = first; it != last; ++it) push_back(static_cast<char32_t>(*it));
    }

    // 禁止 nullptr 隐式构造
    utf8pp(std::nullptr_t) = delete;

    utf8pp(const utf8pp& other) : byte_size_(other.byte_size_), cp_count_(other.cp_count_)
    {
        if (other.is_sso())
        {
            data_ = sso_buffer_;
            byte_capacity_ = SSO_CAPACITY;
            cp_offsets_ = sso_cp_offsets_;
            cp_offsets_capacity_ = SSO_CAPACITY;
        }
        else
        {
            data_ = static_cast<char*>(utf8pp_alloc(other.byte_capacity_ + 1));
            if (!data_) std::abort();
            byte_capacity_ = other.byte_capacity_;
            cp_offsets_ = static_cast<uint32_t*>(utf8pp_alloc(other.cp_offsets_capacity_ * sizeof(uint32_t)));
            if (!cp_offsets_) std::abort();
            cp_offsets_capacity_ = other.cp_offsets_capacity_;
        }
        if (byte_size_ > 0)
        {
            std::memcpy(data_, other.data_, byte_size_);
        }
        data_[byte_size_] = '\0';

        if (cp_count_ > 0)
        {
            std::memcpy(cp_offsets_, other.cp_offsets_, cp_count_ * sizeof(uint32_t));
        }
    }

    utf8pp(utf8pp&& other) noexcept
        : byte_size_(other.byte_size_), cp_count_(other.cp_count_)
    {
        if (other.is_sso())
        {
            data_ = sso_buffer_;
            byte_capacity_ = SSO_CAPACITY;
            cp_offsets_ = sso_cp_offsets_;
            cp_offsets_capacity_ = SSO_CAPACITY;
            std::memcpy(sso_buffer_, other.sso_buffer_, SSO_CAPACITY + 1);
            std::memcpy(sso_cp_offsets_, other.sso_cp_offsets_, SSO_CAPACITY * sizeof(uint32_t));
        }
        else
        {
            data_ = other.data_;
            byte_capacity_ = other.byte_capacity_;
            cp_offsets_ = other.cp_offsets_;
            cp_offsets_capacity_ = other.cp_offsets_capacity_;
        }
        other.data_ = other.sso_buffer_;
        other.byte_size_ = 0;
        other.byte_capacity_ = SSO_CAPACITY;
        other.cp_offsets_ = other.sso_cp_offsets_;
        other.cp_count_ = 0;
        other.cp_offsets_capacity_ = SSO_CAPACITY;
        other.sso_buffer_[0] = '\0';
    }

    ~utf8pp() { release(); }

    utf8pp& operator=(const utf8pp& other)
    {
        if (this != &other)
        {
            utf8pp tmp(other);
            swap(tmp);
        }
        return *this;
    }

    utf8pp& operator=(utf8pp&& other) noexcept
    {
        if (this != &other)
        {
            release();
            if (other.is_sso())
            {
                data_ = sso_buffer_;
                byte_capacity_ = SSO_CAPACITY;
                cp_offsets_ = sso_cp_offsets_;
                cp_offsets_capacity_ = SSO_CAPACITY;
                byte_size_ = other.byte_size_;
                cp_count_ = other.cp_count_;
                std::memcpy(sso_buffer_, other.sso_buffer_, SSO_CAPACITY + 1);
                std::memcpy(sso_cp_offsets_, other.sso_cp_offsets_, SSO_CAPACITY * sizeof(uint32_t));
            }
            else
            {
                data_ = other.data_;
                byte_size_ = other.byte_size_;
                byte_capacity_ = other.byte_capacity_;
                cp_offsets_ = other.cp_offsets_;
                cp_count_ = other.cp_count_;
                cp_offsets_capacity_ = other.cp_offsets_capacity_;
            }
            other.data_ = other.sso_buffer_;
            other.byte_size_ = 0;
            other.byte_capacity_ = SSO_CAPACITY;
            other.cp_offsets_ = other.sso_cp_offsets_;
            other.cp_count_ = 0;
            other.cp_offsets_capacity_ = SSO_CAPACITY;
            other.sso_buffer_[0] = '\0';
        }
        return *this;
    }

    utf8pp& operator=(const char* s) { return assign(s, s ? std::strlen(s) : 0); }
    utf8pp& operator=(std::string_view sv) { return assign(sv.data(), sv.size()); }
    utf8pp& operator=(char32_t cp) { clear(); push_back(cp); return *this; }
    utf8pp& operator=(const char8_t* s)
    {
        const char* p = reinterpret_cast<const char*>(s);
        return assign(p, p ? std::strlen(p) : 0);
    }
    utf8pp& operator=(std::initializer_list<char32_t> il) { return assign(il); }
    utf8pp& operator=(const std::string& s) { return assign(s.data(), s.size()); }
    utf8pp& operator=(const std::u8string& s)
    {
        return assign(reinterpret_cast<const char*>(s.data()), s.size());
    }
    utf8pp& operator=(const utf8_view& v) { return assign(v.data(), v.byte_size()); }

    utf8pp& assign(const char* s, size_t byte_len)
    {
        clear();
        if (byte_len > 0) init_from_utf8(s, byte_len);
        return *this;
    }

    // assign 单参/范围重载 (与 std::string::assign 对齐)
    utf8pp& assign(const utf8pp& other) { return assign(other.data_, other.byte_size_); }
    utf8pp& assign(const char* s) { return assign(s, s ? std::strlen(s) : 0); }
    utf8pp& assign(std::string_view sv) { return assign(sv.data(), sv.size()); }
    utf8pp& assign(const std::string& s) { return assign(s.data(), s.size()); }
    utf8pp& assign(const utf8_view& v) { return assign(v.data(), v.byte_size()); }
    utf8pp& assign(const char8_t* s)
    {
        const char* p = reinterpret_cast<const char*>(s);
        return assign(p, p ? std::strlen(p) : 0);
    }
    utf8pp& assign(std::initializer_list<char32_t> il)
    {
        clear();
        for (char32_t cp : il) push_back(cp);
        return *this;
    }
    template <typename InputIt, typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    utf8pp& assign(InputIt first, InputIt last)
    {
        clear();
        for (InputIt it = first; it != last; ++it) push_back(static_cast<char32_t>(*it));
        return *this;
    }

    // fill-assign: n 个 cp (与 std::string::assign(size_type, char) 对齐)
    utf8pp& assign(size_t n, char32_t cp)
    {
        clear();
        append_cp(n, cp);
        return *this;
    }
    // std::u8string / u32string 适配 assign
    utf8pp& assign(const std::u8string& s)
    {
        return assign(reinterpret_cast<const char*>(s.data()), s.size());
    }
    utf8pp& assign(const std::u32string& s)
    {
        clear();
        for (char32_t c : s) push_back(c);
        return *this;
    }
    // 子串 assign: 来自 other 的 [pos, pos+n) (与 std::string::assign(const string&, pos, n) 对齐)
    utf8pp& assign(const utf8pp& other, size_t pos, size_t n = npos)
    {
        return assign(other.substr(pos, n));
    }

    void swap(utf8pp& other) noexcept
    {
        if (is_sso() && other.is_sso())
        {
            // SSO ↔ SSO: 直接交换嵌入数据
            char tmp_bytes[SSO_CAPACITY + 1];
            uint32_t tmp_cps[SSO_CAPACITY];
            std::memcpy(tmp_bytes, sso_buffer_, SSO_CAPACITY + 1);
            std::memcpy(tmp_cps, sso_cp_offsets_, SSO_CAPACITY * sizeof(uint32_t));
            std::memcpy(sso_buffer_, other.sso_buffer_, SSO_CAPACITY + 1);
            std::memcpy(sso_cp_offsets_, other.sso_cp_offsets_, SSO_CAPACITY * sizeof(uint32_t));
            std::memcpy(other.sso_buffer_, tmp_bytes, SSO_CAPACITY + 1);
            std::memcpy(other.sso_cp_offsets_, tmp_cps, SSO_CAPACITY * sizeof(uint32_t));
            std::swap(byte_size_, other.byte_size_);
            std::swap(cp_count_, other.cp_count_);
        }
        else if (!is_sso() && !other.is_sso())
        {
            // heap ↔ heap: 直接交换指针
            std::swap(data_, other.data_);
            std::swap(byte_size_, other.byte_size_);
            std::swap(byte_capacity_, other.byte_capacity_);
            std::swap(cp_offsets_, other.cp_offsets_);
            std::swap(cp_count_, other.cp_count_);
            std::swap(cp_offsets_capacity_, other.cp_offsets_capacity_);
        }
        else
        {
            // SSO ↔ heap: 用临时对象中转
            utf8pp tmp(std::move(*this));
            *this = std::move(other);
            other = std::move(tmp);
        }
    }

    // === 容量 ===
    [[nodiscard]] size_t size() const noexcept { return cp_count_; }
    [[nodiscard]] size_t length() const noexcept { return cp_count_; }
    [[nodiscard]] size_t byte_size() const noexcept { return byte_size_; }
    [[nodiscard]] size_t capacity() const noexcept { return byte_capacity_; }
    [[nodiscard]] bool empty() const noexcept { return cp_count_ == 0; }
    [[nodiscard]] constexpr bool is_sso() const noexcept { return data_ == sso_buffer_; }
    [[nodiscard]] constexpr size_t sso_capacity() const noexcept { return SSO_CAPACITY; }

    // === 字节级访问 (与码点级 at/operator[] 互补) ===
    // 字节访问: 越界返回 '\0'
    [[nodiscard]] char byte_at(size_t byte_idx) const noexcept
    {
        if (byte_idx >= byte_size_ || !data_) return '\0';
        return data_[byte_idx];
    }
    // 字节访问 (带边界检查, 越界 std::abort, 与 std::string::at 语义对齐)
    [[nodiscard]] char at_byte(size_t byte_idx) const
    {
        if (byte_idx >= byte_size_ || !data_) std::abort();
        return data_[byte_idx];
    }
    // 字节级子串 (按字节范围, 不验证 UTF-8 边界; 调用者负责保证语义正确)
    [[nodiscard]] utf8pp byte_substr(size_t byte_pos, size_t byte_len = npos) const
    {
        if (byte_pos >= byte_size_ || !data_) return utf8pp();
        if (byte_len > byte_size_ - byte_pos) byte_len = byte_size_ - byte_pos;
        return utf8pp(data_ + byte_pos, byte_len);
    }
    // 字节偏移 → 码点索引 (公开版; 越界或非码点起点返回 npos)
    [[nodiscard]] size_t byte_to_cp_idx(size_t byte_idx) const noexcept
    {
        return byte_idx_to_cp_idx(byte_idx);
    }
    // 码点索引 → 字节偏移 (越界返回 byte_size_)
    [[nodiscard]] size_t cp_to_byte_idx(size_t cp_idx) const noexcept
    {
        if (cp_idx >= cp_count_) return byte_size_;
        return cp_offsets_[cp_idx];
    }

    void reserve(size_t byte_cap)
    {
        if (byte_cap > byte_capacity_) grow_byte_capacity(byte_cap);
    }

    [[nodiscard]] size_t max_size() const noexcept { return static_cast<size_t>(-1) / sizeof(char); }

    void reserve_exact(size_t byte_cap)
    {
        if (byte_cap > byte_capacity_) grow_byte_capacity(byte_cap);
    }

    // 码点偏移容量预留 (项目规范: 容量预留用 increase_capacity/reserve_exact, 此处补充码点级)
    void reserve_cp(size_t cp_cap)
    {
        if (cp_cap > cp_offsets_capacity_) grow_cp_capacity(cp_cap);
    }

    [[nodiscard]] size_t cp_capacity() const noexcept { return cp_offsets_capacity_; }

    void increase_capacity(size_t new_cap)
    {
        if (new_cap > byte_capacity_) grow_byte_capacity(new_cap);
        if (new_cap > cp_offsets_capacity_) grow_cp_capacity(new_cap);
    }

    void clear() noexcept
    {
        byte_size_ = 0;
        cp_count_ = 0;
        if (data_) data_[0] = '\0';
    }

    void shrink_to_fit()
    {
        // SSO 模式: data_/cp_offsets_ 指向栈数组, 无法也无需缩减
        if (is_sso()) return;
        // heap 模式: 内容可放入 SSO 时回退到 SSO
        if (byte_size_ <= SSO_CAPACITY)
        {
            char tmp_bytes[SSO_CAPACITY + 1];
            uint32_t tmp_cps[SSO_CAPACITY];
            std::memcpy(tmp_bytes, data_, byte_size_ + 1);
            if (cp_count_ > 0) std::memcpy(tmp_cps, cp_offsets_, cp_count_ * sizeof(uint32_t));
            utf8pp_free(data_);
            utf8pp_free(cp_offsets_);
            data_ = sso_buffer_;
            cp_offsets_ = sso_cp_offsets_;
            byte_capacity_ = SSO_CAPACITY;
            cp_offsets_capacity_ = SSO_CAPACITY;
            std::memcpy(sso_buffer_, tmp_bytes, byte_size_ + 1);
            if (cp_count_ > 0) std::memcpy(sso_cp_offsets_, tmp_cps, cp_count_ * sizeof(uint32_t));
            return;
        }
        // heap 模式: 缩减字节缓冲区到实际大小
        if (byte_capacity_ > byte_size_)
        {
            size_t cap = byte_size_ > 0 ? byte_size_ : 1;
            char* new_data = static_cast<char*>(utf8pp_alloc(cap + 1));
            if (!new_data) std::abort();
            if (byte_size_ > 0) std::memcpy(new_data, data_, byte_size_);
            new_data[byte_size_] = '\0';
            utf8pp_free(data_);
            data_ = new_data;
            byte_capacity_ = cap;
        }
        // heap 模式: 缩减码点偏移缓冲区到实际大小
        if (cp_offsets_capacity_ > cp_count_)
        {
            size_t cap = cp_count_ > 0 ? cp_count_ : 1;
            uint32_t* new_cp = static_cast<uint32_t*>(utf8pp_alloc(cap * sizeof(uint32_t)));
            if (!new_cp) std::abort();
            if (cp_count_ > 0) std::memcpy(new_cp, cp_offsets_, cp_count_ * sizeof(uint32_t));
            utf8pp_free(cp_offsets_);
            cp_offsets_ = new_cp;
            cp_offsets_capacity_ = cap;
        }
    }

    // === 访问 ===
    [[nodiscard]] char32_t at(size_t cp_idx) const noexcept
    {
        if (cp_idx >= cp_count_ || !data_) return U'\uFFFD';
        return char32_t(cp_at_byte(cp_offsets_[cp_idx]));
    }

    [[nodiscard]] char32_t operator[](size_t cp_idx) const noexcept
    {
        if (cp_idx >= cp_count_ || !data_) return U'\uFFFD';
        return char32_t(cp_at_byte(cp_offsets_[cp_idx]));
    }

    [[nodiscard]] char32_t front() const noexcept { return at(0); }
    [[nodiscard]] char32_t back() const noexcept { return at(cp_count_ > 0 ? cp_count_ - 1 : 0); }

    // === 字节访问 ===
    [[nodiscard]] const char* c_str() const noexcept { return data_ ? data_ : ""; }
    [[nodiscard]] const char* data() const noexcept { return data_ ? data_ : ""; }
    // C++17 风格: 非 const data(), 允许直接修改字节缓冲区
    // 注意: 修改后必须调用 rebuild_cp_offsets() 重建码点偏移缓存, 否则码点级接口行为未定义
    [[nodiscard]] char* data() noexcept { return data_; }
    // 直接修改 data() 后, 调用此函数重建码点偏移缓存 (若 byte_size_ 也变化需先更新)
    void rebuild_cp_offsets() noexcept { build_cp_offsets(); }
    // 重建并设置新的字节大小 (直接修改 data() 后的便捷接口)
    void rebuild(size_t new_byte_size) noexcept
    {
        byte_size_ = new_byte_size;
        if (data_) data_[byte_size_] = '\0';
        build_cp_offsets();
    }
    [[nodiscard]] std::string_view view() const noexcept { return std::string_view(data_ ? data_ : "", byte_size_); }
    [[nodiscard]] std::string_view binary_view() const noexcept { return std::string_view(data_ ? data_ : "", byte_size_); }
    [[nodiscard]] std::u8string_view u8view() const noexcept { return std::u8string_view(reinterpret_cast<const char8_t*>(data_ ? data_ : ""), byte_size_); }

    // === 迭代器 ===
    [[nodiscard]] const_iterator begin() const noexcept
    {
        const_iterator it(data_, data_ + byte_size_);
        it.set_begin(data_);
        return it;
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        const_iterator it(data_ + byte_size_, data_ + byte_size_);
        it.set_begin(data_);
        return it;
    }
    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    // === 字节迭代器接口 ===
    [[nodiscard]] const_byte_iterator byte_begin() const noexcept { return const_byte_iterator(data_ ? data_ : ""); }
    [[nodiscard]] const_byte_iterator byte_end() const noexcept { return const_byte_iterator((data_ ? data_ : "") + byte_size_); }
    [[nodiscard]] const_byte_iterator byte_cbegin() const noexcept { return byte_begin(); }
    [[nodiscard]] const_byte_iterator byte_cend() const noexcept { return byte_end(); }
    [[nodiscard]] const_byte_iterator rbyte_begin() const noexcept { return const_byte_iterator((data_ ? data_ : "") + byte_size_); }
    [[nodiscard]] const_byte_iterator rbyte_end() const noexcept { return const_byte_iterator(data_ ? data_ : ""); }
    [[nodiscard]] const_byte_iterator byte_crbegin() const noexcept { return rbyte_begin(); }
    [[nodiscard]] const_byte_iterator byte_crend() const noexcept { return rbyte_end(); }

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

    // === 修改 ===
    void push_back(char32_t cp) { insert(cp_count_, cp); }

    void append(const char* s) { append(s, s ? std::strlen(s) : 0); }

    void append(const char8_t* s)
    {
        const char* p = reinterpret_cast<const char*>(s);
        append(p, p ? std::strlen(p) : 0);
    }

    void append(const char* s, size_t byte_len)
    {
        if (byte_len == 0) return;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
        const uint8_t* end = p + byte_len;
        ensure_byte_capacity(byte_size_ + byte_len);
        // SSO 模式下 cp_offsets_ 容量 = SSO_CAPACITY, 足够容纳新码点 (≤ byte_size + byte_len ≤ SSO_CAPACITY)
        if (!is_sso())
        {
            ensure_cp_capacity(cp_count_ + byte_len);
        }

        while (p < end)
        {
            uint32_t cp = 0;
            size_t len = 0;
            (void)detail_utf8::utf8_decode_one(p, end, &cp, &len);
            cp_offsets_[cp_count_] = static_cast<uint32_t>(byte_size_);
            ++cp_count_;
            std::memcpy(data_ + byte_size_, p, len);
            byte_size_ += len;
            p += len;
        }
        data_[byte_size_] = '\0';
    }

    void append(const char32_t* s, size_t cp_count)
    {
        for (size_t i = 0; i < cp_count; ++i) push_back(s[i]);
    }

    void append(const utf8pp& other) { append(other.data_, other.byte_size_); }
    void append(std::string_view sv) { append(sv.data(), sv.size()); }
    void append(const std::string& s) { append(s.data(), s.size()); }
    void append(const std::u8string& s) { append(reinterpret_cast<const char*>(s.data()), s.size()); }
    void append(const std::u32string& s) { append(s.data(), s.size()); }
    void append(std::initializer_list<char32_t> il) { for (char32_t cp : il) push_back(cp); }
    void append(size_t n, char32_t cp) { append_cp(n, cp); }

    template <typename InputIt, typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    void append(InputIt first, InputIt last) { for (; first != last; ++first) push_back(static_cast<char32_t>(*first)); }

    utf8pp& operator+=(char32_t cp) { push_back(cp); return *this; }
    utf8pp& operator+=(const char* s) { append(s); return *this; }
    utf8pp& operator+=(const utf8pp& other) { append(other); return *this; }
    utf8pp& operator+=(std::string_view sv) { append(sv); return *this; }
    utf8pp& operator+=(const char8_t* s) { append(s); return *this; }
    utf8pp& operator+=(std::initializer_list<char32_t> il) { append(il); return *this; }

    utf8pp& insert(size_t cp_idx, char32_t cp)
    {
        if (cp_idx > cp_count_) cp_idx = cp_count_;

        uint8_t enc[4];
        size_t enc_len = 0;
        if (!detail_utf8::utf8_encode_one(static_cast<uint32_t>(cp), enc, &enc_len))
        {
            (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &enc_len);
        }

        ensure_byte_capacity(byte_size_ + enc_len);
        ensure_cp_capacity(cp_count_ + 1);

        size_t byte_idx = (cp_idx < cp_count_) ? cp_offsets_[cp_idx] : byte_size_;

        if (byte_idx < byte_size_)
        {
            std::memmove(data_ + byte_idx + enc_len, data_ + byte_idx, byte_size_ - byte_idx);
        }
        std::memcpy(data_ + byte_idx, enc, enc_len);
        byte_size_ += enc_len;
        data_[byte_size_] = '\0';

        if (cp_idx < cp_count_)
        {
            std::memmove(cp_offsets_ + cp_idx + 1, cp_offsets_ + cp_idx, (cp_count_ - cp_idx) * sizeof(uint32_t));
            for (size_t i = cp_idx + 1; i <= cp_count_; ++i) cp_offsets_[i] += static_cast<uint32_t>(enc_len);
        }
        cp_offsets_[cp_idx] = static_cast<uint32_t>(byte_idx);
        ++cp_count_;
        return *this;
    }

    void erase(size_t cp_idx, size_t n = 1)
    {
        if (cp_idx >= cp_count_) return;
        if (n > cp_count_ - cp_idx) n = cp_count_ - cp_idx;
        if (n == 0) return;

        size_t start_byte = cp_offsets_[cp_idx];
        size_t end_byte = (cp_idx + n < cp_count_) ? cp_offsets_[cp_idx + n] : byte_size_;
        size_t erased = end_byte - start_byte;

        if (end_byte < byte_size_)
        {
            std::memmove(data_ + start_byte, data_ + end_byte, byte_size_ - end_byte);
        }
        byte_size_ -= erased;
        data_[byte_size_] = '\0';

        if (cp_idx + n < cp_count_)
        {
            std::memmove(cp_offsets_ + cp_idx, cp_offsets_ + cp_idx + n, (cp_count_ - cp_idx - n) * sizeof(uint32_t));
            for (size_t i = cp_idx; i < cp_count_ - n; ++i) cp_offsets_[i] -= static_cast<uint32_t>(erased);
        }
        cp_count_ -= n;
    }

    // === insert 字符串重载 (返回 *this 支持链式调用, 与 std::string 对齐) ===
    utf8pp& insert(size_t cp_idx, const utf8pp& str) { insert_str(cp_idx, str); return *this; }
    utf8pp& insert(size_t cp_idx, const char* s) { if (s) insert_str(cp_idx, utf8pp(s)); return *this; }
    utf8pp& insert(size_t cp_idx, std::string_view sv) { insert_str(cp_idx, utf8pp(sv)); return *this; }
    utf8pp& insert(size_t cp_idx, const char* s, size_t byte_len) { insert_str(cp_idx, utf8pp(s, byte_len)); return *this; }
    // 子串插入: 从 str 的 pos2 开始取 n2 个码点插入 (与 std::string::insert(index, str, index_str, count) 对齐)
    utf8pp& insert(size_t cp_idx, const utf8pp& str, size_t pos2, size_t n2) { insert_str(cp_idx, str.substr(pos2, n2)); return *this; }
    // fill-insert: 在 cp_idx 处插入 n 个 cp (与 std::string::insert(pos, n, char) 对齐)
    utf8pp& insert(size_t cp_idx, size_t n, char32_t cp)
    {
        if (cp_idx > cp_count_) cp_idx = cp_count_;
        utf8pp tmp(n, cp);
        insert_str(cp_idx, tmp);
        return *this;
    }
    // initializer_list 插入
    utf8pp& insert(size_t cp_idx, std::initializer_list<char32_t> il)
    {
        if (cp_idx > cp_count_) cp_idx = cp_count_;
        utf8pp tmp(il);
        insert_str(cp_idx, tmp);
        return *this;
    }

    // === 迭代器版 insert/erase ===
    const_iterator insert(const_iterator pos, char32_t cp)
    {
        size_t idx = iterator_to_cp_idx(pos);
        insert(idx, cp);
        return const_iterator(data_ + cp_offsets_[idx], data_ + byte_size_);
    }

    const_iterator insert(const_iterator pos, size_t n, char32_t cp)
    {
        size_t idx = iterator_to_cp_idx(pos);
        for (size_t i = 0; i < n; ++i) insert(idx + i, cp);
        return const_iterator(data_ + cp_offsets_[idx], data_ + byte_size_);
    }

    template <typename InputIt>
    const_iterator insert(const_iterator pos, InputIt first, InputIt last)
    {
        size_t idx = iterator_to_cp_idx(pos);
        size_t i = 0;
        for (InputIt it = first; it != last; ++it, ++i)
        {
            insert(idx + i, *it);
        }
        return const_iterator(data_ + cp_offsets_[idx], data_ + byte_size_);
    }

    // 迭代器版 insert: 字符串 / string_view / initializer_list
    const_iterator insert(const_iterator pos, const utf8pp& str)
    {
        size_t idx = iterator_to_cp_idx(pos);
        insert_str(idx, str);
        return const_iterator(data_ + (idx < cp_count_ ? cp_offsets_[idx] : byte_size_), data_ + byte_size_);
    }
    const_iterator insert(const_iterator pos, const char* s)
    {
        return insert(pos, utf8pp(s));
    }
    const_iterator insert(const_iterator pos, const char* s, size_t byte_len)
    {
        return insert(pos, utf8pp(s, byte_len));
    }
    const_iterator insert(const_iterator pos, std::string_view sv)
    {
        return insert(pos, utf8pp(sv));
    }
    const_iterator insert(const_iterator pos, std::initializer_list<char32_t> il)
    {
        return insert(pos, utf8pp(il));
    }

    const_iterator erase(const_iterator pos)
    {
        size_t idx = iterator_to_cp_idx(pos);
        if (idx >= cp_count_) return end();
        erase(idx, 1);
        if (idx < cp_count_) return const_iterator(data_ + cp_offsets_[idx], data_ + byte_size_);
        return end();
    }

    const_iterator erase(const_iterator first, const_iterator last)
    {
        size_t idx_first = iterator_to_cp_idx(first);
        size_t idx_last = iterator_to_cp_idx(last);
        if (idx_first >= cp_count_) return end();
        if (idx_last > cp_count_) idx_last = cp_count_;
        size_t n = idx_last - idx_first;
        erase(idx_first, n);
        if (idx_first < cp_count_) return const_iterator(data_ + cp_offsets_[idx_first], data_ + byte_size_);
        return end();
    }

    [[nodiscard]] utf8pp substr(size_t pos, size_t cp_count = npos) const
    {
        if (pos >= cp_count_) return utf8pp();
        if (cp_count > cp_count_ - pos) cp_count = cp_count_ - pos;
        size_t start_byte = cp_offsets_[pos];
        size_t end_byte = (pos + cp_count < cp_count_) ? cp_offsets_[pos + cp_count] : byte_size_;
        return utf8pp(data_ + start_byte, end_byte - start_byte);
    }

    // === 重复填充 / 调整长度 ===
    void append_cp(size_t n, char32_t cp)
    {
        if (n == 0) return;
        uint8_t enc[4];
        size_t enc_len = 0;
        if (!detail_utf8::utf8_encode_one(static_cast<uint32_t>(cp), enc, &enc_len))
        {
            (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &enc_len);
        }
        ensure_byte_capacity(byte_size_ + n * enc_len);
        ensure_cp_capacity(cp_count_ + n);
        for (size_t i = 0; i < n; ++i)
        {
            cp_offsets_[cp_count_] = static_cast<uint32_t>(byte_size_);
            ++cp_count_;
            std::memcpy(data_ + byte_size_, enc, enc_len);
            byte_size_ += enc_len;
        }
        data_[byte_size_] = '\0';
    }

    void assign_cp(size_t n, char32_t cp)
    {
        clear();
        append_cp(n, cp);
    }

    void resize_cp(size_t n, char32_t cp = U'\0')
    {
        if (n < cp_count_)
        {
            erase(n, cp_count_ - n);
        }
        else if (n > cp_count_)
        {
            append_cp(n - cp_count_, cp);
        }
    }
    // std::string 兼容别名: resize(n) 用 U'\0' 填充, resize(n, cp) 用指定码点填充
    void resize(size_t n) { resize_cp(n, U'\0'); }
    void resize(size_t n, char32_t cp) { resize_cp(n, cp); }

    void pop_back()
    {
        if (cp_count_ > 0) erase(cp_count_ - 1, 1);
    }

    // === copy 拷贝到外部缓冲区 (返回拷贝字节数) ===
    size_t copy(char* buf, size_t n, size_t pos = 0) const
    {
        if (pos >= cp_count_ || !buf) return 0;
        size_t avail = cp_count_ - pos;
        if (n > avail) n = avail;
        size_t start_byte = cp_offsets_[pos];
        size_t end_byte = (pos + n < cp_count_) ? cp_offsets_[pos + n] : byte_size_;
        size_t byte_n = end_byte - start_byte;
        std::memcpy(buf, data_ + start_byte, byte_n);
        return byte_n;
    }

    // === 查找 (字节级, UTF-8 保证字节序 = 码点序) ===
    [[nodiscard]] size_t find(char32_t cp, size_t pos = 0) const noexcept
    {
        for (size_t i = pos; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_offsets_[i])) == cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find(const utf8pp& str, size_t pos = 0) const noexcept
    {
        if (str.cp_count_ == 0) return pos <= cp_count_ ? pos : npos;
        if (str.cp_count_ > cp_count_) return npos;

        for (size_t i = pos; i + str.cp_count_ <= cp_count_; ++i)
        {
            size_t start = cp_offsets_[i];
            size_t len = ((i + str.cp_count_ < cp_count_) ? cp_offsets_[i + str.cp_count_] : byte_size_) - start;
            if (len == str.byte_size_ && std::memcmp(data_ + start, str.data_, len) == 0)
            {
                return i;
            }
        }
        return npos;
    }

    [[nodiscard]] size_t find(const char* s, size_t pos = 0) const noexcept
    {
        return find(std::string_view(s ? s : "", s ? std::strlen(s) : 0), pos);
    }

    [[nodiscard]] size_t find(std::string_view sv, size_t pos = 0) const noexcept
    {
        // 字节级匹配: UTF-8 字节序 = 码点序, 合法 UTF-8 的匹配起始必为码点边界
        if (sv.empty()) return pos <= cp_count_ ? pos : npos;
        if (sv.size() > byte_size_) return npos;
        std::string_view self(data_ ? data_ : "", byte_size_);
        size_t byte_pos = (pos < cp_count_) ? cp_offsets_[pos] : byte_size_;
        size_t found = self.find(sv, byte_pos);
        if (found == std::string_view::npos) return npos;
        return byte_idx_to_cp_idx(found);
    }

    [[nodiscard]] size_t rfind(char32_t cp, size_t pos = npos) const noexcept
    {
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            if (char32_t(cp_at_byte(cp_offsets_[i])) == cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t rfind(const utf8pp& str, size_t pos = npos) const noexcept
    {
        if (str.cp_count_ == 0) return pos < cp_count_ ? pos : cp_count_;
        if (str.cp_count_ > cp_count_) return npos;
        if (pos > cp_count_ - str.cp_count_) pos = cp_count_ - str.cp_count_;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            size_t start = cp_offsets_[i];
            size_t len = ((i + str.cp_count_ < cp_count_) ? cp_offsets_[i + str.cp_count_] : byte_size_) - start;
            if (len == str.byte_size_ && std::memcmp(data_ + start, str.data_, len) == 0)
            {
                return i;
            }
        }
        return npos;
    }

    [[nodiscard]] size_t rfind(const char* s, size_t pos = npos) const noexcept
    {
        return rfind(std::string_view(s ? s : "", s ? std::strlen(s) : 0), pos);
    }

    [[nodiscard]] size_t rfind(std::string_view sv, size_t pos = npos) const noexcept
    {
        if (sv.empty()) return pos < cp_count_ ? pos : cp_count_;
        if (sv.size() > byte_size_) return npos;
        std::string_view self(data_ ? data_ : "", byte_size_);
        size_t byte_pos = (pos < cp_count_) ? cp_offsets_[pos] : byte_size_;
        size_t found = self.rfind(sv, byte_pos);
        if (found == std::string_view::npos) return npos;
        return byte_idx_to_cp_idx(found);
    }

    // === find_first_of / find_last_of / find_first_not_of / find_last_not_of ===
    [[nodiscard]] size_t find_first_of(char32_t cp, size_t pos = 0) const noexcept
    {
        for (size_t i = pos; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_offsets_[i])) == cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_first_of(const utf8pp& str, size_t pos = 0) const noexcept
    {
        for (size_t i = pos; i < cp_count_; ++i)
        {
            char32_t cur = char32_t(cp_at_byte(cp_offsets_[i]));
            for (size_t j = 0; j < str.cp_count_; ++j)
            {
                if (char32_t(str.cp_at_byte(str.cp_offsets_[j])) == cur) return i;
            }
        }
        return npos;
    }

    [[nodiscard]] size_t find_last_of(char32_t cp, size_t pos = npos) const noexcept
    {
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            if (char32_t(cp_at_byte(cp_offsets_[i])) == cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_last_of(const utf8pp& str, size_t pos = npos) const noexcept
    {
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            char32_t cur = char32_t(cp_at_byte(cp_offsets_[i]));
            for (size_t j = 0; j < str.cp_count_; ++j)
            {
                if (char32_t(str.cp_at_byte(str.cp_offsets_[j])) == cur) return i;
            }
        }
        return npos;
    }

    [[nodiscard]] size_t find_first_not_of(char32_t cp, size_t pos = 0) const noexcept
    {
        for (size_t i = pos; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_offsets_[i])) != cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_first_not_of(const utf8pp& str, size_t pos = 0) const noexcept
    {
        for (size_t i = pos; i < cp_count_; ++i)
        {
            char32_t cur = char32_t(cp_at_byte(cp_offsets_[i]));
            bool found = false;
            for (size_t j = 0; j < str.cp_count_; ++j)
            {
                if (char32_t(str.cp_at_byte(str.cp_offsets_[j])) == cur) { found = true; break; }
            }
            if (!found) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_last_not_of(char32_t cp, size_t pos = npos) const noexcept
    {
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            if (char32_t(cp_at_byte(cp_offsets_[i])) != cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_last_not_of(const utf8pp& str, size_t pos = npos) const noexcept
    {
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            char32_t cur = char32_t(cp_at_byte(cp_offsets_[i]));
            bool found = false;
            for (size_t j = 0; j < str.cp_count_; ++j)
            {
                if (char32_t(str.cp_at_byte(str.cp_offsets_[j])) == cur) { found = true; break; }
            }
            if (!found) return i;
        }
        return npos;
    }

    // find_*_of 的 const char* / string_view 重载 (委托 utf8pp 版本)
    [[nodiscard]] size_t find_first_of(const char* s, size_t pos = 0) const noexcept { return find_first_of(utf8pp(s), pos); }
    [[nodiscard]] size_t find_first_of(std::string_view sv, size_t pos = 0) const noexcept { return find_first_of(utf8pp(sv), pos); }
    [[nodiscard]] size_t find_last_of(const char* s, size_t pos = npos) const noexcept { return find_last_of(utf8pp(s), pos); }
    [[nodiscard]] size_t find_last_of(std::string_view sv, size_t pos = npos) const noexcept { return find_last_of(utf8pp(sv), pos); }
    [[nodiscard]] size_t find_first_not_of(const char* s, size_t pos = 0) const noexcept { return find_first_not_of(utf8pp(s), pos); }
    [[nodiscard]] size_t find_first_not_of(std::string_view sv, size_t pos = 0) const noexcept { return find_first_not_of(utf8pp(sv), pos); }
    [[nodiscard]] size_t find_last_not_of(const char* s, size_t pos = npos) const noexcept { return find_last_not_of(utf8pp(s), pos); }
    [[nodiscard]] size_t find_last_not_of(std::string_view sv, size_t pos = npos) const noexcept { return find_last_not_of(utf8pp(sv), pos); }

    // === find/rfind/find_*_of 三参 (s, pos, n) 重载 (与 std::string 对齐: 子串 s 前 n 字节) ===
    [[nodiscard]] size_t find(const char* s, size_t pos, size_t n) const noexcept
    {
        return find(std::string_view(s ? s : "", n), pos);
    }
    [[nodiscard]] size_t rfind(const char* s, size_t pos, size_t n) const noexcept
    {
        return rfind(std::string_view(s ? s : "", n), pos);
    }
    [[nodiscard]] size_t find_first_of(const char* s, size_t pos, size_t n) const noexcept
    {
        return find_first_of(utf8pp(s, n), pos);
    }
    [[nodiscard]] size_t find_last_of(const char* s, size_t pos, size_t n) const noexcept
    {
        return find_last_of(utf8pp(s, n), pos);
    }
    [[nodiscard]] size_t find_first_not_of(const char* s, size_t pos, size_t n) const noexcept
    {
        return find_first_not_of(utf8pp(s, n), pos);
    }
    [[nodiscard]] size_t find_last_not_of(const char* s, size_t pos, size_t n) const noexcept
    {
        return find_last_not_of(utf8pp(s, n), pos);
    }

    // === 比较 (字节级, UTF-8 保证字节序 = 码点序) ===
    [[nodiscard]] int compare(const utf8pp& other) const noexcept
    {
        size_t min_len = byte_size_ < other.byte_size_ ? byte_size_ : other.byte_size_;
        int r = std::memcmp(data_ ? data_ : "", other.data_ ? other.data_ : "", min_len);
        if (r != 0) return r;
        if (byte_size_ < other.byte_size_) return -1;
        if (byte_size_ > other.byte_size_) return 1;
        return 0;
    }

    [[nodiscard]] int compare(const char* s) const noexcept
    {
        size_t slen = s ? std::strlen(s) : 0;
        size_t min_len = byte_size_ < slen ? byte_size_ : slen;
        int r = std::memcmp(data_ ? data_ : "", s ? s : "", min_len);
        if (r != 0) return r;
        if (byte_size_ < slen) return -1;
        if (byte_size_ > slen) return 1;
        return 0;
    }

    [[nodiscard]] int compare(std::string_view sv) const noexcept
    {
        size_t min_len = byte_size_ < sv.size() ? byte_size_ : sv.size();
        int r = std::memcmp(data_ ? data_ : "", sv.data(), min_len);
        if (r != 0) return r;
        if (byte_size_ < sv.size()) return -1;
        if (byte_size_ > sv.size()) return 1;
        return 0;
    }

    // 子串比较: 从 pos 起 n 个码点与 s 比较
    [[nodiscard]] int compare(size_t pos, size_t n, const utf8pp& s) const
    {
        utf8pp sub = substr(pos, n);
        return sub.compare(s);
    }

    [[nodiscard]] int compare(size_t pos, size_t n, const char* s) const
    {
        utf8pp sub = substr(pos, n);
        return sub.compare(s);
    }

    [[nodiscard]] int compare(size_t pos, size_t n, std::string_view sv) const
    {
        utf8pp sub = substr(pos, n);
        return sub.compare(sv);
    }
    // 双区间比较: 本串 [pos1, pos1+n1) 与 other [pos2, pos2+n2) 比较
    [[nodiscard]] int compare(size_t pos1, size_t n1, const utf8pp& s, size_t pos2, size_t n2) const
    {
        utf8pp sub1 = substr(pos1, n1);
        utf8pp sub2 = s.substr(pos2, n2);
        return sub1.compare(sub2);
    }
    [[nodiscard]] int compare(size_t pos1, size_t n1, const char* s, size_t n2) const
    {
        utf8pp sub1 = substr(pos1, n1);
        utf8pp sub2(s, n2);
        return sub1.compare(sub2);
    }

    // 双子串替换: 本串 [pos1, pos1+n1) 替换为 other 的 [pos2, pos2+n2)
    utf8pp& replace(size_t pos1, size_t n1, const utf8pp& other, size_t pos2, size_t n2)
    {
        utf8pp sub = other.substr(pos2, n2);
        return replace(pos1, n1, sub);
    }
    utf8pp& replace(size_t pos1, size_t n1, const char* s, size_t pos2, size_t n2)
    {
        utf8pp sub(s ? s : "", s ? std::strlen(s) : 0);
        return replace(pos1, n1, sub.substr(pos2, n2));
    }

    bool operator==(const utf8pp& other) const noexcept { return compare(other) == 0; }
    bool operator!=(const utf8pp& other) const noexcept { return compare(other) != 0; }
    bool operator<(const utf8pp& other) const noexcept { return compare(other) < 0; }
    bool operator>(const utf8pp& other) const noexcept { return compare(other) > 0; }
    bool operator<=(const utf8pp& other) const noexcept { return compare(other) <= 0; }
    bool operator>=(const utf8pp& other) const noexcept { return compare(other) >= 0; }

    bool operator==(const char* s) const noexcept { return compare(s) == 0; }
    bool operator!=(const char* s) const noexcept { return compare(s) != 0; }
    bool operator<(const char* s) const noexcept { return compare(s) < 0; }
    bool operator>(const char* s) const noexcept { return compare(s) > 0; }
    bool operator<=(const char* s) const noexcept { return compare(s) <= 0; }
    bool operator>=(const char* s) const noexcept { return compare(s) >= 0; }

    bool operator==(std::string_view sv) const noexcept { return compare(sv) == 0; }
    bool operator!=(std::string_view sv) const noexcept { return compare(sv) != 0; }
    bool operator<(std::string_view sv) const noexcept { return compare(sv) < 0; }
    bool operator>(std::string_view sv) const noexcept { return compare(sv) > 0; }
    bool operator<=(std::string_view sv) const noexcept { return compare(sv) <= 0; }
    bool operator>=(std::string_view sv) const noexcept { return compare(sv) >= 0; }

    [[nodiscard]] auto operator<=>(const utf8pp& other) const noexcept
    {
        return compare(other) <=> 0;
    }
    [[nodiscard]] auto operator<=>(const char* s) const noexcept
    {
        return compare(s) <=> 0;
    }
    [[nodiscard]] auto operator<=>(std::string_view sv) const noexcept
    {
        return compare(sv) <=> 0;
    }

    // === 与 std::string / u8string / u32string / char32_t 互操作 ===
    [[nodiscard]] int compare(const std::string& s) const noexcept
    { return compare(std::string_view(s.data(), s.size())); }
    [[nodiscard]] int compare(const std::u8string& s) const noexcept
    { return compare(std::string_view(reinterpret_cast<const char*>(s.data()), s.size())); }
    [[nodiscard]] int compare(const std::u32string& s) const
    {
        utf8pp tmp(s);
        return compare(tmp);
    }
    [[nodiscard]] int compare(char32_t cp) const noexcept
    {
        if (cp_count_ == 0) return -1;
        if (cp_count_ == 1)
        {
            char32_t mine = char32_t(cp_at_byte(cp_offsets_[0]));
            if (mine < cp) return -1;
            if (mine > cp) return 1;
            return 0;
        }
        // 取首码点比较, 多余码点视为大于
        char32_t mine = char32_t(cp_at_byte(cp_offsets_[0]));
        if (mine < cp) return -1;
        if (mine > cp) return 1;
        return 1;
    }

    bool operator==(const std::string& s) const noexcept      { return compare(s) == 0; }
    bool operator!=(const std::string& s) const noexcept      { return compare(s) != 0; }
    bool operator< (const std::string& s) const noexcept      { return compare(s) <  0; }
    bool operator> (const std::string& s) const noexcept      { return compare(s) >  0; }
    bool operator<=(const std::string& s) const noexcept      { return compare(s) <= 0; }
    bool operator>=(const std::string& s) const noexcept      { return compare(s) >= 0; }

    bool operator==(const std::u8string& s) const noexcept    { return compare(s) == 0; }
    bool operator!=(const std::u8string& s) const noexcept    { return compare(s) != 0; }
    bool operator< (const std::u8string& s) const noexcept    { return compare(s) <  0; }
    bool operator> (const std::u8string& s) const noexcept    { return compare(s) >  0; }
    bool operator<=(const std::u8string& s) const noexcept    { return compare(s) <= 0; }
    bool operator>=(const std::u8string& s) const noexcept    { return compare(s) >= 0; }

    bool operator==(const std::u32string& s) const            { return compare(s) == 0; }
    bool operator!=(const std::u32string& s) const            { return compare(s) != 0; }
    bool operator< (const std::u32string& s) const            { return compare(s) <  0; }
    bool operator> (const std::u32string& s) const            { return compare(s) >  0; }
    bool operator<=(const std::u32string& s) const            { return compare(s) <= 0; }
    bool operator>=(const std::u32string& s) const            { return compare(s) >= 0; }

    bool operator==(char32_t cp) const noexcept               { return compare(cp) == 0; }
    bool operator!=(char32_t cp) const noexcept               { return compare(cp) != 0; }
    bool operator< (char32_t cp) const noexcept               { return compare(cp) <  0; }
    bool operator> (char32_t cp) const noexcept               { return compare(cp) >  0; }
    bool operator<=(char32_t cp) const noexcept               { return compare(cp) <= 0; }
    bool operator>=(char32_t cp) const noexcept               { return compare(cp) >= 0; }

    [[nodiscard]] auto operator<=>(const std::string& s) const noexcept    { return compare(s) <=> 0; }
    [[nodiscard]] auto operator<=>(const std::u8string& s) const noexcept  { return compare(s) <=> 0; }
    [[nodiscard]] auto operator<=>(const std::u32string& s) const          { return compare(s) <=> 0; }
    [[nodiscard]] auto operator<=>(char32_t cp) const noexcept             { return compare(cp) <=> 0; }

    // === 转换 ===
    [[nodiscard]] std::string to_std_string() const { return std::string(data_ ? data_ : "", byte_size_); }
    [[nodiscard]] std::u32string to_u32string() const
    {
        std::u32string result;
        result.reserve(cp_count_);
        for (size_t i = 0; i < cp_count_; ++i)
        {
            result.push_back(static_cast<char32_t>(cp_at_byte(cp_offsets_[i])));
        }
        return result;
    }
    [[nodiscard]] std::u8string to_u8string() const
    {
        return data_ ? std::u8string(reinterpret_cast<const char8_t*>(data_), byte_size_)
                     : std::u8string();
    }
    // 转换为 utf8_view (零拷贝, 指向内部缓冲区; 生命周期受 *this 限制)
    [[nodiscard]] utf8_view to_utf8_view() const noexcept
    {
        return utf8_view(data_ ? data_ : "", byte_size_);
    }

    // === 字符串 → 数字 (不抛异常, 转换失败返回 0/0.0; pos 输出消费的字符数) ===
    // 与 std::stoi/stol/stof 等价但无异常, base 仅整数有效 (2/8/10/16)
    [[nodiscard]] int to_int(size_t* pos = nullptr, int base = 10) const
    {
        return static_cast<int>(to_ll_internal(pos, base));
    }

    [[nodiscard]] long to_long(size_t* pos = nullptr, int base = 10) const
    {
        return static_cast<long>(to_ll_internal(pos, base));
    }

    [[nodiscard]] long long to_ll(size_t* pos = nullptr, int base = 10) const
    {
        return to_ll_internal(pos, base);
    }

    [[nodiscard]] unsigned long to_ulong(size_t* pos = nullptr, int base = 10) const
    {
        return static_cast<unsigned long>(to_ull_internal(pos, base));
    }

    [[nodiscard]] unsigned long long to_ull(size_t* pos = nullptr, int base = 10) const
    {
        return to_ull_internal(pos, base);
    }

    [[nodiscard]] float to_float(size_t* pos = nullptr) const
    {
        return static_cast<float>(to_double_internal(pos));
    }

    [[nodiscard]] double to_double(size_t* pos = nullptr) const
    {
        return to_double_internal(pos);
    }

    [[nodiscard]] long double to_long_double(size_t* pos = nullptr) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0.0L; }
        char* endp = nullptr;
        errno = 0;
        long double v = std::strtold(data_, &endp);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    // std 风格别名 (便于 std::string 迁移)
    [[nodiscard]] int         stoi(size_t* pos = nullptr, int base = 10) const { return to_int(pos, base); }
    [[nodiscard]] long        stol(size_t* pos = nullptr, int base = 10) const { return to_long(pos, base); }
    [[nodiscard]] long long   stoll(size_t* pos = nullptr, int base = 10) const { return to_ll(pos, base); }
    [[nodiscard]] unsigned long      stoul(size_t* pos = nullptr, int base = 10) const { return to_ulong(pos, base); }
    [[nodiscard]] unsigned long long stoull(size_t* pos = nullptr, int base = 10) const { return to_ull(pos, base); }
    [[nodiscard]] float       stof(size_t* pos = nullptr) const  { return to_float(pos); }
    [[nodiscard]] double      stod(size_t* pos = nullptr) const  { return to_double(pos); }
    [[nodiscard]] long double stold(size_t* pos = nullptr) const { return to_long_double(pos); }

    // === 解析 (返回 bool 表示是否完全转换, 输出值到 out) ===
    // 整数解析允许前导 +/- 与首尾空白; base∈{2,8,10,16}; 全串须为有效数字
    [[nodiscard]] bool parse_int(int& out, int base = 10) const noexcept
    {
        long long v = 0;
        if (!parse_ll(v, base)) return false;
        if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
            v > static_cast<long long>(std::numeric_limits<int>::max())) return false;
        out = static_cast<int>(v);
        return true;
    }

    [[nodiscard]] bool parse_long(long& out, int base = 10) const noexcept
    {
        long long v = 0;
        if (!parse_ll(v, base)) return false;
        if (v < static_cast<long long>(std::numeric_limits<long>::min()) ||
            v > static_cast<long long>(std::numeric_limits<long>::max())) return false;
        out = static_cast<long>(v);
        return true;
    }

    [[nodiscard]] bool parse_ll(long long& out, int base = 10) const noexcept
    {
        if (cp_count_ == 0) return false;
        // 跳过首尾空白
        size_t start = 0;
        size_t end = cp_count_;
        while (start < end && is_space_cp(cp_at_byte(cp_offsets_[start]))) ++start;
        while (end > start && is_space_cp(cp_at_byte(cp_offsets_[end - 1]))) --end;
        if (start >= end) return false;
        // 取出字节范围
        size_t bstart = cp_offsets_[start];
        size_t bend = (end < cp_count_) ? cp_offsets_[end] : byte_size_;
        // 构造临时 C 串 (源可能无 '\0', 复制到临时缓冲)
        char buf[64];
        char* p = buf;
        size_t len = bend - bstart;
        if (len >= sizeof(buf))
        {
            p = static_cast<char*>(std::malloc(len + 1));
            if (!p) return false;
        }
        std::memcpy(p, data_ + bstart, len);
        p[len] = '\0';
        char* endp = nullptr;
        errno = 0;
        long long v = std::strtoll(p, &endp, base);
        bool ok = (endp == p + len) && errno != ERANGE;
        if (p != buf) std::free(p);
        if (!ok) return false;
        out = v;
        return true;
    }

    [[nodiscard]] bool parse_ulong(unsigned long& out, int base = 10) const noexcept
    {
        unsigned long long v = 0;
        if (!parse_ull(v, base)) return false;
        if (v > static_cast<unsigned long long>(std::numeric_limits<unsigned long>::max())) return false;
        out = static_cast<unsigned long>(v);
        return true;
    }

    [[nodiscard]] bool parse_ull(unsigned long long& out, int base = 10) const noexcept
    {
        if (cp_count_ == 0) return false;
        size_t start = 0;
        size_t end = cp_count_;
        while (start < end && is_space_cp(cp_at_byte(cp_offsets_[start]))) ++start;
        while (end > start && is_space_cp(cp_at_byte(cp_offsets_[end - 1]))) --end;
        if (start >= end) return false;
        size_t bstart = cp_offsets_[start];
        size_t bend = (end < cp_count_) ? cp_offsets_[end] : byte_size_;
        char buf[64];
        char* p = buf;
        size_t len = bend - bstart;
        if (len >= sizeof(buf))
        {
            p = static_cast<char*>(std::malloc(len + 1));
            if (!p) return false;
        }
        std::memcpy(p, data_ + bstart, len);
        p[len] = '\0';
        char* endp = nullptr;
        errno = 0;
        unsigned long long v = std::strtoull(p, &endp, base);
        bool ok = (endp == p + len) && errno != ERANGE;
        if (p != buf) std::free(p);
        if (!ok) return false;
        out = v;
        return true;
    }

    [[nodiscard]] bool parse_float(float& out) const noexcept
    {
        double v = 0.0;
        if (!parse_double(v)) return false;
        out = static_cast<float>(v);
        return true;
    }

    [[nodiscard]] bool parse_double(double& out) const noexcept
    {
        if (cp_count_ == 0) return false;
        size_t start = 0;
        size_t end = cp_count_;
        while (start < end && is_space_cp(cp_at_byte(cp_offsets_[start]))) ++start;
        while (end > start && is_space_cp(cp_at_byte(cp_offsets_[end - 1]))) --end;
        if (start >= end) return false;
        size_t bstart = cp_offsets_[start];
        size_t bend = (end < cp_count_) ? cp_offsets_[end] : byte_size_;
        char buf[64];
        char* p = buf;
        size_t len = bend - bstart;
        if (len >= sizeof(buf))
        {
            p = static_cast<char*>(std::malloc(len + 1));
            if (!p) return false;
        }
        std::memcpy(p, data_ + bstart, len);
        p[len] = '\0';
        char* endp = nullptr;
        errno = 0;
        double v = std::strtod(p, &endp);
        bool ok = (endp == p + len) && errno != ERANGE;
        if (p != buf) std::free(p);
        if (!ok) return false;
        out = v;
        return true;
    }

    // long double 严格解析
    [[nodiscard]] bool parse_long_double(long double& out) const noexcept
    {
        if (cp_count_ == 0) return false;
        size_t start = 0;
        size_t end = cp_count_;
        while (start < end && is_space_cp(cp_at_byte(cp_offsets_[start]))) ++start;
        while (end > start && is_space_cp(cp_at_byte(cp_offsets_[end - 1]))) --end;
        if (start >= end) return false;
        size_t bstart = cp_offsets_[start];
        size_t bend = (end < cp_count_) ? cp_offsets_[end] : byte_size_;
        char buf[64];
        char* p = buf;
        size_t len = bend - bstart;
        if (len >= sizeof(buf))
        {
            p = static_cast<char*>(std::malloc(len + 1));
            if (!p) return false;
        }
        std::memcpy(p, data_ + bstart, len);
        p[len] = '\0';
        char* endp = nullptr;
        errno = 0;
        long double v = std::strtold(p, &endp);
        bool ok = (endp == p + len) && errno != ERANGE;
        if (p != buf) std::free(p);
        if (!ok) return false;
        out = v;
        return true;
    }

    // === 内容判断 ===
    // is_integer: 整数 (允许前导 +/-, 首尾空白; base 默认 10)
    [[nodiscard]] bool is_integer(int base = 10) const noexcept
    {
        long long v = 0;
        return parse_ll(v, base);
    }

    // is_float: 浮点数 (允许 +/-/小数点/指数, 首尾空白; 不接受 inf/nan 之外的纯整数也算浮点)
    [[nodiscard]] bool is_float() const noexcept
    {
        if (cp_count_ == 0) return false;
        double v = 0.0;
        return parse_double(v);
    }

    // is_number: 整数或浮点数
    [[nodiscard]] bool is_number() const noexcept
    {
        if (is_integer()) return true;
        return is_float();
    }

    // === 进制判断 (便捷别名) ===
    [[nodiscard]] bool is_hex() const noexcept    { return is_integer(16); }
    [[nodiscard]] bool is_binary() const noexcept { return is_integer(2); }
    [[nodiscard]] bool is_octal() const noexcept  { return is_integer(8); }

    // === 单码点字符分类 (公开静态; 完整 Unicode 覆盖 via unicode_data) ===
    // 覆盖: Latin/Greek/Cyrillic/Armenian/Hebrew/Arabic/Indic/CJK/Hangul/Hiragana/Katakana 等
    [[nodiscard]] static bool is_alpha(char32_t cp) noexcept
    {
        return unicode_data::is_alpha_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_digit(char32_t cp) noexcept
    {
        return unicode_data::is_digit_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_alnum(char32_t cp) noexcept
    {
        return unicode_data::is_alnum_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_space(char32_t cp) noexcept
    {
        return unicode_data::is_unicode_space(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_punct(char32_t cp) noexcept
    {
        // ASCII 标点: ! " # $ % & ' ( ) * + , - . / : ; < = > ? @ [ \ ] ^ _ ` { | } ~
        if ((cp >= U'!' && cp <= U'/') || (cp >= U':' && cp <= U'@') ||
            (cp >= U'[' && cp <= U'`') || (cp >= U'{' && cp <= U'~')) return true;
        // Latin-1 标点 (¡ ¢ £ ¤ ¥ ¦ § ¨ © ª « ¬ ­ ® ¯ ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½ ¾ ¿)
        if (cp >= U'\u00A1' && cp <= U'\u00BF') return true;
        // General Punctuation / CJK Symbols / 全角标点
        if (cp >= U'\u2000' && cp <= U'\u206F') return true;   // General Punctuation
        if (cp >= U'\u3000' && cp <= U'\u303F') return true;   // CJK Symbols and Punctuation
        if (cp >= U'\uFF01' && cp <= U'\uFF0F') return true;   // 全角 ASCII 标点
        if (cp >= U'\uFF1A' && cp <= U'\uFF20') return true;
        if (cp >= U'\uFF3B' && cp <= U'\uFF40') return true;
        if (cp >= U'\uFF5B' && cp <= U'\uFF65') return true;
        return false;
    }
    [[nodiscard]] static bool is_lower(char32_t cp) noexcept
    {
        return unicode_data::is_lower_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_upper(char32_t cp) noexcept
    {
        return unicode_data::is_upper_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_xdigit(char32_t cp) noexcept
    {
        return (cp >= U'0' && cp <= U'9') ||
               (cp >= U'A' && cp <= U'F') ||
               (cp >= U'a' && cp <= U'f');
    }
    [[nodiscard]] static bool is_cntrl(char32_t cp) noexcept
    {
        return cp < U' ' || cp == U'\x7F' ||
               (cp >= U'\u0080' && cp <= U'\u009F');
    }
    [[nodiscard]] static bool is_printable(char32_t cp) noexcept
    {
        if (is_cntrl(cp)) return false;
        if (cp == U'\uFFFD') return false;
        return cp >= U' ';
    }
    [[nodiscard]] static bool is_combining(char32_t cp) noexcept
    {
        return unicode_data::is_combining_mark(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_wide(char32_t cp) noexcept
    {
        return unicode_data::is_wide(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_zero_width(char32_t cp) noexcept
    {
        return unicode_data::is_zero_width(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_emoji(char32_t cp) noexcept
    {
        return unicode_data::is_extended_pictographic(static_cast<uint32_t>(cp));
    }
    // 单码点显示宽度 (0/1/2): 零宽=0, 全角/宽字符=2, 其他=1
    [[nodiscard]] static int cp_width(char32_t cp) noexcept
    {
        return unicode_data::cp_display_width(static_cast<uint32_t>(cp));
    }
    // 单码点大小写转换 (完整 Unicode via unicode_data)
    [[nodiscard]] static char32_t to_lower_cp(char32_t cp) noexcept
    {
        return static_cast<char32_t>(unicode_data::to_lower_cp(static_cast<uint32_t>(cp)));
    }
    [[nodiscard]] static char32_t to_upper_cp(char32_t cp) noexcept
    {
        return static_cast<char32_t>(unicode_data::to_upper_cp(static_cast<uint32_t>(cp)));
    }
    [[nodiscard]] static char32_t to_title_cp(char32_t cp) noexcept
    {
        return static_cast<char32_t>(unicode_data::to_title_cp(static_cast<uint32_t>(cp)));
    }

    // === Unicode Script 判断 (UAX #24) ===
    // 复用 unicode_data::script 枚举与查找表; 用于识别字符所属脚本 (中文/英文/日文等)
    using script = unicode_data::script;

    [[nodiscard]] static script script_of(char32_t cp) noexcept
    {
        return unicode_data::script_of(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_script(char32_t cp, script s) noexcept
    {
        return unicode_data::is_script(static_cast<uint32_t>(cp), s);
    }
    // 脚本名称 (用于输出/调试)
    [[nodiscard]] static const char* script_name(script s) noexcept
    {
        switch (s)
        {
            case script::unknown:     return "Unknown";
            case script::common:      return "Common";
            case script::inherited:   return "Inherited";
            case script::latin:       return "Latin";
            case script::greek:       return "Greek";
            case script::cyrillic:    return "Cyrillic";
            case script::armenian:    return "Armenian";
            case script::hebrew:      return "Hebrew";
            case script::arabic:      return "Arabic";
            case script::syriac:      return "Syriac";
            case script::thaana:      return "Thaana";
            case script::devanagari:  return "Devanagari";
            case script::bengali:     return "Bengali";
            case script::gurmukhi:    return "Gurmukhi";
            case script::gujarati:    return "Gujarati";
            case script::oriya:       return "Oriya";
            case script::tamil:       return "Tamil";
            case script::telugu:      return "Telugu";
            case script::kannada:     return "Kannada";
            case script::malayalam:   return "Malayalam";
            case script::sinhala:     return "Sinhala";
            case script::thai:        return "Thai";
            case script::lao:         return "Lao";
            case script::tibetan:     return "Tibetan";
            case script::myanmar:     return "Myanmar";
            case script::georgian:    return "Georgian";
            case script::hangul:      return "Hangul";
            case script::hiragana:    return "Hiragana";
            case script::katakana:    return "Katakana";
            case script::han:         return "Han";
            case script::ethiopic:    return "Ethiopic";
            case script::cherokee:    return "Cherokee";
            case script::canadian:    return "Canadian_Aboriginal";
            case script::ogham:       return "Ogham";
            case script::runic:       return "Runic";
            case script::tagalog:     return "Tagalog";
            case script::mongolian:   return "Mongolian";
            case script::cjk_ext:     return "CJK_Ext";
            case script::emoji_picto: return "Emoji";
        }
        return "Unknown";
    }

    // === 串级字符分类 (整串是否全部满足某分类) ===
    [[nodiscard]] bool is_all_alpha() const noexcept
    {
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_alpha(char32_t(cp_at_byte(cp_offsets_[i])))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_digit() const noexcept
    {
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_digit(char32_t(cp_at_byte(cp_offsets_[i])))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_alnum() const noexcept
    {
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_alnum(char32_t(cp_at_byte(cp_offsets_[i])))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_space() const noexcept
    {
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_space(char32_t(cp_at_byte(cp_offsets_[i])))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_xdigit() const noexcept
    {
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_xdigit(char32_t(cp_at_byte(cp_offsets_[i])))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_printable() const noexcept
    {
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_printable(char32_t(cp_at_byte(cp_offsets_[i])))) return false;
        return true;
    }

    // === 串级 Script 判断 ===
    // 返回首字符的脚本 (空串返回 unknown; 首字符为组合标记时为 inherited)
    [[nodiscard]] script script_of() const noexcept
    {
        if (cp_count_ == 0) return script::unknown;
        return unicode_data::script_of(cp_at_byte(cp_offsets_[0]));
    }
    // 整串是否全部属于指定脚本 (common/inherited 视为通配, 不影响判断; 空串返回 false)
    [[nodiscard]] bool is_all_script(script s) const noexcept
    {
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            script sc = unicode_data::script_of(cp_at_byte(cp_offsets_[i]));
            if (sc == script::common || sc == script::inherited) continue;
            if (sc != s) return false;
        }
        return true;
    }
    // 是否包含至少一个指定脚本的码点
    [[nodiscard]] bool contains_script(script s) const noexcept
    {
        for (size_t i = 0; i < cp_count_; ++i)
            if (unicode_data::script_of(cp_at_byte(cp_offsets_[i])) == s) return true;
        return false;
    }

    // === 前缀/后缀判断 ===
    [[nodiscard]] bool starts_with(char32_t cp) const noexcept
    {
        if (cp_count_ == 0) return false;
        return char32_t(cp_at_byte(cp_offsets_[0])) == cp;
    }

    [[nodiscard]] bool starts_with(const utf8pp& prefix) const noexcept
    {
        if (prefix.cp_count_ > cp_count_) return false;
        if (prefix.byte_size_ > byte_size_) return false;
        return std::memcmp(data_ ? data_ : "", prefix.data_ ? prefix.data_ : "", prefix.byte_size_) == 0;
    }

    [[nodiscard]] bool starts_with(const char* s) const noexcept
    {
        if (!s) return false;
        size_t slen = std::strlen(s);
        if (slen > byte_size_) return false;
        return std::memcmp(data_ ? data_ : "", s, slen) == 0;
    }

    [[nodiscard]] bool starts_with(std::string_view sv) const noexcept
    {
        if (sv.size() > byte_size_) return false;
        return std::memcmp(data_ ? data_ : "", sv.data(), sv.size()) == 0;
    }

    [[nodiscard]] bool ends_with(char32_t cp) const noexcept
    {
        if (cp_count_ == 0) return false;
        return char32_t(cp_at_byte(cp_offsets_[cp_count_ - 1])) == cp;
    }

    [[nodiscard]] bool ends_with(const utf8pp& suffix) const noexcept
    {
        if (suffix.cp_count_ > cp_count_) return false;
        if (suffix.byte_size_ > byte_size_) return false;
        return std::memcmp(data_ + byte_size_ - suffix.byte_size_, suffix.data_, suffix.byte_size_) == 0;
    }

    [[nodiscard]] bool ends_with(const char* s) const noexcept
    {
        if (!s) return false;
        size_t slen = std::strlen(s);
        if (slen > byte_size_) return false;
        return std::memcmp(data_ + byte_size_ - slen, s, slen) == 0;
    }

    [[nodiscard]] bool ends_with(std::string_view sv) const noexcept
    {
        if (sv.size() > byte_size_) return false;
        return std::memcmp(data_ + byte_size_ - sv.size(), sv.data(), sv.size()) == 0;
    }

    // === 包含判断 ===
    [[nodiscard]] bool contains(char32_t cp) const noexcept { return find(cp) != npos; }
    [[nodiscard]] bool contains(const utf8pp& str) const noexcept { return find(str) != npos; }
    [[nodiscard]] bool contains(const char* s) const noexcept { return find(s) != npos; }
    [[nodiscard]] bool contains(std::string_view sv) const noexcept { return find(sv) != npos; }

    // === 统计 ===
    [[nodiscard]] size_t count(char32_t cp) const noexcept
    {
        size_t n = 0;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_offsets_[i])) == cp) ++n;
        }
        return n;
    }

    [[nodiscard]] size_t count(const utf8pp& str) const noexcept
    {
        if (str.cp_count_ == 0) return 0;
        if (str.cp_count_ > cp_count_) return 0;
        size_t n = 0;
        size_t pos = 0;
        while (pos + str.cp_count_ <= cp_count_)
        {
            size_t found = find(str, pos);
            if (found == npos) break;
            ++n;
            pos = found + str.cp_count_;
        }
        return n;
    }

    [[nodiscard]] size_t count(const char* s) const noexcept { return count(utf8pp(s)); }
    [[nodiscard]] size_t count(std::string_view sv) const noexcept { return count(utf8pp(sv)); }

    // === 替换 ===
    utf8pp& replace(size_t pos, size_t n, const utf8pp& str)
    {
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, str);
        return *this;
    }

    utf8pp& replace(size_t pos, size_t n, const char* s)
    {
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(s));
        return *this;
    }

    utf8pp& replace(size_t pos, size_t n, std::string_view sv)
    {
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(sv));
        return *this;
    }
    // 替换为 C 字符串前 n2 字节 (与 std::string::replace(pos, n, s, n2) 对齐)
    utf8pp& replace(size_t pos, size_t n, const char* s, size_t n2)
    {
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(s, n2));
        return *this;
    }
    // fill-replace: 替换为 n2 个 cp (与 std::string::replace(pos, n, n2, char) 对齐)
    utf8pp& replace(size_t pos, size_t n, size_t n2, char32_t cp)
    {
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(n2, cp));
        return *this;
    }
    // initializer_list replace (与 std::string::replace(pos, count, initializer_list) 对齐)
    utf8pp& replace(size_t pos, size_t n, std::initializer_list<char32_t> il)
    {
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(il));
        return *this;
    }
    // 迭代器范围 replace (与 std::string 迭代器版对齐)
    utf8pp& replace(const_iterator first, const_iterator last, const utf8pp& str)
    {
        size_t pos = iterator_to_cp_idx(first);
        size_t end_idx = iterator_to_cp_idx(last);
        if (pos >= cp_count_) return *this;
        if (end_idx > cp_count_) end_idx = cp_count_;
        return replace(pos, end_idx - pos, str);
    }
    utf8pp& replace(const_iterator first, const_iterator last, const char* s)
    {
        size_t pos = iterator_to_cp_idx(first);
        size_t end_idx = iterator_to_cp_idx(last);
        if (pos >= cp_count_) return *this;
        if (end_idx > cp_count_) end_idx = cp_count_;
        return replace(pos, end_idx - pos, s);
    }
    utf8pp& replace(const_iterator first, const_iterator last, std::string_view sv)
    {
        size_t pos = iterator_to_cp_idx(first);
        size_t end_idx = iterator_to_cp_idx(last);
        if (pos >= cp_count_) return *this;
        if (end_idx > cp_count_) end_idx = cp_count_;
        return replace(pos, end_idx - pos, sv);
    }
    utf8pp& replace(const_iterator first, const_iterator last, const char* s, size_t n2)
    {
        size_t pos = iterator_to_cp_idx(first);
        size_t end_idx = iterator_to_cp_idx(last);
        if (pos >= cp_count_) return *this;
        if (end_idx > cp_count_) end_idx = cp_count_;
        return replace(pos, end_idx - pos, s, n2);
    }
    utf8pp& replace(const_iterator first, const_iterator last, size_t n2, char32_t cp)
    {
        size_t pos = iterator_to_cp_idx(first);
        size_t end_idx = iterator_to_cp_idx(last);
        if (pos >= cp_count_) return *this;
        if (end_idx > cp_count_) end_idx = cp_count_;
        return replace(pos, end_idx - pos, n2, cp);
    }
    template <typename InputIt, typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    utf8pp& replace(const_iterator first, const_iterator last, InputIt ifirst, InputIt ilast)
    {
        utf8pp tmp(ifirst, ilast);
        return replace(first, last, tmp);
    }

    utf8pp& replace_all(const utf8pp& old_str, const utf8pp& new_str)
    {
        if (old_str.cp_count_ == 0 || old_str.cp_count_ > cp_count_) return *this;
        size_t pos = 0;
        while (pos + old_str.cp_count_ <= cp_count_)
        {
            size_t found = find(old_str, pos);
            if (found == npos) break;
            erase(found, old_str.cp_count_);
            insert_str(found, new_str);
            pos = found + new_str.cp_count_;
        }
        return *this;
    }

    utf8pp& replace_all(const char* old_s, const char* new_s) { return replace_all(utf8pp(old_s), utf8pp(new_s)); }
    utf8pp& replace_all(std::string_view old_sv, std::string_view new_sv) { return replace_all(utf8pp(old_sv), utf8pp(new_sv)); }
    utf8pp& replace_all(const char* old_s, const utf8pp& new_str) { return replace_all(utf8pp(old_s), new_str); }
    utf8pp& replace_all(const utf8pp& old_str, const char* new_s) { return replace_all(old_str, utf8pp(new_s)); }

    utf8pp& replace_all(char32_t old_cp, char32_t new_cp)
    {
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_offsets_[i])) == old_cp)
            {
                replace_cp_at(i, new_cp);
            }
        }
        return *this;
    }

    // === trim ===
    utf8pp& trim_left()
    {
        size_t i = 0;
        while (i < cp_count_ && is_space_cp(char32_t(cp_at_byte(cp_offsets_[i]))))
        {
            ++i;
        }
        if (i > 0) erase(0, i);
        return *this;
    }

    utf8pp& trim_right()
    {
        size_t i = cp_count_;
        while (i > 0)
        {
            --i;
            if (!is_space_cp(char32_t(cp_at_byte(cp_offsets_[i])))) break;
        }
        if (i + 1 < cp_count_) erase(i + 1, cp_count_ - i - 1);
        return *this;
    }

    utf8pp& trim()
    {
        trim_left();
        trim_right();
        return *this;
    }

    [[nodiscard]] utf8pp trimmed() const { utf8pp t(*this); t.trim(); return t; }
    [[nodiscard]] utf8pp trimmed_left() const { utf8pp t(*this); t.trim_left(); return t; }
    [[nodiscard]] utf8pp trimmed_right() const { utf8pp t(*this); t.trim_right(); return t; }

    // === trim 谓词版 / 字符集版 ===
    // Pred 必须可调用为 bool(char32_t); 排除 utf8pp/const char*/string_view 等容器类型
    template <typename Pred, typename = std::enable_if_t<
        std::is_invocable_r_v<bool, Pred, char32_t>>>
    utf8pp& trim_left(Pred pred)
    {
        size_t i = 0;
        while (i < cp_count_ && pred(char32_t(cp_at_byte(cp_offsets_[i])))) ++i;
        if (i > 0) erase(0, i);
        return *this;
    }
    template <typename Pred, typename = std::enable_if_t<
        std::is_invocable_r_v<bool, Pred, char32_t>>>
    utf8pp& trim_right(Pred pred)
    {
        size_t i = cp_count_;
        while (i > 0)
        {
            --i;
            if (!pred(char32_t(cp_at_byte(cp_offsets_[i])))) break;
        }
        if (i + 1 < cp_count_) erase(i + 1, cp_count_ - i - 1);
        return *this;
    }
    template <typename Pred, typename = std::enable_if_t<
        std::is_invocable_r_v<bool, Pred, char32_t>>>
    utf8pp& trim(Pred pred) { trim_left(pred); trim_right(pred); return *this; }

    template <typename Pred, typename = std::enable_if_t<
        std::is_invocable_r_v<bool, Pred, char32_t>>>
    [[nodiscard]] utf8pp trimmed(Pred pred) const { utf8pp t(*this); t.trim(pred); return t; }
    template <typename Pred, typename = std::enable_if_t<
        std::is_invocable_r_v<bool, Pred, char32_t>>>
    [[nodiscard]] utf8pp trimmed_left(Pred pred) const { utf8pp t(*this); t.trim_left(pred); return t; }
    template <typename Pred, typename = std::enable_if_t<
        std::is_invocable_r_v<bool, Pred, char32_t>>>
    [[nodiscard]] utf8pp trimmed_right(Pred pred) const { utf8pp t(*this); t.trim_right(pred); return t; }

    // 字符集版: 去除两端所有出现在 chars 中的码点
    utf8pp& trim(const utf8pp& chars) { return trim([&](char32_t cp) { return chars.contains(cp); }); }
    utf8pp& trim_left(const utf8pp& chars) { return trim_left([&](char32_t cp) { return chars.contains(cp); }); }
    utf8pp& trim_right(const utf8pp& chars) { return trim_right([&](char32_t cp) { return chars.contains(cp); }); }
    utf8pp& trim(const char* chars) { return trim(utf8pp(chars ? chars : "")); }
    utf8pp& trim_left(const char* chars) { return trim_left(utf8pp(chars ? chars : "")); }
    utf8pp& trim_right(const char* chars) { return trim_right(utf8pp(chars ? chars : "")); }
    [[nodiscard]] utf8pp trimmed(const utf8pp& chars) const { utf8pp t(*this); t.trim(chars); return t; }
    [[nodiscard]] utf8pp trimmed_left(const utf8pp& chars) const { utf8pp t(*this); t.trim_left(chars); return t; }
    [[nodiscard]] utf8pp trimmed_right(const utf8pp& chars) const { utf8pp t(*this); t.trim_right(chars); return t; }

    // === 显示宽度 (East Asian Width, UAX #11) ===
    // 返回整串的显示宽度 (单元宽度列): 全角/CJK=2, 零宽=0, 其他=1
    [[nodiscard]] size_t display_width() const noexcept
    {
        size_t w = 0;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            w += static_cast<size_t>(unicode_data::cp_display_width(cp_at_byte(cp_offsets_[i])));
        }
        return w;
    }

    // === 对齐填充 (按显示宽度, East Asian Width 感知) ===
    // 注: 全角字符宽度 2, 零宽字符宽度 0, ASCII 宽度 1
    utf8pp& pad_left(size_t width, char32_t fill = U' ')
    {
        size_t cur_w = display_width();
        if (width <= cur_w) return *this;
        size_t add = width - cur_w;
        utf8pp padding(add, fill);
        insert_str(0, padding);
        return *this;
    }

    utf8pp& pad_right(size_t width, char32_t fill = U' ')
    {
        size_t cur_w = display_width();
        if (width <= cur_w) return *this;
        size_t add = width - cur_w;
        append_cp(add, fill);
        return *this;
    }

    utf8pp& center(size_t width, char32_t fill = U' ')
    {
        size_t cur_w = display_width();
        if (width <= cur_w) return *this;
        size_t total = width - cur_w;
        size_t left = total / 2;
        size_t right = total - left;
        if (right > 0) append_cp(right, fill);
        if (left > 0)
        {
            utf8pp padding(left, fill);
            insert_str(0, padding);
        }
        return *this;
    }

    [[nodiscard]] utf8pp padded_left(size_t width, char32_t fill = U' ') const { utf8pp t(*this); t.pad_left(width, fill); return t; }
    [[nodiscard]] utf8pp padded_right(size_t width, char32_t fill = U' ') const { utf8pp t(*this); t.pad_right(width, fill); return t; }
    [[nodiscard]] utf8pp centered(size_t width, char32_t fill = U' ') const { utf8pp t(*this); t.center(width, fill); return t; }

    // === 大小写转换 (完整 Unicode via unicode_data) ===
    utf8pp& to_lower()
    {
        for (size_t i = 0; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_offsets_[i]);
            char32_t lc = to_lower_cp(char32_t(cp));
            if (lc != char32_t(cp)) replace_cp_at(i, static_cast<uint32_t>(lc));
        }
        return *this;
    }

    utf8pp& to_upper()
    {
        for (size_t i = 0; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_offsets_[i]);
            char32_t uc = to_upper_cp(char32_t(cp));
            if (uc != char32_t(cp)) replace_cp_at(i, static_cast<uint32_t>(uc));
        }
        return *this;
    }

    // to_title: 每词首字符大写, 其余小写 (词以空白分隔)
    utf8pp& to_title()
    {
        bool new_word = true;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_offsets_[i]);
            if (is_space(char32_t(cp)))
            {
                new_word = true;
                continue;
            }
            if (new_word)
            {
                char32_t tc = to_title_cp(char32_t(cp));
                if (tc != char32_t(cp)) replace_cp_at(i, static_cast<uint32_t>(tc));
            }
            else
            {
                char32_t lc = to_lower_cp(char32_t(cp));
                if (lc != char32_t(cp)) replace_cp_at(i, static_cast<uint32_t>(lc));
            }
            new_word = false;
        }
        return *this;
    }

    // swapcase: 大小写互换 (完整 Unicode)
    utf8pp& swapcase()
    {
        for (size_t i = 0; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_offsets_[i]);
            if (is_upper(char32_t(cp)))
                replace_cp_at(i, static_cast<uint32_t>(to_lower_cp(char32_t(cp))));
            else if (is_lower(char32_t(cp)))
                replace_cp_at(i, static_cast<uint32_t>(to_upper_cp(char32_t(cp))));
        }
        return *this;
    }

    [[nodiscard]] utf8pp lowered() const  { utf8pp t(*this); t.to_lower();  return t; }
    [[nodiscard]] utf8pp uppered() const  { utf8pp t(*this); t.to_upper();  return t; }
    [[nodiscard]] utf8pp titled() const   { utf8pp t(*this); t.to_title();  return t; }
    [[nodiscard]] utf8pp swapcased() const { utf8pp t(*this); t.swapcase(); return t; }

    // === Unicode 规范化 (NFC/NFD/NFKC/NFKD) ===
    // 内部统一实现: decompose(canonical + compat + hangul) → CCC 排序 → (compose)
    // compat=true 时执行 NFKD/NFKC; compat=false 时执行 NFD/NFC
private:
    // 分解单个码点到 out; 返回新增码点数
    static void decompose_cp(uint32_t cp, dense<uint32_t>& out, bool compat) noexcept
    {
        // Hangul 算法分解 (优先, 不查表)
        uint32_t hg[3] = {0, 0, 0};
        if (unicode_data::hangul_decompose(cp, hg) > 0)
        {
            for (uint32_t i = 0; i < 3 && hg[i] != 0; ++i) out.push_back(hg[i]);
            return;
        }
        // 兼容性分解 (NFKD 表 + 全角算法)
        if (compat)
        {
            uint32_t fw = 0;
            if (unicode_data::nfkd_fullwidth_decompose(cp, fw))
            {
                out.push_back(fw);
                return;
            }
            uint32_t dt[4] = {0, 0, 0, 0};
            uint8_t dl = 0;
            if (unicode_data::nfkd_lookup(cp, dt, dl))
            {
                for (uint8_t i = 0; i < dl; ++i) out.push_back(dt[i]);
                return;
            }
        }
        // canonical 分解 (预组合 → base + combining, 递归一层)
        uint32_t base = 0, combining = 0;
        if (unicode_data::nfc_decompose_lookup(cp, base, combining))
        {
            decompose_cp(base, out, compat);
            out.push_back(combining);
            return;
        }
        out.push_back(cp);
    }

    // canonical ordering: 对每段连续 CCC>0 码点按 CCC 升序稳定排序
    static void canonical_order(dense<uint32_t>& v) noexcept
    {
        const size_t n = v.size();
        size_t i = 0;
        while (i < n)
        {
            if (unicode_data::canonical_combining_class(v[i]) == 0) { ++i; continue; }
            size_t j = i;
            while (j < n && unicode_data::canonical_combining_class(v[j]) > 0) ++j;
            for (size_t a = i + 1; a < j; ++a)
            {
                uint32_t key = v[a];
                uint8_t key_ccc = unicode_data::canonical_combining_class(key);
                size_t b = a;
                while (b > i && unicode_data::canonical_combining_class(v[b - 1]) > key_ccc)
                {
                    v[b] = v[b - 1];
                    --b;
                }
                v[b] = key;
            }
            i = j;
        }
    }

    // compose: 合并 starter + 组合序列 (含 Hangul 算法 + canonical 表 + blocking)
    // 原地 canonical composition (读写双指针, 零额外分配)
    // 合并只减不减码点数: write_idx <= read_idx 恒成立, 读取未处理数据不被覆盖
    static void compose_seq(dense<uint32_t>& out) noexcept
    {
        size_t write_idx = 0;
        size_t starter_idx = SIZE_MAX;
        uint8_t last_unmerged_ccc = 0;
        for (size_t read_idx = 0; read_idx < out.size(); ++read_idx)
        {
            uint32_t cur = out[read_idx];
            uint8_t cur_ccc = unicode_data::canonical_combining_class(cur);
            if (cur_ccc == 0)
            {
                // Hangul: LV + T → LVT (starter 是 LV 音节, cur 是 T)
                if (starter_idx != SIZE_MAX)
                {
                    uint32_t hg = unicode_data::hangul_compose(out[starter_idx], cur);
                    if (hg != 0)
                    {
                        out[starter_idx] = hg;
                        continue; // 吸收 cur
                    }
                }
                out[write_idx] = cur;
                ++write_idx;
                starter_idx = write_idx - 1;
                last_unmerged_ccc = 0;
                continue;
            }
            // 组合标记: 尝试 Hangul (L + V) 或 canonical 表合并
            bool merged = false;
            if (starter_idx != SIZE_MAX && last_unmerged_ccc < cur_ccc)
            {
                uint32_t hg = unicode_data::hangul_compose(out[starter_idx], cur);
                if (hg != 0)
                {
                    out[starter_idx] = hg;
                    merged = true;
                }
                else
                {
                    uint32_t composed = unicode_data::nfc_compose_lookup(out[starter_idx], cur);
                    if (composed != 0)
                    {
                        out[starter_idx] = composed;
                        merged = true;
                    }
                }
            }
            if (!merged)
            {
                out[write_idx] = cur;
                ++write_idx;
                last_unmerged_ccc = cur_ccc;
            }
        }
        // 截断多余元素 (uint32_t trivially destructible, pop_back 仅减 size)
        while (out.size() > write_idx) out.pop_back();
    }

    // 通用规范化内核
    utf8pp& normalize_impl(bool compose, bool compat)
    {
        if (cp_count_ == 0) return *this;
        // 步骤1: 分解 (canonical + compat + hangul)
        dense<uint32_t> decomp;
        decomp.reserve_exact(cp_count_ * 2);
        for (size_t i = 0; i < cp_count_; ++i)
        {
            decompose_cp(cp_at_byte(cp_offsets_[i]), decomp, compat);
        }
        // 步骤2: canonical ordering
        canonical_order(decomp);
        // 步骤3: compose (NFC/NFKC)
        if (compose)
        {
            compose_seq(decomp);
        }
        // 步骤4: 与原串比较, 相同则跳过重建
        bool changed = (decomp.size() != cp_count_);
        if (!changed)
        {
            for (size_t i = 0; i < decomp.size(); ++i)
            {
                if (decomp[i] != cp_at_byte(cp_offsets_[i])) { changed = true; break; }
            }
        }
        if (!changed) return *this;
        // 步骤5: 重建字符串
        clear();
        for (size_t k = 0; k < decomp.size(); ++k)
        {
            push_back(static_cast<char32_t>(decomp[k]));
        }
        return *this;
    }

public:
    utf8pp& to_nfc()  { return normalize_impl(true,  false); }
    utf8pp& to_nfd()  { return normalize_impl(false, false); }
    utf8pp& to_nfkc() { return normalize_impl(true,  true);  }
    utf8pp& to_nfkd() { return normalize_impl(false, true);  }

    [[nodiscard]] utf8pp nfc()  const { utf8pp t(*this); t.to_nfc();  return t; }
    [[nodiscard]] utf8pp nfd()  const { utf8pp t(*this); t.to_nfd();  return t; }
    [[nodiscard]] utf8pp nfkc() const { utf8pp t(*this); t.to_nfkc(); return t; }
    [[nodiscard]] utf8pp nfkd() const { utf8pp t(*this); t.to_nfkd(); return t; }

    // === 反转 (码点级) ===
    utf8pp& reverse()
    {
        if (cp_count_ <= 1) return *this;
        char* new_data = static_cast<char*>(utf8pp_alloc(byte_size_ + 1));
        if (!new_data) std::abort();
        size_t write_pos = 0;
        for (size_t i = cp_count_; i > 0; --i)
        {
            size_t idx = i - 1;
            size_t start = cp_offsets_[idx];
            size_t end = (idx + 1 < cp_count_) ? cp_offsets_[idx + 1] : byte_size_;
            size_t len = end - start;
            std::memcpy(new_data + write_pos, data_ + start, len);
            write_pos += len;
        }
        new_data[byte_size_] = '\0';
        // SSO 模式下 data_ 是 sso_buffer_ (栈), 不能 free; 切换到 heap 后 cp_offsets_ 也需迁堆
        bool was_sso = is_sso();
        if (!was_sso) utf8pp_free(data_);
        data_ = new_data;
        byte_capacity_ = byte_size_;
        if (was_sso)
        {
            size_t cp_cap = calc_cp_growth(cp_count_ > 0 ? cp_count_ : 16);
            uint32_t* new_cp = static_cast<uint32_t*>(utf8pp_alloc(cp_cap * sizeof(uint32_t)));
            if (!new_cp) std::abort();
            if (cp_count_ > 0) std::memcpy(new_cp, sso_cp_offsets_, cp_count_ * sizeof(uint32_t));
            cp_offsets_ = new_cp;
            cp_offsets_capacity_ = cp_cap;
        }
        // 重建反转后的码点偏移
        size_t acc = 0;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            size_t src_idx = cp_count_ - 1 - i;
            size_t src_start = cp_offsets_[src_idx];
            size_t src_end = (src_idx + 1 < cp_count_) ? cp_offsets_[src_idx + 1] : byte_size_;
            cp_offsets_[i] = static_cast<uint32_t>(acc);
            acc += (src_end - src_start);
        }
        return *this;
    }

    [[nodiscard]] utf8pp reversed() const { utf8pp t(*this); t.reverse(); return t; }

    // === format (printf 风格静态构造, 类内声明; 类外定义) ===
    [[nodiscard]] static utf8pp format(const char* fmt, ...);
    [[nodiscard]] static utf8pp vformat(const char* fmt, std::va_list ap);

    // === split (返回 dense<utf8pp>) ===
    [[nodiscard]] dense<utf8pp> split(char32_t delim) const
    {
        dense<utf8pp> result;
        if (cp_count_ == 0) return result;
        size_t start = 0;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_offsets_[i])) == delim)
            {
                result.push_back(substr(start, i - start));
                start = i + 1;
            }
        }
        result.push_back(substr(start));
        return result;
    }

    [[nodiscard]] dense<utf8pp> split(const utf8pp& delim) const
    {
        dense<utf8pp> result;
        if (cp_count_ == 0) return result;
        if (delim.cp_count_ == 0)
        {
            result.push_back(*this);
            return result;
        }
        size_t start = 0;
        size_t pos = 0;
        while (pos + delim.cp_count_ <= cp_count_)
        {
            size_t found = find(delim, pos);
            if (found == npos) break;
            result.push_back(substr(start, found - start));
            pos = found + delim.cp_count_;
            start = pos;
        }
        result.push_back(substr(start));
        return result;
    }

    [[nodiscard]] dense<utf8pp> split(const char* delim) const { return split(utf8pp(delim)); }
    [[nodiscard]] dense<utf8pp> split(std::string_view delim) const { return split(utf8pp(delim)); }

    // === split_view: 零拷贝分割, 返回 dense<utf8_view> (复用原字符串内存) ===
    [[nodiscard]] dense<utf8_view> split_view(char32_t delim) const
    {
        dense<utf8_view> result;
        if (cp_count_ == 0) return result;
        size_t start = 0;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_offsets_[i])) == delim)
            {
                result.push_back(utf8_view(data_ + cp_offsets_[start], cp_offsets_[i] - cp_offsets_[start]));
                start = i + 1;
            }
        }
        result.push_back(utf8_view(data_ + cp_offsets_[start], byte_size_ - cp_offsets_[start]));
        return result;
    }

    [[nodiscard]] dense<utf8_view> split_view(const utf8pp& delim) const
    {
        dense<utf8_view> result;
        if (cp_count_ == 0) return result;
        if (delim.cp_count_ == 0)
        {
            result.push_back(utf8_view(data_, byte_size_));
            return result;
        }
        size_t start = 0;
        size_t pos = 0;
        while (pos + delim.cp_count_ <= cp_count_)
        {
            size_t found = find(delim, pos);
            if (found == npos) break;
            result.push_back(utf8_view(data_ + cp_offsets_[start], cp_offsets_[found] - cp_offsets_[start]));
            pos = found + delim.cp_count_;
            start = pos;
        }
        result.push_back(utf8_view(data_ + cp_offsets_[start], byte_size_ - cp_offsets_[start]));
        return result;
    }

    [[nodiscard]] dense<utf8_view> split_view(const char* delim) const { return split_view(utf8pp(delim)); }
    [[nodiscard]] dense<utf8_view> split_view(std::string_view delim) const { return split_view(utf8pp(delim)); }

    // === 便捷重载: split 输出到 std::vector / std::array / 裸指针 ===
    void split_to(char32_t delim, std::vector<utf8pp>& out) const
    {
        dense<utf8pp> r = split(delim);
        out.clear();
        out.reserve(r.size());
        for (size_t i = 0; i < r.size(); ++i) out.push_back(r[i]);
    }

    void split_to(const utf8pp& delim, std::vector<utf8pp>& out) const
    {
        dense<utf8pp> r = split(delim);
        out.clear();
        out.reserve(r.size());
        for (size_t i = 0; i < r.size(); ++i) out.push_back(r[i]);
    }

    void split_to(const utf8pp& delim, utf8pp* out, size_t out_cap) const
    {
        dense<utf8pp> r = split(delim);
        size_t n = r.size() < out_cap ? r.size() : out_cap;
        for (size_t i = 0; i < n; ++i) out[i] = r[i];
    }
    // split_to 字符串分隔符重载 (委托 utf8pp 版本)
    void split_to(const char* delim, std::vector<utf8pp>& out) const { split_to(utf8pp(delim), out); }
    void split_to(std::string_view delim, std::vector<utf8pp>& out) const { split_to(utf8pp(delim), out); }
    void split_to(const char* delim, utf8pp* out, size_t out_cap) const { split_to(utf8pp(delim), out, out_cap); }
    void split_to(std::string_view delim, utf8pp* out, size_t out_cap) const { split_to(utf8pp(delim), out, out_cap); }

    // === join (静态方法) ===
    static utf8pp join(const dense<utf8pp>& parts, const utf8pp& delim)
    {
        utf8pp result;
        if (parts.size() == 0) return result;
        result.append(parts[0]);
        for (size_t i = 1; i < parts.size(); ++i)
        {
            result.append(delim);
            result.append(parts[i]);
        }
        return result;
    }

    static utf8pp join(const dense<utf8pp>& parts, char32_t delim)
    {
        utf8pp result;
        if (parts.size() == 0) return result;
        result.append(parts[0]);
        for (size_t i = 1; i < parts.size(); ++i)
        {
            result.push_back(delim);
            result.append(parts[i]);
        }
        return result;
    }

    template <size_t N>
    static utf8pp join(const std::array<utf8pp, N>& parts, const utf8pp& delim)
    {
        utf8pp result;
        if (N == 0) return result;
        result.append(parts[0]);
        for (size_t i = 1; i < N; ++i)
        {
            result.append(delim);
            result.append(parts[i]);
        }
        return result;
    }

    static utf8pp join(const std::vector<utf8pp>& parts, const utf8pp& delim)
    {
        utf8pp result;
        if (parts.empty()) return result;
        result.append(parts[0]);
        for (size_t i = 1; i < parts.size(); ++i)
        {
            result.append(delim);
            result.append(parts[i]);
        }
        return result;
    }

    static utf8pp join(const utf8pp* parts, size_t count, const utf8pp& delim)
    {
        utf8pp result;
        if (count == 0) return result;
        result.append(parts[0]);
        for (size_t i = 1; i < count; ++i)
        {
            result.append(delim);
            result.append(parts[i]);
        }
        return result;
    }

    // === 构造: 从范围 (std::vector / std::array / 裸指针) ===
    template <size_t N>
    explicit utf8pp(const std::array<utf8pp, N>& parts) : utf8pp(join(parts, utf8pp())) {}

    explicit utf8pp(const std::vector<utf8pp>& parts) : utf8pp(join(parts, utf8pp())) {}

    // === assign: 从范围 ===
    template <size_t N>
    utf8pp& assign(const std::array<utf8pp, N>& parts)
    {
        clear();
        for (size_t i = 0; i < N; ++i) append(parts[i]);
        return *this;
    }

    utf8pp& assign(const std::vector<utf8pp>& parts)
    {
        clear();
        for (size_t i = 0; i < parts.size(); ++i) append(parts[i]);
        return *this;
    }

    utf8pp& append(const std::vector<utf8pp>& parts)
    {
        for (size_t i = 0; i < parts.size(); ++i) append(parts[i]);
        return *this;
    }

    template <size_t N>
    utf8pp& append(const std::array<utf8pp, N>& parts)
    {
        for (size_t i = 0; i < N; ++i) append(parts[i]);
        return *this;
    }

    utf8pp& append(const utf8pp* parts, size_t count)
    {
        for (size_t i = 0; i < count; ++i) append(parts[i]);
        return *this;
    }

    // === append: std::span ===
    utf8pp& append(std::span<const utf8pp> parts)
    {
        for (size_t i = 0; i < parts.size(); ++i) append(parts[i]);
        return *this;
    }

    // === BOM ===
    [[nodiscard]] bool has_bom() const noexcept
    {
        return byte_size_ >= 3 && data_ && (uint8_t)data_[0] == 0xEF && (uint8_t)data_[1] == 0xBB && (uint8_t)data_[2] == 0xBF;
    }

    void strip_bom()
    {
        if (!has_bom()) return;
        std::memmove(data_, data_ + 3, byte_size_ - 3);
        byte_size_ -= 3;
        data_[byte_size_] = '\0';
        build_cp_offsets(); // 重建 cp_offsets_ (SSO 和 heap 均适用)
    }

    // === 校验 ===
    [[nodiscard]] bool valid() const noexcept
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        while (p < end)
        {
            uint32_t cp = 0;
            size_t len = 0;
            if (!detail_utf8::utf8_decode_one(p, end, &cp, &len)) return false;
            p += len;
        }
        return true;
    }

    size_t validate() const noexcept
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        size_t cp_idx = 0;
        while (p < end)
        {
            uint32_t cp = 0;
            size_t len = 0;
            if (!detail_utf8::utf8_decode_one(p, end, &cp, &len))
            {
                return cp_idx;
            }
            p += len;
            ++cp_idx;
        }
        return npos;
    }

private:
    char*       data_{nullptr};
    uint32_t    byte_size_{0};
    uint32_t    byte_capacity_{0};
    uint32_t*   cp_offsets_{nullptr};
    uint32_t    cp_count_{0};
    uint32_t    cp_offsets_capacity_{0};
    char        sso_buffer_[SSO_CAPACITY + 1]{};
    uint32_t    sso_cp_offsets_[SSO_CAPACITY]{}; // SSO 模式下 cp_offsets_ 指向此处

    size_t iterator_to_cp_idx(const const_iterator& it) const noexcept
    {
        const char* p = it.p_;
        if (!p || !data_) return cp_count_;
        size_t byte_idx = static_cast<size_t>(p - data_);
        if (byte_idx >= byte_size_) return cp_count_;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (cp_offsets_[i] == static_cast<uint32_t>(byte_idx)) return i;
        }
        return cp_count_;
    }

    // 字节偏移 → 码点索引 (向上取整: 返回首个 offset >= byte_idx 的码点索引; 越界返回 cp_count_)
    [[nodiscard]] size_t byte_idx_to_cp_idx_ceil(size_t byte_idx) const noexcept
    {
        if (byte_idx >= byte_size_) return cp_count_;
        size_t lo = 0, hi = cp_count_;
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (cp_offsets_[mid] < byte_idx) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    // data_ 始终以 '\0' 结尾, 可直接传给 strtoll/strtod
    long long to_ll_internal(size_t* pos, int base) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0; }
        char* endp = nullptr;
        errno = 0;
        long long v = std::strtoll(data_, &endp, base);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    unsigned long long to_ull_internal(size_t* pos, int base) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0; }
        char* endp = nullptr;
        errno = 0;
        unsigned long long v = std::strtoull(data_, &endp, base);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    double to_double_internal(size_t* pos) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0.0; }
        char* endp = nullptr;
        errno = 0;
        double v = std::strtod(data_, &endp);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    void release() noexcept
    {
        // 仅释放堆内存, SSO 缓冲区 (sso_buffer_/sso_cp_offsets_) 不能 free
        if (!is_sso() && data_) { utf8pp_free(data_); }
        if (cp_offsets_ && cp_offsets_ != sso_cp_offsets_) { utf8pp_free(cp_offsets_); }
        data_ = nullptr;
        byte_size_ = 0;
        byte_capacity_ = 0;
        cp_count_ = 0;
        cp_offsets_ = nullptr;
        cp_offsets_capacity_ = 0;
    }

    void insert_str(size_t cp_idx, const utf8pp& str)
    {
        if (str.cp_count_ == 0) return;
        if (cp_idx > cp_count_) cp_idx = cp_count_;

        ensure_byte_capacity(byte_size_ + str.byte_size_);
        ensure_cp_capacity(cp_count_ + str.cp_count_);

        size_t byte_idx = (cp_idx < cp_count_) ? cp_offsets_[cp_idx] : byte_size_;
        if (byte_idx < byte_size_)
        {
            std::memmove(data_ + byte_idx + str.byte_size_, data_ + byte_idx, byte_size_ - byte_idx);
        }
        std::memcpy(data_ + byte_idx, str.data_, str.byte_size_);
        byte_size_ += str.byte_size_;
        data_[byte_size_] = '\0';

        // 后移现有偏移并累加插入字节数
        if (cp_idx < cp_count_)
        {
            std::memmove(cp_offsets_ + cp_idx + str.cp_count_, cp_offsets_ + cp_idx,
                         (cp_count_ - cp_idx) * sizeof(uint32_t));
            for (size_t i = cp_idx + str.cp_count_; i < cp_count_ + str.cp_count_; ++i)
            {
                cp_offsets_[i] += static_cast<uint32_t>(str.byte_size_);
            }
        }
        // 填充新插入码点的偏移 (str.cp_offsets_ 始终有效)
        for (size_t i = 0; i < str.cp_count_; ++i)
        {
            cp_offsets_[cp_idx + i] = static_cast<uint32_t>(byte_idx + str.cp_offsets_[i]);
        }
        cp_count_ += str.cp_count_;
    }

    void replace_cp_at(size_t cp_idx, uint32_t new_cp)
    {
        if (cp_idx >= cp_count_) return;
        uint8_t new_enc[4];
        size_t new_len = 0;
        if (!detail_utf8::utf8_encode_one(new_cp, new_enc, &new_len))
        {
            (void)detail_utf8::utf8_encode_one(0xFFFD, new_enc, &new_len);
        }

        size_t byte_idx = cp_offsets_[cp_idx];
        size_t end_byte = (cp_idx + 1 < cp_count_) ? cp_offsets_[cp_idx + 1] : byte_size_;
        size_t old_len = end_byte - byte_idx;
        if (new_len == old_len)
        {
            std::memcpy(data_ + byte_idx, new_enc, new_len);
        }
        else
        {
            if (new_len < old_len)
            {
                std::memmove(data_ + byte_idx + new_len, data_ + end_byte, byte_size_ - end_byte);
            }
            else
            {
                ensure_byte_capacity(byte_size_ + (new_len - old_len));
                // ensure_byte_capacity 可能切换 SSO→heap, cp_offsets_ 始终有效, 重新取偏移
                byte_idx = cp_offsets_[cp_idx];
                end_byte = (cp_idx + 1 < cp_count_) ? cp_offsets_[cp_idx + 1] : byte_size_;
                std::memmove(data_ + byte_idx + new_len, data_ + end_byte, byte_size_ - end_byte);
            }
            std::memcpy(data_ + byte_idx, new_enc, new_len);
            int32_t diff = static_cast<int32_t>(new_len) - static_cast<int32_t>(old_len);
            byte_size_ += diff;
            data_[byte_size_] = '\0';
            for (size_t i = cp_idx + 1; i < cp_count_; ++i)
            {
                cp_offsets_[i] = static_cast<uint32_t>(static_cast<int32_t>(cp_offsets_[i]) + diff);
            }
        }
    }

    [[nodiscard]] static bool is_space_cp(uint32_t cp) noexcept
    {
        return unicode_data::is_unicode_space(cp);
    }

    // 3 级增长策略 (与 dense<T> 一致)
    [[nodiscard]] static constexpr size_t calc_byte_growth(size_t required) noexcept
    {
        if (required <= 64) return 64;
        if (required >= 65536) return required;
        size_t cap = 64;
        while (cap < required)
        {
            if (cap < 1024) cap *= 4;
            else if (cap < 65536) cap *= 4;
            else cap *= 4;
        }
        return cap;
    }

    [[nodiscard]] static constexpr size_t calc_cp_growth(size_t required) noexcept
    {
        if (required <= 16) return 16;
        if (required >= 65536) return required;
        size_t cap = 16;
        while (cap < required)
        {
            if (cap < 1024) cap *= 4;
            else if (cap < 65536) cap *= 4;
            else cap *= 4;
        }
        return cap;
    }

    void grow_byte_capacity(size_t new_cap)
    {
        size_t cap = calc_byte_growth(new_cap);
        char* new_data = static_cast<char*>(utf8pp_alloc(cap + 1));
        if (!new_data) std::abort();

        bool was_sso = is_sso();
        if (data_ && byte_size_ > 0)
        {
            std::memcpy(new_data, data_, byte_size_);
        }
        new_data[byte_size_] = '\0';
        if (!was_sso && data_) utf8pp_free(data_);
        data_ = new_data;
        byte_capacity_ = cap;

        // SSO → heap: cp_offsets_ 必须迁移到堆, 避免指向 sso_cp_offsets_ 的混合状态
        // (否则 swap heap↔heap 会交换栈指针, 导致悬空)
        if (was_sso)
        {
            size_t cp_cap = calc_cp_growth(cp_count_ > 0 ? cp_count_ : 16);
            uint32_t* new_cp = static_cast<uint32_t*>(utf8pp_alloc(cp_cap * sizeof(uint32_t)));
            if (!new_cp) std::abort();
            if (cp_count_ > 0)
            {
                std::memcpy(new_cp, sso_cp_offsets_, cp_count_ * sizeof(uint32_t));
            }
            cp_offsets_ = new_cp;
            cp_offsets_capacity_ = cp_cap;
        }
    }

    void grow_cp_capacity(size_t new_cap)
    {
        // 不能用 realloc: cp_offsets_ 可能指向 sso_cp_offsets_ (栈数组)
        size_t cap = calc_cp_growth(new_cap);
        uint32_t* new_p = static_cast<uint32_t*>(utf8pp_alloc(cap * sizeof(uint32_t)));
        if (!new_p) std::abort();
        if (cp_offsets_ && cp_count_ > 0)
        {
            std::memcpy(new_p, cp_offsets_, cp_count_ * sizeof(uint32_t));
        }
        if (cp_offsets_ != sso_cp_offsets_) { utf8pp_free(cp_offsets_); }
        cp_offsets_ = new_p;
        cp_offsets_capacity_ = cap;
    }

    // byte_capacity_ 表示数据容量 (不含 '\0', 缓冲区实际为 byte_capacity_+1)
    void ensure_byte_capacity(size_t needed) { if (needed > byte_capacity_) grow_byte_capacity(needed); }
    void ensure_cp_capacity(size_t needed) { if (needed > cp_offsets_capacity_) grow_cp_capacity(needed); }

    void init_from_utf8(const char* s, size_t byte_len)
    {
        // 统一路径: SSO 模式下 ensure_* 不扩容, cp_offsets_ 指向 sso_cp_offsets_
        ensure_byte_capacity(byte_len);
        std::memcpy(data_, s, byte_len);
        byte_size_ = byte_len;
        data_[byte_size_] = '\0';
        ensure_cp_capacity(byte_len);
        build_cp_offsets();
    }

    void init_from_char32(const char32_t* s, size_t cp_count)
    {
        // 统一路径: SSO 模式下 ensure_* 不扩容
        ensure_cp_capacity(cp_count);
        for (size_t i = 0; i < cp_count; ++i)
        {
            uint8_t enc[4];
            size_t len = 0;
            uint32_t cp = static_cast<uint32_t>(s[i]);
            if (!detail_utf8::utf8_encode_one(cp, enc, &len))
            {
                (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &len);
            }
            ensure_byte_capacity(byte_size_ + len);
            cp_offsets_[cp_count_] = static_cast<uint32_t>(byte_size_);
            ++cp_count_;
            std::memcpy(data_ + byte_size_, enc, len);
            byte_size_ += len;
        }
        if (byte_size_ > 0) data_[byte_size_] = '\0';
    }

    void build_cp_offsets() noexcept
    {
        cp_count_ = 0;
        if (byte_size_ == 0) return;
        // 预分配: cp_count_ <= byte_size (每码点至少 1 字节)
        // SSO 模式下 byte_size <= SSO_CAPACITY = cp_offsets_capacity_, 不扩容 (避免混合状态)
        ensure_cp_capacity(byte_size_);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        while (p < end)
        {
            cp_offsets_[cp_count_] = static_cast<uint32_t>(p - reinterpret_cast<const uint8_t*>(data_));
            ++cp_count_;
            uint8_t lead = *p;
            uint8_t seq = detail_utf8::k_utf8_seq_len[lead];
            if (seq == 0) seq = 1;
            p += seq;
            if (p > end) p = end;
        }
    }

    // 保留 cp_at_byte: 按字节偏移解码单个码点
    [[nodiscard]] uint32_t cp_at_byte(size_t byte_idx) const noexcept
    {
        uint32_t cp = 0;
        size_t len = 0;
        (void)detail_utf8::utf8_decode_one(
            reinterpret_cast<const uint8_t*>(data_) + byte_idx,
            reinterpret_cast<const uint8_t*>(data_) + byte_size_, &cp, &len);
        return cp;
    }

    // 字节偏移 → 码点索引 (二分 cp_offsets_, O(log n))
    [[nodiscard]] size_t byte_idx_to_cp_idx(size_t byte_idx) const noexcept
    {
        if (cp_count_ == 0) return npos;
        if (byte_idx >= byte_size_) return npos;
        size_t lo = 0, hi = cp_count_;
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (cp_offsets_[mid] < byte_idx) lo = mid + 1;
            else hi = mid;
        }
        return lo < cp_count_ && cp_offsets_[lo] == byte_idx ? lo : npos;
    }
};

// === 非成员 operator+ 系列 ===
[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const utf8pp& rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, char32_t rhs)
{
    utf8pp r(lhs);
    r.push_back(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(char32_t lhs, const utf8pp& rhs)
{
    utf8pp r;
    r.push_back(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const char* rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const char* lhs, const utf8pp& rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, std::string_view rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(std::string_view lhs, const utf8pp& rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const char8_t* rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

[[nodiscard]] inline utf8pp operator+(const char8_t* lhs, const utf8pp& rhs)
{
    utf8pp r(lhs);
    r.append(rhs);
    return r;
}

// === 与 std::string / u8string / u32string 互操作的 operator+ ===
[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const std::string& rhs)
{
    utf8pp r(lhs);
    r.append(rhs.data(), rhs.size());
    return r;
}
[[nodiscard]] inline utf8pp operator+(const std::string& lhs, const utf8pp& rhs)
{
    utf8pp r(lhs.data(), lhs.size());
    r.append(rhs);
    return r;
}
[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const std::u8string& rhs)
{
    utf8pp r(lhs);
    r.append(reinterpret_cast<const char*>(rhs.data()), rhs.size());
    return r;
}
[[nodiscard]] inline utf8pp operator+(const std::u8string& lhs, const utf8pp& rhs)
{
    utf8pp r(reinterpret_cast<const char*>(lhs.data()), lhs.size());
    r.append(rhs);
    return r;
}
[[nodiscard]] inline utf8pp operator+(const utf8pp& lhs, const std::u32string& rhs)
{
    utf8pp r(lhs);
    for (char32_t c : rhs) r.push_back(c);
    return r;
}
[[nodiscard]] inline utf8pp operator+(const std::u32string& lhs, const utf8pp& rhs)
{
    utf8pp r(lhs.data(), lhs.size());
    r.append(rhs);
    return r;
}

// === 流操作符 ===
inline std::ostream& operator<<(std::ostream& os, const utf8pp& s)
{
    os.write(s.data(), static_cast<std::streamsize>(s.byte_size()));
    return os;
}

inline std::istream& operator>>(std::istream& is, utf8pp& s)
{
    s.clear();
    char buf[4096];
    while (is.read(buf, sizeof(buf)) || is.gcount() > 0)
    {
        s.append(buf, static_cast<size_t>(is.gcount()));
    }
    return is;
}

// === getline: 从流读取一行 (分隔符默认 '\n', 遇到 EOF 或分隔符停止) ===
inline std::istream& getline(std::istream& is, utf8pp& s, char delim = '\n')
{
    s.clear();
    char c;
    bool any = false;
    while (is.get(c))
    {
        any = true;
        if (c == delim) break;
        s.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
    if (!any && !is.good())
    {
        // 完全未读到且流已结束: 设置失败位 (与 std::getline 一致)
        is.setstate(std::ios::failbit);
    }
    return is;
}

// === 非成员 swap ===
inline void swap(utf8pp& lhs, utf8pp& rhs) noexcept { lhs.swap(rhs); }

// === 数字 → utf8pp (类似 std::to_string, 无异常) ===
[[nodiscard]] inline utf8pp to_utf8pp(int v)
{
    char buf[16];
    int n = std::snprintf(buf, sizeof(buf), "%d", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(long v)
{
    char buf[24];
    int n = std::snprintf(buf, sizeof(buf), "%ld", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(long long v)
{
    char buf[24];
    int n = std::snprintf(buf, sizeof(buf), "%lld", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(unsigned v)
{
    char buf[16];
    int n = std::snprintf(buf, sizeof(buf), "%u", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(unsigned long v)
{
    char buf[24];
    int n = std::snprintf(buf, sizeof(buf), "%lu", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(unsigned long long v)
{
    char buf[24];
    int n = std::snprintf(buf, sizeof(buf), "%llu", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(float v)
{
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(double v)
{
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%g", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

[[nodiscard]] inline utf8pp to_utf8pp(long double v)
{
    char buf[48];
    int n = std::snprintf(buf, sizeof(buf), "%Lg", v);
    return (n > 0) ? utf8pp(buf, static_cast<size_t>(n)) : utf8pp();
}

// === utf8pp::format (printf 风格, 内部 vsnprintf; 不抛异常, 失败返回空串) ===
[[nodiscard]] inline utf8pp utf8pp_format(const char* fmt, ...)
{
    if (!fmt) return utf8pp();
    std::va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return utf8pp();
    if (static_cast<size_t>(n) < sizeof(buf)) return utf8pp(buf, static_cast<size_t>(n));
    // 大于栈缓冲: 动态分配
    std::string tmp(static_cast<size_t>(n), '\0');
    va_start(ap, fmt);
    int n2 = std::vsnprintf(tmp.data(), tmp.size() + 1, fmt, ap);
    va_end(ap);
    if (n2 < 0) return utf8pp();
    return utf8pp(tmp.data(), static_cast<size_t>(n2));
}

// utf8pp::format 的静态成员版本 (与 std::format 风格统一, 但用 printf 格式串)
// 用法: utf8pp::format("x=%d y=%s", 42, "hi")
[[nodiscard]] inline utf8pp utf8pp_vformat(const char* fmt, std::va_list ap)
{
    if (!fmt) return utf8pp();
    char buf[1024];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0) return utf8pp();
    if (static_cast<size_t>(n) < sizeof(buf)) return utf8pp(buf, static_cast<size_t>(n));
    std::string tmp(static_cast<size_t>(n), '\0');
    std::va_list ap2;
    va_copy(ap2, ap);
    int n2 = std::vsnprintf(tmp.data(), tmp.size() + 1, fmt, ap2);
    va_end(ap2);
    if (n2 < 0) return utf8pp();
    return utf8pp(tmp.data(), static_cast<size_t>(n2));
}

// utf8pp 类内静态方法的定义 (前向声明的 format/vformat)
[[nodiscard]] inline utf8pp utf8pp::format(const char* fmt, ...)
{
    if (!fmt) return utf8pp();
    std::va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return utf8pp();
    if (static_cast<size_t>(n) < sizeof(buf)) return utf8pp(buf, static_cast<size_t>(n));
    std::string tmp(static_cast<size_t>(n), '\0');
    va_start(ap, fmt);
    int n2 = std::vsnprintf(tmp.data(), tmp.size() + 1, fmt, ap);
    va_end(ap);
    if (n2 < 0) return utf8pp();
    return utf8pp(tmp.data(), static_cast<size_t>(n2));
}

[[nodiscard]] inline utf8pp utf8pp::vformat(const char* fmt, std::va_list ap)
{
    return utf8pp_vformat(fmt, ap);
}

// === 字面量运算符 ===
[[nodiscard]] inline utf8pp operator""_u8(const char* s, size_t len)
{
    return utf8pp(s, len);
}

[[nodiscard]] inline utf8pp operator""_u8(const char8_t* s, size_t len)
{
    return utf8pp(reinterpret_cast<const char*>(s), len);
}

[[nodiscard]] inline utf8pp operator""_utf8(const char* s, size_t len)
{
    return utf8pp(s, len);
}

[[nodiscard]] inline utf8pp operator""_utf8(const char8_t* s, size_t len)
{
    return utf8pp(reinterpret_cast<const char*>(s), len);
}

// === std::hash 特化 ===
namespace std {
template <>
struct hash<utf8pp>
{
    size_t operator()(const utf8pp& s) const noexcept
    {
        // FNV-1a 字节哈希 (分布优于朴素 *31)
        size_t h = 14695981039346656037ULL;
        const char* p = s.data();
        size_t n = s.byte_size();
        for (size_t i = 0; i < n; ++i)
        {
            h ^= static_cast<size_t>(static_cast<unsigned char>(p[i]));
            h *= 1099511628211ULL;
        }
        return h;
    }
};

// === std::swap 特化 (与 std::swap(std::string&, std::string&) 对齐) ===
template <>
inline void swap<utf8pp>(utf8pp& a, utf8pp& b) noexcept(noexcept(a.swap(b)))
{
    a.swap(b);
}
} // namespace std

// === 非成员对称比较运算符 (避免 const char*/std::string lhs 时构造临时 utf8pp) ===
[[nodiscard]] inline bool operator==(const char* lhs, const utf8pp& rhs) noexcept { return rhs == lhs; }
[[nodiscard]] inline bool operator==(std::string_view lhs, const utf8pp& rhs) noexcept { return rhs == lhs; }
[[nodiscard]] inline bool operator==(const std::string& lhs, const utf8pp& rhs) noexcept { return rhs == lhs; }
[[nodiscard]] inline bool operator==(char32_t lhs, const utf8pp& rhs) noexcept { return rhs == lhs; }

[[nodiscard]] inline bool operator!=(const char* lhs, const utf8pp& rhs) noexcept { return !(rhs == lhs); }
[[nodiscard]] inline bool operator!=(std::string_view lhs, const utf8pp& rhs) noexcept { return !(rhs == lhs); }
[[nodiscard]] inline bool operator!=(const std::string& lhs, const utf8pp& rhs) noexcept { return !(rhs == lhs); }
[[nodiscard]] inline bool operator!=(char32_t lhs, const utf8pp& rhs) noexcept { return !(rhs == lhs); }

[[nodiscard]] inline std::strong_ordering operator<=>(const char* lhs, const utf8pp& rhs) noexcept
{
    int c = rhs.compare(lhs);
    if (c < 0) return std::strong_ordering::greater;
    if (c > 0) return std::strong_ordering::less;
    return std::strong_ordering::equal;
}
[[nodiscard]] inline std::strong_ordering operator<=>(std::string_view lhs, const utf8pp& rhs) noexcept
{
    int c = rhs.compare(lhs);
    if (c < 0) return std::strong_ordering::greater;
    if (c > 0) return std::strong_ordering::less;
    return std::strong_ordering::equal;
}
[[nodiscard]] inline std::strong_ordering operator<=>(const std::string& lhs, const utf8pp& rhs) noexcept
{
    int c = rhs.compare(lhs);
    if (c < 0) return std::strong_ordering::greater;
    if (c > 0) return std::strong_ordering::less;
    return std::strong_ordering::equal;
}

// === C++20 std::erase / std::erase_if 自由函数 (与 std::erase(std::string, value) 对齐) ===
// 注意: utf8pp 的 erase(cp_idx, n) 是码点级; 这里提供单码点 erase 和谓词 erase_if
// 放在全局命名空间 (utf8pp 非 std 容器, 不放入 std; 但与 std::erase 用法一致)
[[nodiscard]] inline size_t erase(utf8pp& s, char32_t cp)
{
    size_t removed = 0;
    size_t pos = s.find(cp);
    while (pos != utf8pp::npos)
    {
        s.erase(pos, 1);
        ++removed;
        pos = s.find(cp, pos);
    }
    return removed;
}

template <typename Pred>
[[nodiscard]] inline size_t erase_if(utf8pp& s, Pred pred)
{
    size_t removed = 0;
    size_t i = 0;
    while (i < s.size())
    {
        if (pred(s[i])) { s.erase(i, 1); ++removed; }
        else ++i;
    }
    return removed;
}

// === std::formatter 特化 (C++20 std::format 支持, 可选) ===
// 支持: 原始输出 / width / fill / align / 大小写转换
// 用法:
//   std::format("{}",         utf8pp("Hello"))      → "Hello"
//   std::format("{:10}",      utf8pp("Hi"))         → "Hi        "
//   std::format("{:<10}",     utf8pp("Hi"))         → "Hi        "
//   std::format("{:>10}",     utf8pp("Hi"))         → "        Hi"
//   std::format("{:^10}",     utf8pp("Hi"))         → "    Hi    "
//   std::format("{:*<10}",    utf8pp("Hi"))         → "Hi********"
//   std::format("{:L}",       utf8pp("Hello"))      → "hello"  (小写)
//   std::format("{:U}",       utf8pp("Hello"))      → "HELLO"  (大写)
//   std::format("{:T}",       utf8pp("hello world"))→ "Hello World"
//   std::format("{:C10}",     utf8pp("你好"))       → "  你好  " (按 display_width 对齐)
#if __cpp_lib_format >= 201907L
#include <format>
template <>
struct std::formatter<utf8pp>
{
    char fill_{' '};
    char align_{0};     // 0 = 默认 (字符串左对齐), '<' '>' '^'
    int width_{-1};
    bool upper_{false}; // U: 大写
    bool lower_{false}; // L: 小写
    bool title_{false}; // T: 标题大小写
    bool disp_w_{false};// C: 按 display_width 对齐 (East Asian Width 感知)

    constexpr auto parse(std::format_parse_context& ctx)
    {
        auto it = ctx.begin();
        auto end = ctx.end();
        // [[fill]align][width]['.' precision][type]
        // 处理 fill+align
        if (it != end && (*it == '<' || *it == '>' || *it == '^'))
        {
            align_ = *it;
            ++it;
        }
        else if (it + 1 != end &&
                 *it != '<' && *it != '>' && *it != '^' && *it != '{' &&
                 (it[1] == '<' || it[1] == '>' || it[1] == '^'))
        {
            fill_ = *it;
            align_ = it[1];
            it += 2;
        }
        // width (数字)
        if (it != end && (*it >= '0' && *it <= '9'))
        {
            int w = 0;
            while (it != end && *it >= '0' && *it <= '9')
            {
                w = w * 10 + (*it - '0');
                ++it;
            }
            width_ = w;
        }
        // type 标志 (大小写 / display_width)
        while (it != end && *it != '}')
        {
            if (*it == 'U') upper_ = true;
            else if (*it == 'L') lower_ = true;
            else if (*it == 'T') title_ = true;
            else if (*it == 'C') disp_w_ = true;
            else if (*it == 's') { /* default */ }
            else std::abort(); // 非法格式说明符, 编程错误
            ++it;
        }
        return it;
    }

    auto format(const utf8pp& s, std::format_context& ctx) const
    {
        // 1. 先做大小写转换 (产生临时 utf8pp)
        utf8pp tmp;
        if (upper_)      tmp = utf8pp(s).to_upper();
        else if (lower_) tmp = utf8pp(s).to_lower();
        else if (title_) tmp = utf8pp(s).to_title();
        else             tmp = s;
        const utf8pp& out = tmp;

        // 2. 无 width, 直接输出字节
        if (width_ < 0)
        {
            return std::format_to(ctx.out(), "{}",
                std::string_view(out.data(), out.byte_size()));
        }

        // 3. 有 width, 按对齐方式填充
        size_t cur_w = disp_w_ ? out.display_width() : out.size();
        size_t target_w = static_cast<size_t>(width_);
        if (cur_w >= target_w)
        {
            return std::format_to(ctx.out(), "{}",
                std::string_view(out.data(), out.byte_size()));
        }

        // 计算填充字符 (仅支持 ASCII fill, 因 std::format spec fill 字符为单字节)
        char fill_buf[1] = {fill_};
        size_t fill_len = 1;
        size_t total_pad = target_w - cur_w;
        size_t left_pad = 0, right_pad = 0;
        char eff_align = (align_ == 0) ? '<' : align_; // 字符串默认左对齐
        if (eff_align == '<') right_pad = total_pad;
        else if (eff_align == '>') left_pad = total_pad;
        else { left_pad = total_pad / 2; right_pad = total_pad - left_pad; }

        auto out_it = ctx.out();
        for (size_t i = 0; i < left_pad; ++i)
            out_it = std::format_to(out_it, "{}", std::string_view(fill_buf, fill_len));
        out_it = std::format_to(out_it, "{}",
            std::string_view(out.data(), out.byte_size()));
        for (size_t i = 0; i < right_pad; ++i)
            out_it = std::format_to(out_it, "{}", std::string_view(fill_buf, fill_len));
        return out_it;
    }
};
#endif
