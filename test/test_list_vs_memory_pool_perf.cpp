// test_list_vs_memory_pool_perf.cpp - list_memory_pool vs memory_pool 性能对比
// 重点对比维度:
//   1. 纯分配 (小块变长 / 大块)
//   2. 分配+释放循环
//   3. owns 命中/未命中 (list O(1) 位掩码 vs memory_pool 二分查找)
//   4. 多轮 chunk 创建/销毁 (list 链表 O(1) vs memory_pool dense 数组扩容拷贝)
//   5. soft_deallocate 复用 (list 独有, 参考用)
#include "perf_common.hpp"
#include "include/part/memory/memory_pool.hpp"
#include "include/part/memory/list_memory_pool.hpp"
#include "include/part/dense.hpp"
#include <cstdio>

using namespace std;
using namespace memory;

// 对比结果输出 (带倍率)
inline void print_vs(const char* label, double ns_a, double ns_b) noexcept
{
    double ratio = (ns_b > 0 && ns_a > 0) ? ns_b / ns_a : 0;
    const char* tag = (ns_a < ns_b) ? "WIN" : "   ";
    std::cout << "  " << std::left << std::setw(38) << label
              << " | list=" << std::right << std::fixed << std::setprecision(3) << std::setw(8) << ns_a << " ns"
              << " | pool=" << std::setw(8) << ns_b << " ns"
              << " | " << tag << " " << std::setprecision(2) << std::setw(5) << ratio << "x"
              << " | " << (ns_a < ns_b ? "list 快" : (ns_a > ns_b ? "pool 快" : "持平")) << "\n";
}

// === 场景 A: 纯分配 (小块变长 16-128B) ===
static void test_alloc_small_variable()
{
    print_header("A: allocate(16-128B, 变长)");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1 << 18;  // 256K

    mt19937 rng(42);
    uniform_int_distribution<size_t> dist(16, 128);
    dense<size_t> sizes;
    sizes.increase_capacity(OPS, 0);
    for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

    double ns_list = best_ns(REPEAT, [&]() {
        list_memory_pool pool;
        for (size_t i = 0; i < OPS; ++i)
        {
            void* p = pool.allocate(sizes[i]);
            touch_ptr(p);
        }
        compiler_barrier();
        return pool.total_allocated_bytes();
    });
    print_ns("list_memory_pool", OPS, ns_list / static_cast<double>(OPS));

    double ns_pool = best_ns(REPEAT, [&]() {
        memory_pool pool;
        for (size_t i = 0; i < OPS; ++i)
        {
            void* p = pool.allocate(sizes[i]);
            touch_ptr(p);
        }
        compiler_barrier();
        return pool.total_used();
    });
    print_ns("memory_pool     ", OPS, ns_pool / static_cast<double>(OPS));

    print_vs("=> 单次开销", ns_list / OPS, ns_pool / OPS);
    print_footer();
}

// === 场景 B: 分配+释放循环 (16-256B) ===
static void test_alloc_dealloc_small()
{
    print_header("B: alloc+dealloc(16-256B)");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1 << 18;

    mt19937 rng(42);
    uniform_int_distribution<size_t> dist(16, 256);
    dense<size_t> sizes;
    sizes.increase_capacity(OPS, 0);
    for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

    double ns_list = best_ns(REPEAT, [&]() {
        list_memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) ptrs[i] = pool.allocate(sizes[i]);
        for (size_t i = 0; i < OPS; ++i) pool.hard_deallocate(ptrs[i]);
        compiler_barrier();
        return pool.total_allocated_bytes();
    });
    print_ns("list(hard_dealloc)", 2 * OPS, ns_list / static_cast<double>(2 * OPS));

    double ns_pool = best_ns(REPEAT, [&]() {
        memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) ptrs[i] = pool.allocate(sizes[i]);
        for (size_t i = 0; i < OPS; ++i) pool.deallocate(ptrs[i]);
        compiler_barrier();
        return pool.total_used();
    });
    print_ns("memory_pool      ", 2 * OPS, ns_pool / static_cast<double>(2 * OPS));

    print_vs("=> 单次开销", ns_list / (2 * OPS), ns_pool / (2 * OPS));
    print_footer();
}

// === 场景 C: 大块分配 (1K-16K) ===
static void test_alloc_large()
{
    print_header("C: allocate(1K-16K, 大块)");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1 << 14;  // 16K

    mt19937 rng(42);
    uniform_int_distribution<size_t> dist(1024, 16384);
    dense<size_t> sizes;
    sizes.increase_capacity(OPS, 0);
    for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

    double ns_list = best_ns(REPEAT, [&]() {
        list_memory_pool pool;
        for (size_t i = 0; i < OPS; ++i)
        {
            void* p = pool.allocate(sizes[i]);
            touch_ptr(p);
        }
        compiler_barrier();
        return pool.total_allocated_bytes();
    });
    print_ns("list_memory_pool", OPS, ns_list / static_cast<double>(OPS));

    double ns_pool = best_ns(REPEAT, [&]() {
        memory_pool pool;
        for (size_t i = 0; i < OPS; ++i)
        {
            void* p = pool.allocate(sizes[i]);
            touch_ptr(p);
        }
        compiler_barrier();
        return pool.total_used();
    });
    print_ns("memory_pool     ", OPS, ns_pool / static_cast<double>(OPS));

    print_vs("=> 单次开销", ns_list / OPS, ns_pool / OPS);
    print_footer();
}

// === 场景 D: owns 命中 (list O(1) 位掩码 vs pool 二分查找) ===
static void test_owns_hit()
{
    print_header("D: owns (命中)");
    constexpr int REPEAT = 5;
    constexpr size_t N = 1000;
    constexpr size_t ITERS = 1000000;

    // 预分配 N 个指针, 反复查询
    list_memory_pool lpool;
    dense<void*> lptrs;
    lptrs.increase_capacity(N, nullptr);
    for (size_t i = 0; i < N; ++i) lptrs[i] = lpool.allocate(64);

    memory_pool mpool;
    dense<void*> mptrs;
    mptrs.increase_capacity(N, nullptr);
    for (size_t i = 0; i < N; ++i) mptrs[i] = mpool.allocate(64);

    double ns_list = best_ns(REPEAT, [&]() {
        volatile bool sink = false;
        for (size_t k = 0; k < ITERS; ++k)
        {
            sink = lpool.owns(lptrs[k % N]);
        }
        (void)sink;
        return 0;
    });
    print_ns("list_memory_pool", ITERS, ns_list / static_cast<double>(ITERS));

    double ns_pool = best_ns(REPEAT, [&]() {
        volatile bool sink = false;
        for (size_t k = 0; k < ITERS; ++k)
        {
            sink = mpool.owns(mptrs[k % N]);
        }
        (void)sink;
        return 0;
    });
    print_ns("memory_pool     ", ITERS, ns_pool / static_cast<double>(ITERS));

    print_vs("=> 单次开销", ns_list / ITERS, ns_pool / ITERS);
    print_footer();
}

// === 场景 E: owns 未命中 (list O(1) vs pool 二分全扫描) ===
static void test_owns_miss()
{
    print_header("E: owns (未命中, 栈指针)");
    constexpr int REPEAT = 5;
    constexpr size_t ITERS = 1000000;

    list_memory_pool lpool;
    for (size_t i = 0; i < 1000; ++i) { touch_ptr(lpool.allocate(64)); }

    memory_pool mpool;
    for (size_t i = 0; i < 1000; ++i) { touch_ptr(mpool.allocate(64)); }

    char dummy = 0;
    double ns_list = best_ns(REPEAT, [&]() {
        volatile bool sink = false;
        for (size_t k = 0; k < ITERS; ++k)
        {
            sink = lpool.owns(&dummy);
        }
        (void)sink;
        return 0;
    });
    print_ns("list_memory_pool", ITERS, ns_list / static_cast<double>(ITERS));

    double ns_pool = best_ns(REPEAT, [&]() {
        volatile bool sink = false;
        for (size_t k = 0; k < ITERS; ++k)
        {
            sink = mpool.owns(&dummy);
        }
        (void)sink;
        return 0;
    });
    print_ns("memory_pool     ", ITERS, ns_pool / static_cast<double>(ITERS));

    print_vs("=> 单次开销", ns_list / ITERS, ns_pool / ITERS);
    print_footer();
}

// === 场景 F: 多轮 chunk 创建/销毁 (list 链表 O(1) vs pool dense 数组扩容拷贝) ===
// 反复分配大量小块再全部释放, 触发 chunk 增删
static void test_chunk_create_destroy_cycles()
{
    print_header("F: 多轮 chunk 创建/销毁 (10 轮 x 10K x 64B)");
    constexpr int REPEAT = 5;
    constexpr int ROUNDS = 10;
    constexpr size_t PER_ROUND = 10000;

    double ns_list = best_ns(REPEAT, [&]() {
        list_memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(PER_ROUND, nullptr);
        for (int r = 0; r < ROUNDS; ++r)
        {
            for (size_t i = 0; i < PER_ROUND; ++i)
            {
                ptrs[i] = pool.allocate(64);
                touch_ptr(ptrs[i]);
            }
            for (size_t i = 0; i < PER_ROUND; ++i)
            {
                pool.hard_deallocate(ptrs[i]);
            }
        }
        compiler_barrier();
        return pool.chunk_count();
    });
    size_t total_ops = static_cast<size_t>(ROUNDS) * PER_ROUND * 2;
    print_ns("list_memory_pool", total_ops, ns_list / static_cast<double>(total_ops));

    double ns_pool = best_ns(REPEAT, [&]() {
        memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(PER_ROUND, nullptr);
        for (int r = 0; r < ROUNDS; ++r)
        {
            for (size_t i = 0; i < PER_ROUND; ++i)
            {
                ptrs[i] = pool.allocate(64);
                touch_ptr(ptrs[i]);
            }
            for (size_t i = 0; i < PER_ROUND; ++i)
            {
                pool.deallocate(ptrs[i]);
            }
        }
        compiler_barrier();
        return pool.total_used();
    });
    print_ns("memory_pool     ", total_ops, ns_pool / static_cast<double>(total_ops));

    print_vs("=> 单次开销", ns_list / total_ops, ns_pool / total_ops);
    print_footer();
}

// === 场景 G: soft_deallocate 复用 (仅 list, 参考用) ===
// 对比: 硬删除后重新分配 vs 软删除后重新分配 (软删除应更快, 因 chunk 保留无需 OS 调用)
static void test_soft_vs_hard_realloc()
{
    print_header("G: soft_deallocate 复用 vs hard_deallocate (仅 list)");
    constexpr int REPEAT = 5;
    constexpr int ROUNDS = 20;
    constexpr size_t PER_ROUND = 5000;

    // 软删除路径: chunk 全空也保留, 后续 allocate 复用
    double ns_soft = best_ns(REPEAT, [&]() {
        list_memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(PER_ROUND, nullptr);
        for (int r = 0; r < ROUNDS; ++r)
        {
            for (size_t i = 0; i < PER_ROUND; ++i)
            {
                ptrs[i] = pool.allocate(64);
                touch_ptr(ptrs[i]);
            }
            for (size_t i = 0; i < PER_ROUND; ++i)
            {
                pool.soft_deallocate(ptrs[i]); // 软删除: 保留 chunk
            }
        }
        compiler_barrier();
        return pool.chunk_count();
    });
    size_t total_ops = static_cast<size_t>(ROUNDS) * PER_ROUND * 2;
    print_ns("soft_deallocate", total_ops, ns_soft / static_cast<double>(total_ops));

    // 硬删除路径: chunk 全空归还 OS, 下轮需重新申请新 chunk
    double ns_hard = best_ns(REPEAT, [&]() {
        list_memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(PER_ROUND, nullptr);
        for (int r = 0; r < ROUNDS; ++r)
        {
            for (size_t i = 0; i < PER_ROUND; ++i)
            {
                ptrs[i] = pool.allocate(64);
                touch_ptr(ptrs[i]);
            }
            for (size_t i = 0; i < PER_ROUND; ++i)
            {
                pool.hard_deallocate(ptrs[i]); // 硬删除: chunk 全空归还 OS
            }
        }
        compiler_barrier();
        return pool.chunk_count();
    });
    print_ns("hard_deallocate", total_ops, ns_hard / static_cast<double>(total_ops));

    print_vs("=> 单次开销 (soft vs hard)", ns_soft / total_ops, ns_hard / total_ops);
    print_footer();
}

// === 场景 H: 固定大小高频 alloc/free (单档热点) ===
static void test_fixed_size_hot_loop()
{
    print_header("H: 固定 64B 高频 alloc/free");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1 << 18;

    double ns_list = best_ns(REPEAT, [&]() {
        list_memory_pool pool;
        for (size_t i = 0; i < OPS; ++i)
        {
            void* p = pool.allocate(64);
            touch_ptr(p);
            pool.hard_deallocate(p);
        }
        compiler_barrier();
        return pool.allocation_count();
    });
    print_ns("list_memory_pool", OPS, ns_list / static_cast<double>(OPS));

    double ns_pool = best_ns(REPEAT, [&]() {
        memory_pool pool;
        for (size_t i = 0; i < OPS; ++i)
        {
            void* p = pool.allocate(64);
            touch_ptr(p);
            pool.deallocate(p);
        }
        compiler_barrier();
        return pool.total_used();
    });
    print_ns("memory_pool     ", OPS, ns_pool / static_cast<double>(OPS));

    print_vs("=> 单次开销", ns_list / OPS, ns_pool / OPS);
    print_footer();
}

int main()
{
    std::cout << "============================================================\n";
    std::cout << "  list_memory_pool vs memory_pool 性能对比\n";
    std::cout << "  (list: 链表+位图 O(1) owns / pool: dense 数组+二分 owns)\n";
    std::cout << "============================================================\n";

    test_alloc_small_variable();
    test_alloc_dealloc_small();
    test_alloc_large();
    test_owns_hit();
    test_owns_miss();
    test_chunk_create_destroy_cycles();
    test_soft_vs_hard_realloc();
    test_fixed_size_hot_loop();

    std::cout << "\n============================================================\n";
    std::cout << "  对比完成\n";
    std::cout << "============================================================\n";
    return 0;
}
