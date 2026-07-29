// test_reflection.cpp - 反射模块功能测试
#include "test_common.hpp"
#include "include/reflection/reflection.hpp"
#include <thread>

// === 测试用类型定义 ===

// 聚合类型 (公有字段, 自动遍历)
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };
struct Pod4 { int a; };
struct Pod8 { int a, b; };
struct Pod16 { int a, b, c, d; };

// 非聚合类型 (侵入式自动推导, 类内 REFLECT + 类外 REFLECT_PRIVATE)
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

// 方法测试类
class Calculator
{
public:
    int add(int a, int b) { return a + b; }
    int sum5(int a, int b, int c, int d, int e) { return a + b + c + d + e; }
    bool is_positive(int x) const { return x > 0; }
    void no_return(int x) { (void)x; }
    static int multiply(int a, int b) { return a * b; }
};

// 自定义字段名聚合类型
struct Color { float r, g, b, a; };

// 大返回值类型 (>64B, 测试 invoke 缓冲修复)
struct BigResult { int data[20]; };
class BigCalculator
{
public:
    BigResult get_big() const
    {
        BigResult r{};
        for (int i = 0; i < 20; ++i) r.data[i] = i + 1;
        return r;
    }
};

// 未注册类型 (测试软失败)
struct UnregisteredType { int x; };

// === 注册 ===
REGISTER(Vec3);
REGISTER(Vec2);
REGISTER(Pod4);
REGISTER(Pod8);
REGISTER(Pod16);
REGISTER(Account);
REGISTER(Calculator);
REGISTER_NAMED(Color, "r", "g", "b", "a");
REGISTER(BigCalculator);

// 注册私有成员 (侵入式自动推导, 偏移量和类型由成员指针计算)
REFLECT_PRIVATE(Account, name_, balance_);

// 注册方法
REGISTER_METHOD(Calculator, add);
REGISTER_METHOD(Calculator, sum5);
REGISTER_METHOD(Calculator, is_positive);
REGISTER_METHOD(Calculator, no_return);
REGISTER_STATIC_METHOD(Calculator, multiply);

REGISTER_METHOD(Account, deposit);
REGISTER_METHOD(Account, get_balance);
REGISTER_STATIC_METHOD(Account, version);
REGISTER_METHOD(BigCalculator, get_big);

int main()
{
    // === 1. 聚合类型字段自动遍历 ===
    print_section(1, "聚合类型字段自动遍历");
    {
        auto v3 = reflect::get<Vec3>();
        print_item("Vec3 字段数 == 3", v3.field_count() == 3);
        print_item("Vec3 sizeof == 12", v3.size() == 12);
        print_item("Vec3 field_0 偏移 == 0", v3.field(0).offset == 0);
        print_item("Vec3 field_1 偏移 == 4", v3.field(1).offset == 4);
        print_item("Vec3 field_2 偏移 == 8", v3.field(2).offset == 8);
        print_item("Vec3 类型名", std::string(v3.name()) == "Vec3");

        auto v2 = reflect::get<Vec2>();
        print_item("Vec2 字段数 == 2", v2.field_count() == 2);

        auto p4 = reflect::get<Pod4>();
        print_item("Pod4 字段数 == 1", p4.field_count() == 1);

        auto p16 = reflect::get<Pod16>();
        print_item("Pod16 字段数 == 4", p16.field_count() == 4);
    }

    // === 2. 字段类型 id ===
    print_section(2, "字段类型 id");
    {
        auto v3 = reflect::get<Vec3>();
        int float_id = type_id::get_type_id<float>();
        print_item("Vec3 field_0 type_id == float", v3.field(0).type_id == float_id);
        print_item("Vec3 field_1 type_id == float", v3.field(1).type_id == float_id);

        auto p8 = reflect::get<Pod8>();
        int int_id = type_id::get_type_id<int>();
        print_item("Pod8 field_0 type_id == int", p8.field(0).type_id == int_id);
    }

    // === 3. 字段按名查询 ===
    print_section(3, "字段按名查询");
    {
        auto v3 = reflect::get<Vec3>();
        const auto* f = v3.field_by_name("field_0");
        print_item("Vec3 field_0 按名查找成功", f != nullptr);
        print_item("Vec3 field_0 偏移 == 0", f && f->offset == 0);

        const auto* not_found = v3.field_by_name("nonexistent");
        print_item("不存在字段返回 nullptr", not_found == nullptr);
    }

    // === 4. 字段值访问 ===
    print_section(4, "字段值访问");
    {
        Vec3 v{1.0f, 2.0f, 3.0f};
        auto view = reflect::get<Vec3>();

        print_item("get<float>(0) == 1.0", view.get<float>(&v, 0) == 1.0f);
        print_item("get<float>(1) == 2.0", view.get<float>(&v, 1) == 2.0f);
        print_item("get<float>(2) == 3.0", view.get<float>(&v, 2) == 3.0f);

        // 按名访问
        print_item("get_by_name<float>(field_0) == 1.0", view.get_by_name<float>(&v, "field_0") == 1.0f);

        // 修改值
        view.get<float>(&v, 0) = 10.0f;
        print_item("修改后 x == 10.0", v.x == 10.0f);

        // 类型擦除指针
        void* ptr = view.get_ptr(&v, "field_1");
        print_item("get_ptr 后 *ptr == 2.0", *static_cast<float*>(ptr) == 2.0f);
    }

    // === 5. 遍历实例字段 ===
    print_section(5, "遍历实例字段");
    {
        Vec3 v{5.0f, 6.0f, 7.0f};
        auto view = reflect::get<Vec3>();

        size_t count = 0;
        float sum = 0;
        view.for_each_field(&v, [&](const char* name, void* ptr, int tid) {
            (void)name; (void)tid;
            sum += *static_cast<float*>(ptr);
            ++count;
        });
        print_item("遍历字段数 == 3", count == 3);
        print_item("字段值之和 == 18.0", sum == 18.0f);
    }

    // === 6. 私有成员访问 (手填偏移量) ===
    print_section(6, "私有成员访问");
    {
        auto view = reflect::get<Account>();
        print_item("Account 字段数 == 2", view.field_count() == 2);

        const auto* balance = view.field_by_name("balance_");
        print_item("balance_ 字段找到", balance != nullptr);
        print_item("balance_ 偏移 == 32", balance && balance->offset == 32);
        print_item("balance_ is_private == true", balance && balance->is_private);

        // 通过偏移量访问私有成员
        Account acc{"Alice", 100};
        int bal = view.get_by_name<int>(&acc, "balance_");
        print_item("读取私有 balance_ == 100", bal == 100);

        // 修改私有成员
        view.get_by_name<int>(&acc, "balance_") = 200;
        print_item("修改私有 balance_ == 200", acc.get_balance() == 200);
    }

    // === 7. 方法注册与调用 ===
    print_section(7, "方法注册与调用");
    {
        auto view = reflect::get<Calculator>();
        print_item("Calculator 方法数 == 5", view.method_count() == 5);

        Calculator calc;

        // 2 参数方法
        int result = view.invoke<int>(&calc, "add", 10, 20);
        print_item("add(10,20) == 30", result == 30);

        // 5 参数方法 (参数无上限)
        int s5 = view.invoke<int>(&calc, "sum5", 1, 2, 3, 4, 5);
        print_item("sum5(1,2,3,4,5) == 15", s5 == 15);

        // const 方法
        bool pos = view.invoke<bool>(&calc, "is_positive", 5);
        print_item("is_positive(5) == true", pos);

        bool neg = view.invoke<bool>(&calc, "is_positive", -1);
        print_item("is_positive(-1) == false", !neg);

        // void 返回方法
        view.invoke<void>(&calc, "no_return", 42);
        print_item("no_return 调用成功", true);

        // 静态方法
        int mul = view.invoke<int>(nullptr, "multiply", 3, 4);
        print_item("multiply(3,4) == 12", mul == 12);
    }

    // === 8. 方法元数据查询 ===
    print_section(8, "方法元数据查询");
    {
        auto view = reflect::get<Calculator>();

        const auto* m = view.method_by_name("add");
        print_item("add 方法找到", m != nullptr);
        print_item("add 参数数 == 2", m && m->arg_count == 2);
        print_item("add 返回类型 == int", m && m->return_type_id == type_id::get_type_id<int>());
        print_item("add 非静态", m && !m->is_static);

        const auto* mp = view.method_by_name("is_positive");
        print_item("is_positive 是 const", mp && mp->is_const);

        const auto* ms = view.method_by_name("multiply");
        print_item("multiply 是静态", ms && ms->is_static);
        print_item("multiply 参数数 == 2", ms && ms->arg_count == 2);
    }

    // === 9. 按类型名查询 ===
    print_section(9, "按类型名查询");
    {
        auto view = reflect::get_by_name("Vec3");
        print_item("按名查询 Vec3 成功", std::string(view.name()) == "Vec3");
        print_item("Vec3 字段数 == 3", view.field_count() == 3);

        auto view2 = reflect::get_by_name("Calculator");
        print_item("按名查询 Calculator 成功", view2.method_count() == 5);
    }

    // === 10. 遍历字段元数据 ===
    print_section(10, "遍历字段元数据");
    {
        auto view = reflect::get<Vec3>();
        size_t count = 0;
        view.for_each_field_meta([&](const reflect::field_meta& fm) {
            (void)fm;
            ++count;
        });
        print_item("遍历字段元数据数 == 3", count == 3);
    }

    // === 11. 遍历方法元数据 ===
    print_section(11, "遍历方法元数据");
    {
        auto view = reflect::get<Calculator>();
        size_t count = 0;
        view.for_each_method([&](const reflect::method_meta& mm) {
            (void)mm;
            ++count;
        });
        print_item("遍历方法元数据数 == 5", count == 5);
    }

    // === 12. 重复注册保护 ===
    print_section(12, "重复注册保护");
    {
        // 重复注册 (直接调用, 宏 REGISTER 只能在命名空间作用域使用)
        ::reflect::global().register_type<Vec3>("Vec3");
        auto view = reflect::get<Vec3>();
        print_item("重复注册后字段数仍 == 3", view.field_count() == 3);
    }

    // === 13. 自定义字段名 ===
    print_section(13, "自定义字段名 (REGISTER_NAMED)");
    {
        auto c = reflect::get<Color>();
        print_item("Color 字段数 == 4", c.field_count() == 4);
        print_item("Color field 'r' 找到", c.field_by_name("r") != nullptr);
        print_item("Color field 'g' 找到", c.field_by_name("g") != nullptr);
        print_item("Color field 'b' 找到", c.field_by_name("b") != nullptr);
        print_item("Color field 'a' 找到", c.field_by_name("a") != nullptr);
        print_item("Color 无 field_0", c.field_by_name("field_0") == nullptr);

        Color col{0.1f, 0.2f, 0.3f, 0.4f};
        print_item("Color.r == 0.1", c.get_by_name<float>(&col, "r") == 0.1f);
        print_item("Color.a == 0.4", c.get_by_name<float>(&col, "a") == 0.4f);
    }

    // === 14. 软失败接口 ===
    print_section(14, "软失败接口 (try_get / try_invoke)");
    {
        auto view = reflect::try_get<Vec3>();
        print_item("try_get<Vec3> 有效", view.valid());

        auto invalid = reflect::try_get<UnregisteredType>();
        print_item("try_get 未注册类型无效", !invalid.valid());

        auto by_name = reflect::try_get_by_name("Vec3");
        print_item("try_get_by_name Vec3 有效", by_name.valid());

        auto not_found = reflect::try_get_by_name("Nonexistent");
        print_item("try_get_by_name 未注册无效", !not_found.valid());

        Calculator calc;
        auto calc_view = reflect::try_get<Calculator>();
        auto add_result = calc_view.try_invoke<int>(&calc, "add", 1, 2);
        print_item("try_invoke add 成功", add_result.has_value() && *add_result == 3);

        auto fail = calc_view.try_invoke<int>(&calc, "nonexistent", 1, 2);
        print_item("try_invoke 不存在方法返回空", !fail.has_value());

        auto bad_args = calc_view.try_invoke<int>(&calc, "add", 1);
        print_item("try_invoke 参数数量不匹配返回空", !bad_args.has_value());

        bool void_ok = calc_view.try_invoke<void>(&calc, "no_return", 42);
        print_item("try_invoke void 成功", void_ok);

        bool void_fail = calc_view.try_invoke<void>(&calc, "nonexistent", 42);
        print_item("try_invoke void 不存在返回 false", !void_fail);

        bool void_bad = calc_view.try_invoke<void>(&calc, "no_return");
        print_item("try_invoke void 参数不匹配返回 false", !void_bad);
    }

    // === 15. 大返回值 (>64B) ===
    print_section(15, "大返回值 (sizeof > 64B)");
    {
        auto view = reflect::get<BigCalculator>();
        print_item("BigCalculator 方法数 == 1", view.method_count() == 1);

        BigCalculator bc;
        BigResult result = view.invoke<BigResult>(&bc, "get_big");
        print_item("大返回值 data[0] == 1", result.data[0] == 1);
        print_item("大返回值 data[19] == 20", result.data[19] == 20);

        auto try_result = view.try_invoke<BigResult>(&bc, "get_big");
        print_item("try_invoke 大返回值成功", try_result.has_value() && try_result->data[0] == 1);
        print_item("try_invoke 大返回值 data[19] == 20", try_result.has_value() && try_result->data[19] == 20);
    }

    // === 16. 多线程注册 ===
    print_section(16, "多线程注册安全");
    {
        std::thread t1([]{ ::reflect::global().register_type<Vec3>("Vec3"); });
        std::thread t2([]{ ::reflect::global().register_type<Vec3>("Vec3"); });
        std::thread t3([]{ ::reflect::global().register_type<Color>("Color"); });
        std::thread t4([]{ ::reflect::global().register_type<Color>("Color"); });
        t1.join();
        t2.join();
        t3.join();
        t4.join();

        auto v3 = reflect::get<Vec3>();
        print_item("多线程注册 Vec3 字段数仍 == 3", v3.field_count() == 3);

        auto col = reflect::get<Color>();
        print_item("多线程注册 Color 字段 'r' 仍存在", col.field_by_name("r") != nullptr);
    }

    print_summary("功能测试");
    return 0;
}
