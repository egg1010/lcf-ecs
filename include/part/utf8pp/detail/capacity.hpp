// capacity.hpp - 容量/访问/迭代器接口

    // === 容量 ===
    [[nodiscard]] size_t size() const noexcept { ensure_cp_info(); return cp_count_; }
    [[nodiscard]] size_t length() const noexcept { ensure_cp_info(); return cp_count_; }
    [[nodiscard]] size_t byte_size() const noexcept { return byte_size_; }
    [[nodiscard]] size_t capacity() const noexcept { return byte_capacity_; }
    [[nodiscard]] bool empty() const noexcept { return byte_size_ == 0; }
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
        ensure_cp_info();
        if (cp_idx >= cp_count_) return byte_size_;
        return cp_byte_offset(cp_idx);
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
        invalidate_cp_info();
        byte_size_ = 0;
        if (data_) data_[0] = '\0';
    }

    void shrink_to_fit()
    {
        // SSO 模式: 无堆分配可缩减
        if (is_sso()) return;
        // heap 模式: 内容可放入 SSO 时回退到 SSO
        if (byte_size_ <= SSO_CAPACITY)
        {
            char tmp_bytes[SSO_CAPACITY + 1];
            std::memcpy(tmp_bytes, data_, byte_size_ + 1);
            utf8pp_free(data_);
            if (cp_offsets_) utf8pp_free(cp_offsets_);
            data_ = sso_buffer_;
            cp_offsets_ = nullptr;
            cp_offsets_capacity_ = 0;
            byte_capacity_ = SSO_CAPACITY;
            cp_count_ = 0;
            cp_info_state_ = 0;
            std::memcpy(sso_buffer_, tmp_bytes, byte_size_ + 1);
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
        // heap 模式: 缩减码点偏移缓冲区到实际大小 (仅已构建时)
        if (cp_info_state_ == 2 && cp_offsets_capacity_ > cp_count_)
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

    // === 码点访问 ===
    [[nodiscard]] char32_t at(size_t cp_idx) const noexcept
    {
        ensure_cp_info();
        if (cp_idx >= cp_count_ || !data_) return U'\uFFFD';
        return char32_t(cp_at_byte(cp_byte_offset(cp_idx)));
    }

    [[nodiscard]] char32_t operator[](size_t cp_idx) const noexcept
    {
        ensure_cp_info();
        if (cp_idx >= cp_count_ || !data_) return U'\uFFFD';
        return char32_t(cp_at_byte(cp_byte_offset(cp_idx)));
    }

    [[nodiscard]] char32_t front() const noexcept { return at(0); }
    [[nodiscard]] char32_t back() const noexcept { ensure_cp_info(); return at(cp_count_ > 0 ? cp_count_ - 1 : 0); }

    // === 字节指针访问 ===
    [[nodiscard]] const char* c_str() const noexcept { return data_ ? data_ : ""; }
    [[nodiscard]] const char* data() const noexcept { return data_ ? data_ : ""; }
    // C++17 风格: 非 const data(), 允许直接修改字节缓冲区
    // 注意: 修改后必须调用 rebuild_cp_offsets() 重建码点偏移缓存, 否则码点级接口行为未定义
    [[nodiscard]] char* data() noexcept { return data_; }
    // 直接修改 data() 后, 调用此函数重建码点偏移缓存 (若 byte_size_ 也变化需先更新)
    void rebuild_cp_offsets() noexcept
    {
        invalidate_cp_info();
        ensure_cp_info();
    }
    // 重建并设置新的字节大小 (直接修改 data() 后的便捷接口)
    void rebuild(size_t new_byte_size) noexcept
    {
        byte_size_ = new_byte_size;
        if (data_) data_[byte_size_] = '\0';
        invalidate_cp_info();
        ensure_cp_info();
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
