// test_aggregate_reflect.cpp - aggregate_reflect 模块功能测试
#include "test_common.hpp"
#include "include/part/aggregate_reflect.hpp"

// === 测试用类型 ===
struct Empty {};
struct Pod1 { int a; };
struct Pod2 { int a; float b; };
struct Pod3 { float x, y, z; };
struct Pod4 { int a, b, c, d; };
struct Pod8 { int a, b, c, d, e, f, g, h; };
struct Pod16 { int a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p; };
struct Pod17 { int a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q; };
struct Pod32 { int a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z, aa, ab, ac, ad, ae, af; };
struct Pod64 { int a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z, aa, ab, ac, ad, ae, af, ag, ah, ai, aj, ak, al, am, an, ao, ap, aq, ar, as, at, au, av, aw, ax, ay, az, ba, bb, bc, bd, be, bf, bg, bh, bi, bj, bk, bl; };

struct Nested
{
    Pod3 inner;
    int extra;
};

struct WithArray
{
    int arr[4];
    float f;
};

// 非聚合类型 (有自定义构造)
struct NonAggregate
{
    int a, b;
    NonAggregate(int x, int y) : a(x), b(y) {}
};

int main()
{
    // === 1. 字段计数 ===
    print_section(1, "字段计数");
    {
        print_item("Empty 字段数 == 0", aggregate_field_count_v<Empty> == 0);
        print_item("Pod1 字段数 == 1", aggregate_field_count_v<Pod1> == 1);
        print_item("Pod2 字段数 == 2", aggregate_field_count_v<Pod2> == 2);
        print_item("Pod3 字段数 == 3", aggregate_field_count_v<Pod3> == 3);
        print_item("Pod4 字段数 == 4", aggregate_field_count_v<Pod4> == 4);
        print_item("Pod8 字段数 == 8", aggregate_field_count_v<Pod8> == 8);
        print_item("Pod16 字段数 == 16", aggregate_field_count_v<Pod16> == 16);
        print_item("Nested 字段数 == 2", aggregate_field_count_v<Nested> == 2);
        // WithArray { int arr[4]; float f; } 数组成员会被识别为多字段聚合
        print_item("WithArray 字段数 > 0", aggregate_field_count_v<WithArray> > 0);
    }

    // === 2. 非聚合类型返回 0 ===
    print_section(2, "非聚合类型");
    {
        print_item("NonAggregate 字段数 == 0", aggregate_field_count_v<NonAggregate> == 0);
        print_item("int 字段数 == 0", aggregate_field_count_v<int> == 0);
        print_item("float 字段数 == 0", aggregate_field_count_v<float> == 0);
    }

    // === 3. 编译期常量 ===
    print_section(3, "编译期常量");
    {
        constexpr size_t n3 = aggregate_field_count_v<Pod3>;
        constexpr size_t n4 = aggregate_field_count_v<Pod4>;
        print_item("constexpr Pod3 == 3", n3 == 3);
        print_item("constexpr Pod4 == 4", n4 == 4);
    }

    // === 4. for_each_aggregate_member 遍历 ===
    print_section(4, "for_each_aggregate_member 遍历");
    {
        Pod3 v{1.0f, 2.0f, 3.0f};
        size_t count = 0;
        float sum = 0;
        for_each_aggregate_member(v, [&](auto& member, size_t idx) {
            sum += static_cast<float>(member);
            ++count;
            (void)idx;
        });
        print_item("Pod3 遍历字段数 == 3", count == 3);
        print_item("Pod3 字段值之和 == 6.0", sum == 6.0f);
    }

    // === 5. 遍历索引正确性 ===
    print_section(5, "遍历索引正确性");
    {
        Pod4 v{10, 20, 30, 40};
        int indices[4] = {0, 0, 0, 0};
        int values[4] = {0, 0, 0, 0};
        for_each_aggregate_member(v, [&](auto& member, size_t idx) {
            if (idx < 4)
            {
                indices[idx] = static_cast<int>(idx);
                values[idx] = member;
            }
        });
        print_item("索引 0 == 0", indices[0] == 0);
        print_item("索引 1 == 1", indices[1] == 1);
        print_item("索引 2 == 2", indices[2] == 2);
        print_item("索引 3 == 3", indices[3] == 3);
        print_item("值 0 == 10", values[0] == 10);
        print_item("值 1 == 20", values[1] == 20);
        print_item("值 2 == 30", values[2] == 30);
        print_item("值 3 == 40", values[3] == 40);
    }

    // === 6. 修改字段值 ===
    print_section(6, "修改字段值");
    {
        Pod3 v{0, 0, 0};
        for_each_aggregate_member(v, [&](auto& member, size_t idx) {
            (void)idx;
            member = 100.0f;
        });
        print_item("修改后 x == 100", v.x == 100.0f);
        print_item("修改后 y == 100", v.y == 100.0f);
        print_item("修改后 z == 100", v.z == 100.0f);
    }

    // === 7. const 对象遍历 ===
    print_section(7, "const 对象遍历");
    {
        const Pod3 v{5.0f, 6.0f, 7.0f};
        size_t count = 0;
        float sum = 0;
        for_each_aggregate_member(v, [&](const auto& member, size_t idx) {
            sum += static_cast<float>(member);
            ++count;
            (void)idx;
        });
        print_item("const Pod3 遍历字段数 == 3", count == 3);
        print_item("const Pod3 字段值之和 == 18.0", sum == 18.0f);
    }

    // === 8. 空类型遍历不调用 ===
    print_section(8, "空类型遍历");
    {
        Empty e{};
        size_t count = 0;
        for_each_aggregate_member(e, [&](auto& member, size_t idx) {
            (void)member; (void)idx;
            ++count;
        });
        print_item("Empty 遍历字段数 == 0", count == 0);
    }

    // === 9. 大字段数 (16 个) ===
    print_section(9, "大字段数");
    {
        Pod16 v{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        size_t count = 0;
        int sum = 0;
        for_each_aggregate_member(v, [&](auto& member, size_t idx) {
            sum += member;
            ++count;
            (void)idx;
        });
        print_item("Pod16 遍历字段数 == 16", count == 16);
        print_item("Pod16 字段值之和 == 120", sum == 120);
    }

    // === 9b. 超过 16 字段 (扩展上限测试) ===
    print_section(11, "扩展字段数 (17/32/64)");
    {
        print_item("Pod17 字段数 == 17", aggregate_field_count_v<Pod17> == 17);
        print_item("Pod32 字段数 == 32", aggregate_field_count_v<Pod32> == 32);
        print_item("Pod64 字段数 == 64", aggregate_field_count_v<Pod64> == 64);
    }

    // === 9c. Pod17 遍历 ===
    print_section(12, "Pod17 遍历");
    {
        Pod17 v{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        size_t count = 0;
        int sum = 0;
        for_each_aggregate_member(v, [&](auto& member, size_t idx) {
            sum += member;
            ++count;
            (void)idx;
        });
        print_item("Pod17 遍历字段数 == 17", count == 17);
        print_item("Pod17 字段值之和 == 136", sum == 136);
    }

    // === 9d. Pod32 遍历 ===
    print_section(13, "Pod32 遍历");
    {
        Pod32 v{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
        size_t count = 0;
        int sum = 0;
        for_each_aggregate_member(v, [&](auto& member, size_t idx) {
            sum += member;
            ++count;
            (void)idx;
        });
        print_item("Pod32 遍历字段数 == 32", count == 32);
        print_item("Pod32 字段值之和 == 496", sum == 496);
    }

    // === 9e. Pod64 遍历 ===
    print_section(14, "Pod64 遍历");
    {
        Pod64 v{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
                32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
                48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63};
        size_t count = 0;
        long long sum = 0;
        for_each_aggregate_member(v, [&](auto& member, size_t idx) {
            sum += member;
            ++count;
            (void)idx;
        });
        print_item("Pod64 遍历字段数 == 64", count == 64);
        print_item("Pod64 字段值之和 == 2016", sum == 2016);
    }

    // === 10. member_offset 计算 ===
    print_section(15, "member_offset 计算");
    {
        print_item("Pod3 成员 x 偏移 == 0", member_offset(&Pod3::x) == 0);
        print_item("Pod3 成员 y 偏移 == 4", member_offset(&Pod3::y) == 4);
        print_item("Pod3 成员 z 偏移 == 8", member_offset(&Pod3::z) == 8);
        print_item("Pod4 成员 b 偏移 == 4", member_offset(&Pod4::b) == 4);
        print_item("Pod4 成员 d 偏移 == 12", member_offset(&Pod4::d) == 12);
    }

    print_summary("功能测试");
    return 0;
}
