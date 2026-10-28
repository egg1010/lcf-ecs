// test_module_list_memory_pool.cpp - 模块功能测试: list_memory_pool
#include "test_common.hpp"
#include "include/part/memory/list_memory_pool.hpp"

#include <cstdint>
#include <cstring>

using memory::list_memory_pool;
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

void test_lifecycle()
{
    t_section(1, "生命周期与计数");
    list_memory_pool pool;

    t_check("acquire(0) 返回空", pool.acquire(0) == nullptr);
    t_check("acquire(SIZE_MAX) 返回空", pool.acquire(SIZE_MAX) == nullptr);

    void* small = pool.acquire(64);
    t_check("小块分配非空", small != nullptr);
    t_check("小块被 owns 识别", pool.owns(small));
    t_check_eq("小块 size_of 为档位大小", pool.size_of(small), static_cast<size_t>(64));
    t_check("empty 为假", !pool.empty());

    memory_stats s = pool.stats();
    t_check_eq("acquire_count == 1", s.acquire_count, static_cast<size_t>(1));
    t_check_eq("live_bytes 为档位大小", s.live_bytes, static_cast<size_t>(64));

    pool.soft_delete(small);
    t_check("软删除后 owns 为假", !pool.owns(small));
    t_check_eq("软删除后 size_of == 0", pool.size_of(small), static_cast<size_t>(0));
    t_check("软删除后 empty 为真", pool.empty());

    s = pool.stats();
    t_check_eq("release_count == 1", s.release_count, static_cast<size_t>(1));
    t_check_eq("soft_delete_count == 1", s.soft_delete_count, static_cast<size_t>(1));

    pool.soft_delete(small);
    pool.hard_delete(small);
    pool.soft_delete(nullptr);
    pool.hard_delete(nullptr);
    t_check("重复/交叉/空删除不计数", pool.stats().release_count == 1);

    void* big = pool.acquire(8192);
    t_check("大块分配非空", big != nullptr);
    t_check_eq("大块 size_of 为请求大小", pool.size_of(big), static_cast<size_t>(8192));
    pool.hard_delete(big);
    t_check("大块硬删除后 owns 为假", !pool.owns(big));
    t_check_eq("hard_delete_count == 1", pool.stats().hard_delete_count, static_cast<size_t>(1));
    t_check_eq("release_count == soft + hard",
               pool.stats().release_count,
               pool.stats().soft_delete_count + pool.stats().hard_delete_count);
}

void test_zeroed_and_isolation()
{
    t_section(2, "清零与分配隔离");
    list_memory_pool pool;

    unsigned char* z = static_cast<unsigned char*>(pool.acquire_zeroed(96));
    t_check("acquire_zeroed 非空", z != nullptr);
    bool all_zero = true;
    for (size_t i = 0; i < 96; ++i)
    {
        if (z[i] != 0) { all_zero = false; }
    }
    t_check("清零区域全 0", all_zero);
    pool.soft_delete(z);

    void* a = pool.acquire(128);
    void* b = pool.acquire(128);
    void* c = pool.acquire(128);
    t_check("三次分配均非空", a && b && c);
    t_check("地址互异", a != b && b != c && a != c);
    t_check("三块均被 owns 识别", pool.owns(a) && pool.owns(b) && pool.owns(c));
    pool.soft_delete(c);
    pool.soft_delete(b);
    pool.soft_delete(a);
}

void test_small_reuse()
{
    t_section(3, "小块软删除复用");
    list_memory_pool pool;

    void* p = pool.acquire(64);
    pool.soft_delete(p);
    void* q = pool.acquire(64);
    t_check("同档小块复用软删除 slot", q == p);
    t_check("复用块 size_of 一致", pool.size_of(q) == 64);
    pool.hard_delete(q);

    void* r = pool.acquire_sized<128>();
    t_check("acquire_sized<128> 非空", r != nullptr);
    pool.soft_delete_sized<128>(r);
    void* r2 = pool.acquire_sized<128>();
    t_check("sized 同档复用", r2 == r);
    pool.hard_delete_sized<128>(r2);
}

void test_big_soft_cache()
{
    t_section(4, "大块软删除按档缓存");
    list_memory_pool pool;

    void* a = pool.acquire(8192);
    t_check("8192 大块非空", a != nullptr);
    pool.soft_delete(a);

    void* b = pool.acquire(8192);
    t_check("同档同大小请求复用软删除大块", b == a);
    t_check_eq("复用后 size_of 保持请求大小", pool.size_of(b), static_cast<size_t>(8192));
    pool.hard_delete(b);

    void* c = pool.acquire(6000);
    t_check("6000 大块非空", c != nullptr);
    pool.soft_delete(c);

    void* d = pool.acquire(5000);
    t_check("同档较小请求复用软删除大块", d == c);
    t_check_eq("复用后 size_of 为较小请求", pool.size_of(d), static_cast<size_t>(5000));
    pool.hard_delete(d);

    void* e = pool.acquire(12000);
    t_check("跨档大块分配非空", e != nullptr);
    pool.soft_delete(e);
    void* f = pool.acquire(12000);
    t_check("跨档同大小请求仍复用", f == e);
    void* g = pool.acquire(20000);
    t_check("更大跨档请求不复用较小块", g != e && g != nullptr);
    t_check_eq("新块 size_of 为请求值", pool.size_of(g), static_cast<size_t>(20000));
    pool.hard_delete(g);
    pool.hard_delete(f);
}

void test_sized()
{
    t_section(5, "编译期大小接口");
    list_memory_pool pool;

    void* small = pool.acquire_sized<64>();
    t_check("acquire_sized<64> 非空", small != nullptr);
    const size_t before = pool.stats().release_count;
    pool.soft_delete_sized<4000>(small);
    t_check("过大的 sized 删除被忽略", pool.owns(small));
    t_check_eq("忽略时不计数", pool.stats().release_count, before);
    pool.hard_delete_sized<64>(small);
    t_check("正确 sized 硬删除生效", !pool.owns(small));

    void* big = pool.acquire_sized<8192>();
    t_check("acquire_sized<8192> 非空", big != nullptr);
    pool.soft_delete_sized<16384>(big);
    t_check("大块过大 sized 删除被忽略", pool.owns(big));
    pool.soft_delete_sized<8192>(big);
    t_check("大块正确 sized 软删除生效", !pool.owns(big));

    void* reuse = pool.acquire_sized<8192>();
    t_check("sized 大块同档复用", reuse == big);
    pool.hard_delete_sized<8192>(reuse);
}

void test_aligned()
{
    t_section(6, "对齐分配与统一释放");
    list_memory_pool pool;

    t_check("acquire_aligned(0, 16) 返回空", pool.acquire_aligned(0, 16) == nullptr);
    t_check("acquire_aligned(64, 0) 返回空", pool.acquire_aligned(64, 0) == nullptr);
    t_check("非 2 的幂对齐返回空", pool.acquire_aligned(64, 24) == nullptr);
    t_check("对齐求和溢出返回空", pool.acquire_aligned(SIZE_MAX, 16) == nullptr);
    t_check("对齐值溢出返回空", pool.acquire_aligned(64, SIZE_MAX) == nullptr);

    void* p64 = pool.acquire_aligned(100, 64);
    void* p4096 = pool.acquire_aligned(100, 4096);
    t_check("64 字节对齐分配非空", p64 != nullptr);
    t_check("4096 字节对齐分配非空", p4096 != nullptr);
    t_check("64 字节对齐成立",
            p64 && reinterpret_cast<uintptr_t>(p64) % 64 == 0);
    t_check("4096 字节对齐成立",
            p4096 && reinterpret_cast<uintptr_t>(p4096) % 4096 == 0);
    t_check("对齐分配被 owns 识别", pool.owns(p64) && pool.owns(p4096));
    t_check_eq("对齐分配 size_of 为请求值", pool.size_of(p64), static_cast<size_t>(100));

    pool.soft_delete(p64);
    pool.hard_delete(p4096);
    t_check("对齐分配统一释放后 owns 为假", !pool.owns(p64) && !pool.owns(p4096));

    align64_obj* obj = pool.create<align64_obj>();
    t_check("create 过对齐类型非空", obj != nullptr);
    t_check("create 满足 alignof(T)",
            obj && reinterpret_cast<uintptr_t>(obj) % alignof(align64_obj) == 0);
    pool.dispose(obj);
    pool.soft_delete(obj);
}

void test_create_dispose()
{
    t_section(7, "对象创建与析构分离");
    list_memory_pool pool;

    counted_obj::live_count = 0;
    counted_obj::dtor_count = 0;

    counted_obj* obj = pool.create<counted_obj>(9);
    t_check("create 非空", obj != nullptr);
    t_check_eq("构造函数已执行", obj ? obj->value : 0, 9);

    const memory_stats before = pool.stats();
    pool.dispose(obj);
    const memory_stats after = pool.stats();
    t_check_eq("dispose 只析构", counted_obj::dtor_count, 1);
    t_check_eq("dispose 不改变存活字节", after.live_bytes, before.live_bytes);
    t_check_eq("dispose 不改变删除计数", after.release_count, before.release_count);
    t_check("dispose 后内存仍存活", pool.owns(obj));

    pool.soft_delete(obj);
    t_check("对象内存软删除后 invalid", !pool.owns(obj));
}

void test_ownership()
{
    t_section(8, "精确归属与范围查询");
    list_memory_pool pool;

    int local = 0;
    void* small = pool.acquire(128);
    unsigned char* interior = static_cast<unsigned char*>(small) + 8;

    t_check("小块起始地址 owns", pool.owns(small));
    t_check("slot 内部指针 owns 为假", !pool.owns(interior));
    t_check("slot 内部指针 contains 为真", pool.contains(interior));
    t_check("非本池指针 owns 为假", !pool.owns(&local));
    t_check("非本池指针 contains 为假", !pool.contains(&local));

    pool.soft_delete(small);
    t_check("软删除后 contains 仍为真", pool.contains(small));
    t_check("软删除后 owns 为假", !pool.owns(small));

    void* big = pool.acquire(8192);
    t_check("大块起始地址 owns", pool.owns(big));
    t_check("大块内部指针 owns 为假",
            !pool.owns(static_cast<unsigned char*>(big) + 16));
    t_check("大块内部指针 contains 为真",
            pool.contains(static_cast<unsigned char*>(big) + 16));
    pool.hard_delete(big);
    t_check("大块硬删除后 owns 为假", !pool.owns(big));

    void* independent = pool.acquire(600000);
    t_check("独立大块分配非空", independent != nullptr);
    pool.hard_delete(independent);
    t_check("独立大块硬删除后 contains 为假", !pool.contains(independent));

    void* aligned = pool.acquire_aligned(64, 256);
    t_check("对齐分配 owns 为真", pool.owns(aligned));
    t_check("对齐分配内部偏移 owns 为假",
            !pool.owns(static_cast<unsigned char*>(aligned) + 8));
    pool.soft_delete(aligned);
}

void test_capacity()
{
    t_section(9, "容量增减与存活块稳定");
    list_memory_pool pool;

    const size_t reserved_before = pool.stats().reserved_bytes;
    pool.increase_capacity(0);
    t_check("increase_capacity(0) 无操作", pool.stats().reserved_bytes == reserved_before);

    pool.increase_capacity(2 * 1024 * 1024);
    const size_t reserved_grown = pool.stats().reserved_bytes;
    t_check("increase_capacity 扩到目标以上", reserved_grown >= 2 * 1024 * 1024);
    pool.increase_capacity(1);
    t_check("increase_capacity 不缩容", pool.stats().reserved_bytes == reserved_grown);
    pool.reduce_capacity(0);
    t_check("无存活时 reduce_capacity(0) 归还 wilderness",
            pool.stats().reserved_bytes == 0);

    void* live = pool.acquire(600000);
    t_check("独立大块分配非空", live != nullptr);
    t_check_eq("独立大块 size_of", pool.size_of(live), static_cast<size_t>(600000));
    const size_t reserved_live = pool.stats().reserved_bytes;
    pool.reduce_capacity(0);
    t_check("reduce_capacity 不释放存活块", pool.owns(live));
    t_check("reduce_capacity 后 live 不变", pool.stats().live_bytes == 600000);
    t_check("reduce_capacity 目标低于存活下限时保留容量",
            pool.stats().reserved_bytes >= 600000);
    t_check("reduce_capacity 不扩大容量",
            pool.stats().reserved_bytes <= reserved_live);

    pool.hard_delete(live);
    t_check("独立大块硬删除归还系统",
            pool.stats().reserved_bytes + 600000 <= reserved_live);
}

void test_reduce_soft_big()
{
    t_section(10, "软删除大块容量回收");
    list_memory_pool pool;

    void* p = pool.acquire(600000);
    t_check("软删除测试大块非空", p != nullptr);
    const size_t reserved_live = pool.stats().reserved_bytes;
    pool.soft_delete(p);
    t_check_eq("软删除后 live 归零", pool.stats().live_bytes, static_cast<size_t>(0));
    t_check("软删除大块计入可复用", pool.stats().reusable_bytes >= 600000);

    pool.reduce_capacity(0);
    t_check("reduce_capacity 回收软删除独立大块",
            pool.stats().reserved_bytes < reserved_live);
    t_check("回收后不可复用", pool.stats().reusable_bytes == 0);
}

void test_resize()
{
    t_section(11, "重分配");
    list_memory_pool pool;

    unsigned char* small = static_cast<unsigned char*>(pool.acquire(64));
    void* guard = pool.acquire(16);
    t_check("小块重分配初始非空", small != nullptr && guard != nullptr);
    for (size_t i = 0; i < 64; ++i)
    {
        small[i] = static_cast<unsigned char>(i & 0xFF);
    }

    t_check("小块缩容可原位", pool.resize_inplace(small, 64, 32));
    t_check("小块跨档不可原位", !pool.resize_inplace(small, 64, 128));
    unsigned char* grown = static_cast<unsigned char*>(pool.resize(small, 64, 128));
    t_check("小块跨档重分配非空", grown != nullptr);
    bool copied = true;
    for (size_t i = 0; i < 64; ++i)
    {
        if (grown[i] != static_cast<unsigned char>(i & 0xFF)) { copied = false; }
    }
    t_check("重分配保留内容", copied);
    t_check("重分配旧块已释放", !pool.owns(small));
    pool.soft_delete(guard);
    pool.hard_delete(grown);

    unsigned char* big = static_cast<unsigned char*>(pool.acquire(600000));
    t_check("大块重分配初始非空", big != nullptr);
    for (size_t i = 0; i < 64; ++i)
    {
        big[i] = static_cast<unsigned char>(i);
    }
    t_check("大块缩容可原位", pool.resize_inplace(big, 600000, 500000));
    t_check("原位缩容后可用大小仍满足请求",
            pool.size_of(big) >= 500000);
    t_check("大块扩容回原容量可原位", pool.resize_inplace(big, 500000, 600000));
    t_check("超过容量不可原位", !pool.resize_inplace(big, 600000, 700000));
    unsigned char* big2 = static_cast<unsigned char*>(pool.resize(big, 600000, 700000));
    t_check("大块移动重分配非空", big2 != nullptr);
    t_check("大块移动重分配内容一致", big2[0] == 0 && big2[63] == 63);
    t_check("大块移动后旧块已释放", !pool.owns(big));
    pool.hard_delete(big2);

    void* null_ptr = pool.resize(nullptr, 0, 128);
    t_check("resize(nullptr) 等价 acquire", null_ptr != nullptr);
    t_check("resize 目标 0 释放并返回空", pool.resize(null_ptr, 128, 0) == nullptr);

    void* aligned = pool.acquire_aligned(100, 4096);
    void* aligned_grown = pool.resize(aligned, 100, 200);
    t_check("过对齐重分配非空", aligned_grown != nullptr);
    t_check("过对齐重分配保持对齐",
            aligned_grown && reinterpret_cast<uintptr_t>(aligned_grown) % 4096 == 0);
    t_check("过对齐重分配释放旧块", !pool.owns(aligned));
    pool.soft_delete(aligned_grown);
}

void test_stats_and_reset()
{
    t_section(12, "统计不变量与重置");
    list_memory_pool pool;

    void* a = pool.acquire(64);
    void* b = pool.acquire(8192);
    void* c = pool.acquire_aligned(128, 64);
    t_check("统计测试分配非空", a && b && c);

    memory_stats s = pool.stats();
    t_check("reserved >= live", s.reserved_bytes >= s.live_bytes);
    t_check("reusable <= reserved - live",
            s.reusable_bytes <= s.reserved_bytes - s.live_bytes);

    pool.soft_delete(a);
    pool.hard_delete(b);
    pool.soft_delete(c);
    s = pool.stats();
    t_check("删除计数等于软 + 硬",
            s.release_count == s.soft_delete_count + s.hard_delete_count);
    t_check("删除后 live 归零", s.live_bytes == 0);
    t_check("peak >= live", s.peak_live_bytes >= s.live_bytes);

    pool.reset();
    s = pool.stats();
    t_check("reset 后 live 归零", s.live_bytes == 0);
    t_check("reset 保留保留容量", s.reserved_bytes > 0);
    void* after_reset = pool.acquire(8192);
    t_check("reset 后池仍可分配", after_reset != nullptr);
    pool.hard_delete(after_reset);

    pool.reset_stats();
    s = pool.stats();
    t_check("reset_stats 清零计数", s.acquire_count == 0 && s.release_count == 0);
    t_check_eq("reset_stats 峰值回到当前 live", s.peak_live_bytes, s.live_bytes);

    pool.release_all();
    s = pool.stats();
    t_check("release_all 后 reserved 归零", s.reserved_bytes == 0);
    t_check("release_all 后 live 归零", s.live_bytes == 0);
    t_check("release_all 后 reusable 归零", s.reusable_bytes == 0);
    void* after = pool.acquire(128);
    t_check("release_all 后仍可继续分配", after != nullptr);
    pool.soft_delete(after);
}

void test_sized_validation()
{
    t_section(13, "sized 删除单次验证");
    list_memory_pool pool;

    void* aligned = pool.acquire_aligned(100, 64);
    t_check("对齐 sized 测试分配非空", aligned != nullptr);
    const memory_stats before = pool.stats();
    pool.soft_delete_sized<101>(aligned);
    const memory_stats ignored = pool.stats();
    t_check("对齐 sized 过大被忽略", pool.owns(aligned));
    t_check("对齐 sized 忽略不改存活", ignored.live_bytes == before.live_bytes);
    t_check("对齐 sized 忽略不计数", ignored.release_count == before.release_count);
    pool.hard_delete_sized<100>(aligned);
    t_check("对齐 sized 正确硬删除生效", !pool.owns(aligned));

    void* slot = pool.acquire_sized<128>();
    t_check("slot sized 测试分配非空", slot != nullptr);
    pool.hard_delete_sized<129>(slot);
    t_check("slot sized 过大被忽略", pool.owns(slot));
    pool.soft_delete_sized<128>(slot);
    t_check("slot sized 正确软删除生效", !pool.owns(slot));

    void* big = pool.acquire_sized<8192>();
    t_check("大块 sized 测试分配非空", big != nullptr);
    pool.soft_delete_sized<8193>(big);
    t_check("大块 sized 过大被忽略", pool.owns(big));
    pool.soft_delete_sized<8192>(big);
    t_check("大块 sized 正确软删除生效", !pool.owns(big));
    void* reused = pool.acquire_sized<8192>();
    t_check("大块 sized 释放后可复用", reused == big);
    pool.hard_delete_sized<8192>(reused);
}

void test_free_iteration()
{
    t_section(14, "可复用块遍历");
    list_memory_pool pool;

    void* a = pool.acquire(64);
    void* b = pool.acquire(8192);
    pool.soft_delete(a);
    pool.soft_delete(b);

    size_t free_blocks = 0;
    size_t free_bytes = 0;
    pool.iterate_free([&](void* ptr, size_t size)
    {
        if (ptr) { ++free_blocks; }
        free_bytes += size;
    });
    t_check("iterate_free 报告 slot 与软删除大块", free_blocks >= 2);
    t_check("iterate_free 字节数覆盖软删除大块", free_bytes >= 8192);
}

} // namespace

int main()
{
    t_begin("test_module_list_memory_pool — 模块功能测试: list_memory_pool");

    test_lifecycle();
    test_zeroed_and_isolation();
    test_small_reuse();
    test_big_soft_cache();
    test_sized();
    test_aligned();
    test_create_dispose();
    test_ownership();
    test_capacity();
    test_reduce_soft_big();
    test_resize();
    test_stats_and_reset();
    test_sized_validation();
    test_free_iteration();

    return t_end("list_memory_pool ");
}
