// string_ops.hpp - 字符串操作 (replace/trim/pad/reverse/format/split/join/BOM/valid)

    // === 替换 ===
    utf8pp& replace(size_t pos, size_t n, const utf8pp& str)
    {
        ensure_cp_info();
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, str);
        return *this;
    }

    utf8pp& replace(size_t pos, size_t n, const char* s)
    {
        ensure_cp_info();
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(s));
        return *this;
    }

    utf8pp& replace(size_t pos, size_t n, std::string_view sv)
    {
        ensure_cp_info();
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(sv));
        return *this;
    }
    // 替换为 C 字符串前 n2 字节 (与 std::string::replace(pos, n, s, n2) 对齐)
    utf8pp& replace(size_t pos, size_t n, const char* s, size_t n2)
    {
        ensure_cp_info();
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(s, n2));
        return *this;
    }
    // fill-replace: 替换为 n2 个 cp (与 std::string::replace(pos, n, n2, char) 对齐)
    utf8pp& replace(size_t pos, size_t n, size_t n2, char32_t cp)
    {
        ensure_cp_info();
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(n2, cp));
        return *this;
    }
    // initializer_list replace (与 std::string::replace(pos, count, initializer_list) 对齐)
    utf8pp& replace(size_t pos, size_t n, std::initializer_list<char32_t> il)
    {
        ensure_cp_info();
        if (pos >= cp_count_) return *this;
        if (n > cp_count_ - pos) n = cp_count_ - pos;
        erase(pos, n);
        insert_str(pos, utf8pp(il));
        return *this;
    }
    // 迭代器范围 replace (与 std::string 迭代器版对齐)
    utf8pp& replace(const_iterator first, const_iterator last, const utf8pp& str)
    {
        ensure_cp_info();
        size_t pos = iterator_to_cp_idx(first);
        size_t end_idx = iterator_to_cp_idx(last);
        if (pos >= cp_count_) return *this;
        if (end_idx > cp_count_) end_idx = cp_count_;
        return replace(pos, end_idx - pos, str);
    }
    utf8pp& replace(const_iterator first, const_iterator last, const char* s)
    {
        ensure_cp_info();
        size_t pos = iterator_to_cp_idx(first);
        size_t end_idx = iterator_to_cp_idx(last);
        if (pos >= cp_count_) return *this;
        if (end_idx > cp_count_) end_idx = cp_count_;
        return replace(pos, end_idx - pos, s);
    }
    utf8pp& replace(const_iterator first, const_iterator last, std::string_view sv)
    {
        ensure_cp_info();
        size_t pos = iterator_to_cp_idx(first);
        size_t end_idx = iterator_to_cp_idx(last);
        if (pos >= cp_count_) return *this;
        if (end_idx > cp_count_) end_idx = cp_count_;
        return replace(pos, end_idx - pos, sv);
    }
    utf8pp& replace(const_iterator first, const_iterator last, const char* s, size_t n2)
    {
        ensure_cp_info();
        size_t pos = iterator_to_cp_idx(first);
        size_t end_idx = iterator_to_cp_idx(last);
        if (pos >= cp_count_) return *this;
        if (end_idx > cp_count_) end_idx = cp_count_;
        return replace(pos, end_idx - pos, s, n2);
    }
    utf8pp& replace(const_iterator first, const_iterator last, size_t n2, char32_t cp)
    {
        ensure_cp_info();
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
        ensure_cp_info();
        old_str.ensure_cp_info();
        new_str.ensure_cp_info();
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
        ensure_cp_info();
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_byte_offset(i))) == old_cp)
            {
                replace_cp_at(i, new_cp);
            }
        }
        return *this;
    }

    // === trim ===
    utf8pp& trim_left()
    {
        ensure_cp_info();
        size_t i = 0;
        while (i < cp_count_ && is_space_cp(char32_t(cp_at_byte(cp_byte_offset(i)))))
        {
            ++i;
        }
        if (i > 0) erase(0, i);
        return *this;
    }

    utf8pp& trim_right()
    {
        ensure_cp_info();
        size_t i = cp_count_;
        while (i > 0)
        {
            --i;
            if (!is_space_cp(char32_t(cp_at_byte(cp_byte_offset(i))))) break;
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
        ensure_cp_info();
        size_t i = 0;
        while (i < cp_count_ && pred(char32_t(cp_at_byte(cp_byte_offset(i))))) ++i;
        if (i > 0) erase(0, i);
        return *this;
    }
    template <typename Pred, typename = std::enable_if_t<
        std::is_invocable_r_v<bool, Pred, char32_t>>>
    utf8pp& trim_right(Pred pred)
    {
        ensure_cp_info();
        size_t i = cp_count_;
        while (i > 0)
        {
            --i;
            if (!pred(char32_t(cp_at_byte(cp_byte_offset(i))))) break;
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
        ensure_cp_info();
        size_t w = 0;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            w += static_cast<size_t>(unicode_data::cp_display_width(cp_at_byte(cp_byte_offset(i))));
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

    // === 反转 (码点级) ===
    utf8pp& reverse()
    {
        ensure_cp_info();
        if (cp_count_ <= 1) return *this;
        char* new_data = static_cast<char*>(utf8pp_alloc(byte_size_ + 1));
        if (!new_data) std::abort();
        size_t write_pos = 0;
        for (size_t i = cp_count_; i > 0; --i)
        {
            size_t idx = i - 1;
            size_t start = cp_byte_offset(idx);
            size_t end = (idx + 1 < cp_count_) ? cp_byte_offset(idx + 1) : byte_size_;
            size_t len = end - start;
            std::memcpy(new_data + write_pos, data_ + start, len);
            write_pos += len;
        }
        new_data[byte_size_] = '\0';
        // SSO 模式下 data_ 是 sso_buffer_ (栈), 不能 free
        bool was_sso = is_sso();
        if (!was_sso) utf8pp_free(data_);
        data_ = new_data;
        byte_capacity_ = byte_size_;
        // 字节已反转, 失效码点信息, 下次访问惰性重建
        invalidate_cp_info();
        return *this;
    }

    [[nodiscard]] utf8pp reversed() const { utf8pp t(*this); t.reverse(); return t; }

    // === format (printf 风格静态构造, 类内声明; 类外定义) ===
    [[nodiscard]] static utf8pp format(const char* fmt, ...);
    [[nodiscard]] static utf8pp vformat(const char* fmt, std::va_list ap);

    // === split (返回 dense<utf8pp>) ===
    [[nodiscard]] dense<utf8pp> split(char32_t delim) const
    {
        ensure_cp_info();
        dense<utf8pp> result;
        if (cp_count_ == 0) return result;
        size_t start = 0;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_byte_offset(i))) == delim)
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
        ensure_cp_info();
        delim.ensure_cp_info();
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
        ensure_cp_info();
        dense<utf8_view> result;
        if (cp_count_ == 0) return result;
        size_t start = 0;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_byte_offset(i))) == delim)
            {
                result.push_back(utf8_view(data_ + cp_byte_offset(start), cp_byte_offset(i) - cp_byte_offset(start)));
                start = i + 1;
            }
        }
        result.push_back(utf8_view(data_ + cp_byte_offset(start), byte_size_ - cp_byte_offset(start)));
        return result;
    }

    [[nodiscard]] dense<utf8_view> split_view(const utf8pp& delim) const
    {
        ensure_cp_info();
        delim.ensure_cp_info();
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
            result.push_back(utf8_view(data_ + cp_byte_offset(start), cp_byte_offset(found) - cp_byte_offset(start)));
            pos = found + delim.cp_count_;
            start = pos;
        }
        result.push_back(utf8_view(data_ + cp_byte_offset(start), byte_size_ - cp_byte_offset(start)));
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
        invalidate_cp_info(); // 失效码点信息, 下次访问惰性重建
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
