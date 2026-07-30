// test_serialization_perf.cpp - ECS 序列化性能测试
#include "include/serialization/serialization.hpp"
#include "include/part/json_writer.hpp"
#include "include/part/json_reader.hpp"
#include "test_common.hpp"
#include <string>

using ecs::serialization;
using ecs::entity;
using ecs::manager;

// trivially copyable 组件 (base64 路径)
struct Vec3 {
    float x, y, z;
    Vec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
};
static_assert(std::is_trivially_copyable_v<Vec3>);

// 提供 to_json/from_json 的 trivial 组件 (JSON 路径)
struct Hp {
    int current;
    int max;
    Hp(int c = 100, int m = 100) : current(c), max(m) {}
    std::string to_json() const {
        json_writer w;
        w.begin_object();
        w.key("c").value(current);
        w.key("m").value(max);
        w.end_object();
        return w.take();
    }
    void from_json(std::string_view s) {
        json_reader r(s);
        if (!r.enter_object()) return;
        std::string_view k;
        while (!(k = r.next_key()).empty()) {
            if (k == "c") current = r.read_int32();
            else if (k == "m") max = r.read_int32();
            else r.skip_value();
        }
    }
};

static void bench_json_writer_basic(size_t count)
{
    print_perf_sub("json_writer 基础写入");
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_writer w(8192);
        w.begin_object();
        w.key("name").value("test");
        w.key("value").value(static_cast<int32_t>(42));
        w.key("flag").value(true);
        w.key("arr").begin_array().value(1).value(2).value(3).end_array();
        w.end_object();
        lcf_sink(w.size());
        total += t.elapsed_ms();
    }
    print_perf("json_writer (基础对象)", count, total);
}

static void bench_json_reader_basic(size_t count)
{
    print_perf_sub("json_reader 基础解析");
    std::string json = R"({"name":"test","value":42,"flag":true,"arr":[1,2,3]})";
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_reader r(json);
        r.enter_object();
        std::string_view k;
        while (!(k = r.next_key()).empty())
        {
            if (k == "name") { std::string s = r.read_string(); lcf_sink(s.size()); }
            else if (k == "value") lcf_sink(r.read_int32());
            else if (k == "flag") lcf_sink(r.read_bool());
            else if (k == "arr")
            {
                r.enter_array();
                while (r.next_element()) lcf_sink(r.read_int32());
            }
            else r.skip_value();
        }
        total += t.elapsed_ms();
    }
    print_perf("json_reader (基础对象)", count, total);
}

static void bench_save_trivial(size_t entity_count, size_t iter_count)
{
    print_perf_sub("序列化 trivial 组件 (base64)");
    manager mgr;
    for (size_t i = 0; i < entity_count; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add<Vec3>(e, Vec3(static_cast<float>(i), static_cast<float>(i + 1), static_cast<float>(i + 2)));
    }
    double total = 0;
    std::string json;
    for (size_t iter = 0; iter < iter_count; ++iter)
    {
        timer t;
        serialization(mgr).save_to_string<Vec3>(json);
        total += t.elapsed_ms();
    }
    lcf_sink(json.size());
    print_perf("save<Vec3> (" + std::to_string(entity_count) + " 实体)", iter_count, total);
}

static void bench_load_trivial(size_t entity_count, size_t iter_count)
{
    print_perf_sub("反序列化 trivial 组件 (base64)");
    manager mgr;
    for (size_t i = 0; i < entity_count; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add<Vec3>(e, Vec3(static_cast<float>(i), static_cast<float>(i + 1), static_cast<float>(i + 2)));
    }
    std::string json;
    serialization(mgr).save_to_string<Vec3>(json);

    double total = 0;
    for (size_t iter = 0; iter < iter_count; ++iter)
    {
        manager mgr2;
        timer t;
        serialization(mgr2).load_from_string<Vec3>(json);
        total += t.elapsed_ms();
    }
    print_perf("load<Vec3> (" + std::to_string(entity_count) + " 实体)", iter_count, total);
}

static void bench_save_json_method(size_t entity_count, size_t iter_count)
{
    print_perf_sub("序列化 to_json 组件 (JSON 路径)");
    manager mgr;
    for (size_t i = 0; i < entity_count; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add<Hp>(e, Hp(static_cast<int>(i), static_cast<int>(i) + 100));
    }
    double total = 0;
    std::string json;
    for (size_t iter = 0; iter < iter_count; ++iter)
    {
        timer t;
        serialization(mgr).save_to_string<Hp>(json);
        total += t.elapsed_ms();
    }
    lcf_sink(json.size());
    print_perf("save<Hp> (" + std::to_string(entity_count) + " 实体)", iter_count, total);
}

static void bench_load_json_method(size_t entity_count, size_t iter_count)
{
    print_perf_sub("反序列化 from_json 组件 (JSON 路径)");
    manager mgr;
    for (size_t i = 0; i < entity_count; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add<Hp>(e, Hp(static_cast<int>(i), static_cast<int>(i) + 100));
    }
    std::string json;
    serialization(mgr).save_to_string<Hp>(json);

    double total = 0;
    for (size_t iter = 0; iter < iter_count; ++iter)
    {
        manager mgr2;
        timer t;
        serialization(mgr2).load_from_string<Hp>(json);
        total += t.elapsed_ms();
    }
    print_perf("load<Hp> (" + std::to_string(entity_count) + " 实体)", iter_count, total);
}

static void bench_round_trip_multi(size_t entity_count, size_t iter_count)
{
    print_perf_sub("多类型往返 (Vec3 + Hp)");
    manager mgr;
    for (size_t i = 0; i < entity_count; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add<Vec3>(e, Vec3(static_cast<float>(i), 0, 0));
        mgr.add<Hp>(e, Hp(static_cast<int>(i), 100));
    }
    double total = 0;
    std::string json;
    for (size_t iter = 0; iter < iter_count; ++iter)
    {
        manager mgr2;
        timer t;
        serialization(mgr).save_to_string<Vec3, Hp>(json);
        serialization(mgr2).load_from_string<Vec3, Hp>(json);
        total += t.elapsed_ms();
    }
    lcf_sink(json.size());
    print_perf("round_trip<Vec3,Hp> (" + std::to_string(entity_count) + " 实体)", iter_count, total);
}

static void bench_json_writer_large_array(size_t count)
{
    print_perf_sub("json_writer 大数组");
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_writer w(1 << 20);
        w.begin_array();
        for (int i = 0; i < 10000; ++i)
        {
            w.value(i);
        }
        w.end_array();
        lcf_sink(w.size());
        total += t.elapsed_ms();
    }
    print_perf("json_writer 10000 整数数组", count, total);
}

static void bench_json_reader_large_array(size_t count)
{
    print_perf_sub("json_reader 大数组解析");
    json_writer w(1 << 20);
    w.begin_array();
    for (int i = 0; i < 10000; ++i)
    {
        w.value(i);
    }
    w.end_array();
    std::string json = w.take();

    double total = 0;
    long long sum = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_reader r(json);
        r.enter_array();
        while (r.next_element())
        {
            sum += r.read_int32();
        }
        total += t.elapsed_ms();
    }
    lcf_sink(sum);
    print_perf("json_reader 10000 整数数组", count, total);
}

// === 极端性能测试 ===

// 100万整数数组 (测试大数据量吞吐)
static void bench_extreme_writer_1m_ints(size_t count)
{
    print_perf_sub("极端: json_writer 100万整数");
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_writer w(1 << 24);
        w.begin_array();
        for (int i = 0; i < 1000000; ++i)
        {
            w.value(i);
        }
        w.end_array();
        lcf_sink(w.size());
        total += t.elapsed_ms();
    }
    print_perf("writer 100万 int", count, total);
}

static void bench_extreme_reader_1m_ints(size_t count)
{
    print_perf_sub("极端: json_reader 100万整数");
    json_writer w(1 << 24);
    w.begin_array();
    for (int i = 0; i < 1000000; ++i) w.value(i);
    w.end_array();
    std::string json = w.take();

    double total = 0;
    long long sum = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_reader r(json);
        r.enter_array();
        while (r.next_element()) sum += r.read_int32();
        total += t.elapsed_ms();
    }
    lcf_sink(sum);
    print_perf("reader 100万 int", count, total);
}

// 10万对象数组 (测试对象开销)
static void bench_extreme_writer_100k_objects(size_t count)
{
    print_perf_sub("极端: json_writer 10万对象");
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_writer w(1 << 24);
        w.begin_array();
        for (int i = 0; i < 100000; ++i)
        {
            w.begin_object();
            w.key("id").value(i);
            w.key("x").value(static_cast<double>(i));
            w.key("y").value(static_cast<double>(i * 2));
            w.key("name").value("item");
            w.end_object();
        }
        w.end_array();
        lcf_sink(w.size());
        total += t.elapsed_ms();
    }
    print_perf("writer 10万 object", count, total);
}

static void bench_extreme_reader_100k_objects(size_t count)
{
    print_perf_sub("极端: json_reader 10万对象");
    json_writer w(1 << 24);
    w.begin_array();
    for (int i = 0; i < 100000; ++i)
    {
        w.begin_object();
        w.key("id").value(i);
        w.key("x").value(static_cast<double>(i));
        w.key("y").value(static_cast<double>(i * 2));
        w.key("name").value("item");
        w.end_object();
    }
    w.end_array();
    std::string json = w.take();

    double total = 0;
    long long sum = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_reader r(json);
        r.enter_array();
        while (r.next_element())
        {
            r.enter_object();
            std::string_view k;
            while (!(k = r.next_key()).empty())
            {
                if (k == "id") sum += r.read_int32();
                else r.skip_value();
            }
        }
        total += t.elapsed_ms();
    }
    lcf_sink(sum);
    print_perf("reader 10万 object", count, total);
}

// 1MB 大字符串 (测试大字符串处理)
static void bench_extreme_writer_1mb_string(size_t count)
{
    print_perf_sub("极端: json_writer 1MB字符串");
    std::string big(1 << 20, 'x');
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_writer w(1 << 22);
        w.begin_object();
        w.key("data").value(big);
        w.end_object();
        lcf_sink(w.size());
        total += t.elapsed_ms();
    }
    print_perf("writer 1MB string", count, total);
}

static void bench_extreme_reader_1mb_string(size_t count)
{
    print_perf_sub("极端: json_reader 1MB字符串");
    std::string big(1 << 20, 'x');
    json_writer w(1 << 22);
    w.begin_object();
    w.key("data").value(big);
    w.end_object();
    std::string json = w.take();

    double total = 0;
    size_t len = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_reader r(json);
        r.enter_object();
        std::string_view k;
        while (!(k = r.next_key()).empty())
        {
            if (k == "data")
            {
                std::string s = r.read_string();
                len = s.size();
            }
            else r.skip_value();
        }
        total += t.elapsed_ms();
    }
    lcf_sink(len);
    print_perf("reader 1MB string", count, total);
}

// 深嵌套 100 层对象 (测试递归深度)
static void bench_extreme_writer_deep_nesting(size_t count)
{
    print_perf_sub("极端: json_writer 100层深嵌套");
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_writer w(1 << 16);
        for (int i = 0; i < 100; ++i)
        {
            w.begin_object();
            w.key("n").value(i);
            w.key("child");
        }
        w.begin_object();
        w.key("leaf").value(true);
        w.end_object();
        for (int i = 0; i < 100; ++i) w.end_object();
        lcf_sink(w.size());
        total += t.elapsed_ms();
    }
    print_perf("writer 100层嵌套", count, total);
}

static void bench_extreme_reader_deep_nesting(size_t count)
{
    print_perf_sub("极端: json_reader 100层深嵌套");
    json_writer w(1 << 16);
    for (int i = 0; i < 100; ++i)
    {
        w.begin_object();
        w.key("n").value(i);
        w.key("child");
    }
    w.begin_object();
    w.key("leaf").value(true);
    w.end_object();
    for (int i = 0; i < 100; ++i) w.end_object();
    std::string json = w.take();

    double total = 0;
    long long sum = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_reader r(json);
        int depth = 0;
        while (r.enter_object())
        {
            ++depth;
            std::string_view k;
            bool has_child = false;
            while (!(k = r.next_key()).empty())
            {
                if (k == "n") sum += r.read_int32();
                else if (k == "child") has_child = true;
                else if (k == "leaf") r.read_bool();
                else r.skip_value();
            }
            if (!has_child) break;
        }
        for (int i = 1; i < depth; ++i)
        {
            r.exit_object();
        }
        total += t.elapsed_ms();
    }
    lcf_sink(sum);
    print_perf("reader 100层嵌套", count, total);
}

// 100万次小对象 (测试高频小对象开销)
static void bench_extreme_writer_1m_tiny_objects(size_t count)
{
    print_perf_sub("极端: json_writer 100万小对象");
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_writer w(1 << 24);
        w.begin_array();
        for (int i = 0; i < 1000000; ++i)
        {
            w.begin_object();
            w.key("v").value(i);
            w.end_object();
        }
        w.end_array();
        lcf_sink(w.size());
        total += t.elapsed_ms();
    }
    print_perf("writer 100万 tiny obj", count, total);
}

// 浮点数密集 (测试 to_chars 浮点转换)
static void bench_extreme_writer_floats(size_t count)
{
    print_perf_sub("极端: json_writer 10万浮点数");
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_writer w(1 << 22);
        w.begin_array();
        for (int i = 0; i < 100000; ++i)
        {
            w.value(static_cast<double>(i) * 3.14159265358979);
        }
        w.end_array();
        lcf_sink(w.size());
        total += t.elapsed_ms();
    }
    print_perf("writer 10万 double", count, total);
}

static void bench_extreme_reader_floats(size_t count)
{
    print_perf_sub("极端: json_reader 10万浮点数");
    json_writer w(1 << 22);
    w.begin_array();
    for (int i = 0; i < 100000; ++i)
    {
        w.value(static_cast<double>(i) * 3.14159265358979);
    }
    w.end_array();
    std::string json = w.take();

    double total = 0;
    double sum = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_reader r(json);
        r.enter_array();
        while (r.next_element()) sum += r.read_double();
        total += t.elapsed_ms();
    }
    lcf_sink(sum);
    print_perf("reader 10万 double", count, total);
}

// 大量转义字符 (测试转义处理开销)
static void bench_extreme_writer_escaped(size_t count)
{
    print_perf_sub("极端: json_writer 转义密集字符串");
    std::string escaped;
    escaped.reserve(10000);
    for (int i = 0; i < 1000; ++i)
    {
        escaped += "line\\t\\n\"quote\"\\/";
    }
    double total = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_writer w(1 << 20);
        w.begin_array();
        for (int i = 0; i < 100; ++i) w.value(escaped);
        w.end_array();
        lcf_sink(w.size());
        total += t.elapsed_ms();
    }
    print_perf("writer 转义密集 (100个)", count, total);
}

static void bench_extreme_reader_escaped(size_t count)
{
    print_perf_sub("极端: json_reader 转义密集字符串");
    std::string escaped;
    escaped.reserve(10000);
    for (int i = 0; i < 1000; ++i)
    {
        escaped += "line\\t\\n\"quote\"\\/";
    }
    json_writer w(1 << 20);
    w.begin_array();
    for (int i = 0; i < 100; ++i) w.value(escaped);
    w.end_array();
    std::string json = w.take();

    double total = 0;
    size_t len = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_reader r(json);
        r.enter_array();
        while (r.next_element())
        {
            std::string s = r.read_string();
            len = s.size();
        }
        total += t.elapsed_ms();
    }
    lcf_sink(len);
    print_perf("reader 转义密集 (100个)", count, total);
}

// 容忍模式性能 (注释+单引号+尾随逗号, 测试 skip_ws 额外开销)
static void bench_extreme_reader_tolerant(size_t count)
{
    print_perf_sub("极端: json_reader 容忍模式 (注释/单引号)");
    std::string json =
        "{\n"
        "  // 用户注释\n"
        "  'name' : 'Alice',\n"
        "  'age' : +30,\n"
        "  'scores' : [ +90, +85, +95, ],\n"
        "  /* 块注释 */\n"
        "  'meta' : { 'active' : true, },\n"
        "}\n";

    double total = 0;
    long long sum = 0;
    for (size_t iter = 0; iter < count; ++iter)
    {
        timer t;
        json_reader r(json);
        r.enter_object();
        std::string_view k;
        while (!(k = r.next_key()).empty())
        {
            if (k == "age") sum += r.read_int32();
            else if (k == "scores")
            {
                r.enter_array();
                while (r.next_element()) sum += r.read_int32();
            }
            else r.skip_value();
        }
        total += t.elapsed_ms();
    }
    lcf_sink(sum);
    print_perf("reader 容忍模式", count, total);
}

int main()
{
    print_section(1, "ECS 序列化性能测试");

    print_sub("JSON 基础读写");
    bench_json_writer_basic(10000);
    bench_json_reader_basic(10000);
    bench_json_writer_large_array(200);
    bench_json_reader_large_array(200);

    print_sub("ECS 序列化 (1000 实体)");
    bench_save_trivial(1000, 200);
    bench_load_trivial(1000, 200);
    bench_save_json_method(1000, 200);
    bench_load_json_method(1000, 200);
    bench_round_trip_multi(1000, 200);

    print_sub("ECS 序列化 (10000 实体)");
    bench_save_trivial(10000, 50);
    bench_load_trivial(10000, 50);
    bench_save_json_method(10000, 50);
    bench_load_json_method(10000, 50);
    bench_round_trip_multi(10000, 50);

    print_section(2, "JSON 极端性能测试");

    print_sub("大数据量吞吐");
    bench_extreme_writer_1m_ints(5);
    bench_extreme_reader_1m_ints(5);
    bench_extreme_writer_100k_objects(10);
    bench_extreme_reader_100k_objects(10);
    bench_extreme_writer_1m_tiny_objects(5);

    print_sub("大字符串/深嵌套");
    bench_extreme_writer_1mb_string(50);
    bench_extreme_reader_1mb_string(50);
    bench_extreme_writer_deep_nesting(1000);
    bench_extreme_reader_deep_nesting(1000);

    print_sub("浮点数/转义密集");
    bench_extreme_writer_floats(20);
    bench_extreme_reader_floats(20);
    bench_extreme_writer_escaped(100);
    bench_extreme_reader_escaped(100);

    print_sub("容忍模式开销");
    bench_extreme_reader_tolerant(10000);

    print_summary("性能测试");
    return 0;
}
