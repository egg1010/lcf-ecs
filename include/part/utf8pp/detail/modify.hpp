// 修改操作


    // === 修改 ===
    void push_back(char32_t cp) { ensure_cp_info(); insert(cp_count_, cp); }

    void append(const char* s) { append(s, s ? std::strlen(s) : 0); }

    void append(const char8_t* s)
    {
        const char* p = reinterpret_cast<const char*>(s);
        append(p, p ? std::strlen(p) : 0);
    }

    FORCE_INLINE void append(const char* s, size_t byte_len)
    {
        if (byte_len == 0) return;
        invalidate_cp_cache();  // 字节内容将变更, 失效预解码缓存 (在 cp_count_ 变更前)
        ensure_byte_capacity(byte_size_ + byte_len);
        std::memcpy(data_ + byte_size_, s, byte_len);
        byte_size_ += static_cast<uint32_t>(byte_len);
        data_[byte_size_] = '\0';

        // 状态=0: ensure_cp_count 扫描全量缓冲 (含新字节), cp_count_ 已正确
        if (cp_info_state_ == 0) { ensure_cp_count(); return; }

        // 状态=3 热路径: 已计数, 未建偏移 (append 场景最常见)
        if (cp_info_state_ == 3) [[likely]]
        {
            // 均匀 3 字节快速路径 (中文): 常量除数转 GCC 倒数乘法
            if (uniform_byte_len_ == 3) [[likely]]
            {
                if (byte_len % 3 == 0) [[likely]]
                {
                    const uint8_t* q = reinterpret_cast<const uint8_t*>(data_ + byte_size_ - byte_len);
                    bool uniform_ok = true;
                    for (size_t off = 0; off < byte_len; off += 3)
                    {
                        if ((q[off] & 0xF0) != 0xE0) { uniform_ok = false; break; }
                    }
                    if (uniform_ok) [[likely]]
                    {
                        cp_count_ += static_cast<uint32_t>(byte_len / 3);
                        return;
                    }
                }
            }
            else if (uniform_byte_len_ != 0 && byte_len % uniform_byte_len_ == 0)
            {
                const uint8_t* q = reinterpret_cast<const uint8_t*>(data_ + byte_size_ - byte_len);
                size_t stride = uniform_byte_len_;
                bool uniform_ok = true;
                for (size_t off = 0; off < byte_len; off += stride)
                {
                    if (detail_utf8::k_utf8_seq_len[q[off]] != stride) { uniform_ok = false; break; }
                }
                if (uniform_ok)
                {
                    cp_count_ += static_cast<uint32_t>(byte_len / stride);
                    return;
                }
            }
            // 回退: SWAR 计数新码点
            size_t new_cps = 0;
            const uint8_t* q = reinterpret_cast<const uint8_t*>(data_ + byte_size_ - byte_len);
            const uint8_t* qend = q + byte_len;
            while (q + 8 <= qend)
            {
                uint64_t chunk;
                std::memcpy(&chunk, q, 8);
                uint64_t x = chunk & 0x8080808080808080ULL;
                uint64_t y = chunk & 0x4040404040404040ULL;
                uint64_t cont = x & ~(y << 1);
                uint64_t lead_mask = cont ^ 0x8080808080808080ULL;
                new_cps += std::popcount(lead_mask);
                q += 8;
            }
            while (q < qend) { if ((*q & 0xC0) != 0x80) ++new_cps; ++q; }
            cp_count_ += static_cast<uint32_t>(new_cps);
            uniform_byte_len_ = 0;  // 非均匀追加, 失效
            return;
        }

        if (cp_info_state_ == 1)
        {
            // 状态=1 (ASCII): SWAR 快速检测新字节是否含非 ASCII
            const uint8_t* p = reinterpret_cast<const uint8_t*>(data_ + byte_size_ - byte_len);
            const uint8_t* end = p + byte_len;
            bool has_non_ascii = false;
            while (p + 8 <= end)
            {
                uint64_t chunk;
                std::memcpy(&chunk, p, 8);
                if (chunk & 0x8080808080808080ULL) { has_non_ascii = true; break; }
                p += 8;
            }
            if (!has_non_ascii)
            {
                while (p < end) { if (*p & 0x80) { has_non_ascii = true; break; } ++p; }
            }
            if (has_non_ascii)
            {
                // 非 ASCII: SWAR 计数新码点, 转为 state=3
                size_t new_cps = 0;
                const uint8_t* q = reinterpret_cast<const uint8_t*>(data_ + byte_size_ - byte_len);
                const uint8_t* qend = q + byte_len;
                while (q + 8 <= qend)
                {
                    uint64_t chunk;
                    std::memcpy(&chunk, q, 8);
                    uint64_t x = chunk & 0x8080808080808080ULL;
                    uint64_t y = chunk & 0x4040404040404040ULL;
                    uint64_t cont = x & ~(y << 1);
                    uint64_t lead_mask = cont ^ 0x8080808080808080ULL;
                    new_cps += std::popcount(lead_mask);
                    q += 8;
                }
                while (q < qend) { if ((*q & 0xC0) != 0x80) ++new_cps; ++q; }
                cp_count_ += static_cast<uint32_t>(new_cps);
                cp_info_state_ = 3;
                // 检测新串是否均匀 (首次非 ASCII 追加后, 串可能全为 3 字节中文)
                // 使后续 append 走 uniform 快速路径, 避免 SWAR 回退
                detect_uniform_byte_len();
            }
            else
            {
                cp_count_ += static_cast<uint32_t>(byte_len);  // 纯 ASCII: 码点数 = 字节数
            }
            return;
        }

        // 状态=2: 增量维护 cp_offsets_ (保持 O(1) 随机访问)
        {
            const uint8_t* base = reinterpret_cast<const uint8_t*>(data_);
            const uint8_t* q = base + byte_size_ - byte_len;
            const uint8_t* qend = base + byte_size_;
            ensure_cp_capacity(cp_count_ + byte_len);  // 上限: 每字节 1 码点
            while (q + 8 <= qend)
            {
                uint64_t chunk;
                std::memcpy(&chunk, q, 8);
                uint64_t x = chunk & 0x8080808080808080ULL;
                uint64_t y = chunk & 0x4040404040404040ULL;
                uint64_t cont = x & ~(y << 1);
                uint64_t lead_mask = cont ^ 0x8080808080808080ULL;
                size_t chunk_off = static_cast<size_t>(q - base);
                while (lead_mask)
                {
                    int bit = std::countr_zero(lead_mask);
                    cp_offsets_[cp_count_++] = static_cast<uint32_t>(chunk_off + (static_cast<size_t>(bit) >> 3));
                    lead_mask &= lead_mask - 1;
                }
                q += 8;
            }
            while (q < qend)
            {
                if ((*q & 0xC0) != 0x80)
                {
                    cp_offsets_[cp_count_++] = static_cast<uint32_t>(q - base);
                }
                ++q;
            }
            recheck_uniform();
        }
    }

    void append(const char32_t* s, size_t cp_count)
    {
        if (cp_count == 0) return;
        invalidate_cp_cache();  // 字节内容将变更, 失效预解码缓存 (在 cp_count_ 变更前)
        ensure_cp_count();
        bool was_state_2 = (cp_info_state_ == 2);
        // 保守预分配: 每码点最多 4 字节 (单遍扫描, 双遍预计算的额外开销不抵消缓存收益)
        ensure_byte_capacity(byte_size_ + cp_count * 4);

        // 先编码全部码点 (不维护 cp_offsets_), 计算实际字节数 + 检测是否全 ASCII
        size_t cur_byte = byte_size_;
        bool all_ascii = true;
        for (size_t i = 0; i < cp_count; ++i)
        {
            uint8_t enc[4];
            size_t len = 0;
            uint32_t cp = static_cast<uint32_t>(s[i]);
            if (!detail_utf8::utf8_encode_one(cp, enc, &len))
            {
                (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &len);
            }
            if (cp >= 0x80) all_ascii = false;
            std::memcpy(data_ + cur_byte, enc, len);
            cur_byte += len;
        }

        if (was_state_2)
        {
            // 已构建 cp_offsets_, 增量维护
            ensure_cp_capacity(cp_count_ + cp_count);
            size_t write_byte = byte_size_;
            for (size_t i = 0; i < cp_count; ++i)
            {
                uint8_t enc[4];
                size_t len = 0;
                uint32_t cp = static_cast<uint32_t>(s[i]);
                if (!detail_utf8::utf8_encode_one(cp, enc, &len))
                {
                    (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &len);
                }
                cp_offsets_[cp_count_++] = static_cast<uint32_t>(write_byte);
                write_byte += len;
            }
        }
        else
        {
            cp_count_ += static_cast<uint32_t>(cp_count);
            if (cp_info_state_ == 1 && !all_ascii) cp_info_state_ = 3;
            else if (cp_info_state_ == 0) cp_info_state_ = all_ascii ? 1 : 3;
        }

        byte_size_ = static_cast<uint32_t>(cur_byte);
        data_[byte_size_] = '\0';
    }

    void append(const utf8pp& other) { append(other.data_, other.byte_size_); }
    void append(std::string_view sv) { append(sv.data(), sv.size()); }
    void append(const std::string& s) { append(s.data(), s.size()); }
    void append(const std::u8string& s) { append(reinterpret_cast<const char*>(s.data()), s.size()); }
    void append(const std::u32string& s) { append(s.data(), s.size()); }
    void append(std::initializer_list<char32_t> il) { append(il.begin(), il.size()); }
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
        ensure_cp_info();
        if (cp_idx > cp_count_) cp_idx = cp_count_;
        invalidate_cp_cache();  // 字节内容将变更, 失效预解码缓存 (在 cp_count_ 变更前)
        if (cp_info_state_ == 1) promote_ascii_to_offsets();

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
        recheck_uniform();
        return *this;
    }

    void erase(size_t cp_idx, size_t n = 1)
    {
        ensure_cp_info();
        if (cp_idx >= cp_count_) return;
        if (n > cp_count_ - cp_idx) n = cp_count_ - cp_idx;
        if (n == 0) return;
        invalidate_cp_cache();  // 字节内容将变更, 失效预解码缓存 (在 cp_count_ 变更前)
        if (cp_info_state_ == 1) promote_ascii_to_offsets();

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
        recheck_uniform();
    }

    // === insert 字符串重载 (返回 *this 支持链式调用, 与 std::string 对齐) ===
    utf8pp& insert(size_t cp_idx, const utf8pp& str) { insert_str(cp_idx, str); return *this; }
    utf8pp& insert(size_t cp_idx, const char* s) { if (s) insert_str(cp_idx, utf8pp(s)); return *this; }
    utf8pp& insert(size_t cp_idx, std::string_view sv) { insert_str(cp_idx, utf8pp(sv)); return *this; }
    utf8pp& insert(size_t cp_idx, const char* s, size_t byte_len) { insert_str(cp_idx, utf8pp(s, byte_len)); return *this; }
    // 子串插入: 从 str 的 pos2 开始取 n2 个码点插入 (与 std::string::insert(index, str, index_str, count) 对齐)
    utf8pp& insert(size_t cp_idx, const utf8pp& str, size_t pos2, size_t n2) { insert_str(cp_idx, str.substr(pos2, n2)); return *this; }
    // 填充插入: 在 cp_idx 处插入 n 个 cp (与 std::string::insert(pos, n, char) 对齐)
    utf8pp& insert(size_t cp_idx, size_t n, char32_t cp)
    {
        ensure_cp_info();
        if (cp_idx > cp_count_) cp_idx = cp_count_;
        utf8pp tmp(n, cp);
        insert_str(cp_idx, tmp);
        return *this;
    }
    // 初始化列表插入
    utf8pp& insert(size_t cp_idx, std::initializer_list<char32_t> il)
    {
        ensure_cp_info();
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
        return make_iterator(idx);
    }

    const_iterator insert(const_iterator pos, size_t n, char32_t cp)
    {
        size_t idx = iterator_to_cp_idx(pos);
        for (size_t i = 0; i < n; ++i) insert(idx + i, cp);
        return make_iterator(idx);
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
        return make_iterator(idx);
    }

    // 迭代器版 insert: 字符串 / string_view / initializer_list
    const_iterator insert(const_iterator pos, const utf8pp& str)
    {
        size_t idx = iterator_to_cp_idx(pos);
        insert_str(idx, str);
        return make_iterator(idx);
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
        return make_iterator(idx);
    }

    const_iterator erase(const_iterator first, const_iterator last)
    {
        size_t idx_first = iterator_to_cp_idx(first);
        size_t idx_last = iterator_to_cp_idx(last);
        if (idx_first >= cp_count_) return end();
        if (idx_last > cp_count_) idx_last = cp_count_;
        size_t n = idx_last - idx_first;
        erase(idx_first, n);
        return make_iterator(idx_first);
    }

    [[nodiscard]] utf8pp substr(size_t pos, size_t cp_count = npos) const
    {
        // 内联 state 检查: 大部分场景 state!=0 (构造后), 跳过函数调用
        if (cp_info_state_ == 0) [[unlikely]] ensure_cp_count();
        if (pos >= cp_count_) return utf8pp();
        if (cp_count > cp_count_ - pos) cp_count = cp_count_ - pos;

        size_t start_byte, sub_byte_len;
        if (cp_info_state_ == 1)
        {
            // 纯 ASCII: 字节偏移 = 码点偏移
            start_byte = pos;
            sub_byte_len = cp_count;
        }
        else if (uniform_byte_len_ != 0) [[likely]]
        {
            // 均匀码点: O(1) 乘法, 无需构建/查表 cp_offsets_
            start_byte = pos * uniform_byte_len_;
            sub_byte_len = cp_count * uniform_byte_len_;
        }
        else
        {
            // 非均匀: 需要完整 cp_offsets_
            ensure_cp_info();
            start_byte = cp_offsets_[pos];
            sub_byte_len = (pos + cp_count < cp_count_) ? (cp_offsets_[pos + cp_count] - static_cast<uint32_t>(start_byte)) : (byte_size_ - static_cast<uint32_t>(start_byte));
        }

        // 裸构造: 跳过默认构造的字段初始化, 手动设置避免冗余写入
        utf8pp result(raw_construct);
        result.cp_offsets_ = nullptr;
        result.cp_offsets_capacity_ = 0;
        result.cp_cache_ = nullptr;
        result.byte_size_ = static_cast<uint32_t>(sub_byte_len);
        result.cp_count_ = static_cast<uint32_t>(cp_count);

        if (sub_byte_len > SSO_CAPACITY)
        {
            result.data_ = static_cast<char*>(utf8pp_alloc(sub_byte_len + 1));
            if (!result.data_) std::abort();
            result.byte_capacity_ = static_cast<uint32_t>(sub_byte_len);
        }
        else
        {
            // 内联: data_ 指向 sso_buffer_, byte_capacity_ = SSO_CAPACITY
            result.data_ = result.sso_buffer_;
            result.byte_capacity_ = SSO_CAPACITY;
        }
        std::memcpy(result.data_, data_ + start_byte, sub_byte_len);
        result.data_[sub_byte_len] = '\0';

        // 设置状态/均匀性 (内联避免分支)
        // 均匀路径直接继承: substr 的 start_byte/sub_byte_len 已保证子串均匀
        // 非均匀父串 uniform_byte_len_=0, 子串继承 0 也正确
        if (cp_info_state_ == 1)
        {
            result.cp_info_state_ = 1;
            result.uniform_byte_len_ = 1;
        }
        else
        {
            result.cp_info_state_ = 3;
            result.uniform_byte_len_ = uniform_byte_len_;
        }
        return result;
    }

    // === 重复填充 / 调整长度 ===
    void append_cp(size_t n, char32_t cp)
    {
        if (n == 0) return;
        invalidate_cp_cache();  // 字节内容将变更, 失效预解码缓存 (在 cp_count_ 变更前)
        // 只需 cp_count_, 不强制构建 cp_offsets_
        ensure_cp_count();
        bool was_state_2 = (cp_info_state_ == 2);

        uint8_t enc[4];
        size_t enc_len = 0;
        if (!detail_utf8::utf8_encode_one(static_cast<uint32_t>(cp), enc, &enc_len))
        {
            (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &enc_len);
        }
        ensure_byte_capacity(byte_size_ + n * enc_len);

        if (was_state_2)
        {
            // 已构建 cp_offsets_, 增量维护
            ensure_cp_capacity(cp_count_ + n);
            for (size_t i = 0; i < n; ++i)
            {
                cp_offsets_[cp_count_] = static_cast<uint32_t>(byte_size_);
                ++cp_count_;
                std::memcpy(data_ + byte_size_, enc, enc_len);
                byte_size_ += enc_len;
            }
        }
        else
        {
            // 不构建 cp_offsets_, 只更新 cp_count_ + 写入字节
            // 纯 ASCII 填充保持/提升到 state=1; 非ASCII 填充提升到 state=3
            for (size_t i = 0; i < n; ++i)
            {
                std::memcpy(data_ + byte_size_, enc, enc_len);
                byte_size_ += enc_len;
            }
            cp_count_ += static_cast<uint32_t>(n);
            if (cp_info_state_ == 0)
            {
                cp_info_state_ = (enc_len == 1) ? 1 : 3;
            }
            else if (cp_info_state_ == 1 && enc_len > 1)
            {
                cp_info_state_ = 3;
            }
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
        ensure_cp_info();
        if (n < cp_count_)
        {
            erase(n, cp_count_ - n);
        }
        else if (n > cp_count_)
        {
            append_cp(n - cp_count_, cp);
        }
    }
    // 兼容别名: resize(n) 用 U'\0' 填充, resize(n, cp) 用指定码点填充
    void resize(size_t n) { resize_cp(n, U'\0'); }
    void resize(size_t n, char32_t cp) { resize_cp(n, cp); }

    void pop_back()
    {
        ensure_cp_info();
        if (cp_count_ > 0) erase(cp_count_ - 1, 1);
    }

    // === copy 拷贝到外部缓冲区 (返回拷贝字节数) ===
    size_t copy(char* buf, size_t n, size_t pos = 0) const
    {
        ensure_cp_info();
        if (pos >= cp_count_ || !buf) return 0;
        size_t avail = cp_count_ - pos;
        if (n > avail) n = avail;
        size_t start_byte = cp_byte_offset(pos);
        size_t end_byte = (pos + n < cp_count_) ? cp_byte_offset(pos + n) : byte_size_;
        size_t byte_n = end_byte - start_byte;
        std::memcpy(buf, data_ + start_byte, byte_n);
        return byte_n;
    }

    // === 范围 append: 从容器/裸指针/span ===
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

    utf8pp& append(std::span<const utf8pp> parts)
    {
        for (size_t i = 0; i < parts.size(); ++i) append(parts[i]);
        return *this;
    }

