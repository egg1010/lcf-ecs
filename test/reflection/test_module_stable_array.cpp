// test_module_stable_array.cpp - 模块功能测试: stable_array
#include "test_common.hpp"
#include "include/part/stable_array.hpp"

struct stable_item
{
    int value;
    int extra;
};

int main()
{
    t_begin("test_module_stable_array — 模块功能测试: stable_array");

    t_section(1, "按需增长");
    {
        stable_array<int, 16> arr;
        t_check("初始为空", arr.size() == 0);
        t_check("ensure 8", arr.ensure(8) && arr.size() == 8);
        t_check("ensure 64", arr.ensure(64) && arr.size() == 64);
        arr[63] = 7;
        t_check("末项可写", arr[63] == 7);
        t_check("ensure 幂等", arr.ensure(64) && arr.size() == 64);
    }

    t_section(2, "扩容后地址稳定");
    {
        stable_array<int, 16> arr;
        t_check("首次分配", arr.ensure(16));
        arr[0] = 42;
        int* first = &arr[0];
        t_check("跨块扩容", arr.ensure(4096));
        t_check("首项地址不变", first == &arr[0]);
        t_check("首项值保留", *first == 42);
        t_check("新块末项可写", (arr[4095] = 9) == 9);
    }

    t_section(3, "元素默认构造");
    {
        stable_array<stable_item, 8> items;
        t_check("预先构造 3 项", items.ensure(3));
        t_check("首项默认零值", items[0].value == 0 && items[0].extra == 0);
        t_check("末项默认零值", items[2].value == 0 && items[2].extra == 0);
        items[2].value = 5;
        t_check("写入生效", items[2].value == 5);
    }

    t_section(4, "clear 后重新增长");
    {
        stable_array<int, 4> arr;
        t_check("首次增长", arr.ensure(12));
        arr[11] = 3;
        arr.clear();
        t_check("clear 后为空", arr.size() == 0);
        t_check("重新增长", arr.ensure(20) && arr.size() == 20);
        t_check("重新构造为零值", arr[11] == 0);
    }

    return t_end("stable_array ");
}
