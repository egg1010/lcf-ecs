// test_string_to_code_utf8_view.cpp - string_to_code 统一 utf8_view 接口验证
// 验证: utf8_view 直接构造, 以及 const char*/string_view/string 隐式转 utf8_view 的路径
#include "include/part/string_to_code.hpp"
#include <cstdio>
#include <string>
#include <string_view>

using namespace string_to_code;

static int g_pass = 0;
static int g_fail = 0;

static void check(const char* desc, bool ok)
{
    ++(ok ? g_pass : g_fail);
    std::printf("[%s] %s\n", ok ? "OK" : "FAIL", desc);
}

int main()
{
    // === 直接 utf8_view 构造 ===
    // 短串
    {
        utf8_view sv("player");
        code_value v(sv);
        check("utf8_view short construct", v.is_inline());
        check("utf8_view short inline_value", v.inline_value() != 0);
        check("utf8_view short decode", v.decode() == "player");
        check("utf8_view short size", v.string_size() == 6);
    }

    // 长串
    {
        utf8_view sv("this_is_a_long_string_for_test");
        code_value v(sv);
        check("utf8_view long construct", !v.is_inline());
        check("utf8_view long decode", v.decode() == "this_is_a_long_string_for_test");
        check("utf8_view long size", v.string_size() == 30);
    }

    // 空串
    {
        utf8_view sv("");
        code_value v(sv);
        check("utf8_view empty", v.empty());
        check("utf8_view empty decode", v.decode().empty());
    }

    // === encode 自由函数 ===
    {
        utf8_view sv("hello");
        auto v = encode(sv);
        check("encode utf8_view short", v.is_inline());
        check("encode utf8_view decode", v.decode() == "hello");
    }

    {
        utf8_view sv("a_very_long_string_exceeding_8_bytes");
        auto v = encode(sv);
        check("encode utf8_view long", !v.is_inline());
        check("encode utf8_view decode", v.decode() == "a_very_long_string_exceeding_8_bytes");
    }

    // === 隐式转换: const char* → utf8_view → code_value ===
    {
        code_value v("direct_char_ptr");
        check("const char* implicit construct", v.decode() == "direct_char_ptr");
    }

    // === 隐式转换: string_view → utf8_view → code_value ===
    {
        code_value v{std::string_view("from_string_view")};
        check("string_view implicit construct", v.decode() == "from_string_view");
    }

    // === 隐式转换: std::string → utf8_view → code_value ===
    {
        std::string s("from_std_string");
        code_value v{utf8_view(s)};
        check("std::string implicit construct", v.decode() == "from_std_string");
    }

    // === 不同来源编码结果一致 ===
    {
        const char* s = "same_string";
        code_value v_cstr(s);
        code_value v_sv{std::string_view(s)};
        utf8_view uv(s);
        code_value v_uv(uv);
        check("all sources equal", v_cstr.equals(v_uv) && v_sv.equals(v_uv));
    }

    // === decode 返回 utf8_view ===
    {
        code_value v("decode_test");
        utf8_view decoded = v.decode();
        check("decode returns utf8_view", decoded.byte_size() == 11);
        check("decode utf8_view byte_at", decoded.byte_at(0) == 'd');
    }

    // === 中文 UTF-8 ===
    {
        utf8_view sv("你好世界");
        code_value v(sv);
        check("utf8_view chinese decode", v.decode() == "你好世界");
        check("utf8_view chinese byte_size", v.string_size() == 12);
    }

    std::printf("\n==== 结果: %d 通过, %d 失败 ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
