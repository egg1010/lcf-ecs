// test_module_operating_message.cpp - 模块功能测试: operating_message
#include "test_common.hpp"
#include "include/part/operating_message.hpp"
#include "include/part/om_extensions.hpp"
#include <string>
#include <string_view>
#include <type_traits>

int main()
{
    t_begin("test_module_operating_message — 模块功能测试: operating_message");

    t_section(1, "对象形态");
    {
        t_check("可平凡复制", std::is_trivially_copyable_v<operating_message>);
        t_check("大小不超过 128 字节", sizeof(operating_message) <= 128);
        t_check("文本容量为 64", om_text_capacity == 64);
    }

    t_section(2, "成功/失败状态");
    {
        operating_message om;
        t_check("默认成功", static_cast<bool>(om));
        t_check("默认文本空", om.text().empty());
        t_check("默认文本模式 off", om.text_mode() == om_text_mode::off);

        om.write(false, "something failed");
        t_check("write(false) 置失败", !static_cast<bool>(om));
        t_check("off 模式不产生文本", om.text().empty());

        om.reset();
        t_check("reset 恢复成功", static_cast<bool>(om) && om.text().empty());
    }

    t_section(3, "文本已移出对象");
    {
        operating_message om;
        om.write(false, "plain text");
        t_check("文本不再内联存储", om.text().empty());
        t_check("截断标志恒为假", !om.truncated());
        t_check("消息长度恒为 0", om.message_size() == 0);

        om.reset();
        om.write(msg::fmt("x={} y={}", 1, 2));
        t_check("格式化参数不影响状态", om.text().empty());
    }

    t_section(4, "写入与对象大小");
    {
        operating_message om;
        om.write("hello ", 42);
        t_check("非标签参数被忽略", om.text().empty() && static_cast<bool>(om));

        std::string big(200, 'A');
        om.write(big);
        t_check("超长文本不再截断", om.text().empty() && !om.truncated());

        om.clear_message();
        t_check("清空后状态不变", static_cast<bool>(om));

        static_assert(sizeof(operating_message) == 2, "错误对象应为 2 字节");
        t_check("对象大小为 2 字节", sizeof(operating_message) == 2);
    }

    t_section(5, "错误码与诊断字段");
    {
        operating_message om;
        om.write_failure(om_err_invalid_entity.code, msg_level::warn, msg::here());
        t_check("失败状态", !static_cast<bool>(om));
        t_check("错误码生效", om.is_code(om_err_invalid_entity));
        t_check("级别生效", om.level() == msg_level::warn);
        t_check("off 模式失败无文本", om.text().empty());

        operating_message ok;
        ok.write("success");
        t_check("成功 code 为 0", ok.code() == 0);

        const msg::code_tag codes[] = {
            om_err_type_mismatch, om_err_invalid_entity, om_err_version_mismatch,
            om_err_out_of_range, om_err_null_pointer, om_err_capacity_exceeded,
            om_err_not_found, om_err_already_exists, om_err_out_of_memory,
            om_err_invalid_argument, om_err_unsupported_operation, om_err_cancelled,
            om_err_timeout, om_err_not_running, om_err_shutting_down,
            om_err_queue_full, om_err_decode_failed, om_err_io_failed,
            om_err_checksum_failed, om_err_spawn_failed,
        };
        const size_t code_count = sizeof(codes) / sizeof(codes[0]);
        bool all_positive = true;
        bool all_distinct = true;
        bool all_named = true;
        for (size_t i = 0; i < code_count; ++i)
        {
            all_positive = all_positive && codes[i].code != 0 && codes[i].code <= 0x7FFFFFFFu;
            all_named = all_named && om_error_name(codes[i].code) != "unknown";
            for (size_t j = i + 1; j < code_count; ++j)
            {
                all_distinct = all_distinct && codes[i].code != codes[j].code;
            }
        }
        t_check("错误码落在正 int32 范围", all_positive);
        t_check("错误码互不重复", all_distinct);
        t_check("错误码均有名称", all_named);
        t_check("目录去重检查通过", om_catalog_codes_unique());
    }

    t_section(6, "首错工具");
    {
        operating_message first;
        t_check("写入首错", om_write_first_failure(
            &first, true, msg_level::warn, om_err_queue_full, msg::here()));
        t_check("首错码正确", first.is_code(om_err_queue_full));

        t_check("后续失败不覆盖", !om_write_first_failure(
            &first, true, msg_level::error, om_err_timeout, msg::here()));
        t_check("首错码保持", first.is_code(om_err_queue_full));

        t_check("空目标不写", !om_write_first_failure(
            nullptr, true, msg_level::error, om_err_timeout, msg::here()));

        operating_message src;
        src.write_failure(om_err_io_failed.code, msg_level::error, msg::here());
        operating_message dst;
        t_check("合并到成功目标", om_merge_first_failure(dst, src));
        t_check("合并后码正确", dst.is_code(om_err_io_failed));
        t_check("目标已失败不覆盖", !om_merge_first_failure(dst, src));
        t_check("合并后码保持", dst.is_code(om_err_io_failed));
    }

    t_section(7, "check 前置条件");
    {
        operating_message om;
        om.set_text_mode(om_text_mode::plain);
        bool r1 = om.check(true, "should not appear");
        t_check("check(true) 返回真", r1);
        t_check("check(true) 不写入", om.text().empty());

        bool r2 = om.check(false, "condition failed");
        t_check("check(false) 返回假", !r2);
        t_check("check(false) 置失败", !static_cast<bool>(om));
    }

    t_section(8, "粘性失败");
    {
        operating_message om;
        om.write(false, om_err_timeout, "first");
        om.write(true, "second");
        t_check("失败粘性保持", !static_cast<bool>(om));
        t_check("首错码保持", om.is_code(om_err_timeout));
    }

    return t_end("operating_message ");
}
