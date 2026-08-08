// test_void_any_vs_std_any_perf.cpp - void_any vs std::any 性能对比
// 覆盖: 构造/析构/拷贝/移动/赋值/reset/类型查询/指针访问/取值
// 使用 volatile sink 阻止 DCE, 确保真实数据
#include "perf_common.hpp"
#include "include/part/void_any.hpp"
#include <any>
#include <string>

using namespace std;

// === 测试组件 (覆盖不同存储路径) ===
struct Small     { int v; };                  // 4B,  SSO
struct Medium    { float a[8]; };            // 32B, SSO
struct Large     { double a[32]; };           // 256B, heap
struct NonTrivial {                          // 非 trivially copyable
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

// === 对比输出辅助 ===
inline void print_compare(const char* label, size_t n,
                          double va_ns, double sa_ns) noexcept
{
    double va_tp = (va_ns > 0 && n > 0) ? static_cast<double>(n) / va_ns : 0;
    double sa_tp = (sa_ns > 0 && n > 0) ? static_cast<double>(n) / sa_ns : 0;
    const char* verdict;
    if (va_ns < sa_ns * 0.95)       verdict = "[void_any WIN]";
    else if (sa_ns < va_ns * 0.95) verdict = "[std::any WIN]";
    else                           verdict = "[TIE]";

    cout << "  " << left << setw(32) << label
         << " | void_any: " << fixed << setprecision(3) << setw(9) << va_ns << " ns"
         << " (" << setprecision(2) << setw(7) << va_tp << " G/s)"
         << " | std::any: " << setw(9) << sa_ns << " ns"
         << " (" << setw(7) << sa_tp << " G/s)"
         << " " << verdict << "\n";
}

// === Section 1: 构造与析构 ===
template <typename T>
static void test_construct_compare(size_t n)
{
    print_header(("Section 1: construct/dtor (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 5;

    // 1.1 默认构造
    {
        double va_ns = best_ns(REPEAT, [&]() {
            void_any a; compiler_barrier();
            return a.has_value();
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            std::any a; compiler_barrier();
            return a.has_value();
        });
        print_compare("default ctor", 1, va_ns, sa_ns);
    }

    // 1.2 模板构造 (从值)
    {
        T v{};
        double va_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                void_any a(v);
                compiler_barrier();
            }
            return n;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                std::any a(v);
                compiler_barrier();
            }
            return n;
        });
        print_compare("template ctor(T)", n, va_ns / n, sa_ns / n);
    }

    // 1.3 拷贝构造
    {
        T v{};
        void_any va_src(v);
        std::any sa_src(v);
        double va_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                void_any a(va_src);
                compiler_barrier();
            }
            return n;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                std::any a(sa_src);
                compiler_barrier();
            }
            return n;
        });
        print_compare("copy ctor", n, va_ns / n, sa_ns / n);
    }

    // 1.4 移动构造
    {
        T v{};
        double va_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                void_any src(v);
                void_any a(std::move(src));
                compiler_barrier();
            }
            return n;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                std::any src(v);
                std::any a(std::move(src));
                compiler_barrier();
            }
            return n;
        });
        print_compare("move ctor", n, va_ns / n, sa_ns / n);
    }

    // 1.5 析构 (含构造)
    {
        T v{};
        double va_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                void_any a(v);
                compiler_barrier();
            }
            return n;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                std::any a(v);
                compiler_barrier();
            }
            return n;
        });
        print_compare("dtor (incl ctor)", n, va_ns / n, sa_ns / n);
    }

    print_footer();
}

// === Section 2: 赋值与重置 ===
template <typename T>
static void test_assign_compare(size_t n)
{
    print_header(("Section 2: assign/reset (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;

    // 2.1 set / emplace (从值, 已含值)
    {
        T v{};
        double va_ns = best_ns(REPEAT, [&]() {
            void_any a(v);
            for (size_t i = 0; i < n; ++i) a.set(v);
            compiler_barrier();
            return a.has_value();
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            std::any a(v);
            for (size_t i = 0; i < n; ++i) a = v;
            compiler_barrier();
            return a.has_value();
        });
        print_compare("set(T) / a=v", n, va_ns / n, sa_ns / n);
    }

    // 2.2 拷贝赋值
    {
        T v{};
        void_any va_src(v);
        std::any sa_src(v);
        double va_ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < n; ++i) a = va_src;
            compiler_barrier();
            return a.has_value();
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            std::any a;
            for (size_t i = 0; i < n; ++i) a = sa_src;
            compiler_barrier();
            return a.has_value();
        });
        print_compare("copy assign", n, va_ns / n, sa_ns / n);
    }

    // 2.3 移动赋值
    {
        T v{};
        double va_ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < n; ++i)
            {
                void_any src(v);
                a = std::move(src);
            }
            compiler_barrier();
            return a.has_value();
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            std::any a;
            for (size_t i = 0; i < n; ++i)
            {
                std::any src(v);
                a = std::move(src);
            }
            compiler_barrier();
            return a.has_value();
        });
        print_compare("move assign", n, va_ns / n, sa_ns / n);
    }

    // 2.4 reset
    {
        T v{};
        double va_ns = best_ns(REPEAT, [&]() {
            void_any a(v);
            for (size_t i = 0; i < n; ++i)
            {
                a.reset();
                a.set(v);
            }
            compiler_barrier();
            return a.has_value();
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            std::any a(v);
            for (size_t i = 0; i < n; ++i)
            {
                a.reset();
                a = v;
            }
            compiler_barrier();
            return a.has_value();
        });
        print_compare("reset", n, va_ns / n, sa_ns / n);
    }

    print_footer();
}

// === Section 3: 状态查询 ===
template <typename T>
static void test_query_compare()
{
    print_header(("Section 3: query (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    T v{};
    void_any va(v);
    std::any sa(v);

    // 3.1 has_value
    {
        double va_ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i) s = va.has_value();
            (void)s;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i) s = sa.has_value();
            (void)s;
        });
        print_compare("has_value", OPS, va_ns / OPS, sa_ns / OPS);
    }

    // 3.2 type_id / type()
    {
        double va_ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i) s += va.type_id();
            (void)s;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i) s += sa.type().hash_code();
            (void)s;
        });
        print_compare("type_id / type().hash", OPS, va_ns / OPS, sa_ns / OPS);
    }

    print_footer();
}

// === Section 4: 指针/值访问 ===
template <typename T>
static void test_access_compare(size_t n)
{
    print_header(("Section 4: access (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;

    T v{};
    void_any va(v);
    std::any sa(v);

    // 4.1 get_ptr (含类型校验) / any_cast<T> (返回 nullptr 表示不匹配)
    //     使用 g_sink 强制每次循环执行, 阻止循环不变量外提
    {
        double va_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = va.get_ptr<T>();
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = std::any_cast<T>(&sa);
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        print_compare("get_ptr<T> (checked)", n, va_ns / n, sa_ns / n);
    }

    // 4.2 fast_get_ptr (void_any 跳过 type_id 检查)
    //     std::any 无对应接口, 用 any_cast 对比
    {
        double va_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = va.fast_get_ptr<T>();
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = std::any_cast<T>(&sa);
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        print_compare("fast_get_ptr vs any_cast", n, va_ns / n, sa_ns / n);
    }

    // 4.3 get_ptr_unchecked (void_any 编译期 SSO 判定)
    //     std::any 无对应接口, 用 any_cast 对比
    {
        double va_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = va.get_ptr_unchecked<T>();
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = std::any_cast<T>(&sa);
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        print_compare("get_ptr_unchecked vs any_cast", n, va_ns / n, sa_ns / n);
    }

    // 4.4 get<T> (返回值拷贝) / any_cast<T> (返回拷贝, 抛异常路径已避开)
    //     用 g_sink 抓取返回值首字节, 阻止 DCE
    {
        double va_ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i)
            {
                sink = va.get<T>();
                g_sink += *reinterpret_cast<const uint8_t*>(&sink);
            }
            compiler_barrier();
            return g_sink;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i)
            {
                sink = std::any_cast<T>(sa);
                g_sink += *reinterpret_cast<const uint8_t*>(&sink);
            }
            compiler_barrier();
            return g_sink;
        });
        print_compare("get<T> / any_cast<T>", n, va_ns / n, sa_ns / n);
    }

    // 4.5 get_ptr 类型不匹配 (返回 nullptr)
    {
        struct Other { int x; };
        double va_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                Other* p = va.get_ptr<Other>();
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                Other* p = std::any_cast<Other>(&sa);
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        print_compare("mismatch (nullptr)", n, va_ns / n, sa_ns / n);
    }

    // 4.6 get_void (void*) / 等效 any_cast<void> 不存在, 用 any_cast<T> 模拟访问
    {
        double va_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                void* p = va.get_void();
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < n; ++i)
            {
                T* p = std::any_cast<T>(&sa);
                g_sink += reinterpret_cast<size_t>(p);
            }
            compiler_barrier();
            return g_sink;
        });
        print_compare("get_void vs any_cast", n, va_ns / n, sa_ns / n);
    }

    print_footer();
}

// === Section 5: 编译期已知 T 的高性能接口 ===
template <typename T>
static void test_compile_time_compare(size_t n)
{
    print_header(("Section 5: compile-time T (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;

    // 5.1 copy_from (void_any 编译期 sizeof) / std::any = a (赋值)
    {
        T v{};
        double va_ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < n; ++i) a.copy_from(v);
            compiler_barrier();
            return a.has_value();
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            std::any a;
            for (size_t i = 0; i < n; ++i) a = v;
            compiler_barrier();
            return a.has_value();
        });
        print_compare("copy_from<T> vs a=v", n, va_ns / n, sa_ns / n);
    }

    // 5.2 move_from / std::any = std::move(tmp)
    {
        double va_ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < n; ++i)
            {
                T tmp{};
                a.move_from(std::move(tmp));
            }
            compiler_barrier();
            return a.has_value();
        });
        double sa_ns = best_ns(REPEAT, [&]() {
            std::any a;
            for (size_t i = 0; i < n; ++i)
            {
                T tmp{};
                a = std::move(tmp);
            }
            compiler_barrier();
            return a.has_value();
        });
        print_compare("move_from<T> vs a=move", n, va_ns / n, sa_ns / n);
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  void_any vs std::any 性能对比\n";
    cout << "  (SSO=ON, layered_allocator=ON, sizeof(void_any)="
         << sizeof(void_any) << "B, sizeof(std::any)=" << sizeof(std::any) << "B)\n";
    cout << "============================================================\n";

    const size_t OPS = 1 << 16;  // 64K (Large 类型耗时高, 减小 ops)

    cout << "\n=== Small (4B, SSO) ===\n";
    test_construct_compare<Small>(OPS);
    test_assign_compare<Small>(OPS);
    test_query_compare<Small>();
    test_access_compare<Small>(OPS);
    test_compile_time_compare<Small>(OPS);

    cout << "\n=== Medium (32B, SSO) ===\n";
    test_construct_compare<Medium>(OPS);
    test_assign_compare<Medium>(OPS);
    test_query_compare<Medium>();
    test_access_compare<Medium>(OPS);
    test_compile_time_compare<Medium>(OPS);

    cout << "\n=== Large (256B, heap) ===\n";
    test_construct_compare<Large>(OPS);
    test_assign_compare<Large>(OPS);
    test_query_compare<Large>();
    test_access_compare<Large>(OPS);
    test_compile_time_compare<Large>(OPS);

    cout << "\n=== NonTrivial (std::string, heap) ===\n";
    test_construct_compare<NonTrivial>(OPS);
    test_assign_compare<NonTrivial>(OPS);
    test_query_compare<NonTrivial>();
    test_access_compare<NonTrivial>(OPS);
    test_compile_time_compare<NonTrivial>(OPS);

    cout << "\n============================================================\n";
    cout << "  对比测试完成\n";
    cout << "============================================================\n";
    return 0;
}
