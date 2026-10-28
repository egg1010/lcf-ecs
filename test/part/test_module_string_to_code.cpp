// test_module_string_to_code.cpp - 模块功能测试: string_to_code
#include "test_common.hpp"
#include "include/part/string_to_code.hpp"
#include "include/part/utf8pp/utf8_view.hpp"

int main()
{
    t_begin("test_module_string_to_code — 模块功能测试: string_to_code");

    t_section(1, "短串内联编码");
    {
        utf8_view v("hi");
        string_to_code::code_value c = string_to_code::encode(v);
        t_check("短串 is_inline", c.is_inline());
        t_check("string_size == 2", c.string_size() == 2);
        t_check("decode 长度一致", c.decode().size() == 2);
        t_check("decode 内容一致", c.decode() == v);
    }

    t_section(2, "长串堆编码");
    {
        utf8_view v("longer_than_eight_bytes");
        string_to_code::code_value c = string_to_code::encode(v);
        t_check("长串非 inline", !c.is_inline());
        t_check("长串 size", c.string_size() == v.size());
        t_check("长串 decode 一致", c.decode() == v);
    }

    t_section(3, "相等比较");
    {
        string_to_code::code_value c1 = string_to_code::encode(utf8_view("hello"));
        string_to_code::code_value c2 = string_to_code::encode(utf8_view("hello"));
        string_to_code::code_value c3 = string_to_code::encode(utf8_view("world"));
        t_check("相同内容 equals", c1.equals(c2));
        t_check("相同内容 equals_strict", c1.equals_strict(c2));
        t_check("不同内容 equals false", !c1.equals(c3));
    }

    t_section(4, "空串");
    {
        utf8_view v("");
        string_to_code::code_value c = string_to_code::encode(v);
        t_check("空串 empty", c.empty());
        t_check("空串 string_size == 0", c.string_size() == 0);
    }

    return t_end("string_to_code ");
}
