// 构造/析构/赋值/swap/assign

    // 内联缓冲容量: 惰性 cp_info 后无 sso_cp_offsets_, 省下空间用于扩大字节缓冲
    // 对象总大小 144 bytes, 头部成员 40 bytes, sso_buffer_ 占 104 bytes
    static constexpr size_t SSO_CAPACITY = 103;

    // 复用 unicode_data::script 枚举 (UAX #24); 提前声明供后续 detail 文件使用
    using script = unicode_data::script;

    // === 构造/析构 ===
    utf8pp() noexcept
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
    }

    // 裸构造: 不初始化任何字段, 供 substr/拷贝等内部热点使用
    // 跳过默认构造的字段写入和 0 初始化, 减少覆盖开销
    struct raw_construct_t {};
    static constexpr raw_construct_t raw_construct{};
    explicit utf8pp(raw_construct_t) noexcept {}

    utf8pp(const char* s) : utf8pp(s, s ? std::strlen(s) : 0) {}

    utf8pp(const char8_t* s)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        const char* p = reinterpret_cast<const char*>(s);
        if (p) init_from_utf8(p, std::strlen(p));
    }

    utf8pp(const char8_t* s, size_t byte_len)
        : utf8pp(reinterpret_cast<const char*>(s), byte_len) {}

    utf8pp(const char* s, size_t byte_len)
        : cp_info_state_(0), uniform_byte_len_(0), data_(nullptr),
          byte_size_(0), byte_capacity_(0), cp_offsets_(nullptr),
          cp_count_(0), cp_offsets_capacity_(0), cp_cache_(nullptr)
    {
        // 热路径优化: byte_len > SSO 时跳过 SSO 初始化, 让 init_from_utf8 直接走堆分配
        if (byte_len == 0)
        {
            data_ = sso_buffer_;
            byte_capacity_ = SSO_CAPACITY;
            data_[0] = '\0';
            cp_info_state_ = 1;
            uniform_byte_len_ = 1;
            return;
        }
        if (byte_len <= SSO_CAPACITY)
        {
            data_ = sso_buffer_;
            byte_capacity_ = SSO_CAPACITY;
        }
        // 否则 data_=nullptr/byte_capacity_=0, init_from_utf8 内部走堆分配路径
        init_from_utf8(s, byte_len);
    }

    utf8pp(const char32_t* s, size_t cp_count)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        if (cp_count == 0) return;
        init_from_char32(s, cp_count);
    }

    utf8pp(size_t n, char32_t cp)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        if (n == 0) return;
        append_cp(n, cp);
    }

    utf8pp(std::string_view sv) : utf8pp(sv.data(), sv.size()) {}

    // 适配构造: 与 std::string 互操作
    utf8pp(const std::string& s) : utf8pp(s.data(), s.size()) {}
    utf8pp(const std::u8string& s) : utf8pp(reinterpret_cast<const char*>(s.data()), s.size()) {}
    utf8pp(const std::u32string& s) : utf8pp(s.data(), s.size()) {}

    // 视图构造: 零拷贝视图转拥有内存拷贝
    utf8pp(const utf8_view& v) : utf8pp(v.data(), v.byte_size()) {}

    // 初始化列表构造 (与 std::string 的 initializer_list<char> 对齐)
    utf8pp(std::initializer_list<char32_t> il)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        for (char32_t cp : il) push_back(cp);
    }

    // 迭代器范围构造 (与 std::string(InputIt, InputIt) 对齐)
    // 约束: 迭代器解引用结果可转换为 char32_t
    template <typename InputIt, typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    utf8pp(InputIt first, InputIt last)
    {
        data_ = sso_buffer_;
        byte_capacity_ = SSO_CAPACITY;
        data_[0] = '\0';
        for (InputIt it = first; it != last; ++it) push_back(static_cast<char32_t>(*it));
    }

    // 禁止 nullptr 隐式构造
    utf8pp(std::nullptr_t) = delete;

    // 范围构造: 从容器 (依赖 join, 类内成员函数延迟解析, 顺序无关)
    template <size_t N>
    explicit utf8pp(const std::array<utf8pp, N>& parts) : utf8pp(join(parts, utf8pp())) {}

    explicit utf8pp(const std::vector<utf8pp>& parts) : utf8pp(join(parts, utf8pp())) {}

    utf8pp(const utf8pp& other) : byte_size_(other.byte_size_)
    {
        // 仅需计数, 不强制构建偏移 (避免 source state=3→2 的昂贵升级)
        // 内联 state 检查: 大部分场景 other 已 state!=0 (构造后), 跳过函数调用
        if (other.cp_info_state_ == 0) [[unlikely]] other.ensure_cp_count();
        cp_info_state_ = other.cp_info_state_;
        cp_count_ = other.cp_count_;
        uniform_byte_len_ = other.uniform_byte_len_;
        if (other.is_sso())
        {
            data_ = sso_buffer_;
            byte_capacity_ = SSO_CAPACITY;
        }
        else
        {
            data_ = static_cast<char*>(utf8pp_alloc(other.byte_capacity_ + 1));
            if (!data_) std::abort();
            byte_capacity_ = other.byte_capacity_;
        }
        if (byte_size_ > 0)
        {
            std::memcpy(data_, other.data_, byte_size_);
        }
        data_[byte_size_] = '\0';

        // 仅当源已构建偏移时复制 (ASCII/state=3 无需复制, 热路径不进入)
        if (cp_info_state_ == 2 && other.cp_offsets_ && other.cp_count_ > 0) [[unlikely]]
        {
            cp_offsets_capacity_ = other.cp_offsets_capacity_;
            cp_offsets_ = static_cast<uint32_t*>(utf8pp_alloc(cp_offsets_capacity_ * sizeof(uint32_t)));
            if (!cp_offsets_) std::abort();
            std::memcpy(cp_offsets_, other.cp_offsets_, cp_count_ * sizeof(uint32_t));
        }
    }

    utf8pp(utf8pp&& other) noexcept
        : cp_info_state_(other.cp_info_state_), uniform_byte_len_(other.uniform_byte_len_),
          byte_size_(other.byte_size_), cp_count_(other.cp_count_)
    {
        if (other.is_sso())
        {
            data_ = sso_buffer_;
            byte_capacity_ = SSO_CAPACITY;
            std::memcpy(sso_buffer_, other.sso_buffer_, SSO_CAPACITY + 1);
            // 内联模式 cp_offsets_/cp_cache_ 必为空 (惰性不内嵌)
            cp_offsets_ = nullptr;
            cp_offsets_capacity_ = 0;
            cp_cache_ = nullptr;
        }
        else
        {
            data_ = other.data_;
            byte_capacity_ = other.byte_capacity_;
            cp_offsets_ = other.cp_offsets_;
            cp_offsets_capacity_ = other.cp_offsets_capacity_;
            cp_cache_ = other.cp_cache_;
        }
        other.data_ = other.sso_buffer_;
        other.byte_size_ = 0;
        other.byte_capacity_ = SSO_CAPACITY;
        other.cp_offsets_ = nullptr;
        other.cp_count_ = 0;
        other.cp_offsets_capacity_ = 0;
        other.cp_info_state_ = 0;
        other.uniform_byte_len_ = 0;
        other.cp_cache_ = nullptr;
        other.sso_buffer_[0] = '\0';
    }

    ~utf8pp() { release_memory_only(); }

    // === 赋值 ===
    utf8pp& operator=(const utf8pp& other)
    {
        if (this != &other)
        {
            utf8pp tmp(other);
            swap(tmp);
        }
        return *this;
    }

    utf8pp& operator=(utf8pp&& other) noexcept
    {
        if (this != &other)
        {
            release_memory_only();
            if (other.is_sso())
            {
                data_ = sso_buffer_;
                byte_capacity_ = SSO_CAPACITY;
                byte_size_ = other.byte_size_;
                cp_count_ = other.cp_count_;
                cp_info_state_ = other.cp_info_state_;
                uniform_byte_len_ = other.uniform_byte_len_;
                std::memcpy(sso_buffer_, other.sso_buffer_, SSO_CAPACITY + 1);
                cp_offsets_ = nullptr;
                cp_offsets_capacity_ = 0;
                cp_cache_ = nullptr;
            }
            else
            {
                data_ = other.data_;
                byte_size_ = other.byte_size_;
                byte_capacity_ = other.byte_capacity_;
                cp_offsets_ = other.cp_offsets_;
                cp_count_ = other.cp_count_;
                cp_offsets_capacity_ = other.cp_offsets_capacity_;
                cp_info_state_ = other.cp_info_state_;
                uniform_byte_len_ = other.uniform_byte_len_;
                cp_cache_ = other.cp_cache_;
            }
            other.data_ = other.sso_buffer_;
            other.byte_size_ = 0;
            other.byte_capacity_ = SSO_CAPACITY;
            other.cp_offsets_ = nullptr;
            other.cp_count_ = 0;
            other.cp_offsets_capacity_ = 0;
            other.cp_info_state_ = 0;
            other.uniform_byte_len_ = 0;
            other.cp_cache_ = nullptr;
            other.sso_buffer_[0] = '\0';
        }
        return *this;
    }

    utf8pp& operator=(const char* s) { return assign(s, s ? std::strlen(s) : 0); }
    utf8pp& operator=(std::string_view sv) { return assign(sv.data(), sv.size()); }
    utf8pp& operator=(char32_t cp) { clear(); push_back(cp); return *this; }
    utf8pp& operator=(const char8_t* s)
    {
        const char* p = reinterpret_cast<const char*>(s);
        return assign(p, p ? std::strlen(p) : 0);
    }
    utf8pp& operator=(std::initializer_list<char32_t> il) { return assign(il); }
    utf8pp& operator=(const std::string& s) { return assign(s.data(), s.size()); }
    utf8pp& operator=(const std::u8string& s)
    {
        return assign(reinterpret_cast<const char*>(s.data()), s.size());
    }
    utf8pp& operator=(const utf8_view& v) { return assign(v.data(), v.byte_size()); }

    // === assign ===
    utf8pp& assign(const char* s, size_t byte_len)
    {
        clear();
        if (byte_len > 0) init_from_utf8(s, byte_len);
        return *this;
    }

    // 单参/范围 assign 重载 (与 std::string::assign 对齐)
    utf8pp& assign(const utf8pp& other) { return assign(other.data_, other.byte_size_); }
    utf8pp& assign(const char* s) { return assign(s, s ? std::strlen(s) : 0); }
    utf8pp& assign(std::string_view sv) { return assign(sv.data(), sv.size()); }
    utf8pp& assign(const std::string& s) { return assign(s.data(), s.size()); }
    utf8pp& assign(const utf8_view& v) { return assign(v.data(), v.byte_size()); }
    utf8pp& assign(const char8_t* s)
    {
        const char* p = reinterpret_cast<const char*>(s);
        return assign(p, p ? std::strlen(p) : 0);
    }
    utf8pp& assign(std::initializer_list<char32_t> il)
    {
        clear();
        for (char32_t cp : il) push_back(cp);
        return *this;
    }
    template <typename InputIt, typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    utf8pp& assign(InputIt first, InputIt last)
    {
        clear();
        for (InputIt it = first; it != last; ++it) push_back(static_cast<char32_t>(*it));
        return *this;
    }

    // 填充 assign: n 个 cp (与 std::string::assign(size_type, char) 对齐)
    utf8pp& assign(size_t n, char32_t cp)
    {
        clear();
        append_cp(n, cp);
        return *this;
    }
    // 适配 assign: u8string/u32string
    utf8pp& assign(const std::u8string& s)
    {
        return assign(reinterpret_cast<const char*>(s.data()), s.size());
    }
    utf8pp& assign(const std::u32string& s)
    {
        clear();
        for (char32_t c : s) push_back(c);
        return *this;
    }
    // 子串 assign: 来自 other 的 [pos, pos+n) (与 std::string::assign(const string&, pos, n) 对齐)
    utf8pp& assign(const utf8pp& other, size_t pos, size_t n = npos)
    {
        return assign(other.substr(pos, n));
    }

    // 范围 assign: 从容器
    template <size_t N>
    utf8pp& assign(const std::array<utf8pp, N>& parts)
    {
        clear();
        for (size_t i = 0; i < N; ++i) append(parts[i]);
        return *this;
    }

    utf8pp& assign(const std::vector<utf8pp>& parts)
    {
        clear();
        for (size_t i = 0; i < parts.size(); ++i) append(parts[i]);
        return *this;
    }

    // === swap ===
    void swap(utf8pp& other) noexcept
    {
        if (is_sso() && other.is_sso())
        {
            // 内联 ↔ 内联: 交换嵌入数据 + 状态
            char tmp_bytes[SSO_CAPACITY + 1];
            std::memcpy(tmp_bytes, sso_buffer_, SSO_CAPACITY + 1);
            std::memcpy(sso_buffer_, other.sso_buffer_, SSO_CAPACITY + 1);
            std::memcpy(other.sso_buffer_, tmp_bytes, SSO_CAPACITY + 1);
            std::swap(byte_size_, other.byte_size_);
            std::swap(cp_count_, other.cp_count_);
            std::swap(cp_info_state_, other.cp_info_state_);
            std::swap(uniform_byte_len_, other.uniform_byte_len_);
        }
        else if (!is_sso() && !other.is_sso())
        {
            // 堆 ↔ 堆: 直接交换指针
            std::swap(data_, other.data_);
            std::swap(byte_size_, other.byte_size_);
            std::swap(byte_capacity_, other.byte_capacity_);
            std::swap(cp_offsets_, other.cp_offsets_);
            std::swap(cp_count_, other.cp_count_);
            std::swap(cp_offsets_capacity_, other.cp_offsets_capacity_);
            std::swap(cp_info_state_, other.cp_info_state_);
            std::swap(uniform_byte_len_, other.uniform_byte_len_);
        }
        else
        {
            // 内联 ↔ 堆: 用临时对象中转
            utf8pp tmp(std::move(*this));
            *this = std::move(other);
            other = std::move(tmp);
        }
    }
