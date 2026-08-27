// composite_views.hpp —— manager 类内片段,由 component.hpp 在 manager 类内部 include
// 不要单独 include 此文件
    template <typename T, typename... ExcludeTypes>
    class single_view_without
    {
    private:
        single_class_set* set_;
        manager* mgr_;
        dense<uint64_t> exclude_masks_;
        std::array<single_class_set*, sizeof...(ExcludeTypes)> exclude_sets_{};
        uint32_t max_block_{0};
        bool use_mask_path_{false};


        [[nodiscard]] NOINLINE bool is_excluded(uint32_t idx) const noexcept
        {
            if (use_mask_path_) [[likely]]
            {
                for (uint32_t b = 0; b <= max_block_; ++b)
                {
                    if (exclude_masks_[b] == 0) continue;
                    if ((mgr_->get_entity_manager().get_block(idx, b) & exclude_masks_[b]) != 0)
                        return true;
                }
                return false;
            }
            else
            {
                for (auto* set : exclude_sets_)
                {
                    if (set && set->sparse_dense_at(idx) != single_class_set::dense_invalid)
                        return true;
                }
                return false;
            }
        }

    public:
        single_view_without(single_class_set* set, manager* mgr) noexcept
            : set_(set), mgr_(mgr)
        {
            if (mgr_)
            {
                auto block_of = [](int tid) noexcept -> uint32_t {
                    return static_cast<uint32_t>(tid - 1) / 64;
                };
                max_block_ = 0;
                auto update_block = [&](int tid) noexcept {
                    uint32_t b = block_of(tid);
                    if (b > max_block_) max_block_ = b;
                };
                (update_block(type_id::get_type_id<ExcludeTypes>()), ...);
                exclude_masks_.clear();
                exclude_masks_.increase_capacity(max_block_ + 1, 0);
                auto fill_mask = [&](int tid) noexcept {
                    uint32_t block = block_of(tid);
                    uint32_t offset = static_cast<uint32_t>(tid - 1) % 64;
                    exclude_masks_[block] |= (1ULL << offset);
                };
                (fill_mask(type_id::get_type_id<ExcludeTypes>()), ...);
                if constexpr (sizeof...(ExcludeTypes) > 0)
                {
                    size_t idx = 0;
                    ((exclude_sets_[idx++] = mgr_->template get_single_class_set<ExcludeTypes>()), ...);
                }
                use_mask_path_ = (sizeof...(ExcludeTypes) >= 3) || ((max_block_ + 1) <= 5);
            }
        }

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            if (!set_ || !set_->template get_ptr<T>(e)) return false;
            return !is_excluded(e.parts_.index_);
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
                if (!is_excluded(idx)) return entity(idx, ver);
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
                for (size_t i = 0; i < n; ++i)
                {
                    if (i + 32 < n) [[likely]]
                    {
                        PREFETCH_R(&(*pool)[i + 32]);
                    }
                    uint32_t idx = indices[i];
                    uint32_t ver = set_->sparse_version_at(idx);
                    if (is_excluded(idx)) [[unlikely]] continue;
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
                    if (is_excluded(idx)) [[unlikely]] continue;
                    func((*pool)[i]);
                }
            }
        }
    };

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
            for (size_t i = 0; i < n; ++i)
            {
                if (i + 32 < n) [[likely]]
                {
                    PREFETCH_R(&(*pool)[i + 32]);
                }
                uint32_t idx = indices[i];
                uint32_t ver = set_->sparse_version_at(idx);
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
                    // 直接传递指针到寄存器参数, 避免 std::make_tuple+std::apply 逐实体构造元组
                    // (LTO 全内联后栈读写膨胀 i-cache, 复合视图遍历退化)
                    if constexpr (std::is_invocable_v<Func, entity, T&, GetTypes*...>)
                        func(e, comp, mgr_->template get_ptr_fast<GetTypes>(e)...);
                    else
                        func(comp, mgr_->template get_ptr_fast<GetTypes>(e)...);
                }
            }
        }
    };

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
                    for (size_t i = 0; i < idx_a.size(); ++i)
                    {
                        uint32_t eid = idx_a[i];
                        uint32_t a_ver = set_a_->sparse_version_at(eid);
                        entity e(eid, a_ver);
                        B* b = nullptr;
                        if (pool_b && eid < b_sparse_size)
                        {
                            uint32_t bd = set_b_->sparse_dense_at(eid);
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
                    for (size_t i = 0; i < idx_b.size(); ++i)
                    {
                        uint32_t eid = idx_b[i];
                        if (set_a_ && eid < a_sparse_size)
                        {
                            uint32_t ad = set_a_->sparse_dense_at(eid);
                            if (ad != UINT32_MAX) continue;
                        }
                        uint32_t b_ver = set_b_->sparse_version_at(eid);
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
            dense<uint64_t> visited;
            visited.increase_capacity((max_idx >> 6) + 1, 0);

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

                    // 直接传递指针到寄存器参数, 避免 std::make_tuple+std::apply 逐实体构造元组
                    if constexpr (std::is_invocable_v<decltype(func), entity, type_at<Is>*...>)
                    {
                        func(e, [&]() -> type_at<Is>* {
                            if (Is == set_idx) return nullptr;
                            return sets_[Is] ? sets_[Is]->template get_ptr_fast<type_at<Is>>(e) : nullptr;
                        }()...);
                    }
                    else
                    {
                        func([&]() -> type_at<Is>* {
                            if (Is == set_idx) return nullptr;
                            return sets_[Is] ? sets_[Is]->template get_ptr_fast<type_at<Is>>(e) : nullptr;
                        }()...);
                    }
                }
            }
        }

        template <typename Func>
        void for_each(Func&& func) noexcept
        {
            for_each_impl(std::forward<Func>(func), std::index_sequence_for<Types...>{});
        }
    };

    template <typename T, typename Pred>
    class filter_view
    {
        manager*       mgr_;
        Pred           pred_;
        dense<size_t> filtered_;

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
            const uint32_t dense_idx = set->sparse_dense_at(e.parts_.index_);
            if (dense_idx == single_class_set::dense_invalid) return false;
            if (set->sparse_version_at(e.parts_.index_) != e.parts_.version_) return false;
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
                    uint32_t ver = set->sparse_version_at(idx);
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

    template <typename T, typename B, typename Pred>
    class filter_and_view
    {
        manager*       mgr_;
        Pred           pred_;
        dense<uint64_t> filtered_;

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
            for (size_t i = 0; i < pool_a->size(); ++i)
            {
                if (!pred_((*pool_a)[i])) continue;
                uint32_t eid = indices[i];
                if (eid >= b_sparse_size) continue;
                uint32_t b_dense = set_b->sparse_dense_at(eid);
                if (b_dense != UINT32_MAX)
                {
                    filtered_.push_back((static_cast<uint64_t>(i) << 32) | b_dense);
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
            const uint32_t dense_idx = set_a->sparse_dense_at(e.parts_.index_);
            if (dense_idx == single_class_set::dense_invalid) return false;
            if (set_a->sparse_version_at(e.parts_.index_) != e.parts_.version_) return false;
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

            for (size_t i = 0; i < filtered_.size(); ++i)
            {
                uint64_t packed = filtered_[i];
                size_t a_dense = static_cast<size_t>(packed >> 32);
                size_t b_dense = static_cast<size_t>(packed & 0xFFFFFFFF);
                if constexpr (std::is_invocable_v<Func, entity, T&, B&>)
                {
                    uint32_t idx = indices[a_dense];
                    uint32_t ver = set_a->sparse_version_at(idx);
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

    template <typename T, typename B, typename Pred>
    class filter_or_view
    {
        manager*       mgr_;
        Pred           pred_;
        dense<uint64_t> filtered_;
        dense<size_t> filtered_b_;

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

            for (size_t i = 0; i < pool_a->size(); ++i)
            {
                if (!pred_((*pool_a)[i])) continue;
                uint32_t eid = indices[i];
                uint64_t b_dense = UINT32_MAX;
                if (set_b && eid < b_sparse_size)
                {
                    uint32_t bd = set_b->sparse_dense_at(eid);
                    if (bd != UINT32_MAX) b_dense = bd;
                }
                filtered_.push_back((static_cast<uint64_t>(i) << 32) | b_dense);
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
                        filtered_b_.push_back(i);
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
            const uint32_t dense_idx = set_a->sparse_dense_at(e.parts_.index_);
            if (dense_idx == single_class_set::dense_invalid) return false;
            if (set_a->sparse_version_at(e.parts_.index_) != e.parts_.version_) return false;
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
