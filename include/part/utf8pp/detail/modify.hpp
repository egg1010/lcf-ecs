// modify.hpp - 修改操作


    // === 修改 ===
    void push_back(char32_t cp) { ensure_cp_info(); insert(cp_count_, cp); }

    void append(const char* s) { append(s, s ? std::strlen(s) : 0); }

    void append(const char8_t* s)
    {
        const char* p = reinterpret_cast<const char*>(s);
        append(p, p ? std::strlen(p) : 0);
    }

    void append(const char* s, size_t byte_len)
    {
        if (byte_len == 0) return;
        ensure_cp_info();
        // 若当前为 ASCII, 提升为偏移缓存以增量维护
        if (cp_info_state_ == 1) promote_ascii_to_offsets();

        const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
        const uint8_t* end = p + byte_len;
        ensure_byte_capacity(byte_size_ + byte_len);
        ensure_cp_capacity(cp_count_ + byte_len);

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
        ensure_cp_info();
        if (cp_idx > cp_count_) cp_idx = cp_count_;
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
        return *this;
    }

    void erase(size_t cp_idx, size_t n = 1)
    {
        ensure_cp_info();
        if (cp_idx >= cp_count_) return;
        if (n > cp_count_ - cp_idx) n = cp_count_ - cp_idx;
        if (n == 0) return;
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
        ensure_cp_info();
        if (cp_idx > cp_count_) cp_idx = cp_count_;
        utf8pp tmp(n, cp);
        insert_str(cp_idx, tmp);
        return *this;
    }
    // initializer_list 插入
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
        return const_iterator(data_ + cp_byte_offset(idx), data_ + byte_size_);
    }

    const_iterator insert(const_iterator pos, size_t n, char32_t cp)
    {
        size_t idx = iterator_to_cp_idx(pos);
        for (size_t i = 0; i < n; ++i) insert(idx + i, cp);
        return const_iterator(data_ + cp_byte_offset(idx), data_ + byte_size_);
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
        return const_iterator(data_ + cp_byte_offset(idx), data_ + byte_size_);
    }

    // 迭代器版 insert: 字符串 / string_view / initializer_list
    const_iterator insert(const_iterator pos, const utf8pp& str)
    {
        size_t idx = iterator_to_cp_idx(pos);
        insert_str(idx, str);
        return const_iterator(data_ + (idx < cp_count_ ? cp_byte_offset(idx) : byte_size_), data_ + byte_size_);
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
        if (idx < cp_count_) return const_iterator(data_ + cp_byte_offset(idx), data_ + byte_size_);
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
        if (idx_first < cp_count_) return const_iterator(data_ + cp_byte_offset(idx_first), data_ + byte_size_);
        return end();
    }

    [[nodiscard]] utf8pp substr(size_t pos, size_t cp_count = npos) const
    {
        ensure_cp_info();
        if (pos >= cp_count_) return utf8pp();
        if (cp_count > cp_count_ - pos) cp_count = cp_count_ - pos;
        size_t start_byte = cp_byte_offset(pos);
        size_t end_byte = (pos + cp_count < cp_count_) ? cp_byte_offset(pos + cp_count) : byte_size_;
        return utf8pp(data_ + start_byte, end_byte - start_byte);
    }

    // === 重复填充 / 调整长度 ===
    void append_cp(size_t n, char32_t cp)
    {
        if (n == 0) return;
        ensure_cp_info();
        if (cp_info_state_ == 1) promote_ascii_to_offsets();

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
    // std::string 兼容别名: resize(n) 用 U'\0' 填充, resize(n, cp) 用指定码点填充
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

