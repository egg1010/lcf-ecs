// test_entity_mask_manager_perf.cpp - entity_mask_manager 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/entity_mask_manager.hpp"

using namespace std;

// === Section 1: 容量与块管理 ===
static void test_capacity()
{
    print_header("Section: capacity & block mgmt");
    constexpr int REPEAT = 3;
    constexpr size_t N = 1 << 18;

    // 1.1 默认构造
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_mask_manager m;
            compiler_barrier();
            return m.size();
        });
        print_ns("default ctor", 1, ns);
    }

    // 1.2 移动构造/赋值
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_mask_manager a;
            a.ensure_entity(N);
            entity_mask_manager b = std::move(a);
            compiler_barrier();
            entity_mask_manager c;
            c = std::move(b);
            return c.size();
        });
        print_ns("move ctor/assign", 1, ns);
    }

    // 1.3 reserve_blocks
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_mask_manager m;
            m.reserve_blocks(4);
            compiler_barrier();
            return m.num_blocks();
        });
        print_ns("reserve_blocks", 1, ns);
    }

    // 1.4 ensure_entity
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_mask_manager m;
            m.ensure_entity(N);
            compiler_barrier();
            return m.size();
        });
        print_ns("ensure_entity", N, ns / static_cast<double>(N));
    }

    // 1.5 resize_entities / increase_capacity / reserve_exact
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_mask_manager m;
            m.resize_entities(N);
            compiler_barrier();
            return m.size();
        });
        print_ns("resize_entities", 1, ns);

        ns = best_ns(REPEAT, [&]() {
            entity_mask_manager m;
            m.increase_capacity(N);
            compiler_barrier();
            return m.capacity();
        });
        print_ns("increase_capacity", 1, ns);

        ns = best_ns(REPEAT, [&]() {
            entity_mask_manager m;
            m.reserve_exact(N);
            compiler_barrier();
            return m.capacity();
        });
        print_ns("reserve_exact", 1, ns);
    }

    // 1.6 shrink_to_fit / reduce_capacity
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_mask_manager m;
            m.resize_entities(N);
            m.shrink_to_fit();
            compiler_barrier();
            return m.capacity();
        });
        print_ns("shrink_to_fit", 1, ns);

        ns = best_ns(REPEAT, [&]() {
            entity_mask_manager m;
            m.resize_entities(N);
            m.reduce_capacity(N / 2);
            compiler_barrier();
            return m.capacity();
        });
        print_ns("reduce_capacity", 1, ns);
    }

    // 1.7 clear
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_mask_manager m;
            m.resize_entities(N);
            m.clear();
            compiler_barrier();
            return m.size();
        });
        print_ns("clear", 1, ns);
    }

    print_footer();
}

// === Section 2: bit 操作 ===
static void test_bit_ops()
{
    print_header("Section: bit operations");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 18;
    constexpr size_t OPS = 1000000;

    entity_mask_manager m;
    m.resize_entities(N);
    m.reserve_blocks(4);

    // 2.1 set_bit (带边界检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t v = 0;
            for (size_t i = 0; i < OPS; ++i)
                m.set_bit(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint32_t>(i % 64)));
            (void)v;
        });
        print_ns("set_bit", OPS, ns / static_cast<double>(OPS));
    }

    // 2.2 set_bit_no_check
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t v = 0;
            for (size_t i = 0; i < OPS; ++i)
                m.set_bit_no_check(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint32_t>(i % 64)));
            (void)v;
        });
        print_ns("set_bit_no_check", OPS, ns / static_cast<double>(OPS));
    }

    // 2.3 clear_bit / clear_bit_no_check
    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.clear_bit(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint32_t>(i % 64)));
            compiler_barrier();
        });
        print_ns("clear_bit", OPS, ns / static_cast<double>(OPS));

        ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.clear_bit_no_check(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint32_t>(i % 64)));
            compiler_barrier();
        });
        print_ns("clear_bit_no_check", OPS, ns / static_cast<double>(OPS));
    }

    // 2.4 clear_entity
    {
        for (size_t i = 0; i < N; ++i) m.set_bit_no_check(static_cast<uint32_t>(i), 0, i % 64);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < N; ++i) m.clear_entity(static_cast<uint32_t>(i));
            compiler_barrier();
        });
        print_ns("clear_entity", N, ns / static_cast<double>(N));
    }

    // 2.5 get_block
    {
        for (size_t i = 0; i < N; ++i) m.set_bit_no_check(static_cast<uint32_t>(i), 0, i % 64);
        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t s = 0;
            for (size_t i = 0; i < OPS; ++i) s += m.get_block(opaque(static_cast<uint32_t>(i % N)), 0);
            (void)s;
        });
        print_ns("get_block", OPS, ns / static_cast<double>(OPS));
    }

    // 2.6 for_each_set_bit
    {
        for (size_t i = 0; i < 64; ++i) m.set_bit_no_check(0, 0, static_cast<uint32_t>(i));
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t cnt = 0;
            m.for_each_set_bit(0, [&](uint32_t, uint32_t) { ++cnt; });
            compiler_barrier();
            return cnt;
        });
        print_ns("for_each_set_bit", 1, ns);
    }

    print_footer();
}

// === Section 3: 容量查询 ===
static void test_query()
{
    print_header("Section: capacity query");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 18;
    constexpr size_t OPS = 1000000;

    entity_mask_manager m;
    m.resize_entities(N);
    m.reserve_blocks(4);

    // 3.1 size / capacity / empty / num_blocks
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += m.size(); s += m.capacity(); s += m.empty() ? 1 : 0;
                s += m.num_blocks();
            }
            (void)s;
        });
        print_ns("size/cap/empty/num_blocks", OPS, ns / static_cast<double>(OPS));
    }

    // 3.2 size_bytes / capacity_bytes
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += m.size_bytes(); s += m.capacity_bytes();
            }
            (void)s;
        });
        print_ns("size_bytes/capacity_bytes", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  entity_mask_manager 独立性能测试\n";
    cout << "  编译: MinGW GCC 15.2.0 -O3 -std=c++20 -mavx2 -mbmi -mbmi2\n";
    cout << "============================================================\n";

    test_capacity();
    test_bit_ops();
    test_query();

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
