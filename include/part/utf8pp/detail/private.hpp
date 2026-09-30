// 私有成员

private:
    // 码点信息状态机 (惰性构建 + 纯 ASCII 快速路径)
    // 状态取值:
    //   0 = 未知 (首次访问需检测)
    //   1 = 纯 ASCII (码点数 = 字节数)
    //   2 = 已构建 cp_offsets_
    //   3 = 已计数但未构建偏移
    mutable uint8_t cp_info_state_ = 0;
    // 均匀码点字节长度 (0=未知, 1/2/3/4=所有码点等长)
    // 仅当 byte_size_ == cp_count_ * uniform_byte_len_ 时有效
    mutable uint8_t uniform_byte_len_ = 0;
    char*       data_{nullptr};
    uint32_t    byte_size_{0};
    uint32_t    byte_capacity_{0};
    uint32_t*   cp_offsets_{nullptr};       // 仅 cp_info_state_==2 时有效
    uint32_t    cp_count_{0};               // 纯 ASCII 时 = byte_size_
    uint32_t    cp_offsets_capacity_{0};
    // 预解码缓存: 全码点解码为 char32_t 数组, 迭代器遍历此数组
    // 首次 begin() 时构建, invalidate/release 时释放
    mutable char32_t* cp_cache_{nullptr};
    // 调用方仅可读取 [0, byte_size_] 范围内字节, 不依赖未读区域的零值
    char        sso_buffer_[SSO_CAPACITY + 1];

    // 纯 ASCII 块检测 (8 字节一组 SWAR, 尾部逐字节)
    [[nodiscard]] static bool is_ascii_block(const uint8_t* p, size_t n) noexcept
    {
        const uint8_t* end = p + n;
        while (p + 8 <= end)
        {
            uint64_t chunk;
            std::memcpy(&chunk, p, 8);
            if (chunk & 0x8080808080808080ULL) return false;
            p += 8;
        }
        while (p < end)
        {
            if (*p & 0x80) return false;
            ++p;
        }
        return true;
    }

    // 仅需 cp_count_ (size() 用): 不分配 cp_offsets_ 数组
    FORCE_INLINE void ensure_cp_count() const noexcept
    {
        if (cp_info_state_ != 0) return;
        ensure_cp_count_slow();
    }
    // SSE2 全量扫描, 仅首次访问执行
    NOINLINE void ensure_cp_count_slow() const noexcept
    {
        if (byte_size_ == 0)
        {
            const_cast<utf8pp*>(this)->cp_info_state_ = 1;
            const_cast<utf8pp*>(this)->cp_count_ = 0;
            const_cast<utf8pp*>(this)->uniform_byte_len_ = 1;
            return;
        }
        // 全量扫描精确计数
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        bool all_ascii = true;
        size_t count = detail_utf8::count_codepoints_and_ascii(p, end, all_ascii);
        if (all_ascii)
        {
            const_cast<utf8pp*>(this)->cp_count_ = byte_size_;
            const_cast<utf8pp*>(this)->cp_info_state_ = 1;
            const_cast<utf8pp*>(this)->uniform_byte_len_ = 1;
        }
        else
        {
            const_cast<utf8pp*>(this)->cp_count_ = static_cast<uint32_t>(count);
            const_cast<utf8pp*>(this)->cp_info_state_ = 3;
            const_cast<utf8pp*>(this)->detect_uniform_byte_len();
        }
    }

    // 需要完整 cp_offsets_ (at/substr/insert 用)
    FORCE_INLINE void ensure_cp_info() const noexcept
    {
        ensure_cp_count();
        if (cp_info_state_ == 3)
        {
            const_cast<utf8pp*>(this)->build_cp_offsets();
            const_cast<utf8pp*>(this)->cp_info_state_ = 2;
        }
    }

    // 预解码缓存: 全码点批量解码为 char32_t 数组
    NOINLINE void build_cp_cache() const noexcept
    {
        if (cp_cache_) return;
        ensure_cp_count();
        if (cp_count_ == 0 || !data_) return;
        size_t bytes = static_cast<size_t>(cp_count_) * sizeof(char32_t);
        char32_t* buf = static_cast<char32_t*>(utf8pp_alloc(bytes));
        if (!buf) std::abort();
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        if (uniform_byte_len_ == 1)
        {
            // 纯 ASCII
            for (size_t i = 0; i < cp_count_; ++i)
                buf[i] = char32_t(p[i]);
        }
        else if (uniform_byte_len_ == 3)
        {
            // 均匀 3 字节: 4 字节 load (含 1 字节 overlap)
            for (size_t i = 0; i < cp_count_; ++i)
            {
                uint32_t v;
                std::memcpy(&v, p + i * 3, 4);
                buf[i] = char32_t(((v & 0x0F) << 12) | ((v & 0x3F00) >> 2) | ((v & 0x3F0000) >> 16));
            }
        }
        else if (uniform_byte_len_ == 2)
        {
            // 均匀 2 字节
            for (size_t i = 0; i < cp_count_; ++i)
            {
                buf[i] = char32_t(
                    (static_cast<uint32_t>(p[i * 2] & 0x1F) << 6) | (p[i * 2 + 1] & 0x3F));
            }
        }
        else if (uniform_byte_len_ == 4)
        {
            // 均匀 4 字节
            for (size_t i = 0; i < cp_count_; ++i)
            {
                uint32_t v;
                std::memcpy(&v, p + i * 4, 4);
                buf[i] = char32_t(
                    ((v & 0x07) << 18) | ((v & 0x3F00) << 4) | ((v & 0x3F0000) >> 10) | ((v & 0x3F000000) >> 24));
            }
        }
        else
        {
            // 非均匀
            ensure_cp_info();
            for (size_t i = 0; i < cp_count_; ++i)
                buf[i] = char32_t(cp_at_byte_unchecked(cp_offsets_[i]));
        }
        const_cast<utf8pp*>(this)->cp_cache_ = buf;
    }

    // 仅失效预解码缓存 (cp_cache_), 保留 cp_offsets_/cp_count_/state
    // 必须在 cp_count_ 变更前调用 (free 大小依赖当前 cp_count_)
    void invalidate_cp_cache() noexcept
    {
        if (cp_cache_) { utf8pp_free(cp_cache_, static_cast<size_t>(cp_count_) * sizeof(char32_t)); cp_cache_ = nullptr; }
    }

    // 修改字节内容后调用, 失效码点信息
    void invalidate_cp_info() noexcept
    {
        // 状态 2 时 cp_offsets_ 可能已分配; 状态 1 时无偏移
        if (cp_info_state_ == 2 && cp_offsets_ && cp_offsets_ != reinterpret_cast<uint32_t*>(sso_buffer_))
        {
            utf8pp_free(cp_offsets_, static_cast<size_t>(cp_offsets_capacity_) * sizeof(uint32_t));
        }
        if (cp_cache_) { utf8pp_free(cp_cache_, static_cast<size_t>(cp_count_) * sizeof(char32_t)); cp_cache_ = nullptr; }
        cp_offsets_ = nullptr;
        cp_count_ = 0;
        cp_offsets_capacity_ = 0;
        cp_info_state_ = 0;
        uniform_byte_len_ = 0;
    }

    // 失效码点布局但保留 cp_count_ (用于 reverse/replace_all 等仅重排内容的操作)
    // 调用者负责确保 cp_count_ 在调用前已正确更新
    void invalidate_cp_layout() noexcept
    {
        if (cp_info_state_ == 2 && cp_offsets_ && cp_offsets_ != reinterpret_cast<uint32_t*>(sso_buffer_))
        {
            utf8pp_free(cp_offsets_, static_cast<size_t>(cp_offsets_capacity_) * sizeof(uint32_t));
        }
        if (cp_cache_) { utf8pp_free(cp_cache_, static_cast<size_t>(cp_count_) * sizeof(char32_t)); cp_cache_ = nullptr; }
        cp_offsets_ = nullptr;
        cp_offsets_capacity_ = 0;
        // 保留 cp_count_, 设 state=3
        if (cp_info_state_ != 0) cp_info_state_ = 3;
        uniform_byte_len_ = 0;
    }

    // 精确验证 cp_offsets_ 是否为等差数列 i*avg (SSE2 4 项/迭代)
    [[nodiscard]] bool check_offsets_uniform(size_t avg) const noexcept
    {
        size_t i = 0;
#if LCF_UTF8_HAS_SSE2
        const __m128i step = _mm_set1_epi32(static_cast<int>(4 * avg));
        __m128i cur = _mm_setr_epi32(0, static_cast<int>(avg),
                                     static_cast<int>(2 * avg), static_cast<int>(3 * avg));
        for (; i + 4 <= cp_count_; i += 4)
        {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(cp_offsets_ + i));
            if (_mm_movemask_epi8(_mm_cmpeq_epi32(v, cur)) != 0xFFFF) return false;
            cur = _mm_add_epi32(cur, step);
        }
#endif
        for (; i < cp_count_; ++i)
        {
            if (cp_offsets_[i] != static_cast<uint32_t>(i * avg)) return false;
        }
        return true;
    }

    // 尾部追加块后维护均匀性 (k 码点共 bytes 字节, O(1))
    void update_uniform_append(size_t new_cps, size_t new_bytes) noexcept
    {
        if (cp_info_state_ == 1)
        {
            if (new_bytes != new_cps)
            {
                cp_info_state_ = 3;
                uniform_byte_len_ = 0;
            }
            return;
        }
        if (uniform_byte_len_ == 0) return;
        if (new_bytes != new_cps * uniform_byte_len_)
        {
            uniform_byte_len_ = 0;
        }
    }

    // 中部插入块后维护均匀性 (块宽 blk_len, 0 表示非均匀块, O(1))
    void update_uniform_insert_block(size_t blk_len) noexcept
    {
        if (uniform_byte_len_ != 0 && blk_len != uniform_byte_len_)
        {
            uniform_byte_len_ = 0;
        }
    }

    // 下界定位: 返回首个满足 offs[i] >= byte_idx 的索引
    // 调用前需已 ensure_cp_info 且非纯 ASCII 非均匀
    [[nodiscard]] FORCE_INLINE size_t offsets_lower_bound(size_t byte_idx) const noexcept
    {
        const uint32_t* offs = cp_offsets_;
        size_t lo = 0;
        size_t hi = cp_count_;  // 搜索区间 [lo, hi)
        size_t avg = byte_size_ / cp_count_;
        if (avg > 0)
        {
            // 插值估算位置
            size_t guess = byte_idx / avg;
            if (guess >= cp_count_) guess = cp_count_ - 1;
            if (offs[guess] < byte_idx) lo = guess + 1;
            else hi = guess + 1;
        }
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (offs[mid] < byte_idx) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    // 获取码点 i 的字节偏移
    [[nodiscard]] FORCE_INLINE uint32_t cp_byte_offset(size_t i) const noexcept
    {
        if (cp_info_state_ == 1) return static_cast<uint32_t>(i);
        if (uniform_byte_len_ != 0) return static_cast<uint32_t>(i * uniform_byte_len_);
        return cp_offsets_[i];
    }

    // 获取码点总数 (仅计数, 不构建偏移)
    [[nodiscard]] size_t cp_count_safe() const noexcept
    {
        ensure_cp_count();
        return cp_count_;
    }

    size_t iterator_to_cp_idx(const const_iterator& it) const noexcept
    {
        if (cp_cache_)
        {
            if (!it.p_) return cp_count_;
            size_t idx = static_cast<size_t>(it.p_ - cp_cache_);
            return idx <= cp_count_ ? idx : cp_count_;
        }
        ensure_cp_info();
        const char* p = reinterpret_cast<const char*>(it.p_);
        if (!p || !data_) return cp_count_;
        size_t byte_idx = static_cast<size_t>(p - data_);
        if (byte_idx >= byte_size_) return cp_count_;
        if (cp_info_state_ == 1) return byte_idx;
        size_t lb = offsets_lower_bound(byte_idx);
        return (lb < cp_count_ && cp_offsets_[lb] == byte_idx) ? lb : cp_count_;
    }

    // 字节偏移 → 码点索引 (向上取整: 返回首个 offset >= byte_idx 的码点索引; 越界返回 cp_count_)
    [[nodiscard]] FORCE_INLINE size_t byte_idx_to_cp_idx_ceil(size_t byte_idx) const noexcept
    {
        ensure_cp_info();
        if (byte_idx >= byte_size_) return cp_count_;
        if (cp_info_state_ == 1) return byte_idx;
        return offsets_lower_bound(byte_idx);
    }

    // 数据 data_ 始终以 '\0' 结尾, 可直接传给 strtoll/strtod 等 C 函数
    long long to_ll_internal(size_t* pos, int base) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0; }
        if (pos) ensure_cp_info();
        char* endp = nullptr;
        errno = 0;
        long long v = std::strtoll(data_, &endp, base);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    unsigned long long to_ull_internal(size_t* pos, int base) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0; }
        if (pos) ensure_cp_info();
        char* endp = nullptr;
        errno = 0;
        unsigned long long v = std::strtoull(data_, &endp, base);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    double to_double_internal(size_t* pos) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0.0; }
        if (pos) ensure_cp_info();
        char* endp = nullptr;
        errno = 0;
        double v = std::strtod(data_, &endp);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    // 仅释放堆内存 (析构用, 不重置字段)
    // 缓冲区 sso_buffer_ 不能 free
    void release_memory_only() noexcept
    {
        if (!is_sso() && data_) { utf8pp_free(data_, static_cast<size_t>(byte_capacity_) + 1); }
        if (cp_offsets_ && cp_info_state_ == 2) { utf8pp_free(cp_offsets_, static_cast<size_t>(cp_offsets_capacity_) * sizeof(uint32_t)); }
        if (cp_cache_) { utf8pp_free(cp_cache_, static_cast<size_t>(cp_count_) * sizeof(char32_t)); }
    }

    void release() noexcept
    {
        release_memory_only();
        data_ = nullptr;
        byte_size_ = 0;
        byte_capacity_ = 0;
        cp_count_ = 0;
        cp_offsets_ = nullptr;
        cp_offsets_capacity_ = 0;
        cp_info_state_ = 0;
        uniform_byte_len_ = 0;
        cp_cache_ = nullptr;
    }

    // 将 [pos, pos+n) 码点区间替换为 str, 单次字节搬移 + 单次偏移重排
    void replace_range_core(size_t pos, size_t n, const utf8pp& str)
    {
        if (&str == this)
        {
            utf8pp snapshot(*this);
            replace_range_core(pos, n, snapshot);
            return;
        }
        ensure_cp_info();
        if (pos >= cp_count_)
        {
            return;
        }
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        str.ensure_cp_info();
        if (n == 0 && str.cp_count_ == 0) return;
        invalidate_cp_cache();
        if (cp_info_state_ == 1) promote_ascii_to_offsets();

        size_t b0 = cp_offsets_[pos];
        size_t b1 = (pos + n < cp_count_) ? cp_offsets_[pos + n] : byte_size_;
        int64_t diff = static_cast<int64_t>(str.byte_size_) - static_cast<int64_t>(b1 - b0);
        if (diff > 0)
        {
            ensure_byte_capacity(static_cast<size_t>(byte_size_ + diff));
            b0 = cp_offsets_[pos];
            b1 = (pos + n < cp_count_) ? cp_offsets_[pos + n] : byte_size_;
        }

        if (b1 < byte_size_)
        {
            std::memmove(data_ + b1 + diff, data_ + b1, byte_size_ - b1);
        }
        if (str.byte_size_ > 0)
        {
            std::memcpy(data_ + b0, str.data_, str.byte_size_);
        }
        byte_size_ = static_cast<uint32_t>(static_cast<int64_t>(byte_size_) + diff);
        data_[byte_size_] = '\0';

        if (pos + n < cp_count_)
        {
            std::memmove(cp_offsets_ + pos + str.cp_count_, cp_offsets_ + pos + n,
                         (cp_count_ - pos - n) * sizeof(uint32_t));
            for (size_t i = pos + str.cp_count_; i < cp_count_ + str.cp_count_ - n; ++i)
            {
                cp_offsets_[i] = static_cast<uint32_t>(static_cast<int64_t>(cp_offsets_[i]) + diff);
            }
        }
        if (str.cp_info_state_ == 1)
        {
            for (size_t i = 0; i < str.cp_count_; ++i)
            {
                cp_offsets_[pos + i] = static_cast<uint32_t>(b0 + i);
            }
        }
        else
        {
            for (size_t i = 0; i < str.cp_count_; ++i)
            {
                cp_offsets_[pos + i] = static_cast<uint32_t>(b0 + str.cp_offsets_[i]);
            }
        }
        cp_count_ = static_cast<uint32_t>(cp_count_ + str.cp_count_ - n);
        update_uniform_insert_block((str.cp_info_state_ == 1) ? 1 : str.uniform_byte_len_);
    }

    void insert_str(size_t cp_idx, const utf8pp& str)
    {
        if (&str == this)
        {
            // 自插入(含区间重叠): 先快照, 否则源指针随扩容/搬移失效
            utf8pp snapshot(*this);
            insert_str(cp_idx, snapshot);
            return;
        }
        str.ensure_cp_info();
        if (str.cp_count_ == 0) return;
        invalidate_cp_cache();
        ensure_cp_info();
        if (cp_idx > cp_count_) cp_idx = cp_count_;

        // 纯 ASCII 快速路径切换到已构建状态以增量维护
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
            // 插入串为纯 ASCII: 偏移连续
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
        update_uniform_insert_block((str.cp_info_state_ == 1) ? 1 : str.uniform_byte_len_);
    }

    void replace_cp_at(size_t cp_idx, uint32_t new_cp)
    {
        ensure_cp_info();
        if (cp_idx >= cp_count_) return;
        invalidate_cp_cache();
        // 纯 ASCII 快速路径需提升为偏移缓存
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
            uniform_byte_len_ = 0;  // 码点长度变更, 串不再均匀
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
        if (!was_sso && data_) utf8pp_free(data_, static_cast<size_t>(byte_capacity_) + 1);
        data_ = new_data;
        byte_capacity_ = cap;
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
        if (cp_offsets_) { utf8pp_free(cp_offsets_, static_cast<size_t>(cp_offsets_capacity_) * sizeof(uint32_t)); }
        cp_offsets_ = new_p;
        cp_offsets_capacity_ = cap;
    }

    // 纯 ASCII 快速路径提升为偏移缓存
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

    // 容量 byte_capacity_ 表示数据容量 (不含 '\0', 缓冲区实际为 byte_capacity_+1)
    void ensure_byte_capacity(size_t needed) { if (needed > byte_capacity_) grow_byte_capacity(needed); }
    void ensure_cp_capacity(size_t needed) { if (needed > cp_offsets_capacity_) grow_cp_capacity(needed); }

    void init_from_utf8(const char* s, size_t byte_len)
    {
        if (byte_len == 0)
        {
            data_[0] = '\0';
            byte_size_ = 0;
            cp_count_ = 0;
            cp_info_state_ = 1;
            uniform_byte_len_ = 1;
            return;
        }
        // 精确分配
        if (byte_len > byte_capacity_)
        {
            if (!is_sso() && data_) utf8pp_free(data_, static_cast<size_t>(byte_capacity_) + 1);
            char* new_data = static_cast<char*>(utf8pp_alloc(byte_len + 1));
            if (!new_data) std::abort();
            data_ = new_data;
            byte_capacity_ = static_cast<uint32_t>(byte_len);
        }

        // 融合扫描+拷贝: 单次读取源数据, 同时完成码点计数 + ASCII 检测 + memcpy
        const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
        uint8_t* d = reinterpret_cast<uint8_t*>(data_);
        bool all_ascii = true;
        size_t count = detail_utf8::fused_count_copy_and_ascii(p, d, byte_len, all_ascii);
        byte_size_ = static_cast<uint32_t>(byte_len);
        data_[byte_size_] = '\0';
        cp_count_ = static_cast<uint32_t>(count);
        if (all_ascii)
        {
            cp_info_state_ = 1;
            uniform_byte_len_ = 1;
        }
        else
        {
            cp_info_state_ = 3;
            detect_uniform_byte_len();
        }
    }

    // 验证均匀码点: lead 位置恰为 i*avg
    [[nodiscard]] bool verify_uniform_exact(size_t avg) const noexcept
    {
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* p = base;
        const uint8_t* end = base + byte_size_;
#if LCF_UTF8_HAS_SSE2
        const __m128i mask_C0 = _mm_set1_epi8(static_cast<char>(0xC0));
        const __m128i mask_80 = _mm_set1_epi8(static_cast<char>(0x80));
        // avg=3: 16≡1 (mod 3), 期望掩码按块索引 mod 3 轮转
        static constexpr uint16_t k_pat3[3] = {0x9249u, 0x4924u, 0x2492u};
        uint32_t rot = 0;
        while (p + 16 <= end)
        {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
            __m128i is_cont = _mm_cmpeq_epi8(_mm_and_si128(v, mask_C0), mask_80);
            uint16_t lead_mask = static_cast<uint16_t>(~_mm_movemask_epi8(is_cont));
            uint16_t expect;
            if (avg == 3) { expect = k_pat3[rot]; rot = (rot == 2) ? 0 : rot + 1; }
            else if (avg == 2) { expect = 0x5555u; }
            else { expect = 0x1111u; }  // avg == 4
            if (lead_mask != expect) return false;
            p += 16;
        }
#else
        // SWAR 8 字节/迭代, avg=3: 8≡2 (mod 3) 掩码按块索引轮转
        static constexpr uint8_t k_pat3[3] = {0x49u, 0x92u, 0x24u};
        uint32_t rot = 0;
        while (p + 8 <= end)
        {
            uint64_t chunk;
            std::memcpy(&chunk, p, 8);
            uint64_t x = chunk & 0x8080808080808080ULL;
            uint64_t y = chunk & 0x4040404040404040ULL;
            uint64_t cont = x & ~(y << 1);
            uint64_t lead = cont ^ 0x8080808080808080ULL;
            uint8_t lead_mask = static_cast<uint8_t>((lead * 0x0002040810204081ULL) >> 56);
            uint8_t expect;
            if (avg == 3) { expect = k_pat3[rot]; rot = (rot == 2) ? 0 : rot + 1; }
            else if (avg == 2) { expect = 0x55u; }
            else { expect = 0x11u; }  // avg == 4
            if (lead_mask != expect) return false;
            p += 8;
        }
#endif
        // 尾部逐字节: lead 当且仅当偏移 % avg == 0
        size_t off = static_cast<size_t>(p - base);
        while (p < end)
        {
            bool is_lead = (*p & 0xC0) != 0x80;
            if (is_lead != (off % avg == 0)) return false;
            ++p;
            ++off;
        }
        return true;
    }

    // 均匀码点检测: byte_size/cp_count 整除 + 全量验证 lead 位置
    void detect_uniform_byte_len() noexcept
    {
        uniform_byte_len_ = 0;
        if (cp_count_ == 0 || byte_size_ == 0) return;
        if (byte_size_ % cp_count_ != 0) return;
        size_t avg = byte_size_ / cp_count_;
        // avg==1 即纯 ASCII (state=1) 不会到此
        if (avg < 2 || avg > 4) return;
        if (verify_uniform_exact(avg))
        {
            uniform_byte_len_ = static_cast<uint8_t>(avg);
        }
    }

    void init_from_char32(const char32_t* s, size_t cp_count)
    {
        if (cp_count == 0) return;
        // 预计算总字节容量
        size_t total_bytes = 0;
        for (size_t i = 0; i < cp_count; ++i)
        {
            uint32_t cp = static_cast<uint32_t>(s[i]);
            if (!detail_utf8::is_valid_codepoint(cp)) cp = 0xFFFD;
            total_bytes += (cp < 0x80) ? 1 : (cp < 0x800) ? 2 : (cp < 0x10000) ? 3 : 4;
        }
        ensure_byte_capacity(total_bytes);
        ensure_cp_capacity(cp_count);
        // 一次性写入 (无容量检查)
        for (size_t i = 0; i < cp_count; ++i)
        {
            uint8_t enc[4];
            size_t len = 0;
            uint32_t cp = static_cast<uint32_t>(s[i]);
            if (!detail_utf8::utf8_encode_one(cp, enc, &len))
            {
                (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &len);
            }
            cp_offsets_[cp_count_] = static_cast<uint32_t>(byte_size_);
            ++cp_count_;
            std::memcpy(data_ + byte_size_, enc, len);
            byte_size_ += len;
        }
        data_[byte_size_] = '\0';
        cp_info_state_ = 2;
        // 所有码点等长时记录 uniform
        if (cp_count_ > 0 && byte_size_ % cp_count_ == 0)
        {
            size_t avg = byte_size_ / cp_count_;
            if (avg >= 1 && avg <= 4 && check_offsets_uniform(avg))
            {
                uniform_byte_len_ = static_cast<uint8_t>(avg);
            }
        }
    }

    void build_cp_offsets() noexcept
    {
        cp_count_ = 0;
        if (byte_size_ == 0) return;
        ensure_cp_capacity(byte_size_);
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* p = base;
        const uint8_t* end = p + byte_size_;
#if LCF_UTF8_HAS_SSE2
        // SSE2: 16 字节/迭代, pcmpeqb+pmovmskb 定位 lead 字节
        const __m128i mask_C0 = _mm_set1_epi8(static_cast<char>(0xC0));
        const __m128i mask_80 = _mm_set1_epi8(static_cast<char>(0x80));
        while (p + 16 <= end)
        {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
            __m128i m = _mm_and_si128(v, mask_C0);
            __m128i is_cont = _mm_cmpeq_epi8(m, mask_80);
            uint16_t cont_mask = static_cast<uint16_t>(_mm_movemask_epi8(is_cont));
            uint16_t lead_mask = static_cast<uint16_t>(~cont_mask);
            size_t base_off = static_cast<size_t>(p - base);
            while (lead_mask)
            {
                int bit = std::countr_zero(lead_mask);
                cp_offsets_[cp_count_++] = static_cast<uint32_t>(base_off + static_cast<size_t>(bit));
                lead_mask &= lead_mask - 1;
            }
            p += 16;
        }
#else
        // SWAR: 8 字节/迭代
        while (p + 8 <= end)
        {
            uint64_t chunk;
            std::memcpy(&chunk, p, 8);
            uint64_t x = chunk & 0x8080808080808080ULL;
            uint64_t y = chunk & 0x4040404040404040ULL;
            uint64_t cont = x & ~(y << 1);
            uint64_t lead_mask = cont ^ 0x8080808080808080ULL;
            size_t base_off = static_cast<size_t>(p - base);
            while (lead_mask)
            {
                int bit = std::countr_zero(lead_mask);
                cp_offsets_[cp_count_++] = static_cast<uint32_t>(base_off + (static_cast<size_t>(bit) >> 3));
                lead_mask &= lead_mask - 1;
            }
            p += 8;
        }
#endif
        // 尾部逐字节
        while (p < end)
        {
            if ((*p & 0xC0) != 0x80)
            {
                cp_offsets_[cp_count_++] = static_cast<uint32_t>(p - base);
            }
            ++p;
        }
        // 所有码点等长时记录 uniform
        if (cp_count_ > 0 && byte_size_ % cp_count_ == 0)
        {
            size_t avg = byte_size_ / cp_count_;
            if (avg >= 1 && avg <= 4 && check_offsets_uniform(avg))
            {
                uniform_byte_len_ = static_cast<uint8_t>(avg);
            }
        }
    }

    // 按字节偏移解码单个码点 (有校验)
    [[nodiscard]] uint32_t cp_at_byte(size_t byte_idx) const noexcept
    {
        uint32_t cp = 0;
        size_t len = 0;
        (void)detail_utf8::utf8_decode_one(
            reinterpret_cast<const uint8_t*>(data_) + byte_idx,
            reinterpret_cast<const uint8_t*>(data_) + byte_size_, &cp, &len);
        return cp;
    }

    // 无校验解码: utf8pp 内部数据保证合法
    [[nodiscard]] FORCE_INLINE uint32_t cp_at_byte_unchecked(size_t byte_idx) const noexcept
    {
        return static_cast<uint32_t>(detail_utf8::utf8_decode_unchecked(
            reinterpret_cast<const uint8_t*>(data_) + byte_idx));
    }

    // 字节偏移 → 码点索引 (非码点起点返回 npos)
    [[nodiscard]] FORCE_INLINE size_t byte_idx_to_cp_idx(size_t byte_idx) const noexcept
    {
        if (byte_idx >= byte_size_) return npos;
        ensure_cp_count();
        if (cp_count_ == 0) return npos;
        if (cp_info_state_ == 1) return byte_idx;
        if (uniform_byte_len_ != 0)
        {
            switch (uniform_byte_len_)
            {
                case 1: return byte_idx;
                case 2: return byte_idx >> 1;
                case 3: return byte_idx / 3;
                case 4: return byte_idx >> 2;
            }
        }
        ensure_cp_info();
        size_t lb = offsets_lower_bound(byte_idx);
        return (lb < cp_count_ && cp_offsets_[lb] == byte_idx) ? lb : npos;
    }

    // 字节偏移 → 码点索引 (SWAR 计数, 无 cp_offsets_)
    [[nodiscard]] size_t byte_idx_to_cp_idx_swar(size_t byte_idx) const noexcept
    {
        if (byte_idx >= byte_size_) return npos;
        if (cp_info_state_ == 1) return byte_idx;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
        return detail_utf8::count_codepoints(base, base + byte_idx);
    }

    // 码点索引 → 字节偏移 (SWAR 推进, 无 cp_offsets_)
    [[nodiscard]] size_t cp_idx_to_byte_offset_swar(size_t pos) const noexcept
    {
        if (pos >= cp_count_) return byte_size_;
        if (cp_info_state_ == 1) return pos;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = base + byte_size_;
        const uint8_t* p = detail_utf8::advance_codepoints(base, end, pos);
        return static_cast<size_t>(p - base);
    }
    