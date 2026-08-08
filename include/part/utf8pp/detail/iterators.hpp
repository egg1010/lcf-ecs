// 迭代器

    static constexpr size_t npos = static_cast<size_t>(-1);

    // 正向迭代器: trivial 指针包装器, 遍历 cp_cache_ (char32_t 数组)
    // 编译器可自动向量化 range-for (SSE2 4 元素/迭代, 与 u32string 同速)
    class const_iterator
    {
    public:
        using value_type = char32_t;
        using reference = char32_t;
        using pointer = const char32_t*;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::contiguous_iterator_tag;

        friend class utf8pp;

        const_iterator() noexcept = default;
        explicit const_iterator(const char32_t* p) noexcept : p_(p) {}
        // 兼容旧接口: 接收 const char* (reinterpret_cast 到 char32_t*)
        explicit const_iterator(const char* p) noexcept
            : p_(reinterpret_cast<const char32_t*>(p)) {}

        FORCE_INLINE const_iterator& operator++() noexcept { ++p_; return *this; }
        const_iterator operator++(int) noexcept { auto t = *this; ++p_; return t; }
        FORCE_INLINE const_iterator& operator--() noexcept { --p_; return *this; }
        const_iterator operator--(int) noexcept { auto t = *this; --p_; return t; }
        const_iterator& operator+=(difference_type n) noexcept { p_ += n; return *this; }
        const_iterator& operator-=(difference_type n) noexcept { p_ -= n; return *this; }

        [[nodiscard]] const_iterator operator+(difference_type n) const noexcept { return const_iterator(p_ + n); }
        [[nodiscard]] friend const_iterator operator+(difference_type n, const const_iterator& it) noexcept { return const_iterator(it.p_ + n); }
        [[nodiscard]] const_iterator operator-(difference_type n) const noexcept { return const_iterator(p_ - n); }
        [[nodiscard]] difference_type operator-(const const_iterator& o) const noexcept { return p_ - o.p_; }

        [[nodiscard]] FORCE_INLINE char32_t operator*() const noexcept { return *p_; }
        [[nodiscard]] char32_t operator[](difference_type n) const noexcept { return p_[n]; }

        [[nodiscard]] FORCE_INLINE bool operator==(const const_iterator& o) const noexcept { return p_ == o.p_; }
        [[nodiscard]] FORCE_INLINE bool operator!=(const const_iterator& o) const noexcept { return p_ != o.p_; }
        [[nodiscard]] bool operator<(const const_iterator& o) const noexcept { return p_ < o.p_; }
        [[nodiscard]] bool operator>(const const_iterator& o) const noexcept { return p_ > o.p_; }
        [[nodiscard]] bool operator<=(const const_iterator& o) const noexcept { return p_ <= o.p_; }
        [[nodiscard]] bool operator>=(const const_iterator& o) const noexcept { return p_ >= o.p_; }

        const char* ptr() const noexcept { return reinterpret_cast<const char*>(p_); }
        const char32_t* cp_ptr() const noexcept { return p_; }

    private:
        const char32_t* p_{nullptr};
    };

    class const_reverse_iterator
    {
    public:
        using value_type = char32_t;
        using reference = char32_t;
        using pointer = const char32_t*;
        using difference_type = std::ptrdiff_t;
        // 注: operator+= 为 O(n) 线性推进, 非真正的随机访问
        using iterator_category = std::bidirectional_iterator_tag;

        const_reverse_iterator() noexcept = default;
        const_reverse_iterator(const char* p, const char* begin, const char* end) noexcept
            : p_(p), begin_(begin), end_(end) {}
        const_reverse_iterator(const char* p, const char* begin, const char* end, uint8_t uniform_len) noexcept
            : p_(p), begin_(begin), end_(end), uniform_len_(uniform_len) {}

        FORCE_INLINE const_reverse_iterator& operator++() noexcept
        {
            // 均匀码点快速路径: 直接 p_ -= uniform_len_
            if (uniform_len_ != 0 && p_ > begin_)
            {
                p_ -= uniform_len_;
                return *this;
            }
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

        FORCE_INLINE const_reverse_iterator& operator--() noexcept
        {
            if (p_ < end_)
            {
                // 分支链优于查表 (避免 load-use 延迟)
                const uint8_t* q = reinterpret_cast<const uint8_t*>(p_);
                uint8_t lead = *q;
                size_t seq;
                if (lead < 0x80) seq = 1;
                else if (lead < 0xE0) seq = 2;
                else if (lead < 0xF0) seq = 3;
                else if (lead < 0xF8) seq = 4;
                else seq = 1;
                q += seq;
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

        [[nodiscard]] FORCE_INLINE char32_t operator*() const noexcept
        {
            // 均匀码点快速路径: 已知码点长度, 直接定位起始 (无需回退扫描)
            if (uniform_len_ != 0)
            {
                const uint8_t* q = reinterpret_cast<const uint8_t*>(p_) - uniform_len_;
                if (uniform_len_ == 1) return static_cast<char32_t>(q[0]);
                if (uniform_len_ == 3) [[likely]]
                {
                    return static_cast<char32_t>(
                        (static_cast<uint32_t>(q[0] & 0x0F) << 12)
                        | (static_cast<uint32_t>(q[1] & 0x3F) << 6)
                        | (q[2] & 0x3F));
                }
                if (uniform_len_ == 2)
                {
                    return static_cast<char32_t>(
                        (static_cast<uint32_t>(q[0] & 0x1F) << 6) | (q[1] & 0x3F));
                }
                return static_cast<char32_t>(
                    (static_cast<uint32_t>(q[0] & 0x07) << 18)
                    | (static_cast<uint32_t>(q[1] & 0x3F) << 12)
                    | (static_cast<uint32_t>(q[2] & 0x3F) << 6)
                    | (q[3] & 0x3F));
            }
            // 反向回退到码点起始, 无校验快速解码
            const uint8_t* q = reinterpret_cast<const uint8_t*>(p_);
            const uint8_t* b = reinterpret_cast<const uint8_t*>(begin_);
            --q;
            while (q > b && (*q & 0xC0) == 0x80) --q;
            uint8_t lead = *q;
            // 3 字节中文路径前置 [[likely]]
            if ((lead & 0xF0) == 0xE0) [[likely]]
            {
                return static_cast<char32_t>(
                    (static_cast<uint32_t>(lead & 0x0F) << 12)
                    | (static_cast<uint32_t>(q[1] & 0x3F) << 6)
                    | (q[2] & 0x3F));
            }
            // 纯 ASCII
            if (lead < 0x80) return static_cast<char32_t>(lead);
            // 2 字节
            if (lead < 0xE0)
            {
                return static_cast<char32_t>(
                    (static_cast<uint32_t>(lead & 0x1F) << 6) | (q[1] & 0x3F));
            }
            // 4 字节
            return static_cast<char32_t>(
                (static_cast<uint32_t>(lead & 0x07) << 18)
                | (static_cast<uint32_t>(q[1] & 0x3F) << 12)
                | (static_cast<uint32_t>(q[2] & 0x3F) << 6)
                | (q[3] & 0x3F));
        }

        [[nodiscard]] char32_t operator[](difference_type n) const noexcept
        {
            return *(*this + n);
        }

        [[nodiscard]] FORCE_INLINE bool operator==(const const_reverse_iterator& o) const noexcept { return p_ == o.p_; }
        [[nodiscard]] FORCE_INLINE bool operator!=(const const_reverse_iterator& o) const noexcept { return p_ != o.p_; }
        [[nodiscard]] bool operator<(const const_reverse_iterator& o) const noexcept { return p_ > o.p_; }
        [[nodiscard]] bool operator>(const const_reverse_iterator& o) const noexcept { return p_ < o.p_; }
        [[nodiscard]] bool operator<=(const const_reverse_iterator& o) const noexcept { return p_ >= o.p_; }
        [[nodiscard]] bool operator>=(const const_reverse_iterator& o) const noexcept { return p_ <= o.p_; }

    private:
        friend class utf8pp;
        const char* p_{nullptr};
        const char* begin_{nullptr};
        const char* end_{nullptr};
        uint8_t   uniform_len_{0};
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

    // === 字形簇迭代器 (UAX #29 简化版) ===
    // 字形簇 = 用户感知字符 (组合标记/ZWJ 序列不分割)
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

            // 规则 GB3: CR × LF 不分割
            if (prev_cp == 0x000D && next < end_)
            {
                uint32_t cp2 = 0; size_t len2 = 0;
                decode_one(next, end_, cp2, len2);
                if (cp2 == 0x000A) { next_ = next + (len2 ? len2 : 1); return; }
            }

            // 规则 GB6/GB7/GB8: Hangul LVT 序列
            // 码点范围 L=0x1100-0x115F/0xA960-0xA97F, V=0x1160-0x11A7/0xD7B0-0xD7FF,
            // 范围 T=0x11A8-0x11FF, LV/LVT=0xAC00-0xD7A3
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

                // 规则 GB4: Control (含 CR/LF) 后断开
                if (cp2 == 0x000D || cp2 == 0x000A ||
                    (cp2 < 0x20 && cp2 != 0x09 && cp2 != 0x0A && cp2 != 0x0D) ||
                    (cp2 >= 0x7F && cp2 <= 0x9F)) break;

                // 规则 GB6: L × (L|V|LV|LVT)
                if (is_hangul_l(prev_cp) &&
                    (is_hangul_l(cp2) || is_hangul_v(cp2) || is_hangul_lv_lvt(cp2)))
                { prev_cp = cp2; cur = next; next += len2; in_hangul = true; continue; }

                // 规则 GB7: (LV|V) × (V|T)
                if ((is_hangul_lv_lvt(prev_cp) || is_hangul_v(prev_cp)) &&
                    (is_hangul_v(cp2) || is_hangul_t(cp2)))
                {
                    // 仅前缀为 LV/LVT 时按 LV/LVT 规则
                    if (is_hangul_v(prev_cp) && is_hangul_v(cp2))
                    { prev_cp = cp2; cur = next; next += len2; continue; }
                    if (is_hangul_lv_lvt(prev_cp) && !is_hangul_lvt_only(prev_cp))
                    { prev_cp = cp2; cur = next; next += len2; continue; }
                    // 通用情况
                    { prev_cp = cp2; cur = next; next += len2; continue; }
                }

                // 规则 GB8: (LVT|T) × T
                if ((is_hangul_lvt_only(prev_cp) || is_hangul_t(prev_cp)) && is_hangul_t(cp2))
                { prev_cp = cp2; cur = next; next += len2; continue; }

                // 规则 GB9: × (Extend | ZWJ) 不在组合标记/ZWJ 前断开
                if (unicode_data::is_combining_mark(cp2) || cp2 == 0x200D)
                { prev_cp = cp2; cur = next; next += len2; continue; }

                // 规则 GB9a: × SpacingMark (Indic)
                if ((cp2 >= 0x0903 && cp2 <= 0x0939) ||
                    (cp2 >= 0x093E && cp2 <= 0x094D) ||
                    (cp2 >= 0x0951 && cp2 <= 0x0954))
                { prev_cp = cp2; cur = next; next += len2; continue; }

                // 规则 GB11: Extended_Pictographic Extend* ZWJ × Extended_Pictographic
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

                // 规则 GB999: 其他情况断开
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
