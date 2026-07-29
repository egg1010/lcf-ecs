// test_entity_perf.cpp - entity/entity_manager 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/entity.hpp"
#include "include/entity_manager.hpp"

using namespace std;
using ecs::entity;
using ecs::entity_manager;
using ecs::entity_flag;

// === Section 1: entity 结构体 ===
static void test_entity()
{
    print_header("Section: entity");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 1.1 默认构造
    {
        double ns = best_ns(REPEAT, [&]() {
            entity e;
            compiler_barrier();
            return e.handle_;
        });
        print_ns("default ctor", 1, ns);
    }

    // 1.2 (index, version) 构造
    {
        double ns = best_ns(REPEAT, [&]() {
            entity e(opaque(static_cast<uint32_t>(1)), opaque(static_cast<uint32_t>(2)));
            compiler_barrier();
            return e.handle_;
        });
        print_ns("(idx,ver) ctor", 1, ns);
    }

    // 1.3 operator==
    {
        entity a(1, 2), b(1, 2);
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i) s = (a == b);
            (void)s;
        });
        print_ns("operator==", OPS, ns / static_cast<double>(OPS));
    }

    // 1.4 operator!=
    {
        entity a(1, 2), b(1, 3);
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i) s = (a != b);
            (void)s;
        });
        print_ns("operator!=", OPS, ns / static_cast<double>(OPS));
    }

    // 1.5 is_valid
    {
        entity e(1, 1);
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i) s = e.is_valid();
            (void)s;
        });
        print_ns("is_valid", OPS, ns / static_cast<double>(OPS));
    }

    // 1.6 std::hash<entity>
    {
        entity e(123, 456);
        std::hash<entity> h;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i) s = h(e);
            (void)s;
        });
        print_ns("std::hash<entity>", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 2: entity_manager - 实体生命周期 ===
static void test_entity_lifecycle()
{
    print_header("Section: entity_manager (lifecycle)");
    constexpr int REPEAT = 3;
    constexpr size_t N = 1 << 18;  // 256K

    // 2.1 默认构造
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_manager em;
            compiler_barrier();
            return em.num_mask_blocks();
        });
        print_ns("default ctor", 1, ns);
    }

    // 2.2 预分配构造
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_manager em(N);
            compiler_barrier();
            return em.num_mask_blocks();
        });
        print_ns("prealloc ctor", 1, ns);
    }

    // 2.3 append_preallocated_entities
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_manager em;
            em.append_preallocated_entities(N);
            compiler_barrier();
            return em.num_mask_blocks();
        });
        print_ns("append_preallocated", N, ns / static_cast<double>(N));
    }

    // 2.4 get_entity (从预分配池)
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_manager em;
            em.append_preallocated_entities(N);
            entity e{};
            for (size_t i = 0; i < N; ++i) { e = em.get_entity(); }
            compiler_barrier();
            (void)e;
            return em.num_mask_blocks();
        });
        print_ns("get_entity (prealloc)", N, ns / static_cast<double>(N));
    }

    // 2.5 get_entity (allocate_entity 路径)
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_manager em;
            entity e{};
            for (size_t i = 0; i < N; ++i) { e = em.get_entity(); }
            compiler_barrier();
            (void)e;
            return em.num_mask_blocks();
        });
        print_ns("get_entity (allocate)", N, ns / static_cast<double>(N));
    }

    // 2.6 destroy_entity
    {
        double ns = best_ns(REPEAT, [&]() {
            entity_manager em;
            vector<entity> ents(N);
            for (size_t i = 0; i < N; ++i) ents[i] = em.get_entity();
            for (size_t i = 0; i < N; ++i) em.destroy_entity(ents[i]);
            compiler_barrier();
            return em.num_mask_blocks();
        });
        print_ns("destroy_entity", N, ns / static_cast<double>(N));
    }

    // 2.7 is_version_valid
    {
        entity_manager em;
        vector<entity> ents(N);
        for (size_t i = 0; i < N; ++i) ents[i] = em.get_entity();
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < N; ++i) s = em.is_version_valid(ents[i]);
            (void)s;
        });
        print_ns("is_version_valid", N, ns / static_cast<double>(N));
    }

    print_footer();
}

// === Section 3: entity_manager - 掩码操作 ===
static void test_entity_mask()
{
    print_header("Section: entity_manager (mask)");
    constexpr int REPEAT = 3;
    constexpr size_t N = 1 << 18;
    constexpr size_t OPS = 1000000;

    entity_manager em;
    em.reserve_mask_blocks(4);
    for (size_t i = 0; i < N; ++i) em.get_entity();

    // 3.1 set_mask_bit (带边界检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t v = 0;
            for (size_t i = 0; i < OPS; ++i) em.set_mask_bit(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint32_t>(i % 64)));
            (void)v;
        });
        print_ns("set_mask_bit", OPS, ns / static_cast<double>(OPS));
    }

    // 3.2 set_mask_bit_no_bounds_check
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t v = 0;
            for (size_t i = 0; i < OPS; ++i) em.set_mask_bit_no_bounds_check(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint32_t>(i % 64)));
            (void)v;
        });
        print_ns("set_mask_bit_no_bounds_check", OPS, ns / static_cast<double>(OPS));
    }

    // 3.3 clear_mask_bit / clear_mask_bit_no_bounds_check
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t v = 0;
            for (size_t i = 0; i < OPS; ++i) em.clear_mask_bit(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint32_t>(i % 64)));
            (void)v;
        });
        print_ns("clear_mask_bit", OPS, ns / static_cast<double>(OPS));

        ns = best_ns(REPEAT, [&]() {
            volatile uint32_t v = 0;
            for (size_t i = 0; i < OPS; ++i) em.clear_mask_bit_no_bounds_check(opaque(static_cast<uint32_t>(i % N)), 0, opaque(static_cast<uint32_t>(i % 64)));
            (void)v;
        });
        print_ns("clear_mask_bit_no_bounds_check", OPS, ns / static_cast<double>(OPS));
    }

    // 3.4 get_mask / get_block / num_mask_blocks / reserve_mask_blocks
    {
        for (size_t i = 0; i < 64; ++i) em.set_mask_bit_no_bounds_check(0, 0, static_cast<uint32_t>(i));
        double ns = best_ns(REPEAT, [&]() {
            volatile uint64_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += em.get_mask(opaque(static_cast<uint32_t>(i % N)));
                s += em.get_block(opaque(static_cast<uint32_t>(i % N)), 0);
                s += em.num_mask_blocks();
            }
            (void)s;
        });
        print_ns("get_mask/get_block/num_blocks", OPS, ns / static_cast<double>(OPS));
    }

    // 3.5 for_each_set_bit
    {
        for (size_t i = 0; i < 64; ++i) em.set_mask_bit_no_bounds_check(0, 0, static_cast<uint32_t>(i));
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t cnt = 0;
            em.for_each_set_bit(0, [&](uint32_t, uint32_t) { ++cnt; });
            compiler_barrier();
            return cnt;
        });
        print_ns("for_each_set_bit", 1, ns);
    }

    print_footer();
}

// === Section 4: entity_manager - 信号子系统 ===
static void test_entity_signals()
{
    print_header("Section: entity_manager (signals)");
    constexpr int REPEAT = 3;
    constexpr size_t N = 1 << 16;
    constexpr size_t OPS = 1000000;

    // 4.1 enable / disable / has_pending / overflow_count
    {
        entity_manager em;
        em.enable_entity_signals();
        for (size_t i = 0; i < N; ++i) em.get_entity();
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                em.enable_entity_signals();
                em.disable_entity_signals();
                em.enable_entity_signals();
                s += em.has_pending_signals() ? 1 : 0;
                s += em.signal_overflow_count();
            }
            (void)s;
        });
        print_ns("enable/disable/has_pending/overflow", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 flush_signals
    {
        entity_manager em;
        em.enable_entity_signals();
        for (size_t i = 0; i < N; ++i) em.get_entity();
        double ns = best_ns(REPEAT, [&]() {
            em.flush_signals([](uint32_t, uint32_t) {});
            compiler_barrier();
            return em.has_pending_signals() ? 1 : 0;
        });
        print_ns("flush_signals", 1, ns);
    }

    // 4.3 reset_signal_overflow_count / reserve_signal_capacity
    {
        entity_manager em;
        double ns = best_ns(REPEAT, [&]() {
            em.reset_signal_overflow_count();
            em.reserve_signal_capacity(1024);
            compiler_barrier();
            return em.signal_overflow_count();
        });
        print_ns("reset_overflow/reserve_capacity", 1, ns);
    }

    print_footer();
}

// === Section 5: entity_manager - 实体状态池 ===
static void test_entity_state()
{
    print_header("Section: entity_manager (entity_state)");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1 << 18;
    constexpr size_t OPS = 1000000;

    entity_manager em;
    for (size_t i = 0; i < N; ++i) em.get_entity();

    // 5.1 get_entity_state
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                auto& st = em.get_entity_state(opaque(static_cast<uint32_t>(i % N)));
                s += st.flags;
            }
            (void)s;
        });
        print_ns("get_entity_state", OPS, ns / static_cast<double>(OPS));
    }

    // 5.2 set_entity_flag / clear_entity_flag / has_entity_flag
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i)
            {
                em.set_entity_flag(opaque(static_cast<uint32_t>(i % N)), entity_flag::active);
                em.clear_entity_flag(opaque(static_cast<uint32_t>(i % N)), entity_flag::active);
                s = em.has_entity_flag(opaque(static_cast<uint32_t>(i % N)), entity_flag::active);
            }
            (void)s;
        });
        print_ns("set/clear/has_entity_flag", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  entity / entity_manager 独立性能测试\n";
    cout << "============================================================\n";

    test_entity();
    test_entity_lifecycle();
    test_entity_mask();
    test_entity_signals();
    test_entity_state();

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
