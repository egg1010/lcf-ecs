// 专有 Unicode 操作

    // === 大小写转换公共实现 ===
    // 工厂返回可调用对象 uint32_t(uint32_t), 支持有状态映射 (如 to_title 的 new_word)
    // 预扫描与构建各创建一次工厂实例, 重置内部状态
    template<typename MapFnFactory>
    utf8pp& case_transform_inplace(MapFnFactory factory)
    {
        ensure_cp_info();
        if (cp_count_ == 0) return *this;

        const uint8_t* src = reinterpret_cast<const uint8_t*>(data_);
        const uint8_t* src_end = src + byte_size_;
        // 预扫描: 计算新字节长度并检测变更 (字节指针, 不依赖 cp_offsets_)
        size_t new_byte_size = 0;
        bool any_change = false;
        {
            auto map_fn = factory();
            const uint8_t* p = src;
            while (p < src_end)
            {
                uint32_t cp = 0;
                size_t len = 0;
                (void)detail_utf8::utf8_decode_one(p, src_end, &cp, &len);
                uint32_t mapped = map_fn(cp);
                if (mapped != cp) any_change = true;
                new_byte_size += (mapped < 0x80) ? 1 : (mapped < 0x800) ? 2 : (mapped < 0x10000) ? 3 : 4;
                p += len;
            }
        }
        if (!any_change) return *this;
        // 构建新缓冲区与 cp_offsets_ (同步写入避免重建)
        char* new_data = static_cast<char*>(utf8pp_alloc(new_byte_size + 1));
        if (!new_data) std::abort();
        ensure_cp_capacity(cp_count_);
        size_t write_pos = 0;
        size_t i = 0;
        {
            auto map_fn = factory();  // 重新创建以重置状态
            const uint8_t* p = src;
            while (p < src_end)
            {
                uint32_t cp = 0;
                size_t len = 0;
                (void)detail_utf8::utf8_decode_one(p, src_end, &cp, &len);
                uint32_t mapped = map_fn(cp);
                uint8_t enc[4];
                size_t enc_len = 0;
                if (!detail_utf8::utf8_encode_one(mapped, enc, &enc_len))
                {
                    (void)detail_utf8::utf8_encode_one(0xFFFD, enc, &enc_len);
                }
                cp_offsets_[i] = static_cast<uint32_t>(write_pos);
                std::memcpy(new_data + write_pos, enc, enc_len);
                write_pos += enc_len;
                p += len;
                ++i;
            }
        }
        new_data[new_byte_size] = '\0';
        bool was_sso = is_sso();
        if (!was_sso) utf8pp_free(data_, static_cast<size_t>(byte_capacity_) + 1);
        data_ = new_data;
        byte_size_ = static_cast<uint32_t>(new_byte_size);
        byte_capacity_ = static_cast<uint32_t>(new_byte_size);
        cp_info_state_ = 2;  // 偏移已构建
        return *this;
    }

    // === 大小写转换 (完整 Unicode) ===
    utf8pp& to_lower()
    {
        ensure_cp_info();
        // 纯 ASCII 快速路径: 直接字节操作 (无分配无重编码, state 保持 1)
        if (cp_info_state_ == 1) [[likely]]
        {
            for (size_t i = 0; i < byte_size_; ++i)
            {
                if (data_[i] >= 'A' && data_[i] <= 'Z') data_[i] += 32;
            }
            return *this;
        }
        return case_transform_inplace([]() {
            return [](uint32_t cp) -> uint32_t {
                return static_cast<uint32_t>(to_lower_cp(char32_t(cp)));
            };
        });
    }

    utf8pp& to_upper()
    {
        ensure_cp_info();
        // 纯 ASCII 快速路径: 直接字节操作 (无分配无重编码, state 保持 1)
        if (cp_info_state_ == 1) [[likely]]
        {
            for (size_t i = 0; i < byte_size_; ++i)
            {
                if (data_[i] >= 'a' && data_[i] <= 'z') data_[i] -= 32;
            }
            return *this;
        }
        return case_transform_inplace([]() {
            return [](uint32_t cp) -> uint32_t {
                return static_cast<uint32_t>(to_upper_cp(char32_t(cp)));
            };
        });
    }

    // 每词首字符大写, 其余小写 (词以空白分隔)
    utf8pp& to_title()
    {
        return case_transform_inplace([]() {
            return [new_word = true](uint32_t cp) mutable -> uint32_t {
                if (is_space(char32_t(cp)))
                {
                    new_word = true;
                    return cp;
                }
                uint32_t result = new_word
                    ? static_cast<uint32_t>(to_title_cp(char32_t(cp)))
                    : static_cast<uint32_t>(to_lower_cp(char32_t(cp)));
                new_word = false;
                return result;
            };
        });
    }

    // 大小写互换 (完整 Unicode)
    utf8pp& swapcase()
    {
        return case_transform_inplace([]() {
            return [](uint32_t cp) -> uint32_t {
                if (is_upper(char32_t(cp))) return static_cast<uint32_t>(to_lower_cp(char32_t(cp)));
                if (is_lower(char32_t(cp))) return static_cast<uint32_t>(to_upper_cp(char32_t(cp)));
                return cp;
            };
        });
    }

    [[nodiscard]] utf8pp lowered() const  { utf8pp t(*this); t.to_lower();  return t; }
    [[nodiscard]] utf8pp uppered() const  { utf8pp t(*this); t.to_upper();  return t; }
    [[nodiscard]] utf8pp titled() const   { utf8pp t(*this); t.to_title();  return t; }
    [[nodiscard]] utf8pp swapcased() const { utf8pp t(*this); t.swapcase(); return t; }

    // === Unicode 规范化 (NFC/NFD/NFKC/NFKD) ===
    // 内部统一实现: 分解 (规范 + 兼容 + 韩文) → CCC 排序 → 组合
    // 参数 compat=true 执行 NFKD/NFKC; false 执行 NFD/NFC
private:
    // 分解单个码点到 out
    static void decompose_cp(uint32_t cp, dense<uint32_t>& out, bool compat) noexcept
    {
        // 韩文算法分解 (优先, 不查表)
        uint32_t hg[3] = {0, 0, 0};
        if (unicode_data::hangul_decompose(cp, hg) > 0)
        {
            for (uint32_t i = 0; i < 3 && hg[i] != 0; ++i) out.push_back(hg[i]);
            return;
        }
        // 兼容性分解 (NFKD 表 + 全角算法)
        if (compat)
        {
            uint32_t fw = 0;
            if (unicode_data::nfkd_fullwidth_decompose(cp, fw))
            {
                out.push_back(fw);
                return;
            }
            uint32_t dt[4] = {0, 0, 0, 0};
            uint8_t dl = 0;
            if (unicode_data::nfkd_lookup(cp, dt, dl))
            {
                for (uint8_t i = 0; i < dl; ++i) out.push_back(dt[i]);
                return;
            }
        }
        // 规范分解 (预组合 → base + combining, 递归一层)
        uint32_t base = 0, combining = 0;
        if (unicode_data::nfc_decompose_lookup(cp, base, combining))
        {
            decompose_cp(base, out, compat);
            out.push_back(combining);
            return;
        }
        out.push_back(cp);
    }

    // 规范排序: 对每段连续 CCC>0 码点按 CCC 升序稳定排序
    static void canonical_order(dense<uint32_t>& v) noexcept
    {
        const size_t n = v.size();
        size_t i = 0;
        while (i < n)
        {
            if (unicode_data::canonical_combining_class(v[i]) == 0) { ++i; continue; }
            size_t j = i;
            while (j < n && unicode_data::canonical_combining_class(v[j]) > 0) ++j;
            for (size_t a = i + 1; a < j; ++a)
            {
                uint32_t key = v[a];
                uint8_t key_ccc = unicode_data::canonical_combining_class(key);
                size_t b = a;
                while (b > i && unicode_data::canonical_combining_class(v[b - 1]) > key_ccc)
                {
                    v[b] = v[b - 1];
                    --b;
                }
                v[b] = key;
            }
            i = j;
        }
    }

    // 组合: 合并 starter 与组合序列 (含韩文算法 + 规范表 + blocking)
    // 原地规范组合 (读写双指针, 零额外分配)
    // 陷阱: 合并只减码点数, write_idx <= read_idx 恒成立, 未处理数据不被覆盖
    static void compose_seq(dense<uint32_t>& out) noexcept
    {
        size_t write_idx = 0;
        size_t starter_idx = SIZE_MAX;
        uint8_t last_unmerged_ccc = 0;
        for (size_t read_idx = 0; read_idx < out.size(); ++read_idx)
        {
            uint32_t cur = out[read_idx];
            uint8_t cur_ccc = unicode_data::canonical_combining_class(cur);
            if (cur_ccc == 0)
            {
                // 韩文: LV + T → LVT (starter 是 LV 音节, cur 是 T)
                if (starter_idx != SIZE_MAX)
                {
                    uint32_t hg = unicode_data::hangul_compose(out[starter_idx], cur);
                    if (hg != 0)
                    {
                        out[starter_idx] = hg;
                        continue; // 吸收 cur
                    }
                }
                out[write_idx] = cur;
                ++write_idx;
                starter_idx = write_idx - 1;
                last_unmerged_ccc = 0;
                continue;
            }
            // 组合标记: 尝试韩文 (L + V) 或规范表合并
            bool merged = false;
            if (starter_idx != SIZE_MAX && last_unmerged_ccc < cur_ccc)
            {
                uint32_t hg = unicode_data::hangul_compose(out[starter_idx], cur);
                if (hg != 0)
                {
                    out[starter_idx] = hg;
                    merged = true;
                }
                else
                {
                    uint32_t composed = unicode_data::nfc_compose_lookup(out[starter_idx], cur);
                    if (composed != 0)
                    {
                        out[starter_idx] = composed;
                        merged = true;
                    }
                }
            }
            if (!merged)
            {
                out[write_idx] = cur;
                ++write_idx;
                last_unmerged_ccc = cur_ccc;
            }
        }
        // 截断多余元素
        while (out.size() > write_idx) out.pop_back();
    }

    // 通用规范化内核
    utf8pp& normalize_impl(bool compose, bool compat)
    {
        ensure_cp_info();
        if (cp_count_ == 0) return *this;
        // 步骤1: 分解 (规范 + 兼容 + 韩文)
        dense<uint32_t> decomp;
        decomp.reserve_exact(cp_count_ * 2);
        for (size_t i = 0; i < cp_count_; ++i)
        {
            decompose_cp(cp_at_byte(cp_byte_offset(i)), decomp, compat);
        }
        // 步骤2: 规范排序
        canonical_order(decomp);
        // 步骤3: 组合 (NFC/NFKC)
        if (compose)
        {
            compose_seq(decomp);
        }
        // 步骤4: 与原串比较, 相同则跳过重建
        bool changed = (decomp.size() != cp_count_);
        if (!changed)
        {
            for (size_t i = 0; i < decomp.size(); ++i)
            {
                if (decomp[i] != cp_at_byte(cp_byte_offset(i))) { changed = true; break; }
            }
        }
        if (!changed) return *this;
        // 步骤5: 重建字符串
        clear();
        for (size_t k = 0; k < decomp.size(); ++k)
        {
            push_back(static_cast<char32_t>(decomp[k]));
        }
        return *this;
    }

public:
    utf8pp& to_nfc()  { return normalize_impl(true,  false); }
    utf8pp& to_nfd()  { return normalize_impl(false, false); }
    utf8pp& to_nfkc() { return normalize_impl(true,  true);  }
    utf8pp& to_nfkd() { return normalize_impl(false, true);  }

    [[nodiscard]] utf8pp nfc()  const { utf8pp t(*this); t.to_nfc();  return t; }
    [[nodiscard]] utf8pp nfd()  const { utf8pp t(*this); t.to_nfd();  return t; }
    [[nodiscard]] utf8pp nfkc() const { utf8pp t(*this); t.to_nfkc(); return t; }
    [[nodiscard]] utf8pp nfkd() const { utf8pp t(*this); t.to_nfkd(); return t; }
