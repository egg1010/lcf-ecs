// test_highlevel_reflection.cpp - 高层封装测试: 反射模块
#include "test_common.hpp"
#include "include/reflection/manager.hpp"
#include <array>
#include <cstdint>
#include <string>
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

struct sa_component
{
    std::array<int, 3> xs;
};

struct person_component
{
    std::string name;
};

struct aligned_component
{
    alignas(64) int x;
};

struct ib_a { int a; };
struct ib_b { int b; };
struct ib_d : ib_a, ib_b { int d; };

struct ord_component
{
    int m() { return 3; }
};

static int register_method_first() noexcept
{
    reflect::global().register_method<&ord_component::m>("m");
    return 0;
}

static const int g_method_first = register_method_first();

static int register_all_types() noexcept
{
    auto& s = reflect::global();

    s.register_type_only<vec3>("vec3");
    s.register_member_runtime<vec3>("vec3", "x", &vec3::x);
    s.register_member_runtime<vec3>("vec3", "y", &vec3::y);
    s.register_member_runtime<vec3>("vec3", "z", &vec3::z);

    s.register_type_only<calc>("calc");
    s.register_member_runtime<calc>("calc", "base", &calc::base);
    s.register_method<&calc::add>("add");
    s.register_method<&calc::scale>("scale");

    s.register_type_only<sa_component>("sa_component");
    s.register_member_runtime<sa_component>("sa_component", "xs", &sa_component::xs);

    s.register_type_only<person_component>("person_component");
    s.register_member_runtime<person_component>("person_component", "name", &person_component::name);

    s.register_type_only<aligned_component>("aligned_component");
    s.register_member_runtime<aligned_component>("aligned_component", "x", &aligned_component::x);

    s.register_type_only<ib_a>("ib_a");
    s.register_member_runtime<ib_a>("ib_a", "a", &ib_a::a);
    s.register_type_only<ib_b>("ib_b");
    s.register_member_runtime<ib_b>("ib_b", "b", &ib_b::b);
    s.register_type_only<ib_d>("ib_d");
    s.register_member_runtime<ib_d>("ib_d", "d", &ib_d::d);
    s.register_base_typed<ib_d, ib_a>();
    s.register_base_typed<ib_d, ib_b>();

    s.register_type_only<ord_component>("ord_component");
    return 0;
}

static const int g_types_registered = register_all_types();

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

    t_section(6, "数组步长与安全访问");
    {
        auto q = reflect::try_get<sa_component>();
        const reflect::field_meta* f = q.field_by_name("xs");
        sa_component s{};
        char* p0 = static_cast<char*>(q.array_element_ptr(&s, 0, 0));
        char* p1 = static_cast<char*>(q.array_element_ptr(&s, 0, 1));
        t_check("std::array 元素步长", f != nullptr
                && f->element_stride == sizeof(int)
                && (p1 - p0) == static_cast<ptrdiff_t>(sizeof(int))
                && f->total_elements == 3);

        t_check("try_get_by_name 未知字段为空", q.try_get_by_name<int>(&s, "nope") == nullptr);
        t_check("try_array_get 越界为空", q.try_array_get<int>(&s, 0, 99) == nullptr);
        int* ok_ptr = q.try_get_by_name<int>(&s, "nope");
        t_check("安全访问不崩溃", ok_ptr == nullptr);
    }

    t_section(7, "方法类型校验与重载分派");
    {
        auto q = reflect::try_get<calc>();
        calc c{10};
        t_check("合法调用", q.invoke<int>(&c, "add", 3, 4) == 7);
        t_check("参数类型不匹配拒绝",
                q.try_invoke<void>(&c, "add", std::string("x"), std::string("y")) == false
                && q.try_invoke<int>(&c, "add", 1.5, 2.5).has_value() == false);
        t_check("返回类型不匹配拒绝",
                q.try_invoke<void>(&c, "scale", 5) == false
                && q.try_invoke<void>(&c, "add", 1, 2) == false);
    }

    t_section(8, "哈希契约与深拷贝");
    {
        float pos = 0.0f;
        float neg = -0.0f;
        auto hv = reflect::get_hash<vec3>();
        auto cv = reflect::get_compare<vec3>();
        vec3 a{pos, 1.0f, 2.0f};
        vec3 b{neg, 1.0f, 2.0f};
        t_check("+0/-0 字段相等", cv.equal(&a, &b));
        t_check("+0/-0 哈希一致", hv.hash(&a) == hv.hash(&b));

        person_component src;
        src.name = std::string(64, 'x');
        alignas(person_component) char buf[sizeof(person_component)];
        reflect::get_compare<person_component>().clone(&src, buf);
        person_component* dst = reinterpret_cast<person_component*>(buf);
        t_check("非平凡字段深拷贝", dst->name == src.name
                && dst->name.data() != src.name.data());
        dst->~person_component();
    }

    t_section(9, "继承调整与对齐生命周期");
    {
        ib_d obj{};
        obj.b = 42;
        const reflect::type_meta* dm = reflect::global().get_type(type_id::get_type_id<ib_d>());
        void* up = reflect::inheritance_view(dm).upcast(&obj, type_id::get_type_id<ib_b>());
        t_check("第二基类 upcast 一致", up == static_cast<ib_b*>(&obj)
                && static_cast<ib_b*>(up)->b == 42);

        auto ac = reflect::get_construct<aligned_component>();
        void* heap = ac.create();
        bool aligned = heap != nullptr
                       && (reinterpret_cast<uintptr_t>(heap) % alignof(aligned_component)) == 0;
        ac.destroy_heap(heap);
        t_check("过对齐 create/destroy_heap", aligned);
    }

    t_section(10, "注册顺序无关");
    {
        auto q = reflect::try_get<ord_component>();
        t_check("方法先于类型注册仍可用", q.valid() && q.method_count() == 1
                && q.method_by_name("m") != nullptr);
        ord_component o{};
        t_check("后注册方法可调用", q.invoke<int>(&o, "m") == 3);
    }


    t_section(11, "类型名统一查找");
    {
        const int tid = type_id::get_type_id<vec3>();
        const reflect::type_meta* by_name = reflect::global().find_type("vec3");
        t_check("find_type 命中", by_name != nullptr && by_name->type_id == tid);
        t_check("type_id 名字反查一致", type_id::get_def_type_id("vec3") == tid);
        t_check("未知名返回空", reflect::global().find_type("no_such_type_xyz") == nullptr);
    }

    return t_end("reflection ");
}
