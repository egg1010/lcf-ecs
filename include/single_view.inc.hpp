// single_view.inc.hpp —— manager 类内片段,由 component.hpp 在 manager 类内部 include
// 不要单独 include 此文件
    // ======================== single_view ========================
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
            single_view* view_;
            size_t index_;
        public:
            iterator(single_view* view, size_t index) noexcept : view_(view), index_(index) {}

            [[nodiscard]] entity operator*() const noexcept
            {
                auto& indices = view_->set_->get_entity_indices();
                return entity(indices[index_], view_->set_->get_version_unchecked(indices[index_]));
            }

            iterator& operator++() noexcept { ++index_; return *this; }
            [[nodiscard]] bool operator!=(const iterator& other) const noexcept { return index_ != other.index_; }
        };

        using component_iterator = T*;

        [[nodiscard]] component_iterator component_begin() noexcept
        {
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return pool ? pool->data() : nullptr;
        }
        [[nodiscard]] component_iterator component_end() noexcept
        {
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return pool ? pool->data() + pool->size() : nullptr;
        }

        [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
        [[nodiscard]] iterator end() noexcept { return iterator(this, set_ ? set_->size() : 0); }

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            return set_ && set_->template get_ptr<T>(e) != nullptr;
        }

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            return set_ ? set_->template get_ptr_fast<T>(e) : nullptr;
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
            auto* pool = set_->template get_typed_pool_ptr<T>();
            return pool ? &(*pool)[index] : nullptr;
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
                const size_t n = indices.size();
                const sparse_entry* cur_ver_page = nullptr;
                size_t cur_page_idx = SIZE_MAX;
                for (size_t i = 0; i < n; ++i)
                {
                    uint32_t eid = indices[i];
                    size_t pid = eid >> set_->page_shift;
                    if (pid != cur_page_idx) [[unlikely]]
                    {
                        cur_ver_page = set_->get_version_page(eid);
                        cur_page_idx = pid;
                    }
                    uint32_t ver = 0;
                    if (cur_ver_page) [[likely]]
                        ver = single_class_set::read_version_from_page(cur_ver_page, eid, set_->page_mask);
                    entity e(eid, ver);
                    func(e, (*pool)[i]);
                }
            }
            else
            {
                T* it = pool->data();
                T* end = it + pool->size();
                for (; it != end; ++it)
                    func(*it);
            }
        }

        // ======================== single_view::paged_view ========================
        class paged_view
        {
        private:
            single_view* base_;
            size_t offset_;
            size_t limit_;

        public:
            paged_view(single_view* base, size_t offset, size_t limit) noexcept
                : base_(base), offset_(offset), limit_(limit) {}

            [[nodiscard]] size_t size() const noexcept
            {
                size_t base_sz = base_->size();
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
                base_->for_each([&](auto&... args) {
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
            return paged_view(this, offset, limit);
        }

        // ======================== single_view::sorted_component_view ========================
        template <typename Compare>
        class sorted_component_view
        {
        private:
            static constexpr size_t prefetch_distance_ = sizeof(T) <= 16 ? 32 : (sizeof(T) <= 64 ? 16 : 8);

            single_view* base_;
            Compare cmp_;
            class_pool<size_t> sorted_indices_;
            class_pool<size_t> radix_temp_buf_;
            class_pool<T> sorted_pool_copy_;
            class_pool<entity> sorted_entities_;
            uint64_t last_version_{0};
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                sorted_pool_copy_.clear();
                sorted_entities_.clear();
                if (!base_->set_) [[unlikely]] return;
                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;

                const size_t n = pool->size();
                if (n == 0) [[unlikely]]
                {
                    last_version_ = base_->set_->get_pool_version();
                    needs_rebuild_ = false;
                    return;
                }

                T* pool_data = pool->data();

                sorted_indices_.increase_capacity(n);
                for (size_t i = 0; i < n; ++i)
                    sorted_indices_.emplace_back(i);

                size_t* idx_data = sorted_indices_.data();

                if constexpr (std::is_same_v<std::decay_t<Compare>, std::less<T>>)
                {
                    ecs::tiered_sort_indices<T>(idx_data, pool_data, n);
                }
                else
                {
                    // MinGW+AVX2 下 std::sort+lambda 会崩溃, 使用 ecs::pdqsort 替代
                    ecs::pdqsort<size_t>(idx_data, n,
                        [pool_data, this](size_t a, size_t b) {
                            return cmp_(pool_data[a], pool_data[b]);
                        });
                }

                auto& indices = base_->set_->get_entity_indices();
                sorted_pool_copy_.increase_capacity(n);
                sorted_entities_.increase_capacity(n);
                const sparse_entry* cur_ver_page = nullptr;
                size_t cur_page_idx = SIZE_MAX;
                for (size_t i = 0; i < n; ++i)
                {
                    size_t idx = sorted_indices_[i];
                    sorted_pool_copy_.emplace_back(pool_data[idx]);
                    uint32_t eid = indices[idx];
                    size_t pid = eid >> base_->set_->page_shift;
                    if (pid != cur_page_idx) [[unlikely]]
                    {
                        cur_ver_page = base_->set_->get_version_page(eid);
                        cur_page_idx = pid;
                    }
                    uint32_t ver = 0;
                    if (cur_ver_page) [[likely]]
                        ver = single_class_set::read_version_from_page(cur_ver_page, eid, base_->set_->page_mask);
                    sorted_entities_.emplace_back(entity(eid, ver));
                }

                last_version_ = base_->set_->get_pool_version();
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (base_->set_ && base_->set_->get_pool_version() != last_version_)
                    rebuild();
            }

        public:
            sorted_component_view(single_view* base, Compare cmp) noexcept
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
            return sorted_component_view<Compare>(this, std::forward<Compare>(cmp));
        }

        // ======================== single_view::grouped_component_view ========================
        template <typename KeyType, typename KeyFunc>
        class grouped_component_view
        {
        private:
            single_view* base_;
            KeyFunc key_func_;
            class_pool<size_t> sorted_indices_;
            class_pool<KeyType> group_keys_;
            class_pool<size_t> group_starts_;
            uint64_t last_version_{0};
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                sorted_indices_.clear();
                group_keys_.clear();
                group_starts_.clear();
                if (!base_->set_) [[unlikely]] return;
                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;

                const size_t n = pool->size();
                T* pool_data = pool->data();

                struct sort_entry { KeyType key; size_t index; };
                class_pool<sort_entry> entries;
                entries.resize(n, {});

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
                    // MinGW+AVX2 下 std::sort+lambda 会崩溃, 使用 ecs::tiered_sort 替代
                    ecs::tiered_sort(entries.data(), n, [](const sort_entry& a, const sort_entry& b) {
                        return a.key < b.key;
                    });
                }

                sorted_indices_.resize(n, size_t{0});
                group_keys_.resize(n, KeyType{});
                for (size_t i = 0; i < n; ++i)
                {
                    sorted_indices_[i] = entries[i].index;
                    group_keys_[i] = entries[i].key;
                }

                group_starts_.emplace_back(0);
                for (size_t i = 1; i < n; ++i)
                {
                    if (group_keys_[i] != group_keys_[i - 1])
                        group_starts_.emplace_back(i);
                }

                last_version_ = base_->set_->get_pool_version();
                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (base_->set_ && base_->set_->get_pool_version() != last_version_)
                    rebuild();
            }

        public:
            grouped_component_view(single_view* base, KeyFunc key_func) noexcept
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

                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                auto& indices = base_->set_->get_entity_indices();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    const sparse_entry* g_cur_ver_page = nullptr;
                    size_t g_cur_page_idx = SIZE_MAX;
                    for (size_t i = 0; i < sorted_indices_.size(); ++i)
                    {
                        if (i + 32 < sorted_indices_.size()) [[likely]]
                        {
                            size_t next_idx = sorted_indices_[i + 32];
                            PREFETCH_R(&(*pool)[next_idx]);
                        }
                        size_t idx = sorted_indices_[i];
                        uint32_t eid = indices[idx];
                        size_t pid = eid >> base_->set_->page_shift;
                        if (pid != g_cur_page_idx) [[unlikely]]
                        {
                            g_cur_ver_page = base_->set_->get_version_page(eid);
                            g_cur_page_idx = pid;
                        }
                        uint32_t ver = 0;
                        if (g_cur_ver_page) [[likely]]
                            ver = single_class_set::read_version_from_page(g_cur_ver_page, eid, base_->set_->page_mask);
                        entity e(eid, ver);
                        func(e, (*pool)[idx]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < sorted_indices_.size(); ++i)
                    {
                        if (i + 32 < sorted_indices_.size()) [[likely]]
                            PREFETCH_R(&(*pool)[sorted_indices_[i + 32]]);
                        func((*pool)[sorted_indices_[i]]);
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
            return grouped_component_view<KeyType, KeyFunc>(this, std::forward<KeyFunc>(key_func));
        }

        // ======================== single_view::changed_view ========================
        class changed_view
        {
        private:
            single_view* base_;
            uint64_t last_version_{0};
            class_pool<size_t> changed_indices_;
            bool needs_rebuild_{true};

            void rebuild() noexcept
            {
                changed_indices_.clear();
                base_->resolve_set();
                if (!base_->set_) [[unlikely]]
                {
                    needs_rebuild_ = false;
                    return;
                }

                uint64_t cur = base_->set_->get_pool_version();
                if (cur == last_version_)
                {
                    needs_rebuild_ = false;
                    return;
                }
                last_version_ = cur;

                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                const size_t n = pool->size();
                for (size_t i = 0; i < n; ++i)
                {
                    changed_indices_.emplace_back(i);
                }

                needs_rebuild_ = false;
            }

            void ensure_fresh() noexcept
            {
                if (!needs_rebuild_) return;
                rebuild();
            }

        public:
            changed_view(single_view* base) noexcept : base_(base)
            {
                rebuild();
            }

            [[nodiscard]] size_t size() const noexcept { return changed_indices_.size(); }
            [[nodiscard]] bool empty() const noexcept { return changed_indices_.empty(); }

            void reset_tracking() noexcept
            {
                last_version_ = 0;
                changed_indices_.clear();
                needs_rebuild_ = true;
            }

            template <typename Func>
            void for_each(Func&& func) noexcept
            {
                ensure_fresh();
                needs_rebuild_ = true;
                if (changed_indices_.empty()) return;

                auto* pool = base_->set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                auto& indices = base_->set_->get_entity_indices();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    const sparse_entry* ch_cur_ver_page = nullptr;
                    size_t ch_cur_page_idx = SIZE_MAX;
                    for (size_t i = 0; i < changed_indices_.size(); ++i)
                    {
                        size_t idx = changed_indices_[i];
                        uint32_t eid = indices[idx];
                        size_t pid = eid >> base_->set_->page_shift;
                        if (pid != ch_cur_page_idx) [[unlikely]]
                        {
                            ch_cur_ver_page = base_->set_->get_version_page(eid);
                            ch_cur_page_idx = pid;
                        }
                        uint32_t ver = 0;
                        if (ch_cur_ver_page) [[likely]]
                            ver = single_class_set::read_version_from_page(ch_cur_ver_page, eid, base_->set_->page_mask);
                        entity e(eid, ver);
                        func(e, (*pool)[idx]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < changed_indices_.size(); ++i)
                    {
                        func((*pool)[changed_indices_[i]]);
                    }
                }
            }
        };

        auto track_changes() noexcept
        {
            return changed_view(this);
        }

        // ======================== single_view::filter_changed ========================
        class filter_changed_view
        {
        private:
            single_view base_;
            class_pool<uint64_t> last_observed_versions_;
            class_pool<size_t> changed_indices_;
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
                // 仅扩展，不覆盖已追踪的版本号
                while (last_observed_versions_.size() < n)
                    last_observed_versions_.emplace_back(0);

                for (size_t i = 0; i < n; ++i)
                {
                    uint64_t cur = base_.set_->get_entity_change_version(i);
                    if (i >= last_observed_versions_.size() || cur != last_observed_versions_[i])
                    {
                        if (i < last_observed_versions_.size())
                            last_observed_versions_[i] = cur;
                        else
                            last_observed_versions_.emplace_at(i, cur);
                        changed_indices_.emplace_back(i);
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

                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                auto& indices = base_.set_->get_entity_indices();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    const sparse_entry* fc_cur_ver_page = nullptr;
                    size_t fc_cur_page_idx = SIZE_MAX;
                    for (size_t i = 0; i < changed_indices_.size(); ++i)
                    {
                        size_t idx = changed_indices_[i];
                        uint32_t eid = indices[idx];
                        size_t pid = eid >> base_.set_->page_shift;
                        if (pid != fc_cur_page_idx) [[unlikely]]
                        {
                            fc_cur_ver_page = base_.set_->get_version_page(eid);
                            fc_cur_page_idx = pid;
                        }
                        uint32_t ver = 0;
                        if (fc_cur_ver_page) [[likely]]
                            ver = single_class_set::read_version_from_page(fc_cur_ver_page, eid, base_.set_->page_mask);
                        entity e(eid, ver);
                        func(e, (*pool)[idx]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < changed_indices_.size(); ++i)
                    {
                        func((*pool)[changed_indices_[i]]);
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
            class_pool<uint64_t> last_observed_added_;
            class_pool<size_t> added_indices_;
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
                while (last_observed_added_.size() < n)
                    last_observed_added_.emplace_back(0);

                for (size_t i = 0; i < n; ++i)
                {
                    uint64_t cur = base_.set_->get_entity_added_version(i);
                    if (cur > baseline_added_counter_ && (i >= last_observed_added_.size() || cur != last_observed_added_[i]))
                    {
                        if (i < last_observed_added_.size())
                            last_observed_added_[i] = cur;
                        else
                            last_observed_added_.emplace_at(i, cur);
                        added_indices_.emplace_back(i);
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

                auto* pool = base_.set_->template get_typed_pool_ptr<T>();
                if (!pool) [[unlikely]] return;
                auto& indices = base_.set_->get_entity_indices();

                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    const sparse_entry* fa_cur_ver_page = nullptr;
                    size_t fa_cur_page_idx = SIZE_MAX;
                    for (size_t i = 0; i < added_indices_.size(); ++i)
                    {
                        size_t idx = added_indices_[i];
                        uint32_t eid = indices[idx];
                        size_t pid = eid >> base_.set_->page_shift;
                        if (pid != fa_cur_page_idx) [[unlikely]]
                        {
                            fa_cur_ver_page = base_.set_->get_version_page(eid);
                            fa_cur_page_idx = pid;
                        }
                        uint32_t ver = 0;
                        if (fa_cur_ver_page) [[likely]]
                            ver = single_class_set::read_version_from_page(fa_cur_ver_page, eid, base_.set_->page_mask);
                        entity e(eid, ver);
                        func(e, (*pool)[idx]);
                    }
                }
                else
                {
                    for (size_t i = 0; i < added_indices_.size(); ++i)
                    {
                        func((*pool)[added_indices_[i]]);
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
