// search.hpp - 查找/比较

    // === 查找 (字节级, UTF-8 保证字节序 = 码点序) ===
    [[nodiscard]] size_t find(char32_t cp, size_t pos = 0) const noexcept
    {
        ensure_cp_info();
        for (size_t i = pos; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_byte_offset(i))) == cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find(const utf8pp& str, size_t pos = 0) const noexcept
    {
        ensure_cp_info();
        str.ensure_cp_info();
        if (str.cp_count_ == 0) return pos <= cp_count_ ? pos : npos;
        if (str.cp_count_ > cp_count_) return npos;

        for (size_t i = pos; i + str.cp_count_ <= cp_count_; ++i)
        {
            size_t start = cp_byte_offset(i);
            size_t len = ((i + str.cp_count_ < cp_count_) ? cp_byte_offset(i + str.cp_count_) : byte_size_) - start;
            if (len == str.byte_size_ && std::memcmp(data_ + start, str.data_, len) == 0)
            {
                return i;
            }
        }
        return npos;
    }

    [[nodiscard]] size_t find(const char* s, size_t pos = 0) const noexcept
    {
        return find(std::string_view(s ? s : "", s ? std::strlen(s) : 0), pos);
    }

    [[nodiscard]] size_t find(std::string_view sv, size_t pos = 0) const noexcept
    {
        ensure_cp_info();
        // 字节级匹配: UTF-8 字节序 = 码点序, 合法 UTF-8 的匹配起始必为码点边界
        if (sv.empty()) return pos <= cp_count_ ? pos : npos;
        if (sv.size() > byte_size_) return npos;
        std::string_view self(data_ ? data_ : "", byte_size_);
        size_t byte_pos = (pos < cp_count_) ? cp_byte_offset(pos) : byte_size_;
        size_t found = self.find(sv, byte_pos);
        if (found == std::string_view::npos) return npos;
        return byte_idx_to_cp_idx(found);
    }

    [[nodiscard]] size_t rfind(char32_t cp, size_t pos = npos) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            if (char32_t(cp_at_byte(cp_byte_offset(i))) == cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t rfind(const utf8pp& str, size_t pos = npos) const noexcept
    {
        ensure_cp_info();
        str.ensure_cp_info();
        if (str.cp_count_ == 0) return pos < cp_count_ ? pos : cp_count_;
        if (str.cp_count_ > cp_count_) return npos;
        if (pos > cp_count_ - str.cp_count_) pos = cp_count_ - str.cp_count_;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            size_t start = cp_byte_offset(i);
            size_t len = ((i + str.cp_count_ < cp_count_) ? cp_byte_offset(i + str.cp_count_) : byte_size_) - start;
            if (len == str.byte_size_ && std::memcmp(data_ + start, str.data_, len) == 0)
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
        ensure_cp_info();
        if (sv.empty()) return pos < cp_count_ ? pos : cp_count_;
        if (sv.size() > byte_size_) return npos;
        std::string_view self(data_ ? data_ : "", byte_size_);
        size_t byte_pos = (pos < cp_count_) ? cp_byte_offset(pos) : byte_size_;
        size_t found = self.rfind(sv, byte_pos);
        if (found == std::string_view::npos) return npos;
        return byte_idx_to_cp_idx(found);
    }

    // === find_first_of / find_last_of / find_first_not_of / find_last_not_of ===
    [[nodiscard]] size_t find_first_of(char32_t cp, size_t pos = 0) const noexcept
    {
        ensure_cp_info();
        for (size_t i = pos; i < cp_count_; ++i)
        {
            if (char32_t(cp_at_byte(cp_byte_offset(i))) == cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_first_of(const utf8pp& str, size_t pos = 0) const noexcept
    {
        ensure_cp_info();
        str.ensure_cp_info();
        for (size_t i = pos; i < cp_count_; ++i)
        {
            char32_t cur = char32_t(cp_at_byte(cp_byte_offset(i)));
            for (size_t j = 0; j < str.cp_count_; ++j)
            {
                if (char32_t(str.cp_at_byte(str.cp_byte_offset(j))) == cur) return i;
            }
        }
        return npos;
    }

    [[nodiscard]] size_t find_last_of(char32_t cp, size_t pos = npos) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            if (char32_t(cp_at_byte(cp_byte_offset(i))) == cp) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_last_of(const utf8pp& str, size_t pos = npos) const noexcept
    {
        ensure_cp_info();
        str.ensure_cp_info();
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            char32_t cur = char32_t(cp_at_byte(cp_byte_offset(i)));
            for (size_t j = 0; j < str.cp_count_; ++j)
            {
                if (char32_t(str.cp_at_byte(str.cp_byte_offset(j))) == cur) return i;
            }
        }
        return npos;
    }

    [[nodiscard]] size_t find_first_not_of(char32_t cp, size_t pos = 0) const noexcept
    {
        ensure_cp_info();
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
        for (size_t i = pos; i < cp_count_; ++i)
        {
            char32_t cur = char32_t(cp_at_byte(cp_byte_offset(i)));
            bool found = false;
            for (size_t j = 0; j < str.cp_count_; ++j)
            {
                if (char32_t(str.cp_at_byte(str.cp_byte_offset(j))) == cur) { found = true; break; }
            }
            if (!found) return i;
        }
        return npos;
    }

    [[nodiscard]] size_t find_last_not_of(char32_t cp, size_t pos = npos) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return npos;
        if (pos >= cp_count_) pos = cp_count_ - 1;
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
        size_t i = pos + 1;
        while (i > 0)
        {
            --i;
            char32_t cur = char32_t(cp_at_byte(cp_byte_offset(i)));
            bool found = false;
            for (size_t j = 0; j < str.cp_count_; ++j)
            {
                if (char32_t(str.cp_at_byte(str.cp_byte_offset(j))) == cur) { found = true; break; }
            }
            if (!found) return i;
        }
        return npos;
    }

    // find_*_of 的 const char* / string_view 重载 (委托 utf8pp 版本)
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
