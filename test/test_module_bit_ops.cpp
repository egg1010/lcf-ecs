// test_module_bit_ops.cpp - 模块功能测试: 位运算与取整
#include "test_common.hpp"
#include "include/part/bit_ops.hpp"

#include <cstddef>

int main()
{
    t_begin("test_module_bit_ops — 模块功能测试: bit_ops");

    t_section(1, "对齐");
    {
        t_check("0 对齐到 8", align_up(0, 8) == 0);
        t_check("1 对齐到 8", align_up(1, 8) == 8);
        t_check("8 对齐到 8", align_up(8, 8) == 8);
        t_check("9 对齐到 8", align_up(9, 8) == 16);
        t_check("非 2 的幂对齐", align_up(10, 6) == 12);
        t_check("对齐为 0 原值返回", align_up(10, 0) == 10);
    }

    t_section(2, "2 的幂判断");
    {
        t_check("1 是 2 的幂", is_power_of_two(1));
        t_check("64 是 2 的幂", is_power_of_two(64));
        t_check("0 不是", !is_power_of_two(0));
        t_check("63 不是", !is_power_of_two(63));
    }

    t_section(3, "向上取 2 的幂");
    {
        t_check("0 → 1", round_up_power_of_two(0) == 1);
        t_check("1 → 1", round_up_power_of_two(1) == 1);
        t_check("3 → 4", round_up_power_of_two(3) == 4);
        t_check("4 → 4", round_up_power_of_two(4) == 4);
        t_check("5 → 8", round_up_power_of_two(5) == 8);
        t_check("1000 → 1024", round_up_power_of_two(1000) == 1024);
        t_check("结果均为 2 的幂", is_power_of_two(round_up_power_of_two(12345)));
    }

    t_section(4, "位运算与随机");
    {
        t_check("尾零计数", count_trailing_zeros(8) == 3);
        t_check("零的尾零为 64", count_trailing_zeros(0) == 64);
        t_check("前导零计数", count_leading_zeros(1) == 63);
        t_check("置位计数", popcount(0xF0u) == 4);
        t_check("位混合确定性", mix_bits(1) == mix_bits(1));
        fast_rand rng{12345};
        t_check("随机数非零", rng.next() != 0);
    }

    return t_end("test_module_bit_ops");
}
