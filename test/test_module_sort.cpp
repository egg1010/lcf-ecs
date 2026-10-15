// test_module_sort.cpp - 模块功能测试: tiered_sort / pdqsort
#include "test_common.hpp"
#include "include/part/tiered_sort.hpp"
#include "include/part/dense.hpp"

static bool is_ascending(const dense<int>& v)
{
    for (size_t i = 1; i < v.size(); ++i)
    {
        if (v[i - 1] > v[i]) { return false; }
    }
    return true;
}

int main()
{
    t_begin("test_module_sort — 模块功能测试: 分级排序");

    t_section(1, "sort 升序");
    {
        dense<int> v{5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
        sort(v.data(), v.size());
        t_check("sort 升序", is_ascending(v));
        t_check("首尾元素", v.front() == 0 && v.back() == 9);
    }

    t_section(2, "自定义比较器");
    {
        dense<int> v{5, 3, 8, 1, 9, 2};
        sort(v.data(), v.size(), [](int a, int b) { return a > b; });
        bool desc = true;
        for (size_t i = 1; i < v.size(); ++i)
        {
            if (v[i - 1] < v[i]) { desc = false; }
        }
        t_check("降序比较器", desc);
        t_check("降序首尾", v.front() == 9 && v.back() == 1);
    }

    t_section(3, "pdqsort / tiered_sort");
    {
        dense<int> a{9, 4, 7, 1, 5};
        pdqsort(a.data(), a.size(), [](int x, int y) { return x < y; });
        t_check("pdqsort 升序", is_ascending(a));

        dense<int> b{3, 6, 1, 8, 2};
        tiered_sort(b.data(), b.size(), [](int x, int y) { return x < y; });
        t_check("tiered_sort 升序", is_ascending(b));
    }

    t_section(4, "已排序与重复元素");
    {
        dense<int> sorted{1, 2, 3, 4, 5};
        sort(sorted.data(), sorted.size());
        t_check("已排序保持", is_ascending(sorted) && sorted.front() == 1);

        dense<int> dup{3, 1, 3, 1, 3, 1};
        sort(dup.data(), dup.size());
        t_check("重复元素排序", is_ascending(dup) && dup.front() == 1 && dup.back() == 3);
    }

    t_section(5, "sort_indices 间接排序");
    {
        dense<int> vals{30, 10, 20, 50, 40};
        dense<size_t> idx;
        for (size_t i = 0; i < vals.size(); ++i)
        {
            idx.push_back(i);
        }
        sort_indices(idx.data(), vals.data(), idx.size());
        bool ok = vals[idx[0]] == 10 && vals[idx[1]] == 20 && vals[idx[2]] == 30
               && vals[idx[3]] == 40 && vals[idx[4]] == 50;
        t_check("sort_indices 间接有序", ok);
    }

    t_section(6, "大数组");
    {
        dense<int> v;
        v.increase_capacity(2000);
        uint64_t state = 12345;
        for (int i = 0; i < 2000; ++i)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            v.push_back(static_cast<int>(state % 10000));
        }
        sort(v.data(), v.size());
        t_check("2000 元素有序", is_ascending(v));
    }

    return t_end("sort ");
}
