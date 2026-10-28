// test_module_layered_allocator.cpp - 模块功能测试: layered_allocator
#include "test_common.hpp"
#include "include/part/memory/layered_allocator.hpp"

#include <cstdint>
#include <cstring>

using memory::layered_allocator;
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

void test_routing()
{
    t_section(1, "按大小分层路由");
    layered_allocator pool;

    t_check_eq("slab 上限", pool.slab_max(), static_cast<size_t>(128));
    t_check_eq("16B 层块大小", pool.slab(0).block_size(), static_cast<size_t>(16));
    t_check_eq("128B 层块大小", pool.slab(3).block_size(), static_cast<size_t>(128));

    void* s8 = pool.acquire(8);
    void* s32 = pool.acquire(32);
    void* s100 = pool.acquire(100);
    void* s128 = pool.acquire(128);
    void* big = pool.acquire(129);
    t_check("各层分配均非空", s8 && s32 && s100 && s128 && big);
    t_check("全部被 owns 识别",
            pool.owns(s8) && pool.owns(s32) && pool.owns(s100) &&
            pool.owns(s128) && pool.owns(big));
    t_check_eq("8B 请求落在 16B 层", pool.size_of(s8), static_cast<size_t>(16));
    t_check_eq("32B 请求落在 32B 层", pool.size_of(s32), static_cast<size_t>(32));
    t_check_eq("100B 请求落在 128B 层", pool.size_of(s100), static_cast<size_t>(128));
    t_check_eq("128B 请求落在 128B 层", pool.size_of(s128), static_cast<size_t>(128));
    t_check("129B 请求落在链式大池", pool.size_of(big) >= 129);

    pool.soft_delete(s8);
    pool.soft_delete(s32);
    pool.soft_delete(s100);
    pool.soft_delete(s128);
    pool.soft_delete(big);
    t_check("全部释放后 empty", pool.empty());
}

void test_reuse()
{
    t_section(2, "分层复用");
    layered_allocator pool;

    void* small = pool.acquire(64);
    pool.soft_delete(small);
    void* small2 = pool.acquire(64);
    t_check("slab 层软删除后复用", small2 == small);
    pool.hard_delete(small2);

    void* mid = pool.acquire(500);
    pool.soft_delete(mid);
    void* mid2 = pool.acquire(500);
    t_check("大池 slot 层软删除后复用", mid2 == mid);
    pool.hard_delete(mid2);

    void* big = pool.acquire(8192);
    pool.soft_delete(big);
    void* big2 = pool.acquire(8192);
    t_check("大池大块软删除后复用", big2 == big);
    pool.hard_delete(big2);
}

void test_sized()
{
    t_section(3, "编译期大小接口");
    layered_allocator pool;

    void* p = pool.acquire_sized<33>();
    t_check("acquire_sized<33> 非空", p != nullptr);
    t_check_eq("33B 请求落在 64B 层", pool.size_of(p), static_cast<size_t>(64));

    const size_t before = pool.stats().release_count;
    pool.soft_delete_sized<129>(p);
    t_check("跨层错误 sized 删除被忽略", pool.owns(p));
    t_check_eq("忽略时不计数", pool.stats().release_count, before);

    pool.soft_delete_sized<33>(p);
    t_check("正确 sized 软删除生效", !pool.owns(p));

    void* big = pool.acquire_sized<8192>();
    t_check("acquire_sized<8192> 非空", big != nullptr);
    pool.hard_delete_sized<8192>(big);
    t_check("大池 sized 硬删除生效", !pool.owns(big));
}

void test_aligned()
{
    t_section(4, "对齐分配路由");
    layered_allocator pool;

    t_check("acquire_aligned(0, 16) 返回空", pool.acquire_aligned(0, 16) == nullptr);
    t_check("acquire_aligned(64, 0) 返回空", pool.acquire_aligned(64, 0) == nullptr);
    t_check("非 2 的幂对齐返回空", pool.acquire_aligned(64, 24) == nullptr);

    void* slab_aligned = pool.acquire_aligned(64, 64);
    t_check("对齐不超过层块大小时走 slab", slab_aligned != nullptr);
    t_check("slab 对齐成立",
            slab_aligned && reinterpret_cast<uintptr_t>(slab_aligned) % 64 == 0);
    t_check_eq("slab 对齐 size_of 为层块大小", pool.size_of(slab_aligned), static_cast<size_t>(64));

    void* big_aligned = pool.acquire_aligned(32, 64);
    t_check("对齐超过层块大小时走大池", big_aligned != nullptr);
    t_check("大池 64 字节对齐成立",
            big_aligned && reinterpret_cast<uintptr_t>(big_aligned) % 64 == 0);
    t_check_eq("大池对齐 size_of 为请求值", pool.size_of(big_aligned), static_cast<size_t>(32));

    void* page_aligned = pool.acquire_aligned(100, 4096);
    t_check("4096 字节对齐成立",
            page_aligned && reinterpret_cast<uintptr_t>(page_aligned) % 4096 == 0);

    pool.soft_delete(slab_aligned);
    pool.hard_delete(big_aligned);
    pool.soft_delete(page_aligned);
    t_check("对齐分配统一释放", !pool.owns(slab_aligned) &&
            !pool.owns(big_aligned) && !pool.owns(page_aligned));
}

void test_create_dispose()
{
    t_section(5, "对象与数组生命周期");
    layered_allocator pool;

    counted_obj::live_count = 0;
    counted_obj::dtor_count = 0;

    counted_obj* obj = pool.create<counted_obj>(13);
    t_check("create 非空", obj != nullptr);
    t_check_eq("构造函数已执行", obj ? obj->value : 0, 13);
    const memory_stats before = pool.stats();
    pool.dispose(obj);
    t_check_eq("dispose 只析构", counted_obj::dtor_count, 1);
    t_check_eq("dispose 不改变删除计数", pool.stats().release_count, before.release_count);
    pool.soft_delete(obj);

    align64_obj* over = pool.create<align64_obj>();
    t_check("create 过对齐类型非空", over != nullptr);
    t_check("create 满足 alignof(T)",
            over && reinterpret_cast<uintptr_t>(over) % alignof(align64_obj) == 0);
    pool.dispose(over);
    pool.soft_delete(over);

    counted_obj::live_count = 0;
    counted_obj::dtor_count = 0;
    counted_obj* arr = pool.create_array<counted_obj>(3, 5);
    t_check("create_array 非空", arr != nullptr);
    t_check_eq("三个元素均已构造", counted_obj::live_count, 3);
    t_check("create_array 返回起始地址并归属本池", arr && pool.owns(arr));
    pool.dispose_array(arr, 3);
    t_check_eq("dispose_array 全部析构", counted_obj::dtor_count, 3);
    t_check("dispose_array 不释放内存", pool.owns(arr));
    pool.soft_delete(arr);
    t_check("数组内存释放后无效", !pool.owns(arr));

    t_check("create_array(0) 返回空",
            pool.create_array<counted_obj>(0, 1) == nullptr);
}

void test_ownership()
{
    t_section(6, "精确归属");
    layered_allocator pool;

    int local = 0;
    void* p = pool.acquire(100);
    void* big = pool.acquire(1000);
    unsigned char* interior = static_cast<unsigned char*>(p) + 16;

    t_check("slab 起始地址 owns", pool.owns(p));
    t_check("slab 内部指针 owns 为假", !pool.owns(interior));
    t_check_eq("slab 内部指针 size_of == 0", pool.size_of(interior), static_cast<size_t>(0));
    t_check("大池起始地址 owns", pool.owns(big));
    t_check("大池内部指针 owns 为假",
            !pool.owns(static_cast<unsigned char*>(big) + 16));
    t_check("非本池指针 owns 为假", !pool.owns(&local));

    pool.soft_delete(p);
    pool.hard_delete(big);
    t_check("删除后 owns 为假", !pool.owns(p) && !pool.owns(big));
}

void test_capacity()
{
    t_section(7, "重分配");
    layered_allocator pool;

    unsigned char* same = static_cast<unsigned char*>(pool.acquire(100));
    t_check("同层重分配初始非空", same != nullptr);
    for (size_t i = 0; i < 100; ++i)
    {
        same[i] = static_cast<unsigned char>(i);
    }
    t_check("同层缩容可原位", pool.resize_inplace(same, 100, 120));
    t_check("同层缩容地址不变", pool.owns(same));
    pool.soft_delete(same);

    unsigned char* cross = static_cast<unsigned char*>(pool.acquire(32));
    t_check("跨层重分配初始非空", cross != nullptr);
    for (size_t i = 0; i < 32; ++i)
    {
        cross[i] = static_cast<unsigned char>(i + 1);
    }
    t_check("跨层不可原位", !pool.resize_inplace(cross, 32, 64));
    unsigned char* grown = static_cast<unsigned char*>(pool.resize(cross, 32, 64));
    t_check("跨层重分配非空", grown != nullptr);
    bool copied = true;
    for (size_t i = 0; i < 32; ++i)
    {
        if (grown[i] != static_cast<unsigned char>(i + 1)) { copied = false; }
    }
    t_check("跨层重分配保留内容", copied);
    t_check("跨层重分配旧块已释放", !pool.owns(cross));
    pool.hard_delete(grown);

    unsigned char* big = static_cast<unsigned char*>(pool.acquire(6000));
    t_check("大池重分配初始非空", big != nullptr);
    big[0] = 42;
    t_check("大池缩容可原位", pool.resize_inplace(big, 6000, 5000));
    unsigned char* big2 = static_cast<unsigned char*>(pool.resize(big, 6000, 12000));
    t_check("大池扩容移动重分配非空", big2 != nullptr);
    t_check("大池移动重分配保留内容", big2[0] == 42);
    t_check("大池移动后旧块已释放", !pool.owns(big));
    pool.hard_delete(big2);

    void* null_ptr = pool.resize(nullptr, 0, 64);
    t_check("resize(nullptr) 等价 acquire", null_ptr != nullptr);
    t_check("resize 目标 0 释放并返回空", pool.resize(null_ptr, 64, 0) == nullptr);
}

void test_stats_and_reset()
{
    t_section(8, "统计与重置");
    layered_allocator pool;

    void* small = pool.acquire(64);
    void* big = pool.acquire(8192);
    void* aligned = pool.acquire_aligned(100, 64);
    t_check("统计测试分配非空", small && big && aligned);

    memory_stats s = pool.stats();
    t_check("reserved >= live", s.reserved_bytes >= s.live_bytes);
    t_check("reusable <= reserved - live",
            s.reusable_bytes <= s.reserved_bytes - s.live_bytes);
    t_check_eq("acquire_count == 3", s.acquire_count, static_cast<size_t>(3));
    size_t child_live = pool.big_pool().stats().live_bytes;
    for (size_t i = 0; i < 4; ++i)
    {
        child_live += pool.slab(i).stats().live_bytes;
    }
    t_check_eq("分层存活与子池合计一致", s.live_bytes, child_live);
    t_check("分层峰值覆盖当前存活", s.peak_live_bytes >= s.live_bytes);

    pool.soft_delete(small);
    pool.hard_delete(big);
    pool.soft_delete(aligned);
    s = pool.stats();
    t_check_eq("release_count == 3", s.release_count, static_cast<size_t>(3));
    t_check("删除计数等于软 + 硬",
            s.release_count == s.soft_delete_count + s.hard_delete_count);
    t_check("删除后 live 归零", s.live_bytes == 0);
    t_check("peak >= live", s.peak_live_bytes >= s.live_bytes);

    pool.reset();
    t_check("reset 后 live 归零", pool.stats().live_bytes == 0);
    t_check("reset 保留保留容量", pool.stats().reserved_bytes > 0);
    void* after_reset = pool.acquire(64);
    t_check("reset 后仍可分配", after_reset != nullptr);
    pool.soft_delete(after_reset);

    pool.reset_stats();
    s = pool.stats();
    t_check("reset_stats 清零计数", s.acquire_count == 0 && s.release_count == 0);
    t_check_eq("reset_stats 峰值回到 live", s.peak_live_bytes, s.live_bytes);

    pool.release_all();
    s = pool.stats();
    t_check("release_all 后 reserved 归零", s.reserved_bytes == 0);
    t_check("release_all 后 reusable 归零", s.reusable_bytes == 0);
    void* after = pool.acquire(64);
    t_check("release_all 后仍可继续分配", after != nullptr);
    pool.soft_delete(after);
}

void test_range_index()
{
    t_section(9, "多 chunk 范围索引与移动");
    constexpr size_t count = 600;
    static void* p64[count];
    static void* p16[count];

    layered_allocator pool;
    for (size_t i = 0; i < count; ++i)
    {
        p64[i] = pool.acquire(64);
        p16[i] = pool.acquire(16);
    }
    bool all_live = true;
    for (size_t i = 0; i < count; ++i)
    {
        if (!pool.owns(p64[i]) || pool.size_of(p64[i]) != 64 ||
            !pool.owns(p16[i]) || pool.size_of(p16[i]) != 16)
        {
            all_live = false;
        }
    }
    t_check("跨多个 chunk 的起始地址全部有效", all_live);
    t_check("首尾 chunk 内部地址无效",
            !pool.owns(static_cast<unsigned char*>(p64[0]) + 8) &&
            !pool.owns(static_cast<unsigned char*>(p64[count - 1]) + 8));
    t_check_eq("首尾 chunk 内部地址 size_of 为 0",
               pool.size_of(static_cast<unsigned char*>(p64[count - 1]) + 8),
               static_cast<size_t>(0));

    for (size_t i = 0; i < count / 3; ++i)
    {
        pool.soft_delete(p64[i]);
        pool.soft_delete(p16[i]);
    }
    t_check("删除后旧 chunk 地址无效", !pool.owns(p64[0]) && !pool.owns(p16[0]));
    t_check("未删除新 chunk 地址仍有效",
            pool.owns(p64[count - 1]) && pool.owns(p16[count - 1]));

    layered_allocator moved(std::move(pool));
    t_check("移动构造保留范围索引",
            moved.owns(p64[count - 1]) && moved.size_of(p64[count - 1]) == 64);
    t_check("移动源不再识别旧地址", !pool.owns(p64[count - 1]));
    void* source_new = pool.acquire(32);
    t_check("移动源可继续分配", source_new != nullptr);
    pool.soft_delete(source_new);

    layered_allocator assigned;
    void* assigned_old = assigned.acquire(64);
    assigned.soft_delete(assigned_old);
    assigned = std::move(moved);
    t_check("移动赋值保留范围索引",
            assigned.owns(p16[count - 1]) && assigned.size_of(p16[count - 1]) == 16);
    t_check("移动赋值后源不再识别旧地址", !moved.owns(p16[count - 1]));
    const memory_stats moved_stats = moved.stats();
    t_check("移动赋值源统计为空",
            moved_stats.live_bytes == 0 && moved_stats.peak_live_bytes == 0);

    assigned.release_all();
    t_check("release_all 清空范围索引", !assigned.owns(p64[count - 1]));
    void* after_release = assigned.acquire(128);
    t_check("release_all 后可重建范围索引并分配", after_release != nullptr);
    t_check_eq("重建后 size_of 正确", assigned.size_of(after_release), static_cast<size_t>(128));
    assigned.hard_delete(after_release);
}

} // namespace

int main()
{
    t_begin("test_module_layered_allocator — 模块功能测试: layered_allocator");

    test_routing();
    test_reuse();
    test_sized();
    test_aligned();
    test_create_dispose();
    test_ownership();
    test_capacity();
    test_stats_and_reset();
    test_range_index();

    return t_end("layered_allocator ");
}
