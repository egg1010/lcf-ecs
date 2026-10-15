// test_module_void_any.cpp - 模块功能测试: void_any
#include "test_common.hpp"
#include "include/part/void_any.hpp"
#include "include/part/type_id.hpp"
#include <string>

struct widget
{
    int a;
    double b;
};

struct big_payload
{
    char data[128];
};

int main()
{
    t_begin("test_module_void_any — 模块功能测试: void_any");

    t_section(1, "基本存取");
    {
        void_any a(widget{7, 1.5});
        t_check("has_value", a.has_value());
        widget* w = a.get_ptr<widget>();
        t_check("get_ptr 命中", w != nullptr && w->a == 7);
        void_any a2(widget{8, 2.5});
        void_any a3(int(3));
        t_check("同类型 type_id 一致", a.type_id() == a2.type_id());
        t_check("不同类型 type_id 不同", a.type_id() != a3.type_id());

        void_any e;
        t_check("默认空 has_value false", !e.has_value());
        t_check("空 get_ptr null", e.get_ptr<widget>() == nullptr);
    }

    t_section(2, "小对象与内联型");
    {
        void_any small(int(42));
        int* pi = small.get_ptr<int>();
        t_check("int 存取", pi != nullptr && *pi == 42);
        t_check("has_value 为真", small.has_value());

        void_any big(big_payload{});
        big_payload* bp = big.get_ptr<big_payload>();
        t_check("大对象存取", bp != nullptr && big.has_value());
        t_check("大对象 type_id 匹配", big.type_id() == type_id::get_type_id<big_payload>());
    }

    t_section(3, "非平凡类型");
    {
        void_any a(std::string("hello"));
        std::string* s = a.get_ptr<std::string>();
        t_check("string 存取", s != nullptr && *s == "hello");

        void_any b = a;
        std::string* sb = b.get_ptr<std::string>();
        t_check("拷贝独立", sb != nullptr && *sb == "hello" && sb != s);

        void_any c = std::move(a);
        std::string* sc = c.get_ptr<std::string>();
        t_check("移动接管", sc != nullptr && *sc == "hello");
    }

    t_section(4, "类型校验");
    {
        void_any a(widget{1, 0.0});
        t_check("类型不匹配返回 null", a.get_ptr<std::string>() == nullptr);
        t_check("has_value 仍为真", a.has_value());
    }

    return t_end("void_any ");
}
