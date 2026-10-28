// test_module_member_offset.cpp - 模块功能测试: member_offset
#include "test_common.hpp"
#include "include/reflection/base/foundation.hpp"
#include <cstddef>

struct sample
{
    int a;
    double b;
    char c;
    long d;
};

int main()
{
    t_begin("test_module_member_offset — 模块功能测试: member_offset");

    t_section(1, "offset_of 计算");
    {
        size_t off_b = reflect::detail::offset_of(&sample::b);
        t_check("b 偏移与 offsetof 一致", off_b == offsetof(sample, b));
        size_t off_c = reflect::detail::offset_of(&sample::c);
        t_check("c 偏移可读", off_c == offsetof(sample, c));
        t_check("首成员偏移为 0", reflect::detail::offset_of(&sample::a) == 0);
    }

    t_section(2, "offset_access 读写");
    {
        sample s{1, 2.5, 'x', 100};
        int& ra = reflect::detail::offset_access<int>(&s, reflect::detail::offset_of(&sample::a));
        t_check("读首成员", ra == 1);
        ra = 9;
        t_check("写首成员", s.a == 9);

        double& rb = reflect::detail::offset_access<double>(&s, reflect::detail::offset_of(&sample::b));
        t_check("读双精度成员", rb == 2.5);
        rb = 4.5;
        t_check("写双精度成员", s.b == 4.5);

        const sample& cs = s;
        const long& rd = reflect::detail::offset_access<long>(&cs, reflect::detail::offset_of(&sample::d));
        t_check("const 访问", rd == 100);
    }

    t_section(3, "ub_access 成员引用");
    {
        sample s{3, 1.5, 'z', 7};
        double& rb = reflect::detail::ub_access<sample, double>(s, reflect::detail::offset_of(&sample::b));
        t_check("ub_access 读", rb == 1.5);
        rb = 8.5;
        t_check("ub_access 写", s.b == 8.5);
    }

    return t_end("member_offset ");
}
