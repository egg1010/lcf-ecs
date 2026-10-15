// test_module_json_codec.cpp - 模块功能测试: json_writer / json_reader
#include "test_common.hpp"
#include "include/part/codec/json_writer.hpp"
#include "include/part/codec/json_reader.hpp"
#include <string>
#include <string_view>
#include <limits>

int main()
{
    t_begin("test_module_json_codec — 模块功能测试: json_writer / json_reader");

    t_section(1, "writer 基本类型");
    {
        json_writer w;
        w.begin_object();
        w.key("name").value("test");
        w.key("count").value(42u);
        w.key("pi").value(3.5);
        w.key("flag").value(true);
        w.end_object();
        std::string s = w.take();
        t_check("writer 输出非空", !s.empty());
        t_check("含对象花括号", s.find('{') != std::string::npos && s.find('}') != std::string::npos);
        t_check("含字段名", s.find("name") != std::string::npos);
    }

    t_section(2, "writer 数组与嵌套");
    {
        json_writer w;
        w.begin_object();
        w.key("items").begin_array();
        w.value(1);
        w.value(2);
        w.value(3);
        w.end_array();
        w.end_object();
        std::string s = w.take();
        t_check("含方括号", s.find('[') != std::string::npos);
        t_check("含元素", s.find('1') != std::string::npos && s.find('3') != std::string::npos);
    }

    t_section(3, "非有限数写 null");
    {
        json_writer w;
        w.begin_array();
        w.value(std::numeric_limits<double>::quiet_NaN());
        w.value(std::numeric_limits<double>::infinity());
        w.end_array();
        std::string s = w.take();
        t_check("NaN/Inf 写 null", s.find("null") != std::string::npos);
    }

    t_section(4, "reader 读取对象");
    {
        json_reader r(R"({"name":"hello","num":7})");
        bool entered = r.enter_object();
        t_check("enter_object", entered);

        std::string_view k1 = r.next_key();
        t_check("首个 key", k1 == "name");
        std::string v1 = r.read_string();
        t_check("读字符串值", v1 == "hello");

        std::string_view k2 = r.next_key();
        t_check("第二个 key", k2 == "num");
        uint32_t n = r.read_uint32();
        t_check("读整数", n == 7);

        t_check("无解析错误", !r.has_error());
    }

    t_section(5, "reader 跳过未知字段");
    {
        json_reader r(R"({"a":1,"b":{"x":2},"c":3})");
        t_check("enter_object", r.enter_object());
        std::string_view k = r.next_key();
        t_check("key a", k == "a");
        uint32_t a = r.read_uint32();
        t_check("读 a", a == 1);

        k = r.next_key();
        t_check("key b", k == "b");
        t_check("skip_value 嵌套对象", r.skip_value());

        k = r.next_key();
        t_check("key c", k == "c");
        uint32_t c = r.read_uint32();
        t_check("跳过嵌套后仍可读", c == 3);
    }

    t_section(6, "畸形与空输入");
    {
        json_reader r("");
        t_check("空输入 enter 失败", !r.enter_object());
    }

    return t_end("json_codec ");
}
