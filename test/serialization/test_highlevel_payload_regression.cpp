#include "test_common.hpp"
#include "include/serialization/manager.hpp"
#include "include/reflection/manager.hpp"
#include <cstdint>
#include <string>

namespace
{
struct payload_component
{
    int32_t id;
    int32_t values[2];
    std::string text;
};

struct invalid_json_component
{
    std::string to_json() const { return "{bad}"; }
    void from_json(std::string_view) {}
};

void register_reflection()
{
    auto& reflection = reflect::global();
    reflection.register_type_only<payload_component>("payload_regression_component");
    reflection.register_member_runtime<payload_component>(
        "payload_regression_component", "id", &payload_component::id);
    reflection.register_member_runtime<payload_component>(
        "payload_regression_component", "values", &payload_component::values);
    reflection.register_member_runtime<payload_component>(
        "payload_regression_component", "text", &payload_component::text);
}

std::string make_world_archive()
{
    ecs::manager source;
    const auto first = source.create_entity();
    const auto second = source.create_entity();
    (void)source.add(first, payload_component{10, {1, 2}, "first"});
    (void)source.add(second, payload_component{20, {3, 4}, "second"});
    serialize::manager writer(source);
    writer.world()->register_components<payload_component>();
    writer.set_checksum(false).set_format(serialize::archive_format::json);
    std::string data;
    return writer.save_world(data) ? data : std::string{};
}
}

int main()
{
    t_begin("test_payload_regression — 字段与失败语义回归");
    register_reflection();

    serialize::manager objects;
    objects.set_checksum(false).set_format(serialize::archive_format::json);

    const std::string reordered =
        R"({"v":1,"items":[{"unknown":99,"text":"ok","values":[5,6],"id":7}]})";
    payload_component value{};
    t_check("字段重排和未知字段", objects.load_object(reordered, value)
            && value.id == 7 && value.values[0] == 5 && value.values[1] == 6
            && value.text == "ok");

    const std::string missing =
        R"({"v":1,"items":[{"id":8,"text":"missing"}]})";
    value = payload_component{1, {9, 9}, "old"};
    t_check("缺省数组字段保留默认值", objects.load_object(missing, value)
            && value.id == 8 && value.values[0] == 0 && value.values[1] == 0
            && value.text == "missing");

    t_check("字段编号匹配支持多位数",
            serialize::field_codec::field_number_matches("f1", 1)
            && serialize::field_codec::field_number_matches("f12", 12)
            && serialize::field_codec::field_number_matches("f120", 120)
            && !serialize::field_codec::field_number_matches("f01", 1)
            && !serialize::field_codec::field_number_matches("f123", 12));

    const char* invalid_json[] = {
        "{'v':1,'items':[{'id':1,'values':[1,2],'text':'x'}]}",
        R"({"v":1,//comment
           "items":[]})",
        R"({"v":+1,"items":[]})",
        R"({"v":01,"items":[]})",
        R"({"v":1,"items":[]} trailing)",
    };
    for (size_t i = 0; i < sizeof(invalid_json) / sizeof(invalid_json[0]); ++i)
    {
        t_check("严格 JSON 拒绝非标准输入", !objects.load_object(invalid_json[i], value));
    }

    std::string null_output;
    const operating_message null_result = objects.save_array(
        null_output, static_cast<const payload_component*>(nullptr), 1);
    t_check("空数组指针显式失败", !null_result
            && null_result.is_code(om_err_invalid_argument));

    invalid_json_component invalid_custom;
    std::string invalid_custom_output;
    t_check("自定义 to_json 非法片段被拒绝",
            !objects.save_object(invalid_custom_output, invalid_custom)
            && invalid_custom_output.empty());
    t_check("自定义 from_json 非法片段被拒绝",
            !objects.load_object(std::string_view(R"({"v":1,"items":[{bad}]})"),
                                 invalid_custom));

    dense<payload_component> output;
    output.emplace_back(payload_component{42, {4, 2}, "preserve"});
    t_check("load_array 失败保留原输出", !objects.load_array("{bad}", output)
            && output.size() == 1 && output[0].id == 42 && output[0].text == "preserve");

    const std::string valid_world = make_world_archive();
    t_check("失败语义测试存档成功", !valid_world.empty());
    const std::string needle = "\"id\":";
    const size_t first = valid_world.find(needle);
    const size_t second = valid_world.find(needle, first + needle.size());
    std::string corrupted = valid_world;
    const size_t begin = second + needle.size();
    const size_t end = corrupted.find_first_of(",}", begin);
    corrupted.replace(begin, end - begin, "\"bad\"");

    ecs::manager effort_world;
    serialize::manager effort_reader(effort_world);
    effort_reader.world()->register_components<payload_component>();
    effort_reader.world()->set_load_policy(serialize::load_policy::best_effort);
    const operating_message effort_result = effort_reader.load_world(corrupted);
    t_check("best_effort 跳过损坏组件并继续", effort_result
            && effort_world.component_count<payload_component>() == 1
            && effort_reader.last_stats()->skipped_count == 1);

    ecs::manager strict_world;
    serialize::manager strict_reader(strict_world);
    strict_reader.world()->register_components<payload_component>();
    t_check("strict 遇到损坏组件失败", !strict_reader.load_world(corrupted));

    type_def definition{};
    definition.size = sizeof(uint32_t);
    definition.alignment = alignof(uint32_t);
    definition.trivially_copyable = true;
    const int def_id = type_id::register_type_def("payload_regression_def", definition);
    ecs::manager def_source;
    const auto def_entity = def_source.create_entity();
    const uint32_t def_value = 123;
    (void)def_source.add_def(def_entity, def_id, &def_value);
    serialize::manager def_writer(def_source);
    def_writer.set_checksum(false).set_format(serialize::archive_format::json);
    std::string def_data;
    t_check("def 存档成功", def_writer.save_world(def_data));
    const size_t payload_pos = def_data.rfind("\"d\":\"");
    t_check("定位 def 载荷", payload_pos != std::string::npos);
    if (payload_pos != std::string::npos)
    {
        const size_t data_begin = payload_pos + 5;
        const size_t data_end = def_data.find('"', data_begin);
        def_data.replace(data_begin, data_end - data_begin, "AA==");
    }
    ecs::manager def_target;
    serialize::manager def_reader(def_target);
    t_check("strict 拒绝 def 错误尺寸", !def_reader.load_world(def_data));

    ecs::manager def_effort_target;
    serialize::manager def_effort_reader(def_effort_target);
    def_effort_reader.world()->set_load_policy(serialize::load_policy::best_effort);
    t_check("best_effort 跳过 def 错误尺寸", def_effort_reader.load_world(def_data)
            && def_effort_reader.last_stats()->skipped_count == 1
            && def_effort_target.get_def_ptr(def_entity, def_id) == nullptr);

    return t_end("payload regression ");
}
