// test_reflection_perf.cpp - 反射模块独立性能测试, 使用 time.hpp
#include "perf_common.hpp"
#include "include/reflection/reflection.hpp"

using namespace std;

// === 测试用类型定义 ===
struct Vec3 { float x, y, z; };
struct Vec8 { float a, b, c, d, e, f, g, h; };
struct Pod16 { int a, b, c, d; };
struct Path { float points[16]; };
struct Grid { float cells[8][8]; };

class Account
{
    std::string name_;
    int balance_;
public:
    REFLECT(Account);
    Account() : name_(""), balance_(0) {}
    Account(std::string n, int b) : name_(n), balance_(b) {}
    void deposit(int amt) { balance_ += amt; }
    int get_balance() const { return balance_; }
    static int version() { return 42; }
};

class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    int sum5(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
    static int multiply(int a, int b) { return a * b; }
};

// === 注册 (静态初始化) ===
REGISTER(Vec3);
REGISTER(Vec8);
REGISTER(Pod16);
REGISTER(Account);
REGISTER(Calculator);

REFLECT_PRIVATE(Account, name_, balance_);

REGISTER_MEMBERS(Path, points);
REGISTER_MEMBERS(Grid, cells);

REGISTER_FNS(Calculator, add, sum5, is_positive, no_return, multiply);
REGISTER_FNS(Account, deposit, get_balance, version);

// === Section 1: 查询入口 ===
static void test_query_entry()
{
    print_header("Section 1: query entry");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    // 1.1 get<T>() 稳态查询
    {
        volatile auto v = reflect::get<Vec3>();
        (void)v;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                auto view = reflect::get<Vec3>();
                s += view.field_count();
            }
            (void)s;
        });
        print_ns("get<T>() (steady)", OPS, ns / static_cast<double>(OPS));
    }

    // 1.2 get_by_name 查询
    {
        volatile auto v = reflect::get_by_name("Vec3");
        (void)v;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                auto view = reflect::get_by_name("Vec3");
                s += view.field_count();
            }
            (void)s;
        });
        print_ns("get_by_name (1 type)", OPS, ns / static_cast<double>(OPS));
    }

    // 1.3 get_by_name 多类型混合
    {
        volatile auto v1 = reflect::get_by_name("Vec3");
        volatile auto v2 = reflect::get_by_name("Calculator");
        volatile auto v3 = reflect::get_by_name("Account");
        (void)v1; (void)v2; (void)v3;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += reflect::get_by_name("Vec3").field_count();
                s += reflect::get_by_name("Calculator").method_count();
                s += reflect::get_by_name("Account").field_count();
            }
            (void)s;
        });
        print_ns("get_by_name (3 types mixed)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    print_footer();
}

// === Section 2: 字段访问 ===
static void test_field_access()
{
    print_header("Section 2: field access");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    Vec3 v{1.0f, 2.0f, 3.0f};
    auto view = reflect::get<Vec3>();

    // 2.1 按索引访问 (编译期偏移)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += view.get<float>(&v, 0);
                s += view.get<float>(&v, 1);
                s += view.get<float>(&v, 2);
            }
            (void)s;
        });
        print_ns("get<T>(idx) (3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 2.2 按名访问 (运行期 strcmp)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += view.get_by_name<float>(&v, "field_0");
                s += view.get_by_name<float>(&v, "field_1");
                s += view.get_by_name<float>(&v, "field_2");
            }
            (void)s;
        });
        print_ns("get_by_name<T>(name) (3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 2.3 get_ptr 按名
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile void* p = nullptr;
            for (size_t i = 0; i < OPS; ++i)
            {
                p = view.get_ptr(&v, "field_0");
                p = view.get_ptr(&v, "field_1");
                p = view.get_ptr(&v, "field_2");
            }
            (void)p;
        });
        print_ns("get_ptr(name) (3 fields)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 2.4 直接访问 (基线对照)
    {
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

    // 2.5 私有成员访问
    {
        Account acc{"Alice", 100};
        auto view2 = reflect::get<Account>();
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += view2.get_by_name<int>(&acc, "balance_");
            }
            (void)s;
        });
        print_ns("get_by_name<T>(private)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 3: 字段元数据查询 ===
static void test_field_meta_query()
{
    print_header("Section 3: field meta query");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    auto view = reflect::get<Vec3>();

    // 3.1 field(i) 元数据
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += view.field(0).offset;
                s += view.field(1).offset;
                s += view.field(2).offset;
            }
            (void)s;
        });
        print_ns("field(idx).offset", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 3.2 field(name) 元数据 (strcmp)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint32_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += view.field_by_name("field_0")->offset;
                s += view.field_by_name("field_1")->offset;
                s += view.field_by_name("field_2")->offset;
            }
            (void)s;
        });
        print_ns("field(name)->offset", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 3.3 for_each_field_meta 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                view.for_each_field_meta([&](const reflect::field_meta& fm) {
                    s += fm.offset;
                });
            }
            (void)s;
        });
        print_ns("for_each_field_meta", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 3.4 for_each_field 遍历实例
    {
        Vec3 v{1.0f, 2.0f, 3.0f};
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                view.for_each_field(&v, [&](const char* /*name*/, void* ptr, int /*tid*/) {
                    s += *static_cast<float*>(ptr);
                });
            }
            (void)s;
        });
        print_ns("for_each_field (instance)", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    print_footer();
}

// === Section 4: 方法调用 ===
static void test_method_invoke()
{
    print_header("Section 4: method invoke");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    auto view = reflect::get<Calculator>();
    Calculator calc;

    // 4.1 2 参数方法
    {
        int a = opaque(10);
        int b = opaque(20);
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += view.invoke<int>(&calc, "add", a, b);
            }
            (void)s;
        });
        print_ns("invoke(add, 2 args)", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 5 参数方法
    {
        int a = opaque(1);
        int b = opaque(2);
        int c = opaque(3);
        int d = opaque(4);
        int e = opaque(5);
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += view.invoke<int>(&calc, "sum5", a, b, c, d, e);
            }
            (void)s;
        });
        print_ns("invoke(sum5, 5 args)", OPS, ns / static_cast<double>(OPS));
    }

    // 4.3 const 方法
    {
        int x = opaque(5);
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i)
            {
                s = view.invoke<bool>(&calc, "is_positive", x);
            }
            (void)s;
        });
        print_ns("invoke(const method)", OPS, ns / static_cast<double>(OPS));
    }

    // 4.4 void 返回方法
    {
        int x = opaque(42);
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i)
            {
                view.invoke<void>(&calc, "no_return", x);
            }
        });
        print_ns("invoke(void method)", OPS, ns / static_cast<double>(OPS));
    }

    // 4.5 静态方法
    {
        int a = opaque(3);
        int b = opaque(4);
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += view.invoke<int>(nullptr, "multiply", a, b);
            }
            (void)s;
        });
        print_ns("invoke(static method)", OPS, ns / static_cast<double>(OPS));
    }

    // 4.6 直接调用基线对照
    {
        int a = opaque(10);
        int b = opaque(20);
        double ns = best_ns(REPEAT, [&]() {
            volatile int s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += calc.add(a, b);
            }
            (void)s;
        });
        print_ns("direct call (baseline)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: 方法元数据查询 ===
static void test_method_meta_query()
{
    print_header("Section 5: method meta query");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    auto view = reflect::get<Calculator>();

    // 5.1 method(name) 元数据
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile uint8_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += view.method_by_name("add")->arg_count;
                s += view.method_by_name("sum5")->arg_count;
                s += view.method_by_name("multiply")->arg_count;
            }
            (void)s;
        });
        print_ns("method(name)->arg_count", 3 * OPS, ns / static_cast<double>(3 * OPS));
    }

    // 5.2 for_each_method 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                view.for_each_method([&](const reflect::method_meta& mm) {
                    s += mm.arg_count;
                });
            }
            (void)s;
        });
        print_ns("for_each_method", 5 * OPS, ns / static_cast<double>(5 * OPS));
    }

    print_footer();
}

// === Section 6: 注册开销 ===
static void test_register_cost()
{
    print_header("Section 6: register cost");

    // 6.1 聚合类型注册 (cold, 新类型)
    {
        struct ColdVec3 { float x, y, z; };
        constexpr int REPEAT = 1000;
        double ns = best_ns(REPEAT, [&]() {
            ::reflect::global().register_type<ColdVec3>("ColdVec3");
        });
        print_ns("register_type (aggregate)", 1, ns);
    }

    // 6.2 非聚合类型注册 (cold, 新类型)
    {
        struct ColdEmpty {};
        constexpr int REPEAT = 1000;
        double ns = best_ns(REPEAT, [&]() {
            ::reflect::global().register_type<ColdEmpty>("ColdEmpty");
        });
        print_ns("register_type (empty)", 1, ns);
    }

    // 6.3 重复注册 (热路径, 已注册)
    {
        constexpr int REPEAT = 5;
        constexpr size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                ::reflect::global().register_type<Vec3>("Vec3");
                s += 1;
            }
            (void)s;
        });
        print_ns("register_type (repeat, skip)", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 7: 数组字段 ===
static void test_array_field()
{
    print_header("Section 7: array field");
    constexpr int REPEAT = 5;
    constexpr size_t OPS = 1000000;

    auto path_view = reflect::get<Path>();
    auto grid_view = reflect::get<Grid>();

    Path p{};
    for (int i = 0; i < 16; ++i) p.points[i] = static_cast<float>(i);
    Grid g{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            g.cells[i][j] = static_cast<float>(i * 8 + j);

    // 7.1 is_array 查询
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile bool s = false;
            for (size_t i = 0; i < OPS; ++i)
            {
                s = path_view.is_array(0);
                s = path_view.is_array_by_name("points");
            }
            (void)s;
        });
        print_ns("is_array", OPS * 2, ns / static_cast<double>(OPS * 2));
    }

    // 7.2 array_total_elements / array_element_stride
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += path_view.array_total_elements(0);
                s += path_view.array_element_stride(0);
                s += path_view.array_rank(0);
                s += path_view.array_extent(0, 0);
            }
            (void)s;
        });
        print_ns("array_meta_query", OPS * 4, ns / static_cast<double>(OPS * 4));
    }

    // 7.3 array_element_ptr (一维)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                auto* ptr = static_cast<float*>(path_view.array_element_ptr(&p, 0, opaque(i & 15)));
                s += *ptr;
            }
            (void)s;
        });
        print_ns("array_element_ptr (1D)", OPS, ns / static_cast<double>(OPS));
    }

    // 7.4 array_element_ptr (二维)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                auto* ptr = static_cast<float*>(grid_view.array_element_ptr(&g, 0, opaque(i & 63)));
                s += *ptr;
            }
            (void)s;
        });
        print_ns("array_element_ptr (2D)", OPS, ns / static_cast<double>(OPS));
    }

    // 7.5 for_each_array_element (一维 16 元素)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS / 16; ++i)
            {
                float sum = 0;
                path_view.for_each_array_element(&p, 0, [&](void* ptr, uint32_t idx, int tid) {
                    sum += *static_cast<float*>(ptr);
                    (void)idx; (void)tid;
                });
                s = sum;
            }
            (void)s;
        });
        print_ns("for_each_array_element (1D, 16)", OPS / 16, ns / static_cast<double>(OPS / 16));
    }

    // 7.6 for_each_array_element (二维 64 元素)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile float s = 0;
            for (size_t i = 0; i < OPS / 64; ++i)
            {
                float sum = 0;
                grid_view.for_each_array_element(&g, 0, [&](void* ptr, uint32_t idx, int tid) {
                    sum += *static_cast<float*>(ptr);
                    (void)idx; (void)tid;
                });
                s = sum;
            }
            (void)s;
        });
        print_ns("for_each_array_element (2D, 64)", OPS / 64, ns / static_cast<double>(OPS / 64));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  反射模块独立性能测试\n";
    cout << "============================================================\n";

    test_query_entry();
    test_field_access();
    test_field_meta_query();
    test_method_invoke();
    test_method_meta_query();
    test_register_cost();
    test_array_field();

    cout << "\n============================================================\n";
    cout << "  反射模块性能测试完成\n";
    cout << "============================================================\n";
    return 0;
}
