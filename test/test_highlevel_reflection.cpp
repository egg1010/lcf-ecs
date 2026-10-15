// test_highlevel_reflection.cpp - 高层封装测试: 反射模块
#include "test_common.hpp"
#include "include/reflection/reflection.hpp"
#include <string_view>

struct vec3
{
    float x;
    float y;
    float z;
};

struct calc
{
    int base;
    int add(int a, int b) { return a + b; }
    int scale(int k) { return base * k; }
};

REGISTER_TYPE_ONLY(vec3);
REGISTER_MEMBERS(vec3, x, y, z);
REGISTER_TYPE_ONLY(calc);
REGISTER_MEMBERS(calc, base);
REGISTER_FNS(calc, add, scale);

int main()
{
    t_begin("test_highlevel_reflection — 高层封装测试: 反射模块");

    t_section(1, "类型查询");
    {
        auto q = reflect::try_get<vec3>();
        t_check("query valid", q.valid());
        t_check("类型名", std::string_view(q.name()) == "vec3");
        t_check("size 匹配", q.size() == sizeof(vec3));
        t_check("align 匹配", q.align() == alignof(vec3));
        t_check("field_count == 3", q.field_count() == 3);
        t_check("type_id_value 有效", q.type_id_value() >= 0);
    }

    t_section(2, "字段访问");
    {
        auto q = reflect::try_get<vec3>();
        const reflect::field_meta* fy = q.field_by_name("y");
        t_check("field_by_name('y')", fy != nullptr);
        t_check("未知字段返回 null", q.field_by_name("nope") == nullptr);

        vec3 v{1.0f, 2.0f, 3.0f};
        float* py = static_cast<float*>(q.get_ptr(&v, "y"));
        t_check("get_ptr('y') 取值", py != nullptr && *py == 2.0f);

        float* px = static_cast<float*>(q.get_ptr(&v, "x"));
        t_check("get_ptr('x') 可写", px != nullptr);
        if (px) { *px = 9.0f; }
        t_check("写回生效", v.x == 9.0f);
    }

    t_section(3, "方法查询");
    {
        auto q = reflect::try_get<calc>();
        t_check("calc field_count == 1", q.field_count() == 1);
        t_check("calc method_count == 2", q.method_count() == 2);
        t_check("method_by_name('add')", q.method_by_name("add") != nullptr);
        t_check("未知方法 null", q.method_by_name("nope") == nullptr);
    }

    t_section(4, "方法调用");
    {
        auto q = reflect::try_get<calc>();
        calc c{10};
        int r = q.invoke<int>(&c, "add", 3, 4);
        t_check("invoke add", r == 7);
        int s = q.invoke<int>(&c, "scale", 5);
        t_check("invoke scale", s == 50);
    }

    t_section(5, "字段枚举");
    {
        auto q = reflect::try_get<vec3>();
        size_t named = 0;
        for (size_t i = 0; i < q.field_count(); ++i)
        {
            const reflect::field_meta* f = q.field_by_name(
                i == 0 ? "x" : (i == 1 ? "y" : "z"));
            if (f) { ++named; }
        }
        t_check("三个字段均可查", named == 3);
    }

    return t_end("reflection ");
}
