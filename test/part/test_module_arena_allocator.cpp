// test_module_arena_allocator.cpp - 模块功能测试: arena_allocator
#include "test_common.hpp"
#include "include/part/memory/arena_allocator.hpp"

#include <cstdint>
#include <cstring>

using memory::arena_allocator;
using memory::arena_scope;
using memory::double_buffered_arena;
using memory::memory_stats;

namespace {

struct counted_obj
{
    static inline int live_count = 0;
    static inline int dtor_count = 0;

    int value;

    explicit counted_obj(int v) noexcept : value(v) { ++live_count; }
    ~counted_obj() noexcept { --live_count; ++dtor_count; }
};

struct alignas(64) align64_obj
{
    int value;
};

void test_basic()
{
    t_section(1, "线性分配");
    arena_allocator arena(1024);

    t_check_eq("容量", arena.capacity(), static_cast<size_t>(1024));
    t_check("初始为空", arena.empty() && arena.used() == 0);

    unsigned char* a = static_cast<unsigned char*>(arena.acquire(64));
    t_check("acquire(64) 非空", a != nullptr);
    t_check("满足 16 字节基础对齐", a && reinterpret_cast<uintptr_t>(a) % 16 == 0);
    t_check("起始地址被 owns 识别", arena.owns(a));
    t_check("used 前移", arena.used() >= 64);
    t_check("remaining 减少", arena.remaining() <= 1024 - 64);
    t_check("empty 为假", !arena.empty());

    unsigned char* b = static_cast<unsigned char*>(arena.acquire(100));
    t_check("第二次分配非空", b != nullptr);
    t_check("两次分配不重叠", b != a);
    t_check("used 继续前移", arena.used() >= 164);

    unsigned char* z = static_cast<unsigned char*>(arena.acquire_zeroed(48));
    t_check("acquire_zeroed 非空", z != nullptr);
    bool all_zero = true;
    for (size_t i = 0; i < 48; ++i)
    {
        if (z[i] != 0) { all_zero = false; }
    }
    t_check("清零区域全 0", all_zero);

    t_check("acquire(0) 返回空", arena.acquire(0) == nullptr);
    t_check("acquire_aligned(64, 0) 返回空", arena.acquire_aligned(64, 0) == nullptr);
    t_check("非 2 的幂对齐返回空", arena.acquire_aligned(64, 24) == nullptr);
    t_check("容量不足返回空", arena.acquire(2048) == nullptr);
    t_check("失败请求不改变 used", arena.used() >= 164 && arena.used() <= 1024);
}

void test_alignment()
{
    t_section(2, "对齐分配");
    arena_allocator arena(4096);

    void* p64 = arena.acquire_aligned(100, 64);
    void* p4096 = arena.acquire_aligned(64, 4096);
    t_check("64 字节对齐分配非空", p64 != nullptr);
    t_check("4096 字节对齐分配非空", p4096 != nullptr);
    t_check("64 字节对齐成立",
            p64 && reinterpret_cast<uintptr_t>(p64) % 64 == 0);
    t_check("4096 字节对齐成立",
            p4096 && reinterpret_cast<uintptr_t>(p4096) % 4096 == 0);
    t_check("对齐分配被 owns 识别", arena.owns(p64) && arena.owns(p4096));

    align64_obj* obj = arena.create<align64_obj>();
    t_check("create 过对齐类型非空", obj != nullptr);
    t_check("create 满足 alignof(T)",
            obj && reinterpret_cast<uintptr_t>(obj) % alignof(align64_obj) == 0);
    arena.dispose(obj);
}

void test_ownership()
{
    t_section(3, "精确归属");
    arena_allocator arena(512);

    int local = 0;
    unsigned char* p = static_cast<unsigned char*>(arena.acquire(64));
    t_check("起始地址 owns", arena.owns(p));
    t_check("内部指针 owns 为假", !arena.owns(p + 8));
    t_check("非本池指针 owns 为假", !arena.owns(&local));
    t_check("空指针 owns 为假", !arena.owns(nullptr));

    arena.reset();
    t_check("reset 后旧指针 owns 为假", !arena.owns(p));

    arena_allocator empty_arena;
    t_check("空 arena capacity 为 0", empty_arena.capacity() == 0);
    t_check("空 arena acquire 返回空", empty_arena.acquire(16) == nullptr);

    alignas(64) unsigned char buffer[256];
    arena_allocator external(buffer, sizeof(buffer));
    t_check_eq("外部缓冲区容量", external.capacity(), sizeof(buffer));
    void* q = external.acquire(32);
    t_check("外部缓冲区可分配", q != nullptr && external.owns(q));
    t_check("外部缓冲区地址在范围内",
            static_cast<unsigned char*>(q) >= buffer &&
            static_cast<unsigned char*>(q) < buffer + sizeof(buffer));
}

void test_save_restore()
{
    t_section(4, "保存点与回退");
    arena_allocator arena(1024);

    const size_t outer = arena.save_point();
    void* a = arena.acquire(64);
    const size_t after_a = arena.save_point();
    t_check("外层分配非空", a != nullptr);
    t_check("保存点前移", after_a >= 64);

    void* b = arena.acquire(128);
    void* c = arena.acquire(256);
    t_check("子作用域分配非空", b && c);
    t_check("子作用域块均归属", arena.owns(b) && arena.owns(c));

    const size_t live_before = arena.stats().live_bytes;
    arena.restore(after_a);
    t_check("回退后 live 减少", arena.stats().live_bytes < live_before);
    t_check("回退后子块失效", !arena.owns(b) && !arena.owns(c));
    t_check("回退后外层块仍有效", arena.owns(a));

    void* b2 = arena.acquire(128);
    t_check("回退后可重新分配", b2 != nullptr);
    t_check("重新分配复用回退地址", b2 == b);
    arena.restore(after_a);
    arena.restore(outer);
    t_check("回退到最外层后全部失效", !arena.owns(a) && arena.used() == 0);
    t_check_eq("最外层回退后 live 归零", arena.stats().live_bytes, static_cast<size_t>(0));

    void* p = arena.acquire(64);
    const size_t point = arena.save_point();
    t_check("切分检查分配非空", p != nullptr);
    arena.restore(point - 1);
    t_check("回退点切分分配时被拒绝", arena.owns(p));
    arena.restore(point + 1024);
    t_check("回退点超过 used 时无操作", arena.owns(p));
    arena.reset();
}

void test_scope()
{
    t_section(5, "作用域守卫");
    arena_allocator arena(512);

    void* before = arena.acquire(32);
    const size_t mark_before_scope = arena.used();
    void* inner = nullptr;
    {
        arena_scope scope(arena);
        inner = arena.acquire(128);
        t_check("作用域内分配非空", inner != nullptr);
        t_check("作用域内 used 前移", arena.used() > mark_before_scope);
    }
    t_check("作用域退出后内部分配失效", !arena.owns(inner));
    t_check("作用域退出后外层分配有效", arena.owns(before));
    t_check_eq("作用域退出后 used 复原", arena.used(), mark_before_scope);
}

void test_create_dispose()
{
    t_section(6, "对象与数组生命周期");
    arena_allocator arena(1024);

    counted_obj::live_count = 0;
    counted_obj::dtor_count = 0;

    counted_obj* obj = arena.create<counted_obj>(21);
    t_check("create 非空", obj != nullptr);
    t_check_eq("构造函数已执行", obj ? obj->value : 0, 21);
    const memory_stats before = arena.stats();
    arena.dispose(obj);
    t_check_eq("dispose 只析构", counted_obj::dtor_count, 1);
    t_check_eq("dispose 不改变存活字节", arena.stats().live_bytes, before.live_bytes);
    t_check_eq("dispose 不改变分配计数", arena.stats().acquire_count, before.acquire_count);
    t_check("dispose 后内存仍归属", arena.owns(obj));

    counted_obj::live_count = 0;
    counted_obj::dtor_count = 0;
    counted_obj* arr = arena.create_array<counted_obj>(4, 8);
    t_check("create_array 非空", arr != nullptr);
    t_check_eq("四个元素已构造", counted_obj::live_count, 4);
    t_check("数组起始地址归属", arena.owns(arr));
    arena.dispose_array(arr, 4);
    t_check_eq("dispose_array 全部析构", counted_obj::dtor_count, 4);
    t_check("dispose_array 不改变分配计数", arena.stats().acquire_count == before.acquire_count + 1);
    t_check("create_array(0) 返回空", arena.create_array<counted_obj>(0, 1) == nullptr);
}

void test_reset_stats()
{
    t_section(7, "统计与重置");
    arena_allocator arena(1024);

    void* a = arena.acquire(64);
    void* b = arena.acquire_aligned(128, 64);
    t_check("统计测试分配非空", a && b);

    memory_stats s = arena.stats();
    t_check_eq("reserved 等于容量", s.reserved_bytes, arena.capacity());
    t_check("live 为请求字节和", s.live_bytes >= 192);
    t_check_eq("reusable 等于剩余", s.reusable_bytes, arena.remaining());
    t_check("reserved >= live", s.reserved_bytes >= s.live_bytes);
    t_check_eq("acquire_count == 2", s.acquire_count, static_cast<size_t>(2));
    t_check("arena 不产生删除计数",
            s.release_count == 0 && s.soft_delete_count == 0 && s.hard_delete_count == 0);

    arena.reset_stats();
    s = arena.stats();
    t_check("reset_stats 清零计数", s.acquire_count == 0);
    t_check_eq("reset_stats 峰值回到 live", s.peak_live_bytes, s.live_bytes);

    arena.reset();
    s = arena.stats();
    t_check_eq("reset 后 live 归零", s.live_bytes, static_cast<size_t>(0));
    t_check_eq("reset 后 used 归零", arena.used(), static_cast<size_t>(0));
    t_check_eq("reset 保留容量", s.reserved_bytes, arena.capacity());
    t_check("reset 后仍可分配", arena.acquire(64) != nullptr);
}

void test_move_and_swap()
{
    t_section(8, "移动与交换");
    arena_allocator arena(256);
    void* p = arena.acquire(64);
    t_check("移动前分配非空", p != nullptr);

    arena_allocator moved(std::move(arena));
    t_check("移动后新 arena 拥有原分配", moved.owns(p));
    t_check_eq("移动后容量转移", moved.capacity(), static_cast<size_t>(256));
    t_check("移动后源 arena 为空", arena.capacity() == 0 && arena.acquire(16) == nullptr);

    arena_allocator other(128);
    moved.swap(other);
    t_check("交换后原分配随容量转移", other.owns(p));
    t_check_eq("交换后容量互换", moved.capacity(), static_cast<size_t>(128));
}

void test_double_buffered()
{
    t_section(9, "双帧 arena");
    double_buffered_arena buffers(256);

    unsigned char* frame0 = static_cast<unsigned char*>(buffers.current().acquire(64));
    t_check("帧 0 分配非空", frame0 != nullptr);
    if (frame0) { frame0[0] = 0xAB; }

    buffers.flip();
    t_check("翻转后当前帧为空", buffers.current().used() == 0);
    t_check("翻转后上一帧仍可读", buffers.previous().owns(frame0));
    t_check("上一帧数据保留", frame0 && frame0[0] == 0xAB);

    unsigned char* frame1 = static_cast<unsigned char*>(buffers.current().acquire(128));
    t_check("帧 1 分配非空", frame1 != nullptr);
    t_check("两帧地址不同", frame1 != frame0);

    buffers.flip();
    t_check("再次翻转后帧 0 重置", buffers.current().used() == 0);
    t_check("再次翻转后帧 1 可读", buffers.previous().owns(frame1));
}

} // namespace

int main()
{
    t_begin("test_module_arena_allocator — 模块功能测试: arena_allocator");

    test_basic();
    test_alignment();
    test_ownership();
    test_save_restore();
    test_scope();
    test_create_dispose();
    test_reset_stats();
    test_move_and_swap();
    test_double_buffered();

    return t_end("arena_allocator ");
}
