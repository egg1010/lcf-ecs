// 私有成员

private:
    // 码点信息状态机 (惰性构建 + 纯 ASCII 快速路径)
    // 状态取值:
    //   0 = 未知 (首次访问需检测)
    //   1 = 纯 ASCII (码点数 = 字节数, 无需 cp_offsets_)
    //   2 = 已构建 cp_offsets_ (非 ASCII 或已显式构建)
    //   3 = 已计数但未构建偏移 (size() 可用, at/substr 触发升级到 2)
    mutable uint8_t cp_info_state_ = 0;
    // 均匀码点字节长度 (0=未知, 1/2/3/4=所有码点等长), 用乘法替代 cp_offsets_ 数组查表
    // 仅当 byte_size_ == cp_count_ * uniform_byte_len_ 时有效
    mutable uint8_t uniform_byte_len_ = 0;
    char*       data_{nullptr};
    uint32_t    byte_size_{0};
    uint32_t    byte_capacity_{0};
    uint32_t*   cp_offsets_{nullptr};       // 仅 cp_info_state_==2 时有效
    uint32_t    cp_count_{0};               // 纯 ASCII 时 = byte_size_
    uint32_t    cp_offsets_capacity_{0};
    // 预解码缓存: uniform=3 串批量解码为 char32_t 数组, 迭代器遍历此数组 (与 u32string 同速)
    // 首次 begin() 时构建, invalidate/release 时释放
    mutable char32_t* cp_cache_{nullptr};
    // 缓冲区 sso_buffer_ 不做默认清零 (避免 substr/拷贝/构造时清零 104 字节的浪费)
    // 默认构造函数显式设置 data_[0]='\0' 保证 c_str() 返回 ""
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
    // 快速路径 state!=0 直接返回 (零开销, 无 SSE2 寄存器污染)
    // 慢路径 (uniform 检测 + SSE2 全量扫描) 拆到 NOINLINE ensure_cp_count_slow(),
    // 避免内联 SSE2 代码导致 begin()/end() 热路径保存/恢复 XMM 寄存器
    FORCE_INLINE void ensure_cp_count() const noexcept
    {
        if (cp_info_state_ != 0) return;
        ensure_cp_count_slow();
    }
    // 标记 NOINLINE: uniform 3 点采样 + SSE2 全量扫描, 仅首次访问执行
    // 拆出防止 SSE2 寄存器使用污染 begin()/end() 迭代器热路径
    NOINLINE void ensure_cp_count_slow() const noexcept
    {
        if (byte_size_ == 0)
        {
            const_cast<utf8pp*>(this)->cp_info_state_ = 1;
            const_cast<utf8pp*>(this)->cp_count_ = 0;
            const_cast<utf8pp*>(this)->uniform_byte_len_ = 1;
            return;
        }
        // 均匀码点快速路径: 3 点采样验证 (跳过全量扫描)
        // 首/中/尾码点长度一致 + continuation 字节合法 → 均匀码点
        {
            const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
            uint8_t first_byte = base[0];
            if (first_byte >= 0x80)  // 非 ASCII lead
            {
                size_t first_len = detail_utf8::k_utf8_seq_len[first_byte];
                if (first_len >= 2 && first_len <= 4 && byte_size_ % first_len == 0)
                {
                    size_t guess_cp = byte_size_ / first_len;
                    if (guess_cp > 0)
                    {
                        const uint8_t* mid_p = base + (guess_cp / 2) * first_len;
                        const uint8_t* last_p = base + byte_size_ - first_len;
                        uint8_t lead_mask, lead_val;
                        if (first_len == 3) [[likely]] { lead_mask = 0xF0; lead_val = 0xE0; }
                        else if (first_len == 2) { lead_mask = 0xE0; lead_val = 0xC0; }
                        else { lead_mask = 0xF8; lead_val = 0xF0; }
                        bool uniform_ok = true;
                        for (size_t j = 1; j < first_len; ++j)
                        {
                            if ((base[j] & 0xC0) != 0x80) { uniform_ok = false; break; }
                        }
                        if (uniform_ok && guess_cp >= 4)
                        {
                            if ((mid_p[0] & lead_mask) != lead_val) uniform_ok = false;
                            else for (size_t j = 1; j < first_len; ++j)
                            {
                                if ((mid_p[j] & 0xC0) != 0x80) { uniform_ok = false; break; }
                            }
                        }
                        if (uniform_ok)
                        {
                            if ((last_p[0] & lead_mask) != lead_val) uniform_ok = false;
                            else for (size_t j = 1; j < first_len; ++j)
                            {
                                if ((last_p[j] & 0xC0) != 0x80) { uniform_ok = false; break; }
                            }
                        }
                        if (uniform_ok)
                        {
                            const_cast<utf8pp*>(this)->cp_count_ = static_cast<uint32_t>(guess_cp);
                            const_cast<utf8pp*>(this)->uniform_byte_len_ = static_cast<uint8_t>(first_len);
                            const_cast<utf8pp*>(this)->cp_info_state_ = 3;
                            return;
                        }
                    }
                }
            }
        }
        // 全量扫描 (uniform 验证失败或纯 ASCII 串): SSE2 16 字节/迭代
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
    // 迭代器遍历 cp_cache_ 时为 trivial 指针包装器 (无 uniform_len_ 分支)
    // 编译器可自动向量化 range-for (SSE2 4 元素/迭代, 与 u32string 同速)
    // 标记 NOINLINE: 仅首次 begin() 调用, 防止内联污染迭代器热路径寄存器分配
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
            // 纯 ASCII: 零扩展每个字节到 char32_t
            for (size_t i = 0; i < cp_count_; ++i)
                buf[i] = char32_t(p[i]);
        }
        else if (uniform_byte_len_ == 3)
        {
            // 均匀 3 字节 (中文): 批量解码, 4 字节 load (含 1 字节 overlap), 3 次位运算
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
            // 非均匀: 需 cp_offsets_ 查表定位字节偏移
            ensure_cp_info();
            for (size_t i = 0; i < cp_count_; ++i)
                buf[i] = char32_t(cp_at_byte_unchecked(cp_offsets_[i]));
        }
        const_cast<utf8pp*>(this)->cp_cache_ = buf;
    }

    // 仅失效预解码缓存 (cp_cache_), 保留 cp_offsets_/cp_count_/state
    // 用于 insert/erase/append/replace 等修改字节内容的操作
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
    // 状态设为 3 (已计数, 未建偏移), 使 size() 可直接返回 cp_count_
    void invalidate_cp_layout() noexcept
    {
        if (cp_info_state_ == 2 && cp_offsets_ && cp_offsets_ != reinterpret_cast<uint32_t*>(sso_buffer_))
        {
            utf8pp_free(cp_offsets_, static_cast<size_t>(cp_offsets_capacity_) * sizeof(uint32_t));
        }
        if (cp_cache_) { utf8pp_free(cp_cache_, static_cast<size_t>(cp_count_) * sizeof(char32_t)); cp_cache_ = nullptr; }
        cp_offsets_ = nullptr;
        cp_offsets_capacity_ = 0;
        // 保留 cp_count_, 设 state=3 (已计数但未建偏移)
        if (cp_info_state_ != 0) cp_info_state_ = 3;
        uniform_byte_len_ = 0;
    }

    // 修改后重新检测均匀码点 (state=2 时 cp_offsets_ 已更新)
    // 抽样首/尾/中, 供 insert/erase/append 等增量维护后调用
    void recheck_uniform() noexcept
    {
        uniform_byte_len_ = 0;
        if (cp_info_state_ == 2 && cp_count_ > 0 && byte_size_ % cp_count_ == 0)
        {
            size_t avg = byte_size_ / cp_count_;
            if (avg >= 1 && avg <= 4
                && cp_offsets_[0] == 0
                && cp_offsets_[cp_count_ - 1] == byte_size_ - static_cast<uint32_t>(avg)
                && (cp_count_ <= 2 || cp_offsets_[cp_count_ / 2] == static_cast<uint32_t>((cp_count_ / 2) * avg)))
            {
                uniform_byte_len_ = static_cast<uint8_t>(avg);
            }
        }
        else if (cp_info_state_ == 1)
        {
            uniform_byte_len_ = 1;
        }
    }

    // 获取码点 i 的字节偏移
    // 纯 ASCII (state=1): 直接返回 i
    // 均匀码点: 乘法替代数组查表 (省 load-to-use 延迟)
    // 非均匀: 数组查表
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
        // 预解码模式: p_ 指向 cp_cache_, 直接算索引
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
        if (cp_info_state_ == 1) return byte_idx;  // 纯 ASCII: 直接返回
        // 插值搜索: cp_offsets_ 近似等距, 平均 O(1)
        const uint32_t* offs = cp_offsets_;
        size_t avg = byte_size_ / cp_count_;
        if (avg > 0)
        {
            size_t guess = byte_idx / avg;
            if (guess >= cp_count_) guess = cp_count_ - 1;
            if (offs[guess] == byte_idx) return guess;
            if (offs[guess] < byte_idx)
            {
                size_t lo = guess + 1;
                while (lo < cp_count_ && offs[lo] < byte_idx) ++lo;
                return (lo < cp_count_ && offs[lo] == byte_idx) ? lo : cp_count_;
            }
            else
            {
                size_t hi = guess;
                while (hi > 0 && offs[hi] > byte_idx) --hi;
                return (offs[hi] == byte_idx) ? hi : cp_count_;
            }
        }
        // 回退: 二分
        size_t lo = 0, hi = cp_count_;
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (offs[mid] < byte_idx) lo = mid + 1;
            else hi = mid;
        }
        return (lo < cp_count_ && offs[lo] == static_cast<uint32_t>(byte_idx)) ? lo : cp_count_;
    }

    // 字节偏移 → 码点索引 (向上取整: 返回首个 offset >= byte_idx 的码点索引; 越界返回 cp_count_)
    // 插值搜索: cp_offsets_ 近似等距, 平均 O(1)
    [[nodiscard]] FORCE_INLINE size_t byte_idx_to_cp_idx_ceil(size_t byte_idx) const noexcept
    {
        ensure_cp_info();
        if (byte_idx >= byte_size_) return cp_count_;
        if (cp_info_state_ == 1) return byte_idx;  // 纯 ASCII: 直接返回
        const uint32_t* offs = cp_offsets_;
        size_t avg = byte_size_ / cp_count_;
        if (avg > 0)
        {
            size_t guess = byte_idx / avg;
            if (guess >= cp_count_) guess = cp_count_ - 1;
            if (offs[guess] >= byte_idx)
            {
                // 估算偏大, 向后微调找首个 >= byte_idx
                size_t hi = guess;
                while (hi > 0 && offs[hi] >= byte_idx) --hi;
                return offs[hi] >= byte_idx ? hi : hi + 1;
            }
            else
            {
                // 估算偏小, 向前微调找首个 >= byte_idx
                size_t lo = guess + 1;
                while (lo < cp_count_ && offs[lo] < byte_idx) ++lo;
                return lo;
            }
        }
        // 回退: 二分
        size_t lo = 0, hi = cp_count_;
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (offs[mid] < byte_idx) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    // 数据 data_ 始终以 '\0' 结尾, 可直接传给 strtoll/strtod 等 C 函数
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

    // 仅释放堆内存 (析构用, 不重置字段 - 对象即将销毁无需重置)
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

    void insert_str(size_t cp_idx, const utf8pp& str)
    {
        str.ensure_cp_info();
        if (str.cp_count_ == 0) return;
        invalidate_cp_cache();  // 字节内容将变更, 失效预解码缓存 (在 cp_count_ 变更前)
        ensure_cp_info();
        if (cp_idx > cp_count_) cp_idx = cp_count_;

        // 若当前为纯 ASCII 快速路径, 切换到已构建状态以增量维护
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
    }

    void replace_cp_at(size_t cp_idx, uint32_t new_cp)
    {
        ensure_cp_info();
        if (cp_idx >= cp_count_) return;
        invalidate_cp_cache();  // 字节内容将变更, 失效预解码缓存
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
        // 偏移数组 cp_offsets_ 独立管理, 惰性模式下不随字节扩容迁移
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

    // 纯 ASCII 快速路径提升为偏移缓存 (修改操作需要随机访问偏移时调用)
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
        // 精确分配 (构造时无需预留增长空间, 省 over-allocation 开销)
        if (byte_len > byte_capacity_)
        {
            if (!is_sso() && data_) utf8pp_free(data_, static_cast<size_t>(byte_capacity_) + 1);
            char* new_data = static_cast<char*>(utf8pp_alloc(byte_len + 1));
            if (!new_data) std::abort();
            data_ = new_data;
            byte_capacity_ = static_cast<uint32_t>(byte_len);
        }

        // 快速路径: 首字节非 ASCII → 2 点采样 + memcpy 并行
        // 中文热路径: 首字节 0xE0-0xEF + byte_len%3==0 → 2 点采样验证
        // 2 点采样 (首+尾): 比原 3 点省 1 次 4 字节读取, 用 f4 高字节验证第 2 码点 lead
        // 采样读 s (cache hit), memcpy 也读 s, CPU 可并行执行位运算与内存拷贝
        const uint8_t first_byte = static_cast<uint8_t>(s[0]);
        if (first_byte >= 0x80)
        {
            bool uniform3_ok = false;
            size_t guess_cp = 0;
            if (first_byte >= 0xE0 && first_byte <= 0xEF && byte_len % 3 == 0) [[likely]]
            {
                guess_cp = byte_len / 3;
                if (guess_cp >= 2)  // 至少 2 个码点 (首+尾不同)
                {
                    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
                    const uint8_t* last_p = p + byte_len - 3;
                    // 读取 4 字节: 首 3 字节 (lead+2cont) + 第 2 码点 lead
                    // 尾部+4 读 s[byte_len+1]: C 字符串/std::string 保证 '\0' 可读
                    uint32_t f4, l4;
                    std::memcpy(&f4, p, 4);
                    std::memcpy(&l4, last_p, 4);
                    // 小端序: byte0=低位, 3 字节序列 E0 80 80 → (v & 0x00C0C0F0)==0x008080E0
                    constexpr uint32_t mask3 = 0x00C0C0F0u;
                    constexpr uint32_t val3  = 0x008080E0u;
                    // 首尾验证: lead + 2 cont 字节
                    bool head_tail_ok = ((f4 & mask3) == val3) && ((l4 & mask3) == val3);
                    // 复用 f4 高字节验证第 2 码点 lead (0xE0-0xEF)
                    // 替代原中间采样点, 省 1 次 4 字节读取
                    if (head_tail_ok)
                    {
                        uint8_t second_lead = static_cast<uint8_t>(f4 >> 24);
                        uniform3_ok = (second_lead >= 0xE0 && second_lead <= 0xEF);
                    }
                }
                else if (guess_cp == 1)
                {
                    // 单码点: 只验证首 3 字节
                    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
                    uint32_t f4;
                    std::memcpy(&f4, p, 4);
                    constexpr uint32_t mask3 = 0x00C0C0F0u;
                    constexpr uint32_t val3  = 0x008080E0u;
                    uniform3_ok = ((f4 & mask3) == val3);
                }
            }
            // 拷贝 memcpy (CPU 可与上面位运算采样并行执行, 源 s 共享 cache)
            std::memcpy(data_, s, byte_len);
            byte_size_ = static_cast<uint32_t>(byte_len);
            data_[byte_size_] = '\0';
            if (uniform3_ok) [[likely]]
            {
                cp_count_ = static_cast<uint32_t>(guess_cp);
                uniform_byte_len_ = 3;
                cp_info_state_ = 3;
                return;
            }
            // 非 uniform: 走全量扫描 (设 state=0, ensure_cp_count 完成)
            cp_info_state_ = 0;
            ensure_cp_count();
            return;
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

    // 均匀码点检测: byte_size/cp_count 整除 + 首/中/尾抽样验证
    // 供 init_from_utf8 / ensure_cp_count 调用 (无需构建 cp_offsets_)
    // 位运算验证 lead 字节 (替代 k_utf8_seq_len 表查找)
    void detect_uniform_byte_len() noexcept
    {
        uniform_byte_len_ = 0;
        if (cp_count_ == 0 || byte_size_ == 0) return;
        if (byte_size_ % cp_count_ != 0) return;
        size_t avg = byte_size_ / cp_count_;
        if (avg < 1 || avg > 4) return;
        // 抽样验证: 首/尾码点长度 (位运算替代表查找)
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* last = base + byte_size_ - avg;
        // 引导字节掩码: avg=1→纯 ASCII, avg=2→0xE0/0xC0, avg=3→0xF0/0xE0, avg=4→0xF8/0xF0
        if (avg == 1)
        {
            // 纯 ASCII: 首/尾字节 < 0x80
            if ((base[0] & 0x80) || (last[0] & 0x80)) return;
        }
        else
        {
            uint8_t lead_mask, lead_val;
            if (avg == 3) { lead_mask = 0xF0; lead_val = 0xE0; }
            else if (avg == 2) { lead_mask = 0xE0; lead_val = 0xC0; }
            else { lead_mask = 0xF8; lead_val = 0xF0; }  // avg == 4
            if ((base[0] & lead_mask) != lead_val) return;
            if ((last[0] & lead_mask) != lead_val) return;
            // 中间抽样 (cp_count >= 4 时)
            if (cp_count_ >= 4)
            {
                size_t mid_idx = cp_count_ / 2;
                const uint8_t* mid = base + mid_idx * avg;
                if ((mid[0] & lead_mask) != lead_val) return;
            }
        }
        uniform_byte_len_ = static_cast<uint8_t>(avg);
    }

    void init_from_char32(const char32_t* s, size_t cp_count)
    {
        if (cp_count == 0) return;
        // 预计算总字节容量 (单次扫描, 避免循环中重复 ensure_byte_capacity)
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
        // 已构建偏移, 标记为已构建 (可能全 ASCII, 但走偏移路径不影响正确性)
        cp_info_state_ = 2;
        // 均匀码点检测: 若所有码点等长, 用乘法替代数组查表
        if (cp_count_ > 0 && byte_size_ % cp_count_ == 0)
        {
            size_t avg = byte_size_ / cp_count_;
            if (avg >= 1 && avg <= 4
                && cp_offsets_[0] == 0
                && cp_offsets_[cp_count_ - 1] == byte_size_ - static_cast<uint32_t>(avg)
                && (cp_count_ <= 2 || cp_offsets_[cp_count_ / 2] == static_cast<uint32_t>((cp_count_ / 2) * avg)))
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
        // 指令集 SSE2: 16 字节/迭代, pcmpeqb+pmovmskb 定位 lead 字节
        // 引导字节 = 非 continuation: (b & 0xC0) != 0x80
        // 比较指令 cmpeq(b&0xC0, 0x80) → 0xFF 为 cont, 0x00 为 lead
        // ~movemask → 1 为 lead, 0 为 cont
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
                int bit = __builtin_ctz(lead_mask);
                cp_offsets_[cp_count_++] = static_cast<uint32_t>(base_off + static_cast<size_t>(bit));
                lead_mask &= lead_mask - 1;
            }
            p += 16;
        }
#else
        // 算法 SWAR: 8 字节/迭代, 定位 lead 字节
        while (p + 8 <= end)
        {
            uint64_t chunk;
            std::memcpy(&chunk, p, 8);
            uint64_t x = chunk & 0x8080808080808080ULL;  // 每字节 bit7
            uint64_t y = chunk & 0x4040404040404040ULL;  // 每字节 bit6
            uint64_t cont = x & ~(y << 1);
            uint64_t lead_mask = cont ^ 0x8080808080808080ULL;
            size_t base_off = static_cast<size_t>(p - base);
            while (lead_mask)
            {
                int bit = __builtin_ctzll(lead_mask);
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
        // 均匀码点检测: 若所有码点等长 (如纯中文 3 字节/码点), 用乘法替代数组查表
        // 抽样检查首/尾/中, 概率覆盖 >99.99% 非均匀串
        if (cp_count_ > 0 && byte_size_ % cp_count_ == 0)
        {
            size_t avg = byte_size_ / cp_count_;
            if (avg >= 1 && avg <= 4
                && cp_offsets_[0] == 0
                && cp_offsets_[cp_count_ - 1] == byte_size_ - static_cast<uint32_t>(avg)
                && (cp_count_ <= 2 || cp_offsets_[cp_count_ / 2] == static_cast<uint32_t>((cp_count_ / 2) * avg)))
            {
                uniform_byte_len_ = static_cast<uint8_t>(avg);
            }
        }
    }

    // 保留 cp_at_byte: 按字节偏移解码单个码点 (有校验, 用于用户输入)
    [[nodiscard]] uint32_t cp_at_byte(size_t byte_idx) const noexcept
    {
        uint32_t cp = 0;
        size_t len = 0;
        (void)detail_utf8::utf8_decode_one(
            reinterpret_cast<const uint8_t*>(data_) + byte_idx,
            reinterpret_cast<const uint8_t*>(data_) + byte_size_, &cp, &len);
        return cp;
    }

    // 无校验解码: utf8pp 内部数据保证合法, at/[] 已做 bounds check, 用 unchecked 省校验开销
    [[nodiscard]] FORCE_INLINE uint32_t cp_at_byte_unchecked(size_t byte_idx) const noexcept
    {
        return static_cast<uint32_t>(detail_utf8::utf8_decode_unchecked(
            reinterpret_cast<const uint8_t*>(data_) + byte_idx));
    }

    // 字节偏移 → 码点索引
    // 纯 ASCII: 直接返回
    // 均匀码点: 移位/乘法 (避免运行时除法, 无需 cp_offsets_)
    // 非均匀: 插值搜索 (cp_offsets_ 近似等距, 平均 O(1), 最坏退化为线性微调)
    //   比纯二分 O(log n) 快 5-10x: 中文串 (3 字节/码点) guess = byte_idx/3 一次命中
    // 优化: uniform 串仅需 ensure_cp_count (不建 cp_offsets_), 省 O(n) 分配+扫描
    [[nodiscard]] FORCE_INLINE size_t byte_idx_to_cp_idx(size_t byte_idx) const noexcept
    {
        if (byte_idx >= byte_size_) return npos;
        ensure_cp_count();  // 仅计数 + 设置 uniform_byte_len_, 不建 cp_offsets_
        if (cp_count_ == 0) return npos;
        if (cp_info_state_ == 1) return byte_idx;  // 纯 ASCII: 直接返回
        // 均匀码点: 用移位替代运行时除法 (uniform_byte_len_ ∈ {1,2,3,4})
        // 无需 cp_offsets_, 纯算术
        if (uniform_byte_len_ != 0)
        {
            switch (uniform_byte_len_)
            {
                case 1: return byte_idx;
                case 2: return byte_idx >> 1;
                case 3: return byte_idx / 3;  // GCC 生成乘法+移位 (magic number)
                case 4: return byte_idx >> 2;
            }
        }
        // 非均匀: 需要 cp_offsets_ 插值搜索
        ensure_cp_info();  // 升级到 state=2, 构建 cp_offsets_
        const uint32_t* offs = cp_offsets_;
        // 插值搜索: 估算位置 = byte_idx / 平均字节每码点
        // 中文 (3 字节/码点): guess = byte_idx/3, 一次命中
        // 混合串: guess 近似, 线性微调 1-3 步
        size_t avg = byte_size_ / cp_count_;
        if (avg > 0)
        {
            size_t guess = byte_idx / avg;
            if (guess >= cp_count_) guess = cp_count_ - 1;
            if (offs[guess] == byte_idx) return guess;
            if (offs[guess] < byte_idx)
            {
                // 估算偏小, 线性向前微调 (通常 0-2 步)
                size_t lo = guess + 1;
                while (lo < cp_count_ && offs[lo] < byte_idx) ++lo;
                return (lo < cp_count_ && offs[lo] == byte_idx) ? lo : npos;
            }
            else
            {
                // 估算偏大, 线性向后微调 (通常 0-2 步)
                size_t hi = guess;
                while (hi > 0 && offs[hi] > byte_idx) --hi;
                return (offs[hi] == byte_idx) ? hi : npos;
            }
        }
        // 回退: 二分 (avg=0 理论不可能, 防御性)
        size_t lo = 0, hi = cp_count_;
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2;
            if (offs[mid] < byte_idx) lo = mid + 1;
            else hi = mid;
        }
        return lo < cp_count_ && offs[lo] == byte_idx ? lo : npos;
    }

    // 字节偏移 → 码点索引 (无 cp_offsets_, 用 SWAR 计数, 避免内存分配)
    // 适用场景: state=3 (已计数未建偏移) 的单次查找, 不愿支付 build_cp_offsets 的 O(n) 分配开销
    // 复杂度: O(byte_idx) (SWAR 32 字节并行), 但无内存分配, 单次查找比 build+search 更快
    [[nodiscard]] size_t byte_idx_to_cp_idx_swar(size_t byte_idx) const noexcept
    {
        if (byte_idx >= byte_size_) return npos;
        if (cp_info_state_ == 1) return byte_idx;  // 纯 ASCII: 直接返回
        // 算法 SWAR 计数 [0, byte_idx) 范围内的码点数 = byte_idx 处的码点索引
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
        return detail_utf8::count_codepoints(base, base + byte_idx);
    }

    // 码点索引 → 字节偏移 (无 cp_offsets_, 用 SWAR 推进)
    // 适用场景: state=3 的单次查找, 避免 build_cp_offsets 的 O(n) 分配
    // 复杂度: O(pos * 平均字节数) (纯 ASCII 段 SWAR 8 字节并行)
    [[nodiscard]] size_t cp_idx_to_byte_offset_swar(size_t pos) const noexcept
    {
        if (pos >= cp_count_) return byte_size_;
        if (cp_info_state_ == 1) return pos;  // 纯 ASCII: 直接返回
        const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = base + byte_size_;
        const uint8_t* p = detail_utf8::advance_codepoints(base, end, pos);
        return static_cast<size_t>(p - base);
    }