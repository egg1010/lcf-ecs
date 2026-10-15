// test_module_layered_allocator.cpp - 模块功能测试: layered_allocator
#include "test_common.hpp"
#include "include/part/memory/layered_allocator.hpp"

using memory::layered_allocator;

int main()
{
    t_begin("test_module_layered_allocator — 模块功能测试: layered_allocator");

    t_section(1, "按大小分层路由");
    {
        layered_allocator la;
        void* s16 = la.allocate(16);
        void* s64 = la.allocate(64);
        void* big = la.allocate(4096);
        t_check("小/中/大分配均非空", s16 && s64 && big);
        t_check("16B 归属 16B 档", la.allocation_size(s16) == 16);
        t_check("64B 归属 64B 档", la.allocation_size(s64) == 64);
        t_check("4K 归属大池", la.allocation_size(big) >= 4096);
        la.deallocate(s16);
        la.deallocate(s64);
        la.deallocate(big, 4096);
    }

    t_section(2, "交错分配归属正确 (M03 回归)");
    {
        layered_allocator la;
        void* p16[64];
        void* p32[64];
        void* p128[64];
        for (int i = 0; i < 64; ++i)
        {
            p16[i] = la.allocate(16);
            p32[i] = la.allocate(32);
            p128[i] = la.allocate(128);
        }
        bool ok = true;
        for (int i = 0; i < 64; ++i)
        {
            if (la.allocation_size(p16[i]) != 16) { ok = false; }
            if (la.allocation_size(p32[i]) != 32) { ok = false; }
            if (la.allocation_size(p128[i]) != 128) { ok = false; }
        }
        t_check("交错分配后每指针归属正确", ok);
        for (int i = 0; i < 64; ++i)
        {
            la.deallocate(p16[i]);
            la.deallocate(p32[i]);
            la.deallocate(p128[i]);
        }
    }

    t_section(3, "对齐分配 (M04 回归)");
    {
        layered_allocator la;
        bool a64 = true;
        bool a128 = true;
        void* ps[32];
        for (int i = 0; i < 32; ++i)
        {
            ps[i] = la.allocate_aligned(16, 64);
            if (reinterpret_cast<uintptr_t>(ps[i]) % 64 != 0) { a64 = false; }
        }
        t_check("allocate_aligned(16,64) 全 64B 对齐", a64);
        for (int i = 0; i < 32; ++i)
        {
            void* p = la.allocate_aligned(128, 128);
            if (reinterpret_cast<uintptr_t>(p) % 128 != 0) { a128 = false; }
            la.deallocate_aligned(p);
        }
        t_check("allocate_aligned(128,128) 全 128B 对齐", a128);
        for (int i = 0; i < 32; ++i)
        {
            la.deallocate_aligned(ps[i]);
        }
    }

    t_section(4, "过度对齐构造 (M05 回归)");
    {
        struct alignas(64) A64 { char x[64]; };
        layered_allocator la;
        A64* a = la.construct<A64>();
        t_check("construct<alignas(64)> 返回 64B 对齐",
                a && reinterpret_cast<uintptr_t>(a) % 64 == 0);
        la.destroy(a);
    }

    t_section(5, "小尺寸分档 (M10 回归)");
    {
        layered_allocator la;
        // 预热 16B 档 (首次分配触发 grow), 后续逐个验证走 slab
        void* warm = la.allocate(1);
        la.deallocate(warm, 1);
        bool ok = true;
        for (size_t n = 1; n <= 16; ++n)
        {
            size_t fb0 = la.slab(0).free_blocks();
            void* q = la.allocate(n);
            if (la.slab(0).free_blocks() != fb0 - 1) { ok = false; }
            la.deallocate(q, n);
        }
        t_check("allocate(1..16) 全部走 16B 档", ok);
    }

    t_section(6, "reset 与统计");
    {
        layered_allocator la;
        void* a = la.allocate(32);
        void* b = la.allocate(1024);
        t_check("分配后总量大于 0", la.total_allocated_bytes() > 0);
        la.deallocate(a);
        la.deallocate(b, 1024);
        la.reset();
        t_check("reset 后仍可分配", la.allocate(32) != nullptr);
    }

    return t_end("layered_allocator ");
}
