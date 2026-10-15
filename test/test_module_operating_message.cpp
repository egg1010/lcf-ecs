// test_module_operating_message.cpp - 模块功能测试: operating_message
#include "test_common.hpp"
#include "include/part/operating_message.hpp"
#include <string_view>

int main()
{
    t_begin("test_module_operating_message — 模块功能测试: operating_message");

    t_section(1, "成功/失败状态");
    {
        operating_message om;
        t_check("默认成功", static_cast<bool>(om));
        t_check("默认消息空", om.read_message().empty());

        om.write(false, "something failed");
        t_check("write(false) 置失败", !static_cast<bool>(om));
        t_check("失败写入内容", om.read_message().find("something failed") != std::string_view::npos);

        om.reset();
        t_check("reset 恢复成功", static_cast<bool>(om) && om.read_message().empty());
    }

    t_section(2, "写入与格式化");
    {
        operating_message om;
        om.write("hello ", 42);
        std::string_view msg = om.read_message();
        t_check("写入字符串与整数", msg.find("hello 42") != std::string_view::npos);

        operating_message om2;
        om2.write(msg::fmt("x={} y={}", 1, 2));
        t_check("编译期格式化", om2.read_message().find("x=1 y=2") != std::string_view::npos);
    }

    t_section(3, "错误码");
    {
        operating_message om;
        om.write(false, om_err_invalid_entity);
        t_check("错误码生效", om.is_code(om_err_invalid_entity));
        t_check("code() 非 0", om.code() != 0);

        operating_message ok;
        ok.write("success");
        t_check("成功 code 为 0", ok.code() == 0);
    }

    t_section(4, "check 前置条件");
    {
        operating_message om;
        bool r1 = om.check(true, "should not appear");
        t_check("check(true) 返回真", r1);
        t_check("check(true) 不写入", om.read_message().empty());

        bool r2 = om.check(false, "condition failed");
        t_check("check(false) 返回假", !r2);
        t_check("check(false) 写入", om.read_message().find("condition failed") != std::string_view::npos);
    }

    t_section(5, "粘性失败");
    {
        operating_message om;
        om.write(false, "first");
        om.write(true, "second");
        t_check("失败粘性保持", !static_cast<bool>(om));
    }

    t_section(6, "容量与大消息");
    {
        operating_message om;
        std::string big(200, 'A');
        om.write(big);
        t_check("大消息写入", om.read_message().size() >= 200);
        t_check("消息记录开关默认为真", message_recording_enabled());
    }

    return t_end("operating_message ");
}
