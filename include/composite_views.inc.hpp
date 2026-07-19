// composite_views.inc.hpp —— manager 类内片段,由 component.hpp 在 manager 类内部 include
// 不要单独 include 此文件
    // ======================== single_view_without ========================
    template <typename T, typename... ExcludeTypes>
    class single_view_without
    {
    private:
        single_class_set* set_;
        manager* mgr_;
        uint64_t exclude_mask_{0};
        std::array<single_class_set*, sizeof...(ExcludeTypes)> exclude_sets_{};
        bool use_mask_path_{true};

        // 双轨:mask 快路径(mask_block==0)+ sparse 交集慢路径(mask_block>0)
        [[nodiscard]] bool is_excluded(uint32_t idx, uint32_t ver) const noexcept
        {
            if (use_mask_path_)
                return (mgr_->get_entity_manager().get_mask(idx) & exclude_mask_) != 0;
            for (size_t k = 0; k < sizeof...(ExcludeTypes); ++k)
            {
                if (single_class_set::sparse_contains_version(exclude_sets_[k], idx, ver))
                    return true;
            }
            return false;
        }

    public:
        single_view_without(single_class_set* set, manager* mgr) noexcept
            : set_(set), mgr_(mgr)
        {
            if (mgr_)
            {
                use_mask_path_ = (... && ((static_cast<uint32_t>(type_id::get_type_id<ExcludeTypes>() - 1) / 64) == 0));
                exclude_mask_ = (... | mgr_->template get_component_bit<ExcludeTypes>());
                if constexpr (sizeof...(ExcludeTypes) > 0)
                {
                    size_t idx = 0;
                    ((exclude_sets_[idx++] = mgr_->template get_single_class_set<ExcludeTypes>()), ...);
                }
            }
        }

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            if (!set_ || !set_->template get_ptr<T>(e)) return false;
            return !is_excluded(e.parts_.index_, e.parts_.version_);
        }

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            return set_ ? set_->template get_ptr_fast<T>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!set_) [[unlikely]] return entity{};
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                uint32_t idx = indices[i];
                uint32_t ver = set_->get_version_unchecked(idx);
                if (!is_excluded(idx, ver)) return entity(idx, ver);
            }
            return entity{};
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return;
            auto& indices = set_->get_entity_indices();
            const size_t n = indices.size();

            if constexpr (std::is_invocable_v<Func, entity, T&>)
            {
                const sparse_entry* cv_cur_ver_page = nullptr;
                size_t cv_cur_page_idx = SIZE_MAX;
                for (size_t i = 0; i < n; ++i)
                {
                    if (i + 32 < n) [[likely]]
                    {
                        PREFETCH_R(&(*pool)[i + 32]);
                    }
                    uint32_t idx = indices[i];
                    size_t pid = idx >> set_->page_shift;
                    if (pid != cv_cur_page_idx) [[unlikely]]
                    {
                        cv_cur_ver_page = set_->get_version_page(idx);
                        cv_cur_page_idx = pid;
                    }
                    uint32_t ver = 0;
                    if (cv_cur_ver_page) [[likely]]
                        ver = single_class_set::read_version_from_page(cv_cur_ver_page, idx, set_->page_mask);
                    if (is_excluded(idx, ver)) [[unlikely]] continue;
                    entity e(idx, ver);
                    func(e, (*pool)[i]);
                }
            }
            else
            {
                for (size_t i = 0; i < n; ++i)
                {
                    if (i + 32 < n) [[likely]]
                        PREFETCH_R(&(*pool)[i + 32]);
                    uint32_t idx = indices[i];
                    uint32_t ver = set_->get_version_unchecked(idx);
                    if (is_excluded(idx, ver)) [[unlikely]] continue;
                    func((*pool)[i]);
                }
            }
        }
    };

    // ======================== single_view_with ========================
    template <typename T, typename... GetTypes>
    class single_view_with
    {
    private:
        single_class_set* set_;
        manager* mgr_;

    public:
        single_view_with(single_class_set* set, manager* mgr) noexcept : set_(set), mgr_(mgr) {}

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

        template <typename U>
        [[nodiscard]] U* get_optional_component_for_entity(entity e) noexcept
        {
            return mgr_ ? mgr_->template get_ptr_fast<U>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!set_ || set_->size() == 0) [[unlikely]] return entity{};
            auto& indices = set_->get_entity_indices();
            return entity(indices[0], set_->get_version_unchecked(indices[0]));
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;
            auto* pool = set_->template get_typed_pool_ptr<T>();
            if (!pool) [[unlikely]] return;
            auto& indices = set_->get_entity_indices();
            const size_t n = indices.size();
            const sparse_entry* cv_cur_ver_page = nullptr;
            size_t cv_cur_page_idx = SIZE_MAX;
            for (size_t i = 0; i < n; ++i)
            {
                if (i + 32 < n) [[likely]]
                {
                    PREFETCH_R(&(*pool)[i + 32]);
                }
                uint32_t idx = indices[i];
                size_t pid = idx >> set_->page_shift;
                if (pid != cv_cur_page_idx) [[unlikely]]
                {
                    cv_cur_ver_page = set_->get_version_page(idx);
                    cv_cur_page_idx = pid;
                }
                uint32_t ver = 0;
                if (cv_cur_ver_page) [[likely]]
                    ver = single_class_set::read_version_from_page(cv_cur_ver_page, idx, set_->page_mask);
                entity e(idx, ver);
                auto& comp = (*pool)[i];
                if constexpr (sizeof...(GetTypes) == 0)
                {
                    if constexpr (std::is_invocable_v<Func, entity, T&>)
                        func(e, comp);
                    else
                        func(comp);
                }
                else
                {
                    auto get_ptrs = std::make_tuple(mgr_->template get_ptr_fast<GetTypes>(e)...);
                    std::apply([&](auto*... pts) {
                        if constexpr (std::is_invocable_v<Func, entity, T&, GetTypes*...>)
                            func(e, comp, pts...);
                        else
                            func(comp, pts...);
                    }, get_ptrs);
                }
            }
        }
    };

    // ======================== or_view ========================
    template <typename A, typename B>
    class or_view
    {
        single_class_set* set_a_;
        single_class_set* set_b_;
    public:
        or_view(single_class_set* a, single_class_set* b) noexcept : set_a_(a), set_b_(b) {}

        [[nodiscard]] size_t size() const noexcept
        {
            size_t s = 0;
            if (set_a_) s += set_a_->size();
            if (set_b_) s += set_b_->size();
            return s;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return (!set_a_ || set_a_->empty()) && (!set_b_ || set_b_->empty());
        }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            return (set_a_ && set_a_->template get_ptr_fast<A>(e) != nullptr)
                || (set_b_ && set_b_->template get_ptr_fast<B>(e) != nullptr);
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (set_a_ && set_a_->size() > 0)
            {
                auto& idx = set_a_->get_entity_indices();
                return entity(idx[0], set_a_->get_version_unchecked(idx[0]));
            }
            if (set_b_ && set_b_->size() > 0)
            {
                auto& idx = set_b_->get_entity_indices();
                return entity(idx[0], set_b_->get_version_unchecked(idx[0]));
            }
            return entity{};
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            if (set_a_)
            {
                auto* pool_a = set_a_->template get_typed_pool_ptr<A>();
                if (pool_a)
                {
                    auto& idx_a = set_a_->get_entity_indices();
                    auto* pool_b = set_b_ ? set_b_->template get_typed_pool_ptr<B>() : nullptr;
                    size_t b_sparse_size = set_b_ ? set_b_->get_sparse_size() : 0;
                    const sparse_entry* cv_a_ver_page = nullptr;
                    size_t cv_a_page_idx = SIZE_MAX;
                    const sparse_entry* cv_b_dense_page = nullptr;
                    size_t cv_b_page_idx = SIZE_MAX;
                    for (size_t i = 0; i < idx_a.size(); ++i)
                    {
                        uint32_t eid = idx_a[i];
                        size_t pid_a = eid >> set_a_->page_shift;
                        if (pid_a != cv_a_page_idx) [[unlikely]]
                        {
                            cv_a_ver_page = set_a_->get_version_page(eid);
                            cv_a_page_idx = pid_a;
                        }
                        uint32_t a_ver = 0;
                        if (cv_a_ver_page) [[likely]]
                            a_ver = single_class_set::read_version_from_page(cv_a_ver_page, eid, set_a_->page_mask);
                        entity e(eid, a_ver);
                        B* b = nullptr;
                        if (pool_b && eid < b_sparse_size)
                        {
                            size_t pid_b = eid >> set_b_->page_shift;
                            if (pid_b != cv_b_page_idx) [[unlikely]]
                            {
                                cv_b_dense_page = set_b_->get_dense_page(eid);
                                cv_b_page_idx = pid_b;
                            }
                            uint32_t bd = UINT32_MAX;
                            if (cv_b_dense_page) [[likely]]
                                bd = single_class_set::read_dense_from_page(cv_b_dense_page, eid, set_b_->page_mask);
                            if (bd != UINT32_MAX) b = &(*pool_b)[bd];
                        }
                        if constexpr (std::is_invocable_v<Func, entity, A*, B*>)
                        {
                            func(e, &(*pool_a)[i], b);
                        }
                        else
                        {
                            func(&(*pool_a)[i], b);
                        }
                    }
                }
            }
            if (set_b_)
            {
                auto* pool_b = set_b_->template get_typed_pool_ptr<B>();
                if (pool_b)
                {
                    auto& idx_b = set_b_->get_entity_indices();
                    size_t a_sparse_size = set_a_ ? set_a_->get_sparse_size() : 0;
                    const sparse_entry* cv_a2_dense_page = nullptr;
                    size_t cv_a2_page_idx = SIZE_MAX;
                    const sparse_entry* cv_b2_ver_page = nullptr;
                    size_t cv_b2_page_idx = SIZE_MAX;
                    for (size_t i = 0; i < idx_b.size(); ++i)
                    {
                        uint32_t eid = idx_b[i];
                        if (set_a_ && eid < a_sparse_size)
                        {
                            size_t pid_a = eid >> set_a_->page_shift;
                            if (pid_a != cv_a2_page_idx) [[unlikely]]
                            {
                                cv_a2_dense_page = set_a_->get_dense_page(eid);
                                cv_a2_page_idx = pid_a;
                            }
                            uint32_t ad = UINT32_MAX;
                            if (cv_a2_dense_page) [[likely]]
                                ad = single_class_set::read_dense_from_page(cv_a2_dense_page, eid, set_a_->page_mask);
                            if (ad != UINT32_MAX) continue;
                        }
                        size_t pid_b = eid >> set_b_->page_shift;
                        if (pid_b != cv_b2_page_idx) [[unlikely]]
                        {
                            cv_b2_ver_page = set_b_->get_version_page(eid);
                            cv_b2_page_idx = pid_b;
                        }
                        uint32_t b_ver = 0;
                        if (cv_b2_ver_page) [[likely]]
                            b_ver = single_class_set::read_version_from_page(cv_b2_ver_page, eid, set_b_->page_mask);
                        entity e(eid, b_ver);
                        if constexpr (std::is_invocable_v<Func, entity, A*, B*>)
                        {
                            func(e, nullptr, &(*pool_b)[i]);
                        }
                        else
                        {
                            func(nullptr, &(*pool_b)[i]);
                        }
                    }
                }
            }
        }
    };

    // ======================== any_of_view ========================
    template <typename... Types>
    class any_of_view
    {
    private:
        static constexpr size_t N = sizeof...(Types);
        std::array<single_class_set*, N> sets_;

        template <size_t I>
        using type_at = std::tuple_element_t<I, std::tuple<Types...>>;

        [[nodiscard]] size_t max_entity_index() const noexcept
        {
            size_t max_idx = 0;
            for (auto* s : sets_)
            {
                if (s)
                {
                    size_t sz = s->get_sparse_size();
                    if (sz > max_idx) max_idx = sz;
                }
            }
            return max_idx;
        }

    public:
        any_of_view(std::array<single_class_set*, N> s) noexcept : sets_(s) {}

        [[nodiscard]] size_t size() const noexcept
        {
            size_t total = 0;
            for (auto* s : sets_) if (s) total += s->size();
            return total;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            for (auto* s : sets_) if (s && s->size() > 0) return false;
            return true;
        }

        template <size_t... Is>
        void for_each_impl(auto&& func, std::index_sequence<Is...>) noexcept
        {
            size_t max_idx = max_entity_index();
            if (max_idx == 0) return;
            class_pool<uint64_t> visited;
            visited.resize((max_idx >> 6) + 1, 0);

            for (size_t set_idx = 0; set_idx < N; ++set_idx)
            {
                auto* set = sets_[set_idx];
                if (!set) continue;
                auto& indices = set->get_entity_indices();
                const size_t n = indices.size();

                for (size_t i = 0; i < n; ++i)
                {
                    uint32_t idx = indices[i];
                    size_t word = idx >> 6;
                    uint64_t bit = uint64_t{1} << (idx & 63);
                    if (visited[word] & bit) continue;
                    visited[word] |= bit;

                    entity e(idx, set->get_version_unchecked(idx));

                    auto comp_ptrs = std::make_tuple(
                        [&]() -> type_at<Is>* {
                            if (Is == set_idx) return nullptr;
                            return sets_[Is] ? sets_[Is]->template get_ptr_fast<type_at<Is>>(e) : nullptr;
                        }()...
                    );

                    std::apply([&](auto*... ptrs) {
                        if constexpr (std::is_invocable_v<decltype(func), entity, type_at<Is>*...>)
                            func(e, ptrs...);
                        else
                            func(ptrs...);
                    }, comp_ptrs);
                }
            }
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            for_each_impl(std::forward<Func>(func), std::index_sequence_for<Types...>{});
        }
    };

    // ======================== filter_view ========================
    template <typename T, typename Pred>
    class filter_view
    {
        manager*       mgr_;
        Pred           pred_;
        class_pool<size_t> filtered_;

        void rebuild_impl() noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return;
            auto* pool = set->template get_typed_pool_ptr<T>();
            if (!pool) return;
            size_t pool_size = pool->size();
            filtered_.clear();
            filtered_.increase_capacity(pool_size);
            for (size_t i = 0; i < pool_size; ++i)
            {
                if (pred_((*pool)[i]))
                    filtered_.push_back_unchecked(i);
            }
        }

    public:
        filter_view(manager* mgr, Pred&& pred) noexcept
            : mgr_(mgr), pred_(std::forward<Pred>(pred))
        {
            rebuild_impl();
        }

        void rebuild() noexcept { rebuild_impl(); }

        [[nodiscard]] size_t size() const noexcept { return filtered_.size(); }
        [[nodiscard]] bool   empty() const noexcept { return filtered_.empty(); }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return false;
            auto* pool = set->template get_typed_pool_ptr<T>();
            if (!pool) return false;
            if (e.parts_.index_ >= set->get_sparse_size()) return false;
            const uint32_t dense_idx = set->sparse_dense_at_public(e.parts_.index_);
            if (dense_idx == single_class_set::dense_invalid) return false;
            if (set->sparse_version_at_public(e.parts_.index_) != e.parts_.version_) return false;
            for (size_t i = 0; i < filtered_.size(); ++i)
            {
                if (filtered_[i] == dense_idx) return pred_((*pool)[dense_idx]);
            }
            return false;
        }

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            return set ? set->template get_ptr_fast<T>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (filtered_.empty()) [[unlikely]] return entity{};
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return entity{};
            auto& indices = set->get_entity_indices();
            size_t dense_idx = filtered_[0];
            return entity(indices[dense_idx], set->get_version_unchecked(indices[dense_idx]));
        }

        [[nodiscard]] entity get_entity_at_index(size_t index) const noexcept
        {
            if (index >= filtered_.size()) [[unlikely]] return entity{};
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return entity{};
            auto& indices = set->get_entity_indices();
            size_t dense_idx = filtered_[index];
            return entity(indices[dense_idx], set->get_version_unchecked(indices[dense_idx]));
        }

        [[nodiscard]] T* get_component_at_index(size_t index) noexcept
        {
            if (index >= filtered_.size()) [[unlikely]] return nullptr;
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return nullptr;
            auto* pool = set->template get_typed_pool_ptr<T>();
            return pool ? &(*pool)[filtered_[index]] : nullptr;
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            if (!set) return;
            auto* pool = set->template get_typed_pool_ptr<T>();
            auto& indices = set->get_entity_indices();
            size_t n = filtered_.size();
            size_t* filtered_data = filtered_.data();
            const sparse_entry* cv_cur_ver_page = nullptr;
            size_t cv_cur_page_idx = SIZE_MAX;

            for (size_t i = 0; i < n; ++i)
            {
                if (i + 32 < n) [[likely]]
                {
                    size_t next_idx = filtered_data[i + 32];
                    PREFETCH_R(&(*pool)[next_idx]);
                }
                size_t dense_index = filtered_data[i];
                if constexpr (std::is_invocable_v<Func, entity, T&>)
                {
                    uint32_t idx = indices[dense_index];
                    size_t pid = idx >> set->page_shift;
                    if (pid != cv_cur_page_idx) [[unlikely]]
                    {
                        cv_cur_ver_page = set->get_version_page(idx);
                        cv_cur_page_idx = pid;
                    }
                    uint32_t ver = 0;
                    if (cv_cur_ver_page) [[likely]]
                        ver = single_class_set::read_version_from_page(cv_cur_ver_page, idx, set->page_mask);
                    entity e(idx, ver);
                    func(e, (*pool)[dense_index]);
                }
                else
                {
                    func((*pool)[dense_index]);
                }
            }
        }

        template <typename B> auto and_() noexcept;
        template <typename B> auto or_() noexcept;
    };

    // ======================== filter_and_view ========================
    template <typename T, typename B, typename Pred>
    class filter_and_view
    {
        manager*       mgr_;
        Pred           pred_;
        class_pool<uint64_t> filtered_;

        void rebuild_impl() noexcept
        {
            filtered_.clear();
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a || !set_b) return;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            auto* pool_b = set_b->template get_typed_pool_ptr<B>();
            if (!pool_a || !pool_b) return;
            auto& indices = set_a->get_entity_indices();
            size_t b_sparse_size = set_b->get_sparse_size();
            const sparse_entry* cv_b_dense_page = nullptr;
            size_t cv_b_page_idx = SIZE_MAX;
            for (size_t i = 0; i < pool_a->size(); ++i)
            {
                if (!pred_((*pool_a)[i])) continue;
                uint32_t eid = indices[i];
                if (eid >= b_sparse_size) continue;
                size_t pid = eid >> set_b->page_shift;
                if (pid != cv_b_page_idx) [[unlikely]]
                {
                    cv_b_dense_page = set_b->get_dense_page(eid);
                    cv_b_page_idx = pid;
                }
                uint32_t b_dense = UINT32_MAX;
                if (cv_b_dense_page) [[likely]]
                    b_dense = single_class_set::read_dense_from_page(cv_b_dense_page, eid, set_b->page_mask);
                if (b_dense != UINT32_MAX)
                {
                    filtered_.emplace_back((static_cast<uint64_t>(i) << 32) | b_dense);
                }
            }
        }

    public:
        filter_and_view(manager* mgr, Pred&& pred) noexcept
            : mgr_(mgr), pred_(std::forward<Pred>(pred))
        {
            rebuild_impl();
        }

        void rebuild() noexcept { rebuild_impl(); }

        [[nodiscard]] size_t size() const noexcept { return filtered_.size(); }
        [[nodiscard]] bool   empty() const noexcept { return filtered_.empty(); }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a || !set_b) return false;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            if (!pool_a) return false;
            if (e.parts_.index_ >= set_a->get_sparse_size()) return false;
            const uint32_t dense_idx = set_a->sparse_dense_at_public(e.parts_.index_);
            if (dense_idx == single_class_set::dense_invalid) return false;
            if (set_a->sparse_version_at_public(e.parts_.index_) != e.parts_.version_) return false;
            if (!pred_((*pool_a)[dense_idx])) return false;
            if (!set_b->template get_ptr_fast<B>(e)) return false;
            for (size_t i = 0; i < filtered_.size(); ++i)
            {
                if (static_cast<size_t>(filtered_[i] >> 32) == dense_idx) return true;
            }
            return false;
        }

        [[nodiscard]] T* get_component_for_entity(entity e) noexcept
        {
            auto* set = mgr_->template get_single_class_set<T>();
            return set ? set->template get_ptr_fast<T>(e) : nullptr;
        }

        [[nodiscard]] B* get_optional_component_for_entity(entity e) noexcept
        {
            auto* set = mgr_->template get_single_class_set<B>();
            return set ? set->template get_ptr_fast<B>(e) : nullptr;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (filtered_.empty()) [[unlikely]] return entity{};
            auto* set_a = mgr_->template get_single_class_set<T>();
            if (!set_a) return entity{};
            auto& indices = set_a->get_entity_indices();
            size_t dense_idx = static_cast<size_t>(filtered_[0] >> 32);
            return entity(indices[dense_idx], set_a->get_version_unchecked(indices[dense_idx]));
        }

        [[nodiscard]] entity get_entity_at_index(size_t index) const noexcept
        {
            if (index >= filtered_.size()) [[unlikely]] return entity{};
            auto* set_a = mgr_->template get_single_class_set<T>();
            if (!set_a) return entity{};
            auto& indices = set_a->get_entity_indices();
            size_t dense_idx = static_cast<size_t>(filtered_[index] >> 32);
            return entity(indices[dense_idx], set_a->get_version_unchecked(indices[dense_idx]));
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a || !set_b) return;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            auto* pool_b = set_b->template get_typed_pool_ptr<B>();
            if (!pool_a || !pool_b) return;
            auto& indices = set_a->get_entity_indices();
            const sparse_entry* cv_cur_ver_page = nullptr;
            size_t cv_cur_page_idx = SIZE_MAX;

            for (size_t i = 0; i < filtered_.size(); ++i)
            {
                uint64_t packed = filtered_[i];
                size_t a_dense = static_cast<size_t>(packed >> 32);
                size_t b_dense = static_cast<size_t>(packed & 0xFFFFFFFF);
                if constexpr (std::is_invocable_v<Func, entity, T&, B&>)
                {
                    uint32_t idx = indices[a_dense];
                    size_t pid = idx >> set_a->page_shift;
                    if (pid != cv_cur_page_idx) [[unlikely]]
                    {
                        cv_cur_ver_page = set_a->get_version_page(idx);
                        cv_cur_page_idx = pid;
                    }
                    uint32_t ver = 0;
                    if (cv_cur_ver_page) [[likely]]
                        ver = single_class_set::read_version_from_page(cv_cur_ver_page, idx, set_a->page_mask);
                    entity e(idx, ver);
                    func(e, (*pool_a)[a_dense], (*pool_b)[b_dense]);
                }
                else
                {
                    func((*pool_a)[a_dense], (*pool_b)[b_dense]);
                }
            }
        }
    };

    // ======================== filter_or_view ========================
    template <typename T, typename B, typename Pred>
    class filter_or_view
    {
        manager*       mgr_;
        Pred           pred_;
        class_pool<uint64_t> filtered_;
        class_pool<size_t> filtered_b_;

        void rebuild_impl() noexcept
        {
            filtered_.clear();
            filtered_b_.clear();
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a) return;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            if (!pool_a) return;
            auto& indices = set_a->get_entity_indices();
            size_t b_sparse_size = set_b ? set_b->get_sparse_size() : 0;
            const sparse_entry* cv_b_dense_page = nullptr;
            size_t cv_b_page_idx = SIZE_MAX;

            for (size_t i = 0; i < pool_a->size(); ++i)
            {
                if (!pred_((*pool_a)[i])) continue;
                uint32_t eid = indices[i];
                uint64_t b_dense = UINT32_MAX;
                if (set_b && eid < b_sparse_size)
                {
                    size_t pid = eid >> set_b->page_shift;
                    if (pid != cv_b_page_idx) [[unlikely]]
                    {
                        cv_b_dense_page = set_b->get_dense_page(eid);
                        cv_b_page_idx = pid;
                    }
                    uint32_t bd = UINT32_MAX;
                    if (cv_b_dense_page) [[likely]]
                        bd = single_class_set::read_dense_from_page(cv_b_dense_page, eid, set_b->page_mask);
                    if (bd != UINT32_MAX) b_dense = bd;
                }
                filtered_.emplace_back((static_cast<uint64_t>(i) << 32) | b_dense);
            }

            if (set_b)
            {
                auto* pool_b = set_b->template get_typed_pool_ptr<B>();
                if (pool_b)
                {
                    auto& idx_b = set_b->get_entity_indices();
                    for (size_t i = 0; i < idx_b.size(); ++i)
                    {
                        entity e(idx_b[i], set_b->get_version_unchecked(idx_b[i]));
                        if (set_a)
                        {
                            T* a = set_a->template get_ptr_fast<T>(e);
                            if (a && pred_(*a)) continue;
                        }
                        filtered_b_.emplace_back(i);
                    }
                }
            }
        }

    public:
        filter_or_view(manager* mgr, Pred&& pred) noexcept
            : mgr_(mgr), pred_(std::forward<Pred>(pred))
        {
            rebuild_impl();
        }

        void rebuild() noexcept { rebuild_impl(); }

        [[nodiscard]] size_t size() const noexcept { return filtered_.size() + filtered_b_.size(); }
        [[nodiscard]] bool   empty() const noexcept { return filtered_.empty() && filtered_b_.empty(); }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();
            if (!set_a) return false;
            auto* pool_a = set_a->template get_typed_pool_ptr<T>();
            if (!pool_a) return false;
            if (e.parts_.index_ >= set_a->get_sparse_size()) return false;
            const uint32_t dense_idx = set_a->sparse_dense_at_public(e.parts_.index_);
            if (dense_idx == single_class_set::dense_invalid) return false;
            if (set_a->sparse_version_at_public(e.parts_.index_) != e.parts_.version_) return false;
            if (pred_((*pool_a)[dense_idx]))
            {
                for (size_t i = 0; i < filtered_.size(); ++i)
                {
                    if (static_cast<size_t>(filtered_[i] >> 32) == dense_idx) return true;
                }
            }
            if (set_b && set_b->template get_ptr_fast<B>(e)) return true;
            return false;
        }

        [[nodiscard]] entity get_first_entity() const noexcept
        {
            if (!filtered_.empty())
            {
                auto* set_a = mgr_->template get_single_class_set<T>();
                if (set_a)
                {
                    auto& indices = set_a->get_entity_indices();
                    size_t dense_idx = static_cast<size_t>(filtered_[0] >> 32);
                    return entity(indices[dense_idx], set_a->get_version_unchecked(indices[dense_idx]));
                }
            }
            if (!filtered_b_.empty())
            {
                auto* set_b = mgr_->template get_single_class_set<B>();
                if (set_b)
                {
                    auto& idx = set_b->get_entity_indices();
                    return entity(idx[filtered_b_[0]], set_b->get_version_unchecked(idx[filtered_b_[0]]));
                }
            }
            return entity{};
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            auto* set_a = mgr_->template get_single_class_set<T>();
            auto* set_b = mgr_->template get_single_class_set<B>();

            if (set_a && !filtered_.empty())
            {
                auto* pool_a = set_a->template get_typed_pool_ptr<T>();
                if (pool_a)
                {
                    auto& idx_a = set_a->get_entity_indices();
                    for (size_t i = 0; i < filtered_.size(); ++i)
                    {
                        uint64_t packed = filtered_[i];
                        size_t a_dense = static_cast<size_t>(packed >> 32);
                        size_t b_dense = static_cast<size_t>(packed & 0xFFFFFFFF);
                        entity e(idx_a[a_dense], set_a->get_version_unchecked(idx_a[a_dense]));
                        B* b = (b_dense != UINT32_MAX && set_b) ? &(*set_b->template get_typed_pool_ptr<B>())[b_dense] : nullptr;
                        if constexpr (std::is_invocable_v<Func, entity, T*, B*>)
                        {
                            func(e, &(*pool_a)[a_dense], b);
                        }
                        else
                        {
                            func(&(*pool_a)[a_dense], b);
                        }
                    }
                }
            }

            if (set_b && !filtered_b_.empty())
            {
                auto* pool_b = set_b->template get_typed_pool_ptr<B>();
                if (pool_b)
                {
                    auto& idx_b = set_b->get_entity_indices();
                    for (size_t i = 0; i < filtered_b_.size(); ++i)
                    {
                        size_t b_dense = filtered_b_[i];
                        entity e(idx_b[b_dense], set_b->get_version_unchecked(idx_b[b_dense]));
                        if constexpr (std::is_invocable_v<Func, entity, T*, B*>)
                        {
                            func(e, nullptr, &(*pool_b)[b_dense]);
                        }
                        else
                        {
                            func(nullptr, &(*pool_b)[b_dense]);
                        }
                    }
                }
            }
        }
    };
