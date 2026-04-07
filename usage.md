```c++

#include "include/component.hpp"
#include <iostream>



struct info
{
    std::string name;
    int age;
};



struct pos
{
    int x,y;    
};

#include <functional>

struct CallbackComponent
{
    std::function<void(int)> callback;
    
    CallbackComponent(std::function<void(int)> cb) : callback(std::move(cb)) {}
};

int f(int as)
{
    return 1;
}


int main()
{   


    ecs::manager ecss;

    //提前创建实体 默认创建500*1000
    //Create entity in advance.The default number of entities created is 500*1000
    ecss.append_preallocated_entities(1000*1000);

    auto entity1=ecss.create_entity();
    auto entity2=ecss.create_entity();  
    auto entity3=ecss.create_entity(); 
    auto entity4=ecss.create_entity(); 

    //获取运行信息
    //Get running information
    auto msg1=ecss.add(entity4,pos{15555,2222});


    //所有运行信息（只要有一次失败，就算作“false”。）
    //All running information (As long as there is one failure, it counts as 'false'.)
    auto msg2=ecss.get_operating_message();

    if (msg1||msg2)
    {
        std::cout<<"No error"<<std::endl;
    }
    else
    {
        std::cout<<msg1.read_messge()<<std::endl;
        std::cout<<msg2.read_messge()<<std::endl;
    }

    ecss.add(entity1,pos{15555,2222});
    ecss.add(pos{133,123},entity2);
    ecss.add(entity3,pos{18758,2678});
    
    ecss.addc(entity1,info{"name",10})
        .addc(info{"name",10},entity2)
        .addc(entity3,info{"name",10})
        .addc(entity4,info{"name",10});

    //重复添加将被直接覆盖
    //Duplicate additions will be directly overwritten
    ecss.addc(entity1,pos{55,66})
        .addc(pos{33,44},entity2)
        .addc(entity3,pos{11,12});
    
    //获得某类型容器
    //Obtain a certain type of container
    auto pos_v = ecss.get_component_vector<pos>();

    //获得某类型单一ecs
    //Obtain a single type of ECS
    auto pos_s = ecss.get_single_class_set<pos>();

    auto msg=ecss.get_operating_message();
    if(msg)
    {
        std::cout<<"No error"<<std::endl;
    }
    else
    {
        std::cout<<msg.read_messge()<<std::endl;
    }
    
    for (auto &poss : *pos_v)
    {
        auto pos1 = poss.get_ptr<pos>();
        if(pos1!=nullptr)
        {
            std::cout<<pos1->x<<" "<<pos1->y<<std::endl;
        }
        else
        {
            std::cout<<"nullptr"<<std::endl;
        }

        poss=pos{0,0};
    }


    std::cout<<"get_single_class_set"<<std::endl;

    pos_s->add(entity3,pos{15555,2222});

    auto pos2=pos_s->get_ptr<pos>(entity1);

    if(pos2!=nullptr)
    {
        std::cout<<pos2->x<<" "<<pos2->y<<std::endl;
    }
    else
    {
        std::cout<<"nullptr"<<std::endl;
    }

    for (auto &poss : *pos_s)
    {
        auto pos1 = poss.get_ptr<pos>();
        if(pos1!=nullptr)
        {
            std::cout<<pos1->x<<" "<<pos1->y<<std::endl;
        }
        else
        {
            std::cout<<"nullptr"<<std::endl;
        }
        
    }

    // --- EnTT 风格视图系统 ---
    // --- EnTT style view system ---

    std::cout << "\n--- View (EnTT style) ---" << std::endl;
    // 获取单组件视图
    // Get single-component view
    auto pos_view = ecss.view<pos>();
    std::cout << "pos_view.size(): " << pos_view.size() << std::endl;
    std::cout << "pos_view.empty(): " << (pos_view.empty() ? "true" : "false") << std::endl;

    std::cout << "\n--- Range-based for loop ---" << std::endl;
    // 使用范围 for 循环遍历视图中的所有实体
    // Use range-based for loop to iterate all entities in the view
    for (auto entity : pos_view)
    {
        std::cout << "Entity: " << entity.index_ << std::endl;
    }

    std::cout << "\n--- each() ---" << std::endl;
    // 使用 each() 遍历组件（不需要 entity）
    // Use each() to iterate components (no entity needed)
    pos_view.each([](pos& p) {
        std::cout << "pos: " << p.x << " " << p.y << std::endl;
    });

    std::cout << "\n--- use() (with entity) ---" << std::endl;
    // 使用 use() 同时访问 entity 和组件
    // Use use() to access both entity and component
    pos_view.use([](entity e, pos& p) {
        std::cout << "Entity " << e.index_ << ": " << p.x << " " << p.y << std::endl;
    });

    std::cout << "\n--- contains() ---" << std::endl;
    // 检查 entity 是否在视图中
    // Check if entity is in the view
    std::cout << "pos_view.contains(entity1): " << (pos_view.contains(entity1) ? "true" : "false") << std::endl;
    std::cout << "pos_view.contains(entity2): " << (pos_view.contains(entity2) ? "true" : "false") << std::endl;

    std::cout << "\n--- Dual View (pos + info) ---" << std::endl;
    // 获取双组件视图（交集：同时拥有 pos 和 info 的实体）
    // Get dual-component view (intersection: entities with both pos and info)
    auto dual_view = ecss.view<pos, info>();
    std::cout << "dual_view.size(): " << dual_view.size() << std::endl;
    std::cout << "dual_view.empty(): " << (dual_view.empty() ? "true" : "false") << std::endl;

    std::cout << "\n--- Dual View each() ---" << std::endl;
    // 使用 each() 遍历两个组件
    // Use each() to iterate both components
    dual_view.each([](pos& p, info& i) {
        std::cout << "pos: " << p.x << " " << p.y << ", info: " << i.name << " " << i.age << std::endl;
    });

    std::cout << "\n--- Dual View use() ---" << std::endl;
    // 使用 use() 同时访问 entity 和两个组件
    // Use use() to access entity and both components
    dual_view.use([](entity e, pos& p, info& i) {
        std::cout << "Entity " << e.index_ << ": pos(" << p.x << "," << p.y << "), info(" << i.name << "," << i.age << ")" << std::endl;
    });

    std::cout << "\n--- View with exclude<info> ---" << std::endl;
    // 排除拥有 info 组件的实体（只遍历有 pos 但没有 info 的实体）
    // Exclude entities with info component (only iterate entities with pos but no info)
    auto view_exclude = ecss.view<pos>(ecs::exclude<info>{});
    view_exclude.each([](pos& p) {
        std::cout << "pos (no info): " << p.x << " " << p.y << std::endl;
    });

    std::cout << "\n--- View with get<info> ---" << std::endl;
    // 获取可选组件（info 是可选的，可能为 nullptr）
    // Get optional component (info is optional, may be nullptr)
    auto view_get = ecss.view<pos>(ecs::get<info>{});
    view_get.each([](pos& p, info* i) {
        std::cout << "pos: " << p.x << " " << p.y;
        if (i) std::cout << ", info: " << i->name << " " << i->age;
        std::cout << std::endl;
    });
    
    ecss.soft_remove<info>(entity1);

    ecss.soft_removec<info>(entity2)
        .soft_removec<info>(entity3)
        .soft_removec<info>(entity4);

    //删除某类型容器
    //Remove the container instance of type [X].
    ecss.delete_type_container<pos>();
    
    // 删除实体。
    // Delete entity.
    ecss.delete_entity(entity4);


    std::cout << "\n--- Test Function Storage ---" << std::endl;
    std::cout << "--- Testing function storage ---" << std::endl;
    
    auto entity_cb = ecss.create_entity();
    
    // 添加 lambda 函数作为组件
    ecss.add(entity_cb, CallbackComponent([](int x) {
        std::cout << "Lambda called with x=" << x << std::endl;
    }));
    
    // 获取并调用
    auto* cb_comp = ecss.get_ptr<CallbackComponent>(entity_cb);
    if (cb_comp)
    {
        std::cout << "Calling lambda callback..." << std::endl;
        cb_comp->callback(42);
    }
    
    std::cout<<"endl"<<std::endl;
    return 0;
}

```
