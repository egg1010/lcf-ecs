// predicates.hpp - 谓词

    [[nodiscard]] bool is_all_alpha() const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_alpha(char32_t(cp_at_byte(cp_byte_offset(i))))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_digit() const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_digit(char32_t(cp_at_byte(cp_byte_offset(i))))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_alnum() const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_alnum(char32_t(cp_at_byte(cp_byte_offset(i))))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_space() const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_space(char32_t(cp_at_byte(cp_byte_offset(i))))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_xdigit() const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_xdigit(char32_t(cp_at_byte(cp_byte_offset(i))))) return false;
        return true;
    }
    [[nodiscard]] bool is_all_printable() const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
            if (!is_printable(char32_t(cp_at_byte(cp_byte_offset(i))))) return false;
        return true;
    }

    // === 串级 Script 判断 ===
    // 返回首字符的脚本 (空串返回 unknown; 首字符为组合标记时为 inherited)
    [[nodiscard]] script script_of() const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return script::unknown;
        return unicode_data::script_of(cp_at_byte(cp_byte_offset(0)));
    }
    // 整串是否全部属于指定脚本 (common/inherited 视为通配, 不影响判断; 空串返回 false)
    [[nodiscard]] bool is_all_script(script s) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            script sc = unicode_data::script_of(cp_at_byte(cp_byte_offset(i)));
            if (sc == script::common || sc == script::inherited) continue;
            if (sc != s) return false;
        }
        return true;
    }
    // 是否包含至少一个指定脚本的码点
    [[nodiscard]] bool contains_script(script s) const noexcept
    {
        ensure_cp_info();
        for (size_t i = 0; i < cp_count_; ++i)
            if (unicode_data::script_of(cp_at_byte(cp_byte_offset(i))) == s) return true;
        return false;
    }

    // === 前缀/后缀判断 ===
    [[nodiscard]] bool starts_with(char32_t cp) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        return char32_t(cp_at_byte(cp_byte_offset(0))) == cp;
    }

    [[nodiscard]] bool starts_with(const utf8pp& prefix) const noexcept
    {
        ensure_cp_info();
        prefix.ensure_cp_info();
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
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        return char32_t(cp_at_byte(cp_byte_offset(cp_count_ - 1))) == cp;
    }

    [[nodiscard]] bool ends_with(const utf8pp& suffix) const noexcept
    {
        ensure_cp_info();
        suffix.ensure_cp_info();
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
        ensure_cp_info();
        size_t n = 0;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_byte_offset(i))) == cp) ++n;
        }
        return n;
    }

    [[nodiscard]] size_t count(const utf8pp& str) const noexcept
    {
        ensure_cp_info();
        str.ensure_cp_info();
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
