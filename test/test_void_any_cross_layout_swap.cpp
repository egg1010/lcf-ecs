// test_void_any_cross_layout_swap.cpp - 跨布局交换功能测试
// 覆盖: trivially copyable SSO 互换, 非 trivially copyable SSO 互换,
//       heap <-> SSO 互换, 空值参与, 容量互不满足退化, 同布局 swap
#include "test_common.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <utility>

using namespace std;

// 测试用 trivially copyable 类型
struct small_trivial
{
    int a;
    int b;
};

struct medium_trivial
{
    int a[8];
};

// 非 trivially copyable 类型
struct non_trivial
{
    string s;
    int v;
    non_trivial() : s("default"), v(0) {}
    non_trivial(const string& str, int x) : s(str), v(x) {}
    non_trivial(const non_trivial& o) : s(o.s), v(o.v) {}
    non_trivial(non_trivial&& o) noexcept : s(std::move(o.s)), v(o.v) {}
    non_trivial& operator=(const non_trivial& o)
    {
        s = o.s;
        v = o.v;
        return *this;
    }
    non_trivial& operator=(non_trivial&& o) noexcept
    {
        s = std::move(o.s);
        v = o.v;
        return *this;
    }
    bool operator==(const non_trivial& o) const { return s == o.s && v == o.v; }
};

// 大对象 (超过默认 void_any SSO 容量 56B)
struct large_trivial
{
    double a[32]; // 256B
};

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT(cond, msg) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

static void test_trivial_sso_to_sso()
{
    printf("=== test_trivial_sso_to_sso ===\n");
    using va_small_t = define_void_any<56, 8>;
    using va_large_t = define_void_any<120, 8>;

    va_small_t a(small_trivial{10, 20});
    va_large_t b(small_trivial{30, 40});

    a.cross_layout_swap(b);

    small_trivial* pa = a.get_ptr<small_trivial>();
    small_trivial* pb = b.get_ptr<small_trivial>();
    EXPECT(pa != nullptr, "a 类型仍为 small_trivial");
    EXPECT(pb != nullptr, "b 类型仍为 small_trivial");
    EXPECT(pa->a == 30 && pa->b == 40, "a 接收 b 的值");
    EXPECT(pb->a == 10 && pb->b == 20, "b 接收 a 的值");
    printf("\n");
}

static void test_medium_trivial_sso_to_sso()
{
    printf("=== test_medium_trivial_sso_to_sso ===\n");
    using va_small_t = define_void_any<56, 8>;
    using va_large_t = define_void_any<120, 8>;

    medium_trivial m1{{1, 2, 3, 4, 5, 6, 7, 8}};
    medium_trivial m2{{10, 20, 30, 40, 50, 60, 70, 80}};

    va_small_t a(m1);
    va_large_t b(m2);

    a.cross_layout_swap(b);

    medium_trivial* pa = a.get_ptr<medium_trivial>();
    medium_trivial* pb = b.get_ptr<medium_trivial>();
    EXPECT(pa != nullptr, "a 仍为 medium_trivial");
    EXPECT(pb != nullptr, "b 仍为 medium_trivial");
    EXPECT(pa->a[0] == 10, "a 接收 b 值");
    EXPECT(pb->a[0] == 1, "b 接收 a 值");
    printf("\n");
}

static void test_non_trivial_sso_to_sso()
{
    printf("=== test_non_trivial_sso_to_sso ===\n");
    using va_small_t = define_void_any<56, 8>;
    using va_large_t = define_void_any<120, 8>;

    non_trivial t1("hello", 100);
    non_trivial t2("world", 200);

    va_small_t a(t1);
    va_large_t b(t2);

    a.cross_layout_swap(b);

    non_trivial* pa = a.get_ptr<non_trivial>();
    non_trivial* pb = b.get_ptr<non_trivial>();
    EXPECT(pa != nullptr, "a 仍为 non_trivial");
    EXPECT(pb != nullptr, "b 仍为 non_trivial");
    EXPECT(pa->s == "world" && pa->v == 200, "a 接收 b 值");
    EXPECT(pb->s == "hello" && pb->v == 100, "b 接收 a 值");
    printf("\n");
}

static void test_heap_to_sso()
{
    printf("=== test_heap_to_sso ===\n");
    using va_small_t = define_void_any<56, 8>;
    using va_large_t = define_void_any<248, 8>;

    large_trivial l1{};
    large_trivial l2{};
    for (int i = 0; i < 32; ++i)
    {
        l1.a[i] = i * 1.0;
        l2.a[i] = i * 10.0;
    }

    va_small_t a(l1);
    va_large_t b(l2);

    EXPECT(a.get_ptr<large_trivial>() != nullptr, "a 初始持有 large");
    EXPECT(b.get_ptr<large_trivial>() != nullptr, "b 初始持有 large");

    a.cross_layout_swap(b);

    large_trivial* pa = a.get_ptr<large_trivial>();
    large_trivial* pb = b.get_ptr<large_trivial>();
    EXPECT(pa != nullptr, "a 交换后仍持有 large");
    EXPECT(pb != nullptr, "b 交换后仍持有 large");
    EXPECT(pa->a[0] == 0.0 && pa->a[1] == 10.0, "a 接收 b 值");
    EXPECT(pb->a[0] == 0.0 && pb->a[1] == 1.0, "b 接收 a 值");
    printf("\n");
}

static void test_empty_to_value()
{
    printf("=== test_empty_to_value ===\n");
    using va_small_t = define_void_any<56, 8>;
    using va_large_t = define_void_any<120, 8>;

    va_small_t a;
    va_large_t b(small_trivial{42, 84});

    EXPECT(!a.has_value(), "a 初始为空");
    EXPECT(b.has_value(), "b 初始有值");

    a.cross_layout_swap(b);

    EXPECT(a.has_value(), "a 接收 b 的值");
    EXPECT(!b.has_value(), "b 变空");

    small_trivial* pa = a.get_ptr<small_trivial>();
    EXPECT(pa != nullptr, "a 持有 small_trivial");
    EXPECT(pa->a == 42 && pa->b == 84, "a 值正确");
    printf("\n");
}

static void test_value_to_empty()
{
    printf("=== test_value_to_empty ===\n");
    using va_small_t = define_void_any<56, 8>;
    using va_large_t = define_void_any<120, 8>;

    va_small_t a(small_trivial{7, 14});
    va_large_t b;

    EXPECT(a.has_value(), "a 初始有值");
    EXPECT(!b.has_value(), "b 初始为空");

    a.cross_layout_swap(b);

    EXPECT(!a.has_value(), "a 变空");
    EXPECT(b.has_value(), "b 接收 a 的值");

    small_trivial* pb = b.get_ptr<small_trivial>();
    EXPECT(pb != nullptr, "b 持有 small_trivial");
    EXPECT(pb->a == 7 && pb->b == 14, "b 值正确");
    printf("\n");
}

static void test_both_empty()
{
    printf("=== test_both_empty ===\n");
    using va_small_t = define_void_any<56, 8>;
    using va_large_t = define_void_any<120, 8>;

    va_small_t a;
    va_large_t b;

    a.cross_layout_swap(b);

    EXPECT(!a.has_value(), "a 仍为空");
    EXPECT(!b.has_value(), "b 仍为空");
    printf("\n");
}

static void test_capacity_mismatch_fallback()
{
    printf("=== test_capacity_mismatch_fallback ===\n");
    // 双方都 SSO, 但容量互不满足
    // 大布局存小对象, 小布局存大对象 (小布局装不下大对象的值)
    using va_small_t = define_void_any<24, 8>;  // 24B SSO
    using va_large_t = define_void_any<248, 8>; // 248B SSO

    va_small_t a(small_trivial{1, 2});
    va_large_t b(large_trivial{});

    a.cross_layout_swap(b);

    EXPECT(a.has_value(), "a 交换后有值");
    EXPECT(b.has_value(), "b 交换后有值");

    large_trivial* pa = a.get_ptr<large_trivial>();
    small_trivial* pb = b.get_ptr<small_trivial>();
    EXPECT(pa != nullptr, "a 持有 large_trivial (转堆)");
    EXPECT(pb != nullptr, "b 持有 small_trivial");
    printf("\n");
}

static void test_same_layout_swap()
{
    printf("=== test_same_layout_swap ===\n");
    using va_t = define_void_any<56, 8>;

    va_t a(small_trivial{11, 22});
    va_t b(small_trivial{33, 44});

    a.swap(b);

    small_trivial* pa = a.get_ptr<small_trivial>();
    small_trivial* pb = b.get_ptr<small_trivial>();
    EXPECT(pa->a == 33 && pa->b == 44, "a 接收 b 值");
    EXPECT(pb->a == 11 && pb->b == 22, "b 接收 a 值");
    printf("\n");
}

static void test_same_layout_swap_non_trivial()
{
    printf("=== test_same_layout_swap_non_trivial ===\n");
    using va_t = define_void_any<56, 8>;

    non_trivial t1("alpha", 1);
    non_trivial t2("beta", 2);
    va_t a(t1);
    va_t b(t2);

    a.swap(b);

    non_trivial* pa = a.get_ptr<non_trivial>();
    non_trivial* pb = b.get_ptr<non_trivial>();
    EXPECT(pa->s == "beta" && pa->v == 2, "a 接收 b 值");
    EXPECT(pb->s == "alpha" && pb->v == 1, "b 接收 a 值");
    printf("\n");
}

static void test_repeated_swap_stability()
{
    printf("=== test_repeated_swap_stability ===\n");
    using va_small_t = define_void_any<56, 8>;
    using va_large_t = define_void_any<120, 8>;

    non_trivial t1("A", 1);
    non_trivial t2("B", 2);
    va_small_t a(t1);
    va_large_t b(t2);

    // 多次交换, 验证稳定性
    for (int i = 0; i < 100; ++i)
    {
        a.cross_layout_swap(b);
    }

    // 偶数次后应回到原状态
    non_trivial* pa = a.get_ptr<non_trivial>();
    non_trivial* pb = b.get_ptr<non_trivial>();
    EXPECT(pa != nullptr && pb != nullptr, "双方仍持有 non_trivial");
    EXPECT(pa->s == "A" && pa->v == 1, "a 回到初始值");
    EXPECT(pb->s == "B" && pb->v == 2, "b 回到初始值");
    printf("\n");
}

static void test_mixed_types_swap()
{
    printf("=== test_mixed_types_swap ===\n");
    using va_small_t = define_void_any<56, 8>;
    using va_large_t = define_void_any<120, 8>;

    va_small_t a(small_trivial{1, 2});
    va_large_t b(non_trivial("mixed", 99));

    a.cross_layout_swap(b);

    non_trivial* pa = a.get_ptr<non_trivial>();
    small_trivial* pb = b.get_ptr<small_trivial>();
    EXPECT(pa != nullptr, "a 接收 non_trivial");
    EXPECT(pb != nullptr, "b 接收 small_trivial");
    EXPECT(pa->s == "mixed" && pa->v == 99, "a 值正确");
    EXPECT(pb->a == 1 && pb->b == 2, "b 值正确");
    printf("\n");
}

// 不同类型 + 不同布局 + 容量互不满足: 走"双方转堆后交换编码与堆指针"路径
static void test_mixed_types_and_layout_mismatch()
{
    printf("=== test_mixed_types_and_layout_mismatch ===\n");
    // 24B 布局装不下 non_trivial (32B), 必须转堆
    using va_small_layout_t = define_void_any<24, 8>;
    using va_large_layout_t = define_void_any<56, 8>;

    va_small_layout_t a(small_trivial{1, 2});      // trivially copyable, inline 模式
    va_large_layout_t b(non_trivial("data", 77));   // 非 trivially copyable, sso vtable 模式

    a.cross_layout_swap(b);

    // a 应转堆持有 non_trivial, b 应持有 small_trivial
    non_trivial* pa = a.get_ptr<non_trivial>();
    small_trivial* pb = b.get_ptr<small_trivial>();
    EXPECT(pa != nullptr, "a 接收 non_trivial (转堆)");
    EXPECT(pb != nullptr, "b 接收 small_trivial");
    EXPECT(pa->s == "data" && pa->v == 77, "a 值正确");
    EXPECT(pb->a == 1 && pb->b == 2, "b 值正确");
    printf("\n");
}

// 不同类型 + 不同布局 + 一方初始就堆: 走"任一方为堆"路径
static void test_mixed_types_with_heap_initial()
{
    printf("=== test_mixed_types_with_heap_initial ===\n");
    using va_small_layout_t = define_void_any<24, 8>;
    using va_large_layout_t = define_void_any<56, 8>;

    // large_trivial (256B) 在 24B 布局中必然堆分配
    va_small_layout_t a(large_trivial{});
    va_large_layout_t b(non_trivial("heap_mix", 88));

    a.cross_layout_swap(b);

    non_trivial* pa = a.get_ptr<non_trivial>();
    large_trivial* pb = b.get_ptr<large_trivial>();
    EXPECT(pa != nullptr, "a 接收 non_trivial");
    EXPECT(pb != nullptr, "b 接收 large_trivial");
    EXPECT(pa->s == "heap_mix" && pa->v == 88, "a 值正确");
    printf("\n");
}

int main()
{
    printf("============================================================\n");
    printf("  void_any 跨布局交换功能测试\n");
    printf("============================================================\n\n");

    test_trivial_sso_to_sso();
    test_medium_trivial_sso_to_sso();
    test_non_trivial_sso_to_sso();
    test_heap_to_sso();
    test_empty_to_value();
    test_value_to_empty();
    test_both_empty();
    test_capacity_mismatch_fallback();
    test_same_layout_swap();
    test_same_layout_swap_non_trivial();
    test_repeated_swap_stability();
    test_mixed_types_swap();
    test_mixed_types_and_layout_mismatch();
    test_mixed_types_with_heap_initial();

    printf("============================================================\n");
    printf("  PASS: %d, FAIL: %d\n", g_pass, g_fail);
    printf("============================================================\n");
    return g_fail == 0 ? 0 : 1;
}
