// test_member_offset_perf.cpp - member_offset 独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/member_offset.hpp"

using namespace std;

// === 测试用类型 ===
struct Pod3
{
    float x, y, z;
};

struct Pod4
{
    int a, b, c, d;
};

struct Mixed
{
    char c;
    int i;
    double d;
};

class PrivateClass
{
    int private_int_;
    double private_double_;
public:
    PrivateClass() : private_int_(100), private_double_(3.14) {}
};

// === Section 1: offset_of 成员指针偏移 ===
static void test_offset_of()
{
    print_header("Section 1: offset_of");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 1.1 单成员偏移
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += offset_of<Pod3, float>(&Pod3::x);
            }
            (void)s;
        });
        print_ns("offset_of (1 field)", OPS, ns / static_cast<double>(OPS));
    }

    // 1.2 多成员偏移
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += offset_of<Pod3, float>(&Pod3::x);
                s += offset_of<Pod3, float>(&Pod3::y);
                s += offset_of<Pod3, float>(&Pod3::z);
            }
            (void)s;
        });
        print_ns("offset_of (3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 1.3 跨类型偏移
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += offset_of<Pod3, float>(&Pod3::x);
                s += offset_of<Pod4, int>(&Pod4::a);
                s += offset_of<Mixed, char>(&Mixed::c);
            }
            (void)s;
        });
        print_ns("offset_of (3 types)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    print_footer();
}

// === Section 2: offset_access 直接指针访问 ===
static void test_offset_access()
{
    print_header("Section 2: offset_access");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 2.1 单字段读取
    {
        Pod3 v{1.0f, 2.0f, 3.0f};
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += offset_access<float>(&v, 0);
            }
            (void)s;
        });
        print_ns("offset_access<float> (1 field)", OPS, ns / static_cast<double>(OPS));
    }

    // 2.2 多字段读取
    {
        Pod3 v{1.0f, 2.0f, 3.0f};
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += offset_access<float>(&v, 0);
                s += offset_access<float>(&v, 4);
                s += offset_access<float>(&v, 8);
            }
            (void)s;
        });
        print_ns("offset_access<float> (3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 2.3 混合类型读取
    {
        Mixed m{'A', 42, 3.14};
        double ns = best_ns(REPEAT, [&]() {
            volatile double s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += offset_access<char>(&m, 0);
                s += offset_access<int>(&m, 4);
                s += offset_access<double>(&m, 8);
            }
            (void)s;
        });
        print_ns("offset_access (mixed types)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 2.4 直接访问基线
    {
        Pod3 v{1.0f, 2.0f, 3.0f};
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += v.x;
                s += v.y;
                s += v.z;
            }
            (void)s;
        });
        print_ns("direct access (baseline)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    print_footer();
}

// === Section 3: const offset_access ===
static void test_const_offset_access()
{
    print_header("Section 3: const offset_access");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    {
        const Pod3 v{5.0f, 6.0f, 7.0f};
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += offset_access<float>(&v, 0);
                s += offset_access<float>(&v, 4);
                s += offset_access<float>(&v, 8);
            }
            (void)s;
        });
        print_ns("const offset_access (3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    print_footer();
}

// === Section 4: ub_access 私有成员访问 ===
static void test_ub_access()
{
    print_header("Section 4: ub_access (private member)");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    PrivateClass obj;

    // 4.1 读取私有成员
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += ub_access<PrivateClass, int>(obj, 0);
            }
            (void)s;
        });
        print_ns("ub_access<int> (read)", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 写入私有成员
    {
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                ub_access<PrivateClass, int>(obj, 0) = static_cast<int>(i);
            }
        });
        print_ns("ub_access<int> (write)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: offset_access 修改值 ===
static void test_offset_access_write()
{
    print_header("Section 5: offset_access write");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    {
        Pod3 v{0, 0, 0};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                offset_access<float>(&v, 0) = static_cast<float>(i);
                offset_access<float>(&v, 4) = static_cast<float>(i);
                offset_access<float>(&v, 8) = static_cast<float>(i);
            }
        });
        print_ns("offset_access write (3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 直接写入基线
    {
        Pod3 v{0, 0, 0};
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                v.x = static_cast<float>(i);
                v.y = static_cast<float>(i);
                v.z = static_cast<float>(i);
            }
        });
        print_ns("direct write (baseline)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  member_offset 独立性能测试\n";
    cout << "============================================================\n";

    test_offset_of();
    test_offset_access();
    test_const_offset_access();
    test_ub_access();
    test_offset_access_write();

    cout << "\n============================================================\n";
    cout << "  member_offset 性能测试完成\n";
    cout << "============================================================\n";
    return 0;
}
