// test_module_memory_allocators.cpp - 模块功能测试: arena / layered / stl_allocator
#include "test_common.hpp"
#include "include/part/memory/arena_allocator.hpp"
#include "include/part/memory/layered_allocator.hpp"
#include "include/part/memory/memory_pool.hpp"
#include "include/part/memory/stl_allocator.hpp"

#include <cstdint>
#include <string>

namespace {

struct alignas(64) over_aligned
{
    int value;

    explicit over_aligned(int v = 0) noexcept : value(v) {}
};

template <typename Pool>
void check_stl_alignment(Pool& pool)
{
    memory::stl_allocator<over_aligned, Pool> alloc(pool);
    for (size_t i = 0; i < 8; ++i)
    {
        over_aligned* p = alloc.allocate(1);
        t_check("STL 过对齐分配满足 alignof(T)",
                reinterpret_cast<uintptr_t>(p) % alignof(over_aligned) == 0);
        alloc.construct(p, static_cast<int>(i));
        t_check("STL 过对齐对象可构造和访问", p->value == static_cast<int>(i));
        alloc.destroy(p);
        alloc.deallocate(p, 1);
    }
}

void test_stl_alignment()
{
    t_section(1, "STL 过对齐分配");
    memory::memory_pool pool;
    check_stl_alignment(pool);

    memory::layered_allocator layered;
    check_stl_alignment(layered);
}

void test_aligned_resize()
{
    t_section(2, "layered 过对齐重分配");
    memory::layered_allocator pool;

    void* slab_ptr = pool.acquire_aligned(48, 64);
    void* slab_grown = pool.resize(slab_ptr, 48, 96);
    t_check("slab 过对齐移动重分配非空", slab_grown != nullptr);
    t_check("slab 过对齐移动重分配保持对齐",
            slab_grown && reinterpret_cast<uintptr_t>(slab_grown) % 64 == 0);
    t_check("slab 过对齐移动重分配释放旧块", !pool.owns(slab_ptr));
    pool.soft_delete(slab_grown);

    void* big_ptr = pool.acquire_aligned(100, 4096);
    void* big_grown = pool.resize(big_ptr, 100, 200);
    t_check("大块过对齐重分配非空", big_grown != nullptr);
    t_check("大块过对齐重分配保持对齐",
            big_grown && reinterpret_cast<uintptr_t>(big_grown) % 4096 == 0);
    t_check("大块过对齐重分配释放旧块", !pool.owns(big_ptr));
    pool.soft_delete(big_grown);
}

void test_array_arguments()
{
    t_section(3, "数组构造复用右值参数");
    memory::arena_allocator arena(1024);
    auto* arena_arr = arena.create_array<std::string>(3, std::string("payload"));
    t_check("arena 数组非空且元素一致",
            arena_arr && arena_arr[0] == "payload" &&
            arena_arr[1] == "payload" && arena_arr[2] == "payload");
    arena.dispose_array(arena_arr, 3);

    memory::layered_allocator layered;
    auto* layered_arr = layered.create_array<std::string>(3, std::string("payload"));
    t_check("layered 数组非空且元素一致",
            layered_arr && layered_arr[0] == "payload" &&
            layered_arr[1] == "payload" && layered_arr[2] == "payload");
    layered.dispose_array(layered_arr, 3);
    layered.soft_delete(layered_arr);
}

void test_layered_peak()
{
    t_section(4, "layered 总存活峰值");
    memory::layered_allocator pool;

    void* p16 = pool.acquire(16);
    pool.soft_delete(p16);
    void* p32 = pool.acquire(32);
    t_check_eq("错峰子池峰值不相加", pool.stats().peak_live_bytes,
               static_cast<size_t>(32));

    void* another16 = pool.acquire(16);
    t_check_eq("并发存活更新总峰值", pool.stats().peak_live_bytes,
               static_cast<size_t>(48));
    pool.reset_stats();
    t_check_eq("统计重置使用当前总存活", pool.stats().peak_live_bytes,
               static_cast<size_t>(48));

    pool.soft_delete(another16);
    pool.soft_delete(p32);
}

} // namespace

int main()
{
    t_begin("test_module_memory_allocators — 模块功能测试: arena / layered / stl_allocator");

    test_stl_alignment();
    test_aligned_resize();
    test_array_arguments();
    test_layered_peak();

    return t_end("memory_allocators ");
}
