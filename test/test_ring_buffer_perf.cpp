// test_ring_buffer_perf.cpp - ring_buffer<T, N> 独立性能测试
#include "perf_common.hpp"
#include "include/part/ring_buffer.hpp"

using namespace std;

// 测试组件 (POD4/POD32 来自 perf_common.hpp)

// === Section 1: 写入接口 ===
template <typename T, size_t N>
static void test_push()
{
    print_header(("Section 1: push (T=" + to_string(sizeof(T)) + "B, N=" + to_string(N) + ")").c_str());
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1 << 20;  // 1M

    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, N> rb;
            for (size_t i = 0; i < OPS; ++i)
            {
                rb.push(v);
            }
            compiler_barrier();
            return rb.pending_count();
        });
        print_ns("push(const&)", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, N> rb;
            for (size_t i = 0; i < OPS; ++i)
            {
                T v{};
                rb.push(std::move(v));
            }
            compiler_barrier();
            return rb.pending_count();
        });
        print_ns("push(&&)", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, N> rb;
            for (size_t i = 0; i < OPS; ++i)
            {
                rb.emplace();
            }
            compiler_barrier();
            return rb.pending_count();
        });
        print_ns("emplace()", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 2: 读取/弹出 ===
template <typename T, size_t N>
static void test_pop_peek()
{
    print_header(("Section 2: pop/peek (T=" + to_string(sizeof(T)) + "B, N=" + to_string(N) + ")").c_str());
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1 << 20;

    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, N> rb;
            for (size_t i = 0; i < N; ++i) rb.push(v);
            for (size_t i = 0; i < OPS; ++i)
            {
                rb.pop();
                rb.push(v);
            }
            compiler_barrier();
            return rb.pending_count();
        });
        print_ns("pop", OPS, ns / static_cast<double>(OPS));
    }

    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, N> rb;
            for (size_t i = 0; i < N; ++i) rb.push(v);
            const T* p = nullptr;
            for (size_t i = 0; i < OPS; ++i) { p = rb.peek(); touch_ptr(p); }
            compiler_barrier();
            return p;
        });
        print_ns("peek", OPS, ns / static_cast<double>(OPS));
    }

    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, N> rb;
            for (size_t i = 0; i < N; ++i) rb.push(v);
            size_t total = 0;
            for (size_t i = 0; i < OPS / N; ++i)
            {
                for (size_t k = 0; k < N; ++k) rb.push(v);
                total += rb.drain([](const T&) {});
            }
            compiler_barrier();
            return total;
        });
        print_ns("drain", OPS, ns / static_cast<double>(OPS));
    }

    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, N> rb;
            for (size_t i = 0; i < N; ++i) rb.push(v);
            size_t total = 0;
            for (size_t i = 0; i < OPS / 64; ++i)
            {
                for (size_t k = 0; k < 64; ++k) rb.push(v);
                total += rb.drain_with_budget(64, [](const T&) {});
            }
            compiler_barrier();
            return total;
        });
        print_ns("drain_with_budget", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 3: 状态查询 ===
template <typename T, size_t N>
static void test_status()
{
    print_header(("Section 3: status (T=" + to_string(sizeof(T)) + "B, N=" + to_string(N) + ")").c_str());
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    ring_buffer<T, N> rb;
    T v{};
    for (size_t i = 0; i < N; ++i) rb.push(v);

    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += rb.empty() ? 1 : 0;
                s += rb.has_pending() ? 1 : 0;
                s += rb.pending_count();
                s += ring_buffer<T, N>::capacity();
            }
            (void)s;
        });
        print_ns("empty/has_pending/pending_count/capacity", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, N> tmp;
            for (size_t i = 0; i < N; ++i) tmp.push(v);
            tmp.clear();
            compiler_barrier();
            return tmp.pending_count();
        });
        print_ns("clear", 1, ns);
    }

    print_footer();
}

// === Section 4: 无界特性 (N=16 push 远超 N) ===
template <typename T>
static void test_unbounded()
{
    print_header(("Section 4: unbounded (T=" + to_string(sizeof(T)) + "B, N=16)").c_str());
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1 << 20;
    T v{};

    {
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, 16> rb;
            for (size_t i = 0; i < OPS; ++i)
            {
                rb.push(v);
            }
            compiler_barrier();
            return rb.pending_count();
        });
        print_ns("push 1M into N=16", OPS, ns / static_cast<double>(OPS));
    }

    {
        double ns = best_ns(REPEAT, [&]() {
            ring_buffer<T, 16> rb;
            for (size_t i = 0; i < OPS; ++i) rb.push(v);
            size_t total = rb.drain([](const T&) {});
            compiler_barrier();
            return total;
        });
        print_ns("drain 1M from N=16", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: 静态池 ===
template <typename T>
static void test_static_pool()
{
    print_header(("Section 5: static_pool (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    {
        T v{};
        ring_buffer<T, 256> rb;
        for (size_t i = 0; i < 256; ++i) rb.push(v);
        rb.clear();

        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += ring_buffer<T, 256>::static_pool_size();
            }
            (void)s;
        });
        print_ns("static_pool_size", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

int main()
{
    cout << "============================================================\n";
    cout << "  ring_buffer<T, N> 独立性能测试\n";
    cout << "============================================================\n";

    // T=POD4, 不同 N
    cout << "\n=== T = POD4 (4B) ===\n";
    test_push<POD4, 16>();
    test_push<POD4, 64>();
    test_push<POD4, 256>();
    test_push<POD4, 1024>();

    test_pop_peek<POD4, 16>();
    test_pop_peek<POD4, 256>();
    test_pop_peek<POD4, 1024>();

    test_status<POD4, 256>();
    test_unbounded<POD4>();
    test_static_pool<POD4>();

    // T=POD32, 不同 N
    cout << "\n=== T = POD32 (32B) ===\n";
    test_push<POD32, 16>();
    test_push<POD32, 256>();
    test_push<POD32, 1024>();

    test_pop_peek<POD32, 256>();
    test_pop_peek<POD32, 1024>();

    test_status<POD32, 256>();
    test_unbounded<POD32>();
    test_static_pool<POD32>();

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
