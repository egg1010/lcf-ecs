// test_highlevel_reflection_capacity.cpp - 反射动态容量测试
#include "test_common.hpp"
#include "include/reflection/manager.hpp"

#include <cstdio>
#include <cstddef>

// 300 个字段, 超过自动聚合遍历 256 上限
struct wide_type
{
    int words[300];
};

struct method_host
{
    int base = 5;
    int touch(int v) { return v + base; }
};

namespace {

constexpr size_t wide_count = 300;
constexpr size_t method_count = 300;
constexpr size_t field_slots = wide_count + 64;

char field_names[field_slots][16];
char method_names[method_count][16];

void fill_names() noexcept
{
    for (size_t i = 0; i < field_slots; ++i)
    {
        std::snprintf(field_names[i], sizeof(field_names[i]), "f_%u",
                      static_cast<unsigned>(i));
    }
    for (size_t i = 0; i < method_count; ++i)
    {
        std::snprintf(method_names[i], sizeof(method_names[i]), "m_%u",
                      static_cast<unsigned>(i));
    }
}

} // namespace

int main()
{
    t_begin("test_highlevel_reflection_capacity — 反射动态容量");

    fill_names();

    reflect::manager m;

    t_section(1, "超过 256 字段的显式注册");
    {
        t_check("add_type 成功", m.add_type<wide_type>());
        t_check("初始字段数为 0", m.get<wide_type>().field_count() == 0);

        const int field_tid = type_id::get_type_id<int>();
        bool all_ok = true;
        for (size_t i = 0; i < wide_count; ++i)
        {
            if (!m.add_field_raw<wide_type>(field_names[i],
                                            i * sizeof(int), field_tid,
                                            static_cast<uint32_t>(sizeof(int)), true))
            {
                all_ok = false;
                break;
            }
        }
        t_check("300 个字段全部注册", all_ok);
        t_check("字段数 == 300", m.get<wide_type>().field_count() == wide_count);

        const reflect::field_meta* last = m.field<wide_type>(field_names[wide_count - 1]);
        t_check("末字段可查", last != nullptr);
        t_check("末字段偏移正确", last != nullptr
                && last->offset == (wide_count - 1) * sizeof(int));
        t_check("第 200 字段可查", m.field<wide_type>(field_names[200]) != nullptr);
    }

    t_section(2, "字段扩容后指针稳定");
    {
        const reflect::field_meta* first = m.field<wide_type>(field_names[0]);
        t_check("首字段已存在", first != nullptr);
        t_check("首字段偏移为 0", first != nullptr && first->offset == 0);

        bool more_ok = true;
        for (size_t i = wide_count; i < wide_count + 64; ++i)
        {
            char* name = field_names[i];
            std::snprintf(name, sizeof(field_names[i]), "f_%u", static_cast<unsigned>(i));
            if (!m.add_field_raw<wide_type>(name, i * sizeof(int),
                                            type_id::get_type_id<int>(),
                                            static_cast<uint32_t>(sizeof(int)), true))
            {
                more_ok = false;
                break;
            }
        }
        t_check("继续追加 64 个字段", more_ok);
        t_check("字段数 == 364", m.get<wide_type>().field_count() == wide_count + 64);
        t_check("首字段指针不变", m.field<wide_type>(field_names[0]) == first);
        t_check("首字段名不变", first != nullptr && first->name == field_names[0]);
    }

    t_section(3, "字段查询与删除");
    {
        wide_type obj{};
        auto q = m.get<wide_type>();
        q.get_by_name<int>(&obj, field_names[123]) = 77;
        t_check("按名写入", obj.words[123] == 77);
        t_check("按名读取", q.get_by_name<int>(&obj, field_names[123]) == 77);
        t_check("按索引读取", q.field(299).offset == 299 * sizeof(int));

        t_check("删除末字段", m.remove_field<wide_type>(field_names[363]));
        t_check("字段数减一", m.get<wide_type>().field_count() == wide_count + 63);
        t_check("删除后不可查", m.field<wide_type>(field_names[363]) == nullptr);
        t_check("重新注册", m.add_field_raw<wide_type>(field_names[363], 363 * sizeof(int),
                                                       type_id::get_type_id<int>(),
                                                       static_cast<uint32_t>(sizeof(int)), true));
        t_check("字段数恢复", m.get<wide_type>().field_count() == wide_count + 64);
    }

    t_section(4, "超过 256 个方法的注册");
    {
        t_check("add_type 成功", m.add_type<method_host>());
        bool all_ok = true;
        for (size_t i = 0; i < method_count; ++i)
        {
            if (!m.add_method(method_names[i], &method_host::touch))
            {
                all_ok = false;
                break;
            }
        }
        t_check("300 个方法全部注册", all_ok);
        t_check("方法数 == 300", m.get<method_host>().method_count() == method_count);
        t_check("末方法可查", m.method<method_host>(method_names[method_count - 1]) != nullptr);
        t_check("第 200 方法可查", m.method<method_host>(method_names[200]) != nullptr);

        method_host host;
        int result = 0;
        t_check("超限后仍可调用", m.invoke(result, host, method_names[method_count - 1], 3)
                && result == 8);

        t_check("删除末方法", m.remove_method(method_names[method_count - 1], &method_host::touch));
        t_check("方法数减一", m.get<method_host>().method_count() == method_count - 1);
    }

    t_section(5, "自动聚合遍历边界内仍可用");
    {
        struct small
        {
            int a;
            int b;
        };
        t_check("自动遍历注册", m.add_aggregate_type<small>());
        t_check("字段数 == 2", m.get<small>().field_count() == 2);
        t_check("自动字段名", m.field<small>("field_0") != nullptr
                && m.field<small>("field_1") != nullptr);
    }

    return t_end("reflection capacity ");
}
