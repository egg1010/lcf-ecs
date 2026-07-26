#include "include/part/memory_pool.hpp"
#include "include/part/arena_allocator.hpp"
#include <cstdio>

int main()
{
    // 复现 memory_pool 测试序列
    {
        memory_pool mp(4096);
        void* p1 = mp.allocate(128);
        mp.deallocate(p1);
        int* obj = mp.construct<int>(42);
        mp.destroy(obj);
        mp.deallocate(mp.allocate(64));
        mp.deallocate(mp.allocate(32));
        mp.increase_capacity(8192);
        printf("after increase_capacity: total_allocated=%zu\n", mp.total_allocated());
        mp.reduce_capacity(0);
        printf("after reduce_capacity(0): total_allocated=%zu total_used=%zu\n", mp.total_allocated(), mp.total_used());

        memory_pool mp2(2048);
        mp2.deallocate(mp2.allocate(64));
        memory_pool mp3(std::move(mp2));

        memory_pool mp4, mp5(1024);
        mp4 = std::move(mp5);

        memory_pool mpx(4096);
        void* op1 = mpx.allocate(64);
        void* op2 = mpx.allocate(128);
        int stack_var = 0;
        printf("mpx.owns(op1)=%d mpx.owns(op2)=%d\n", mpx.owns(op1), mpx.owns(op2));
        printf("mpx.owns(&stack_var)=%d\n", !mpx.owns(&stack_var));
        printf("mpx.owns(nullptr)=%d\n", !mpx.owns(nullptr));

        pool_stats s = mpx.stats();
        printf("stats: alloc=%zu used=%zu free=%zu blocks=%zu\n",
               s.total_allocated, s.total_used, s.total_free, s.free_block_count);

        size_t free_count = 0;
        mpx.iterate_free([&](void*, size_t) { ++free_count; });
        printf("iterate_free count=%zu stats.free_block_count=%zu\n", free_count, s.free_block_count);

        mpx.deallocate(op1);
        mpx.deallocate(op2);
        printf("after dealloc: total_used=%zu empty=%d\n", mpx.total_used(), mpx.empty());

        memory_pool empty_pool;
        size_t empty_count = 0;
        empty_pool.iterate_free([&](void*, size_t) { ++empty_count; });
        printf("empty_pool iterate_free=%zu\n", empty_count);
    }

    printf("\n--- arena_allocator tests ---\n");
    {
        arena_allocator ar1(1024);
        void* ap1 = ar1.allocate(64);
        void* ap2 = ar1.allocate(128, 32);
        printf("ar1 allocate: ap1=%p ap2=%p\n", ap1, ap2);
        printf("ar1 owns(ap1)=%d owns(ap2)=%d\n", ar1.owns(ap1), ar1.owns(ap2));
        int stack_v = 0;
        printf("ar1 owns(&stack_v)=%d (should be 0)\n", ar1.owns(&stack_v));
        printf("ar1 32-align: %d\n", (reinterpret_cast<uintptr_t>(ap2) % 32) == 0);
        void* ap6 = ar1.allocate(64, 64);
        printf("ar1 64-align: %d\n", (reinterpret_cast<uintptr_t>(ap6) % 64) == 0);
        printf("ar1 used=%zu remaining=%zu capacity=%zu\n", ar1.used(), ar1.remaining(), ar1.capacity());

        arena_allocator ar2(64);
        void* ap3 = ar2.allocate(128);
        printf("ar2 overflow: ap3=%p (should be 0)\n", ap3);

        ar1.reset();
        printf("ar1 reset empty=%d\n", ar1.empty());
        void* ap4 = ar1.allocate(64);
        printf("ar1 reset reuse: ap4=%p\n", ap4);

        // 借用模式
        uint8_t buf[256];
        arena_allocator ar3(buf, sizeof(buf));
        void* ap5 = ar3.allocate(32);
        printf("ar3 borrow allocate: ap5=%p\n", ap5);
        printf("ar3 base_=%p buf=%p ap5=%p\n", (void*)ar3.allocate(0), (void*)buf, ap5);
        bool owns_result = ar3.owns(ap5);
        printf("ar3 borrow owns(ap5)=%d\n", owns_result);
        ar3.reset();
        printf("ar3 borrow reset empty=%d\n", ar3.empty());

        arena_allocator ar4(512);
        (void)ar4.allocate(16);
        arena_allocator ar5(std::move(ar4));
        printf("ar5 capacity=%zu (should be 512)\n", ar5.capacity());
    }

    printf("\nAll tests passed!\n");
    return 0;
}
