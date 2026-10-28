// test_module_utf8pp.cpp - 模块功能测试: utf8pp
#include "test_common.hpp"
#include "include/part/utf8pp/utf8pp.hpp"
#include <string>

int main()
{
    t_begin("test_module_utf8pp — 模块功能测试: utf8pp");

    t_section(1, "构造与长度");
    {
        utf8pp s;
        t_check("默认构造为空", s.empty() && s.size() == 0);

        utf8pp s2("hello");
        t_check("const char* 构造", !s2.empty());
        t_check("字节长度 == 5", s2.size() == 5);
        t_check("c_str 可读", s2.c_str()[0] == 'h');
    }

    t_section(2, "多字节 UTF-8");
    {
        utf8pp cn("\xE4\xBD\xA0\xE5\xA5\xBD");
        t_check("中文码点长度 == 2", cn.size() == 2);
        t_check("中文字节长度 == 6", cn.to_utf8_view().size_bytes() == 6);
    }

    t_section(3, "视图与转换");
    {
        utf8pp s("world");
        utf8_view v = s.to_utf8_view();
        t_check("to_utf8_view 字节", v.size_bytes() == 5);
        t_check("to_utf8_view 内容", v.data()[0] == 'w');

        std::string std_s = s.to_std_string();
        t_check("to_std_string", std_s == "world");
    }

    t_section(4, "空串与单字符");
    {
        utf8pp e("");
        t_check("空串 empty", e.empty());

        utf8pp one("x");
        t_check("单字符长度", one.size() == 1 && one.length() == 1);
    }

    return t_end("utf8pp ");
}
