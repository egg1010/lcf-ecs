// test_void_any_perf.cpp - void_any 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/void_any.hpp"

using namespace std;

// 测试组件 (SSO vs heap 不同路径)
struct Small { int v; };                  // 4B, 走 SSO
struct Medium { float a[8]; };            // 32B, 走 SSO (默认 SSO 阈值)
struct Large { double a[32]; };           // 256B, 走 heap
struct NonTrivial {                       // 非 trivially copyable
    std::string s;
    NonTrivial() : s("hello") {}
    NonTrivial(const std::string& v) : s(v) {}
    NonTrivial(const NonTrivial& o) : s(o.s) {}
    NonTrivial(NonTrivial&& o) noexcept : s(std::move(o.s)) {}
    NonTrivial& operator=(const NonTrivial& o) { s = o.s; return *this; }
    NonTrivial& operator=(NonTrivial&& o) noexcept { s = std::move(o.s); return *this; }
    ~NonTrivial() = default;
};

// === Section 1: 构造与析构 ===
template <typename T>
static void test_construct(size_t ops)
{
    print_header(("Section 1: construct (T=" + to_string(sizeof(T)) + "B, ops=" + to_string(ops) + ")").c_str());
    constexpr int REPEAT = 3;

    // 1.1 默认构造
    {
        double ns = best_ns(REPEAT, [&]() {
            void_any a;
            compiler_barrier();
            return a.has_value();
        });
        print_ns("default ctor", 1, ns);
    }

    // 1.2 模板构造 (从值)
    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < ops; ++i)
            {
                void_any a(v);
                compiler_barrier();
            }
            return ops;
        });
        print_ns("template ctor(T)", ops, ns / static_cast<double>(ops));
    }

    // 1.3 拷贝构造
    {
        T v{};
        void_any src(v);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < ops; ++i)
            {
                void_any a(src);
                compiler_barrier();
            }
            return ops;
        });
        print_ns("copy ctor", ops, ns / static_cast<double>(ops));
    }

    // 1.4 移动构造
    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < ops; ++i)
            {
                void_any src(v);
                void_any a(std::move(src));
                compiler_barrier();
            }
            return ops;
        });
        print_ns("move ctor", ops, ns / static_cast<double>(ops));
    }

    // 1.5 析构
    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < ops; ++i)
            {
                void_any a(v);
                compiler_barrier();
            }
            return ops;
        });
        print_ns("dtor (incl template ctor)", ops, ns / static_cast<double>(ops));
    }

    print_footer();
}

// === Section 2: 赋值与重置 ===
template <typename T>
static void test_assign(size_t ops)
{
    print_header(("Section 2: assign (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 3;

    // 2.1 set (从值)
    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < ops; ++i) a.set(v);
            compiler_barrier();
            return a.has_value();
        });
        print_ns("set(T)", ops, ns / static_cast<double>(ops));
    }

    // 2.2 拷贝赋值
    {
        T v{};
        void_any src(v);
        double ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < ops; ++i) a = src;
            compiler_barrier();
            return a.has_value();
        });
        print_ns("copy assign", ops, ns / static_cast<double>(ops));
    }

    // 2.3 移动赋值
    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < ops; ++i)
            {
                void_any src(v);
                a = std::move(src);
            }
            compiler_barrier();
            return a.has_value();
        });
        print_ns("move assign", ops, ns / static_cast<double>(ops));
    }

    // 2.4 reset
    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            void_any a(v);
            for (size_t i = 0; i < ops; ++i)
            {
                a.reset();
                a.set(v);
            }
            compiler_barrier();
            return a.has_value();
        });
        print_ns("reset", ops, ns / static_cast<double>(ops));
    }

    print_footer();
}

// === Section 3: 状态查询 ===
template <typename T>
static void test_query()
{
    print_header(("Section 3: query (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    T v{};
    void_any a(v);

    // 3.1 type_id / has_value
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += a.type_id();
                s += a.has_value() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("type_id/has_value", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 4: 指针/值访问 ===
template <typename T>
static void test_access(size_t ops)
{
    print_header(("Section 4: access (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 5;

    T v{};
    void_any a(v);

    // 4.1 get_ptr (含类型校验)
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = nullptr;
            for (size_t i = 0; i < ops; ++i) p = a.get_ptr<T>();
            touch_ptr(p);
            return p;
        });
        print_ns("get_ptr<T> (checked)", ops, ns / static_cast<double>(ops));
    }

    // 4.2 fast_get_ptr (跳过 type_id 检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = nullptr;
            for (size_t i = 0; i < ops; ++i) p = a.fast_get_ptr<T>();
            touch_ptr(p);
            return p;
        });
        print_ns("fast_get_ptr<T>", ops, ns / static_cast<double>(ops));
    }

    // 4.3 get_ptr_unchecked
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = nullptr;
            for (size_t i = 0; i < ops; ++i) p = a.get_ptr_unchecked<T>();
            touch_ptr(p);
            return p;
        });
        print_ns("get_ptr_unchecked<T>", ops, ns / static_cast<double>(ops));
    }

    // 4.4 get<T> (返回值拷贝)
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < ops; ++i) sink = a.get<T>();
            compiler_barrier();
            (void)sink;
        });
        print_ns("get<T> (copy)", ops, ns / static_cast<double>(ops));
    }

    // 4.5 get_ptr 类型不匹配 (返回 nullptr)
    {
        struct Other { int x; };
        double ns = best_ns(REPEAT, [&]() {
            Other* p = nullptr;
            for (size_t i = 0; i < ops; ++i) p = a.get_ptr<Other>();
            return p;
        });
        print_ns("get_ptr<Other> (mismatch)", ops, ns / static_cast<double>(ops));
    }

    // 4.6 get_void (void* 设计理念)
    {
        double ns = best_ns(REPEAT, [&]() {
            void* p = nullptr;
            for (size_t i = 0; i < ops; ++i) p = a.get_void();
            touch_ptr(p);
            return p;
        });
        print_ns("get_void (void*)", ops, ns / static_cast<double>(ops));
    }

    print_footer();
}

// === Section 5: 编译期已知 T 的高性能接口 ===
template <typename T>
static void test_compile_time(size_t ops)
{
    print_header(("Section 5: compile-time T (T=" + to_string(sizeof(T)) + "B)").c_str());
    constexpr int REPEAT = 3;

    // 5.1 copy_from (编译期 sizeof, 避免 vtable 读取)
    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < ops; ++i) a.copy_from(v);
            compiler_barrier();
            return a.has_value();
        });
        print_ns("copy_from<T> (ct)", ops, ns / static_cast<double>(ops));
    }

    // 5.2 move_from (编译期 sizeof)
    {
        double ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < ops; ++i)
            {
                T tmp{};
                a.move_from(std::move(tmp));
            }
            compiler_barrier();
            return a.has_value();
        });
        print_ns("move_from<T> (ct)", ops, ns / static_cast<double>(ops));
    }

    // 5.3 对比: set (运行时路径)
    {
        T v{};
        double ns = best_ns(REPEAT, [&]() {
            void_any a;
            for (size_t i = 0; i < ops; ++i) a.set(v);
            compiler_barrier();
            return a.has_value();
        });
        print_ns("set(T) (对比)", ops, ns / static_cast<double>(ops));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  void_any 独立性能测试\n";
    cout << "============================================================\n";

    const size_t OPS = 1 << 16;  // 64K (Large 类型耗时高, 减小 ops)

    cout << "\n=== Small (4B, SSO) ===\n";
    test_construct<Small>(OPS);
    test_assign<Small>(OPS);
    test_query<Small>();
    test_access<Small>(OPS);
    test_compile_time<Small>(OPS);

    cout << "\n=== Medium (32B, SSO) ===\n";
    test_construct<Medium>(OPS);
    test_assign<Medium>(OPS);
    test_query<Medium>();
    test_access<Medium>(OPS);
    test_compile_time<Medium>(OPS);

    cout << "\n=== Large (256B, heap) ===\n";
    test_construct<Large>(OPS);
    test_assign<Large>(OPS);
    test_query<Large>();
    test_access<Large>(OPS);
    test_compile_time<Large>(OPS);

    cout << "\n=== NonTrivial (std::string, heap) ===\n";
    test_construct<NonTrivial>(OPS);
    test_assign<NonTrivial>(OPS);
    test_query<NonTrivial>();
    test_access<NonTrivial>(OPS);
    test_compile_time<NonTrivial>(OPS);

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
