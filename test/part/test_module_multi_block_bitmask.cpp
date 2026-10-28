// test_module_multi_block_bitmask.cpp - 模块功能测试: multi_block_bitmask
#include "test_common.hpp"
#include "include/part/multi_block_bitmask.hpp"

int main()
{
    t_begin("test_module_multi_block_bitmask — 模块功能测试: multi_block_bitmask");

    t_section(1, "设置与测试位");
    {
        multi_block_bitmask bm;
        bm.resize_entities(4);
        t_check("num_blocks >= 1", bm.num_blocks() >= 1);

        bm.set_bit(0, 0, 3);
        t_check("test_bit 命中", bm.test_bit(0, 0, 3));
        t_check("未设位为 0", !bm.test_bit(0, 0, 2));
        t_check("get_block 位值", bm.get_block(0, 0) == (1ULL << 3));

        bm.clear_bit(0, 0, 3);
        t_check("clear_bit 生效", !bm.test_bit(0, 0, 3));
    }

    t_section(2, "遍历置位");
    {
        multi_block_bitmask bm;
        bm.resize_entities(4);
        bm.set_bit(1, 0, 1);
        bm.set_bit(1, 0, 5);
        bm.set_bit(1, 0, 9);

        uint32_t cnt = 0;
        bm.for_each_set_bit(1, [&cnt](uint32_t, uint32_t) { ++cnt; });
        t_check("for_each_set_bit 计数", cnt == 3);

        uint32_t slots = 0;
        bm.for_each_set_slot([&slots](uint32_t) { ++slots; });
        t_check("for_each_set_slot 计数", slots == 1);
    }

    t_section(3, "批量置位/清位");
    {
        multi_block_bitmask bm;
        bm.resize_entities(4);
        uint32_t offs[] = {2, 4, 6};
        bm.set_bits_at(2, 0, std::span<const uint32_t>(offs, 3));
        t_check("set_bits_at 位 2", bm.test_bit(2, 0, 2));
        t_check("set_bits_at 位 6", bm.test_bit(2, 0, 6));

        bm.clear_bits_at(2, 0, std::span<const uint32_t>(offs, 3));
        t_check("clear_bits_at 生效", !bm.test_bit(2, 0, 2) && !bm.test_bit(2, 0, 6));
    }

    t_section(4, "clear_entity 与 clear");
    {
        multi_block_bitmask bm;
        bm.resize_entities(4);
        bm.set_bit(3, 0, 1);
        bm.set_bit(3, 0, 2);
        bm.clear_entity(3);
        t_check("clear_entity 清空 slot", !bm.test_bit(3, 0, 1) && bm.get_block(3, 0) == 0);

        bm.set_bit(0, 0, 7);
        bm.clear();
        t_check("clear 全部清空", !bm.test_bit(0, 0, 7));
    }

    t_section(5, "contains_all 与 swap");
    {
        multi_block_bitmask a;
        multi_block_bitmask b;
        a.resize_entities(2);
        b.resize_entities(2);
        a.set_bit(0, 0, 1);
        a.set_bit(0, 0, 2);
        b.set_bit(0, 0, 1);
        t_check("a contains_all b", a.contains_all(b));
        b.set_bit(0, 0, 5);
        t_check("b 多出位后不包含", !a.contains_all(b));

        multi_block_bitmask c;
        c.resize_entities(2);
        c.swap(a);
        t_check("swap 交换内容", c.test_bit(0, 0, 2));
    }

    t_section(6, "block_count_for_bits");
    {
        t_check("64 位 → 1 块", multi_block_bitmask::block_count_for_bits(64) == 1);
        t_check("65 位 → 2 块", multi_block_bitmask::block_count_for_bits(65) == 2);
    }

    return t_end("multi_block_bitmask ");
}
