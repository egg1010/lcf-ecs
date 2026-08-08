// test_json_writer.cpp - JSON 写入器单元测试
#include "include/part/codec/json_writer.hpp"
#include "test_common.hpp"
#include <string>

static void test_basic_object()
{
    json_writer w;
    w.begin_object();
    w.key("name").value("test");
    w.key("version").value(42);
    w.key("active").value(true);
    w.end_object();
    std::string s = w.string();
    bool pass = (s == R"({"name":"test","version":42,"active":true})");
    print_item("基本对象", pass);
    if (!pass) std::cout << "    实际: " << s << "\n";
}

static void test_nested_array()
{
    json_writer w;
    w.begin_object();
    w.key("items").begin_array();
    w.value(1).value(2).value(3);
    w.end_array();
    w.end_object();
    std::string s = w.string();
    bool pass = (s == R"({"items":[1,2,3]})");
    print_item("嵌套数组", pass);
    if (!pass) std::cout << "    实际: " << s << "\n";
}

static void test_string_escape()
{
    json_writer w;
    w.begin_object();
    w.key("path").value("C:\\test\\file.txt");
    w.key("newline").value("line1\nline2");
    w.end_object();
    std::string s = w.string();
    bool pass = (s == R"({"path":"C:\\test\\file.txt","newline":"line1\nline2"})");
    print_item("字符串转义", pass);
    if (!pass) std::cout << "    实际: " << s << "\n";
}

static void test_number_types()
{
    json_writer w;
    w.begin_array();
    w.value(static_cast<int32_t>(-100));
    w.value(static_cast<uint32_t>(200u));
    w.value(static_cast<int64_t>(-100000000000LL));
    w.value(static_cast<uint64_t>(200000000000ULL));
    w.value(3.14f);
    w.value(2.71828);
    w.end_array();
    std::string s = w.string();
    // to_chars 输出最短表示, 仅验证关键内容
    bool pass = (s.find("-100") != std::string::npos)
             && (s.find("200") != std::string::npos)
             && (s.find("3.14") != std::string::npos)
             && (s.find("2.71828") != std::string::npos);
    print_item("数字类型", pass);
    if (!pass) std::cout << "    实际: " << s << "\n";
}

static void test_pretty()
{
    json_writer w(4096, true);
    w.begin_object();
    w.key("a").value(1);
    w.key("b").begin_array();
    w.value(2).value(3);
    w.end_array();
    w.end_object();
    std::string s = w.string();
    bool pass = (s.find("\n") != std::string::npos)
             && (s.find("  ") != std::string::npos);
    print_item("美化输出", pass);
    if (!pass) std::cout << "    实际: " << s << "\n";
}

static void test_null_and_bool()
{
    json_writer w;
    w.begin_array();
    w.null();
    w.value(true);
    w.value(false);
    w.end_array();
    std::string s = w.string();
    bool pass = (s == R"([null,true,false])");
    print_item("null 与 bool", pass);
    if (!pass) std::cout << "    实际: " << s << "\n";
}

static void test_raw_value()
{
    json_writer w;
    w.begin_object();
    w.key("nested").raw_value(R"({"x":1,"y":2})");
    w.end_object();
    std::string s = w.string();
    bool pass = (s == R"({"nested":{"x":1,"y":2}})");
    print_item("raw_value 嵌套", pass);
    if (!pass) std::cout << "    实际: " << s << "\n";
}

static void test_empty_containers()
{
    json_writer w;
    w.begin_object();
    w.key("empty_obj").begin_object().end_object();
    w.key("empty_arr").begin_array().end_array();
    w.end_object();
    std::string s = w.string();
    bool pass = (s == R"({"empty_obj":{},"empty_arr":[]})");
    print_item("空容器", pass);
    if (!pass) std::cout << "    实际: " << s << "\n";
}

int main()
{
    print_section(1, "json_writer 单元测试");
    test_basic_object();
    test_nested_array();
    test_string_escape();
    test_number_types();
    test_pretty();
    test_null_and_bool();
    test_raw_value();
    test_empty_containers();
    print_summary("功能测试");
    return 0;
}
