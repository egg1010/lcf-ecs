// test_module_type_id.cpp - 模块功能测试: type_id
#include "test_common.hpp"
#include "include/part/type_id.hpp"
#include <string_view>

struct ta
{
    int x;
};

struct tb
{
    double y;
};

int main()
{
    t_begin("test_module_type_id — 模块功能测试: type_id");

    t_section(1, "类型 id 分配");
    {
        int ida = type_id::get_type_id<ta>();
        int idb = type_id::get_type_id<tb>();
        t_check("非负 id", ida >= 0 && idb >= 0);
        t_check("不同类型 id 不同", ida != idb);
        t_check("同一类型 id 稳定", type_id::get_type_id<ta>() == ida);
    }

    t_section(2, "掩码位映射自洽");
    {
        int ida = type_id::get_type_id<ta>();
        uint32_t blk = type_id::mask_block_of(ida);
        uint32_t off = type_id::mask_offset_of(ida);
        t_check("mask_type_of 反算一致", type_id::mask_type_of(blk, off) == ida);
    }

    t_section(3, "稳定名绑定与反查");
    {
        int ida = type_id::get_type_id<ta>();
        bool ok = type_id::bind_def_name("TestTa", ida);
        t_check("bind_def_name 成功", ok);
        t_check("get_def_type_id 反查", type_id::get_def_type_id("TestTa") == ida);
        t_check("get_def_type_name 正查", type_id::get_def_type_name(ida) == "TestTa");
        t_check("未绑定名返回 -1", type_id::get_def_type_id("NoSuchName") < 0);
    }

    t_section(4, "type_def 查询");
    {
        // 未注册 type_def 的 id 返回 nullptr 属预期; 无效 id 必须为 null
        t_check("无效 id get_type_def 返回 null", type_id::get_type_def(-1) == nullptr);
    }

    t_section(5, "current_max_id");
    {
        int m0 = type_id::current_max_id();
        t_check("max_id 非负", m0 >= 0);
        (void)type_id::get_type_id<ta>();
        int m1 = type_id::current_max_id();
        t_check("max_id 单调不减", m1 >= m0);
    }

    return t_end("type_id ");
}
