// test_allocators_perf.cpp - 5 个分配器独立性能测试
// memory_pool(TLSF)/slab_allocator/layered_allocator/arena_allocator/list_memory_pool
#include "perf_common.hpp"
#include "include/part/memory/memory_pool.hpp"
#include "include/part/memory/slab_allocator.hpp"
#include "include/part/memory/layered_allocator.hpp"
#include "include/part/memory/arena_allocator.hpp"
#include "include/part/memory/list_memory_pool.hpp"
#include "include/part/dense.hpp"

using namespace std;
using namespace memory;

// === Section 1: memory_pool (TLSF) ===
static void test_memory_pool()
{
    print_header("Section: memory_pool (TLSF)");
    constexpr int REPEAT = 3;
    constexpr size_t OPS = 1 << 18;  // 256K

    // 1.1 allocate (变长, 16-128B) - 预填充页错误, 测小块缓存命中路径
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(16, 128);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS, 0);
        for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

        memory_pool pool;
        dense<void*> warm;
        warm.increase_capacity(OPS, nullptr);
        // 预填充: 分配再释放, 填充小块缓存并触发页错误
        for (size_t i = 0; i < OPS; ++i) { warm[i] = pool.allocate(sizes[i]); touch_ptr(warm[i]); }
        for (size_t i = 0; i < OPS; ++i) pool.deallocate(warm[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                void* p = pool.allocate(sizes[i]);
                touch_ptr(p);
                pool.deallocate(p);
            }
            return pool.total_used();
        });
        print_ns("allocate(16-128B, cache hit)", OPS, ns / static_cast<double>(OPS));
    }

    // 1.2 allocate + deallocate (随机大小, 预填充)
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(16, 256);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS, 0);
        for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

        memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(OPS, nullptr);
        // 预填充: 触发页错误
        for (size_t i = 0; i < OPS; ++i) { ptrs[i] = pool.allocate(sizes[i]); touch_ptr(ptrs[i]); }
        for (size_t i = 0; i < OPS; ++i) pool.deallocate(ptrs[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i) ptrs[i] = pool.allocate(sizes[i]);
            for (size_t i = 0; i < OPS; ++i) pool.deallocate(ptrs[i]);
            return pool.total_used();
        });
        print_ns("allocate+deallocate (16-256B)", 2 * OPS, ns / static_cast<double>(2 * OPS));
    }

    // 1.3 allocate 大块 (1KB-16KB, 预填充)
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(1024, 16384);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS / 16, 0);
        for (size_t i = 0; i < OPS / 16; ++i) sizes[i] = dist(rng);

        memory_pool pool;
        dense<void*> warm;
        warm.increase_capacity(OPS / 16, nullptr);
        for (size_t i = 0; i < OPS / 16; ++i) { warm[i] = pool.allocate(sizes[i]); touch_ptr(warm[i]); }
        for (size_t i = 0; i < OPS / 16; ++i) pool.deallocate(warm[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS / 16; ++i)
            {
                void* p = pool.allocate(sizes[i]);
                touch_ptr(p);
            }
            for (size_t i = 0; i < OPS / 16; ++i) pool.deallocate(warm[i]);
            return pool.total_used();
        });
        print_ns("allocate(1K-16K)", OPS / 16, ns / static_cast<double>(OPS / 16));
    }

    // 1.4 construct / destroy
    {
        struct Obj { uint64_t a, b, c; };
        double ns = best_ns(REPEAT, [&]() {
            memory_pool pool;
            dense<Obj*> ptrs;
            ptrs.increase_capacity(OPS, nullptr);
            for (size_t i = 0; i < OPS; ++i) ptrs[i] = pool.construct<Obj>(i, i + 1, i + 2);
            for (size_t i = 0; i < OPS; ++i) pool.destroy(ptrs[i]);
            compiler_barrier();
            return pool.total_used();
        });
        print_ns("construct/destroy<Obj>", 2 * OPS, ns / static_cast<double>(2 * OPS));
    }

    // 1.5 total_allocated / total_used / chunk_size / empty
    {
        memory_pool pool;
        for (size_t i = 0; i < 1000; ++i) touch_ptr(pool.allocate(64));
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                s += pool.total_allocated();
                s += pool.total_used();
                s += pool.chunk_size();
                s += pool.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("total_alloc/used/chunk/empty", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 1.6 owns (二分查找)
    {
        memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(1000, nullptr);
        for (size_t i = 0; i < 1000; ++i) ptrs[i] = pool.allocate(64);
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < 1000; ++i) sink = pool.owns(ptrs[i]);
            (void)sink;
        });
        print_ns("owns (hit)", 1000, ns / 1000.0);
    }

    // 1.7 owns (miss)
    {
        memory_pool pool;
        char dummy = 0;
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < OPS_Q; ++i) sink = pool.owns(&dummy);
            (void)sink;
        });
        print_ns("owns (miss)", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 1.8 stats (O(1) free_block_count + 增量维护)
    {
        memory_pool pool;
        for (size_t i = 0; i < 10000; ++i) { touch_ptr(pool.allocate(32)); }
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                pool_stats s = pool.stats();
                sink += s.free_block_count;
            }
            (void)sink;
        });
        print_ns("stats()", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 1.9 iterate_free
    {
        memory_pool pool;
        for (size_t i = 0; i < 1000; ++i) touch_ptr(pool.allocate(64));
        const size_t OPS_Q = 100000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                size_t cnt = 0;
                pool.iterate_free([&](void*, size_t) { ++cnt; });
                sink += cnt;
            }
            (void)sink;
        });
        print_ns("iterate_free", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 1.10 increase_capacity / reduce_capacity / reset
    {
        double ns = best_ns(REPEAT, [&]() {
            memory_pool pool;
            pool.increase_capacity(1 << 20);  // 1MB
            return opaque(pool.total_allocated());
        });
        print_ns("increase_capacity(1MB)", 1, ns);

        ns = best_ns(REPEAT, [&]() {
            memory_pool pool;
            pool.increase_capacity(1 << 20);
            pool.reduce_capacity(0);
            return opaque(pool.total_allocated());
        });
        print_ns("reduce_capacity(0)", 1, ns);

        ns = best_ns(REPEAT, [&]() {
            memory_pool pool;
            for (size_t i = 0; i < 1000; ++i) touch_ptr(pool.allocate(64));
            pool.reset();
            return opaque(pool.total_used());
        });
        print_ns("reset", 1, ns);
    }

    print_footer();
}

// === Section 2: slab_allocator ===
static void test_slab_allocator()
{
    print_header("Section: slab_allocator");
    constexpr int REPEAT = 3;
    constexpr size_t OPS = 1 << 18;

    // 2.1 allocate (无参) - 预填充页错误
    {
        slab_allocator slab(64);
        dense<void*> warm;
        warm.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) { warm[i] = slab.allocate(); touch_ptr(warm[i]); }
        for (size_t i = 0; i < OPS; ++i) slab.deallocate(warm[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                void* p = slab.allocate();
                touch_ptr(p);
            }
            for (size_t i = 0; i < OPS; ++i) slab.deallocate(warm[i]);
            return slab.free_blocks();
        });
        print_ns("allocate (64B)", OPS, ns / static_cast<double>(OPS));
    }

    // 2.2 allocate + deallocate (预填充)
    {
        slab_allocator slab(64);
        dense<void*> ptrs;
        ptrs.increase_capacity(OPS, nullptr);
        // 预填充: 触发页错误
        for (size_t i = 0; i < OPS; ++i) { ptrs[i] = slab.allocate(); touch_ptr(ptrs[i]); }
        for (size_t i = 0; i < OPS; ++i) slab.deallocate(ptrs[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i) ptrs[i] = slab.allocate();
            for (size_t i = 0; i < OPS; ++i) slab.deallocate(ptrs[i]);
            return slab.free_blocks();
        });
        print_ns("allocate+deallocate (64B)", 2 * OPS, ns / static_cast<double>(2 * OPS));
    }

    // 2.3 不同 block_size 对比 (预填充)
    {
        for (size_t bs : {16, 32, 64, 128})
        {
            slab_allocator slab(bs);
            dense<void*> ptrs;
            ptrs.increase_capacity(OPS, nullptr);
            for (size_t i = 0; i < OPS; ++i) { ptrs[i] = slab.allocate(); touch_ptr(ptrs[i]); }
            for (size_t i = 0; i < OPS; ++i) slab.deallocate(ptrs[i]);
            double ns = best_ns(REPEAT, [&]() {
                for (size_t i = 0; i < OPS; ++i) ptrs[i] = slab.allocate();
                for (size_t i = 0; i < OPS; ++i) slab.deallocate(ptrs[i]);
                return slab.free_blocks();
            });
            char label[64];
            snprintf(label, sizeof(label), "alloc+dealloc (%zuB)", bs);
            print_ns(label, 2 * OPS, ns / static_cast<double>(2 * OPS));
        }
    }

    // 2.4 owns (单 chunk, O(1))
    {
        slab_allocator slab(64);
        void* p = slab.allocate();
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < OPS_Q; ++i) sink = slab.owns(p);
            (void)sink;
        });
        print_ns("owns (single chunk)", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 2.5 owns (多 chunk, 二分)
    {
        slab_allocator slab(64, 16, 64);  // blocks_per_chunk=64
        dense<void*> ptrs;
        for (size_t i = 0; i < 10000; ++i) ptrs.emplace_back(slab.allocate());
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < 10000; ++i) sink = slab.owns(ptrs[i]);
            (void)sink;
        });
        print_ns("owns (multi chunk)", 10000, ns / 10000.0);
    }

    // 2.6 block_size / total_blocks / free_blocks / empty
    {
        slab_allocator slab(64);
        for (size_t i = 0; i < 1000; ++i) touch_ptr(slab.allocate());
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                s += slab.block_size(); s += slab.total_blocks();
                s += slab.free_blocks(); s += slab.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("block_size/total/free/empty", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 2.7 min_addr / max_addr
    {
        slab_allocator slab(64);
        for (size_t i = 0; i < 1000; ++i) touch_ptr(slab.allocate());
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                s += reinterpret_cast<size_t>(slab.min_addr());
                s += reinterpret_cast<size_t>(slab.max_addr());
            }
            (void)s;
        });
        print_ns("min_addr/max_addr", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    print_footer();
}

// === Section 3: layered_allocator ===
static void test_layered_allocator()
{
    print_header("Section: layered_allocator");
    constexpr int REPEAT = 3;
    constexpr size_t OPS = 1 << 18;

    // 3.1 allocate 小块 (<=128B, 走 slab) - 预填充
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(16, 128);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS, 0);
        for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

        layered_allocator la;
        dense<void*> warm;
        warm.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) { warm[i] = la.allocate(sizes[i]); touch_ptr(warm[i]); }
        for (size_t i = 0; i < OPS; ++i) la.deallocate(warm[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                void* p = la.allocate(sizes[i]);
                touch_ptr(p);
                la.deallocate(p);
            }
            return 0;
        });
        print_ns("allocate(16-128B, slab)", OPS, ns / static_cast<double>(OPS));
    }

    // 3.2 allocate 大块 (>128B, 走 big_pool) - 预填充
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(129, 1024);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS / 4, 0);
        for (size_t i = 0; i < OPS / 4; ++i) sizes[i] = dist(rng);

        layered_allocator la;
        dense<void*> warm;
        warm.increase_capacity(OPS / 4, nullptr);
        for (size_t i = 0; i < OPS / 4; ++i) { warm[i] = la.allocate(sizes[i]); touch_ptr(warm[i]); }
        for (size_t i = 0; i < OPS / 4; ++i) la.deallocate(warm[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS / 4; ++i)
            {
                void* p = la.allocate(sizes[i]);
                touch_ptr(p);
                la.deallocate(p);
            }
            return 0;
        });
        print_ns("allocate(129-1K, big_pool)", OPS / 4, ns / static_cast<double>(OPS / 4));
    }

    // 3.3 allocate + deallocate (无大小提示, 需 find_slab) - 预填充
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(16, 128);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS, 0);
        for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

        layered_allocator la;
        dense<void*> ptrs;
        ptrs.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) { ptrs[i] = la.allocate(sizes[i]); touch_ptr(ptrs[i]); }
        for (size_t i = 0; i < OPS; ++i) la.deallocate(ptrs[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i) ptrs[i] = la.allocate(sizes[i]);
            for (size_t i = 0; i < OPS; ++i) la.deallocate(ptrs[i]);  // 无大小提示
            return 0;
        });
        print_ns("alloc+dealloc(no hint)", 2 * OPS, ns / static_cast<double>(2 * OPS));
    }

    // 3.4 allocate + deallocate (带大小提示, O(1)) - 预填充
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(16, 128);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS, 0);
        for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

        layered_allocator la;
        dense<void*> ptrs;
        ptrs.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) { ptrs[i] = la.allocate(sizes[i]); touch_ptr(ptrs[i]); }
        for (size_t i = 0; i < OPS; ++i) la.deallocate(ptrs[i], sizes[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i) ptrs[i] = la.allocate(sizes[i]);
            for (size_t i = 0; i < OPS; ++i) la.deallocate(ptrs[i], sizes[i]);  // 带大小提示
            return 0;
        });
        print_ns("alloc+dealloc(with hint)", 2 * OPS, ns / static_cast<double>(2 * OPS));
    }

    // 3.5 construct / destroy - 预填充
    {
        struct Obj { uint64_t a, b; };
        layered_allocator la;
        dense<Obj*> ptrs;
        ptrs.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) { ptrs[i] = la.construct<Obj>(i, i + 1); touch_ptr(ptrs[i]); }
        for (size_t i = 0; i < OPS; ++i) la.destroy(ptrs[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i) ptrs[i] = la.construct<Obj>(i, i + 1);
            for (size_t i = 0; i < OPS; ++i) la.destroy(ptrs[i]);
            return 0;
        });
        print_ns("construct/destroy<Obj>", 2 * OPS, ns / static_cast<double>(2 * OPS));
    }

    // 3.6 owns
    {
        layered_allocator la;
        void* p = la.allocate(64);
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < OPS_Q; ++i) sink = la.owns(p);
            (void)sink;
        });
        print_ns("owns", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 3.7 slab_max / big_pool
    {
        layered_allocator la;
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                s += la.slab_max();
                s += la.big_pool().total_allocated_bytes();
            }
            (void)s;
        });
        print_ns("slab_max/big_pool()", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    print_footer();
}

// === Section 4: arena_allocator ===
static void test_arena_allocator()
{
    print_header("Section: arena_allocator (bump)");
    constexpr int REPEAT = 3;
    constexpr size_t OPS = 1 << 18;

    // 4.1 allocate (顺序 bump) - 预触发页错误, 测得分配器真实耗时
    {
        arena_allocator arena(1 << 24);  // 16MB, 复用同一 arena
        // 预填充: 触发所有页缺页, 计时阶段不再缺页
        for (size_t i = 0; i < OPS; ++i) touch_ptr(arena.allocate(64));
        arena.reset();
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                void* p = arena.allocate(64);
                touch_ptr(p);
            }
            arena.reset();
            compiler_barrier();
            return arena.used();
        });
        print_ns("allocate(64B)", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 allocate 不同大小 (预填充避免缺页)
    {
        for (size_t sz : {16, 64, 256, 1024})
        {
            size_t max_ops = (1 << 24) / (sz + 16);
            arena_allocator arena(1 << 24);
            for (size_t i = 0; i < max_ops; ++i) touch_ptr(arena.allocate(sz));
            arena.reset();
            double ns = best_ns(REPEAT, [&]() {
                for (size_t i = 0; i < max_ops; ++i)
                {
                    void* p = arena.allocate(sz);
                    touch_ptr(p);
                }
                arena.reset();
                compiler_barrier();
                return arena.used();
            });
            char label[64];
            snprintf(label, sizeof(label), "allocate(%zuB)", sz);
            print_ns(label, max_ops, ns / static_cast<double>(max_ops));
        }
    }

    // 4.3 reset (复用已填充 arena, 仅测 reset 开销)
    {
        arena_allocator arena(1 << 20);
        for (size_t i = 0; i < 10000; ++i) touch_ptr(arena.allocate(64));
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                arena.reset();
                sink += arena.used();
            }
            (void)sink;
        });
        print_ns("reset", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 4.4 used / capacity / remaining / empty
    {
        arena_allocator arena(1 << 20);
        for (size_t i = 0; i < 1000; ++i) touch_ptr(arena.allocate(64));
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                s += arena.used(); s += arena.capacity();
                s += arena.remaining(); s += arena.empty() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("used/cap/remaining/empty", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 4.5 owns
    {
        arena_allocator arena(1 << 20);
        void* p = arena.allocate(64);
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < OPS_Q; ++i) sink = arena.owns(p);
            (void)sink;
        });
        print_ns("owns (hit)", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 4.6 借用模式 (外部 buffer)
    {
        alignas(64) char buf[1 << 16];
        double ns = best_ns(REPEAT, [&]() {
            arena_allocator arena(buf, sizeof(buf));
            volatile size_t sink = 0;
            for (size_t i = 0; i < 1000; ++i)
            {
                void* p = arena.allocate(64);
                sink += *static_cast<volatile uint8_t*>(p);
            }
            arena.reset();
            (void)sink;
            return opaque(arena.used());
        });
        print_ns("borrowed buffer alloc", 1000, ns / 1000.0);
    }

    // 4.7 移动构造/赋值
    {
        const size_t OPS_Q = 100000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                arena_allocator a(1 << 16);
                arena_allocator b = std::move(a);
                arena_allocator c(1 << 16);
                c = std::move(b);
                sink += c.used();
            }
            (void)sink;
        });
        print_ns("move ctor/assign", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    print_footer();
}

// === Section 5: list_memory_pool ===
static void test_list_memory_pool()
{
    print_header("Section: list_memory_pool");
    constexpr int REPEAT = 3;
    constexpr size_t OPS = 1 << 18;  // 256K

    // 5.1 allocate (变长, 16-128B) - 预填充页错误
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(16, 128);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS, 0);
        for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

        list_memory_pool pool;
        dense<void*> warm;
        warm.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) { warm[i] = pool.allocate(sizes[i]); touch_ptr(warm[i]); }
        for (size_t i = 0; i < OPS; ++i) pool.hard_deallocate(warm[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                void* p = pool.allocate(sizes[i]);
                touch_ptr(p);
                pool.hard_deallocate(p);
            }
            return pool.total_allocated_bytes();
        });
        print_ns("allocate(16-128B)", OPS, ns / static_cast<double>(OPS));
    }

    // 5.2 allocate + hard_deallocate (16-256B, 预填充)
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(16, 256);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS, 0);
        for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

        list_memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) { ptrs[i] = pool.allocate(sizes[i]); touch_ptr(ptrs[i]); }
        for (size_t i = 0; i < OPS; ++i) pool.hard_deallocate(ptrs[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i) ptrs[i] = pool.allocate(sizes[i]);
            for (size_t i = 0; i < OPS; ++i) pool.hard_deallocate(ptrs[i]);
            return pool.deallocation_count();
        });
        print_ns("alloc+hard_dealloc (16-256B)", 2 * OPS, ns / static_cast<double>(2 * OPS));
    }

    // 5.3 allocate + soft_deallocate (16-256B, 预填充)
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(16, 256);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS, 0);
        for (size_t i = 0; i < OPS; ++i) sizes[i] = dist(rng);

        list_memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) { ptrs[i] = pool.allocate(sizes[i]); touch_ptr(ptrs[i]); }
        for (size_t i = 0; i < OPS; ++i) pool.soft_deallocate(ptrs[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i) ptrs[i] = pool.allocate(sizes[i]);
            for (size_t i = 0; i < OPS; ++i) pool.soft_deallocate(ptrs[i]);
            return pool.deallocation_count();
        });
        print_ns("alloc+soft_dealloc (16-256B)", 2 * OPS, ns / static_cast<double>(2 * OPS));
    }

    // 5.4 allocate 大块 (1K-16K, 预填充)
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(1024, 16384);
        dense<size_t> sizes;
        sizes.increase_capacity(OPS / 16, 0);
        for (size_t i = 0; i < OPS / 16; ++i) sizes[i] = dist(rng);

        list_memory_pool pool;
        dense<void*> warm;
        warm.increase_capacity(OPS / 16, nullptr);
        for (size_t i = 0; i < OPS / 16; ++i) { warm[i] = pool.allocate(sizes[i]); touch_ptr(warm[i]); }
        for (size_t i = 0; i < OPS / 16; ++i) pool.hard_deallocate(warm[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS / 16; ++i)
            {
                void* p = pool.allocate(sizes[i]);
                touch_ptr(p);
            }
            for (size_t i = 0; i < OPS / 16; ++i) pool.hard_deallocate(warm[i]);
            return pool.total_allocated_bytes();
        });
        print_ns("allocate(1K-16K)", OPS / 16, ns / static_cast<double>(OPS / 16));
    }

    // 5.5 allocate_zeroed (预填充)
    {
        list_memory_pool pool;
        dense<void*> warm;
        warm.increase_capacity(OPS, nullptr);
        for (size_t i = 0; i < OPS; ++i) { warm[i] = pool.allocate_zeroed(64); touch_ptr(warm[i]); }
        for (size_t i = 0; i < OPS; ++i) pool.hard_deallocate(warm[i]);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                void* p = pool.allocate_zeroed(64);
                touch_ptr(p);
                pool.hard_deallocate(p);
            }
            return pool.total_allocated_bytes();
        });
        print_ns("allocate_zeroed(64B)", OPS, ns / static_cast<double>(OPS));
    }

    // 5.6 owns (命中, O(1) 位掩码)
    {
        list_memory_pool pool;
        dense<void*> ptrs;
        ptrs.increase_capacity(1000, nullptr);
        for (size_t i = 0; i < 1000; ++i) ptrs[i] = pool.allocate(64);
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < OPS_Q; ++i) sink = pool.owns(ptrs[i % 1000]);
            (void)sink;
        });
        print_ns("owns (hit)", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 5.7 owns (未命中)
    {
        list_memory_pool pool;
        for (size_t i = 0; i < 1000; ++i) touch_ptr(pool.allocate(64));
        char dummy = 0;
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile bool sink = false;
            for (size_t i = 0; i < OPS_Q; ++i) sink = pool.owns(&dummy);
            (void)sink;
        });
        print_ns("owns (miss)", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 5.8 统计查询 (total_allocated/chunk_count/allocation_count)
    {
        list_memory_pool pool;
        for (size_t i = 0; i < 1000; ++i) touch_ptr(pool.allocate(64));
        const size_t OPS_Q = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS_Q; ++i)
            {
                s += pool.total_allocated_bytes();
                s += pool.total_capacity_bytes();
                s += pool.chunk_count();
                s += pool.allocation_count();
            }
            (void)s;
        });
        print_ns("total_alloc/cap/chunk/count", OPS_Q, ns / static_cast<double>(OPS_Q));
    }

    // 5.9 reallocate (同档原位 / 跨档拷贝)
    {
        mt19937 rng(42);
        uniform_int_distribution<size_t> dist(16, 128);
        dense<size_t> sizes;
        sizes.increase_capacity(1000, 0);
        for (size_t i = 0; i < 1000; ++i) sizes[i] = dist(rng);
        dense<void*> ptrs;
        ptrs.increase_capacity(1000, nullptr);

        double ns = best_ns(REPEAT, [&]() {
            list_memory_pool pool;
            for (size_t i = 0; i < 1000; ++i) ptrs[i] = pool.allocate(sizes[i]);
            for (size_t i = 0; i < 1000; ++i) ptrs[i] = pool.reallocate(ptrs[i], sizes[i], sizes[i] + 16);
            for (size_t i = 0; i < 1000; ++i) pool.hard_deallocate(ptrs[i]);
            compiler_barrier();
            return pool.deallocation_count();
        });
        print_ns("reallocate (1000 * 3 ops)", 3000, ns / 3000.0);
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  分配器独立性能测试\n";
    cout << "  memory_pool / slab_allocator / layered_allocator / arena_allocator\n";
    cout << "  list_memory_pool\n";
    cout << "============================================================\n";

    test_memory_pool();
    test_slab_allocator();
    test_layered_allocator();
    test_arena_allocator();
    test_list_memory_pool();

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
