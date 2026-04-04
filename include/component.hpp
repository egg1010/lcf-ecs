#pragma once
#include <unordered_map>
#include <memory>
#include <string_view>
#include <concepts> 
#include "single_class_set.hpp"
#include "type_id.hpp"
#include <shared_mutex>
#include "entity_manager.hpp" 


template <typename T>
concept entitysss = std::same_as<T, entity>;


namespace ecs
{


class manager
{
private:
    std::unordered_map<int, Single_class_set> components_map_;
    operating_message component_message;
    entity_manager entity_manager_;
    
public:   
    manager(){}   
    
    void append_preallocated_entities(size_t count)
    {
        entity_manager_.append_preallocated_entities(count);
    }
    entity create_entity()
    {
        return entity_manager_.get_entity();
    }
    operating_message &get_operating_message()
    {
        return component_message;
    }

    manager(manager&&) = delete;
    manager(manager const&) = delete;
    manager& operator=(manager&&) = delete;
    manager& operator=(manager const&) = delete;

    template <typename T>
    operating_message add(entity entitys,T&& component)
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        component_message=components_map_[type_id].add(entitys,std::forward<T>(component));
        return component_message;
        
    }

    template <entitysss ee,typename T>
    operating_message add(T&& component,ee entitys)
    {
        add(entitys, std::forward<T>(component));      
        return component_message;
    }
    template <entitysss ee,typename T>
    manager &addc(T&& component,ee entitys)
    {
        add(entitys, std::forward<T>(component));   
        return *this;
    }

    template <typename T>
    manager& addc(entity entitys,T&& component)
    {
        add(entitys, std::forward<T>(component));
        return *this;
    }
    template <typename T> 
    T *get_ptr(entity entitys)
    {   
        
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(!components_map_.contains(type_id))
        {
            component_message.write_message(0,"error:On_different_memory_blocks ", "Object does not exist");
            return nullptr;
        }
        return components_map_[type_id].get_ptr<T>(entitys);

    }

    template <typename T>
    operating_message soft_remove(entity entitys)
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(!components_map_.contains(type_id))
        {   
            component_message.write_message(0,"error:On_different_memory_blocks ", "Object does not exist");
            return component_message;
        }
        component_message=components_map_[type_id].soft_remove(entitys);
        return component_message;

    }

    template <typename T>
    operating_message hard_remove(entity entitys)
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(!components_map_.contains(type_id))
        {   
            component_message.write_message(0,"error:On_different_memory_blocks ", "Object does not exist");
            return component_message;
        }
        component_message=components_map_[type_id].hard_remove(entitys);
        return component_message;
    }

    template <typename T,entitysss ee>
    manager& hard_removec(ee args)
    {
        hard_remove<T>(args);
        return *this;
    }

    template <typename T,entitysss ee>
    manager& soft_removec(ee args)
    {
        soft_remove<T>(args);
        return *this;
    }

    template <typename T>
    Single_class_set*get_single_class_set()
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();

        if(!components_map_.contains(type_id))
        {
            component_message.write_message(0,"error:On_different_memory_blocks ", "Component Set not exist:");
            return nullptr;
        }
        else
        {
            return &components_map_[type_id];
        }

    }

    template <typename T>
    void reserve_component_capacity(size_t capacity)
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(!components_map_.contains(type_id))
        {

            components_map_.try_emplace(type_id);
            components_map_[type_id].reserve(capacity);
        }
        else
        {

            components_map_[type_id].reserve(capacity);
        }
    }

    template <typename T>
    class_pool<void_any>* get_component_vector()
    {         
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(!components_map_.contains(type_id))
        {
            component_message.write_message(0,"error:On_different_memory_blocks ", "Component does not exist:");
            return nullptr;
        }
        return &components_map_[type_id].get_component_vector();
    }

    template <typename T>
    void delete_type_container()
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        components_map_.erase(type_id);
    }

    // 删除实体。
    // Delete entity.
    void delete_entity(entity &entitys)
    {
        if(!entitys.is_valid())
        {
            return;
        }
        entity_manager_.destroy_entity(entitys);
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