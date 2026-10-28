// test_perf_part_generic.cpp - 性能测试: part 通用设施
#include "test_common.hpp"
#include "include/part/bit_ops.hpp"
#include "include/part/id_table.hpp"
#include "include/part/index_pool.hpp"
#include "include/part/keyed_throttle.hpp"
#include "include/part/type/inline_callable.hpp"
#include "include/part/time/timer.hpp"

#include <cstdint>

namespace
{
struct entry
{
    int id{0};
    int value{0};
};

int g_sink = 0;
} // namespace

int main()
{
    t_begin("test_perf_part_generic — 性能测试: part 通用设施 (百万为单位)");

    t_section(1, "索引池");
    {
        constexpr std::uint64_t rounds = 200000;
        index_pool pool;
        (void)pool.create(1024);
        t_bench("index_pool alloc+free", rounds, [&pool]()
        {
            for (std::uint64_t i = 0; i < rounds; ++i)
            {
                std::uint32_t slot = index_pool::none;
                if (pool.allocate(slot))
                {
                    pool.release(slot);
                }
            }
        }, 3);
    }

    t_section(2, "id 表");
    {
        constexpr std::uint64_t rounds = 200000;
        id_table<entry> table;
        for (int i = 0; i < 1024; ++i)
        {
            (void)table.ensure(i, entry{i, i});
        }
        t_bench("id_table find", rounds, [&table]()
        {
            for (std::uint64_t i = 0; i < rounds; ++i)
            {
                const entry* found = table.find(static_cast<int>(i & 1023u));
                g_sink += found != nullptr ? found->value : 0;
            }
        }, 3);
    }

    t_section(3, "键控限流");
    {
        constexpr std::uint64_t rounds = 200000;
        keyed_throttle<64> throttle;
        t_bench("keyed_throttle allow", rounds, [&throttle]()
        {
            for (std::uint64_t i = 0; i < rounds; ++i)
            {
                (void)throttle.allow(i & 63u, 1, 1000000);
            }
        }, 3);
    }

    t_section(4, "可调用体");
    {
        constexpr std::uint64_t rounds = 200000;
        inline_callable<> callable;
        (void)callable.bind([]() noexcept { ++g_sink; });
        t_bench("inline_callable invoke", rounds, [&callable]()
        {
            for (std::uint64_t i = 0; i < rounds; ++i)
            {
                callable();
            }
        }, 3);
    }

    t_section(5, "位运算");
    {
        constexpr std::uint64_t rounds = 200000;
        t_bench("mix_bits", rounds, []()
        {
            std::uint64_t value = 1;
            for (std::uint64_t i = 0; i < rounds; ++i)
            {
                value = mix_bits(value + i);
            }
            g_sink += static_cast<int>(value & 1u);
        }, 3);
    }


    return t_end("part_generic 性能");
}
