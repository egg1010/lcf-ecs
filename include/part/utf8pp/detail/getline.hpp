// 行读取核心 (getline 服务)
// 读取量永不超过 in_avail(), 保证退回字节仍在 get area 内

private:
    // 退回 base[start, start+count) 字节: pubseekoff 优先, 失败降级逆序 sputbackc
    static void streambuf_put_back(std::streambuf* sb, const char* base, size_t start,
                                    size_t count, std::istream& is)
    {
        if (count == 0) return;
        if (sb->pubseekoff(-(std::streamoff)count, std::ios_base::cur, std::ios_base::in)
            == std::streambuf::pos_type(std::streamoff(-1)))
        {
            for (size_t k = count; k > 0; --k)
            {
                if (sb->sputbackc(base[start + k - 1]) == std::char_traits<char>::eof())
                {
                    // 退回失败: 标记坏流, 不静默丢字节
                    is.setstate(std::ios::badbit);
                    return;
                }
            }
        }
    }

    // 读一行到自身 (前置条件: 外层已 clear): 命中分隔符或 EOF 停止, 分隔符不进串
    // 流状态与 std::getline 对齐: 命中分隔符不动; 读尽无分隔符设 eofbit; 一字节未读到设 failbit
    void getline_read_core(std::istream& is, char delim)
    {
        std::streambuf* sb = is.rdbuf();
        bool got_any = false;
        bool saw_delim = false;

        // 阶段1: SSO 试探
        {
            std::streamsize avail = sb->in_avail();
            if (avail > 0)
            {
                size_t want = (size_t)avail < (size_t)SSO_CAPACITY
                    ? (size_t)avail : (size_t)SSO_CAPACITY;
                std::streamsize got = sb->sgetn(data_, (std::streamsize)want);
                if (got > 0)
                {
                    char* hit = static_cast<char*>(std::memchr(data_, delim, (size_t)got));
                    if (hit)
                    {
                        size_t take = static_cast<size_t>(hit - data_);
                        byte_size_ = static_cast<uint32_t>(take);
                        data_[take] = '\0';
                        streambuf_put_back(sb, data_, take + 1, (size_t)got - take - 1, is);
                        got_any = true;
                        saw_delim = true;
                    }
                    else
                    {
                        // 行长超过 SSO: 退回已读, 转大块模式
                        streambuf_put_back(sb, data_, 0, (size_t)got, is);
                    }
                }
            }
        }

        // 阶段2: 大块模式
        while (!saw_delim)        {
            std::streamsize avail = sb->in_avail();
            if (avail <= 0)
            {
                // sgetc 触发底层填充: EOF 则结束, 字节落入 get area 后走批量
                if (sb->sgetc() == std::char_traits<char>::eof()) break;
                if (sb->in_avail() <= 0)
                {
                    // 批量不可用的病态流: 单字节回退路径
                    int ch = sb->sbumpc();
                    if (ch == std::char_traits<char>::eof()) break;
                    ensure_byte_capacity(byte_size_ + 1);
                    char c = static_cast<char>(ch);
                    if (c == delim)
                    {
                        data_[byte_size_] = '\0';
                        got_any = true;
                        saw_delim = true;
                        break;
                    }
                    data_[byte_size_] = c;
                    ++byte_size_;
                    data_[byte_size_] = '\0';
                    got_any = true;
                }
                continue;
            }
            size_t want = (size_t)avail < ((size_t)1 << 20) ? (size_t)avail : ((size_t)1 << 20);
            size_t old = byte_size_;
            // 前瞻预留: 首块照需分配, 后续至少 64K, 封顶现有容量两倍
            size_t reserve_extra = want;
            if (old > 0)
            {
                size_t target = old + want;
                if (target < 65536) target = 65536;
                size_t cap2 = (static_cast<size_t>(byte_capacity_) + 1) * 2;
                if (target > cap2) target = cap2;
                reserve_extra = target - old;
            }
            ensure_byte_capacity(old + reserve_extra);
            std::streamsize got = sb->sgetn(data_ + old, (std::streamsize)want);
            if (got <= 0) break;
            char* base = data_ + old;
            size_t g = static_cast<size_t>(got);
            char* hit = static_cast<char*>(std::memchr(base, delim, g));
            size_t take = hit ? static_cast<size_t>(hit - base) : g;
            byte_size_ = static_cast<uint32_t>(old + take);
            data_[byte_size_] = '\0';
            if (hit)
            {
                streambuf_put_back(sb, base, take + 1, g - take - 1, is);
                got_any = true;
                saw_delim = true;
                break;
            }
            got_any = true;
        }

        if (!got_any)
        {
            is.setstate(std::ios::failbit);
        }
        else if (!saw_delim)
        {
            is.setstate(std::ios::eofbit);
        }
    }

    // 自由函数 getline (nonmember.hpp 定义) 的直读通道
    friend std::istream& getline(std::istream& is, utf8pp& s, char delim);

