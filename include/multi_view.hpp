// multi_view.hpp —— manager 类内片段,由 component.hpp 在 manager 类内部 include
// 不要单独 include 此文件
    template <typename First, typename... Rest>
    class multi_view
    {
    private:
        static constexpr size_t N = 1 + sizeof...(Rest);
        static constexpr size_t total_component_size_ = sizeof(First) + (0 + ... + sizeof(Rest));
        static constexpr size_t prefetch_distance_ =
            total_component_size_ <= 32 ? 32 : (total_component_size_ <= 128 ? 24 : 16);

        std::array<single_class_set*, N> sets_;
        size_t primary_idx_{0};
        manager* mgr_{nullptr};
        mutable dense<std::array<uint32_t, N>> dense_mappings_soa_;
        mutable dense<std::array<uint32_t, N>> compact_mappings_;
        mutable dense<uint32_t> cached_entity_versions_;
        mutable uint64_t cached_versions_[N]{};
        mutable size_t cached_count_{0};
        mutable bool mappings_valid_{false};
        mutable bool all_valid_{false};
        mutable bool pools_aligned_{false};

        using AllTypes = std::tuple<First, Rest...>;

        void find_smallest() noexcept
        {
            size_t min_size = std::numeric_limits<size_t>::max();
            primary_idx_ = 0;
            for (size_t i = 0; i < N; ++i)
            {
                if (sets_[i] && sets_[i]->size() < min_size)
                {
                    min_size = sets_[i]->size();
                    primary_idx_ = i;
                }
            }
        }

        void rebuild_mappings() const noexcept
        {
            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            const size_t n = indices.size();

            if (n == 0)
            {
                dense_mappings_soa_.clear();
                compact_mappings_.clear();
                cached_entity_versions_.clear();
                all_valid_ = true;
                pools_aligned_ = true;
                for (size_t k = 0; k < N; ++k)
                    cached_versions_[k] = sets_[k]->get_pool_version();
                mappings_valid_ = true;
                return;
            }

            bool fast_aligned = true;
            const uint32_t* pi = indices.data();
            const size_t byte_count = n * sizeof(uint32_t);
            for (size_t k = 0; k < N; ++k)
            {
                if (k == primary_idx_) continue;
                if (sets_[k]->size() < n)
                {
                    fast_aligned = false;
                    break;
                }
                auto& other_indices = sets_[k]->get_entity_indices();
                const uint32_t* oi = other_indices.data();
                if (__builtin_memcmp(pi, oi, byte_count) != 0)
                {
                    fast_aligned = false;
                    break;
                }
            }

            if (fast_aligned)
            {
                dense_mappings_soa_.clear();
                compact_mappings_.clear();
                cached_entity_versions_.clear();
                all_valid_ = true;
                pools_aligned_ = true;
                cached_count_ = n;
                for (size_t k = 0; k < N; ++k)
                    cached_versions_[k] = sets_[k]->get_pool_version();
                mappings_valid_ = true;
                return;
            }

            dense_mappings_soa_.clear();
            compact_mappings_.clear();
            cached_entity_versions_.clear();
            cached_entity_versions_.increase_capacity(n, uint32_t{0});

            std::array<size_t, N> set_sparse_size;
            for (size_t k = 0; k < N; ++k)
                set_sparse_size[k] = sets_[k]->get_sparse_size();

            auto* ver_data = cached_entity_versions_.data();
            all_valid_ = true;
            pools_aligned_ = false;

            auto get_sparse_cached = [&](single_class_set* s, uint32_t idx) -> uint64_t {
                uint32_t dense = s->sparse_dense_at_public(idx);
                uint32_t ver = s->sparse_version_at_public(idx);
                return (static_cast<uint64_t>(dense) << 32) | ver;
            };

            // memcmp 失败: 单遍历构建 SoA + 统计有效数
            dense_mappings_soa_.increase_capacity(n, std::array<uint32_t, N>{});
            auto* soa_data = dense_mappings_soa_.data();
            cached_count_ = 0;

            for (size_t i = 0; i < n; ++i)
            {
                if (i + 8 < n) [[likely]]
                    primary->prefetch_sparse_entry(indices[i + 8]);
                uint32_t idx = indices[i];
                uint64_t pc = get_sparse_cached(primary, idx);
                ver_data[i] = static_cast<uint32_t>(pc);
                uint32_t pver = static_cast<uint32_t>(pc);
                auto& m = soa_data[i];
                m[primary_idx_] = static_cast<uint32_t>(i);
                bool valid = true;
                for (size_t k = 0; k < N; ++k)
                {
                    if (k == primary_idx_) continue;
                    if (i + 8 < n) [[likely]]
                        sets_[k]->prefetch_sparse_entry(indices[i + 8]);
                    bool has = (idx < set_sparse_size[k]);
                    uint64_t kc = has ? get_sparse_cached(sets_[k], idx) : 0;
                    uint32_t dense = static_cast<uint32_t>(kc >> 32);
                    if (has && dense != 0xFFFFFFFFu && static_cast<uint32_t>(kc) == pver)
                        m[k] = dense;
                    else
                    {
                        m[k] = UINT32_MAX;
                        valid = false;
                        all_valid_ = false;
                    }
                }
                if (valid) ++cached_count_;
            }

            for (size_t k = 0; k < N; ++k)
                cached_versions_[k] = sets_[k]->get_pool_version();
            mappings_valid_ = true;

            // 非对齐且部分无效: 构建紧凑映射, for_each 只遍历有效实体
            if (!all_valid_ && cached_count_ > 0)
            {
                compact_mappings_.clear();
                compact_mappings_.increase_capacity(cached_count_, std::array<uint32_t, N>{});
                auto* src = dense_mappings_soa_.data();
                auto* dst = compact_mappings_.data();
                size_t w = 0;
                for (size_t i = 0; i < n; ++i)
                {
                    bool valid = true;
                    for (size_t k = 0; k < N; ++k)
                    {
                        if (k == primary_idx_) continue;
                        if (src[i][k] == UINT32_MAX) { valid = false; break; }
                    }
                    if (valid) dst[w++] = src[i];
                }
            }
            else
            {
                compact_mappings_.clear();
            }
        }

        void ensure_mappings() const noexcept
        {
            bool need_rebuild = !mappings_valid_;
            if (!need_rebuild)
            {
                for (size_t k = 0; k < N; ++k)
                {
                    if (cached_versions_[k] != sets_[k]->get_pool_version())
                    {
                        need_rebuild = true;
                        break;
                    }
                }
            }
            if (need_rebuild)
                rebuild_mappings();
        }

        [[nodiscard]] bool all_sets_valid() const noexcept
        {
            for (size_t i = 0; i < N; ++i)
            {
                if (!sets_[i]) return false;
            }
            return true;
        }

        template <size_t I>
        [[nodiscard]] std::tuple_element_t<I, AllTypes>* get_component_mapped(size_t primary_i) const noexcept
        {
            using T = std::tuple_element_t<I, AllTypes>;
            if (pools_aligned_)
            {
                return sets_[I]->template get_ptr_unchecked_by_index<T>(static_cast<uint32_t>(primary_i));
            }
            uint32_t dense_idx = dense_mappings_soa_[primary_i][I];
            if (dense_idx == UINT32_MAX) [[unlikely]] return nullptr;
            return sets_[I]->template get_ptr_unchecked_by_index<T>(dense_idx);
        }

        template <size_t I>
        [[nodiscard]] auto* get_component(entity e, size_t primary_i) const noexcept
        {
            using T = std::tuple_element_t<I, AllTypes>;
            return I == primary_idx_
                ? sets_[I]->template get_ptr_unchecked_by_index<T>(primary_i)
                : sets_[I]->template get_ptr_fast<T>(e);
        }

        template <typename T, typename Tuple>
        [[nodiscard]] static constexpr size_t find_type_index() noexcept
        {
            return find_type_index_impl<T, Tuple, 0>();
        }

        template <typename T, typename Tuple, size_t I>
        [[nodiscard]] static constexpr size_t find_type_index_impl() noexcept
        {
            if constexpr (std::is_same_v<T, std::tuple_element_t<I, Tuple>>) return I;
            else return find_type_index_impl<T, Tuple, I + 1>();
        }

        template <typename Func, size_t... Is>
        void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return;

            ensure_mappings();

            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            const size_t n = indices.size();
            if (n == 0) return;

            // 提取原始 data 指针, 避免循环内 class_pool 对象二次解引用 (pool->data_ptr_)
            // 编译器别名分析可能不 hoist data_ptr_, 显式提取保证循环内仅 1 次访存
            auto data_ptrs = std::make_tuple(
                sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()->data()...
            );

            if (pools_aligned_)
                {
                    constexpr size_t pd = 32;
                    constexpr bool small_data = (total_component_size_ <= 128);
                    // tiny_data: 8x 展开分支 (无软件预取, 依赖 HW 预取器)
                    //   ≤ 96B (1-8 comps): 8x 展开最大化 ILP, 寄存器压力可控
                    //   > 96B (9-10 comps): 8x 展开 80+ loads 触发寄存器 spill, 走 else 分支
                    constexpr bool tiny_data = (total_component_size_ <= 96);
                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    auto& primary_versions = primary->get_entity_versions();
                    const uint32_t* ver_data = primary_versions.data();
                    const size_t ver_size = primary_versions.size();
                    if (ver_size >= n) [[likely]]
                    {
                        if constexpr (small_data)
                        {
                            if constexpr (tiny_data)
                        {
                                constexpr bool needs_pf = (total_component_size_ > 32);
                                constexpr size_t pd_pf = 48;
                            const size_t n8 = n & ~size_t{7};
                            size_t i = 0;
                            for (; i < n8; i += 8)
                            {
                                if constexpr (needs_pf)
                                {
                                    if (i + pd_pf + 7 < n) [[likely]]
                                        (PREFETCH_R(&std::get<Is>(data_ptrs)[i + pd_pf]), ...);
                                }
                                entity e0(indices[i], ver_data[i]);
                                entity e1(indices[i + 1], ver_data[i + 1]);
                                entity e2(indices[i + 2], ver_data[i + 2]);
                                entity e3(indices[i + 3], ver_data[i + 3]);
                                entity e4(indices[i + 4], ver_data[i + 4]);
                                entity e5(indices[i + 5], ver_data[i + 5]);
                                entity e6(indices[i + 6], ver_data[i + 6]);
                                entity e7(indices[i + 7], ver_data[i + 7]);
                                func(e0, std::get<Is>(data_ptrs)[i]...);
                                func(e1, std::get<Is>(data_ptrs)[i + 1]...);
                                func(e2, std::get<Is>(data_ptrs)[i + 2]...);
                                func(e3, std::get<Is>(data_ptrs)[i + 3]...);
                                func(e4, std::get<Is>(data_ptrs)[i + 4]...);
                                func(e5, std::get<Is>(data_ptrs)[i + 5]...);
                                func(e6, std::get<Is>(data_ptrs)[i + 6]...);
                                func(e7, std::get<Is>(data_ptrs)[i + 7]...);
                            }
                                const size_t n4 = n & ~size_t{3};
                                for (; i < n4; i += 4)
                                {
                                    entity e0(indices[i], ver_data[i]);
                                    entity e1(indices[i + 1], ver_data[i + 1]);
                                    entity e2(indices[i + 2], ver_data[i + 2]);
                                    entity e3(indices[i + 3], ver_data[i + 3]);
                                    func(e0, std::get<Is>(data_ptrs)[i]...);
                                    func(e1, std::get<Is>(data_ptrs)[i + 1]...);
                                    func(e2, std::get<Is>(data_ptrs)[i + 2]...);
                                    func(e3, std::get<Is>(data_ptrs)[i + 3]...);
                                }
                                const size_t n2 = n & ~size_t{1};
                                for (; i < n2; i += 2)
                                {
                                    entity e0(indices[i], ver_data[i]);
                                    entity e1(indices[i + 1], ver_data[i + 1]);
                                    func(e0, std::get<Is>(data_ptrs)[i]...);
                                    func(e1, std::get<Is>(data_ptrs)[i + 1]...);
                                }
                                for (; i < n; ++i)
                                {
                                    entity e(indices[i], ver_data[i]);
                                    func(e, std::get<Is>(data_ptrs)[i]...);
                                }
                            }
                            else
                            {
                                const size_t n8 = n & ~size_t{7};
                                size_t i = 0;
                                for (; i < n8; i += 8)
                                {
                                    entity e0(indices[i], ver_data[i]);
                                    entity e1(indices[i + 1], ver_data[i + 1]);
                                    entity e2(indices[i + 2], ver_data[i + 2]);
                                    entity e3(indices[i + 3], ver_data[i + 3]);
                                    entity e4(indices[i + 4], ver_data[i + 4]);
                                    entity e5(indices[i + 5], ver_data[i + 5]);
                                    entity e6(indices[i + 6], ver_data[i + 6]);
                                    entity e7(indices[i + 7], ver_data[i + 7]);
                                    func(e0, std::get<Is>(data_ptrs)[i]...);
                                    func(e1, std::get<Is>(data_ptrs)[i + 1]...);
                                    func(e2, std::get<Is>(data_ptrs)[i + 2]...);
                                    func(e3, std::get<Is>(data_ptrs)[i + 3]...);
                                    func(e4, std::get<Is>(data_ptrs)[i + 4]...);
                                    func(e5, std::get<Is>(data_ptrs)[i + 5]...);
                                    func(e6, std::get<Is>(data_ptrs)[i + 6]...);
                                    func(e7, std::get<Is>(data_ptrs)[i + 7]...);
                                }
                                const size_t n4 = n & ~size_t{3};
                                for (; i < n4; i += 4)
                                {
                                    entity e0(indices[i], ver_data[i]);
                                    entity e1(indices[i + 1], ver_data[i + 1]);
                                    entity e2(indices[i + 2], ver_data[i + 2]);
                                    entity e3(indices[i + 3], ver_data[i + 3]);
                                    func(e0, std::get<Is>(data_ptrs)[i]...);
                                    func(e1, std::get<Is>(data_ptrs)[i + 1]...);
                                    func(e2, std::get<Is>(data_ptrs)[i + 2]...);
                                    func(e3, std::get<Is>(data_ptrs)[i + 3]...);
                                }
                                const size_t n2 = n & ~size_t{1};
                                for (; i < n2; i += 2)
                                {
                                    entity e0(indices[i], ver_data[i]);
                                    entity e1(indices[i + 1], ver_data[i + 1]);
                                    func(e0, std::get<Is>(data_ptrs)[i]...);
                                    func(e1, std::get<Is>(data_ptrs)[i + 1]...);
                                }
                                for (; i < n; ++i)
                                {
                                    entity e(indices[i], ver_data[i]);
                                    func(e, std::get<Is>(data_ptrs)[i]...);
                                }
                            }
                        }
                        else
                        {
                            const size_t main_count = (n > pd) ? (n - pd) : 0;
                            size_t i = 0;
                            for (; i < main_count; ++i)
                            {
                                (PREFETCH_R(&std::get<Is>(data_ptrs)[i + pd]), ...);
                                entity e(indices[i], ver_data[i]);
                                func(e, std::get<Is>(data_ptrs)[i]...);
                            }
                            for (; i < n; ++i)
                            {
                                entity e(indices[i], ver_data[i]);
                                func(e, std::get<Is>(data_ptrs)[i]...);
                            }
                        }
                    }
                    else
                    {
                        const size_t main_count = (n > pd) ? (n - pd) : 0;
                        size_t i = 0;
                        for (; i < main_count; ++i)
                        {
                            (PREFETCH_R(&std::get<Is>(data_ptrs)[i + pd]), ...);
                            primary->prefetch_sparse_entry(indices[i + pd]);
                            uint32_t eid = indices[i];
                            uint32_t ver = primary->sparse_version_at_public(eid);
                            entity e(eid, ver);
                            func(e, std::get<Is>(data_ptrs)[i]...);
                        }
                        for (; i < n; ++i)
                        {
                            uint32_t eid = indices[i];
                            uint32_t ver = primary->sparse_version_at_public(eid);
                            entity e(eid, ver);
                            func(e, std::get<Is>(data_ptrs)[i]...);
                        }
                    }
                }
                else
                {
                    if constexpr (small_data)
                    {
                        if constexpr (tiny_data)
                        {
                            constexpr bool needs_pf = (total_component_size_ > 32);
                            constexpr size_t pd_pf = 32;
                            const size_t n8 = n & ~size_t{7};
                            size_t i = 0;
                            for (; i < n8; i += 8)
                            {
                                if constexpr (needs_pf)
                                {
                                    if (i + pd_pf + 7 < n) [[likely]]
                                        (PREFETCH_R(&std::get<Is>(data_ptrs)[i + pd_pf]), ...);
                                }
                                func(std::get<Is>(data_ptrs)[i]...);
                                func(std::get<Is>(data_ptrs)[i + 1]...);
                                func(std::get<Is>(data_ptrs)[i + 2]...);
                                func(std::get<Is>(data_ptrs)[i + 3]...);
                                func(std::get<Is>(data_ptrs)[i + 4]...);
                                func(std::get<Is>(data_ptrs)[i + 5]...);
                                func(std::get<Is>(data_ptrs)[i + 6]...);
                                func(std::get<Is>(data_ptrs)[i + 7]...);
                            }
                            const size_t n4 = n & ~size_t{3};
                            for (; i < n4; i += 4)
                            {
                                func(std::get<Is>(data_ptrs)[i]...);
                                func(std::get<Is>(data_ptrs)[i + 1]...);
                                func(std::get<Is>(data_ptrs)[i + 2]...);
                                func(std::get<Is>(data_ptrs)[i + 3]...);
                            }
                            const size_t n2 = n & ~size_t{1};
                            for (; i < n2; i += 2)
                            {
                                func(std::get<Is>(data_ptrs)[i]...);
                                func(std::get<Is>(data_ptrs)[i + 1]...);
                            }
                            for (; i < n; ++i)
                            {
                                func(std::get<Is>(data_ptrs)[i]...);
                            }
                        }
                        else
                        {
                            const size_t n8 = n & ~size_t{7};
                            size_t i = 0;
                            for (; i < n8; i += 8)
                            {
                                func(std::get<Is>(data_ptrs)[i]...);
                                func(std::get<Is>(data_ptrs)[i + 1]...);
                                func(std::get<Is>(data_ptrs)[i + 2]...);
                                func(std::get<Is>(data_ptrs)[i + 3]...);
                                func(std::get<Is>(data_ptrs)[i + 4]...);
                                func(std::get<Is>(data_ptrs)[i + 5]...);
                                func(std::get<Is>(data_ptrs)[i + 6]...);
                                func(std::get<Is>(data_ptrs)[i + 7]...);
                            }
                            const size_t n4 = n & ~size_t{3};
                            for (; i < n4; i += 4)
                            {
                                func(std::get<Is>(data_ptrs)[i]...);
                                func(std::get<Is>(data_ptrs)[i + 1]...);
                                func(std::get<Is>(data_ptrs)[i + 2]...);
                                func(std::get<Is>(data_ptrs)[i + 3]...);
                            }
                            const size_t n2 = n & ~size_t{1};
                            for (; i < n2; i += 2)
                            {
                                func(std::get<Is>(data_ptrs)[i]...);
                                func(std::get<Is>(data_ptrs)[i + 1]...);
                            }
                            for (; i < n; ++i)
                            {
                                func(std::get<Is>(data_ptrs)[i]...);
                            }
                        }
                    }
                    else
                    {
                        const size_t main_count = (n > pd) ? (n - pd) : 0;
                        size_t i = 0;
                        for (; i < main_count; ++i)
                        {
                            (PREFETCH_R(&std::get<Is>(data_ptrs)[i + pd]), ...);
                            func(std::get<Is>(data_ptrs)[i]...);
                        }
                        for (; i < n; ++i)
                        {
                            func(std::get<Is>(data_ptrs)[i]...);
                        }
                    }
                }
                return;
            }

            // 非对齐路径: all_valid_ 时全量遍历, 否则用紧凑映射跳过无效实体
            if (all_valid_)
            {
                auto* raw = reinterpret_cast<uint32_t*>(dense_mappings_soa_.data());
                constexpr size_t stride = N;
                constexpr size_t pd = prefetch_distance_;
                constexpr size_t pd_off = pd * stride;
                const uint32_t* raw_end = raw + n * stride;
                const size_t main_count = (n > pd) ? (n - pd) : 0;
                const uint32_t* p_main_end = raw + main_count * stride;

                if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                {
                    auto* versions = cached_entity_versions_.data();
                    const uint32_t* p = raw;
                    size_t i = 0;
                    for (; p < p_main_end; p += stride, ++i)
                    {
                        (PREFETCH_R(&std::get<Is>(data_ptrs)[p[pd_off + Is]]), ...);
                        PREFETCH_R(&versions[i + pd]);
                        entity e(indices[i], versions[i]);
                        func(e, std::get<Is>(data_ptrs)[p[Is]]...);
                    }
                    for (; p < raw_end; p += stride, ++i)
                    {
                        entity e(indices[i], versions[i]);
                        func(e, std::get<Is>(data_ptrs)[p[Is]]...);
                    }
                }
                else
                {
                    const uint32_t* p = raw;
                    for (; p < p_main_end; p += stride)
                    {
                        (PREFETCH_R(&std::get<Is>(data_ptrs)[p[pd_off + Is]]), ...);
                        func(std::get<Is>(data_ptrs)[p[Is]]...);
                    }
                    for (; p < raw_end; p += stride)
                    {
                        func(std::get<Is>(data_ptrs)[p[Is]]...);
                    }
                }
                return;
            }

            // 紧凑映射路径: 只遍历有效实体, 无 UINT32_MAX 检查
            const size_t vn = compact_mappings_.size();
            if (vn == 0) return;
            auto* craw = reinterpret_cast<const uint32_t*>(compact_mappings_.data());
            constexpr size_t cstride = N;
            constexpr size_t cpd = prefetch_distance_;
            constexpr size_t cpd_off = cpd * cstride;
            const uint32_t* craw_end = craw + vn * cstride;
            const size_t cmain_count = (vn > cpd) ? (vn - cpd) : 0;
            const uint32_t* cp_main_end = craw + cmain_count * cstride;

            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
            {
                auto* versions = cached_entity_versions_.data();
                const uint32_t* p = craw;
                size_t i = 0;
                for (; p < cp_main_end; p += cstride, ++i)
                {
                    (PREFETCH_R(&std::get<Is>(data_ptrs)[p[cpd_off + Is]]), ...);
                    uint32_t prim = p[primary_idx_];
                    entity e(indices[prim], versions[prim]);
                    func(e, std::get<Is>(data_ptrs)[p[Is]]...);
                }
                for (; p < craw_end; p += cstride, ++i)
                {
                    uint32_t prim = p[primary_idx_];
                    entity e(indices[prim], versions[prim]);
                    func(e, std::get<Is>(data_ptrs)[p[Is]]...);
                }
            }
            else
            {
                const uint32_t* p = craw;
                for (; p < cp_main_end; p += cstride)
                {
                    (PREFETCH_R(&std::get<Is>(data_ptrs)[p[cpd_off + Is]]), ...);
                    func(std::get<Is>(data_ptrs)[p[Is]]...);
                }
                for (; p < craw_end; p += cstride)
                {
                    func(std::get<Is>(data_ptrs)[p[Is]]...);
                }
            }
        }

        template <size_t... Is>
        [[nodiscard]] bool contains_impl(entity e, std::index_sequence<Is...>) const noexcept
        {
            return (... && (sets_[Is] && sets_[Is]->template get_ptr_fast<std::tuple_element_t<Is, AllTypes>>(e) != nullptr));
        }

    public:
        multi_view(std::array<single_class_set*, N> sets, manager* mgr = nullptr) noexcept
            : sets_(sets), mgr_(mgr)
        {
            find_smallest();
        }

        // 实际匹配数 (所有组件都存在的实体数)
        [[nodiscard]] size_t size() const noexcept
        {
            if (!all_sets_valid()) return 0;
            ensure_mappings();
            return cached_count_;
        }

        // 主组件池大小 (最小池的实体数, O(1))
        [[nodiscard]] size_t pool_size() const noexcept
        {
            auto* p = sets_[primary_idx_];
            return p ? p->size() : 0;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return !all_sets_valid() || sets_[primary_idx_]->empty();
        }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            return all_sets_valid() && contains_impl(e, std::index_sequence_for<First, Rest...>{});
        }

        template <typename T>
        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            constexpr size_t idx = find_type_index<T, AllTypes>();
            return sets_[idx] ? sets_[idx]->template get_ptr_fast<T>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return entity{};
            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
                {
                    uint32_t eid = indices[i];
                    uint32_t ver = primary->sparse_version_at_public(eid);
                    entity e(eid, ver);
                    if (contains_impl(e, std::index_sequence_for<First, Rest...>{})) return e;
                }
                return entity{};
            }

            [[nodiscard]] entity get_last_entity() const noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return entity{};
            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            for (size_t i = indices.size(); i > 0; --i)
                {
                    size_t idx = i - 1;
                    uint32_t eid = indices[idx];
                uint32_t ver = primary->sparse_version_at_public(eid);
                entity e(eid, ver);
                if (contains_impl(e, std::index_sequence_for<First, Rest...>{})) return e;
            }
            return entity{};
        }

        [[nodiscard]] entity get_entity_at_index(size_t index) const noexcept
        {
            if (!all_sets_valid()) [[unlikely]] return entity{};
            auto* primary = sets_[primary_idx_];
            if (index >= primary->size()) [[unlikely]] return entity{};
            auto& indices = primary->get_entity_indices();
            return entity(indices[index], primary->get_version_unchecked(indices[index]));
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
        }

        template <typename... Optionals>
        class with_optionals
        {
        private:
            template <typename T>
            using set_ptr = single_class_set*;

            multi_view base_;
            manager* mgr_;
            std::tuple<set_ptr<Optionals>...> optional_sets_;

        public:
            with_optionals(multi_view base, manager* mgr,
                           std::tuple<set_ptr<Optionals>...> opt_sets) noexcept
                : base_(std::move(base)), mgr_(mgr), optional_sets_(opt_sets) {}

            template <typename U>
            auto include_optional_component() noexcept
            {
                auto* set = mgr_->template get_single_class_set<U>();
                return with_optionals<Optionals..., U>(
                    std::move(base_), mgr_,
                    std::tuple_cat(optional_sets_, std::make_tuple(set)));
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func),
                              std::index_sequence_for<Optionals...>{});
            }

        private:
            template <typename Func, size_t... OIs>
            void for_each_impl(Func&& func, std::index_sequence<OIs...>) noexcept
            {
                base_.for_each([&](entity e, auto&... comps) {
                    auto opt_ptrs = std::make_tuple(
                        std::get<OIs>(optional_sets_)->template get_ptr_fast<
                            std::tuple_element_t<OIs, std::tuple<Optionals...>>
                        >(e)...
                    );
                    std::apply([&](auto*... opts) {
                        if constexpr (std::is_invocable_v<Func, entity, decltype(comps)..., decltype(opts)...>)
                        {
                            func(e, comps..., opts...);
                        }
                        else
                        {
                            func(comps..., opts...);
                        }
                    }, opt_ptrs);
                });
            }
        };

        template <typename U>
        auto include_optional_component() noexcept
        {
            return with_optionals<U>(*this, mgr_,
                std::make_tuple(mgr_->template get_single_class_set<U>()));
        }

        // ======================== paged_view ========================
        class paged_view
        {
        private:
            multi_view base_;
            size_t offset_;
            size_t limit_;

        public:
            paged_view(multi_view base, size_t offset, size_t limit) noexcept
                : base_(base), offset_(offset), limit_(limit) {}

            [[nodiscard]] size_t size() const noexcept
            {
                size_t base_sz = base_.size();
                if (offset_ >= base_sz) return 0;
                size_t rem = base_sz - offset_;
                return rem < limit_ ? rem : limit_;
            }

            [[nodiscard]] bool empty() const noexcept { return size() == 0; }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                size_t skipped = 0;
                size_t processed = 0;
                base_.for_each([&](auto&... args) {
                    if (skipped < offset_)
                    {
                        ++skipped;
                        return;
                    }
                    if (processed >= limit_) return;
                    ++processed;
                    func(args...);
                });
            }
        };

        auto page(size_t offset, size_t limit) noexcept
        {
            return paged_view(*this, offset, limit);
        }

        // ======================== sorted_component_view ========================
        template <typename Compare, size_t SortIdx>
        class sorted_component_view
        {
        private:
            using SortType = std::tuple_element_t<SortIdx, AllTypes>;
            static constexpr size_t total_component_size_ = sizeof(First) + (0 + ... + sizeof(Rest));
            static constexpr size_t prefetch_distance_ =
                total_component_size_ <= 32 ? 32 : (total_component_size_ <= 128 ? 16 : 8);

            multi_view base_;
            Compare cmp_;
            dense<size_t> sorted_indices_;
            dense<size_t> radix_temp_buf_;
            dense<SortType> radix_keys_buf_;
            std::tuple<dense<First>, dense<Rest>...> sorted_pool_copies_;
            dense<entity> sorted_entities_;
            dense<uint64_t> last_versions_;
            bool needs_rebuild_{true};

            template <size_t... Is>
            void copy_valid_entities(std::index_sequence<Is...>) noexcept
            {
                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();

                auto original_pools = std::make_tuple(
                    base_.sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()...
                );

                std::apply([](auto&... pools) { (pools.clear(), ...); }, sorted_pool_copies_);
                sorted_entities_.clear();

                const size_t n = sorted_indices_.size();
                sorted_entities_.increase_capacity(n);
                std::apply([n](auto&... pools) { (pools.increase_capacity(n), ...); }, sorted_pool_copies_);

                if (base_.pools_aligned_)
                {
                    for (size_t i = 0; i < n; ++i)
                    {
                        size_t primary_i = sorted_indices_[i];
                        uint32_t eid = indices[primary_i];
                        uint32_t ver = primary->sparse_version_at_public(eid);
                        sorted_entities_.push_back(entity(eid, ver));
                        (std::get<Is>(sorted_pool_copies_).push_back(
                            (*std::get<Is>(original_pools))[primary_i]), ...);
                    }
                }
                else
                {
                    auto* raw = reinterpret_cast<uint32_t*>(base_.dense_mappings_soa_.data());
                    constexpr size_t stride = N;
                    for (size_t i = 0; i < n; ++i)
                    {
                        size_t primary_i = sorted_indices_[i];
                        const uint32_t* m = raw + primary_i * stride;
                        if (((m[Is] != UINT32_MAX) && ...))
                        {
                            uint32_t eid = indices[primary_i];
                            uint32_t ver = primary->sparse_version_at_public(eid);
                            sorted_entities_.push_back(entity(eid, ver));
                            (std::get<Is>(sorted_pool_copies_).push_back(
                                (*std::get<Is>(original_pools))[m[Is]]), ...);
                        }
                    }
                }
            }

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                if (!base_.all_sets_valid()) [[unlikely]] return;

                base_.ensure_mappings();
                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();
                if (n == 0) [[unlikely]]
                {
                    for (size_t i = 0; i < N; ++i)
                        if (base_.sets_[i]) last_versions_[i] = base_.sets_[i]->get_pool_version();
                    needs_rebuild_ = false;
                    return;
                }

                auto* typed_pool = base_.sets_[SortIdx]->template get_typed_pool_ptr<SortType>();
                if (!typed_pool) { std::abort(); }
                auto* sort_pool = typed_pool->data();
                constexpr size_t stride = N;

                sorted_indices_.increase_capacity(n);
                for (size_t i = 0; i < n; ++i)
                    sorted_indices_.push_back(i);

                size_t* idx_data = sorted_indices_.data();

                if (base_.pools_aligned_)
                {
                    if constexpr (std::is_same_v<std::decay_t<Compare>, std::less<SortType>>)
                    {
                        tiered_sort_indices<SortType>(idx_data, sort_pool, n);
                    }
                    else
                    {
                        pdqsort<size_t>(idx_data, n,
                            [sort_pool, this](size_t a, size_t b) noexcept {
                                return cmp_(sort_pool[a], sort_pool[b]);
                            });
                    }
                }
                else
                {
                    auto* raw = reinterpret_cast<uint32_t*>(base_.dense_mappings_soa_.data());
                    if constexpr (std::is_same_v<std::decay_t<Compare>, std::less<SortType>>)
                    {
                        radix_keys_buf_.increase_capacity(n, SortType{});
                        for (size_t i = 0; i < n; ++i)
                        {
                            uint32_t d = raw[i * stride + SortIdx];
                            radix_keys_buf_[i] = (d != UINT32_MAX) ? sort_pool[d] : SortType{};
                        }
                        tiered_sort_indices<SortType>(idx_data, radix_keys_buf_.data(), n);
                    }
                    else
                    {
                        SortType default_key{};
                        pdqsort<size_t>(idx_data, n,
                            [raw, sort_pool, &default_key, this](size_t a, size_t b) noexcept {
                                uint32_t da = raw[a * stride + SortIdx];
                                uint32_t db = raw[b * stride + SortIdx];
                                const SortType& ka = (da != UINT32_MAX) ? sort_pool[da] : default_key;
                                const SortType& kb = (db != UINT32_MAX) ? sort_pool[db] : default_key;
                                return cmp_(ka, kb);
                            });
                    }
                }

                copy_valid_entities(std::index_sequence_for<First, Rest...>{});

                for (size_t i = 0; i < N; ++i)
                {
                    if (base_.sets_[i])
                        last_versions_[i] = base_.sets_[i]->get_pool_version();
                }
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                for (size_t i = 0; i < N; ++i)
                {
                    if (base_.sets_[i] && base_.sets_[i]->get_pool_version() != last_versions_[i])
                    {
                        rebuild();
                        return;
                    }
                }
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                ensure_fresh();
                const size_t n = sorted_entities_.size();
                if (n == 0) return;

                auto data_ptrs = std::make_tuple(std::get<Is>(sorted_pool_copies_).data()...);
                entity* ents = sorted_entities_.data();

                for (size_t i = 0; i < n; ++i)
                {
                    if (i + prefetch_distance_ < n) [[likely]]
                    {
                        (PREFETCH_R(&std::get<Is>(data_ptrs)[i + prefetch_distance_]), ...);
                        PREFETCH_R(&ents[i + prefetch_distance_]);
                    }
                    if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                        func(ents[i], std::get<Is>(data_ptrs)[i]...);
                    else
                        func(std::get<Is>(data_ptrs)[i]...);
                }
            }

        public:
            sorted_component_view(multi_view base, Compare cmp) noexcept
                : base_(base), cmp_(std::move(cmp))
            {
                last_versions_.increase_capacity(N, 0);
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return sorted_entities_.size(); }
            [[nodiscard]] bool empty() const noexcept { return sorted_entities_.empty(); }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        template <typename T, typename Compare>
        auto sorted_by_component(Compare&& cmp) noexcept
        {
            constexpr size_t idx = find_type_index<T, AllTypes>();
            return sorted_component_view<Compare, idx>(*this, std::forward<Compare>(cmp));
        }

        // ======================== grouped_component_view ========================
        template <typename KeyType, typename KeyFunc>
        class grouped_component_view
        {
        private:
            multi_view base_;
            KeyFunc key_func_;
            dense<size_t> sorted_indices_;
            dense<KeyType> group_keys_;
            dense<size_t> group_starts_;
            dense<uint64_t> last_versions_;
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                group_keys_.clear();
                group_starts_.clear();
                if (!base_.all_sets_valid()) [[unlikely]] return;

                base_.ensure_mappings();
                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();

                struct sort_entry { KeyType key; size_t index; };
                dense<sort_entry> entries;
                entries.increase_capacity(n, {});

                auto* first_pool = base_.sets_[0]->template get_typed_pool_ptr<First>()->data();
                First default_first{};

                if (base_.pools_aligned_)
                {
                    for (size_t i = 0; i < n; ++i)
                    {
                        entries[i].key = key_func_(first_pool[i]);
                        entries[i].index = i;
                    }
                }
                else
                {
                    auto* raw = reinterpret_cast<uint32_t*>(base_.dense_mappings_soa_.data());
                    for (size_t i = 0; i < n; ++i)
                    {
                        uint32_t d = raw[i * N + 0];
                        entries[i].key = (d != UINT32_MAX) ? key_func_(first_pool[d]) : key_func_(default_first);
                        entries[i].index = i;
                    }
                }

                if constexpr (is_radix_sortable_v<KeyType>)
                {
                    radix_sort_entries<KeyType>(entries.data(), n);
                }
                else
                {
                    // MinGW+AVX2 下 std::sort+lambda 会崩溃, 使用 tiered_sort 替代
                    tiered_sort(entries.data(), n, [](const sort_entry& a, const sort_entry& b) {
                        return a.key < b.key;
                    });
                }

                sorted_indices_.increase_capacity(n, size_t{0});
                group_keys_.increase_capacity(n, KeyType{});
                for (size_t i = 0; i < n; ++i)
                {
                    sorted_indices_[i] = entries[i].index;
                    group_keys_[i] = entries[i].key;
                }

                group_starts_.push_back(0);
                for (size_t i = 1; i < n; ++i)
                {
                    if (group_keys_[i] != group_keys_[i - 1])
                        group_starts_.push_back(i);
                }

                for (size_t i = 0; i < N; ++i)
                {
                    if (base_.sets_[i])
                        last_versions_[i] = base_.sets_[i]->get_pool_version();
                }
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                for (size_t i = 0; i < N; ++i)
                {
                    if (base_.sets_[i] && base_.sets_[i]->get_pool_version() != last_versions_[i])
                    {
                        rebuild();
                        return;
                    }
                }
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                ensure_fresh();
                if (sorted_indices_.empty()) return;

                base_.ensure_mappings();
                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();

                auto pools = std::make_tuple(
                    base_.sets_[Is]->template get_typed_pool_ptr<std::tuple_element_t<Is, AllTypes>>()...
                );

                const size_t n = sorted_indices_.size();

                if (base_.pools_aligned_)
                {
                    for (size_t i = 0; i < n; ++i)
                    {
                        if (i + 32 < n) [[likely]]
                        {
                            size_t next_pi = sorted_indices_[i + 32];
                            (PREFETCH_R(&(*std::get<Is>(pools))[next_pi]), ...);
                            primary->prefetch_sparse_entry(indices[next_pi]);
                        }
                        size_t primary_i = sorted_indices_[i];
                        if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                        {
                            uint32_t eid = indices[primary_i];
                            uint32_t ver = primary->sparse_version_at_public(eid);
                            entity e(eid, ver);
                            func(e, (*std::get<Is>(pools))[primary_i]...);
                        }
                        else
                        {
                            func((*std::get<Is>(pools))[primary_i]...);
                        }
                    }
                }
                else
                {
                    auto* raw = reinterpret_cast<uint32_t*>(base_.dense_mappings_soa_.data());
                    constexpr size_t stride = N;
                    for (size_t i = 0; i < n; ++i)
                    {
                        if (i + 32 < n) [[likely]]
                        {
                            size_t next_pi = sorted_indices_[i + 32];
                            const uint32_t* next_m = raw + next_pi * stride;
                            (PREFETCH_R(&(*std::get<Is>(pools))[next_m[Is]]), ...);
                            primary->prefetch_sparse_entry(indices[next_pi]);
                        }
                        size_t primary_i = sorted_indices_[i];
                        const uint32_t* m = raw + primary_i * stride;
                        if (((m[Is] == UINT32_MAX) || ...)) [[unlikely]] continue;
                        if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                        {
                            uint32_t eid = indices[primary_i];
                            uint32_t ver = primary->sparse_version_at_public(eid);
                            entity e(eid, ver);
                            func(e, (*std::get<Is>(pools))[m[Is]]...);
                        }
                        else
                        {
                            func((*std::get<Is>(pools))[m[Is]]...);
                        }
                    }
                }
            }

        public:
            grouped_component_view(multi_view base, KeyFunc key_func) noexcept
                : base_(base), key_func_(std::move(key_func))
            {
                last_versions_.increase_capacity(N, 0);
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return sorted_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return sorted_indices_.empty(); }
            [[nodiscard]] size_t group_count() const noexcept { return group_starts_.size(); }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }

            template <typename Func>
            void for_each_group(Func&& func) noexcept
            {
                ensure_fresh();
                if (sorted_indices_.empty()) return;

                for (size_t g = 0; g < group_starts_.size(); ++g)
                {
                    size_t start = group_starts_[g];
                    size_t end = (g + 1 < group_starts_.size()) ? group_starts_[g + 1] : sorted_indices_.size();
                    KeyType key = group_keys_[start];
                    func(key, start, end);
                }
            }
        };

        template <typename KeyFunc>
        auto sorted_by_component_value(KeyFunc&& key_func) noexcept
        {
            using KeyType = std::invoke_result_t<KeyFunc, First&>;
            return grouped_component_view<KeyType, KeyFunc>(*this, std::forward<KeyFunc>(key_func));
        }

        // ======================== changed_view ========================
        class changed_view
        {
        private:
            multi_view base_;
            dense<uint64_t> last_versions_;
            dense<size_t> changed_indices_;
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                changed_indices_.clear();
                if (!base_.all_sets_valid()) [[unlikely]]
                {
                    needs_rebuild_ = false;
                    return;
                }

                bool any_changed = false;
                for (size_t i = 0; i < N; ++i)
                {
                    if (base_.sets_[i])
                    {
                        uint64_t cur = base_.sets_[i]->get_pool_version();
                        if (cur != last_versions_[i])
                        {
                            last_versions_[i] = cur;
                            any_changed = true;
                        }
                    }
                }

                if (!any_changed)
                {
                    needs_rebuild_ = false;
                    return;
                }

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();

                for (size_t i = 0; i < n; ++i)
                {
                    if (i + 8 < n) [[likely]]
                        primary->prefetch_sparse_entry(indices[i + 8]);
                    uint32_t eid = indices[i];
                    uint32_t ver = primary->sparse_version_at_public(eid);
                    entity e(eid, ver);
                    if (base_.contains_impl(e, std::index_sequence_for<First, Rest...>{}))
                        changed_indices_.push_back(i);
                }
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (!needs_rebuild_) return;
                rebuild();
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                ensure_fresh();
                needs_rebuild_ = true;
                if (changed_indices_.empty()) return;

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();

                for (size_t i = 0; i < changed_indices_.size(); ++i)
                {
                    if (i + 8 < changed_indices_.size()) [[likely]]
                        primary->prefetch_sparse_entry(indices[changed_indices_[i + 8]]);
                    size_t primary_i = changed_indices_[i];
                    uint32_t eid = indices[primary_i];
                    uint32_t ver = primary->sparse_version_at_public(eid);
                    entity e(eid, ver);

                    auto comps = std::make_tuple(
                        base_.template get_component<Is>(e, primary_i)...
                    );

                    if ((... && (std::get<Is>(comps) != nullptr))) [[likely]]
                    {
                        std::apply([&](auto*... ptrs) {
                            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                                func(e, *ptrs...);
                            else
                                func(*ptrs...);
                        }, comps);
                    }
                }
            }

        public:
            changed_view(multi_view base) noexcept : base_(base)
            {
                last_versions_.increase_capacity(N, 0);
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return changed_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return changed_indices_.empty(); }

            void reset_tracking() noexcept
            {
                for (size_t i = 0; i < N; ++i)
                    last_versions_[i] = 0;
                changed_indices_.clear();
                needs_rebuild_ = true;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        auto track_changes() noexcept
        {
            return changed_view(*this);
        }

        // ======================== multi_view::filter_changed ========================
        template <size_t TrackIdx>
        class filter_changed_view
        {
        private:
            multi_view base_;
            dense<uint64_t> last_observed_versions_;
            dense<size_t> changed_indices_;
            bool needs_rebuild_{true};

            using TrackType = std::tuple_element_t<TrackIdx, std::tuple<First, Rest...>>;

            void rebuild() noexcept
            {
                changed_indices_.clear();
                if (!base_.all_sets_valid()) [[unlikely]] { needs_rebuild_ = false; return; }
                if (!base_.sets_[TrackIdx]) [[unlikely]] { needs_rebuild_ = false; return; }

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();
                // 仅扩展，不覆盖已追踪的版本号
                while (last_observed_versions_.size() < n)
                    last_observed_versions_.push_back(0);

                auto* track_set = base_.sets_[TrackIdx];

                for (size_t i = 0; i < n; ++i)
                {
                    if (i + 8 < n) [[likely]]
                        primary->prefetch_sparse_entry(indices[i + 8]);
                    uint32_t eid = indices[i];
                    uint32_t ver = primary->sparse_version_at_public(eid);
                    entity e(eid, ver);
                    if (!base_.contains_impl(e, std::index_sequence_for<First, Rest...>{})) continue;

                    uint32_t dense_idx = track_set->get_dense_at(e.parts_.index_);
                    uint64_t cur = track_set->get_entity_change_version(dense_idx);
                    if (i >= last_observed_versions_.size() || cur != last_observed_versions_[i])
                    {
                        if (i < last_observed_versions_.size())
                            last_observed_versions_[i] = cur;
                        else
                            last_observed_versions_[i] = cur;
                        changed_indices_.push_back(i);
                    }
                }
                needs_rebuild_ = false;
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                if (needs_rebuild_) rebuild();
                needs_rebuild_ = true;
                if (changed_indices_.empty()) return;

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();

                for (size_t i = 0; i < changed_indices_.size(); ++i)
                {
                    if (i + 8 < changed_indices_.size()) [[likely]]
                        primary->prefetch_sparse_entry(indices[changed_indices_[i + 8]]);
                    size_t primary_i = changed_indices_[i];
                    uint32_t eid = indices[primary_i];
                    uint32_t ver = primary->sparse_version_at_public(eid);
                    entity e(eid, ver);
                    auto comps = std::make_tuple(base_.template get_component<Is>(e, primary_i)...);
                    if ((... && (std::get<Is>(comps) != nullptr))) [[likely]]
                    {
                        std::apply([&](auto*... ptrs) {
                            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                                func(e, *ptrs...);
                            else
                                func(*ptrs...);
                        }, comps);
                    }
                }
            }

        public:
            filter_changed_view(multi_view base) noexcept : base_(base) { rebuild(); }

            [[nodiscard]] size_t size() const noexcept { return changed_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return changed_indices_.empty(); }

            void reset_tracking() noexcept
            {
                last_observed_versions_.clear();
                changed_indices_.clear();
                needs_rebuild_ = true;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        template <typename T>
        auto filter_changed() noexcept
        {
            constexpr size_t idx = find_type_index<T, std::tuple<First, Rest...>>();
            return filter_changed_view<idx>(*this);
        }

        auto filter_any_changed() noexcept
        {
            return filter_changed_view<0>(*this);
        }

        // ======================== multi_view::filter_added ========================
        template <size_t TrackIdx>
        class filter_added_view
        {
        private:
            multi_view base_;
            dense<uint64_t> last_observed_added_;
            dense<size_t> added_indices_;
            bool needs_rebuild_{true};
            uint64_t baseline_added_counter_{0};

            using TrackType = std::tuple_element_t<TrackIdx, std::tuple<First, Rest...>>;

            void rebuild() noexcept
            {
                added_indices_.clear();
                if (!base_.all_sets_valid()) [[unlikely]] { needs_rebuild_ = false; return; }
                if (!base_.sets_[TrackIdx]) [[unlikely]] { needs_rebuild_ = false; return; }

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();
                const size_t n = indices.size();
                while (last_observed_added_.size() < n)
                    last_observed_added_.push_back(0);

                auto* track_set = base_.sets_[TrackIdx];

                for (size_t i = 0; i < n; ++i)
                {
                    if (i + 8 < n) [[likely]]
                        primary->prefetch_sparse_entry(indices[i + 8]);
                    uint32_t eid = indices[i];
                    uint32_t ver = primary->sparse_version_at_public(eid);
                    entity e(eid, ver);
                    if (!base_.contains_impl(e, std::index_sequence_for<First, Rest...>{})) continue;

                    uint32_t dense_idx = track_set->get_dense_at(e.parts_.index_);
                    uint64_t cur = track_set->get_entity_added_version(dense_idx);
                    if (cur > baseline_added_counter_ && (i >= last_observed_added_.size() || cur != last_observed_added_[i]))
                    {
                        if (i < last_observed_added_.size())
                            last_observed_added_[i] = cur;
                        else
                            last_observed_added_[i] = cur;
                        added_indices_.push_back(i);
                    }
                }
                needs_rebuild_ = false;
            }

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                if (needs_rebuild_) rebuild();
                needs_rebuild_ = true;
                if (added_indices_.empty()) return;

                auto* primary = base_.sets_[base_.primary_idx_];
                auto& indices = primary->get_entity_indices();

                for (size_t i = 0; i < added_indices_.size(); ++i)
                {
                    if (i + 8 < added_indices_.size()) [[likely]]
                        primary->prefetch_sparse_entry(indices[added_indices_[i + 8]]);
                    size_t primary_i = added_indices_[i];
                    uint32_t eid = indices[primary_i];
                    uint32_t ver = primary->sparse_version_at_public(eid);
                    entity e(eid, ver);
                    auto comps = std::make_tuple(base_.template get_component<Is>(e, primary_i)...);
                    if ((... && (std::get<Is>(comps) != nullptr))) [[likely]]
                    {
                        std::apply([&](auto*... ptrs) {
                            if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                                func(e, *ptrs...);
                            else
                                func(*ptrs...);
                        }, comps);
                    }
                }
            }

        public:
            filter_added_view(multi_view base) noexcept : base_(base)
            {
                if (base_.all_sets_valid() && base_.sets_[TrackIdx])
                    baseline_added_counter_ = base_.sets_[TrackIdx]->get_global_added_counter();
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return added_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return added_indices_.empty(); }

            void reset_tracking() noexcept
            {
                last_observed_added_.clear();
                added_indices_.clear();
                needs_rebuild_ = true;
                baseline_added_counter_ = 0;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        template <typename T>
        auto filter_added() noexcept
        {
            constexpr size_t idx = find_type_index<T, std::tuple<First, Rest...>>();
            return filter_added_view<idx>(*this);
        }

        // ======================== multi_view::exactly_one ========================
        [[nodiscard]] std::tuple<First&, Rest&...> exactly_one() noexcept
        {
            assert(all_sets_valid() && "exactly_one(): sets not valid");
            auto* primary = sets_[primary_idx_];
            auto& indices = primary->get_entity_indices();
            size_t match_count = 0;
            size_t last_match = 0;

            for (size_t i = 0; i < indices.size(); ++i)
            {
                uint32_t eid = indices[i];
                uint32_t ver = primary->sparse_version_at_public(eid);
                entity e(eid, ver);
                if (contains_impl(e, std::index_sequence_for<First, Rest...>{}))
                {
                    last_match = i;
                    ++match_count;
                    if (match_count > 1) break;
                }
            }
            assert(match_count == 1 && "exactly_one(): expected exactly 1 matching entity");

            entity e(indices[last_match], primary->get_version_unchecked(indices[last_match]));
            return std::forward_as_tuple(
                *sets_[find_type_index<First, std::tuple<First, Rest...>>()]->template get_ptr_fast<First>(e),
                *sets_[find_type_index<Rest, std::tuple<First, Rest...>>()]->template get_ptr_fast<Rest>(e)...
            );
        }

        // ======================== multi_view::find_one ========================
        [[nodiscard]] std::tuple<First*, Rest*...> find_one(entity e) noexcept
        {
            if (!all_sets_valid()) [[unlikely]]
                return std::make_tuple(static_cast<First*>(nullptr), static_cast<Rest*>(nullptr)...);
            if (!contains_impl(e, std::index_sequence_for<First, Rest...>{}))
                return std::make_tuple(static_cast<First*>(nullptr), static_cast<Rest*>(nullptr)...);
            return std::make_tuple(
                sets_[find_type_index<First, std::tuple<First, Rest...>>()]->template get_ptr_fast<First>(e),
                sets_[find_type_index<Rest, std::tuple<First, Rest...>>()]->template get_ptr_fast<Rest>(e)...
            );
        }

        // ======================== multi_view::iter_over_entities ========================
        template <typename EntitySpan>
        class entity_specific_view
        {
        private:
            multi_view base_;
            std::remove_reference_t<EntitySpan> entities_;

        public:
            entity_specific_view(multi_view base, EntitySpan entities) noexcept
                : base_(base), entities_(std::forward<EntitySpan>(entities)) {}

            template <typename Func, size_t... Is>
            void for_each_impl(Func&& func, std::index_sequence<Is...>) noexcept
            {
                for (auto e : entities_)
                {
                    if (!base_.contains_impl(e, std::index_sequence_for<First, Rest...>{})) continue;
                    std::apply([&](auto*... ptrs) {
                        if constexpr (std::is_invocable_v<Func, entity, First&, Rest&...>)
                            func(e, *ptrs...);
                        else
                            func(*ptrs...);
                    }, std::make_tuple(
                        base_.sets_[Is]->template get_ptr_fast<std::tuple_element_t<Is, std::tuple<First, Rest...>>>(e)...
                    ));
                }
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                for_each_impl(std::forward<Func>(func), std::index_sequence_for<First, Rest...>{});
            }
        };

        template <typename EntitySpan>
        auto iter_over_entities(EntitySpan&& entities) noexcept
        {
            return entity_specific_view<EntitySpan>(*this, std::forward<EntitySpan>(entities));
        }
    };
