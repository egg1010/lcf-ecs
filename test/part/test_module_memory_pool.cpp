// test_module_memory_pool.cpp - 模块功能测试: memory_pool
#include "test_common.hpp"
#include "include/part/memory/memory_pool.hpp"

#include <cstdint>
#include <cstring>

using memory::memory_pool;
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
    memory_pool pool;

    t_check("acquire(0) 返回空", pool.acquire(0) == nullptr);
    t_check("acquire(SIZE_MAX) 返回空", pool.acquire(SIZE_MAX) == nullptr);
    t_check("acquire(SIZE_MAX - 8) 返回空", pool.acquire(SIZE_MAX - 8) == nullptr);

    void* p = pool.acquire(64);
    t_check("acquire(64) 非空", p != nullptr);
    t_check("存活指针被 owns 识别", pool.owns(p));
    t_check("size_of >= 请求大小", pool.size_of(p) >= 64);
    t_check("empty 为假", !pool.empty());

    memory_stats s = pool.stats();
    t_check_eq("acquire_count == 1", s.acquire_count, static_cast<size_t>(1));
    t_check("live_bytes 已累计", s.live_bytes >= 64);
    t_check("release_count == 0", s.release_count == 0);

    pool.soft_delete(p);
    t_check("软删除后 owns 为假", !pool.owns(p));
    t_check_eq("软删除后 size_of == 0", pool.size_of(p), static_cast<size_t>(0));
    t_check("软删除后 empty 为真", pool.empty());

    s = pool.stats();
    t_check_eq("release_count == 1", s.release_count, static_cast<size_t>(1));
    t_check_eq("soft_delete_count == 1", s.soft_delete_count, static_cast<size_t>(1));
    t_check_eq("hard_delete_count == 0", s.hard_delete_count, static_cast<size_t>(0));
    t_check_eq("软删除后 live_bytes == 0", s.live_bytes, static_cast<size_t>(0));

    pool.soft_delete(p);
    pool.hard_delete(p);
    pool.soft_delete(nullptr);
    pool.hard_delete(nullptr);
    s = pool.stats();
    t_check("重复/交叉/空删除不计数", s.release_count == 1 && s.soft_delete_count == 1);

    void* q = pool.acquire(96);
    t_check("再次 acquire 非空", q != nullptr);
    pool.hard_delete(q);
    t_check("硬删除后 owns 为假", !pool.owns(q));
    t_check_eq("hard_delete_count == 1", pool.stats().hard_delete_count, static_cast<size_t>(1));
    t_check_eq("release_count == soft + hard",
               pool.stats().release_count,
               pool.stats().soft_delete_count + pool.stats().hard_delete_count);
}

void test_zeroed_and_isolation()
{
    t_section(2, "清零与分配隔离");
    memory_pool pool;

    unsigned char* z = static_cast<unsigned char*>(pool.acquire_zeroed(96));
    t_check("acquire_zeroed 非空", z != nullptr);
    bool all_zero = true;
    for (size_t i = 0; i < 96; ++i)
    {
        if (z[i] != 0) { all_zero = false; }
    }
    t_check("清零区域全 0", all_zero);
    pool.soft_delete(z);

    void* a = pool.acquire(32);
    void* b = pool.acquire(32);
    void* c = pool.acquire(32);
    t_check("三次分配均非空", a && b && c);
    t_check("地址互异", a != b && b != c && a != c);
    t_check("三块均被 owns 识别", pool.owns(a) && pool.owns(b) && pool.owns(c));
    pool.soft_delete(c);
    pool.soft_delete(b);
    pool.soft_delete(a);
}

void test_reuse()
{
    t_section(3, "软删除后复用");
    memory_pool pool;

    void* p = pool.acquire_sized<64>();
    t_check("acquire_sized<64> 非空", p != nullptr);
    pool.soft_delete_sized<64>(p);
    t_check("sized 软删除后 owns 为假", !pool.owns(p));

    void* q = pool.acquire_sized<64>();
    t_check("同档 sized 分配复用软删除块", q == p);
    pool.hard_delete_sized<64>(q);

    void* big = pool.acquire(1024);
    t_check("大块分配非空", big != nullptr);
    pool.soft_delete(big);
    void* big2 = pool.acquire(1024);
    t_check("大块软删除后可再次分配", big2 != nullptr);
    t_check("大块地址被复用", big2 == big);
    pool.hard_delete(big2);
}

void test_sized()
{
    t_section(4, "编译期大小接口");
    memory_pool pool;

    void* p = pool.acquire_sized<64>();
    t_check("acquire_sized<64> 非空", p != nullptr);

    const size_t release_before = pool.stats().release_count;
    pool.soft_delete_sized<1024>(p);
    t_check("过大的 sized 删除被忽略", pool.owns(p));
    t_check_eq("忽略时不计数", pool.stats().release_count, release_before);

    pool.hard_delete_sized<64>(p);
    t_check("正确 sized 硬删除生效", !pool.owns(p));
    t_check_eq("sized 删除计入 hard", pool.stats().hard_delete_count, static_cast<size_t>(1));

    void* zero = pool.acquire_sized<16>();
    t_check("复用 16B 档非空", zero != nullptr);
    pool.soft_delete_sized<16>(zero);
}

void test_aligned()
{
    t_section(5, "对齐分配与统一释放");
    memory_pool pool;

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
    t_section(6, "对象创建与析构分离");
    memory_pool pool;

    counted_obj::live_count = 0;
    counted_obj::dtor_count = 0;

    counted_obj* obj = pool.create<counted_obj>(7);
    t_check("create 非空", obj != nullptr);
    t_check_eq("构造函数已执行", obj ? obj->value : 0, 7);
    t_check_eq("存活对象计数为 1", counted_obj::live_count, 1);

    const memory_stats before = pool.stats();
    pool.dispose(obj);
    const memory_stats after = pool.stats();
    t_check_eq("dispose 只析构", counted_obj::dtor_count, 1);
    t_check_eq("dispose 不改变存活字节", after.live_bytes, before.live_bytes);
    t_check_eq("dispose 不改变分配计数", after.acquire_count, before.acquire_count);
    t_check_eq("dispose 不改变删除计数", after.release_count, before.release_count);
    t_check("dispose 后内存仍存活", pool.owns(obj));

    pool.soft_delete(obj);
    t_check("对象内存软删除后 invalid", !pool.owns(obj));
    pool.dispose(static_cast<counted_obj*>(nullptr));
    t_check("dispose(nullptr) 安全", counted_obj::dtor_count == 1);
}

void test_ownership()
{
    t_section(7, "精确归属与范围查询");
    memory_pool pool;

    int local = 0;
    void* p = pool.acquire(128);
    unsigned char* interior = static_cast<unsigned char*>(p) + 8;

    t_check("存活起始地址 owns", pool.owns(p));
    t_check("内部指针 owns 为假", !pool.owns(interior));
    t_check("内部指针 contains 为真", pool.contains(interior));
    t_check("本池指针 contains 为真", pool.contains(p));
    t_check("非本池指针 owns 为假", !pool.owns(&local));
    t_check("非本池指针 contains 为假", !pool.contains(&local));
    t_check_eq("内部指针 size_of == 0", pool.size_of(interior), static_cast<size_t>(0));

    pool.soft_delete(p);
    t_check("软删除后 contains 仍为真", pool.contains(p));
    t_check("软删除后 owns 仍为假", !pool.owns(p));

    void* aligned = pool.acquire_aligned(64, 64);
    t_check("对齐分配 owns 为真", pool.owns(aligned));
    t_check("对齐分配内部偏移 owns 为假",
            !pool.owns(static_cast<unsigned char*>(aligned) + 8));
    pool.soft_delete(aligned);
}

void test_capacity()
{
    t_section(8, "容量调整与存活块稳定");
    memory_pool pool(4096);

    void* live = pool.acquire(64);
    t_check("容量测试分配非空", live != nullptr);

    const size_t reserved_before = pool.stats().reserved_bytes;
    pool.increase_capacity(reserved_before + 65536);
    const size_t reserved_grown = pool.stats().reserved_bytes;
    t_check("increase_capacity 扩容", reserved_grown >= reserved_before + 65536);
    t_check("扩容后存活块地址不变", pool.owns(live));

    pool.increase_capacity(1);
    t_check("increase_capacity 不做缩容", pool.stats().reserved_bytes == reserved_grown);

    pool.reduce_capacity(0);
    t_check("reduce_capacity(0) 不释放存活块", pool.owns(live));
    t_check("reduce_capacity(0) 后 live 不变", pool.stats().live_bytes >= 64);
    t_check("reduce_capacity 降低保留容量", pool.stats().reserved_bytes < reserved_grown);

    pool.soft_delete(live);
}

void test_resize()
{
    t_section(9, "重分配");
    memory_pool pool;

    unsigned char* p = static_cast<unsigned char*>(pool.acquire(100));
    void* guard = pool.acquire(16);   // 阻止 resize 吞并后续空闲块, 强制走移动路径
    t_check("重分配初始块非空", p != nullptr && guard != nullptr);
    for (size_t i = 0; i < 100; ++i)
    {
        p[i] = static_cast<unsigned char>(i & 0xFF);
    }

    t_check("缩容可原位", pool.resize_inplace(p, 100, 50));
    t_check("原位缩容地址不变", pool.owns(p));

    const size_t acquire_before = pool.stats().acquire_count;
    unsigned char* grown = static_cast<unsigned char*>(pool.resize(p, 100, 512));
    t_check("扩容重分配非空", grown != nullptr);
    bool copied = true;
    for (size_t i = 0; i < 100; ++i)
    {
        if (grown[i] != static_cast<unsigned char>(i & 0xFF)) { copied = false; }
    }
    t_check("移动重分配保留内容", copied);
    t_check("移动重分配计入 acquire", pool.stats().acquire_count == acquire_before + 1);
    t_check("移动重分配旧块已释放", !pool.owns(p));
    pool.soft_delete(guard);

    void* null_ptr = pool.resize(nullptr, 0, 64);
    t_check("resize(nullptr) 等价 acquire", null_ptr != nullptr);
    void* null_new = pool.resize(null_ptr, 64, 0);
    t_check("resize 目标 0 释放并返回空", null_new == nullptr && !pool.owns(null_ptr));

    void* aligned = pool.acquire_aligned(100, 4096);
    void* aligned_grown = pool.resize(aligned, 100, 200);
    t_check("过对齐重分配非空", aligned_grown != nullptr);
    t_check("过对齐重分配保持对齐",
            aligned_grown && reinterpret_cast<uintptr_t>(aligned_grown) % 4096 == 0);
    t_check("过对齐重分配释放旧块", !pool.owns(aligned));
    pool.soft_delete(aligned_grown);

    pool.hard_delete(grown);
}

void test_stats_and_reset()
{
    t_section(10, "统计不变量与重置");
    memory_pool pool;

    void* a = pool.acquire(64);
    void* b = pool.acquire(4096);
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
    t_check("peak >= 当前 live", s.peak_live_bytes >= s.live_bytes);

    pool.reset_stats();
    s = pool.stats();
    t_check("reset_stats 清零计数", s.acquire_count == 0 && s.release_count == 0);
    t_check_eq("reset_stats 峰值回到当前 live", s.peak_live_bytes, s.live_bytes);

    pool.reset();
    s = pool.stats();
    t_check("reset 后 live 归零", s.live_bytes == 0);
    t_check("reset 保留保留容量", s.reserved_bytes > 0);
    void* after_reset = pool.acquire(32);
    t_check("reset 后池仍可分配", after_reset != nullptr);
    pool.soft_delete(after_reset);

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
    t_section(11, "sized 删除单次验证");
    memory_pool pool;

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

    void* small = pool.acquire_sized<64>();
    t_check("小块 sized 测试分配非空", small != nullptr);
    pool.hard_delete_sized<65>(small);
    t_check("小块 sized 过大被忽略", pool.owns(small));
    pool.soft_delete_sized<64>(small);
    t_check("小块 sized 正确软删除生效", !pool.owns(small));

    void* big = pool.acquire(1024);
    t_check("大块 sized 测试分配非空", big != nullptr);
    const size_t available = pool.size_of(big);
    pool.hard_delete_sized<(1024 + 1)>(big);
    t_check("大块 sized 过大被忽略", pool.owns(big));
    pool.hard_delete(big);
    t_check("大块清理生效", !pool.owns(big));
    t_check("大块可用大小覆盖请求", available >= 1024);
}

void test_batch_cache()
{
    t_section(12, "批量软删除缓存回收");
    constexpr size_t count = 3000;
    static void* ptrs[count];
    memory_pool pool;

    for (size_t i = 0; i < count; ++i)
    {
        ptrs[i] = pool.acquire(32);
    }
    t_check("批量分配全部非空", ptrs[0] != nullptr && ptrs[count - 1] != nullptr);
    for (size_t i = 0; i < count; ++i)
    {
        pool.soft_delete(ptrs[i]);
    }
    t_check("批量软删除后全部失效", !pool.owns(ptrs[0]) && !pool.owns(ptrs[count - 1]));
    t_check("批量回收保留大部分缓存", pool.free_block_count() >= 1000);
    t_check("批量回收保持可复用字节", pool.stats().reusable_bytes >= count * 32);

    for (size_t i = 0; i < count; ++i)
    {
        ptrs[i] = pool.acquire(32);
    }
    t_check("批量回收后可再次分配", ptrs[0] != nullptr && ptrs[count - 1] != nullptr);
    pool.release_all();
    t_check("批量测试整体释放", pool.stats().reserved_bytes == 0);
}

void test_free_iteration()
{
    t_section(13, "空闲遍历与碎片");
    memory_pool pool;

    void* a = pool.acquire(256);
    void* b = pool.acquire(256);
    pool.soft_delete(a);
    pool.soft_delete(b);

    size_t free_blocks = 0;
    size_t free_bytes = 0;
    pool.iterate_free([&](void* ptr, size_t size)
    {
        if (ptr) { ++free_blocks; }
        free_bytes += size;
    });
    t_check("iterate_free 报告空闲块", free_blocks > 0);
    t_check("iterate_free 字节数大于 0", free_bytes > 0);
    t_check_eq("free_block_count 与遍历一致", free_blocks, pool.free_block_count());
}

} // namespace

void test_error_object()
{
    t_section(14, "错误对象交付");
    memory_pool pool;

    operating_message error;
    void* p = pool.acquire(64, error);
    t_check("成功分配不改错误对象", p != nullptr && static_cast<bool>(error));
    t_check("成功分配错误码为 0", error.code() == 0);

    void* bad = pool.acquire(0, error);
    t_check("零字节分配返回空", bad == nullptr);
    t_check("分配失败写 out_of_memory", error.is_code(om_err_out_of_memory));
    t_check("分配失败级别为 error", error.level() == msg_level::error);
    t_check("分配失败码非零", error.code() != 0);

    t_check("合法删除返回真", pool.soft_delete(p));
    t_check("重复删除返回假", !pool.soft_delete(p));

    error.reset();
    t_check("空指针删除返回假", !pool.soft_delete(nullptr, error));
    t_check("空指针删除写 null_pointer", error.is_code(om_err_null_pointer));

    error.reset();
    t_check("重复删除带错误对象返回假", !pool.soft_delete(p, error));
    t_check("重复删除写 not_found", error.is_code(om_err_not_found));

    void* q = pool.acquire(32);
    error.reset();
    t_check("硬删除返回真", pool.hard_delete(q, error));
    t_check("硬删除成功不写错误", static_cast<bool>(error));
}

int main()
{
    t_begin("test_module_memory_pool — 模块功能测试: memory_pool");

    test_lifecycle();
    test_zeroed_and_isolation();
    test_reuse();
    test_sized();
    test_aligned();
    test_create_dispose();
    test_ownership();
    test_capacity();
    test_resize();
    test_stats_and_reset();
    test_sized_validation();
    test_batch_cache();
    test_free_iteration();
    test_error_object();

    return t_end("memory_pool ");
}
