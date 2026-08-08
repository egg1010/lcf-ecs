// test_json_reader.cpp - JSON 解析器单元测试
#include "include/part/codec/json_reader.hpp"
#include "include/part/codec/json_writer.hpp"
#include "test_common.hpp"
#include <string>

static void test_basic_object()
{
    json_reader r(R"({"name":"test","version":42,"active":true})");
    bool ok = r.enter_object();
    bool pass = ok;
    std::string_view k;
    std::string name;
    int32_t version = 0;
    bool active = false;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "name") name = r.read_string();
        else if (k == "version") version = r.read_int32();
        else if (k == "active") active = r.read_bool();
        else { pass = false; break; }
    }
    pass = pass && !r.has_error() && name == "test" && version == 42 && active == true;
    print_item("基本对象", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

static void test_nested_array()
{
    json_reader r(R"({"items":[1,2,3]})");
    bool pass = r.enter_object();
    std::string_view k;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "items")
        {
            pass = r.enter_array();
            int sum = 0;
            while (pass && r.next_element())
            {
                sum += r.read_int32();
                r.end_element();
            }
            pass = pass && sum == 6;
        }
        else { pass = false; break; }
    }
    print_item("嵌套数组", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

static void test_string_escape()
{
    json_reader r(R"({"path":"C:\\test\\file.txt","newline":"line1\nline2"})");
    bool pass = r.enter_object();
    std::string_view k;
    std::string path, newline;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "path") path = r.read_string();
        else if (k == "newline") newline = r.read_string();
        else { pass = false; break; }
    }
    pass = pass && path == "C:\\test\\file.txt" && newline == "line1\nline2";
    print_item("字符串转义解码", pass);
    if (!pass) std::cout << "    path=[" << path << "] newline=[" << newline << "]\n";
}

static void test_number_types()
{
    json_reader r(R"([-100,200,-100000000000,200000000000,3.14,2.71828])");
    bool pass = r.enter_array();
    int32_t i32 = 0; uint32_t u32 = 0;
    int64_t i64 = 0; uint64_t u64 = 0;
    float f = 0; double d = 0;
    if (pass && r.next_element()) { i32 = r.read_int32(); r.end_element(); }
    if (pass && r.next_element()) { u32 = r.read_uint32(); r.end_element(); }
    if (pass && r.next_element()) { i64 = r.read_int64(); r.end_element(); }
    if (pass && r.next_element()) { u64 = r.read_uint64(); r.end_element(); }
    if (pass && r.next_element()) { f = r.read_float(); r.end_element(); }
    if (pass && r.next_element()) { d = r.read_double(); r.end_element(); }
    if (pass && r.next_element()) pass = false; // 应该无更多元素
    pass = pass && i32 == -100 && u32 == 200
                && i64 == -100000000000LL && u64 == 200000000000ULL
                && f > 3.13f && f < 3.15f
                && d > 2.71 && d < 2.72;
    print_item("数字类型", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

static void test_null_and_bool()
{
    json_reader r(R"([null,true,false])");
    bool pass = r.enter_array();
    bool n1 = false, b1 = false, b2 = true;
    if (pass && r.next_element()) { n1 = r.is_null(); r.end_element(); }
    if (pass && r.next_element()) { b1 = r.read_bool(); r.end_element(); }
    if (pass && r.next_element()) { b2 = r.read_bool(); r.end_element(); }
    pass = pass && n1 && b1 == true && b2 == false;
    print_item("null 与 bool", pass);
}

static void test_empty_containers()
{
    json_reader r(R"({"empty_obj":{},"empty_arr":[]})");
    bool pass = r.enter_object();
    std::string_view k;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "empty_obj")
        {
            pass = r.enter_object();
            std::string_view k2 = r.next_key();
            pass = pass && k2.empty();
        }
        else if (k == "empty_arr")
        {
            pass = r.enter_array();
            bool has = r.next_element();
            pass = pass && !has;
        }
        else { pass = false; break; }
    }
    print_item("空容器", pass);
}

static void test_skip_value()
{
    json_reader r(R"({"known":1,"unknown":{"nested":[1,2,3]},"after":42})");
    bool pass = r.enter_object();
    std::string_view k;
    int32_t known = 0, after = 0;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "known") known = r.read_int32();
        else if (k == "after") after = r.read_int32();
        else { pass = r.skip_value(); }
    }
    pass = pass && known == 1 && after == 42 && !r.has_error();
    print_item("跳过未知字段", pass);
}

static void test_round_trip()
{
    json_writer w;
    w.begin_object();
    w.key("name").value("玩家1");
    w.key("level").value(static_cast<uint32_t>(99));
    w.key("hp").value(static_cast<int32_t>(-50));
    w.key("pos").begin_array().value(1.5f).value(2.5f).value(3.5f).end_array();
    w.key("tags").begin_array().value("hero").value("boss").end_array();
    w.key("meta").begin_object().key("created").value(true).end_object();
    w.end_object();
    std::string json = w.string();

    json_reader r(json);
    bool pass = r.enter_object();
    std::string_view k;
    std::string name;
    uint32_t level = 0;
    int32_t hp = 0;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "name") name = r.read_string();
        else if (k == "level") level = r.read_uint32();
        else if (k == "hp") hp = r.read_int32();
        else if (k == "pos")
        {
            pass = r.enter_array();
            float x = 0, y = 0, z = 0;
            if (pass && r.next_element()) { x = r.read_float(); r.end_element(); }
            if (pass && r.next_element()) { y = r.read_float(); r.end_element(); }
            if (pass && r.next_element()) { z = r.read_float(); r.end_element(); }
            if (pass && r.next_element()) pass = false;
            pass = pass && x == 1.5f && y == 2.5f && z == 3.5f;
        }
        else if (k == "tags")
        {
            pass = r.enter_array();
            std::string t1, t2;
            if (pass && r.next_element()) { t1 = r.read_string(); r.end_element(); }
            if (pass && r.next_element()) { t2 = r.read_string(); r.end_element(); }
            if (pass && r.next_element()) pass = false;
            pass = pass && t1 == "hero" && t2 == "boss";
        }
        else if (k == "meta")
        {
            pass = r.enter_object();
            std::string_view mk = r.next_key();
            pass = pass && mk == "created" && r.read_bool();
            std::string_view mk2 = r.next_key();
            pass = pass && mk2.empty();
        }
        else { pass = r.skip_value(); }
    }
    pass = pass && name == "玩家1" && level == 99 && hp == -50 && !r.has_error();
    print_item("读写往返 (含中文)", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

static void test_raw_value()
{
    json_reader r(R"({"nested":{"x":1,"y":2}})");
    bool pass = r.enter_object();
    std::string_view k;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "nested")
        {
            std::string_view raw = r.read_raw_value();
            pass = raw == R"({"x":1,"y":2})";
        }
        else { pass = r.skip_value(); break; }
    }
    print_item("raw_value 原始片段", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

// === 用户手写 JSON 偏差容忍测试 ===

static void test_tolerant_whitespace()
{
    // 多空格、换行、tab、值前后多空格
    json_reader r("{\n\t\"a\"   :   1  ,\n  \"b\"  :  \"x\"  ,\n  \"arr\"  :  [\n    1,\n    2\n  ]\n}");
    bool pass = r.enter_object();
    std::string_view k;
    int32_t a = 0;
    std::string b;
    int32_t arr_sum = 0;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "a") a = r.read_int32();
        else if (k == "b") b = r.read_string();
        else if (k == "arr")
        {
            pass = r.enter_array();
            while (pass && r.next_element()) { arr_sum += r.read_int32(); r.end_element(); }
        }
        else { pass = r.skip_value(); }
    }
    pass = pass && !r.has_error() && a == 1 && b == "x" && arr_sum == 3;
    print_item("容忍多空格/换行/tab", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

static void test_tolerant_trailing_comma()
{
    // 尾随逗号 (object 和 array 均有)
    json_reader r(R"({"a":1,"b":"x",})");
    bool pass = r.enter_object();
    std::string_view k;
    int32_t a = 0;
    std::string b;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "a") a = r.read_int32();
        else if (k == "b") b = r.read_string();
        else { pass = r.skip_value(); }
    }
    pass = pass && !r.has_error() && a == 1 && b == "x";

    // 数组尾随逗号
    json_reader r2(R"([1,2,3,])");
    bool pass2 = r2.enter_array();
    int32_t sum = 0;
    while (pass2 && r2.next_element()) { sum += r2.read_int32(); r2.end_element(); }
    pass2 = pass2 && !r2.has_error() && sum == 6;

    print_item("容忍尾随逗号", pass && pass2);
    if (!(pass && pass2)) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

static void test_tolerant_single_quote()
{
    // 单引号字符串 (key 和 value 均用单引号)
    json_reader r("{'a':1,'b':'x','nested':{'c':'y'}}");
    bool pass = r.enter_object();
    std::string_view k;
    int32_t a = 0;
    std::string b, c;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "a") a = r.read_int32();
        else if (k == "b") b = r.read_string();
        else if (k == "nested")
        {
            pass = r.enter_object();
            std::string_view nk;
            while (pass && !(nk = r.next_key()).empty())
            {
                if (nk == "c") c = r.read_string();
                else { pass = r.skip_value(); }
            }
        }
        else { pass = r.skip_value(); }
    }
    pass = pass && !r.has_error() && a == 1 && b == "x" && c == "y";
    print_item("容忍单引号字符串", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

static void test_tolerant_comments()
{
    // 块注释和行注释
    json_reader r("{\"a\":1,/* 这是注释 */\"b\":\"x\",\n// 行注释\n\"c\":true}");
    bool pass = r.enter_object();
    std::string_view k;
    int32_t a = 0;
    std::string b;
    bool c = false;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "a") a = r.read_int32();
        else if (k == "b") b = r.read_string();
        else if (k == "c") c = r.read_bool();
        else { pass = r.skip_value(); }
    }
    pass = pass && !r.has_error() && a == 1 && b == "x" && c == true;
    print_item("容忍注释 (块/行)", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

static void test_tolerant_leading_plus()
{
    // 数字前导 + 号
    json_reader r(R"({"a":+1,"b":+3.14,"c":-5})");
    bool pass = r.enter_object();
    std::string_view k;
    int32_t a = 0;
    float b = 0;
    int32_t c = 0;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "a") a = r.read_int32();
        else if (k == "b") b = r.read_float();
        else if (k == "c") c = r.read_int32();
        else { pass = r.skip_value(); }
    }
    pass = pass && !r.has_error() && a == 1 && b > 3.13f && b < 3.15f && c == -5;
    print_item("容忍数字前导+号", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

static void test_tolerant_mixed()
{
    // 综合混合: 多空格 + 换行 + 尾随逗号 + 单引号 + 注释 + 前导+
    std::string json =
        "{\n"
        "  // 用户配置文件\n"
        "  'name' : 'Alice',\n"
        "  'age' : +30,\n"
        "  'scores' : [ +90, +85, +95, ],\n"
        "  /* 元数据 */\n"
        "  'meta' : { 'active' : true, },\n"
        "}";
    json_reader r(json);
    bool pass = r.enter_object();
    std::string_view k;
    std::string name;
    int32_t age = 0;
    int32_t scores_sum = 0;
    bool meta_active = false;
    while (pass && !(k = r.next_key()).empty())
    {
        if (k == "name") name = r.read_string();
        else if (k == "age") age = r.read_int32();
        else if (k == "scores")
        {
            pass = r.enter_array();
            while (pass && r.next_element()) { scores_sum += r.read_int32(); r.end_element(); }
        }
        else if (k == "meta")
        {
            pass = r.enter_object();
            std::string_view mk;
            while (pass && !(mk = r.next_key()).empty())
            {
                if (mk == "active") meta_active = r.read_bool();
                else { pass = r.skip_value(); }
            }
        }
        else { pass = r.skip_value(); }
    }
    pass = pass && !r.has_error()
           && name == "Alice" && age == 30
           && scores_sum == 270 && meta_active == true;
    print_item("综合混合偏差", pass);
    if (!pass) std::cout << "    error: " << r.last_error().read_message() << "\n";
}

int main()
{
    print_section(1, "json_reader 单元测试");
    test_basic_object();
    test_nested_array();
    test_string_escape();
    test_number_types();
    test_null_and_bool();
    test_empty_containers();
    test_skip_value();
    test_round_trip();
    test_raw_value();

    print_section(2, "用户手写 JSON 偏差容忍");
    test_tolerant_whitespace();
    test_tolerant_trailing_comma();
    test_tolerant_single_quote();
    test_tolerant_comments();
    test_tolerant_leading_plus();
    test_tolerant_mixed();

    print_summary("功能测试");
    return 0;
}
