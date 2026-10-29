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

    t_section(5, "游标空区间与截断边界");
    {
        size_t consumed = 99;
        char32_t cp = utf8_next_cp(nullptr, nullptr, &consumed);
        t_check("空游标返回替换字符", cp == static_cast<char32_t>(0xFFFD));
        t_check("空游标消耗 0 字节", consumed == 0);

        const char* empty = "";
        cp = utf8_next_cp(empty, empty, &consumed);
        t_check("空区间安全返回", cp == static_cast<char32_t>(0xFFFD) && consumed == 0);

        const char truncated[] = "\xE4";
        cp = utf8_next_cp(truncated, truncated + 1, &consumed);
        t_check("截断序列安全返回", cp == static_cast<char32_t>(0xFFFD) && consumed == 1);

        uint32_t raw_cp = 0;
        size_t raw_len = 99;
        bool ok = detail_utf8::utf8_decode_one(nullptr, nullptr, &raw_cp, &raw_len);
        t_check("底层解码拒绝空指针", !ok && raw_cp == 0xFFFD && raw_len == 0);
    }

    return t_end("utf8pp ");
}
