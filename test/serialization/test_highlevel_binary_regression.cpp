#include "test_common.hpp"
#include "include/serialization/manager.hpp"
#include "include/reflection/manager.hpp"
#include <cstdint>
#include <string>

namespace
{
struct nested_array_component
{
    int32_t id;
    int32_t values[2];
};

void register_types()
{
    auto& reflection = reflect::global();
    reflection.register_type_only<nested_array_component>("binary_nested_array_component");
    reflection.register_member_runtime<nested_array_component>(
        "binary_nested_array_component", "id", &nested_array_component::id);
    reflection.register_member_runtime<nested_array_component>(
        "binary_nested_array_component", "values", &nested_array_component::values);
}

std::string make_archive(ecs::manager& source)
{
    const auto first = source.create_entity();
    const auto second = source.create_entity();
    (void)source.add(first, nested_array_component{-7, {11, 22}});
    (void)source.add(second, nested_array_component{9, {33, 44}});
    serialize::manager writer(source);
    writer.world()->register_components<nested_array_component>();
    writer.set_checksum(false).set_format(serialize::archive_format::binary);
    std::string data;
    const operating_message result = writer.save_world(data);
    return result ? data : std::string{};
}
}

int main()
{
    t_begin("test_binary_regression — Binary 结构回归");
    register_types();

    ecs::manager source;
    const std::string data = make_archive(source);
    t_check("嵌套数组存档成功", !data.empty());

    binary_codec codec;
    operating_message validation_error;
    t_check("合法输出通过基础校验", codec.validate(data, validation_error));

    ecs::manager target;
    serialize::manager reader(target);
    reader.world()->register_components<nested_array_component>();
    const operating_message loaded = reader.load_world(data);
    const auto* set = target.get_single_class_set<nested_array_component>();
    const auto* pool = set != nullptr ? set->get_typed_pool_ptr<nested_array_component>() : nullptr;
    t_check("嵌套数组完整加载", loaded && target.alive_count() == 2
            && set != nullptr && set->size() == 2 && pool != nullptr
            && (*pool)[0].id == -7 && (*pool)[0].values[0] == 11
            && (*pool)[0].values[1] == 22 && (*pool)[1].id == 9
            && (*pool)[1].values[0] == 33 && (*pool)[1].values[1] == 44);

    std::string future = data;
    future[5] = 99;
    ecs::manager future_target;
    serialize::manager future_reader(future_target);
    future_reader.world()->register_components<nested_array_component>();
    t_check("未知容器版本被拒绝", !future_reader.load_world(future));
    t_check("版本失败后不修改 world", future_target.alive_count() == 0);

    std::string reserved = data;
    reserved[6] = 1;
    t_check("非零保留字节被拒绝", !codec.validate(reserved, validation_error));

    std::string bad_magic = data;
    bad_magic[0] = 'X';
    t_check("非法 magic 被拒绝", !codec.validate(bad_magic, validation_error));
    t_check("截断容器头被拒绝", !codec.validate(std::string_view(data).substr(0, 7),
                                               validation_error));

    return t_end("binary regression ");
}
