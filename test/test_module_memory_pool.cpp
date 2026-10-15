// test_module_memory_pool.cpp - 模块功能测试: memory_pool
#include "test_common.hpp"
#include "include/part/memory/memory_pool.hpp"

using memory::memory_pool;

int main()
{
    t_begin("test_module_memory_pool — 模块功能测试: memory_pool");

    t_section(1, "分配与释放");
    {
        memory_pool pool;
        void* p = pool.allocate(64);
        t_check("allocate 非空", p != nullptr);
        pool.deallocate(p);

        void* q = pool.allocate(128);
        t_check("重复 allocate 非空", q != nullptr);
        pool.deallocate(q, 128);
    }

    t_section(2, "清零分配");
    {
        memory_pool pool;
        unsigned char* z = static_cast<unsigned char*>(pool.allocate_zeroed(64));
        t_check("allocate_zeroed 非空", z != nullptr);
        bool all_zero = true;
        for (int i = 0; i < 64; ++i)
        {
            if (z[i] != 0) { all_zero = false; }
        }
        t_check("zeroed 区域全 0", all_zero);
        pool.deallocate(z);
    }

    t_section(3, "多次分配互不重叠");
    {
        memory_pool pool;
        void* a = pool.allocate(32);
        void* b = pool.allocate(32);
        void* c = pool.allocate(32);
        t_check("三次分配均非空", a && b && c);
        t_check("地址互异", a != b && b != c && a != c);
        pool.deallocate(c);
        pool.deallocate(b);
        pool.deallocate(a);
    }

    t_section(4, "大量分配与 clear");
    {
        memory_pool pool(4096);
        void* ptrs[200];
        bool all_ok = true;
        for (int i = 0; i < 200; ++i)
        {
            ptrs[i] = pool.allocate(48);
            if (!ptrs[i]) { all_ok = false; }
        }
        t_check("200 次分配成功", all_ok);
        for (int i = 0; i < 200; ++i)
        {
            pool.deallocate(ptrs[i]);
        }
        void* after = pool.allocate(48);
        t_check("回收后仍可分配", after != nullptr);
    }

    t_section(5, "大小分级");
    {
        memory_pool pool;
        void* small = pool.allocate(8);
        void* large = pool.allocate(2048);
        t_check("小/大分配均成功", small != nullptr && large != nullptr);
        pool.deallocate(large);
        pool.deallocate(small);
    }

    return t_end("memory_pool ");
}
