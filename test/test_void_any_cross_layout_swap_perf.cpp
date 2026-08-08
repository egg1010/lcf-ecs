// test_void_any_cross_layout_swap_perf.cpp - 跨布局交换性能测试
// 对比: cross_layout_swap vs 同布局 swap vs 不同布局拷贝构造+赋值
#include "perf_common.hpp"
#include "include/part/void_any.hpp"
#include <string>

using namespace std;

// 测试组件
struct Small { int v; };                  // 4B
struct Medium { float a[8]; };            // 32B
struct Large { double a[32]; };           // 256B
struct NonTrivial {                       // 非 trivially copyable
    std::string s;
    NonTrivial() : s("hello") {}
    NonTrivial(const std::string& v) : s(v) {}
    NonTrivial(const NonTrivial& o) : s(o.s) {}
    NonTrivial(NonTrivial&& o) noexcept : s(std::move(o.s)) {}
    NonTrivial& operator=(const NonTrivial& o) { s = o.s; return *this; }
    NonTrivial& operator=(NonTrivial&& o) noexcept { s = std::move(o.s); return *this; }
};

// 全局 volatile sink, 阻止 DCE
static volatile size_t g_sink = 0;

// === Section 1: 同布局 swap (基线) ===
template <typename T>
static void test_same_layout_swap(size_t ops)
{
    print_header(("Section 1: same-layout swap (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;

    T v{};
    {
        double ns = best_ns(REPEAT, [&]() {
            define_void_any<56, 8> a(v);
            define_void_any<56, 8> b(v);
            for (size_t i = 0; i < ops; ++i)
            {
                a.swap(b);
                g_sink += a.has_value();
            }
            compiler_barrier();
            return g_sink;
        });
        print_ns("swap (same layout)", ops, ns / ops);
    }
    print_footer();
}

// === Section 2: 跨布局 swap (不同 SsoSize) ===
template <typename T>
static void test_cross_layout_swap(size_t ops)
{
    print_header(("Section 2: cross-layout swap (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;

    T v{};
    {
        double ns = best_ns(REPEAT, [&]() {
            define_void_any<56, 8> a(v);
            define_void_any<120, 8> b(v);
            for (size_t i = 0; i < ops; ++i)
            {
                a.cross_layout_swap(b);
                g_sink += a.has_value();
            }
            compiler_barrier();
            return g_sink;
        });
        print_ns("cross_layout_swap (56→120)", ops, ns / ops);
    }
    print_footer();
}

// === Section 3: 跨布局 swap (容量互不满足) ===
template <typename T>
static void test_cross_layout_swap_capacity_mismatch(size_t ops)
{
    print_header(("Section 3: cross-layout swap capacity-mismatch (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;

    T v{};
    // 24B SSO vs 248B SSO: 若 T > 24B, 小布局装不下, 必须转堆
    {
        double ns = best_ns(REPEAT, [&]() {
            define_void_any<24, 8> a(v);
            define_void_any<248, 8> b(v);
            for (size_t i = 0; i < ops; ++i)
            {
                a.cross_layout_swap(b);
                g_sink += a.has_value();
            }
            compiler_barrier();
            return g_sink;
        });
        print_ns("cross_layout_swap (24↔248)", ops, ns / ops);
    }
    print_footer();
}

// === Section 4: 跨布局 swap 与拷贝+赋值对比 ===
template <typename T>
static void test_cross_layout_vs_copy_assign(size_t ops)
{
    print_header(("Section 4: cross-layout swap vs copy+assign (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;

    T v{};
    // 方案 A: cross_layout_swap
    {
        double ns = best_ns(REPEAT, [&]() {
            define_void_any<56, 8> a(v);
            define_void_any<120, 8> b(v);
            for (size_t i = 0; i < ops; ++i)
            {
                a.cross_layout_swap(b);
                g_sink += a.has_value();
            }
            compiler_barrier();
            return g_sink;
        });
        print_ns("cross_layout_swap", ops, ns / ops);
    }

    // 方案 B: 通过 get + set 间接交换 (基线: 全拷贝)
    {
        double ns = best_ns(REPEAT, [&]() {
            define_void_any<56, 8> a(v);
            define_void_any<120, 8> b(v);
            for (size_t i = 0; i < ops; ++i)
            {
                // 拷贝出值, 互相赋值
                T tmp = a.get<T>();
                a.set(b.get<T>());
                b.set(tmp);
                g_sink += a.has_value();
            }
            compiler_barrier();
            return g_sink;
        });
        print_ns("get+set (full copy)", ops, ns / ops);
    }
    print_footer();
}

// === Section 5: 空值参与交换 ===
static void test_empty_swap(size_t ops)
{
    print_header("Section 5: empty-to-value swap");
    constexpr int REPEAT = 5;

    // 空值 → 有值
    {
        Small v{42};
        double ns = best_ns(REPEAT, [&]() {
            define_void_any<56, 8> a;
            define_void_any<120, 8> b(v);
            for (size_t i = 0; i < ops; ++i)
            {
                a.cross_layout_swap(b);
                // 下一轮: a 有值, b 空, 交换回来
                b.cross_layout_swap(a);
                g_sink += a.has_value();
            }
            compiler_barrier();
            return g_sink;
        });
        print_ns("empty↔value (56↔120)", ops * 2, ns / (ops * 2));
    }
    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  void_any 跨布局交换性能测试\n";
    cout << "  (sizeof void_any<56,8>=" << sizeof(define_void_any<56, 8>)
         << "B, void_any<120,8>=" << sizeof(define_void_any<120, 8>)
         << "B, void_any<248,8>=" << sizeof(define_void_any<248, 8>) << "B)\n";
    cout << "============================================================\n";

    const size_t OPS = 1 << 16;  // 64K

    cout << "\n=== Small (4B, SSO) ===\n";
    test_same_layout_swap<Small>(OPS);
    test_cross_layout_swap<Small>(OPS);
    test_cross_layout_swap_capacity_mismatch<Small>(OPS);
    test_cross_layout_vs_copy_assign<Small>(OPS);

    cout << "\n=== Medium (32B, SSO) ===\n";
    test_same_layout_swap<Medium>(OPS);
    test_cross_layout_swap<Medium>(OPS);
    test_cross_layout_swap_capacity_mismatch<Medium>(OPS);
    test_cross_layout_vs_copy_assign<Medium>(OPS);

    cout << "\n=== Large (256B, heap) ===\n";
    test_same_layout_swap<Large>(OPS);
    test_cross_layout_swap<Large>(OPS);
    test_cross_layout_swap_capacity_mismatch<Large>(OPS);
    test_cross_layout_vs_copy_assign<Large>(OPS);

    cout << "\n=== NonTrivial (std::string) ===\n";
    test_same_layout_swap<NonTrivial>(OPS);
    test_cross_layout_swap<NonTrivial>(OPS);
    test_cross_layout_swap_capacity_mismatch<NonTrivial>(OPS);
    test_cross_layout_vs_copy_assign<NonTrivial>(OPS);

    cout << "\n=== 空值参与 ===\n";
    test_empty_swap(OPS);

    cout << "\n============================================================\n";
    cout << "  性能测试完成\n";
    cout << "============================================================\n";
    return 0;
}
