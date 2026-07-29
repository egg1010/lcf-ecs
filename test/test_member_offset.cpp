// test_member_offset.cpp - member_offset 模块功能测试
#include "test_common.hpp"
#include "include/part/member_offset.hpp"

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
    std::string private_string_;
public:
    PrivateClass() : private_int_(100), private_double_(3.14), private_string_("hello") {}
    int get_private_int() const { return private_int_; }
    double get_private_double() const { return private_double_; }
    const std::string& get_private_string() const { return private_string_; }
};

int main()
{
    // === 1. offset_of 成员指针偏移计算 ===
    print_section(1, "offset_of 成员指针偏移计算");
    {
        print_item("Pod3 x 偏移 == 0", offset_of<Pod3, float>(&Pod3::x) == 0);
        print_item("Pod3 y 偏移 == 4", offset_of<Pod3, float>(&Pod3::y) == 4);
        print_item("Pod3 z 偏移 == 8", offset_of<Pod3, float>(&Pod3::z) == 8);

        print_item("Pod4 a 偏移 == 0", offset_of<Pod4, int>(&Pod4::a) == 0);
        print_item("Pod4 b 偏移 == 4", offset_of<Pod4, int>(&Pod4::b) == 4);
        print_item("Pod4 c 偏移 == 8", offset_of<Pod4, int>(&Pod4::c) == 8);
        print_item("Pod4 d 偏移 == 12", offset_of<Pod4, int>(&Pod4::d) == 12);

        print_item("Mixed c 偏移 == 0", offset_of<Mixed, char>(&Mixed::c) == 0);
        // i 因对齐有 padding
        print_item("Mixed i 偏移 == 4", offset_of<Mixed, int>(&Mixed::i) == 4);
        // d 因 8 字节对齐有 padding
        print_item("Mixed d 偏移 == 8", offset_of<Mixed, double>(&Mixed::d) == 8);
    }

    // === 2. offset_access 直接指针访问 ===
    print_section(2, "offset_access 直接指针访问");
    {
        Pod3 v{1.0f, 2.0f, 3.0f};
        print_item("offset_access<float> 偏移 0 == 1.0", offset_access<float>(&v, 0) == 1.0f);
        print_item("offset_access<float> 偏移 4 == 2.0", offset_access<float>(&v, 4) == 2.0f);
        print_item("offset_access<float> 偏移 8 == 3.0", offset_access<float>(&v, 8) == 3.0f);

        Pod4 p{10, 20, 30, 40};
        print_item("offset_access<int> 偏移 0 == 10", offset_access<int>(&p, 0) == 10);
        print_item("offset_access<int> 偏移 4 == 20", offset_access<int>(&p, 4) == 20);
        print_item("offset_access<int> 偏移 8 == 30", offset_access<int>(&p, 8) == 30);
        print_item("offset_access<int> 偏移 12 == 40", offset_access<int>(&p, 12) == 40);
    }

    // === 3. offset_access 修改值 ===
    print_section(3, "offset_access 修改值");
    {
        Pod3 v{0, 0, 0};
        offset_access<float>(&v, 0) = 100.0f;
        offset_access<float>(&v, 4) = 200.0f;
        offset_access<float>(&v, 8) = 300.0f;
        print_item("修改后 x == 100", v.x == 100.0f);
        print_item("修改后 y == 200", v.y == 200.0f);
        print_item("修改后 z == 300", v.z == 300.0f);
    }

    // === 4. const offset_access ===
    print_section(4, "const offset_access");
    {
        const Pod3 v{5.0f, 6.0f, 7.0f};
        print_item("const offset_access<float> 偏移 0 == 5.0", offset_access<float>(&v, 0) == 5.0f);
        print_item("const offset_access<float> 偏移 4 == 6.0", offset_access<float>(&v, 4) == 6.0f);
        print_item("const offset_access<float> 偏移 8 == 7.0", offset_access<float>(&v, 8) == 7.0f);
    }

    // === 5. offset_access 不同类型 ===
    print_section(5, "offset_access 不同类型");
    {
        Mixed m{'A', 42, 3.14};
        print_item("Mixed c == 'A'", offset_access<char>(&m, 0) == 'A');
        print_item("Mixed i == 42", offset_access<int>(&m, 4) == 42);
        print_item("Mixed d == 3.14", offset_access<double>(&m, 8) == 3.14);
    }

    // === 6. ub_access UB 突破私有访问 ===
    print_section(6, "ub_access UB 突破私有访问");
    {
        PrivateClass obj;
        // 假设 private_int_ 在偏移 0 (类布局: 首成员)
        // 假设 private_double_ 在偏移 8 (int + 4 padding)
        // 假设 private_string_ 在偏移 16
        // 实际偏移因编译器/平台而异, 用 offsetof 验证
        size_t off_int = 0;
        size_t off_double = 8;
        size_t off_string = 16;

        int read_int = ub_access<PrivateClass, int>(obj, off_int);
        print_item("ub_access 读取 private_int_ == 100", read_int == obj.get_private_int());

        double read_double = ub_access<PrivateClass, double>(obj, off_double);
        print_item("ub_access 读取 private_double_ == 3.14", read_double == obj.get_private_double());

        (void)off_string;
    }

    // === 7. ub_access 修改私有成员 ===
    print_section(7, "ub_access 修改私有成员");
    {
        PrivateClass obj;
        size_t off_int = 0;
        ub_access<PrivateClass, int>(obj, off_int) = 999;
        print_item("ub_access 修改 private_int_ == 999", obj.get_private_int() == 999);
    }

    // === 8. offset_desc 结构 ===
    print_section(8, "offset_desc 结构");
    {
        offset_desc desc{"balance_", 32, type_id::get_type_id<int>()};
        print_item("offset_desc name", std::string(desc.name) == "balance_");
        print_item("offset_desc offset == 32", desc.offset == 32);
        print_item("offset_desc type_id 匹配", desc.type_id == type_id::get_type_id<int>());

        offset_desc descs[] = {
            {"name_",    0,  type_id::get_type_id<std::string>()},
            {"balance_", 32, type_id::get_type_id<int>()}
        };
        print_item("offset_desc 数组数量 == 2", sizeof(descs) / sizeof(descs[0]) == 2);
        print_item("descs[0].name", std::string(descs[0].name) == "name_");
        print_item("descs[1].offset == 32", descs[1].offset == 32);
    }

    // === 9. offset_access 与 offset_of 一致性 ===
    print_section(9, "offset_access 与 offset_of 一致性");
    {
        Pod3 v{1.0f, 2.0f, 3.0f};
        size_t off_x = offset_of<Pod3, float>(&Pod3::x);
        size_t off_y = offset_of<Pod3, float>(&Pod3::y);
        size_t off_z = offset_of<Pod3, float>(&Pod3::z);

        print_item("x 偏移一致", offset_access<float>(&v, off_x) == v.x);
        print_item("y 偏移一致", offset_access<float>(&v, off_y) == v.y);
        print_item("z 偏移一致", offset_access<float>(&v, off_z) == v.z);
    }

    // === 10. 大对象偏移访问 ===
    print_section(10, "大对象偏移访问");
    {
        struct Big
        {
            int header;
            char data[100];
            int tail;
        };
        Big b{};
        b.header = 0xABCD;
        b.tail = 0xDEAD;
        print_item("Big header == 0xABCD", offset_access<int>(&b, 0) == 0xABCD);

        // data 数组偏移
        char* data_ptr = &offset_access<char>(&b, sizeof(int));
        data_ptr[0] = 'X';
        data_ptr[99] = 'Z';
        print_item("Big data[0] == 'X'", b.data[0] == 'X');
        print_item("Big data[99] == 'Z'", b.data[99] == 'Z');
    }

    print_summary("功能测试");
    return 0;
}
