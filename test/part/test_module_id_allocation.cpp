// test_module_id_allocation.cpp - 模块功能测试: id_allocation
#include "test_common.hpp"
#include "include/part/id_.hpp"

int main()
{
    t_begin("test_module_id_allocation — 模块功能测试: id_allocation");

    t_section(1, "递增分配");
    {
        id_allocation<uint32_t> ia;
        uint32_t a = ia.get_id();
        uint32_t b = ia.get_id();
        uint32_t c = ia.get_id();
        t_check("首个 id", a == 1);
        t_check("递增", b == 2 && c == 3);
        t_check("id 互不相同", a != b && b != c);
    }

    t_section(2, "回收与复用");
    {
        id_allocation<uint32_t> ia;
        uint32_t a = ia.get_id();
        uint32_t b = ia.get_id();
        ia.free_id(a);
        t_check("回收计数 == 1", ia.total_number_of_ids() == 1);

        uint32_t reused = ia.get_id();
        t_check("复用回收 id", reused == a);
        t_check("回收计数归零", ia.total_number_of_ids() == 0);
        t_check("maximum_id 不回退", ia.maximum_id() == b);
    }

    t_section(3, "LIFO 复用顺序");
    {
        id_allocation<uint32_t> ia;
        uint32_t a = ia.get_id();
        uint32_t b = ia.get_id();
        ia.free_id(a);
        ia.free_id(b);
        uint32_t r1 = ia.get_id();
        uint32_t r2 = ia.get_id();
        t_check("后进先出", r1 == b && r2 == a);
    }

    t_section(4, "默认类型 size_t");
    {
        id_allocation<> ia;
        size_t a = ia.get_id();
        t_check("默认 size_t 分配", a == 1);
    }

    return t_end("id_allocation ");
}
