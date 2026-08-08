// 查找/比较

    // === 查找 (字节级, UTF-8 保证字节序 = 码点序) ===
    [[nodiscard]] size_t find(char32_t cp, size_t pos = 0) const noexcept
    {
        ensure_cp_count();
        if (pos >= cp_count_) return npos;
        // 纯 ASCII 快速路径: ASCII 字节必为 lead, memchr 命中即码点起点
        if (static_cast<uint32_t>(cp) < 0x80)
        {
            // 位置 pos=0 快速路径: 跳过偏移计算
            if (pos == 0)
            {
                const void* found = std::memchr(data_, static_cast<int>(cp), byte_size_);
                if (!found) return npos;
                if (cp_info_state_ == 1) return static_cast<size_t>(static_cast<const char*>(found) - data_);
                // 均匀码点 uniform/state=3: byte_idx_to_cp_idx 内部处理 (uniform 无需建 cp_offsets_)
                return byte_idx_to_cp_idx(static_cast<size_t>(static_cast<const char*>(found) - data_));
            }
            // 状态=1 (ASCII 串): 码点索引 = 字节索引
            if (cp_info_state_ == 1)
            {
                const void* found = std::memchr(data_ + pos, static_cast<int>(cp), byte_size_ - pos);
                if (!found) return npos;
                return static_cast<size_t>(static_cast<const char*>(found) - data_);
            }
            // 均匀: O(1) 乘法计算 byte_pos (无需 SWAR 推进)
            if (uniform_byte_len_ != 0)
            {
                size_t byte_pos = pos * uniform_byte_len_;
                const void* found = std::memchr(data_ + byte_pos, static_cast<int>(cp), byte_size_ - byte_pos);
                if (!found) return npos;
                return byte_idx_to_cp_idx(static_cast<size_t>(static_cast<const char*>(found) - data_));
            }
            // 状态=3 非均匀: SWAR 推进 + memchr
            if (cp_info_state_ == 3)
            {
                size_t byte_pos = cp_idx_to_byte_offset_swar(pos);
                const void* found = std::memchr(data_ + byte_pos, static_cast<int>(cp), byte_size_ - byte_pos);
                if (!found) return npos;
                return byte_idx_to_cp_idx(static_cast<size_t>(static_cast<const char*>(found) - data_));
            }
            // 状态=2: O(1) 偏移 + memchr
            size_t byte_pos = cp_byte_offset(pos);
            const void* found = std::memchr(data_ + byte_pos, static_cast<int>(cp), byte_size_ - byte_pos);
            if (!found) return npos;
            return byte_idx_to_cp_idx(static_cast<size_t>(static_cast<const char*>(found) - data_));
        }
        // 非 ASCII cp: 预编码 UTF-8 字节序列后字节级搜索
        uint8_t enc[4];
        size_t enc_len = 0;
        if (!detail_utf8::utf8_encode_one(static_cast<uint32_t>(cp), enc, &enc_len))
        {
            (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &enc_len);
        }
        if (cp_info_state_ == 1) return npos;  // 纯 ASCII 串 + 非 ASCII cp: 不可能匹配
        // 位置 pos=0 快速路径
        if (pos == 0)
        {
            std::string_view self(data_, byte_size_);
            size_t found = self.find(reinterpret_cast<const char*>(enc), 0, enc_len);
            if (found == std::string_view::npos) return npos;
            return byte_idx_to_cp_idx(found);  // 均匀码点 uniform 无需建 cp_offsets_
        }
        // 均匀: O(1) 乘法计算 byte_pos
        if (uniform_byte_len_ != 0)
        {
            size_t byte_pos = pos * uniform_byte_len_;
            if (byte_pos + enc_len > byte_size_) return npos;
            std::string_view self(data_, byte_size_);
            size_t found = self.find(reinterpret_cast<const char*>(enc), byte_pos, enc_len);
            if (found == std::string_view::npos) return npos;
            return byte_idx_to_cp_idx(found);
        }
        // 状态=3 非均匀: SWAR 推进 + 字节级搜索
        if (cp_info_state_ == 3)
        {
            size_t byte_pos = cp_idx_to_byte_offset_swar(pos);
            if (byte_pos + enc_len > byte_size_) return npos;
            std::string_view self(data_, byte_size_);
            size_t found = self.find(reinterpret_cast<const char*>(enc), byte_pos, enc_len);
            if (found == std::string_view::npos) return npos;
            return byte_idx_to_cp_idx(found);
        }
        // 状态=2: O(1) 偏移 + 字节级搜索
        size_t byte_pos = cp_byte_offset(pos);
        if (byte_pos + enc_len > byte_size_) return npos;
        std::string_view self(data_, byte_size_);
        size_t found = self.find(reinterpret_cast<const char*>(enc), byte_pos, enc_len);
        if (found == std::string_view::npos) return npos;
        return byte_idx_to_cp_idx(found);
    }

    // 慢路径: 非 uniform=3/pos=0 的完整路径
    [[nodiscard]] size_t find_slow(const utf8pp& str, size_t pos) const noexcept
    {
        // 纯 ASCII 快速路径: 状态=1 跳过 ensure_cp_count, 字节索引 = 码点索引
        uint8_t st = cp_info_state_;
        if (st == 1)
        {
            if (str.byte_size_ == 0) return pos <= byte_size_ ? pos : npos;
            if (str.byte_size_ > byte_size_) return npos;
            std::string_view self(data_, byte_size_);
            std::string_view sv(str.data_, str.byte_size_);
            size_t found = self.find(sv, pos);
            return found == std::string_view::npos ? npos : found;
        }
        if (st == 0) { ensure_cp_count(); st = cp_info_state_; }
        if (str.byte_size_ == 0) return pos <= cp_count_ ? pos : npos;
        if (str.byte_size_ > byte_size_) return npos;
        std::string_view self(data_, byte_size_);
        std::string_view sv(str.data_, str.byte_size_);
        // 位置 pos=0 快速路径
        if (pos == 0)
        {
            size_t found = self.find(sv, 0);
            if (found == std::string_view::npos) return npos;
            switch (uniform_byte_len_)
            {
                case 2: return found >> 1;
                case 3: return found / 3;
                case 4: return found >> 2;
                default: break;
            }
            return byte_idx_to_cp_idx(found);
        }
        // 均匀: O(1) 乘法计算 byte_pos
        if (uniform_byte_len_ != 0)
        {
            size_t byte_pos = (pos < cp_count_) ? pos * uniform_byte_len_ : byte_size_;
            size_t found = self.find(sv, byte_pos);
            if (found == std::string_view::npos) return npos;
            switch (uniform_byte_len_)
            {
                case 2: return found >> 1;
                case 3: return found / 3;
                case 4: return found >> 2;
                default: break;
            }
            return byte_idx_to_cp_idx(found);
        }
        // 状态=3 非均匀: SWAR 推进
        if (st == 3)
        {
            size_t byte_pos = (pos < cp_count_) ? cp_idx_to_byte_offset_swar(pos) : byte_size_;
            size_t found = self.find(sv, byte_pos);
            if (found == std::string_view::npos) return npos;
            return byte_idx_to_cp_idx(found);
        }
        // 状态=2: O(1) 偏移
        size_t byte_pos = (pos < cp_count_) ? cp_byte_offset(pos) : byte_size_;
        size_t found = self.find(sv, byte_pos);
        if (found == std::string_view::npos) return npos;
        return byte_idx_to_cp_idx(found);
    }

    // 热路径: 单基本块 (uniform=3 + pos=0), 冷路径委派 find_slow
    // 函数 memchr (SSE2) 定位首字节 + memcmp 验证, 比朴素循环更快
    [[nodiscard]] FORCE_INLINE size_t find(const utf8pp& str, size_t pos = 0) const noexcept
    {
        // 热路径: uniform=3 + pos=0
        // 变量 uniform_byte_len_!=0 隐含 cp_info_state_!=0 (uniform 只在 state=1/3 时设置)
        if (pos == 0 && uniform_byte_len_ == 3) [[likely]]
        {
            const char* pat = str.data_;
            size_t pat_len = str.byte_size_;
            if (pat_len == 0) return 0;
            if (pat_len > byte_size_) return npos;
            // 单字节模式: memchr 直接定位 (SSE2 向量化)
            if (pat_len == 1)
            {
                const void* found = std::memchr(data_, pat[0], byte_size_);
                return found ? static_cast<size_t>(static_cast<const char*>(found) - data_) / 3 : npos;
            }
            // 多字节模式: memchr 首字节 (SSE2) + memcmp 验证
            // 比朴素逐字节循环快 3-5 倍 (memchr 每次 16 字节)
            const char first = pat[0];
            const char* hay = data_;
            const char* hay_end = data_ + byte_size_ - pat_len + 1;
            while (hay < hay_end)
            {
                const char* found = static_cast<const char*>(
                    std::memchr(hay, first, static_cast<size_t>(hay_end - hay)));
                if (!found) return npos;
                if (std::memcmp(found, pat, pat_len) == 0)
                    return static_cast<size_t>(found - data_) / 3;
                hay = found + 1;
            }
            return npos;
        }
        return find_slow(str, pos);
    }

    [[nodiscard]] FORCE_INLINE size_t find(const char* s, size_t pos = 0) const noexcept
    {
        if (!s) return npos;
        // 纯 ASCII 快速路径: 状态=1 直接 string_view::find
        // 比 strstr 快: 已知主串长度, 使用 memchr+memcmp 而非 Two-Way 算法
        if (cp_info_state_ == 1) [[likely]]
        {
            if (pos > byte_size_) return npos;
            std::string_view self(data_, byte_size_);
            size_t found = self.find(std::string_view(s), pos);
            return found == std::string_view::npos ? npos : found;
        }
        return find(std::string_view(s, std::strlen(s)), pos);
    }

    [[nodiscard]] size_t find(std::string_view sv, size_t pos = 0) const noexcept
    {
        // 纯 ASCII 快速路径: state=1 (已知 ASCII) → 跳过 ensure_cp_count, 字节索引 = 码点索引
        uint8_t st = cp_info_state_;
        if (st == 1)
        {
            if (sv.empty()) return pos <= byte_size_ ? pos : npos;
            if (sv.size() > byte_size_) return npos;
            std::string_view self(data_, byte_size_);
            size_t found = self.find(sv, pos);
            return found == std::string_view::npos ? npos : found;
        }
        if (st == 0) { ensure_cp_count(); st = cp_info_state_; }
        if (sv.empty()) return pos <= cp_count_ ? pos : npos;
        if (sv.size() > byte_size_) return npos;
        std::string_view self(data_, byte_size_);
        // 位置 pos=0 快速路径: 跳过 byte_pos 计算
        if (pos == 0)
        {
            size_t found = self.find(sv, 0);
            if (found == std::string_view::npos) return npos;
            // 内联 uniform 快速路径 (省 byte_idx_to_cp_idx 函数调用 + 冗余分支)
            switch (uniform_byte_len_)
            {
                case 2: return found >> 1;
                case 3: return found / 3;
                case 4: return found >> 2;
                default: break;
            }
            return byte_idx_to_cp_idx(found);  // 非均匀: 回退到完整逻辑
        }
        // 均匀码点: O(1) 乘法计算 byte_pos (无需 SWAR 推进)
        if (uniform_byte_len_ != 0)
        {
            size_t byte_pos = (pos < cp_count_) ? pos * uniform_byte_len_ : byte_size_;
            size_t found = self.find(sv, byte_pos);
            if (found == std::string_view::npos) return npos;
            switch (uniform_byte_len_)
            {
                case 2: return found >> 1;
                case 3: return found / 3;
                case 4: return found >> 2;
                default: break;
            }
            return byte_idx_to_cp_idx(found);
        }
        // 状态 3 非均匀: SWAR 推进 + 字节级搜索
        if (st == 3)
        {
            size_t byte_pos = (pos < cp_count_) ? cp_idx_to_byte_offset_swar(pos) : byte_size_;
            size_t found = self.find(sv, byte_pos);
            if (found == std::string_view::npos) return npos;
            return byte_idx_to_cp_idx(found);
        }
        // 状态 2: O(1) 偏移 + 字节级搜索
        size_t byte_pos = (pos < cp_count_) ? cp_byte_offset(pos) : byte_size_;
        size_t found = self.find(sv, byte_pos);
        if (found == std::string_view::npos) return npos;
        return byte_idx_to_cp_idx(found);
    }

    [[nodiscard]] size_t rfind(char32_t cp, size_t pos = npos) const noexcept
    {
        ensure_cp_count();
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        // 纯 ASCII 快速路径 (ASCII 串): 反向扫描字节 (码点索引 = 字节索引)
        if (static_cast<uint32_t>(cp) < 0x80 && cp_info_state_ == 1)
        {
            const char* p = data_ + pos;
            while (p >= data_)
            {
                if (*p == static_cast<char>(cp)) return static_cast<size_t>(p - data_);
                --p;
            }
            return npos;
        }
        // 预编码 cp 为 UTF-8 字节
        uint8_t enc[4];
        size_t enc_len = 0;
        if (!detail_utf8::utf8_encode_one(static_cast<uint32_t>(cp), enc, &enc_len))
        {
            (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &enc_len);
        }
        // 纯 ASCII 串 + 非 ASCII cp: 不可能匹配 (ASCII 串无多字节字符)
        if (cp_info_state_ == 1) return npos;
        const char first = static_cast<char>(enc[0]);
        // 均匀码点: O(1) 乘法计算 byte offset (无需 cp_offsets_)
        if (uniform_byte_len_ != 0)
        {
            size_t i = pos + 1;
            while (i > 0)
            {
                --i;
                size_t start = i * uniform_byte_len_;
                if (data_[start] != first) continue;
                if (std::memcmp(data_ + start, enc, enc_len) == 0) return i;
            }
            return npos;
        }
        // 非均匀: 码点级反向匹配, 直接访问 cp_offsets_ + 首字节过滤
        ensure_cp_info();
        const uint32_t* offs = cp_offsets_;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            size_t start = offs[i];
            if (data_[start] != first) continue;
            if (std::memcmp(data_ + start, enc, enc_len) == 0) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t rfind(const utf8pp& str, size_t pos = npos) const noexcept
    {
        ensure_cp_count();
        str.ensure_cp_count();
        if (str.cp_count_ == 0) return pos < cp_count_ ? pos : cp_count_;
        if (str.cp_count_ > cp_count_) return npos;
        if (pos > cp_count_ - str.cp_count_) pos = cp_count_ - str.cp_count_;
        const char first = str.data_[0];
        const size_t pat_bytes = str.byte_size_;
        // 纯 ASCII 快速路径: 字节级反向 memcmp (码点索引 = 字节索引)
        if (cp_info_state_ == 1)
        {
            std::string_view self(data_, byte_size_);
            std::string_view sv(str.data_, str.byte_size_);
            size_t byte_pos = (pos < cp_count_) ? pos + str.byte_size_ : byte_size_;
            size_t found = self.rfind(sv, byte_pos);
            return found == std::string_view::npos ? npos : found;
        }
        const size_t pat_cp = str.cp_count_;
        // 均匀码点: O(1) 乘法计算 byte offset (无需 cp_offsets_)
        if (uniform_byte_len_ != 0)
        {
            size_t ulen = uniform_byte_len_;
            size_t i = pos + 1;
            while (i > 0)
            {
                --i;
                size_t start = i * ulen;
                if (data_[start] != first) continue;
                size_t end = (i + pat_cp < cp_count_) ? (i + pat_cp) * ulen : byte_size_;
                if (end - start == pat_bytes && std::memcmp(data_ + start, str.data_, pat_bytes) == 0)
                {
                    return i;
                }
            }
            return npos;
        }
        // 非均匀: 码点级反向匹配, 直接访问 cp_offsets_
        ensure_cp_info();
        str.ensure_cp_info();
        const uint32_t* offs = cp_offsets_;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            size_t start = offs[i];
            // 首字节过滤: 跳过不匹配的起始位置 (避免 memcmp 开销)
            if (data_[start] != first) continue;
            size_t end = (i + pat_cp < cp_count_) ? offs[i + pat_cp] : byte_size_;
            if (end - start == pat_bytes && std::memcmp(data_ + start, str.data_, pat_bytes) == 0)
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
        ensure_cp_count();
        if (sv.empty()) return pos < cp_count_ ? pos : cp_count_;
        if (sv.size() > byte_size_) return npos;
        std::string_view self(data_, byte_size_);
        // 均匀码点: O(1) 乘法计算 byte_pos (无需 cp_offsets_)
        size_t byte_pos;
        if (uniform_byte_len_ != 0)
        {
            byte_pos = (pos < cp_count_) ? pos * uniform_byte_len_ : byte_size_;
        }
        else if (cp_info_state_ == 1)
        {
            byte_pos = (pos < cp_count_) ? pos : byte_size_;
        }
        else
        {
            ensure_cp_info();
            byte_pos = (pos < cp_count_) ? cp_byte_offset(pos) : byte_size_;
        }
        size_t found = self.rfind(sv, byte_pos);
        if (found == std::string_view::npos) return npos;
        return byte_idx_to_cp_idx(found);
    }

    // === find_first_of / find_last_of / find_first_not_of / find_last_not_of ===
    // 函数 find_first_of(char32_t) 等价于 find(char32_t)
    [[nodiscard]] size_t find_first_of(char32_t cp, size_t pos = 0) const noexcept
    {
        return find(cp, pos);
    }

    [[nodiscard]] size_t find_first_of(const utf8pp& str, size_t pos = 0) const noexcept
    {
        ensure_cp_info();
        str.ensure_cp_info();
        if (str.cp_count_ == 0 || pos >= cp_count_) return npos;
        // 构建 ASCII 位图 (cp < 256) + 非 ASCII 标志
        uint64_t bitmap[4] = {0, 0, 0, 0};
        bool has_non_ascii = false;
        for (size_t j = 0; j < str.cp_count_; ++j)
        {
            uint32_t cp = str.cp_at_byte(str.cp_byte_offset(j));
            if (cp < 256) bitmap[cp >> 6] |= (1ULL << (cp & 63));
            else has_non_ascii = true;
        }
        for (size_t i = pos; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_byte_offset(i));
            if (cp < 256)
            {
                if (bitmap[cp >> 6] & (1ULL << (cp & 63))) return i;
            }
            else if (has_non_ascii)
            {
                // 回退: 逐个比较非 ASCII
                for (size_t j = 0; j < str.cp_count_; ++j)
                {
                    if (str.cp_at_byte(str.cp_byte_offset(j)) == cp) return i;
                }
            }
        }
        return npos;
    }

    // 函数 find_last_of(char32_t) 等价于 rfind(char32_t)
    [[nodiscard]] size_t find_last_of(char32_t cp, size_t pos = npos) const noexcept
    {
        return rfind(cp, pos);
    }

    [[nodiscard]] size_t find_last_of(const utf8pp& str, size_t pos = npos) const noexcept
    {
        ensure_cp_info();
        str.ensure_cp_info();
        if (cp_count_ == 0 || str.cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        // 构建 ASCII 位图 (cp < 256) + 非 ASCII 标志
        uint64_t bitmap[4] = {0, 0, 0, 0};
        bool has_non_ascii = false;
        for (size_t j = 0; j < str.cp_count_; ++j)
        {
            uint32_t cp = str.cp_at_byte(str.cp_byte_offset(j));
            if (cp < 256) bitmap[cp >> 6] |= (1ULL << (cp & 63));
            else has_non_ascii = true;
        }
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            uint32_t cp = cp_at_byte(cp_byte_offset(i));
            if (cp < 256)
            {
                if (bitmap[cp >> 6] & (1ULL << (cp & 63))) return i;
            }
            else if (has_non_ascii)
            {
                for (size_t j = 0; j < str.cp_count_; ++j)
                {
                    if (str.cp_at_byte(str.cp_byte_offset(j)) == cp) return i;
                }
            }
        }
        return npos;
    }

    [[nodiscard]] size_t find_first_not_of(char32_t cp, size_t pos = 0) const noexcept
    {
        ensure_cp_info();
        // 纯 ASCII 快速路径: 反向逻辑, 找第一个 != cp 的码点
        if (static_cast<uint32_t>(cp) < 0x80 && cp_info_state_ == 1)
        {
            for (size_t i = pos; i < cp_count_; ++i)
            {
                if (static_cast<uint8_t>(data_[i]) != static_cast<uint32_t>(cp)) return i;
            }
            return npos;
        }
        for (size_t i = pos; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_byte_offset(i))) != cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_first_not_of(const utf8pp& str, size_t pos = 0) const noexcept
    {
        ensure_cp_info();
        str.ensure_cp_info();
        if (str.cp_count_ == 0) return pos < cp_count_ ? pos : npos;
        // 构建 ASCII 位图 (cp < 256) + 非 ASCII 标志
        uint64_t bitmap[4] = {0, 0, 0, 0};
        bool has_non_ascii = false;
        for (size_t j = 0; j < str.cp_count_; ++j)
        {
            uint32_t cp = str.cp_at_byte(str.cp_byte_offset(j));
            if (cp < 256) bitmap[cp >> 6] |= (1ULL << (cp & 63));
            else has_non_ascii = true;
        }
        for (size_t i = pos; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_byte_offset(i));
            bool in_set = (cp < 256)
                ? (bitmap[cp >> 6] & (1ULL << (cp & 63))) != 0
                : (has_non_ascii && [&]{
                    for (size_t j = 0; j < str.cp_count_; ++j)
                        if (str.cp_at_byte(str.cp_byte_offset(j)) == cp) return true;
                    return false;
                  }());
            if (!in_set) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_last_not_of(char32_t cp, size_t pos = npos) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        // 纯 ASCII 快速路径
        if (static_cast<uint32_t>(cp) < 0x80 && cp_info_state_ == 1)
        {
            size_t i = pos + 1;
            while (i > 0)
            {
                --i;
                if (static_cast<uint8_t>(data_[i]) != static_cast<uint32_t>(cp)) return i;
            }
            return npos;
        }
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            if (char32_t(cp_at_byte(cp_byte_offset(i))) != cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_last_not_of(const utf8pp& str, size_t pos = npos) const noexcept
    {
        ensure_cp_info();
        str.ensure_cp_info();
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        if (str.cp_count_ == 0) return pos;
        // 构建 ASCII 位图 (cp < 256) + 非 ASCII 标志
        uint64_t bitmap[4] = {0, 0, 0, 0};
        bool has_non_ascii = false;
        for (size_t j = 0; j < str.cp_count_; ++j)
        {
            uint32_t cp = str.cp_at_byte(str.cp_byte_offset(j));
            if (cp < 256) bitmap[cp >> 6] |= (1ULL << (cp & 63));
            else has_non_ascii = true;
        }
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            uint32_t cp = cp_at_byte(cp_byte_offset(i));
            bool in_set = (cp < 256)
                ? (bitmap[cp >> 6] & (1ULL << (cp & 63))) != 0
                : (has_non_ascii && [&]{
                    for (size_t j = 0; j < str.cp_count_; ++j)
                        if (str.cp_at_byte(str.cp_byte_offset(j)) == cp) return true;
                    return false;
                  }());
            if (!in_set) return i;
        }
        return npos;
    }

    // 函数 find_*_of 的 const char* / string_view 重载 (委托 utf8pp 版本)
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
        ensure_cp_info();
        if (cp_count_ == 0) return -1;
        if (cp_count_ == 1)
        {
            char32_t mine = char32_t(cp_at_byte(cp_byte_offset(0)));
            if (mine < cp) return -1;
            if (mine > cp) return 1;
            return 0;
        }
        // 取首码点比较, 多余码点视为大于
        char32_t mine = char32_t(cp_at_byte(cp_byte_offset(0)));
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
