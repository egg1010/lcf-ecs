// test_module_arena_allocator.cpp - 模块功能测试: arena_allocator
#include "test_common.hpp"
#include "include/part/memory/arena_allocator.hpp"

using memory::arena_allocator;
using memory::arena_scope;

int main()
{
    t_begin("test_module_arena_allocator — 模块功能测试: arena_allocator");

    t_section(1, "线性分配");
    {
        arena_allocator a(1024);
        void* p1 = a.allocate(64);
        void* p2 = a.allocate(128);
        t_check("两次分配均非空", p1 && p2);
        t_check("地址互异", p1 != p2);
        t_check("used 累计 192", a.used() == 192);
    }

    t_section(2, "边界拒绝 (M11 回归)");
    {
        arena_allocator z(0);
        t_check("零容量 allocate 返回 nullptr", z.allocate(16) == nullptr);
        t_check("零容量 used/capacity 为 0", z.used() == 0 && z.capacity() == 0);

        arena_allocator s(64);
        t_check("超容量 allocate 返回 nullptr", s.allocate(1000) == nullptr);
        t_check("非 2 幂对齐返回 nullptr", s.allocate(16, 3) == nullptr);
        t_check("align=0 返回 nullptr", s.allocate(16, 0) == nullptr);
        t_check("失败后容量不变", s.remaining() == 64);
    }

    t_section(3, "对齐分配");
    {
        arena_allocator a(1024);
        bool ok = true;
        for (int i = 0; i < 16; ++i)
        {
            void* p = a.allocate(16, 64);
            if (reinterpret_cast<uintptr_t>(p) % 64 != 0) { ok = false; }
        }
        t_check("allocate(16,64) 全 64B 对齐", ok);
    }

    t_section(4, "save_point / restore");
    {
        arena_allocator a(1024);
        void* p0 = a.allocate(96);          // 16B 对齐尺寸, 避免填充
        size_t mark = a.save_point();
        void* p1 = a.allocate(192);
        t_check("两次分配均非空", p0 && p1);
        t_check("分配后 used 增长", a.used() == 288);
        a.restore(mark);
        t_check("restore 回到保存点", a.used() == 96);
    }

    t_section(5, "作用域守卫");
    {
        arena_allocator a(1024);
        void* p0 = a.allocate(48);
        {
            arena_scope g(a);
            void* p1 = a.allocate(96);
            t_check("作用域内分配非空", p0 && p1);
            t_check("作用域内 used=144", a.used() == 144);
        }
        t_check("作用域退出自动回滚", a.used() == 48);
    }

    t_section(6, "reset 与借用模式");
    {
        arena_allocator a(256);
        void* p0 = a.allocate(100);
        a.reset();
        t_check("reset 后 used 为 0", a.used() == 0);
        t_check("reset 后可再分配", a.allocate(100) != nullptr);
        t_check("reset 前分配非空", p0 != nullptr);

        char buf[128];
        arena_allocator b(buf, sizeof(buf));
        void* p = b.allocate(64);
        t_check("借用模式分配非空", p != nullptr);
        t_check("借用模式 owns 匹配", b.owns(p));
        t_check("借用容量正确", b.capacity() == 128);
    }

    return t_end("arena_allocator ");
}
