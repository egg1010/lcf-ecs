// test_module_aggregate_traverse.cpp - 模块功能测试: aggregate_traverse
#include "test_common.hpp"
#include "include/reflection/base/aggregate_traverse.hpp"

struct agg3
{
    int a;
    double b;
    char c;
};

struct agg1
{
    int only;
};

struct nested_agg
{
    agg1 inner;
    float f;
};

int main()
{
    t_begin("test_module_aggregate_traverse — 模块功能测试: aggregate_traverse");

    t_section(1, "字段数量检测");
    {
        t_check("agg3 字段数 == 3", reflect::detail::aggregate_field_count_v<agg3> == 3);
        t_check("agg1 字段数 == 1", reflect::detail::aggregate_field_count_v<agg1> == 1);
        t_check("nested_agg 字段数 == 2", reflect::detail::aggregate_field_count_v<nested_agg> == 2);
    }

    t_section(2, "成员遍历");
    {
        agg3 o{7, 2.5, 'z'};
        int cnt = 0;
        double sum = 0.0;
        reflect::detail::for_each_aggregate_member(o, [&](auto& m, size_t idx) {
            (void)idx;
            ++cnt;
            sum += static_cast<double>(m);
        });
        t_check("for_each_aggregate_member 计数 == 3", cnt == 3);
        t_check("for_each_aggregate_member 取值", sum == 7.0 + 2.5 + 122.0);
    }

    t_section(3, "遍历索引");
    {
        agg3 o{1, 2.0, 'a'};
        size_t last_idx = 999;
        int cnt = 0;
        reflect::detail::for_each_aggregate_member(o, [&](auto& m, size_t idx) {
            (void)m;
            last_idx = idx;
            ++cnt;
        });
        t_check("索引从 0 递增", cnt == 3 && last_idx == 2);
    }

    t_section(4, "成员偏移");
    {
        size_t off_a = reflect::detail::member_offset(&agg3::a);
        size_t off_b = reflect::detail::member_offset(&agg3::b);
        t_check("首成员偏移 0", off_a == 0);
        t_check("第二成员偏移 > 0", off_b > off_a);
    }

    return t_end("aggregate_traverse ");
}
