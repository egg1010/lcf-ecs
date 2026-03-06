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

    

class global_components_map 
{
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<size_t, Single_class_set> map_;
    global_components_map() = default;
    inline static global_components_map* instance_=nullptr;
public:
    global_components_map(const global_components_map&) = delete;
    global_components_map& operator=(const global_components_map&) = delete;
    global_components_map(global_components_map&&) = delete;
    global_components_map& operator=(global_components_map&&) = delete;
    ~global_components_map()=default;
    static global_components_map &create_g_m()
    {
        static global_components_map instance;
        return instance;
    }

    using iterator = std::unordered_map<size_t, Single_class_set>::iterator;
    using const_iterator = std::unordered_map<size_t, Single_class_set>::const_iterator;

    iterator begin()
    { 
        return map_.begin(); 
    }
    
    iterator end() 
    { 
        return map_.end(); 
    }
    
    const_iterator begin() const 
    { 
        return map_.begin(); 
    }
    
    const_iterator end() const 
    { 
        return map_.end(); 
    }
    
    const_iterator cbegin() const 
    { 
        return map_.cbegin(); 
    }
    
    const_iterator cend() const 
    { 
        return map_.cend(); 
    }


    template<typename Func>
    auto for_each_safe(Func&& func) -> void 
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for(auto &i:map_)
        {
            func(i.first, i.second);
        }
    }

    template<typename Func>
    auto for_each_safe_mutable(Func&& func) -> void 
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for(auto &i:map_)
        {
            func(i.first, i.second);
        }
    }

    auto for_each_safe_mutable_delet_id(entity entitys) -> void 
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        for(auto &i:map_)
        {
            i.second.remove(entitys);
        }
    }


    template<typename T>
    bool contains(T key) const 
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.contains(key);
    }

    template<typename T>
    Single_class_set& operator[](T key) 
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return map_[key];
    }

    template<typename T>
    const Single_class_set& at(T key) const 
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.at(key);
    }

    template<typename K, typename V>
    void insert_or_assign(K&& key, V&& value) 
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        map_.insert_or_assign(std::forward<K>(key), std::forward<V>(value));
    }

    template<typename T>
    size_t erase(T key) 
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return map_.erase(key);
    }

    void clear() 
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        map_.clear();
    }

    // 提供直接访问map的函数（需要外部加锁保证安全）
    //Provides a function to directly access the map (external locking is required to ensure safety)
    template<typename Func>
    auto with_lock(Func&& func) -> decltype(func(std::declval<std::unordered_map<size_t, Single_class_set>&>())) 
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return func(map_);
    }

    template<typename Func>
    auto with_shared_lock(Func&& func) const -> decltype(func(std::declval<const std::unordered_map<size_t, Single_class_set>&>())) 
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return func(map_);
    }
};

//全局ecs
//Global ECS
inline static global_components_map& global_components_map_ = global_components_map::create_g_m();


inline static Single_class_set null_single_class_set{};
enum class ecs_option
{
    On_a_piece_of_memory=1,
    On_different_memory_blocks=2,
};
using oem=ecs_option;



class manager
{
private:
    ecs_option option_=oem::On_a_piece_of_memory;
    std::unordered_map<int, Single_class_set> components_map_;
    operating_message component_message;
    void_any_option memory_option_=vao::Absolute_heap_memory;
    entity_manager entity_manager_;


    manager()=delete;
    manager(void_any_option memory_option=vao::Absolute_heap_memory,ecs_option option=oem::On_a_piece_of_memory)
    {
        option_=option;
        memory_option_=memory_option;
    }   
    
public:   
         
    entity create_entity()
    {
        return entity_manager_.get_entity();
    }
    operating_message &get_operating_message()
    {
        return component_message;
    }
    static std::unique_ptr<manager> create(void_any_option memory_option=vao::Absolute_heap_memory,ecs_option option = oem::On_a_piece_of_memory)
    {
        return std::unique_ptr<manager>(new manager(memory_option, option));
    }
    manager (manager&&)=delete;
    manager(manager const&) = delete;
    manager& operator=(manager&&) = delete;
    manager& operator=(manager const&) = delete;

    template <typename T>
    operating_message add(entity entitys,T&& component)
    {
        


        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(option_==oem::On_a_piece_of_memory)
        {
            component_message=global_components_map_[type_id].add(entitys,std::forward<T>(component),memory_option_);
            return component_message;
        }
        else
        {
            component_message=components_map_[type_id].add(entitys,std::forward<T>(component),memory_option_);
            return component_message;
        }
        
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
        if(option_==oem::On_a_piece_of_memory)
        {
            if(!global_components_map_.contains(type_id))
            {
                component_message.write_message(0,"error:On_a_piece_of_memory ", "Object does not exist");
                return nullptr;
            }
            return global_components_map_[type_id].get_ptr<T>(entitys);
        }
        else
        {
            if(!components_map_.contains(type_id))
            {
                component_message.write_message(0,"error:On_different_memory_blocks ", "Object does not exist");
                return nullptr;
            }
            return components_map_[type_id].get_ptr<T>(entitys);
        }

    }



    template <typename T>
    operating_message remove(entity entitys)
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();

        if(option_==oem::On_a_piece_of_memory)
        {
            if(!global_components_map_.contains(type_id))
            {
                component_message.write_message(0,"error:On_a_piece_of_memory ", "Object does not exist");
                return component_message;
            }
            component_message=global_components_map_[type_id].remove(entitys);
            return component_message;
        }
        else
        {
            if(!components_map_.contains(type_id))
            {   
                component_message.write_message(0,"error:On_different_memory_blocks ", "Object does not exist");
                return component_message;
            }
            component_message=components_map_[type_id].remove(entitys);
            return component_message;
        }
        return component_message;
    }

    template <typename T,entitysss ee>
    manager& removec(ee args)
    {
        remove<T>(args);
        return *this;
    }


    template <typename T>
    Single_class_set*get_single_class_set()
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(option_==oem::On_a_piece_of_memory)
        {
            if(!global_components_map_.contains(type_id))
            {
                component_message.write_message(0,"error:On_a_piece_of_memory ", "Component Set not exist:");
                return nullptr;
            }
            else
            {
                return &global_components_map_[type_id];
            }

        }
        else
        {
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
    }

    template <typename T>
    std::vector <void_any>*get_component_vector()
    {         
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(option_==oem::On_a_piece_of_memory)
        {
            if(!global_components_map_.contains(type_id))
            {
                component_message.write_message(0,"error:On_a_piece_of_memory ", "Component vector not exist:");
                return nullptr; 
            }
            return &global_components_map_[type_id].get_component_vector();
        }
        else
        {
            if(!components_map_.contains(type_id))
            {
                component_message.write_message(0,"error:On_different_memory_blocks ", "Component does not exist:");
                return nullptr;
            }
            return &components_map_[type_id].get_component_vector();
        }
    }

    template <typename T>
    void delete_type_container()
    {
        using DecayedT = std::decay_t<T>;
        int type_id = type_id::get_type_id<DecayedT>();
        if(option_==oem::On_a_piece_of_memory)
        {
            global_components_map_.erase(type_id);
        }
        else
        {
            components_map_.erase(type_id);
        }
    }

    // 删除实体。
    // Delete entity.
    void soft_delete_entitys(entity &entitys)
    {
        if(!entitys.is_valid())
        {
            return;
        }
        entity_manager_.destroy_entity(entitys);
    }


    // 完全删除实体和组件。
    // Completely delete the entity and component.
    void hard_delete_entitys(entity &entitys)
    {
        if(!entitys.is_valid())
        {
            return;
        }
        if(option_==oem::On_a_piece_of_memory)
        {
            global_components_map_.for_each_safe_mutable_delet_id(entitys);
        }
        else
        {
            for(auto &i:components_map_)
            {
                i.second.remove(entitys);
            }
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
        
        void_any_option option_=vao::Absolute_heap_memory;

        template <typename T>
        void add_components(T &&component)
        {
            using DecayedT = std::decay_t<T>;
            size_t type_hs = typeid(DecayedT).hash_code();
            components_map_.insert_or_assign(type_hs, void_any(std::forward<T>(component), option_));
            components_name_map_.insert_or_assign(type_hs, typeid(DecayedT).name());
        }

        Component()=delete;
        
    public: 
        Component(void_any_option options=vao::Absolute_heap_memory) : option_(options) {}
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