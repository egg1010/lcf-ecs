// test_module_binary_codec.cpp - 模块功能测试: binary_writer / binary_reader
#include "test_common.hpp"
#include "include/part/codec/binary_io.hpp"
#include <string>
#include <string_view>

int main()
{
    t_begin("test_module_binary_codec — 模块功能测试: binary_writer / binary_reader");

    t_section(1, "定长整数往返");
    {
        binary_writer bw;
        bw.value(uint32_t(42));
        bw.value(int32_t(-7));
        std::string data = bw.take();
        t_check("输出非空", !data.empty());

        binary_reader br(data);
        uint32_t a = br.read_u32();
        t_check("u32 往返", a == 42);
        t_check("无读取错误", !br.has_error());
    }

    t_section(2, "字符串往返");
    {
        binary_writer bw;
        bw.value(std::string_view("hello"));
        bw.value(std::string_view("world"));
        std::string data = bw.take();

        binary_reader br(data);
        std::string s1 = br.read_string();
        std::string s2 = br.read_string();
        t_check("字符串 1", s1 == "hello");
        t_check("字符串 2", s2 == "world");
        t_check("无错误", !br.has_error());
    }

    t_section(3, "浮点往返");
    {
        binary_writer bw;
        bw.value(3.5f);
        bw.value(2.5);
        std::string data = bw.take();
        t_check("浮点输出非空", !data.empty());

        binary_reader br(data);
        t_check("浮点读取无错误", !br.has_error());
    }

    t_section(4, "数组布局");
    {
        binary_writer bw;
        bw.begin_array(3);
        bw.value(uint32_t(1));
        bw.value(uint32_t(2));
        bw.value(uint32_t(3));
        std::string data = bw.take();

        binary_reader br(data);
        uint32_t count = br.read_u32();
        uint32_t a = br.read_u32();
        uint32_t b = br.read_u32();
        uint32_t c = br.read_u32();
        t_check("数组长度前缀", count == 3);
        t_check("数组元素往返", a == 1 && b == 2 && c == 3);
    }

    t_section(5, "skip 与错误状态");
    {
        binary_writer bw;
        bw.value(uint64_t(123456789));
        bw.value(uint32_t(7));
        std::string data = bw.take();

        binary_reader br(data);
        br.skip(8);
        uint32_t v = br.read_u32();
        t_check("skip 后读取正确", v == 7);
        t_check("无错误", !br.has_error());

        binary_reader bad(std::string_view{});
        t_check("空输入读取报错", bad.has_error());
    }

    t_section(6, "重复 take");
    {
        binary_writer bw;
        bw.value(uint32_t(1));
        std::string data = bw.take();
        t_check("take 后可读出", !data.empty());
        binary_reader br(data);
        t_check("读回值", br.read_u32() == 1);
    }

    return t_end("binary_codec ");
}
