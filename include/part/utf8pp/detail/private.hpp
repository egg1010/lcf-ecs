// private.hpp - 私有成员

private:
    // 码点信息状态机 (惰性构建 + ASCII 快速路径)
    // cp_info_state_ 取值:
    //   0 = 未知 (首次访问需检测)
    //   1 = 纯 ASCII (码点数 = 字节数, 无需 cp_offsets_)
    //   2 = 已构建 cp_offsets_ (非 ASCII 或已显式构建)
    mutable uint8_t cp_info_state_ = 0;
    char*       data_{nullptr};
    uint32_t    byte_size_{0};
    uint32_t    byte_capacity_{0};
    uint32_t*   cp_offsets_{nullptr};       // 仅 cp_info_state_==2 时有效
    uint32_t    cp_count_{0};               // ASCII 时 = byte_size_
    uint32_t    cp_offsets_capacity_{0};
    char        sso_buffer_[SSO_CAPACITY + 1]{};

    // 惰性构建码点信息 (const 接口可用)
    void ensure_cp_info() const noexcept
    {
        if (cp_info_state_ != 0) return;
        if (byte_size_ == 0)
        {
            const_cast<utf8pp*>(this)->cp_info_state_ = 1;
            const_cast<utf8pp*>(this)->cp_count_ = 0;
            return;
        }
        // SIMD 风格 ASCII 检测: 8 字节一组检查高位
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        bool ascii = true;
        while (p + 8 <= end)
        {
            uint64_t chunk;
            std::memcpy(&chunk, p, 8);
            if (chunk & 0x8080808080808080ULL) { ascii = false; break; }
            p += 8;
        }
        while (ascii && p < end)
        {
            if (*p & 0x80) { ascii = false; break; }
            ++p;
        }
        if (ascii)
        {
            // ASCII 快速路径: 无需 cp_offsets_, 码点索引 = 字节索引
            const_cast<utf8pp*>(this)->cp_info_state_ = 1;
            const_cast<utf8pp*>(this)->cp_count_ = byte_size_;
        }
        else
        {
            // 非 ASCII: 构建偏移缓存
            const_cast<utf8pp*>(this)->build_cp_offsets();
            const_cast<utf8pp*>(this)->cp_info_state_ = 2;
        }
    }

    // 修改字节内容后调用, 失效码点信息
    void invalidate_cp_info() noexcept
    {
        if (cp_info_state_ == 2 && cp_offsets_ && cp_offsets_ != reinterpret_cast<uint32_t*>(sso_buffer_))
        {
            utf8pp_free(cp_offsets_);
        }
        cp_offsets_ = nullptr;
        cp_count_ = 0;
        cp_offsets_capacity_ = 0;
        cp_info_state_ = 0;
    }

    // 获取码点 i 的字节偏移 (ASCII 时 O(1) 直接返回)
    [[nodiscard]] uint32_t cp_byte_offset(size_t i) const noexcept
    {
        if (cp_info_state_ == 1) return static_cast<uint32_t>(i);
        return cp_offsets_[i];
    }

    // 获取码点总数 (触发惰性构建)
    [[nodiscard]] size_t cp_count_safe() const noexcept
    {
        ensure_cp_info();
        return cp_count_;
    }

    size_t iterator_to_cp_idx(const const_iterator& it) const noexcept
    {
        ensure_cp_info();
        const char* p = it.p_;
        if (!p || !data_) return cp_count_;
        size_t byte_idx = static_cast<size_t>(p - data_);
        if (byte_idx >= byte_size_) return cp_count_;
        // ASCII 快速路径: 码点索引 = 字节索引
        if (cp_info_state_ == 1) return byte_idx;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (cp_offsets_[i] == static_cast<uint32_t>(byte_idx)) return i;
        }
        return cp_count_;
    }

    // 字节偏移 → 码点索引 (向上取整: 返回首个 offset >= byte_idx 的码点索引; 越界返回 cp_count_)
    [[nodiscard]] size_t byte_idx_to_cp_idx_ceil(size_t byte_idx) const noexcept
    {
        ensure_cp_info();
        if (byte_idx >= byte_size_) return cp_count_;
        // ASCII 快速路径: 码点索引 = 字节索引
        if (cp_info_state_ == 1) return byte_idx;
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
        ensure_cp_info();
        char* endp = nullptr;
        errno = 0;
        long long v = std::strtoll(data_, &endp, base);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    unsigned long long to_ull_internal(size_t* pos, int base) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0; }
        ensure_cp_info();
        char* endp = nullptr;
        errno = 0;
        unsigned long long v = std::strtoull(data_, &endp, base);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    double to_double_internal(size_t* pos) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0.0; }
        ensure_cp_info();
        char* endp = nullptr;
        errno = 0;
        double v = std::strtod(data_, &endp);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    void release() noexcept
    {
        // 仅释放堆内存, SSO 缓冲区 (sso_buffer_) 不能 free
        if (!is_sso() && data_) { utf8pp_free(data_); }
        if (cp_offsets_ && cp_info_state_ == 2) { utf8pp_free(cp_offsets_); }
        data_ = nullptr;
        byte_size_ = 0;
        byte_capacity_ = 0;
        cp_count_ = 0;
        cp_offsets_ = nullptr;
        cp_offsets_capacity_ = 0;
        cp_info_state_ = 0;
    }

    void insert_str(size_t cp_idx, const utf8pp& str)
    {
        str.ensure_cp_info();
        if (str.cp_count_ == 0) return;
        ensure_cp_info();
        if (cp_idx > cp_count_) cp_idx = cp_count_;

        // 若当前为 ASCII 快速路径, 切换到已构建状态以增量维护
        if (cp_info_state_ == 1)
        {
            promote_ascii_to_offsets();
        }

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
        // 填充新插入码点的偏移
        if (str.cp_info_state_ == 1)
        {
            // str 为 ASCII: 偏移连续
            for (size_t i = 0; i < str.cp_count_; ++i)
            {
                cp_offsets_[cp_idx + i] = static_cast<uint32_t>(byte_idx + i);
            }
        }
        else
        {
            for (size_t i = 0; i < str.cp_count_; ++i)
            {
                cp_offsets_[cp_idx + i] = static_cast<uint32_t>(byte_idx + str.cp_offsets_[i]);
            }
        }
        cp_count_ += str.cp_count_;
    }

    void replace_cp_at(size_t cp_idx, uint32_t new_cp)
    {
        ensure_cp_info();
        if (cp_idx >= cp_count_) return;
        // ASCII 快速路径需提升为偏移缓存
        if (cp_info_state_ == 1) promote_ascii_to_offsets();

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

    // 3 级增长策略 (与 dense<T> 一致): 小 4x / 中 4x / 大 1.5x
    [[nodiscard]] static constexpr size_t calc_byte_growth(size_t required) noexcept
    {
        if (required <= 64) return 64;
        if (required >= 65536) return required + required / 2;
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
        if (required >= 65536) return required + required / 2;
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
        // cp_offsets_ 独立管理, 惰性模式下不随字节扩容迁移
    }

    void grow_cp_capacity(size_t new_cap)
    {
        size_t cap = calc_cp_growth(new_cap);
        uint32_t* new_p = static_cast<uint32_t*>(utf8pp_alloc(cap * sizeof(uint32_t)));
        if (!new_p) std::abort();
        if (cp_offsets_ && cp_count_ > 0)
        {
            std::memcpy(new_p, cp_offsets_, cp_count_ * sizeof(uint32_t));
        }
        if (cp_offsets_) { utf8pp_free(cp_offsets_); }
        cp_offsets_ = new_p;
        cp_offsets_capacity_ = cap;
    }

    // ASCII 快速路径提升为偏移缓存 (修改操作需要随机访问偏移时调用)
    void promote_ascii_to_offsets() noexcept
    {
        if (cp_info_state_ != 1) return;
        ensure_cp_capacity(byte_size_);
        for (size_t i = 0; i < byte_size_; ++i)
        {
            cp_offsets_[i] = static_cast<uint32_t>(i);
        }
        cp_info_state_ = 2;
    }

    // byte_capacity_ 表示数据容量 (不含 '\0', 缓冲区实际为 byte_capacity_+1)
    void ensure_byte_capacity(size_t needed) { if (needed > byte_capacity_) grow_byte_capacity(needed); }
    void ensure_cp_capacity(size_t needed) { if (needed > cp_offsets_capacity_) grow_cp_capacity(needed); }

    void init_from_utf8(const char* s, size_t byte_len)
    {
        ensure_byte_capacity(byte_len);
        std::memcpy(data_, s, byte_len);
        byte_size_ = byte_len;
        data_[byte_size_] = '\0';
        // 惰性: 不立即构建 cp_offsets_, 首次码点访问时检测 ASCII 或构建
        cp_info_state_ = 0;
    }

    void init_from_char32(const char32_t* s, size_t cp_count)
    {
        // char32_t 源必然非 ASCII (或全 ASCII 但走通用路径)
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
        // 已构建偏移, 标记为已构建 (可能全 ASCII, 但走偏移路径不影响正确性)
        cp_info_state_ = 2;
    }

    void build_cp_offsets() noexcept
    {
        cp_count_ = 0;
        if (byte_size_ == 0) return;
        ensure_cp_capacity(byte_size_);
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* p = base;
        const uint8_t* end = p + byte_size_;
        // SIMD 风格: 8 字节一组批量处理
        while (p + 8 <= end)
        {
            uint64_t chunk;
            std::memcpy(&chunk, p, 8);
            for (int i = 0; i < 8; ++i)
            {
                uint8_t b = static_cast<uint8_t>(chunk >> (i * 8));
                // 非 continuation 字节 = 码点首字节
                if ((b & 0xC0) != 0x80)
                {
                    cp_offsets_[cp_count_++] = static_cast<uint32_t>(p - base + i);
                }
            }
            p += 8;
        }
        // 尾部逐字节
        while (p < end)
        {
            if ((*p & 0xC0) != 0x80)
            {
                cp_offsets_[cp_count_++] = static_cast<uint32_t>(p - base);
            }
            ++p;
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

    // 字节偏移 → 码点索引 (ASCII 快速路径 O(1), 否则二分 O(log n))
    [[nodiscard]] size_t byte_idx_to_cp_idx(size_t byte_idx) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return npos;
        if (byte_idx >= byte_size_) return npos;
        if (cp_info_state_ == 1) return byte_idx;  // ASCII: 直接返回
        size_t lo = 0, hi = cp_count_;
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (cp_offsets_[mid] < byte_idx) lo = mid + 1;
            else hi = mid;
        }
        return lo < cp_count_ && cp_offsets_[lo] == byte_idx ? lo : npos;
    }