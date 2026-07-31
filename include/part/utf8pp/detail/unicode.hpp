// unicode.hpp - Unicode 专有操作 (大小写转换/规范化)

    // === 大小写转换 (完整 Unicode via unicode_data) ===
    utf8pp& to_lower()
    {
        ensure_cp_info();
        for (size_t i = 0; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_byte_offset(i));
            char32_t lc = to_lower_cp(char32_t(cp));
            if (lc != char32_t(cp)) replace_cp_at(i, static_cast<uint32_t>(lc));
        }
        return *this;
    }

    utf8pp& to_upper()
    {
        ensure_cp_info();
        for (size_t i = 0; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_byte_offset(i));
            char32_t uc = to_upper_cp(char32_t(cp));
            if (uc != char32_t(cp)) replace_cp_at(i, static_cast<uint32_t>(uc));
        }
        return *this;
    }

    // to_title: 每词首字符大写, 其余小写 (词以空白分隔)
    utf8pp& to_title()
    {
        ensure_cp_info();
        bool new_word = true;
        for (size_t i = 0; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_byte_offset(i));
            if (is_space(char32_t(cp)))
            {
                new_word = true;
                continue;
            }
            if (new_word)
            {
                char32_t tc = to_title_cp(char32_t(cp));
                if (tc != char32_t(cp)) replace_cp_at(i, static_cast<uint32_t>(tc));
            }
            else
            {
                char32_t lc = to_lower_cp(char32_t(cp));
                if (lc != char32_t(cp)) replace_cp_at(i, static_cast<uint32_t>(lc));
            }
            new_word = false;
        }
        return *this;
    }

    // swapcase: 大小写互换 (完整 Unicode)
    utf8pp& swapcase()
    {
        ensure_cp_info();
        for (size_t i = 0; i < cp_count_; ++i)
        {
            uint32_t cp = cp_at_byte(cp_byte_offset(i));
            if (is_upper(char32_t(cp)))
                replace_cp_at(i, static_cast<uint32_t>(to_lower_cp(char32_t(cp))));
            else if (is_lower(char32_t(cp)))
                replace_cp_at(i, static_cast<uint32_t>(to_upper_cp(char32_t(cp))));
        }
        return *this;
    }

    [[nodiscard]] utf8pp lowered() const  { utf8pp t(*this); t.to_lower();  return t; }
    [[nodiscard]] utf8pp uppered() const  { utf8pp t(*this); t.to_upper();  return t; }
    [[nodiscard]] utf8pp titled() const   { utf8pp t(*this); t.to_title();  return t; }
    [[nodiscard]] utf8pp swapcased() const { utf8pp t(*this); t.swapcase(); return t; }

    // === Unicode 规范化 (NFC/NFD/NFKC/NFKD) ===
    // 内部统一实现: decompose(canonical + compat + hangul) → CCC 排序 → (compose)
    // compat=true 时执行 NFKD/NFKC; compat=false 时执行 NFD/NFC
private:
    // 分解单个码点到 out; 返回新增码点数
    static void decompose_cp(uint32_t cp, dense<uint32_t>& out, bool compat) noexcept
    {
        // Hangul 算法分解 (优先, 不查表)
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
        // canonical 分解 (预组合 → base + combining, 递归一层)
        uint32_t base = 0, combining = 0;
        if (unicode_data::nfc_decompose_lookup(cp, base, combining))
        {
            decompose_cp(base, out, compat);
            out.push_back(combining);
            return;
        }
        out.push_back(cp);
    }

    // canonical ordering: 对每段连续 CCC>0 码点按 CCC 升序稳定排序
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

    // compose: 合并 starter + 组合序列 (含 Hangul 算法 + canonical 表 + blocking)
    // 原地 canonical composition (读写双指针, 零额外分配)
    // 合并只减不减码点数: write_idx <= read_idx 恒成立, 读取未处理数据不被覆盖
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
                // Hangul: LV + T → LVT (starter 是 LV 音节, cur 是 T)
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
            // 组合标记: 尝试 Hangul (L + V) 或 canonical 表合并
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
        // 截断多余元素 (uint32_t trivially destructible, pop_back 仅减 size)
        while (out.size() > write_idx) out.pop_back();
    }

    // 通用规范化内核
    utf8pp& normalize_impl(bool compose, bool compat)
    {
        ensure_cp_info();
        if (cp_count_ == 0) return *this;
        // 步骤1: 分解 (canonical + compat + hangul)
        dense<uint32_t> decomp;
        decomp.reserve_exact(cp_count_ * 2);
        for (size_t i = 0; i < cp_count_; ++i)
        {
            decompose_cp(cp_at_byte(cp_byte_offset(i)), decomp, compat);
        }
        // 步骤2: canonical ordering
        canonical_order(decomp);
        // 步骤3: compose (NFC/NFKC)
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
