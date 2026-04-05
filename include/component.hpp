#pragma once
#include <unordered_map>
#include <memory>
#include <concepts> 
#include "single_class_set.hpp"
#include "type_id.hpp"
#include "entity_manager.hpp" 


template <typename T>
concept entitysss = std::same_as<T, entity>;


namespace ecs
{

template <typename... Types>
struct exclude {};

template <typename... Types>
struct get {};

template <typename... Types>
struct ordered {};

class manager
{
private:
    class_pool<Single_class_set> components_c_;
    operating_message component_message;
    entity_manager entity_manager_;
    
public:   
    manager() noexcept {}   
    
    void append_preallocated_entities(size_t count) noexcept
    {
        entity_manager_.append_preallocated_entities(count);
    }
    [[nodiscard]] entity create_entity() noexcept
    {
        return entity_manager_.get_entity();
    }
    [[nodiscard]] operating_message &get_operating_message() noexcept
    {
        return component_message;
    }

    manager(manager&&) = delete;
    manager(manager const&) = delete;
    manager& operator=(manager&&) = delete;
    manager& operator=(manager const&) = delete;

    template <typename T>
    operating_message add(entity entitys,T&& component) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        
        if (type_id >= components_c_.size()) [[unlikely]]
        {
            for (int i = components_c_.size(); i <= type_id; ++i)
            {
                components_c_.emplace_back();
            }
        }
        
        component_message = components_c_[type_id].add(entitys, std::forward<T>(component));
        return component_message;
        
    }
    
    template <typename T>
    operating_message add_batch(std::span<const entity> entities, std::span<const T> components) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        
        if (type_id >= components_c_.size()) [[unlikely]]
        {
            for (int i = components_c_.size(); i <= type_id; ++i)
            {
                components_c_.emplace_back();
            }
        }
        
        component_message = components_c_[type_id].add_batch(entities, components);
        return component_message;
    }
    
    template <typename T>
    operating_message add_batch(const std::vector<entity>& entities, const std::vector<T>& components) noexcept
    {
        return add_batch(std::span<const entity>(entities), std::span<const T>(components));
    }
    
    template <typename T>
    operating_message add_batch(std::vector<entity>&& entities, std::vector<T>&& components) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        
        if (type_id >= components_c_.size()) [[unlikely]]
        {
            for (int i = components_c_.size(); i <= type_id; ++i)
            {
                components_c_.emplace_back();
            }
        }
        
        component_message = components_c_[type_id].add_batch(std::move(entities), std::move(components));
        return component_message;
    }

    template <entitysss ee,typename T>
    operating_message add(T&& component,ee entitys) noexcept
    {
        add(entitys, std::forward<T>(component));      
        return component_message;
    }
    template <entitysss ee,typename T>
    manager &addc(T&& component,ee entitys) noexcept
    {
        add(entitys, std::forward<T>(component));   
        return *this;
    }

    template <typename T>
    manager& addc(entity entitys,T&& component) noexcept
    {
        add(entitys, std::forward<T>(component));
        return *this;
    }
    template <typename T> 
    [[nodiscard]] T *get_ptr(entity entitys) noexcept
    {   
        
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(type_id >= components_c_.size()) [[unlikely]]
        {
            component_message.write_message(0,"error: ", "Object does not exist");
            return nullptr;
        }
        return components_c_[type_id].get_ptr<T>(entitys);

    }

    template <typename T>
    operating_message soft_remove(entity entitys) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(type_id >= components_c_.size()) [[unlikely]]
        {   
            component_message.write_message(0,"error: ", "Object does not exist");
            return component_message;
        }
        component_message=components_c_[type_id].soft_remove(entitys);
        return component_message;

    }

    template <typename T>
    operating_message hard_remove(entity entitys) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(type_id >= components_c_.size()) [[unlikely]]
        {   
            component_message.write_message(0,"error: ", "Object does not exist");
            return component_message;
        }
        component_message=components_c_[type_id].hard_remove(entitys);
        return component_message;
    }

    template <typename T,entitysss ee>
    manager& hard_removec(ee args) noexcept
    {
        hard_remove<T>(args);
        return *this;
    }

    template <typename T,entitysss ee>
    manager& soft_removec(ee args) noexcept
    {
        soft_remove<T>(args);
        return *this;
    }

    template <typename T>
    [[nodiscard]] Single_class_set*get_single_class_set() noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();

        if(type_id >= components_c_.size()) [[unlikely]]
        {
            component_message.write_message(0,"error: ", "Component Set not exist:");
            return nullptr;
        }
        else [[likely]]
        {
            return &components_c_[type_id];
        }

    }

    template <typename T>
    void reserve_component_capacity(size_t capacity) noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(type_id >= components_c_.size()) [[unlikely]]
        {
            for (int i = components_c_.size(); i <= type_id; ++i)
            {
                components_c_.emplace_back();
            }
        }
        components_c_[type_id].reserve(capacity);
    }

    template <typename T>
    [[nodiscard]] class_pool<void_any>* get_component_vector() noexcept
    {         
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(type_id >= components_c_.size()) [[unlikely]]
        {
            component_message.write_message(0,"error: ", "Component does not exist:");
            return nullptr;
        }
        return &components_c_[type_id].get_component_vector();
    }

    template <typename T>
    void delete_type_container() noexcept
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(type_id < components_c_.size()) [[likely]]
        {
            components_c_[type_id].clear();
        }
    }

    // 删除实体。
    // Delete entity.
    void delete_entity(entity &entitys) noexcept
    {
        if(!entitys.is_valid()) [[unlikely]]
        {
            return;
        }
        entity_manager_.destroy_entity(entitys);
    }

    template <typename T, typename... ExcludeTypes>
    class single_view_with_exclude
    {
    private:
        Single_class_set* set_;
        manager* mgr_;

    public:
        single_view_with_exclude(Single_class_set* set, manager* mgr) noexcept : set_(set), mgr_(mgr) {}

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        template <typename Func>
        void each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;

            auto& indices = set_->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], 1);
                bool should_exclude = (... || (mgr_->get_ptr<ExcludeTypes>(e) != nullptr));
                
                if (!should_exclude) [[likely]]
                {
                    if (auto* comp = set_->template get_ptr<T>(e)) [[likely]]
                    {
                        std::forward<Func>(func)(*comp);
                    }
                }
            }
        }

        template <typename Func>
        void use(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;

            auto& indices = set_->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], 1);
                bool should_exclude = (... || (mgr_->get_ptr<ExcludeTypes>(e) != nullptr));
                
                if (!should_exclude) [[likely]]
                {
                    if (auto* comp = set_->template get_ptr<T>(e)) [[likely]]
                    {
                        std::forward<Func>(func)(e, *comp);
                    }
                }
            }
        }
    };

    template <typename T, typename... GetTypes>
    class single_view_with_get
    {
    private:
        Single_class_set* set_;
        manager* mgr_;

    public:
        single_view_with_get(Single_class_set* set, manager* mgr) noexcept : set_(set), mgr_(mgr) {}

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        template <typename Func>
        void each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;

            auto& indices = set_->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], 1);
                if (auto* comp = set_->template get_ptr<T>(e)) [[likely]]
                {
                    auto get_ptrs = std::make_tuple(mgr_->get_ptr<GetTypes>(e)...);
                    std::apply([&](auto*... pts) 
                    {
                        std::forward<Func>(func)(*comp, pts...);
                    }, get_ptrs);
                }
            }
        }

        template <typename Func>
        void use(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;

            auto& indices = set_->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], 1);
                if (auto* comp = set_->template get_ptr<T>(e)) [[likely]]
                {
                    auto get_ptrs = std::make_tuple(mgr_->get_ptr<GetTypes>(e)...);
                    std::apply([&](auto*... pts) {
                        std::forward<Func>(func)(e, *comp, pts...);
                    }, get_ptrs);
                }
            }
        }
    };

    template <typename T>
    class single_view
    {
    private:
        Single_class_set* set_;
        manager* mgr_;

    public:
        single_view(Single_class_set* set, manager* mgr) noexcept : set_(set), mgr_(mgr) {}

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
                return entity(indices[index_], 1);
            }

            iterator& operator++() noexcept
            {
                ++index_;
                return *this;
            }

            [[nodiscard]] bool operator!=(const iterator& other) const noexcept
            {
                return index_ != other.index_;
            }
        };

        [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
        [[nodiscard]] iterator end() noexcept { return iterator(this, set_ ? set_->size() : 0); }

        [[nodiscard]] size_t size() const noexcept { return set_ ? set_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set_ ? set_->empty() : true; }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            if (!set_) [[unlikely]] return false;
            return set_->template get_ptr<T>(e) != nullptr;
        }

        template <typename Func>
        void each(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;

            auto& object_pool = set_->template get_object_pool<T>();
            void_any* begin = object_pool.data();
            void_any* end = begin + object_pool.size();
            
            for (void_any* it = begin; it != end; ++it)
            {
                auto* comp = it->template get_ptr_unchecked<T>();
                std::forward<Func>(func)(*comp);
            }
        }

        template <typename Func>
        void use(Func&& func) noexcept
        {
            if (!set_) [[unlikely]] return;

            auto& indices = set_->get_entity_indices();
            auto& object_pool = set_->template get_object_pool<T>();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], 1);
                auto* comp = object_pool[i].template fast_get_ptr<T>();
                std::forward<Func>(func)(e, *comp);
            }
        }
    };

    template <typename T>
    [[nodiscard]] single_view<T> view() noexcept
    {
        return single_view<T>(get_single_class_set<T>(), this);
    }

    template <typename T, typename Func>
    void view(Func&& func) noexcept
    {
        view<T>().each(std::forward<Func>(func));
    }

    template <typename T1, typename T2>
    class dual_view
    {
    private:
        Single_class_set* set1_;
        Single_class_set* set2_;
        manager* mgr_;

    public:
        dual_view(Single_class_set* set1, Single_class_set* set2, manager* mgr) noexcept 
            : set1_(set1), set2_(set2), mgr_(mgr) {}

        [[nodiscard]] size_t size() const noexcept { return set1_ ? set1_->size() : 0; }
        [[nodiscard]] bool empty() const noexcept { return set1_ ? set1_->empty() : true; }

        [[nodiscard]] bool contains(entity e) const noexcept
        {
            if (!set1_ || !set2_) [[unlikely]] return false;
            return set1_->template get_ptr<T1>(e) != nullptr 
                && set2_->template get_ptr<T2>(e) != nullptr;
        }

        template <typename Func>
        void each(Func&& func) noexcept
        {
            if (!set1_ || !set2_) [[unlikely]] return;

            auto& indices = set1_->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], 1);
                auto* comp1 = set1_->template get_ptr<T1>(e);
                auto* comp2 = set2_->template get_ptr<T2>(e);
                
                if (comp1 && comp2) [[likely]]
                {
                    std::forward<Func>(func)(*comp1, *comp2);
                }
            }
        }

        template <typename Func>
        void use(Func&& func) noexcept
        {
            if (!set1_ || !set2_) [[unlikely]] return;

            auto& indices = set1_->get_entity_indices();
            for (size_t i = 0; i < indices.size(); ++i)
            {
                entity e(indices[i], 1);
                auto* comp1 = set1_->template get_ptr<T1>(e);
                auto* comp2 = set2_->template get_ptr<T2>(e);
                
                if (comp1 && comp2) [[likely]]
                {
                    std::forward<Func>(func)(e, *comp1, *comp2);
                }
            }
        }
    };

    template <typename T1, typename T2>
    [[nodiscard]] dual_view<T1, T2> view() noexcept
    {
        return dual_view<T1, T2>(get_single_class_set<T1>(), get_single_class_set<T2>(), this);
    }

    template <typename T, typename... ExcludeTypes>
    [[nodiscard]] single_view_with_exclude<T, ExcludeTypes...> view(exclude<ExcludeTypes...>) noexcept
    {
        return single_view_with_exclude<T, ExcludeTypes...>(get_single_class_set<T>(), this);
    }

    template <typename T, typename... GetTypes>
    [[nodiscard]] single_view_with_get<T, GetTypes...> view(get<GetTypes...>) noexcept
    {
        return single_view_with_get<T, GetTypes...>(get_single_class_set<T>(), this);
    }

    ~manager()=default;    

};  





}
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               

namespace type
{

    class Component
    {
    private:
        std::unordered_map<size_t,void_any> components_map_;
        std::unordered_map<size_t,std::string>components_name_map_;
        

        template <typename T>
        void add_components(T &&component)
        {
            using DecayedT = std::decay_t<T>;
            size_t type_hs = typeid(DecayedT).hash_code();
            components_map_.insert_or_assign(type_hs, void_any(std::forward<T>(component)));
            components_name_map_.insert_or_assign(type_hs, typeid(DecayedT).name());
        }

        
        
    public: 
        Component(){}
        ~Component()=default;
        template <typename ...args>
        Component &add(args&&... argss) 
        {
            ((add_components(std::forward<args>(argss))), ...);
            return *this;
        }        
        
        template <typename T>
        T* get_ptr()
        {
            using DecayedT = std::decay_t<T>;
            size_t type_hs = typeid(DecayedT).hash_code();
            if(components_map_.contains(type_hs))
            {
                return components_map_[type_hs].get_ptr<T>();
            }
            return nullptr;
        }

        template <typename... Types>
        void remove()
        {
            (([this](){
                using DecayedT = std::decay_t<Types>;
                size_t type_hs = typeid(DecayedT).hash_code();
                this->components_map_.erase(type_hs);
                this->components_name_map_.erase(type_hs);
            })(), ...); 
        }
                
    };
}