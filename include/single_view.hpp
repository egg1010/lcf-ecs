// single_view.hpp —— manager 类内片段,由 component.hpp 在 manager 类内部 include
// 不要单独 include 此文件
    template <typename T>
    class single_view
    {
    private:
        single_class_set* set_;
        manager* mgr_;

        void resolve_set() noexcept
        {
            if (mgr_) set_ = mgr_->template get_single_class_set<T>();
        }

    public:
        single_view(single_class_set* set, manager* mgr = nullptr) noexcept : set_(set), mgr_(mgr) {}

        class iterator
        {
        private:
            single_view view_;
            size_t index_;
        public:
            iterator(single_view view, size_t index) noexcept : view_(view), index_(index) {}

            [[nodiscard]] entity operator*() const noexcept
            {
                auto& indices = view_.set_->get_entity_indices();
                return entity(indices[index_], view_.set_->get_version_unchecked(indices[index_]));
            }

            iterator& operator++() noexcept { ++index_; return *this; }
            [[nodiscard]] bool operator!=(const iterator& other) const noexcept { return index_ != other.index_; }
        };

        using component_iterator = T*;

        [[nodiscard]] component_iterator component_begin() noexcept
        {
            if (!set_) [[unlikely]] return nullptr;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return pool ? pool->data() : nullptr;
        }
        [[nodiscard]] component_iterator component_end() noexcept
        {
            if (!set_) [[unlikely]] return nullptr;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return pool ? pool->data() + pool->size() : nullptr;
        }

        [[nodiscard]] iterator begin() noexcept { return iterator(*this, 0); }
        [[nodiscard]] iterator end() noexcept { return iterator(*this, set_ ? set_->size() : 0); }

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            // 合并 dense+version 查找: 单次 sparse_entry 加载,
            //   替代原 sparse_dense_at_public + sparse_version_at_public 两次独立查找
            if (!set_ || !e.is_valid()) [[unlikely]] return false;
            uint32_t ver = 0;
            uint32_t dense = set_->sparse_dense_version_public(e.parts_.index_, ver);
            if (dense == single_class_set::dense_invalid) [[unlikely]] return false;
            return ver == e.parts_.version_;
        }

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            return set_ ? set_->template get_ptr_fast_inline<T>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!set_ || set_->size() == 0) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            return entity(indices[0], set_->get_version_unchecked(indices[0]));
        }

        [[nodiscard]] entity get_last_entity() const noexcept
        {
            if (!set_ || set_->size() == 0) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            size_t last = indices.size() - 1;
            return entity(indices[last], set_->get_version_unchecked(indices[last]));
        }

        [[nodiscard]] entity get_entity_at_index(size_t index) const noexcept
        {
            if (!set_ || index >= set_->size()) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            return entity(indices[index], set_->get_version_unchecked(indices[index]));
        }

        [[nodiscard]] T* get_component_at_index(size_t index) noexcept
        {
            if (!set_ || index >= set_->size()) [[unlikely]] return nullptr;
            // 用缓存的 typed_pool_data_ 直接寻址, 跳过 dense<T>::data() 间接读
            T* data = set_->template get_typed_pool_data_ptr<T>();
            return data ? data + index : nullptr;
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            resolve_set();
            if (!set_) [[unlikely]] return;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return;

            if constexpr (std::is_invocable_v<Func, entity, T&>)
            {
                auto& indices = set_->get_entity_indices();
                auto& versions = set_->get_entity_versions();
                const size_t n = indices.size();
                // 若 versions_ 已同步, 走连续读快速路径, 避免 sparse_entry 间接查找
                if (versions.size() >= n) [[likely]]
                {
                    const uint32_t* idx_data = indices.data();
                    const uint32_t* ver_data = versions.data();
                    T* data = pool->data();
                    // 8x 循环展开: 最大化 ILP, 4 个 entity+component load 并行发射
                    //   瓶颈为 load throughput (2 loads/cyc), 8x 展开摊薄 loop overhead
                    const size_t n8 = n & ~size_t{7};
                    size_t i = 0;
                    for (; i < n8; i += 8)
                    {
                        entity e0(idx_data[i],     ver_data[i]);
                        entity e1(idx_data[i + 1], ver_data[i + 1]);
                        entity e2(idx_data[i + 2], ver_data[i + 2]);
                        entity e3(idx_data[i + 3], ver_data[i + 3]);
                        entity e4(idx_data[i + 4], ver_data[i + 4]);
                        entity e5(idx_data[i + 5], ver_data[i + 5]);
                        entity e6(idx_data[i + 6], ver_data[i + 6]);
                        entity e7(idx_data[i + 7], ver_data[i + 7]);
                        func(e0, data[i]);
                        func(e1, data[i + 1]);
                        func(e2, data[i + 2]);
                        func(e3, data[i + 3]);
                        func(e4, data[i + 4]);
                        func(e5, data[i + 5]);
                        func(e6, data[i + 6]);
                        func(e7, data[i + 7]);
                    }
                    const size_t n4 = n & ~size_t{3};
                    for (; i < n4; i += 4)
                    {
                        entity e0(idx_data[i],     ver_data[i]);
                        entity e1(idx_data[i + 1], ver_data[i + 1]);
                        entity e2(idx_data[i + 2], ver_data[i + 2]);
                        entity e3(idx_data[i + 3], ver_data[i + 3]);
                        func(e0, data[i]);
                        func(e1, data[i + 1]);
                        func(e2, data[i + 2]);
                        func(e3, data[i + 3]);
                    }
                    for (; i < n; ++i)
                    {
                        entity e(idx_data[i], ver_data[i]);
                        func(e, data[i]);
                    }
                }
                else
                {
                    // 回退路径: versions_ 未同步 (旧数据), 走 sparse_entry 查找
                    for (size_t i = 0; i < n; ++i)
                    {
                        uint32_t eid = indices[i];
                        uint32_t ver = set_->sparse_version_at_public(eid);
                        entity e(eid, ver);
                        func(e, (*pool)[i]);
                    }
                }
            }
            else
            {
                T* data = pool->data();
                const size_t n = pool->size();
                // 8x 循环展开 (无 entity 版本)
                const size_t n8 = n & ~size_t{7};
                size_t i = 0;
                for (; i < n8; i += 8)
                {
                    func(data[i]);
                    func(data[i + 1]);
                    func(data[i + 2]);
                    func(data[i + 3]);
                    func(data[i + 4]);
                    func(data[i + 5]);
                    func(data[i + 6]);
                    func(data[i + 7]);
                }
                for (; i < n; ++i)
                {
                    func(data[i]);
                }
            }
        }

        // ======================== single_view::paged_view ========================
        class paged_view
        {
        private:
            single_view base_;
            size_t offset_;
            size_t limit_;

        public:
            paged_view(single_view base, size_t offset, size_t limit) noexcept
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

        // ======================== single_view::sorted_component_view ========================
        template <typename Compare>
        class sorted_component_view
        {
        private:
            static constexpr size_t prefetch_distance_ = sizeof(T) <= 16 ? 32 : (sizeof(T) <= 64 ? 16 : 8);

            single_view base_;
            Compare cmp_;
            dense<size_t> sorted_indices_;
            dense<size_t> radix_temp_buf_;
            dense<T> sorted_pool_copy_;
            dense<entity> sorted_entities_;
            uint64_t last_version_{0};
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                sorted_pool_copy_.clear();
                sorted_entities_.clear();
                if (!base_.set_) [[unlikely]] return;
                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;

                const size_t n = pool->size();
                if (n == 0) [[unlikely]]
                {
                    last_version_ = base_.set_->get_pool_version();
                    needs_rebuild_ = false;
                    return;
                }

                T* pool_data = pool->data();

                sorted_indices_.increase_capacity(n);
                for (size_t i = 0; i < n; ++i)
                    sorted_indices_.push_back(i);

                size_t* idx_data = sorted_indices_.data();

                if constexpr (std::is_same_v<std::decay_t<Compare>, std::less<T>>)
                {
                    tiered_sort_indices<T>(idx_data, pool_data, n);
                }
                else
                {
                    // MinGW+AVX2 下 std::sort+lambda 会崩溃, 使用 pdqsort 替代
                    pdqsort<size_t>(idx_data, n,
                        [pool_data, this](size_t a, size_t b) {
                            return cmp_(pool_data[a], pool_data[b]);
                        });
                }

                auto& indices = base_.set_->get_entity_indices();
                auto& versions = base_.set_->get_entity_versions();
                sorted_pool_copy_.increase_capacity(n);
                sorted_entities_.increase_capacity(n);
                // versions_ 已同步时走顺序读快速路径, 避免 sparse_entry 随机访问
                if (versions.size() >= n) [[likely]]
                {
                    const uint32_t* idx_data = indices.data();
                    const uint32_t* ver_data = versions.data();
                    for (size_t i = 0; i < n; ++i)
                    {
                        size_t idx = sorted_indices_[i];
                        sorted_pool_copy_.push_back(pool_data[idx]);
                        sorted_entities_.push_back(entity(idx_data[idx], ver_data[idx]));
                    }
                }
                else
                {
                    for (size_t i = 0; i < n; ++i)
                    {
                        size_t idx = sorted_indices_[i];
                        sorted_pool_copy_.push_back(pool_data[idx]);
                        uint32_t eid = indices[idx];
                        uint32_t ver = base_.set_->sparse_version_at_public(eid);
                        sorted_entities_.push_back(entity(eid, ver));
                    }
                }

                last_version_ = base_.set_->get_pool_version();
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (base_.set_ && base_.set_->get_pool_version() != last_version_)
                    rebuild();
            }

        public:
            sorted_component_view(single_view base, Compare cmp) noexcept
                : base_(base), cmp_(std::move(cmp))
            {
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return sorted_pool_copy_.size(); }
            [[nodiscard]] bool empty() const noexcept { return sorted_pool_copy_.empty(); }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                ensure_fresh();
                const size_t n = sorted_pool_copy_.size();
                if (n == 0) return;

                T* data = sorted_pool_copy_.data();
                entity* ents = sorted_entities_.data();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    for (size_t i = 0; i < n; ++i)
                    {
                        if (i + prefetch_distance_ < n) [[likely]]
                        {
                            PREFETCH_R(&data[i + prefetch_distance_]);
                            PREFETCH_R(&ents[i + prefetch_distance_]);
                        }
                        func(ents[i], data[i]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < n; ++i)
                    {
                        if (i + prefetch_distance_ < n) [[likely]]
                            PREFETCH_R(&data[i + prefetch_distance_]);
                        func(data[i]);
                    }
                }
            }
        };

        template <typename Compare>
        auto sorted_by_component(Compare&& cmp) noexcept
        {
            return sorted_component_view<Compare>(*this, std::forward<Compare>(cmp));
        }

        // ======================== single_view::grouped_component_view ========================
        template <typename KeyType, typename KeyFunc>
        class grouped_component_view
        {
        private:
            single_view base_;
            KeyFunc key_func_;
            dense<size_t> sorted_indices_;
            dense<KeyType> group_keys_;
            dense<size_t> group_starts_;
            uint64_t last_version_{0};
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                group_keys_.clear();
                group_starts_.clear();
                if (!base_.set_) [[unlikely]] return;
                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;

                const size_t n = pool->size();
                T* pool_data = pool->data();

                struct sort_entry { KeyType key; size_t index; };
                dense<sort_entry> entries;
                entries.increase_capacity(n, {});

                for (size_t i = 0; i < n; ++i)
                {
                    entries[i].key = key_func_(pool_data[i]);
                    entries[i].index = i;
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

                last_version_ = base_.set_->get_pool_version();
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (base_.set_ && base_.set_->get_pool_version() != last_version_)
                    rebuild();
            }

        public:
            grouped_component_view(single_view base, KeyFunc key_func) noexcept
                : base_(base), key_func_(std::move(key_func))
            {
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return sorted_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return sorted_indices_.empty(); }
            [[nodiscard]] size_t group_count() const noexcept { return group_starts_.size(); }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                ensure_fresh();
                if (sorted_indices_.empty()) return;

                T* data = base_.set_->template get_typed_pool_data_ptr<T>();
                if (!data) [[unlikely]] return;
                auto& indices = base_.set_->get_entity_indices();
                auto& versions = base_.set_->get_entity_versions();
                const size_t n = sorted_indices_.size();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    // versions_ 已同步时走顺序读, 避免 sparse_entry 随机访问
                    if (versions.size() >= indices.size()) [[likely]]
                    {
                        const uint32_t* idx_data = indices.data();
                        const uint32_t* ver_data = versions.data();
                        const size_t* sorted = sorted_indices_.data();
                        for (size_t i = 0; i < n; ++i)
                        {
                            if (i + 32 < n) [[likely]]
                                PREFETCH_R(&data[sorted[i + 32]]);
                            size_t idx = sorted[i];
                            entity e(idx_data[idx], ver_data[idx]);
                            func(e, data[idx]);
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < n; ++i)
                        {
                            if (i + 32 < n) [[likely]]
                                PREFETCH_R(&data[sorted_indices_[i + 32]]);
                            size_t idx = sorted_indices_[i];
                            uint32_t eid = indices[idx];
                            uint32_t ver = base_.set_->sparse_version_at_public(eid);
                            entity e(eid, ver);
                            func(e, data[idx]);
                        }
                    }
                }
                else
                {
                    const size_t* sorted = sorted_indices_.data();
                    for (size_t i = 0; i < n; ++i)
                    {
                        if (i + 32 < n) [[likely]]
                            PREFETCH_R(&data[sorted[i + 32]]);
                        func(data[sorted[i]]);
                    }
                }
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
            using KeyType = std::invoke_result_t<KeyFunc, T&>;
            return grouped_component_view<KeyType, KeyFunc>(*this, std::forward<KeyFunc>(key_func));
        }

        // ======================== single_view::changed_view ========================
        class changed_view
        {
        private:
            single_view base_;
            uint64_t last_version_{0};
            size_t changed_count_{0};
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                base_.resolve_set();
                if (!base_.set_) [[unlikely]]
                {
                    changed_count_ = 0;
                    needs_rebuild_ = false;
                    return;
                }

                uint64_t cur = base_.set_->get_pool_version();
                if (cur == last_version_)
                {
                    changed_count_ = 0;
                    needs_rebuild_ = false;
                    return;
                }
                last_version_ = cur;

                // pool_version 变化: 全部实体均为 changed, 无需存储 indices
                // for_each 直接顺序遍历 [0, n)
                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                changed_count_ = pool ? pool->size() : 0;
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (!needs_rebuild_) return;
                rebuild();
            }

        public:
            changed_view(single_view base) noexcept : base_(base)
            {
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return changed_count_; }
            [[nodiscard]] bool empty() const noexcept { return changed_count_ == 0; }

            void reset_tracking() noexcept
            {
                last_version_ = 0;
                changed_count_ = 0;
                needs_rebuild_ = true;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                ensure_fresh();
                needs_rebuild_ = true;
                if (changed_count_ == 0) return;

                T* data = base_.set_->template get_typed_pool_data_ptr<T>();
                if (!data) [[unlikely]] return;
                auto& indices = base_.set_->get_entity_indices();
                auto& versions = base_.set_->get_entity_versions();
                const size_t n = changed_count_;

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    // versions_ 已同步时走顺序读, 避免 sparse_entry 随机访问
                    if (versions.size() >= indices.size()) [[likely]]
                    {
                        const uint32_t* idx_data = indices.data();
                        const uint32_t* ver_data = versions.data();
                        // 8x 循环展开
                        const size_t n8 = n & ~size_t{7};
                        size_t i = 0;
                        for (; i < n8; i += 8)
                        {
                            entity e0(idx_data[i],     ver_data[i]);
                            entity e1(idx_data[i + 1], ver_data[i + 1]);
                            entity e2(idx_data[i + 2], ver_data[i + 2]);
                            entity e3(idx_data[i + 3], ver_data[i + 3]);
                            entity e4(idx_data[i + 4], ver_data[i + 4]);
                            entity e5(idx_data[i + 5], ver_data[i + 5]);
                            entity e6(idx_data[i + 6], ver_data[i + 6]);
                            entity e7(idx_data[i + 7], ver_data[i + 7]);
                            func(e0, data[i]);
                            func(e1, data[i + 1]);
                            func(e2, data[i + 2]);
                            func(e3, data[i + 3]);
                            func(e4, data[i + 4]);
                            func(e5, data[i + 5]);
                            func(e6, data[i + 6]);
                            func(e7, data[i + 7]);
                        }
                        for (; i < n; ++i)
                        {
                            entity e(idx_data[i], ver_data[i]);
                            func(e, data[i]);
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < n; ++i)
                        {
                            uint32_t eid = indices[i];
                            uint32_t ver = base_.set_->sparse_version_at_public(eid);
                            entity e(eid, ver);
                            func(e, data[i]);
                        }
                    }
                }
                else
                {
                    // 8x 循环展开 (无 entity 版本)
                    const size_t n8 = n & ~size_t{7};
                    size_t i = 0;
                    for (; i < n8; i += 8)
                    {
                        func(data[i]);
                        func(data[i + 1]);
                        func(data[i + 2]);
                        func(data[i + 3]);
                        func(data[i + 4]);
                        func(data[i + 5]);
                        func(data[i + 6]);
                        func(data[i + 7]);
                    }
                    for (; i < n; ++i)
                    {
                        func(data[i]);
                    }
                }
            }
        };

        auto track_changes() noexcept
        {
            return changed_view(*this);
        }

        // ======================== single_view::filter_changed ========================
        class filter_changed_view
        {
        private:
            single_view base_;
            dense<uint64_t> last_observed_versions_;
            dense<size_t> changed_indices_;
            bool needs_rebuild_{true};
            uint64_t last_pool_version_{0};

            void rebuild() noexcept
            {
                changed_indices_.clear();
                base_.resolve_set();
                if (!base_.set_) [[unlikely]] { needs_rebuild_ = false; return; }
                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] { needs_rebuild_ = false; return; }

                const size_t n = pool->size();
                // 快速路径: pool_version 未变 + 已追踪所有实体 → 无变化
                //   避免每次 for_each 都 O(n) 扫描 entity_change_tracking_
                if (base_.set_->get_pool_version() == last_pool_version_
                    && last_observed_versions_.size() >= n) [[likely]]
                {
                    needs_rebuild_ = false;
                    return;
                }

                // 仅扩展，不覆盖已追踪的版本号
                while (last_observed_versions_.size() < n)
                    last_observed_versions_.push_back(0);

                // entity_change_tracking_ 是 dense 数组, 顺序访问 + 预取
                const auto* trk = base_.set_->get_entity_change_tracking_data();
                uint64_t* obs = last_observed_versions_.data();
                changed_indices_.reserve_exact(n);
                const size_t n8 = n & ~size_t{7};
                size_t i = 0;
                for (; i < n8; i += 8)
                {
                    if (i + 32 < n) [[likely]] PREFETCH_R(&trk[i + 32]);
                    for (size_t k = 0; k < 8; ++k)
                    {
                        size_t ii = i + k;
                        uint64_t cur = trk[ii].change_version;
                        if (cur != obs[ii])
                        {
                            obs[ii] = cur;
                            changed_indices_.push_back(ii);
                        }
                    }
                }
                for (; i < n; ++i)
                {
                    uint64_t cur = trk[i].change_version;
                    if (cur != obs[i])
                    {
                        obs[i] = cur;
                        changed_indices_.push_back(i);
                    }
                }
                last_pool_version_ = base_.set_->get_pool_version();
                needs_rebuild_ = false;
            }

        public:
            filter_changed_view(single_view base) noexcept : base_(base) { rebuild(); }

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
                base_.resolve_set();
                if (base_.set_ && base_.set_->get_pool_version() != last_pool_version_)
                    needs_rebuild_ = true;
                if (needs_rebuild_) rebuild();
                needs_rebuild_ = true;
                if (changed_indices_.empty()) return;

                T* data = base_.set_->template get_typed_pool_data_ptr<T>();
                if (!data) [[unlikely]] return;
                auto& indices = base_.set_->get_entity_indices();
                auto& versions = base_.set_->get_entity_versions();
                const size_t n = changed_indices_.size();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    // versions_ 已同步时走顺序读, 避免 sparse_entry 随机访问
                    if (versions.size() >= indices.size()) [[likely]]
                    {
                        const uint32_t* idx_data = indices.data();
                        const uint32_t* ver_data = versions.data();
                        const size_t* changed = changed_indices_.data();
                        for (size_t i = 0; i < n; ++i)
                        {
                            size_t idx = changed[i];
                            entity e(idx_data[idx], ver_data[idx]);
                            func(e, data[idx]);
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < n; ++i)
                        {
                            size_t idx = changed_indices_[i];
                            uint32_t eid = indices[idx];
                            uint32_t ver = base_.set_->sparse_version_at_public(eid);
                            entity e(eid, ver);
                            func(e, data[idx]);
                        }
                    }
                }
                else
                {
                    const size_t* changed = changed_indices_.data();
                    for (size_t i = 0; i < n; ++i)
                    {
                        func(data[changed[i]]);
                    }
                }
            }
        };

        auto filter_changed() noexcept
        {
            return filter_changed_view(*this);
        }

        // ======================== single_view::filter_added ========================
        class filter_added_view
        {
        private:
            single_view base_;
            dense<uint64_t> last_observed_added_;
            dense<size_t> added_indices_;
            bool needs_rebuild_{true};
            uint64_t last_pool_version_{0};
            uint64_t baseline_added_counter_{0};

            void rebuild() noexcept
            {
                added_indices_.clear();
                base_.resolve_set();
                if (!base_.set_) [[unlikely]] { needs_rebuild_ = false; return; }
                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] { needs_rebuild_ = false; return; }

                const size_t n = pool->size();
                // 快速路径: pool_version 未变 + 已追踪所有实体 → 无新增
                if (base_.set_->get_pool_version() == last_pool_version_
                    && last_observed_added_.size() >= n) [[likely]]
                {
                    needs_rebuild_ = false;
                    return;
                }

                while (last_observed_added_.size() < n)
                    last_observed_added_.push_back(0);

                // entity_change_tracking_ 顺序访问 + 预取
                const auto* trk = base_.set_->get_entity_change_tracking_data();
                uint64_t* obs = last_observed_added_.data();
                added_indices_.reserve_exact(n);
                const size_t n8 = n & ~size_t{7};
                size_t i = 0;
                for (; i < n8; i += 8)
                {
                    if (i + 32 < n) [[likely]] PREFETCH_R(&trk[i + 32]);
                    for (size_t k = 0; k < 8; ++k)
                    {
                        size_t ii = i + k;
                        uint64_t cur = trk[ii].added_version;
                        if (cur > baseline_added_counter_ && cur != obs[ii])
                        {
                            obs[ii] = cur;
                            added_indices_.push_back(ii);
                        }
                    }
                }
                for (; i < n; ++i)
                {
                    uint64_t cur = trk[i].added_version;
                    if (cur > baseline_added_counter_ && cur != obs[i])
                    {
                        obs[i] = cur;
                        added_indices_.push_back(i);
                    }
                }
                last_pool_version_ = base_.set_->get_pool_version();
                needs_rebuild_ = false;
            }

        public:
            filter_added_view(single_view base) noexcept : base_(base)
            {
                base_.resolve_set();
                if (base_.set_)
                    baseline_added_counter_ = base_.set_->get_global_added_counter();
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
                base_.resolve_set();
                if (base_.set_ && base_.set_->get_pool_version() != last_pool_version_)
                    needs_rebuild_ = true;
                if (needs_rebuild_) rebuild();
                needs_rebuild_ = true;
                if (added_indices_.empty()) return;

                T* data = base_.set_->template get_typed_pool_data_ptr<T>();
                if (!data) [[unlikely]] return;
                auto& indices = base_.set_->get_entity_indices();
                auto& versions = base_.set_->get_entity_versions();
                const size_t n = added_indices_.size();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    if (versions.size() >= indices.size()) [[likely]]
                    {
                        const uint32_t* idx_data = indices.data();
                        const uint32_t* ver_data = versions.data();
                        const size_t* added = added_indices_.data();
                        for (size_t i = 0; i < n; ++i)
                        {
                            size_t idx = added[i];
                            entity e(idx_data[idx], ver_data[idx]);
                            func(e, data[idx]);
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < n; ++i)
                        {
                            size_t idx = added_indices_[i];
                            uint32_t eid = indices[idx];
                            uint32_t ver = base_.set_->sparse_version_at_public(eid);
                            entity e(eid, ver);
                            func(e, data[idx]);
                        }
                    }
                }
                else
                {
                    const size_t* added = added_indices_.data();
                    for (size_t i = 0; i < n; ++i)
                    {
                        func(data[added[i]]);
                    }
                }
            }
        };

        auto filter_added() noexcept
        {
            return filter_added_view(*this);
        }

        // ======================== single_view::exactly_one ========================
        [[nodiscard]] T& exactly_one() noexcept
        {
            resolve_set();
            assert(set_ && set_->size() == 1 && "exactly_one(): expected exactly 1 entity");
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return (*pool)[0];
        }

        [[nodiscard]] const T& exactly_one() const noexcept
        {
            assert(set_ && set_->size() == 1 && "exactly_one(): expected exactly 1 entity");
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return (*pool)[0];
        }
    };
