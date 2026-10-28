// test_module_slab_allocator.cpp - 模块功能测试: slab_allocator
#include "test_common.hpp"
#include "include/part/memory/slab_allocator.hpp"

#include <cstdint>
#include <cstring>

using memory::slab_allocator;
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

struct alignas(32) over_aligned_obj
{
    int value;
};

void test_acquire_release()
{
    t_section(1, "固定块分配与释放");
    slab_allocator pool(64, 16, 8);

    t_check_eq("块大小", pool.block_size(), static_cast<size_t>(64));

    void* p = pool.acquire();
    t_check("acquire 非空", p != nullptr);
    t_check("块被 owns 识别", pool.owns(p));
    t_check_eq("size_of 为块大小", pool.size_of(p), static_cast<size_t>(64));
    t_check("块按 64 字节对齐", reinterpret_cast<uintptr_t>(p) % 64 == 0);
    t_check("empty 为假", !pool.empty());

    memory_stats s = pool.stats();
    t_check_eq("acquire_count == 1", s.acquire_count, static_cast<size_t>(1));
    t_check_eq("live_bytes 为块大小", s.live_bytes, static_cast<size_t>(64));
    t_check("reserved >= live", s.reserved_bytes >= s.live_bytes);
    t_check("reusable 为剩余块", s.reusable_bytes >= 7 * 64);

    pool.soft_delete(p);
    t_check("软删除后 owns 为假", !pool.owns(p));
    t_check_eq("软删除后 size_of == 0", pool.size_of(p), static_cast<size_t>(0));
    t_check("软删除后 empty 为真", pool.empty());
    t_check_eq("soft_delete_count == 1", pool.stats().soft_delete_count, static_cast<size_t>(1));

    void* q = pool.acquire();
    t_check("同块大小复用", q == p);
    pool.hard_delete(q);
    t_check_eq("硬删除计数", pool.stats().hard_delete_count, static_cast<size_t>(1));
    t_check_eq("release_count == soft + hard",
               pool.stats().release_count,
               pool.stats().soft_delete_count + pool.stats().hard_delete_count);

    pool.soft_delete(q);
    t_check("重复删除被忽略", pool.stats().release_count == 2);
}

void test_zeroed_and_sized()
{
    t_section(2, "清零与编译期大小接口");
    slab_allocator pool(64, 16, 8);

    unsigned char* z = static_cast<unsigned char*>(pool.acquire_zeroed());
    t_check("acquire_zeroed 非空", z != nullptr);
    bool all_zero = true;
    for (size_t i = 0; i < 64; ++i)
    {
        if (z[i] != 0) { all_zero = false; }
    }
    t_check("整块清零", all_zero);
    pool.soft_delete(z);

    t_check("acquire_sized<65> 返回空", pool.acquire_sized<65>() == nullptr);
    void* p = pool.acquire_sized<64>();
    t_check("acquire_sized<64> 非空", p != nullptr);
    pool.soft_delete_sized<65>(p);
    t_check("过大 sized 删除被忽略", pool.owns(p));
    pool.soft_delete_sized<64>(p);
    t_check("正确 sized 软删除生效", !pool.owns(p));

    void* q = pool.acquire_sized<16>();
    t_check("小于块大小的 sized 分配非空", q != nullptr);
    pool.hard_delete_sized<16>(q);
}

void test_create_dispose()
{
    t_section(3, "对象创建与析构分离");
    slab_allocator pool(64, 16, 8);

    counted_obj::live_count = 0;
    counted_obj::dtor_count = 0;

    counted_obj* obj = pool.create<counted_obj>(11);
    t_check("create 非空", obj != nullptr);
    t_check_eq("构造函数已执行", obj ? obj->value : 0, 11);

    const memory_stats before = pool.stats();
    pool.dispose(obj);
    t_check_eq("dispose 只析构", counted_obj::dtor_count, 1);
    t_check_eq("dispose 不改变删除计数", pool.stats().release_count, before.release_count);
    t_check("dispose 后内存仍存活", pool.owns(obj));
    pool.soft_delete(obj);

    struct big_obj { unsigned char bytes[80]; };
    t_check("超过块大小的 create 返回空", pool.create<big_obj>() == nullptr);
    t_check("dispose(nullptr) 安全", (pool.dispose(static_cast<counted_obj*>(nullptr)), true));
}

void test_alignment_limit()
{
    t_section(4, "块大小与对齐限制");
    slab_allocator pool(48, 16, 8);
    t_check_eq("块大小按 16 字节取整", pool.block_size(), static_cast<size_t>(48));

    void* p = pool.acquire();
    t_check("48 字节块按 16 字节对齐", p && reinterpret_cast<uintptr_t>(p) % 16 == 0);
    pool.soft_delete(p);

    t_check("超过块内对齐的 create 返回空", pool.create<over_aligned_obj>() == nullptr);

    slab_allocator zero_pool(0);
    t_check_eq("块大小 0 时 size_of 为 0", zero_pool.block_size(), static_cast<size_t>(0));
    t_check("块大小 0 时 acquire 返回空", zero_pool.acquire() == nullptr);
    t_check("块大小 0 时 create 返回空",
            zero_pool.create<counted_obj>(1) == nullptr);
    t_check("块大小 0 时 stats 全 0",
            zero_pool.stats().reserved_bytes == 0 &&
            zero_pool.stats().live_bytes == 0);
}

void test_ownership()
{
    t_section(5, "精确归属");
    slab_allocator pool(64, 16, 8);

    int local = 0;
    void* p = pool.acquire();
    unsigned char* interior = static_cast<unsigned char*>(p) + 8;

    t_check("起始地址 owns", pool.owns(p));
    t_check("块内偏移 owns 为假", !pool.owns(interior));
    t_check("非本池指针 owns 为假", !pool.owns(&local));
    t_check_eq("块内偏移 size_of == 0", pool.size_of(interior), static_cast<size_t>(0));
    t_check_eq("非本池指针 size_of == 0", pool.size_of(&local), static_cast<size_t>(0));
    t_check("空指针 owns 为假", !pool.owns(nullptr));

    void* cross_block = static_cast<unsigned char*>(p) + pool.block_size();
    t_check("相邻块边界不属于当前块", !pool.owns(cross_block) || cross_block == nullptr);
    pool.soft_delete(p);
}

void test_growth_and_reset()
{
    t_section(6, "扩容、重置与释放全部");
    slab_allocator pool(64, 16, 4);

    void* ptrs[10];
    bool all_ok = true;
    for (size_t i = 0; i < 10; ++i)
    {
        ptrs[i] = pool.acquire();
        if (!ptrs[i]) { all_ok = false; }
    }
    t_check("超过单 chunk 容量时自动扩容", all_ok);
    t_check("total_blocks 已增长", pool.total_blocks() >= 12);
    t_check_eq("free_blocks == total - live",
               pool.free_blocks(),
               pool.total_blocks() - 10);

    for (size_t i = 0; i < 10; i += 2)
    {
        pool.soft_delete(ptrs[i]);
    }
    const size_t reserved = pool.stats().reserved_bytes;
    pool.reset();
    t_check("reset 后 live 归零", pool.stats().live_bytes == 0);
    t_check_eq("reset 后全部块空闲", pool.free_blocks(), pool.total_blocks());
    t_check_eq("reset 不归还底层内存", pool.stats().reserved_bytes, reserved);

    void* after = pool.acquire();
    t_check("reset 后仍可分配", after != nullptr);
    pool.soft_delete(after);

    pool.release_all();
    t_check_eq("release_all 后 reserved 归零", pool.stats().reserved_bytes, static_cast<size_t>(0));
    t_check_eq("release_all 后 total_blocks 归零", pool.total_blocks(), static_cast<size_t>(0));

    void* after_all = pool.acquire();
    t_check("release_all 后仍可继续分配", after_all != nullptr);
    pool.soft_delete(after_all);
}

void test_stats()
{
    t_section(7, "统计口径");
    slab_allocator pool(64, 16, 4);

    void* a = pool.acquire();
    void* b = pool.acquire();
    pool.soft_delete(a);
    pool.hard_delete(b);

    memory_stats s = pool.stats();
    t_check_eq("acquire_count == 2", s.acquire_count, static_cast<size_t>(2));
    t_check_eq("release_count == 2", s.release_count, static_cast<size_t>(2));
    t_check_eq("soft + hard == release", s.soft_delete_count + s.hard_delete_count, s.release_count);
    t_check_eq("live_bytes 归零", s.live_bytes, static_cast<size_t>(0));
    t_check("peak_live_bytes 记录峰值", s.peak_live_bytes >= 128);
    t_check_eq("reusable == free_blocks * 块大小",
               s.reusable_bytes,
               pool.free_blocks() * pool.block_size());

    pool.reset_stats();
    s = pool.stats();
    t_check("reset_stats 清零计数", s.acquire_count == 0 && s.release_count == 0);
    t_check_eq("reset_stats 峰值回到 live", s.peak_live_bytes, s.live_bytes);
}

} // namespace

int main()
{
    t_begin("test_module_slab_allocator — 模块功能测试: slab_allocator");

    test_acquire_release();
    test_zeroed_and_sized();
    test_create_dispose();
    test_alignment_limit();
    test_ownership();
    test_growth_and_reset();
    test_stats();

    return t_end("slab_allocator ");
}
