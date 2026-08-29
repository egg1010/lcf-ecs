// test_string_to_code.cpp - 字符串数字码功能测试
// 使用 operating_message 测试断言宏辅助验证

#include "include/part/string_to_code.hpp"
#include "include/part/operating_message.hpp"

#include <cstdio>
#include <string>
#include <string_view>

using namespace string_to_code;

// 全局统计
static int g_pass = 0;
static int g_fail = 0;

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
    std::printf("[%s] %s\n", ok ? "OK" : "FAIL", desc);
}

// 测试: 空字符串
static void test_empty()
{
    std::printf("=== test_empty ===\n");
    code_value v;
    print_item("默认构造 empty", v.empty());
    print_item("默认构造 is_inline", v.is_inline());
    print_item("默认构造 string_size=0", v.string_size() == 0);
    print_item("默认构造 decode 为空", v.decode().empty());

    code_value v2(std::string_view{});
    print_item("空串编码 empty", v2.empty());
    print_item("空串编码 is_inline", v2.is_inline());
    print_item("空串编码 decode 为空", v2.decode().empty());
    std::printf("\n");
}

// 测试: 短串 (1-8 字节)
static void test_short_string()
{
    std::printf("=== test_short_string ===\n");
    // 1 字节
    {
        code_value v("a");
        print_item("1字节 is_inline", v.is_inline());
        print_item("1字节 string_size=1", v.string_size() == 1);
        print_item("1字节 decode='a'", v.decode() == "a");
        print_item("1字节 not empty", !v.empty());
    }
    // 4 字节
    {
        code_value v("test");
        print_item("4字节 is_inline", v.is_inline());
        print_item("4字节 string_size=4", v.string_size() == 4);
        print_item("4字节 decode='test'", v.decode() == "test");
    }
    // 8 字节 (边界)
    {
        code_value v("12345678");
        print_item("8字节 is_inline", v.is_inline());
        print_item("8字节 string_size=8", v.string_size() == 8);
        print_item("8字节 decode='12345678'", v.decode() == "12345678");
    }
    // 含 \0 的短串
    {
        char buf[] = {'a', '\0', 'b', '\0'};
        code_value v(std::string_view(buf, 4));
        print_item("含\\0 短串 is_inline", v.is_inline());
        print_item("含\\0 短串 string_size=4", v.string_size() == 4);
        std::string_view decoded = v.decode();
        print_item("含\\0 短串 decode 长度=4", decoded.size() == 4);
        print_item("含\\0 短串 decode[0]='a'", decoded[0] == 'a');
        print_item("含\\0 短串 decode[1]='\\0'", decoded[1] == '\0');
        print_item("含\\0 短串 decode[2]='b'", decoded[2] == 'b');
        print_item("含\\0 短串 decode[3]='\\0'", decoded[3] == '\0');
    }
    std::printf("\n");
}

// 测试: 长串 (>8 字节)
static void test_long_string()
{
    std::printf("=== test_long_string ===\n");
    // 9 字节 (边界)
    {
        code_value v("123456789");
        print_item("9字节 not is_inline", !v.is_inline());
        print_item("9字节 string_size=9", v.string_size() == 9);
        print_item("9字节 decode='123456789'", v.decode() == "123456789");
    }
    // 16 字节 (整块)
    {
        code_value v("0123456789abcdef");
        print_item("16字节 not is_inline", !v.is_inline());
        print_item("16字节 string_size=16", v.string_size() == 16);
        print_item("16字节 decode='0123456789abcdef'", v.decode() == "0123456789abcdef");
    }
    // 20 字节 (非整块)
    {
        code_value v("0123456789abcdefghij");
        print_item("20字节 not is_inline", !v.is_inline());
        print_item("20字节 string_size=20", v.string_size() == 20);
        print_item("20字节 decode='0123456789abcdefghij'", v.decode() == "0123456789abcdefghij");
    }
    // 含 \0 的长串
    {
        std::string s = std::string("hello") + '\0' + std::string("world_extra");
        code_value v(s);
        print_item("含\\0 长串 not is_inline", !v.is_inline());
        print_item("含\\0 长串 string_size 正确", v.string_size() == s.size());
        std::string_view decoded = v.decode();
        print_item("含\\0 长串 decode 长度正确", decoded.size() == s.size());
        print_item("含\\0 长串 decode 内容正确",
            std::memcmp(decoded.data(), s.data(), s.size()) == 0);
    }
    // 非常长的字符串
    {
        std::string long_s(1000, 'x');
        code_value v(long_s);
        print_item("1000字节 not is_inline", !v.is_inline());
        print_item("1000字节 string_size=1000", v.string_size() == 1000);
        std::string_view decoded = v.decode();
        print_item("1000字节 decode 长度=1000", decoded.size() == 1000);
        print_item("1000字节 decode 内容正确", decoded == long_s);
    }
    std::printf("\n");
}

// 测试: 可逆性 (编码→解码→验证)
static void test_reversibility()
{
    std::printf("=== test_reversibility ===\n");
    const char* test_strings[] = {
        "",
        "a",
        "ab",
        "abc",
        "abcd",
        "abcde",
        "abcdef",
        "abcdefg",
        "abcdefgh",  // 8 字节
        "abcdefghi", // 9 字节
        "Hello, World!",
        "12345678901234567890",
        "这是一个中文字符串测试",
        ""
    };

    for (size_t i = 0; test_strings[i][0] != '\0' || i == 0; ++i)
    {
        std::string_view original(test_strings[i]);
        code_value v(original);
        std::string_view decoded = v.decode();
        char desc[128];
        std::snprintf(desc, sizeof(desc), "可逆性测试 len=%zu ('%.*s')",
            original.size(),
            static_cast<int>(original.size() > 20 ? 20 : original.size()),
            original.data());
        print_item(desc, decoded == original);
        if (i > 0 && test_strings[i][0] == '\0')
        {
            break;
        }
    }
    std::printf("\n");
}

// 测试: 无冲突 (不同字符串产生不同码)
static void test_no_collision()
{
    std::printf("=== test_no_collision ===\n");
    // 短串无冲突
    {
        code_value v1("abc");
        code_value v2("abd");
        print_item("短串 'abc' != 'abd'", !v1.equals(v2));
    }
    // 长串无冲突
    {
        code_value v1("1234567890");
        code_value v2("1234567891");
        print_item("长串 差1字节不相等", !v1.equals(v2));
    }
    // 前缀相同长度不同
    {
        code_value v1("abc");
        code_value v2("abcd");
        print_item("'abc' != 'abcd' (长度不同)", !v1.equals(v2));
    }
    // 空串 vs 非空串
    {
        code_value v1;
        code_value v2("a");
        print_item("空串 != 'a'", !v1.equals(v2));
    }
    // 短串 vs 长串 (前8字节相同)
    {
        code_value v1("12345678");
        code_value v2("123456789");
        print_item("短串 != 长串 (前8字节相同)", !v1.equals(v2));
    }
    // 含 \0 的字符串
    {
        char buf1[] = {'a', '\0', 'b'};
        char buf2[] = {'a', '\0', 'c'};
        code_value v1(std::string_view(buf1, 3));
        code_value v2(std::string_view(buf2, 3));
        print_item("含\\0 串 差1字节不相等", !v1.equals(v2));
    }
    std::printf("\n");
}

// 测试: 等价比较 (相同字符串产生相等码)
static void test_equals()
{
    std::printf("=== test_equals ===\n");
    // 短串自等
    {
        code_value v1("test");
        code_value v2("test");
        print_item("短串相同 equals", v1.equals(v2));
    }
    // 长串自等
    {
        code_value v1("this_is_long_string");
        code_value v2("this_is_long_string");
        print_item("长串相同 equals", v1.equals(v2));
    }
    // 空串自等
    {
        code_value v1;
        code_value v2;
        print_item("空串相同 equals", v1.equals(v2));
    }
    // 自等
    {
        code_value v("test_string");
        print_item("自等 equals", v.equals(v));
    }
    std::printf("\n");
}

// 测试: 移动语义
static void test_move()
{
    std::printf("=== test_move ===\n");
    // 移动构造
    {
        code_value v1("movable_string");
        code_value v2(std::move(v1));
        print_item("移动构造: 新对象 decode 正确",
            v2.decode() == "movable_string");
        print_item("移动构造: 源对象 empty", v1.empty());
    }
    // 移动赋值
    {
        code_value v1("assign_me_long");
        code_value v2;
        v2 = std::move(v1);
        print_item("移动赋值: 新对象 decode 正确",
            v2.decode() == "assign_me_long");
        print_item("移动赋值: 源对象 empty", v1.empty());
    }
    // 短串移动
    {
        code_value v1("short");
        code_value v2(std::move(v1));
        print_item("短串移动: 新对象 decode='short'",
            v2.decode() == "short");
        print_item("短串移动: 新对象 is_inline", v2.is_inline());
        print_item("短串移动: 源对象 empty", v1.empty());
    }
    // 自赋值
    {
        code_value v1("self_assign_long");
        v1 = std::move(v1);
        print_item("自移动赋值: 不损坏", v1.decode() == "self_assign_long");
    }
    std::printf("\n");
}

// 测试: inline_value 用于 map key
static void test_inline_value_as_key()
{
    std::printf("=== test_inline_value_as_key ===\n");
    code_value v1("player");
    code_value v2("player");
    code_value v3("enemy1");

    print_item("相同短串 inline_value 相同",
        v1.inline_value() == v2.inline_value());
    print_item("不同短串 inline_value 不同",
        v1.inline_value() != v3.inline_value());
    print_item("inline_value 可直接做 key",
        v1.inline_value() == v2.inline_value() && v1.equals(v2));
    std::printf("\n");
}

// 测试: 使用 operating_message 辅助验证
static void test_with_operating_message()
{
    std::printf("=== test_with_operating_message ===\n");
    // 用 operating_message 包装编码操作, 验证可逆性
    operating_message om;

    std::string_view test_str = "operating_message_test";
    code_value v(test_str);

    if (v.decode() != test_str)
    {
        om.write(false, "可逆性验证失败: decode 结果与原串不符");
    }
    else
    {
        om.write(true, "可逆性验证成功");
    }

    msg::expect_ok(om);
    print_item("operating_message 验证可逆性", (bool)om);

    // 用错误码验证无冲突
    operating_message om2;
    code_value v1("conflict_test_a");
    code_value v2("conflict_test_b");

    if (v1.equals(v2))
    {
        om2.write(false, om_err_already_exists,
            "无冲突验证失败: 不同字符串产生了相同码");
    }
    else
    {
        om2.write(true, "无冲突验证成功");
    }

    msg::expect_ok(om2);
    print_item("operating_message 验证无冲突", (bool)om2);
    std::printf("\n");
}

// 测试: 特殊字符
static void test_special_chars()
{
    std::printf("=== test_special_chars ===\n");
    // 全 \0 串
    {
        char buf[8] = {0};
        code_value v(std::string_view(buf, 8));
        print_item("全\\0 8字节 is_inline", v.is_inline());
        print_item("全\\0 8字节 decode 长度=8", v.decode().byte_size() == 8);
        print_item("全\\0 8字节 decode[0]=\\0", v.decode().byte_at(0) == '\0');
    }
    // 全 0xFF 串
    {
        char buf[10];
        std::memset(buf, 0xFF, 10);
        code_value v(std::string_view(buf, 10));
        print_item("全0xFF 10字节 not is_inline", !v.is_inline());
        std::string_view decoded = v.decode();
        print_item("全0xFF 10字节 decode 正确",
            std::memcmp(decoded.data(), buf, 10) == 0);
    }
    // 混合字符
    {
        std::string mixed;
        for (int i = 0; i < 256; ++i)
        {
            mixed.push_back(static_cast<char>(i));
        }
        code_value v(mixed);
        print_item("256全字符 not is_inline", !v.is_inline());
        print_item("256全字符 decode 正确", v.decode().byte_view() == mixed);
    }
    std::printf("\n");
}

int main()
{
    std::printf("============================================================\n");
    std::printf("  string_to_code 字符串数字码功能测试\n");
    std::printf("============================================================\n\n");

    test_empty();
    test_short_string();
    test_long_string();
    test_reversibility();
    test_no_collision();
    test_equals();
    test_move();
    test_inline_value_as_key();
    test_with_operating_message();
    test_special_chars();

    std::printf("============================================================\n");
    std::printf("  PASS: %d, FAIL: %d\n", g_pass, g_fail);
    std::printf("============================================================\n");
    return g_fail == 0 ? 0 : 1;
}
