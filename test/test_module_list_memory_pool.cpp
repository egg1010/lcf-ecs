// test_module_list_memory_pool.cpp - 模块功能测试: list_memory_pool
#include "test_common.hpp"
#include "include/part/memory/list_memory_pool.hpp"
#include <string>

using memory::list_memory_pool;

struct payload
{
    int a;
    double b;
    std::string name;
};

int main()
{
    t_begin("test_module_list_memory_pool — 模块功能测试: list_memory_pool");

    t_section(1, "基本分配与释放");
    {
        list_memory_pool pool;
        void* p = pool.allocate(64);
        t_check("allocate 非空", p != nullptr);
        pool.deallocate(p);

        void* q = pool.allocate(256);
        t_check("大块 allocate 非空", q != nullptr);
        pool.deallocate(q);
    }

    t_section(2, "allocate_zeroed");
    {
        list_memory_pool pool;
        unsigned char* z = static_cast<unsigned char*>(pool.allocate_zeroed(96));
        t_check("allocate_zeroed 非空", z != nullptr);
        bool all_zero = true;
        for (int i = 0; i < 96; ++i)
        {
            if (z[i] != 0) { all_zero = false; }
        }
        t_check("zeroed 区域全 0", all_zero);
        pool.deallocate(z);
    }

    t_section(3, "construct / destroy");
    {
        list_memory_pool pool;
        payload* o = pool.construct<payload>();
        t_check("construct 非空", o != nullptr);
        if (o)
        {
            o->a = 9;
            o->b = 1.5;
            o->name = "test";
        }
        t_check("construct 赋值", o != nullptr && o->a == 9 && o->name == "test");
        pool.destroy(o);
    }

    t_section(4, "大量小块分配与回收");
    {
        list_memory_pool pool;
        void* ptrs[512];
        bool all_ok = true;
        for (int i = 0; i < 512; ++i)
        {
            ptrs[i] = pool.allocate(32);
            if (!ptrs[i]) { all_ok = false; }
        }
        t_check("512 次分配成功", all_ok);
        t_check("地址互异", ptrs[0] != ptrs[1] && ptrs[100] != ptrs[200]);

        for (int i = 0; i < 512; ++i)
        {
            pool.deallocate(ptrs[i]);
        }
        void* again = pool.allocate(32);
        t_check("回收后可再分配", again != nullptr);
        pool.deallocate(again);
    }

    t_section(5, "reset 释放独立大块");
    {
        list_memory_pool pool;
        void* big = pool.allocate(600000);
        t_check("大块分配非空", big != nullptr);
        pool.reset();
        t_check("reset 后 big_block_count 归零", pool.big_block_count() == 0);
        t_check("reset 后容量回落", pool.total_capacity_bytes() < 600000);
        void* after = pool.allocate(64);
        t_check("reset 后仍可分配", after != nullptr);
    }

    return t_end("list_memory_pool ");
}
