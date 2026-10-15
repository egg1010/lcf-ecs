// test_module_slab_allocator.cpp - 模块功能测试: slab_allocator
#include "test_common.hpp"
#include "include/part/memory/slab_allocator.hpp"

using memory::slab_allocator;

int main()
{
    t_begin("test_module_slab_allocator — 模块功能测试: slab_allocator");

    t_section(1, "固定块分配与释放");
    {
        slab_allocator sa(64);
        void* a = sa.allocate();
        void* b = sa.allocate();
        t_check("两次分配均非空", a && b);
        t_check("地址互异", a != b);
        sa.deallocate(a);
        sa.deallocate(b);
    }

    t_section(2, "块对齐保证 (M05 回归)");
    {
        slab_allocator sa(128, 64);
        bool ok = true;
        for (int i = 0; i < 64; ++i)
        {
            void* p = sa.allocate();
            if (reinterpret_cast<uintptr_t>(p) % 64 != 0) { ok = false; }
            sa.deallocate(p);
        }
        t_check("块 128B 全部满足 64B 对齐", ok);
    }

    t_section(3, "过度对齐构造 (M05 回归)");
    {
        struct alignas(64) A64 { char x[64]; };
        slab_allocator sa(128, 64);
        A64* a = sa.construct<A64>();
        t_check("construct<alignas(64)> 对齐", a && reinterpret_cast<uintptr_t>(a) % 64 == 0);
        sa.destroy(a);

        struct alignas(256) A256 { char y[256]; };
        A256* b = sa.construct<A256>();   // 块 128B 装不下 256B 对齐类型
        t_check("construct 超块容量返回 nullptr", b == nullptr);
    }

    t_section(4, "reserve 与 reset");
    {
        slab_allocator sa(32);
        sa.reserve(100);
        t_check("reserve 后空闲块 >= 100", sa.free_blocks() >= 100);
        void* p = sa.allocate();
        sa.reset();
        t_check("reset 后全部空闲", sa.free_blocks() == sa.total_blocks());
        sa.deallocate(p);
    }

    t_section(5, "统计");
    {
        slab_allocator sa(32, 16, 8);
        void* ps[4];
        for (int i = 0; i < 4; ++i) { ps[i] = sa.allocate(); }
        t_check("used_bytes 等于 4*32", sa.used_bytes() == 4 * 32);
        for (int i = 0; i < 4; ++i) { sa.deallocate(ps[i]); }
        t_check("释放后 used_bytes 为 0", sa.used_bytes() == 0);
    }

    return t_end("slab_allocator ");
}
