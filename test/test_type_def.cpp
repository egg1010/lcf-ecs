// test_type_def.cpp - type_id 运行期名字类型注册 (register_type_def / get_def_type_id) 功能测试
#include "include/part/type_id.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace lcf_test_detail {
    struct console_utf8_init {
        console_utf8_init() noexcept { SetConsoleOutputCP(CP_UTF8); }
    };
    inline console_utf8_init g_console_utf8_inst;
}
#endif

static int g_pass = 0;
static int g_fail = 0;

static void print_section(const char* title)
{
    std::printf("\n==== %s ====\n", title);
}

static void print_item(const char* desc, bool ok)
{
    if (ok)
    {
        ++g_pass;
    }
    else
    {
        ++g_fail;
    }
    std::printf("  [%s] %s\n", ok ? "通过" : "失败", desc);
}

// 非平凡 def 的构造/析构
static int g_ctor_calls = 0;
static int g_dtor_calls = 0;
static void test_construct(void*) noexcept { ++g_ctor_calls; }
static void test_destruct(void*) noexcept { ++g_dtor_calls; }

static type_def make_trivial_def(size_t size, size_t alignment)
{
    type_def d;
    d.size = size;
    d.alignment = alignment;
    d.trivially_copyable = true;
    return d;
}

// === 1. 基础注册与查询 ===
static void test_basic()
{
    print_section("1. 基础注册与查询");
    const type_def d1 = make_trivial_def(12, 4);

    const int id1 = type_id::register_type_def("Position", d1);
    print_item("注册短名 Position 返回 id > 0", id1 > 0);
    print_item("查询 Position 返回同一 id", type_id::get_def_type_id("Position") == id1);
    print_item("重复注册同名同语义返回同一 id",
                type_id::register_type_def("Position", d1) == id1);

    const int id2 = type_id::register_type_def("Velocity", d1);
    print_item("注册不同名返回不同 id", id2 > 0 && id2 != id1);
    print_item("查询 Velocity 返回 id2", type_id::get_def_type_id("Velocity") == id2);
    print_item("查询未注册名返回 -1", type_id::get_def_type_id("Nope9999") == -1);

    // 非平凡 def (带构造/析构)
    type_def d2;
    d2.size = 16;
    d2.alignment = 8;
    d2.trivially_copyable = false;
    d2.construct = test_construct;
    d2.destruct = test_destruct;
    const int id3 = type_id::register_type_def("Matrix4x", d2);
    print_item("注册非平凡 def 返回 id > 0", id3 > 0);
    print_item("查询 Matrix4x 返回 id3", type_id::get_def_type_id("Matrix4x") == id3);
}

// === 2. 边界长度与非法名字 ===
static void test_boundary()
{
    print_section("2. 边界长度与非法名字");
    const type_def d = make_trivial_def(8, 4);

    const int a1 = type_id::register_type_def("A", d);
    const int a7 = type_id::register_type_def("AAAAAAA", d);
    const int a8 = type_id::register_type_def("AAAAAAAA", d);
    const int a9 = type_id::register_type_def("AAAAAAAAA", d);
    print_item("1B/7B/8B/9B 名注册均成功",
                a1 > 0 && a7 > 0 && a8 > 0 && a9 > 0);
    print_item("1B/7B/8B/9B 名 id 互不相同",
                a1 != a7 && a1 != a8 && a1 != a9 && a7 != a8 && a7 != a9 && a8 != a9);
    print_item("1B 名查询正确", type_id::get_def_type_id("A") == a1);
    print_item("7B 名查询正确", type_id::get_def_type_id("AAAAAAA") == a7);
    print_item("8B 名查询正确 (短名边界)", type_id::get_def_type_id("AAAAAAAA") == a8);
    print_item("9B 名查询正确 (长名边界)", type_id::get_def_type_id("AAAAAAAAA") == a9);

    // 同前缀不同长度
    print_item("查询 AAAAAAA 不误中 AAAAAAAA",
                type_id::get_def_type_id("AAAAAAA") == a7);

    // 含 NUL 名字 (string_view 显式长度)
    const std::string_view nul3("AB\0", 3);
    const int nid = type_id::register_type_def(nul3, d);
    print_item("含 NUL 名注册成功", nid > 0);
    print_item("含 NUL 名查询正确", type_id::get_def_type_id(nul3) == nid);

    // 非法名字
    print_item("空名注册返回 -1",
                type_id::register_type_def(std::string_view{}, d) == -1);
    const std::string_view all_nul("\0\0\0", 3);
    print_item("全 NUL 名注册返回 -1",
                type_id::register_type_def(all_nul, d) == -1);
    print_item("全 NUL 名查询返回 -1", type_id::get_def_type_id(all_nul) == -1);
}

// === 3. 同前缀长名碰撞 ===
static void test_prefix_collision()
{
    print_section("3. 同前缀长名碰撞");
    const type_def d = make_trivial_def(4, 4);

    // 前 8 字节相同 + 长度相同, 仅尾部不同
    const char* n1 = "TransformDataAlpha";
    const char* n2 = "TransformDataBravo";
    const int t1 = type_id::register_type_def(n1, d);
    const int t2 = type_id::register_type_def(n2, d);
    print_item("同前缀同长度长名注册均成功", t1 > 0 && t2 > 0);
    print_item("同前缀同长度长名 id 不同", t1 != t2);
    print_item("长名 Alpha 查询正确", type_id::get_def_type_id(n1) == t1);
    print_item("长名 Bravo 查询正确", type_id::get_def_type_id(n2) == t2);

    // 前 8 字节相同 + 长度不同
    const char* n3 = "TransformDataCharlie";
    const int t3 = type_id::register_type_def(n3, d);
    print_item("同前缀不同长度注册成功", t3 > 0 && t3 != t1 && t3 != t2);
    print_item("长名 Charlie 查询正确", type_id::get_def_type_id(n3) == t3);
}

// === 4. 反查 ===
static void test_reverse_lookup()
{
    print_section("4. 反查");
    const type_def d = make_trivial_def(24, 8);
    const int id = type_id::register_type_def("Reverse", d);

    const type_def* q = type_id::get_type_def(id);
    print_item("按 id 反查语义非空", q != nullptr);
    print_item("反查 size == 24", q && q->size == 24);
    print_item("反查 alignment == 8", q && q->alignment == 8);
    print_item("反查 trivially_copyable == true", q && q->trivially_copyable);

    print_item("模板轨道 id 反查语义返回 nullptr",
                type_id::get_type_def(type_id::get_type_id<int>()) == nullptr);
    print_item("id 0 反查语义返回 nullptr", type_id::get_type_def(0) == nullptr);
    print_item("id -1 反查语义返回 nullptr", type_id::get_type_def(-1) == nullptr);

    const std::string_view nm = type_id::get_def_type_name(id);
    print_item("按 id 反查名字 == Reverse",
                nm == std::string_view("Reverse"));
    print_item("反查长名正确",
                type_id::get_def_type_name(
                    type_id::get_def_type_id("TransformDataAlpha"))
                == std::string_view("TransformDataAlpha"));
    print_item("模板轨道 id 反查名字为空",
                type_id::get_def_type_name(type_id::get_type_id<double>()).empty());
}

// === 5. 非法 def ===
static void test_invalid_def()
{
    print_section("5. 非法 def");
    {
        type_def bad = make_trivial_def(0, 4);
        print_item("size == 0 返回 -1",
                    type_id::register_type_def("BadSize", bad) == -1);
    }
    {
        type_def bad = make_trivial_def(8, 0);
        print_item("alignment == 0 返回 -1",
                    type_id::register_type_def("BadAlign0", bad) == -1);
    }
    {
        type_def bad = make_trivial_def(8, 3);
        print_item("alignment 非 2 的幂返回 -1",
                    type_id::register_type_def("BadAlign3", bad) == -1);
    }
    {
        type_def bad = make_trivial_def(8, 4);
        bad.trivially_copyable = false;
        print_item("非平凡缺 construct/destruct 返回 -1",
                    type_id::register_type_def("BadNoCtor", bad) == -1);
    }
}

// === 6. 与模板轨道 id 互斥 ===
static void test_track_exclusive()
{
    print_section("6. 与模板轨道 id 互斥");
    struct LocalType { int x; };

    std::vector<int> ids;
    ids.push_back(type_id::get_type_id<int>());
    ids.push_back(type_id::get_type_id<double>());
    ids.push_back(type_id::get_type_id<LocalType>());
    const type_def d = make_trivial_def(4, 4);
    ids.push_back(type_id::register_type_def("TrackA", d));
    ids.push_back(type_id::register_type_def("TrackB", d));

    std::vector<int> sorted_ids = ids;
    std::sort(sorted_ids.begin(), sorted_ids.end());
    bool all_unique = true;
    for (size_t i = 1; i < sorted_ids.size(); ++i)
    {
        if (sorted_ids[i] == sorted_ids[i - 1])
        {
            all_unique = false;
        }
    }
    print_item("模板 id 与 def id 无冲突", all_unique);

    bool all_le_max = true;
    for (const int id : ids)
    {
        if (id > type_id::current_max_id())
        {
            all_le_max = false;
        }
    }
    print_item("所有 id <= current_max_id", all_le_max);
}

// === 7. 压力: 混合长度批量注册 ===
static void test_stress()
{
    print_section("7. 压力: 混合长度批量注册");
    constexpr int COUNT = 600;
    const type_def d = make_trivial_def(4, 4);

    // 名字长度 5~14, 覆盖短名/长名两路径
    std::vector<std::string> names;
    names.reserve(COUNT);
    for (int i = 0; i < COUNT; ++i)
    {
        std::string base = "T" + std::to_string(i);
        const size_t target = 5 + static_cast<size_t>(i % 10);
        while (base.size() < target)
        {
            base += static_cast<char>('a' + (i + base.size()) % 26);
        }
        names.push_back(base);
    }

    std::vector<int> ids;
    ids.reserve(COUNT);
    bool all_ok = true;
    for (int i = 0; i < COUNT; ++i)
    {
        const int id = type_id::register_type_def(names[static_cast<size_t>(i)], d);
        ids.push_back(id);
        if (id <= 0)
        {
            all_ok = false;
        }
    }
    print_item("600 个混合长度名字注册全部成功", all_ok);

    std::vector<int> sorted_ids = ids;
    std::sort(sorted_ids.begin(), sorted_ids.end());
    bool all_unique = true;
    for (size_t i = 1; i < sorted_ids.size(); ++i)
    {
        if (sorted_ids[i] == sorted_ids[i - 1])
        {
            all_unique = false;
        }
    }
    print_item("600 个 id 全部唯一", all_unique);

    bool all_hit = true;
    for (int i = 0; i < COUNT; ++i)
    {
        if (type_id::get_def_type_id(names[static_cast<size_t>(i)]) != ids[static_cast<size_t>(i)])
        {
            all_hit = false;
            break;
        }
    }
    print_item("600 个名字查询全部命中", all_hit);

    bool all_names = true;
    for (int i = 0; i < COUNT; i += 61)
    {
        const std::string_view nm = type_id::get_def_type_name(ids[static_cast<size_t>(i)]);
        if (nm != std::string_view(names[static_cast<size_t>(i)]))
        {
            all_names = false;
            break;
        }
    }
    print_item("抽检反查名字全部正确", all_names);
}

// === 8. 多线程 ===
static void test_multithread()
{
    print_section("8. 多线程");
    const type_def d = make_trivial_def(8, 4);

    // 并发注册同名: 4 线程同 def
    std::vector<int> results(4, -2);
    std::vector<std::thread> threads;
    for (int j = 0; j < 4; ++j)
    {
        threads.emplace_back([&results, j, &d]() {
            results[static_cast<size_t>(j)] = type_id::register_type_def("MTShared", d);
        });
    }
    for (auto& t : threads)
    {
        t.join();
    }
    print_item("并发注册同名返回相同 id",
                results[0] > 0 && results[0] == results[1]
                && results[0] == results[2] && results[0] == results[3]);

    // 并发注册不同名
    const char* mt_names[4] = {"MTA", "MTBxx", "MTCyyy", "MTDzzzz"};
    std::vector<int> mt_ids(4, -2);
    threads.clear();
    for (int j = 0; j < 4; ++j)
    {
        threads.emplace_back([&mt_ids, j, &d, &mt_names]() {
            mt_ids[static_cast<size_t>(j)] =
                type_id::register_type_def(mt_names[j], d);
        });
    }
    for (auto& t : threads)
    {
        t.join();
    }
    bool mt_ok = mt_ids[0] > 0 && mt_ids[1] > 0 && mt_ids[2] > 0 && mt_ids[3] > 0;
    for (int j = 0; j < 4; ++j)
    {
        if (type_id::get_def_type_id(mt_names[j]) != mt_ids[static_cast<size_t>(j)])
        {
            mt_ok = false;
        }
    }
    print_item("并发注册不同名均正确", mt_ok);

    // 并发读已注册名
    const int shared_id = type_id::get_def_type_id("MTShared");
    std::atomic<bool> read_ok{true};
    threads.clear();
    for (int j = 0; j < 2; ++j)
    {
        threads.emplace_back([&read_ok, shared_id]() {
            for (int i = 0; i < 100000; ++i)
            {
                if (type_id::get_def_type_id("MTShared") != shared_id)
                {
                    read_ok.store(false);
                    break;
                }
            }
        });
    }
    for (auto& t : threads)
    {
        t.join();
    }
    print_item("并发查询 20 万次结果稳定", read_ok.load());
}

// === 9. 名字绑定 (模板类型稳定名/别名 → 既有 id) ===
static void test_name_binding()
{
    print_section("9. 名字绑定 (模板类型 ↔ def 表互通)");
    struct BindType { int x; };

    const int tid = type_id::get_type_id<BindType>();
    print_item("未绑定名查询返回 -1", type_id::get_def_type_id("BindStable") == -1);

    print_item("绑定稳定名成功",
                type_id::bind_def_name("BindStable", tid));
    print_item("绑定后按名查询返回模板 id",
                type_id::get_def_type_id("BindStable") == tid);
    print_item("重复绑定同名同 id 幂等",
                type_id::bind_def_name("BindStable", tid));
    print_item("绑定名反查名字正确",
                type_id::get_def_type_name(tid) == std::string_view("BindStable"));

    // 别名: 多名一 id
    print_item("绑定别名成功",
                type_id::bind_def_name("BindLegacy", tid));
    print_item("别名查询返回同 id",
                type_id::get_def_type_id("BindLegacy") == tid);

    // 冲突: 同名异 id
    const type_def d = make_trivial_def(4, 4);
    const int other = type_id::register_type_def("BindOther", d);
    print_item("同名异 id 绑定返回 false",
                !type_id::bind_def_name("BindOther", tid));
    print_item("冲突绑定不改变原映射",
                type_id::get_def_type_id("BindOther") == other);

    // 绑定条目无 def 存储语义: add_def 应拒绝
    print_item("绑定条目反查语义返回 nullptr",
                type_id::get_type_def(tid) == nullptr);
    print_item("def 条目反查语义非空", type_id::get_type_def(other) != nullptr);

    // def 类型名亦可绑定到自身 id (幂等语义)
    print_item("def 类型自绑定幂等",
                type_id::bind_def_name("BindOther", other));

    // 非法输入
    print_item("空名绑定失败", !type_id::bind_def_name("", tid));
    print_item("非法 id 绑定失败", !type_id::bind_def_name("BindX", 0));
}

int main()
{
    std::printf("============================================================\n");
    std::printf("  type_id 运行期名字类型注册 功能测试\n");
    std::printf("============================================================\n");

    test_basic();
    test_boundary();
    test_prefix_collision();
    test_reverse_lookup();
    test_invalid_def();
    test_track_exclusive();
    test_stress();
    test_multithread();
    test_name_binding();

    std::printf("\n============================================================\n");
    std::printf("  汇总: 通过 %d / 失败 %d / 总计 %d\n",
                g_pass, g_fail, g_pass + g_fail);
    std::printf("  结果: %s\n", g_fail == 0 ? "全部通过" : "存在失败");
    std::printf("============================================================\n");
    return g_fail == 0 ? 0 : 1;
}
