// test_multi_block_bitmask_perf.cpp - multi_block_bitmask 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/multi_block_bitmask.hpp"

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
            multi_block_bitmask m;
            compiler_barrier();
            return m.size();
        });
        print_ns("default ctor", 1, ns);
    }

    // 1.2 移动构造/赋值
    {
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask a;
            a.ensure_entity(N);
            multi_block_bitmask b = std::move(a);
            compiler_barrier();
            multi_block_bitmask c;
            c = std::move(b);
            return c.size();
        });
        print_ns("move ctor/assign", 1, ns);
    }

    // 1.3 reserve_blocks
    {
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask m;
            m.reserve_blocks(4);
            compiler_barrier();
            return m.num_blocks();
        });
        print_ns("reserve_blocks", 1, ns);
    }

    // 1.4 ensure_entity
    {
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask m;
            m.ensure_entity(N);
            compiler_barrier();
            return m.size();
        });
        print_ns("ensure_entity", N, ns / static_cast<double>(N));
    }

    // 1.5 resize_entities / increase_capacity / reserve_exact
    {
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask m;
            m.resize_entities(N);
            compiler_barrier();
            return m.size();
        });
        print_ns("resize_entities", 1, ns);

        ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask m;
            m.increase_capacity(N);
            compiler_barrier();
            return m.capacity();
        });
        print_ns("increase_capacity", 1, ns);

        ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask m;
            m.reserve_exact(N);
            compiler_barrier();
            return m.capacity();
        });
        print_ns("reserve_exact", 1, ns);
    }

    // 1.6 shrink_to_fit / reduce_capacity
    {
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask m;
            m.resize_entities(N);
            m.shrink_to_fit();
            compiler_barrier();
            return m.capacity();
        });
        print_ns("shrink_to_fit", 1, ns);

        ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask m;
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
            multi_block_bitmask m;
            m.resize_entities(N);
            m.clear();
            compiler_barrier();
            return m.size();
        });
        print_ns("clear", 1, ns);
    }

    // 1.8 拷贝构造 / clone (含 overflow)
    {
        multi_block_bitmask src;
        src.reserve_blocks(2);
        src.resize_entities(N);
        for (size_t i = 0; i < N; ++i)
            src.set_bit_no_check(static_cast<uint32_t>(i), 1, static_cast<uint32_t>(i % 64));

        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask dst(src);
            compiler_barrier();
            return dst.size();
        });
        print_ns("copy ctor (含 overflow)", N, ns / static_cast<double>(N));

        ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask dst = src.clone();
            compiler_barrier();
            return dst.size();
        });
        print_ns("clone (含 overflow)", N, ns / static_cast<double>(N));
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

    multi_block_bitmask m;
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
            m.for_each_set_bit(0, [&](uint32_t, uint32_t) { cnt = cnt + 1; });
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

    multi_block_bitmask m;
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

// === Section 4: 查询接口 (新增) ===
static void test_bit_query()
{
    print_header("Section: bit query (test_bit/any_set/count/find)");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 18;
    constexpr size_t OPS = 1000000;

    multi_block_bitmask m;
    m.resize_entities(N);
    m.reserve_blocks(2);
    // 预填: 每槽 block 0 位 (i%64), block 1 位 (i%64) 模拟典型场景
    for (size_t i = 0; i < N; ++i)
    {
        m.set_bit_no_check(static_cast<uint32_t>(i), 0, static_cast<uint32_t>(i % 64));
        if ((i & 1) == 0)
            m.set_bit_no_check(static_cast<uint32_t>(i), 1, static_cast<uint32_t>((i + 1) % 64));
    }

    // 4.1 test_bit
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i)
                s = m.test_bit(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint32_t>(i % 64)));
            (void)s;
        });
        print_ns("test_bit", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 any_set
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i)
                s = m.any_set(opaque(static_cast<uint32_t>(i % N)));
            (void)s;
        });
        print_ns("any_set", OPS, ns / static_cast<double>(OPS));
    }

    // 4.3 any_set_in_block
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i)
                s = m.any_set_in_block(opaque(static_cast<uint32_t>(i % N)), 0);
            (void)s;
        });
        print_ns("any_set_in_block", OPS, ns / static_cast<double>(OPS));
    }

    // 4.4 count_set_bits
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
                s += m.count_set_bits(opaque(static_cast<uint32_t>(i % N)));
            (void)s;
        });
        print_ns("count_set_bits", OPS, ns / static_cast<double>(OPS));
    }

    // 4.5 find_first_set
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t b = 0, o = 0;
            volatile bool found = false;
            for (size_t i = 0; i < OPS; ++i)
                found = m.find_first_set(opaque(static_cast<uint32_t>(i % N)), b, o);
            compiler_barrier();
            (void)found; (void)b; (void)o;
        });
        print_ns("find_first_set", OPS, ns / static_cast<double>(OPS));
    }

    // 4.6 find_last_set
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t b = 0, o = 0;
            volatile bool found = false;
            for (size_t i = 0; i < OPS; ++i)
                found = m.find_last_set(opaque(static_cast<uint32_t>(i % N)), b, o);
            compiler_barrier();
            (void)found; (void)b; (void)o;
        });
        print_ns("find_last_set", OPS, ns / static_cast<double>(OPS));
    }

    // 4.7 find_next_set
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t b = 0, o = 0;
            volatile bool found = false;
            for (size_t i = 0; i < OPS; ++i)
                found = m.find_next_set(opaque(static_cast<uint32_t>(i % N)), 0, 0, b, o);
            compiler_barrier();
            (void)found; (void)b; (void)o;
        });
        print_ns("find_next_set", OPS, ns / static_cast<double>(OPS));
    }

    // 4.8 is_zero
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i)
                s = m.is_zero(opaque(static_cast<uint32_t>(i % N)));
            (void)s;
        });
        print_ns("is_zero", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: 整块写入与批量位操作 ===
static void test_block_ops()
{
    print_header("Section: block & batch bit ops");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 18;
    constexpr size_t OPS = 1000000;

    multi_block_bitmask m;
    m.resize_entities(N);
    m.reserve_blocks(2);

    // 5.1 set_block_value
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t v = 0;
            for (size_t i = 0; i < OPS; ++i)
                m.set_block_value(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint64_t>(i)));
            (void)v;
        });
        print_ns("set_block_value block 0", OPS, ns / static_cast<double>(OPS));
    }

    // 5.2 set_block_value block 1 (overflow 路径)
    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.set_block_value(opaque(static_cast<uint32_t>(i % N)), 1, static_cast<uint64_t>(i));
            compiler_barrier();
        });
        print_ns("set_block_value block 1", OPS, ns / static_cast<double>(OPS));
    }

    // 5.3 or_block_value
    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.or_block_value(opaque(static_cast<uint32_t>(i % N)), 0, 0x1ULL << (i % 64));
            compiler_barrier();
        });
        print_ns("or_block_value", OPS, ns / static_cast<double>(OPS));
    }

    // 5.4 and_block_value
    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.and_block_value(opaque(static_cast<uint32_t>(i % N)), 0, 0xFFFFULL);
            compiler_barrier();
        });
        print_ns("and_block_value", OPS, ns / static_cast<double>(OPS));
    }

    // 5.5 xor_block_value
    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.xor_block_value(opaque(static_cast<uint32_t>(i % N)), 0, 0xFFULL);
            compiler_barrier();
        });
        print_ns("xor_block_value", OPS, ns / static_cast<double>(OPS));
    }

    // 5.6 set_bits_at (5 位批量)
    {
        uint32_t offsets[] = {1, 3, 5, 7, 9};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.set_bits_at(opaque(static_cast<uint32_t>(i % N)), 0, offsets);
            compiler_barrier();
        });
        print_ns("set_bits_at (5 位)", OPS, ns / static_cast<double>(OPS));
    }

    // 5.7 clear_bits_at (5 位批量)
    {
        uint32_t offsets[] = {1, 3, 5, 7, 9};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.clear_bits_at(opaque(static_cast<uint32_t>(i % N)), 0, offsets);
            compiler_barrier();
        });
        print_ns("clear_bits_at (5 位)", OPS, ns / static_cast<double>(OPS));
    }

    // 5.8 toggle_bits_at (5 位批量)
    {
        uint32_t offsets[] = {1, 3, 5, 7, 9};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.toggle_bits_at(opaque(static_cast<uint32_t>(i % N)), 0, offsets);
            compiler_barrier();
        });
        print_ns("toggle_bits_at (5 位)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 6: 整槽多块读写 ===
static void test_slot_ops()
{
    print_header("Section: slot read/write (assign/copy)");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 16;
    constexpr size_t OPS = 100000;

    multi_block_bitmask m;
    m.resize_entities(N);
    m.reserve_blocks(4);

    // 6.1 assign_slot (4 块)
    {
        uint64_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.assign_slot(opaque(static_cast<uint32_t>(i % N)), data);
            compiler_barrier();
        });
        print_ns("assign_slot (4 块)", OPS, ns / static_cast<double>(OPS));
    }

    // 6.2 copy_slot_to (4 块)
    {
        uint64_t dst[4];
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.copy_slot_to(opaque(static_cast<uint32_t>(i % N)), dst);
            compiler_barrier();
        });
        print_ns("copy_slot_to (4 块)", OPS, ns / static_cast<double>(OPS));
    }

    // 6.3 assign_slot (1 块, 仅 inline)
    {
        uint64_t data[1] = {0xAA};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
                m.assign_slot(opaque(static_cast<uint32_t>(i % N)), data);
            compiler_barrier();
        });
        print_ns("assign_slot (1 块)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 7: 遍历接口 ===
static void test_iter_ops()
{
    print_header("Section: iteration");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 14;

    multi_block_bitmask m;
    m.resize_entities(N);
    m.reserve_blocks(2);

    // 预填: 每 4 个槽位填一组置位
    for (size_t i = 0; i < N; i += 4)
    {
        m.set_bit_no_check(static_cast<uint32_t>(i), 0, 1);
        m.set_bit_no_check(static_cast<uint32_t>(i), 0, 5);
        m.set_bit_no_check(static_cast<uint32_t>(i), 1, 10);
    }

    // 7.1 for_each_set_bit (单槽, 3 位置位)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t cnt = 0;
            for (size_t i = 0; i < N; i += 4)
                m.for_each_set_bit(static_cast<uint32_t>(i), [&](uint32_t, uint32_t) { cnt = cnt + 1; });
            compiler_barrier();
            return cnt;
        });
        print_ns("for_each_set_bit (3 位置位)", N / 4, ns / static_cast<double>(N / 4));
    }

    // 7.2 for_each_set_slot
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t cnt = 0;
            m.for_each_set_slot([&](uint32_t) { cnt = cnt + 1; });
            compiler_barrier();
            return cnt;
        });
        print_ns("for_each_set_slot", 1, ns);
    }

    // 7.3 for_each_set_bit_global
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t cnt = 0;
            m.for_each_set_bit_global([&](uint32_t, uint32_t, uint32_t) { cnt = cnt + 1; });
            compiler_barrier();
            return cnt;
        });
        print_ns("for_each_set_bit_global", 1, ns);
    }

    // 7.4 count_set_bits_global
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = m.count_set_bits_global();
            compiler_barrier();
            return s;
        });
        print_ns("count_set_bits_global", 1, ns);
    }

    print_footer();
}

// === Section 8: 视图接口 ===
static void test_view_ops()
{
    print_header("Section: view (inline_span/overflow_span)");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 18;
    constexpr size_t OPS = 1000000;

    multi_block_bitmask m;
    m.resize_entities(N);
    m.reserve_blocks(2);
    for (size_t i = 0; i < N; ++i)
        m.set_bit_no_check(static_cast<uint32_t>(i), 1, static_cast<uint32_t>(i % 64));

    // 8.1 inline_span
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                auto sp = m.inline_span();
                s += sp.size();
            }
            (void)s;
        });
        print_ns("inline_span", OPS, ns / static_cast<double>(OPS));
    }

    // 8.2 overflow_span
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                auto sp = m.overflow_span(opaque(static_cast<uint32_t>(i % N)));
                s += sp.size();
            }
            (void)s;
        });
        print_ns("overflow_span", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 9: 集合运算 ===
static void test_set_ops()
{
    print_header("Section: set operations (and/or/xor/sub/overlaps/contains/equals)");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 14;

    multi_block_bitmask a, b;
    a.resize_entities(N);
    b.resize_entities(N);
    a.reserve_blocks(2);
    b.reserve_blocks(2);

    // 预填数据
    for (size_t i = 0; i < N; ++i)
    {
        if ((i & 1) == 0) a.set_bit_no_check(static_cast<uint32_t>(i), 0, 1);
        if ((i & 3) == 0) a.set_bit_no_check(static_cast<uint32_t>(i), 1, 5);
        if ((i & 1) == 1) b.set_bit_no_check(static_cast<uint32_t>(i), 0, 2);
        if ((i & 3) == 0) b.set_bit_no_check(static_cast<uint32_t>(i), 1, 5);
    }

    // 9.1 or_with
    {
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask t = a.clone();
            t.or_with(b);
            compiler_barrier();
            return t.size();
        });
        print_ns("or_with", 1, ns);
    }

    // 9.2 and_with
    {
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask t = a.clone();
            t.and_with(b);
            compiler_barrier();
            return t.size();
        });
        print_ns("and_with", 1, ns);
    }

    // 9.3 xor_with
    {
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask t = a.clone();
            t.xor_with(b);
            compiler_barrier();
            return t.size();
        });
        print_ns("xor_with", 1, ns);
    }

    // 9.4 subtract
    {
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask t = a.clone();
            t.subtract(b);
            compiler_barrier();
            return t.size();
        });
        print_ns("subtract", 1, ns);
    }

    // 9.5 overlaps
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool r = a.overlaps(b);
            compiler_barrier();
            return r;
        });
        print_ns("overlaps", 1, ns);
    }

    // 9.6 contains_all
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool r = a.contains_all(b);
            compiler_barrier();
            return r;
        });
        print_ns("contains_all", 1, ns);
    }

    // 9.7 equals (相等场景)
    {
        multi_block_bitmask c = a.clone();
        double ns = best_ns(REPEAT, [&]() {
            volatile bool r = a.equals(c);
            compiler_barrier();
            return r;
        });
        print_ns("equals (相等)", 1, ns);
    }

    // 9.8 equals (不等场景)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool r = a.equals(b);
            compiler_barrier();
            return r;
        });
        print_ns("equals (不等)", 1, ns);
    }

    print_footer();
}

// === Section 10: 内存压缩 ===
static void test_compact_ops()
{
    print_header("Section: compact (slot/all)");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 16;

    // 10.1 compact_slot (overflow 全零)
    {
        // 准备: N 个槽位 overflow 全零
        multi_block_bitmask m;
        m.resize_entities(N);
        m.reserve_blocks(2);
        for (size_t i = 0; i < N; ++i)
        {
            m.set_bit_no_check(static_cast<uint32_t>(i), 1, 0);
            m.clear_bit_no_check(static_cast<uint32_t>(i), 1, 0);
        }
        // 此时所有 overflow 全零但未释放
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask t = m.clone();
            for (size_t i = 0; i < N; ++i) t.compact_slot(static_cast<uint32_t>(i));
            compiler_barrier();
            return t.overflow_entity_count();
        });
        print_ns("compact_slot (全零)", N, ns / static_cast<double>(N));
    }

    // 10.2 compact_slot (overflow 非零)
    {
        multi_block_bitmask m;
        m.resize_entities(N);
        m.reserve_blocks(2);
        for (size_t i = 0; i < N; ++i)
            m.set_bit_no_check(static_cast<uint32_t>(i), 1, 5);

        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask t = m.clone();
            for (size_t i = 0; i < N; ++i) t.compact_slot(static_cast<uint32_t>(i));
            compiler_barrier();
            return t.overflow_entity_count();
        });
        print_ns("compact_slot (非零)", N, ns / static_cast<double>(N));
    }

    // 10.3 compact_all
    {
        multi_block_bitmask m;
        m.resize_entities(N);
        m.reserve_blocks(2);
        for (size_t i = 0; i < N; ++i)
        {
            m.set_bit_no_check(static_cast<uint32_t>(i), 1, 0);
            m.clear_bit_no_check(static_cast<uint32_t>(i), 1, 0);
        }
        double ns = best_ns(REPEAT, [&]() {
            multi_block_bitmask t = m.clone();
            t.compact_all();
            compiler_barrier();
            return t.overflow_entity_count();
        });
        print_ns("compact_all", 1, ns);
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  multi_block_bitmask 独立性能测试\n";
    cout << "============================================================\n";

    test_capacity();
    test_bit_ops();
    test_query();
    test_bit_query();
    test_block_ops();
    test_slot_ops();
    test_iter_ops();
    test_view_ops();
    test_set_ops();
    test_compact_ops();

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
