// test_module_utf8_view.cpp - 模块功能测试: utf8_view
#include "test_common.hpp"
#include "include/part/utf8pp/utf8_view.hpp"

int main()
{
    t_begin("test_module_utf8_view — 模块功能测试: utf8_view");

    t_section(1, "构造与长度");
    {
        utf8_view v("hello");
        t_check("字节长度 == 5", v.size_bytes() == 5);
        t_check("码点长度 == 5", v.size() == 5);
        t_check("非空", !v.empty());
        t_check("data 可读", v.data()[0] == 'h');

        utf8_view empty;
        t_check("默认构造为空", empty.empty() && empty.size() == 0);
    }

    t_section(2, "多字节 UTF-8");
    {
        // "你好": 每字 3 字节, 共 2 码点
        utf8_view cn("\xE4\xBD\xA0\xE5\xA5\xBD");
        t_check("中文 6 字节", cn.size_bytes() == 6);
        t_check("中文 2 码点", cn.size() == 2);
        t_check("首码点非 ASCII", cn.front() > 0x7F);
    }

    t_section(3, "码点访问");
    {
        utf8_view v("abc");
        t_check("get(0)", v.get(0) == U'a');
        t_check("get(2)", v.get(2) == U'c');
        t_check("front/back", v.front() == U'a' && v.back() == U'c');
    }

    t_section(4, "子串");
    {
        utf8_view v("hello world");
        utf8_view sub = v.substr(6, 5);
        t_check("substr 长度", sub.size() == 5);
        t_check("substr 内容", sub.data()[0] == 'w' && sub.data()[4] == 'd');

        utf8_view sub_bytes = v.substr_bytes(0, 5);
        t_check("substr_bytes 长度", sub_bytes.size_bytes() == 5);
    }

    t_section(5, "查找与比较");
    {
        utf8_view v("hello");
        t_check("find 字符 'l'", v.find(U'l') == 2);
        t_check("find 不存在返回 npos", v.find(U'z') == utf8_view::npos);

        t_check("compare 相等 == 0", v.compare(utf8_view("hello")) == 0);
        t_check("compare 不等 != 0", v.compare(utf8_view("world")) != 0);
        utf8_view a("abc");
        utf8_view b("abd");
        t_check("compare 大小关系", a.compare(b) < 0);
    }

    t_section(6, "拷贝到缓冲");
    {
        utf8_view v("test");
        char buf[8] = {};
        size_t n = v.copy(buf, 4);
        t_check("copy 数量", n == 4);
        t_check("copy 内容", buf[0] == 't' && buf[3] == 't');
    }

    return t_end("utf8_view ");
}
