```c++

//如果对象小于特定大小则在栈上分配，否则在堆上分配。
//Allocate objects on the stack if they are below a certain size; otherwise, allocate them on the heap.
//默认大小=32
//Default size=32
#define SINGLE_OBJECT_STACK_SIZE 64


#include "include/component.hpp"
#include <iostream>

/*
namespace ecs
{
    enum class ecs_option
    {
        //无论创建多少管理对象始终存储在同一块内存上
        //No matter how many management objects are created, they are always stored in the same block of memory.

        On_a_piece_of_memory=1,

        //每个管理器有独立的内存
        //Each manager has its own independent memory

        On_different_memory_blocks=2,
    };
} 

enum class void_any_option
{
    //小对象被存储在栈上
    //Small objects are stored on the stack.

    Enable_stack_memory=1,

    //所有对象都被分配在堆上 
    //All objects are allocated on the heap.

    Absolute_heap_memory=2
};

*/

struct info
{
    std::string name;
    int age;
};



struct pos
{
    int x,y;    
};

int f(int as)
{
    return 1;
}


int main()
{   



    auto ecs = ecs::manager::create(vao::Enable_stack_memory,ecs::ecs_option::On_different_memory_blocks);

    auto entity1=ecs->create_entity();
    auto entity2=ecs->create_entity();  
    auto entity3=ecs->create_entity(); 
    auto entity4=ecs->create_entity(); 

    //单函数运行信息
    //Single function run information
    auto msg1=ecs->add(entity4,pos{15555,2222});


    //所有运行信息（只要有一次失败，就算作“false”。）
    //All running information (As long as there is one failure, it counts as 'false'.)
    auto msg2=ecs->get_operating_message();

    if (msg1||msg2)
    {
        std::cout<<"No error"<<std::endl;
    }
    else
    {
        std::cout<<msg1.read_messge()<<std::endl;
        std::cout<<msg2.read_messge()<<std::endl;
    }

    ecs->add(entity1,pos{15555,2222});
    ecs->add(pos{133,123},entity2);
    ecs->add(entity3,pos{18758,2678});
    
    ecs->addc(entity1,info{"name",10})
        .addc(info{"name",10},entity2)
        .addc(entity3,info{"name",10})
        .addc(entity4,info{"name",10});

    //重复添加将被直接覆盖
    //Duplicate additions will be directly overwritten
    ecs->addc(entity1,pos{55,66})
        .addc(pos{33,44},entity2)
        .addc(entity3,pos{11,12});
    
    //获得某类型容器
    //Obtain a certain type of container
    auto pos_v = ecs->get_component_vector<pos>();

    //获得某类型单一ecs
    //Obtain a single type of ECS
    auto pos_s = ecs->get_single_class_set<pos>();

    auto msg=ecs->get_operating_message();
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
    
    ecs->soft_remove<info>(entity1);

    ecs->soft_removec<info>(entity2)
        .soft_removec<info>(entity3)
        .soft_removec<info>(entity4);

    //删除某类型容器
    //Remove the container instance of type [X].
    ecs->delete_type_container<pos>();
    
    // 删除实体。
    // Delete entity.
    ecs->delete_entitys(entity4);


    std::cout<<"endl"<<std::endl;
    return 0;
}

```



# 回调管理
# Callback Management



``` c++
#include "include/callback_manager.hpp"
#include <iostream>

//event_update_separation eus
//event_message_Non_separation emns



enum class event_a
{
    a,
    b,
    c
};


eus::Callback_manager <event_a>Callback1;



void aaa(int x)
{
    std::cout << "x:" << x << std::endl;
}

int sss(int i, int s)
{
    std::cout << "i:" << i <<" s: "<< s<<std::endl;
    return s;
}



void init()
{
    Callback1.register_callback(event_a::a,function_storage(aaa, "info1", 42));
    Callback1.register_callback(event_a::b,function_storage(sss, "info2", 1,2));
}

static int i = 0;
void envent()
{

    if(i%2==0)
    {
        Callback1.receive_message(event_a::a);
    }
    if(i%5==0)
    {
        Callback1.receive_message(event_a::b);
    }


}
void update()
{
    Callback1.call_function();
    if(!Callback1.get_return_value_v()->empty())
    {
        for(auto &f:*Callback1.get_return_value_v())
        {
            if(f.return_value != nullptr)
            {
                int p = *(int*)f.return_value;
                std::cout << f.function_information << " " << p << std::endl;
            }
        } 
        Callback1.get_return_value_v()->clear();
    }

}

int main()
{   




    bool run = true;


    init();

    while (run)
    {
        ++i;

        envent();
        update();

        if(i>10000)
        {
            run = false;
        }
    }
    
    


    return 0;
}

```
