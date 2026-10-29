// test_highlevel_reflection_manager.cpp - manager 增删改查测试: 反射模块
#include "test_common.hpp"
#include "include/reflection/manager.hpp"

#include <array>
#include <cstring>
#include <new>
#include <string>

struct vec3
{
    float x;
    float y;
    float z;
};

struct path4
{
    int points[4];
};

struct path_leaf
{
    int value;
};

struct path_root
{
    path_leaf leaf;
};

struct calc
{
    int base;
    int add(int a, int b) { return a + b + base; }
    int scale(int k) { return base * k; }
};

class account
{
    std::string name_;
    int balance_ = 0;

    template<typename> friend struct ::reflect::reflect_access;

public:
    int balance() const { return balance_; }
};

namespace reflect {
template<>
struct reflect_access<account>
{
    static auto fields() noexcept
    {
        return std::make_tuple(
            std::pair{"name_", &account::name_},
            std::pair{"balance_", &account::balance_});
    }
};
} // namespace reflect

enum color
{
    col_red = 1,
    col_green = 2,
    col_blue = 3
};

struct shape_base
{
    int a;
};

struct shape_derived : shape_base
{
    int b;
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

struct ib_a
{
    int a;
};

struct ib_b
{
    int b;
};

struct ib_d : ib_a, ib_b
{
    int d;
};

struct opaque_component
{
    int hidden;
};

class sealed
{
    int value_ = 5;

    explicit sealed(int v) : value_(v) {}

    template<typename> friend struct ::reflect::reflect_access;
};

namespace reflect {
template<>
struct reflect_access<sealed>
{
    static void* create(void* buf) noexcept
    {
        if (buf != nullptr)
        {
            return new (buf) sealed(0);
        }
        return new sealed(0);
    }

    static void destroy(void* obj) noexcept
    {
        delete static_cast<sealed*>(obj);
    }
};
} // namespace reflect

class priv_base
{
    int a_ = 1;
};

class priv_derived : private priv_base
{
    int b_ = 2;

    template<typename> friend struct ::reflect::reflect_access;
};

namespace reflect {
template<>
struct reflect_access<priv_derived>
{
    static void* to_base(void* obj) noexcept
    {
        return static_cast<priv_base*>(static_cast<priv_derived*>(obj));
    }
};
} // namespace reflect

class vault
{
    int base_ = 10;

    int secret(int k) { return base_ * k; }
    int read(int k) const { return base_ + k; }
    void bump(int delta) { base_ += delta; }
    std::string echo(const std::string& s) const { return s; }
    int scale(int k) noexcept { return base_ * k; }
    double scale(double k) const noexcept { return base_ * k; }
    static int origin() noexcept { return 100; }

    template<typename> friend struct ::reflect::reflect_access;

public:
    int base() const { return base_; }
};

namespace reflect {
template<>
struct reflect_access<vault>
{
    static auto fields() noexcept
    {
        return std::make_tuple(std::pair{"base_", &vault::base_});
    }

    static auto methods() noexcept
    {
        return std::make_tuple(
            std::pair{"secret", &vault::secret},
            std::pair{"read", &vault::read},
            std::pair{"bump", &vault::bump},
            std::pair{"echo", &vault::echo},
            std::pair{"scale_int", static_cast<int (vault::*)(int) noexcept>(&vault::scale)},
            std::pair{"scale_double", static_cast<double (vault::*)(double) const noexcept>(&vault::scale)});
    }

    static auto static_methods() noexcept
    {
        return std::make_tuple(std::pair{"origin", &vault::origin});
    }
};
} // namespace reflect

int main()
{
    t_console_utf8();

    t_section(1, "manager 增: 类型与字段");
    {
        reflect::manager m;
        const operating_message added = m.add_type<vec3>();
        t_check("add_type 自动类型名", added && m.has<vec3>());
        t_check("类型名命中", m.find("vec3").valid());
        t_check("字段数初始为 0", m.get<vec3>().field_count() == 0);

        t_check("add_field 标量", m.add_field<vec3>("x", &vec3::x));
        (void)m.add_field<vec3>("y", &vec3::y);
        (void)m.add_field<vec3>("z", &vec3::z);
        t_check("字段数 3", m.get<vec3>().field_count() == 3);
        t_check("字段偏移正确", m.field<vec3>("y")->offset == offsetof(vec3, y));

        vec3 v{1.0f, 2.0f, 3.0f};
        t_check("按名读字段", m.get<vec3>().get_by_name<float>(&v, "z") == 3.0f);

        (void)m.add_type<path4>();
        t_check("数组字段", m.add_field<path4>("points", &path4::points));
        const reflect::field_meta* pf = m.field<path4>("points");
        t_check("数组 rank/元素数", pf != nullptr && pf->array_rank == 1 && pf->total_elements == 4);

        path4 p{};
        p.points[3] = 42;
        int* elem = static_cast<int*>(m.get<path4>().array_element_ptr_by_name(&p, "points", 3));
        t_check("数组元素访问", elem != nullptr && *elem == 42);

        reflect::manager nested;
        (void)nested.add_type<path_leaf>();
        (void)nested.add_field<path_leaf>("value", &path_leaf::value);
        (void)nested.add_type<path_root>();
        (void)nested.add_field<path_root>("leaf", &path_root::leaf);
        path_root root{};
        root.leaf.value = 77;
        int* leaf_value = nested.get<path_root>().get_by_path_as<int>(&root, "leaf.value");
        t_check("实例存储的嵌套路径访问", leaf_value != nullptr && *leaf_value == 77);
    }

    t_section(2, "manager 增: 私有字段 / 方法 / 枚举 / 继承");
    {
        reflect::manager m;
        account acc;

        (void)m.add_type<account>();
        t_check("私有字段批量注册", m.add_accessed_members<account>()
                && m.get<account>().field_count() == 2);
        t_check("私有字段读取", m.get<account>().get_by_name<int>(&acc, "balance_") == 0);
        t_check("私有字段标记", m.field<account>("balance_")->is_private());

        (void)m.add_type<calc>();
        (void)m.add_field<calc>("base", &calc::base);
        t_check("方法注册", m.add_method<&calc::add>("add"));
        calc c{};
        c.base = 10;
        int sum = 0;
        t_check("方法调用", m.invoke(sum, c, "add", 1, 2) && sum == 13);
        t_check("方法名查询", m.method<calc>("add") != nullptr);

        t_check("枚举注册", m.add_enum<color>("color", {{color::col_red, "red"},
                                                        {color::col_green, "green"}}));
        const reflect::enum_meta* em = m.store().find_enum<color>();
        t_check("枚举值表", em != nullptr && em->values.size() == 2);
        t_check("枚举项名", em != nullptr && std::strcmp(em->values[0].name, "red") == 0);

        (void)m.add_type<shape_derived>();
        (void)m.add_type<shape_base>();
        t_check("继承注册", m.add_base<shape_derived, shape_base>()
                && m.get<shape_derived>().meta()->base_offsets.size() == 1);
    }

    t_section(3, "manager 查: 遍历视图与计数");
    {
        reflect::manager m;
        (void)m.add_type<vec3>();
        (void)m.add_field<vec3>("x", &vec3::x);
        (void)m.add_field<vec3>("y", &vec3::y);

        size_t seen = 0;
        bool names_ok = true;
        for (const reflect::field_meta& f : m.get_fields<vec3>())
        {
            names_ok = names_ok && f.name != nullptr;
            ++seen;
        }
        t_check("字段视图遍历", seen == 2 && names_ok);
        t_check("字段视图 size", m.get_fields<vec3>().size() == 2);
        t_check("类型计数", m.type_count() == 1);
        t_check("未注册类型 invalid", !m.get<calc>().valid());
        t_check("has 未注册为 false", !m.has<calc>());
    }

    t_section(4, "manager 改");
    {
        reflect::manager m;
        (void)m.add_type<vec3>();
        (void)m.add_field<vec3>("x", &vec3::x);
        (void)m.add_field<vec3>("y", &vec3::y);
        (void)m.add_field<vec3>("z", &vec3::z);

        t_check("改类型名", m.rename_type<vec3>("point3"));
        t_check("新名命中", m.find("point3").valid());
        t_check("旧名失效", !m.find("vec3").valid());
        t_check("同名改名幂等", m.rename_type<vec3>("point3"));

        t_check("改字段名", m.rename_field<vec3>("x", "px"));
        t_check("字段新名命中", m.field<vec3>("px") != nullptr);
        t_check("字段旧名失效", m.field<vec3>("x") == nullptr);

        vec3 v{7.0f, 8.0f, 9.0f};
        t_check("重绑字段偏移", m.rebind_field<vec3>("px", &vec3::y));
        t_check("重绑后读到 y", m.get<vec3>().get_by_name<float>(&v, "px") == 8.0f);

        t_check("改字段标志", m.set_field_flags<vec3>(
            "px", reflect::field_flag_private | reflect::field_flag_const));
        t_check("标志生效", m.field<vec3>("px")->is_private() && m.field<vec3>("px")->is_const());

        (void)m.set_field_attr<vec3>("px", "range", 100);
        (void)m.set_field_attr<vec3>("px", "range", 200);
        const reflect::field_meta* f = m.field<vec3>("px");
        t_check("属性覆盖不追加", f != nullptr && f->attrs.size() == 1);

        (void)m.add_type<calc>();
        (void)m.add_field<calc>("base", &calc::base);
        (void)m.add_method<&calc::add>("add");
        t_check("改方法名", m.rename_method<&calc::add>("add", "plus"));
        t_check("方法新名命中", m.method<calc>("plus") != nullptr && m.method<calc>("add") == nullptr);
    }

    t_section(5, "manager 删");
    {
        reflect::manager m;
        (void)m.add_type<vec3>();
        (void)m.add_field<vec3>("x", &vec3::x);
        (void)m.add_field<vec3>("y", &vec3::y);
        (void)m.add_field<vec3>("z", &vec3::z);

        t_check("删字段", m.remove_field<vec3>("y"));
        t_check("字段数减一", m.get<vec3>().field_count() == 2);
        t_check("被删字段为空", m.field<vec3>("y") == nullptr);
        t_check("后续字段前移", m.field<vec3>("z") != nullptr
                && m.field<vec3>("z")->offset == offsetof(vec3, z));
        t_check("删不存在字段失败", !m.remove_field<vec3>("nope"));

        (void)m.add_type<calc>();
        (void)m.add_field<calc>("base", &calc::base);
        (void)m.add_method<&calc::add>("add");
        t_check("删方法", m.remove_method<&calc::add>("add"));
        t_check("方法数归零", m.get<calc>().method_count() == 0);

        (void)m.add_enum<color>("color", {{color::col_red, "red"}});
        t_check("删枚举", m.remove_enum<color>());
        t_check("枚举表为空", m.store().find_enum<color>() == nullptr);

        (void)m.add_type<shape_derived>();
        (void)m.add_type<shape_base>();
        (void)m.add_base<shape_derived, shape_base>();
        t_check("删继承", m.remove_base<shape_derived, shape_base>());
        t_check("继承表为空", m.get<shape_derived>().meta()->base_offsets.size() == 0);
    }

    t_section(6, "manager 退休与清除");
    {
        reflect::manager m;
        (void)m.add_type<vec3>();
        (void)m.add_field<vec3>("x", &vec3::x);

        t_check("退休类型", m.retire_type<vec3>());
        t_check("退休后不可查", !m.has<vec3>());
        t_check("退休后名字解绑", !m.find("vec3").valid());
        t_check("重复退休失败", !m.retire_type<vec3>());

        t_check("清除类型", m.purge_type<vec3>());
        t_check("清除后计数归零", m.type_count() == 0);
        t_check("清除后可重新注册", m.add_type_as<vec3>("vec3") && m.has<vec3>());

        reflect::manager m2;
        (void)m2.add_type_as<vec3>("vec3");
        t_check("未退休不能清除", !m2.purge_type<vec3>());
    }

    t_section(7, "manager 冻结");
    {
        reflect::manager m;
        (void)m.add_type<vec3>();
        (void)m.add_field<vec3>("x", &vec3::x);
        m.freeze();
        t_check("冻结状态", m.frozen());

        const operating_message frozen_error = m.add_field<vec3>("y", &vec3::y);
        t_check("冻结后写失败", !frozen_error);
        t_check("错误码为 frozen", frozen_error.is_code(om_err_reflect_frozen));
        t_check("冻结后仍可查", m.field<vec3>("x") != nullptr);

        m.unfreeze();
        t_check("解冻后可写", m.add_field<vec3>("y", &vec3::y));
    }

    t_section(8, "manager 实例隔离与错误码");
    {
        reflect::manager a;
        reflect::manager b;
        (void)a.add_type_as<vec3>("shared");
        (void)b.add_type_as<vec3>("other");

        t_check("实例 a 命中自己的名字", a.find("shared").valid() && !a.find("other").valid());
        t_check("实例 b 命中自己的名字", b.find("other").valid() && !b.find("shared").valid());
        t_check("实例计数独立", a.type_count() == 1 && b.type_count() == 1);

        const operating_message missing = a.add_field<calc>("base", &calc::base);
        t_check("未注册类型加字段失败", !missing && missing.is_code(om_err_reflect_type_not_found));

        (void)a.add_field<vec3>("x", &vec3::x);
        const operating_message duplicate = a.add_field<vec3>("x", &vec3::x);
        t_check("重复字段失败", !duplicate && duplicate.is_code(om_err_reflect_duplicate));

        const operating_message empty_name = a.add_field<vec3>("", &vec3::y);
        t_check("空名被拒", !empty_name && empty_name.is_code(om_err_reflect_invalid_name));
    }

    t_section(9, "manager 操作视图");
    {
        reflect::manager m;
        (void)m.add_type<vec3>();
        (void)m.add_field<vec3>("x", &vec3::x);
        (void)m.add_field<vec3>("y", &vec3::y);
        (void)m.add_field<vec3>("z", &vec3::z);

        vec3 a{1.0f, 2.0f, 3.0f};
        vec3 b{1.0f, 2.0f, 3.0f};
        t_check("compare 相等", m.get_compare<vec3>().equal(&a, &b));
        b.z = 9.0f;
        t_check("compare 不等", !m.get_compare<vec3>().equal(&a, &b));
        t_check("hash 稳定", m.get_hash<vec3>().hash(&a) == m.get_hash<vec3>().hash(&a));
        t_check("hash 区分", m.get_hash<vec3>().hash(&a) != m.get_hash<vec3>().hash(&b));

        void* obj = m.get_construct<vec3>().create();
        t_check("construct 创建", obj != nullptr);
        m.get_construct<vec3>().destroy_heap(obj);

        (void)m.add_type<shape_derived>();
        (void)m.add_type<shape_base>();
        t_check("继承注册", m.add_base<shape_derived, shape_base>());
        t_check("继承视图走本实例", m.get_inheritance<shape_derived>().is_derived_from<shape_base>());
        t_check("本实例外不可见",
                !reflect::get_inheritance<shape_derived>().is_derived_from<shape_base>());

        (void)m.add_enum<color>("color", {{color::col_red, "red"}});
        t_check("get_enum 命中", m.get_enum<color>().valid());
        t_check("枚举值转名", std::strcmp(m.get_enum<color>().value_to_name(color::col_red), "red") == 0);
    }

    t_section(10, "完整类的私有成员");
    {
        reflect::manager m;
        (void)m.add_type<sealed>();
        t_check("私有构造注册",
                m.set_construct_destruct<sealed>(&reflect::reflect_access<sealed>::create,
                                                 &reflect::reflect_access<sealed>::destroy));
        t_check("可构造标记", m.get_construct<sealed>().has_default_construct());
        void* obj = m.get_construct<sealed>().create();
        t_check("私有构造可用", obj != nullptr);
        m.get_construct<sealed>().destroy_heap(obj);

        (void)m.add_type<priv_derived>();
        (void)m.add_type<priv_base>();
        t_check("私有继承注册",
                m.add_base_with_adjuster<priv_derived, priv_base>(
                    &reflect::reflect_access<priv_derived>::to_base));
        t_check("私有继承可见", m.get_inheritance<priv_derived>().is_derived_from<priv_base>());
        priv_derived d;
        void* base_ptr = m.get_inheritance<priv_derived>().upcast(&d, type_id::get_type_id<priv_base>());
        t_check("私有继承 upcast", base_ptr != nullptr);

        (void)m.add_type<vec3>();
        t_check("单 pair 添加", m.add_fields<vec3>(std::pair{"x", &vec3::x}));
        t_check("单 pair 字段就位", m.field<vec3>("x") != nullptr);
    }

    t_section(11, "任意私有函数");
    {
        reflect::manager m;
        (void)m.add_type<vault>();
        vault v;
        t_check("授信成员批量注册", m.add_accessed_members<vault>());

        int out = 0;
        t_check("非 const 私有函数", m.invoke(out, v, "secret", 3) && out == 30);
        t_check("const 私有函数", m.invoke(out, v, "read", 5) && out == 15);
        t_check("void 私有函数", m.invoke_void(v, "bump", 7) && v.base() == 17);

        std::string text;
        const std::string input = "abc";
        t_check("引用参数私有函数", m.invoke(text, v, "echo", input) && text == "abc");

        t_check("noexcept 私有函数", m.invoke(out, v, "scale_int", 2) && out == 34);
        double scaled = 0.0;
        t_check("const noexcept 重载", m.invoke(scaled, v, "scale_double", 2.5) && scaled == 42.5);

        int origin = 0;
        t_check("私有静态调用", m.invoke(origin, v, "origin") && origin == 100);
        t_check("私有字段已注册", m.field<vault>("base_") != nullptr);

        const auto methods = reflect::reflect_access<vault>::methods();
        t_check("按指针改方法名",
                m.rename_method("secret", "secret2", std::get<0>(methods).second));
        t_check("改名后可见", m.method<vault>("secret2") != nullptr);
        t_check("按指针删方法",
                m.remove_method("secret2", std::get<0>(methods).second));
        t_check("删除后不可见", m.method<vault>("secret2") == nullptr);
        t_check("其余方法未受影响", m.method<vault>("read") != nullptr);
    }

    t_section(12, "旧契约迁移");
    {
        reflect::manager m;
        (void)m.add_type<calc>();
        (void)m.add_field<calc>("base", &calc::base);
        (void)m.add_method<&calc::add>("add");
        (void)m.add_method<&calc::scale>("scale");
        calc c{10};

        t_check("参数类型不匹配拒绝", !m.try_invoke<int>(c, "add", 1.5, 2.5).has_value());
        t_check("返回类型不匹配拒绝", !m.try_invoke<void>(c, "scale", 5));
        int r = 0;
        t_check("合法调用仍成功", m.invoke(r, c, "add", 3, 4) && r == 17);

        (void)m.add_type<vec3>();
        (void)m.add_fields<vec3>(std::make_tuple(std::pair{"x", &vec3::x},
                                                 std::pair{"y", &vec3::y},
                                                 std::pair{"z", &vec3::z}));
        vec3 zero_pos{0.0f, 1.0f, 2.0f};
        vec3 zero_neg{-0.0f, 1.0f, 2.0f};
        t_check("+0/-0 字段相等", m.get_compare<vec3>().equal(&zero_pos, &zero_neg));
        t_check("+0/-0 哈希一致",
                m.get_hash<vec3>().hash(&zero_pos) == m.get_hash<vec3>().hash(&zero_neg));

        (void)m.add_type<person_component>();
        (void)m.add_field<person_component>("name", &person_component::name);
        person_component src;
        src.name = std::string(64, 'x');
        alignas(person_component) char buf[sizeof(person_component)];
        m.get_compare<person_component>().clone(&src, buf);
        person_component* dst = reinterpret_cast<person_component*>(buf);
        t_check("非平凡字段深拷贝", dst->name == src.name && dst->name.data() != src.name.data());
        dst->~person_component();

        (void)m.add_type<ib_a>();
        (void)m.add_type<ib_b>();
        (void)m.add_type<ib_d>();
        (void)m.add_field<ib_a>("a", &ib_a::a);
        (void)m.add_field<ib_b>("b", &ib_b::b);
        (void)m.add_field<ib_d>("d", &ib_d::d);
        (void)m.add_base<ib_d, ib_a>();
        (void)m.add_base<ib_d, ib_b>();
        ib_d obj{};
        obj.b = 42;
        void* up = m.get_inheritance<ib_d>().upcast(&obj, type_id::get_type_id<ib_b>());
        t_check("第二基类 upcast 一致",
                up == static_cast<ib_b*>(&obj) && static_cast<ib_b*>(up)->b == 42);

        (void)m.add_type<aligned_component>();
        (void)m.add_field<aligned_component>("x", &aligned_component::x);
        void* heap = m.get_construct<aligned_component>().create();
        t_check("过对齐创建", heap != nullptr
                && (reinterpret_cast<uintptr_t>(heap) % alignof(aligned_component)) == 0);
        m.get_construct<aligned_component>().destroy_heap(heap);

        (void)m.add_type<sa_component>();
        (void)m.add_field<sa_component>("xs", &sa_component::xs);
        sa_component s{};
        t_check("数组越界安全", m.get<sa_component>().try_array_get<int>(&s, 0, 99) == nullptr);
        t_check("type_id 名字反查", type_id::get_def_type_id("vec3") == type_id::get_type_id<vec3>());

        (void)m.add_type<opaque_component>();
        t_check("手填偏移注册",
                m.add_field_raw<opaque_component>("hidden",
                                                  offsetof(opaque_component, hidden),
                                                  type_id::get_type_id<int>(),
                                                  static_cast<uint32_t>(sizeof(int)), true));
        t_check("手填偏移可见", m.field<opaque_component>("hidden") != nullptr);
        t_check("手填偏移值正确", m.field<opaque_component>("hidden")->offset
                == offsetof(opaque_component, hidden));
    }

    return t_end("reflection manager ");
}
