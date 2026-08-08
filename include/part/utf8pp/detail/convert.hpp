// 转换

    [[nodiscard]] std::string to_std_string() const { return std::string(data_ ? data_ : "", byte_size_); }
    [[nodiscard]] std::u32string to_u32string() const
    {
        ensure_cp_info();
        std::u32string result;
        if (cp_count_ == 0) return result;
        result.reserve(cp_count_);
        // 无校验快速解码: utf8pp 数据保证合法
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* end = p + byte_size_;
        while (p < end)
        {
            result.push_back(detail_utf8::utf8_decode_unchecked(p));
            p += detail_utf8::k_utf8_seq_len[*p];
        }
        return result;
    }
    [[nodiscard]] std::u8string to_u8string() const
    {
        return data_ ? std::u8string(reinterpret_cast<const char8_t*>(data_), byte_size_)
                     : std::u8string();
    }
    // 零拷贝视图: 指向内部缓冲区, 生命周期受 *this 限制
    [[nodiscard]] utf8_view to_utf8_view() const noexcept
    {
        return utf8_view(data_ ? data_ : "", byte_size_);
    }

    // === 字符串转数字 (不抛异常, 失败返回 0; pos 输出消费字符数) ===
    // 与 std::stoi/stol/stof 等价但无异常, base 仅整数有效 (2/8/10/16)
    [[nodiscard]] int to_int(size_t* pos = nullptr, int base = 10) const
    {
        return static_cast<int>(to_ll_internal(pos, base));
    }

    [[nodiscard]] long to_long(size_t* pos = nullptr, int base = 10) const
    {
        return static_cast<long>(to_ll_internal(pos, base));
    }

    [[nodiscard]] long long to_ll(size_t* pos = nullptr, int base = 10) const
    {
        return to_ll_internal(pos, base);
    }

    [[nodiscard]] unsigned long to_ulong(size_t* pos = nullptr, int base = 10) const
    {
        return static_cast<unsigned long>(to_ull_internal(pos, base));
    }

    [[nodiscard]] unsigned long long to_ull(size_t* pos = nullptr, int base = 10) const
    {
        return to_ull_internal(pos, base);
    }

    [[nodiscard]] float to_float(size_t* pos = nullptr) const
    {
        return static_cast<float>(to_double_internal(pos));
    }

    [[nodiscard]] double to_double(size_t* pos = nullptr) const
    {
        return to_double_internal(pos);
    }

    [[nodiscard]] long double to_long_double(size_t* pos = nullptr) const
    {
        if (!data_ || byte_size_ == 0) { if (pos) *pos = 0; return 0.0L; }
        char* endp = nullptr;
        errno = 0;
        long double v = std::strtold(data_, &endp);
        if (pos) *pos = byte_idx_to_cp_idx_ceil(static_cast<size_t>(endp - data_));
        return v;
    }

    // 风格别名 std (便于 std::string 迁移)
    [[nodiscard]] int         stoi(size_t* pos = nullptr, int base = 10) const { return to_int(pos, base); }
    [[nodiscard]] long        stol(size_t* pos = nullptr, int base = 10) const { return to_long(pos, base); }
    [[nodiscard]] long long   stoll(size_t* pos = nullptr, int base = 10) const { return to_ll(pos, base); }
    [[nodiscard]] unsigned long      stoul(size_t* pos = nullptr, int base = 10) const { return to_ulong(pos, base); }
    [[nodiscard]] unsigned long long stoull(size_t* pos = nullptr, int base = 10) const { return to_ull(pos, base); }
    [[nodiscard]] float       stof(size_t* pos = nullptr) const  { return to_float(pos); }
    [[nodiscard]] double      stod(size_t* pos = nullptr) const  { return to_double(pos); }
    [[nodiscard]] long double stold(size_t* pos = nullptr) const { return to_long_double(pos); }

    // === 解析 (返回 bool 表示是否完全转换, 输出值到 out) ===
    // 整数允许前导 +/- 与首尾空白, base∈{2,8,10,16}, 全串须为有效数字
    [[nodiscard]] bool parse_int(int& out, int base = 10) const noexcept
    {
        long long v = 0;
        if (!parse_ll(v, base)) return false;
        if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
            v > static_cast<long long>(std::numeric_limits<int>::max())) return false;
        out = static_cast<int>(v);
        return true;
    }

    [[nodiscard]] bool parse_long(long& out, int base = 10) const noexcept
    {
        long long v = 0;
        if (!parse_ll(v, base)) return false;
        if (v < static_cast<long long>(std::numeric_limits<long>::min()) ||
            v > static_cast<long long>(std::numeric_limits<long>::max())) return false;
        out = static_cast<long>(v);
        return true;
    }

    [[nodiscard]] bool parse_ll(long long& out, int base = 10) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        // 跳过首尾空白
        size_t start = 0;
        size_t end = cp_count_;
        while (start < end && is_space_cp(cp_at_byte(cp_byte_offset(start)))) ++start;
        while (end > start && is_space_cp(cp_at_byte(cp_byte_offset(end - 1)))) --end;
        if (start >= end) return false;
        size_t bstart = cp_byte_offset(start);
        size_t bend = (end < cp_count_) ? cp_byte_offset(end) : byte_size_;
        // 构造临时 C 串: 源可能无 '\0', 复制到临时缓冲
        char buf[64];
        char* p = buf;
        size_t len = bend - bstart;
        if (len >= sizeof(buf))
        {
            p = static_cast<char*>(std::malloc(len + 1));
            if (!p) return false;
        }
        std::memcpy(p, data_ + bstart, len);
        p[len] = '\0';
        char* endp = nullptr;
        errno = 0;
        long long v = std::strtoll(p, &endp, base);
        bool ok = (endp == p + len) && errno != ERANGE;
        if (p != buf) std::free(p);
        if (!ok) return false;
        out = v;
        return true;
    }

    [[nodiscard]] bool parse_ulong(unsigned long& out, int base = 10) const noexcept
    {
        unsigned long long v = 0;
        if (!parse_ull(v, base)) return false;
        if (v > static_cast<unsigned long long>(std::numeric_limits<unsigned long>::max())) return false;
        out = static_cast<unsigned long>(v);
        return true;
    }

    [[nodiscard]] bool parse_ull(unsigned long long& out, int base = 10) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        size_t start = 0;
        size_t end = cp_count_;
        while (start < end && is_space_cp(cp_at_byte(cp_byte_offset(start)))) ++start;
        while (end > start && is_space_cp(cp_at_byte(cp_byte_offset(end - 1)))) --end;
        if (start >= end) return false;
        size_t bstart = cp_byte_offset(start);
        size_t bend = (end < cp_count_) ? cp_byte_offset(end) : byte_size_;
        char buf[64];
        char* p = buf;
        size_t len = bend - bstart;
        if (len >= sizeof(buf))
        {
            p = static_cast<char*>(std::malloc(len + 1));
            if (!p) return false;
        }
        std::memcpy(p, data_ + bstart, len);
        p[len] = '\0';
        char* endp = nullptr;
        errno = 0;
        unsigned long long v = std::strtoull(p, &endp, base);
        bool ok = (endp == p + len) && errno != ERANGE;
        if (p != buf) std::free(p);
        if (!ok) return false;
        out = v;
        return true;
    }

    [[nodiscard]] bool parse_float(float& out) const noexcept
    {
        double v = 0.0;
        if (!parse_double(v)) return false;
        out = static_cast<float>(v);
        return true;
    }

    [[nodiscard]] bool parse_double(double& out) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        size_t start = 0;
        size_t end = cp_count_;
        while (start < end && is_space_cp(cp_at_byte(cp_byte_offset(start)))) ++start;
        while (end > start && is_space_cp(cp_at_byte(cp_byte_offset(end - 1)))) --end;
        if (start >= end) return false;
        size_t bstart = cp_byte_offset(start);
        size_t bend = (end < cp_count_) ? cp_byte_offset(end) : byte_size_;
        char buf[64];
        char* p = buf;
        size_t len = bend - bstart;
        if (len >= sizeof(buf))
        {
            p = static_cast<char*>(std::malloc(len + 1));
            if (!p) return false;
        }
        std::memcpy(p, data_ + bstart, len);
        p[len] = '\0';
        char* endp = nullptr;
        errno = 0;
        double v = std::strtod(p, &endp);
        bool ok = (endp == p + len) && errno != ERANGE;
        if (p != buf) std::free(p);
        if (!ok) return false;
        out = v;
        return true;
    }

    [[nodiscard]] bool parse_long_double(long double& out) const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        size_t start = 0;
        size_t end = cp_count_;
        while (start < end && is_space_cp(cp_at_byte(cp_byte_offset(start)))) ++start;
        while (end > start && is_space_cp(cp_at_byte(cp_byte_offset(end - 1)))) --end;
        if (start >= end) return false;
        size_t bstart = cp_byte_offset(start);
        size_t bend = (end < cp_count_) ? cp_byte_offset(end) : byte_size_;
        char buf[64];
        char* p = buf;
        size_t len = bend - bstart;
        if (len >= sizeof(buf))
        {
            p = static_cast<char*>(std::malloc(len + 1));
            if (!p) return false;
        }
        std::memcpy(p, data_ + bstart, len);
        p[len] = '\0';
        char* endp = nullptr;
        errno = 0;
        long double v = std::strtold(p, &endp);
        bool ok = (endp == p + len) && errno != ERANGE;
        if (p != buf) std::free(p);
        if (!ok) return false;
        out = v;
        return true;
    }

    // === 内容判断 ===
    // 整数 (允许前导 +/-, 首尾空白, base 默认 10)
    [[nodiscard]] bool is_integer(int base = 10) const noexcept
    {
        long long v = 0;
        return parse_ll(v, base);
    }

    // 浮点数 (允许 +/-/小数点/指数, 首尾空白)
    [[nodiscard]] bool is_float() const noexcept
    {
        ensure_cp_info();
        if (cp_count_ == 0) return false;
        double v = 0.0;
        return parse_double(v);
    }

    // 整数或浮点数
    [[nodiscard]] bool is_number() const noexcept
    {
        if (is_integer()) return true;
        return is_float();
    }

    // === 进制判断 (便捷别名) ===
    [[nodiscard]] bool is_hex() const noexcept    { return is_integer(16); }
    [[nodiscard]] bool is_binary() const noexcept { return is_integer(2); }
    [[nodiscard]] bool is_octal() const noexcept  { return is_integer(8); }

    // === 单码点字符分类 (公开静态, 完整 Unicode 覆盖 via unicode_data) ===
    [[nodiscard]] static bool is_alpha(char32_t cp) noexcept
    {
        return unicode_data::is_alpha_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_digit(char32_t cp) noexcept
    {
        return unicode_data::is_digit_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_alnum(char32_t cp) noexcept
    {
        return unicode_data::is_alnum_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_space(char32_t cp) noexcept
    {
        return unicode_data::is_unicode_space(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_punct(char32_t cp) noexcept
    {
        // 标点 ASCII: ! " # $ % & ' ( ) * + , - . / : ; < = > ? @ [ \ ] ^ _ ` { | } ~
        if ((cp >= U'!' && cp <= U'/') || (cp >= U':' && cp <= U'@') ||
            (cp >= U'[' && cp <= U'`') || (cp >= U'{' && cp <= U'~')) return true;
        // 标点 Latin-1 (¡ ¢ £ ¤ ¥ ¦ § ¨ © ª « ¬ ­ ® ¯ ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½ ¾ ¿)
        if (cp >= U'\u00A1' && cp <= U'\u00BF') return true;
        // 通用标点 General Punctuation / CJK Symbols / 全角标点
        if (cp >= U'\u2000' && cp <= U'\u206F') return true;   // General Punctuation
        if (cp >= U'\u3000' && cp <= U'\u303F') return true;   // CJK Symbols and Punctuation
        if (cp >= U'\uFF01' && cp <= U'\uFF0F') return true;   // 全角 ASCII 标点
        if (cp >= U'\uFF1A' && cp <= U'\uFF20') return true;
        if (cp >= U'\uFF3B' && cp <= U'\uFF40') return true;
        if (cp >= U'\uFF5B' && cp <= U'\uFF65') return true;
        return false;
    }
    [[nodiscard]] static bool is_lower(char32_t cp) noexcept
    {
        return unicode_data::is_lower_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_upper(char32_t cp) noexcept
    {
        return unicode_data::is_upper_cp(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_xdigit(char32_t cp) noexcept
    {
        return (cp >= U'0' && cp <= U'9') ||
               (cp >= U'A' && cp <= U'F') ||
               (cp >= U'a' && cp <= U'f');
    }
    [[nodiscard]] static bool is_cntrl(char32_t cp) noexcept
    {
        return cp < U' ' || cp == U'\x7F' ||
               (cp >= U'\u0080' && cp <= U'\u009F');
    }
    [[nodiscard]] static bool is_printable(char32_t cp) noexcept
    {
        if (is_cntrl(cp)) return false;
        if (cp == U'\uFFFD') return false;
        return cp >= U' ';
    }
    [[nodiscard]] static bool is_combining(char32_t cp) noexcept
    {
        return unicode_data::is_combining_mark(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_wide(char32_t cp) noexcept
    {
        return unicode_data::is_wide(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_zero_width(char32_t cp) noexcept
    {
        return unicode_data::is_zero_width(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_emoji(char32_t cp) noexcept
    {
        return unicode_data::is_extended_pictographic(static_cast<uint32_t>(cp));
    }
    // 单码点显示宽度 (0/1/2): 零宽=0, 全角/宽字符=2, 其他=1
    [[nodiscard]] static int cp_width(char32_t cp) noexcept
    {
        return unicode_data::cp_display_width(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static char32_t to_lower_cp(char32_t cp) noexcept
    {
        return static_cast<char32_t>(unicode_data::to_lower_cp(static_cast<uint32_t>(cp)));
    }
    [[nodiscard]] static char32_t to_upper_cp(char32_t cp) noexcept
    {
        return static_cast<char32_t>(unicode_data::to_upper_cp(static_cast<uint32_t>(cp)));
    }
    [[nodiscard]] static char32_t to_title_cp(char32_t cp) noexcept
    {
        return static_cast<char32_t>(unicode_data::to_title_cp(static_cast<uint32_t>(cp)));
    }

    // === Unicode 脚本判断 (UAX #24) ===
    // 复用 unicode_data::script 枚举与查找表
    // 类型别名 script 定义于 construct.hpp, 此处直接使用

    [[nodiscard]] static script script_of(char32_t cp) noexcept
    {
        return unicode_data::script_of(static_cast<uint32_t>(cp));
    }
    [[nodiscard]] static bool is_script(char32_t cp, script s) noexcept
    {
        return unicode_data::is_script(static_cast<uint32_t>(cp), s);
    }
    // 脚本名称 (用于输出/调试)
    [[nodiscard]] static const char* script_name(script s) noexcept
    {
        switch (s)
        {
            case script::unknown:     return "Unknown";
            case script::common:      return "Common";
            case script::inherited:   return "Inherited";
            case script::latin:       return "Latin";
            case script::greek:       return "Greek";
            case script::cyrillic:    return "Cyrillic";
            case script::armenian:    return "Armenian";
            case script::hebrew:      return "Hebrew";
            case script::arabic:      return "Arabic";
            case script::syriac:      return "Syriac";
            case script::thaana:      return "Thaana";
            case script::devanagari:  return "Devanagari";
            case script::bengali:     return "Bengali";
            case script::gurmukhi:    return "Gurmukhi";
            case script::gujarati:    return "Gujarati";
            case script::oriya:       return "Oriya";
            case script::tamil:       return "Tamil";
            case script::telugu:      return "Telugu";
            case script::kannada:     return "Kannada";
            case script::malayalam:   return "Malayalam";
            case script::sinhala:     return "Sinhala";
            case script::thai:        return "Thai";
            case script::lao:         return "Lao";
            case script::tibetan:     return "Tibetan";
            case script::myanmar:     return "Myanmar";
            case script::georgian:    return "Georgian";
            case script::hangul:      return "Hangul";
            case script::hiragana:    return "Hiragana";
            case script::katakana:    return "Katakana";
            case script::han:         return "Han";
            case script::ethiopic:    return "Ethiopic";
            case script::cherokee:    return "Cherokee";
            case script::canadian:    return "Canadian_Aboriginal";
            case script::ogham:       return "Ogham";
            case script::runic:       return "Runic";
            case script::tagalog:     return "Tagalog";
            case script::mongolian:   return "Mongolian";
            case script::cjk_ext:     return "CJK_Ext";
            case script::emoji_picto: return "Emoji";
        }
        return "Unknown";
    }
