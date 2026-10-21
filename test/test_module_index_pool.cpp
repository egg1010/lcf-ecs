// test_module_index_pool.cpp - 模块功能测试: 无锁索引池
#include "test_common.hpp"
#include "include/part/index_pool.hpp"

#include <cstdint>

int main()
{
    t_begin("test_module_index_pool — 模块功能测试: index_pool");

    t_section(1, "创建与容量");
    {
        index_pool pool;
        t_check("创建索引池", pool.create(4));
        t_check("容量为 4", pool.capacity() == 4);
        t_check("初始全部可用", pool.available() == 4);
        t_check("已创建", pool.created());
        t_check("重复创建被拒绝", !pool.create(8));
    }

    t_section(2, "分配与耗尽");
    {
        index_pool pool;
        (void)pool.create(4);
        std::uint32_t ids[4] = {index_pool::none, index_pool::none, index_pool::none,
                                index_pool::none};
        bool ok = true;
        for (int i = 0; i < 4; ++i)
        {
            ok = ok && pool.allocate(ids[i]);
        }
        t_check("全部分配成功", ok);
        t_check("索引互不相同", ids[0] != ids[1] && ids[1] != ids[2] && ids[2] != ids[3] &&
                                    ids[0] != ids[2] && ids[0] != ids[3] && ids[1] != ids[3]);
        t_check("索引在范围内", ids[0] < 4 && ids[1] < 4 && ids[2] < 4 && ids[3] < 4);
        t_check("耗尽后可用为 0", pool.available() == 0);

        std::uint32_t extra = index_pool::none;
        t_check("耗尽后分配失败", !pool.allocate(extra));
        t_check("失败不写出索引", extra == index_pool::none);
    }

    t_section(3, "释放与复用");
    {
        index_pool pool;
        (void)pool.create(2);
        std::uint32_t first = index_pool::none;
        std::uint32_t second = index_pool::none;
        (void)pool.allocate(first);
        (void)pool.allocate(second);
        pool.release(first);
        t_check("释放后可用 1", pool.available() == 1);
        std::uint32_t reused = index_pool::none;
        t_check("再次分配成功", pool.allocate(reused));
        t_check("复用了刚释放的索引", reused == first);
        pool.release(second);

        pool.release(99);
        t_check("越界释放不影响计数", pool.available() == 1);
    }

    t_section(4, "销毁");
    {
        index_pool pool;
        (void)pool.create(2);
        std::uint32_t id = index_pool::none;
        (void)pool.allocate(id);
        pool.destroy();
        t_check("销毁后未创建", !pool.created());
        t_check("销毁后可用为 0", pool.available() == 0);
        t_check("销毁后可重新创建", pool.create(3));
    }

    t_section(5, "打包编码");
    {
        const std::uint64_t head = index_pool::pack(7, 3);
        t_check("next 往返", index_pool::next_of(head) == 7);
        t_check("generation 往返", index_pool::generation_of(head) == 3);
        t_check("空哨兵可识别", index_pool::next_of(index_pool::pack(index_pool::none, 1)) ==
                                     index_pool::none);
    }

    return t_end("test_module_index_pool");
}
