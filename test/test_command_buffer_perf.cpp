// test_command_buffer_perf.cpp - command_buffer 独立性能测试
#include "perf_common.hpp"
#include "include/component.hpp"  // 包含 manager 完整定义后, 间接包含 command_buffer.hpp

using namespace std;
using ecs::manager;
using ecs::command_buffer;
using ecs::entity;

struct Pos { float x, y, z; };
struct Vel { float vx, vy, vz; };

// === Section 1: 录制接口 ===
static void test_record()
{
    print_header("Section: record (add/remove/destroy)");
    constexpr int REPEAT = 3;
    constexpr size_t N = 1 << 18;  // 256K

    {
        manager mgr;
        mgr.append_preallocated_entities(N);
        mgr.reserve_entity_signal_capacity(N);
        vector<entity> ents(N);
        for (size_t i = 0; i < N; ++i) ents[i] = mgr.create_entity();
        Pos p{1.0f, 2.0f, 3.0f};

        double ns = best_ns(REPEAT, [&]() {
            command_buffer cb(&mgr);
            for (size_t i = 0; i < N; ++i) cb.add_component(ents[i], p);
            compiler_barrier();
            return cb.size();
        });
        print_ns("add_component (record)", N, ns / static_cast<double>(N));

        // 预分配后录制 (无扩容开销)
        ns = best_ns(REPEAT, [&]() {
            command_buffer cb(&mgr);
            cb.reserve(N);
            for (size_t i = 0; i < N; ++i) cb.add_component(ents[i], p);
            compiler_barrier();
            return cb.size();
        });
        print_ns("add_component (record+reserve)", N, ns / static_cast<double>(N));
    }

    {
        manager mgr;
        mgr.append_preallocated_entities(N);
        mgr.reserve_entity_signal_capacity(N);
        vector<entity> ents(N);
        for (size_t i = 0; i < N; ++i)
        {
            ents[i] = mgr.create_entity();
            mgr.add<Pos>(ents[i], Pos{1, 2, 3});
        }

        double ns = best_ns(REPEAT, [&]() {
            command_buffer cb(&mgr);
            cb.reserve(N);
            for (size_t i = 0; i < N; ++i) cb.remove_component<Pos>(ents[i]);
            compiler_barrier();
            return cb.size();
        });
        print_ns("remove_component (record+reserve)", N, ns / static_cast<double>(N));
    }

    {
        manager mgr;
        mgr.append_preallocated_entities(N);
        mgr.reserve_entity_signal_capacity(N);
        vector<entity> ents(N);
        for (size_t i = 0; i < N; ++i) ents[i] = mgr.create_entity();

        double ns = best_ns(REPEAT, [&]() {
            command_buffer cb(&mgr);
            cb.reserve(N);
            for (size_t i = 0; i < N; ++i) cb.destroy_entity(ents[i]);
            compiler_barrier();
            return cb.size();
        });
        print_ns("destroy_entity (record+reserve)", N, ns / static_cast<double>(N));
    }

    print_footer();
}

// === Section 2: 查询接口 ===
static void test_query()
{
    print_header("Section: query (size/empty)");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;
    constexpr size_t N = 1 << 14;

    manager mgr;
    mgr.append_preallocated_entities(N);
    mgr.reserve_entity_signal_capacity(N);
    vector<entity> ents(N);
    for (size_t i = 0; i < N; ++i) ents[i] = mgr.create_entity();
    Pos p{1, 2, 3};

    command_buffer cb(&mgr);
    for (size_t i = 0; i < N; ++i) cb.add_component(ents[i], p);

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += cb.size();
                s += cb.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("size/empty", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 3: flush (回放) ===
static void test_flush()
{
    print_header("Section: flush (replay)");
    constexpr int REPEAT = 3;
    constexpr size_t N = 1 << 16;  // 64K

    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            mgr.append_preallocated_entities(N);
            mgr.reserve_entity_signal_capacity(N);
    vector<entity> ents(N);
            for (size_t i = 0; i < N; ++i) ents[i] = mgr.create_entity();
            Pos p{1, 2, 3};

            command_buffer cb(&mgr);
            cb.reserve(N);
            for (size_t i = 0; i < N; ++i) cb.add_component(ents[i], p);
            cb.flush();
            compiler_barrier();
            return cb.size();
        });
        print_ns("flush (add_component)", N, ns / static_cast<double>(N));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            mgr.append_preallocated_entities(N);
            mgr.reserve_entity_signal_capacity(N);
    vector<entity> ents(N);
            for (size_t i = 0; i < N; ++i)
            {
                ents[i] = mgr.create_entity();
                mgr.add<Pos>(ents[i], Pos{1, 2, 3});
            }

            command_buffer cb(&mgr);
            cb.reserve(N);
            for (size_t i = 0; i < N; ++i) cb.remove_component<Pos>(ents[i]);
            cb.flush();
            compiler_barrier();
            return cb.size();
        });
        print_ns("flush (remove_component)", N, ns / static_cast<double>(N));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            mgr.append_preallocated_entities(N);
            mgr.reserve_entity_signal_capacity(N);
            vector<entity> ents(N);
            for (size_t i = 0; i < N; ++i) ents[i] = mgr.create_entity();

            command_buffer cb(&mgr);
            cb.reserve(N);
            for (size_t i = 0; i < N; ++i) cb.destroy_entity(ents[i]);
            cb.flush();
            compiler_barrier();
            return cb.size();
        });
        print_ns("flush (destroy_entity)", N, ns / static_cast<double>(N));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            manager mgr;
            mgr.append_preallocated_entities(N);
            mgr.reserve_entity_signal_capacity(N);
            vector<entity> ents(N);
            for (size_t i = 0; i < N; ++i) ents[i] = mgr.create_entity();

            command_buffer cb(&mgr);
            cb.reserve(2 * N);
            for (size_t i = 0; i < N; ++i)
            {
                cb.add_component(ents[i], Pos{1, 2, 3});
                cb.add_component(ents[i], Vel{1, 2, 3});
            }
            cb.flush();
            compiler_barrier();
            return cb.size();
        });
        print_ns("flush (mixed 2-comp)", 2 * N, ns / static_cast<double>(2 * N));
    }

    print_footer();
}

// === Section 4: clear ===
static void test_clear()
{
    print_header("Section: clear");
    constexpr int REPEAT = 3;
    constexpr size_t N = 1 << 16;

    manager mgr;
    mgr.append_preallocated_entities(N);
    mgr.reserve_entity_signal_capacity(N);
    vector<entity> ents(N);
    for (size_t i = 0; i < N; ++i) ents[i] = mgr.create_entity();
    Pos p{1, 2, 3};

    {
        double ns = best_ns(REPEAT, [&]() {
            command_buffer cb(&mgr);
            cb.reserve(N);
            for (size_t i = 0; i < N; ++i) cb.add_component(ents[i], p);
            cb.clear();
            compiler_barrier();
            return cb.size();
        });
        print_ns("clear (after record+reserve)", N, ns / static_cast<double>(N));
    }

    print_footer();
}

int main()
{
    cout << "============================================================\n";
    cout << "  command_buffer 独立性能测试\n";
    cout << "============================================================\n";

    test_record();
    test_query();
    test_flush();
    test_clear();

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
