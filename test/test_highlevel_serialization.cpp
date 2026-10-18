// test_highlevel_serialization.cpp - 高层封装测试: 序列化
#include "test_common.hpp"
#include "include/serialization/serialization.hpp"
#include "include/reflection/macros.hpp"
#include "include/reflection/reflection.hpp"
#include <string>
#include <vector>

using serialize::full_archive;
using serialize::archive_format;

struct position
{
    float x;
    float y;
    float z;
};

struct health
{
    int hp;
};

struct probe_component
{
    float scale;
    int id;
};

struct json_payload_component
{
    int code;
    std::string label;

    std::string to_json() const
    {
        json_writer w;
        w.begin_object();
        w.key("code").value(code);
        w.key("label").value(label);
        w.end_object();
        return w.take();
    }

    void from_json(std::string_view s)
    {
        json_reader r(s);
        if (!r.enter_object())
        {
            return;
        }
        std::string_view k;
        while (!(k = r.next_key()).empty())
        {
            if (k == "code")
            {
                code = r.read_int32();
            }
            else if (k == "label")
            {
                label = r.read_string();
            }
            else
            {
                r.skip_value();
            }
        }
    }
};

REGISTER_TYPE_ONLY(probe_component);
REGISTER_MEMBERS(probe_component, scale, id);

struct item_record
{
    int         id;
    float       weight;
    std::string name;
};

REGISTER_TYPE_ONLY(item_record);
REGISTER_MEMBERS(item_record, id, weight, name);

struct arr_record
{
    int xs[2];
    int other;
};

REGISTER_TYPE_ONLY(arr_record);
REGISTER_MEMBERS(arr_record, xs, other);

struct vec_record
{
    int                tag;
    std::vector<int>   xs;
};

REGISTER_TYPE_ONLY(vec_record);
REGISTER_MEMBERS(vec_record, tag, xs);

int main()
{
    t_begin("test_highlevel_serialization — 高层封装测试: 序列化");

    t_section(1, "JSON 往返");
    {
        ecs::manager mgr;
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        mgr.add(e1, position{1.0f, 2.0f, 3.0f});
        mgr.add(e2, position{4.0f, 5.0f, 6.0f});
        mgr.add(e1, health{100});

        full_archive fa(mgr);
        fa.register_components<position, health>();
        fa.set_checksum_enabled(false);
        std::string json;
        auto r = fa.save_to_string(json, archive_format::json);
        t_check("save_to_string 成功", static_cast<bool>(r));
        t_check("json 非空", !json.empty());

        ecs::manager mgr2;
        full_archive fb(mgr2);
        fb.register_components<position, health>();
        auto r2 = fb.load_from_string(json);
        t_check("load_from_string 成功", static_cast<bool>(r2));

        auto* pset = mgr2.get_single_class_set<position>();
        auto* hset = mgr2.get_single_class_set<health>();
        t_check("position 数量 == 2", pset != nullptr && pset->size() == 2);
        t_check("health 数量 == 1", hset != nullptr && hset->size() == 1);
    }

    t_section(2, "二进制往返");
    {
        ecs::manager mgr;
        auto e1 = mgr.create_entity();
        mgr.add(e1, position{7.0f, 8.0f, 9.0f});

        full_archive fa(mgr);
        fa.register_components<position>();
        fa.set_checksum_enabled(false);
        std::string bin;
        auto r = fa.save_to_string(bin, archive_format::binary);
        t_check("二进制 save 成功", static_cast<bool>(r));
        bool magic_ok = bin.size() >= 3 && bin[0] == 'L' && bin[1] == 'C' && bin[2] == 'E';
        t_check("二进制 magic", magic_ok);

        ecs::manager mgr2;
        full_archive fb(mgr2);
        fb.register_components<position>();
        auto r2 = fb.load_from_string(bin);
        t_check("二进制 load 成功", static_cast<bool>(r2));
        auto* pset = mgr2.get_single_class_set<position>();
        t_check("二进制 position 数量 == 1", pset != nullptr && pset->size() == 1);
    }

    t_section(3, "校验和");
    {
        ecs::manager mgr;
        auto e1 = mgr.create_entity();
        mgr.add(e1, position{1.0f, 1.0f, 1.0f});

        full_archive fa(mgr);
        fa.register_components<position>();
        fa.set_checksum_enabled(true);
        std::string json;
        auto r = fa.save_to_string(json, archive_format::json);
        t_check("带校验和 save", static_cast<bool>(r));
        bool has_prefix = json.size() >= 4 && json.compare(0, 4, "LCCS") == 0;
        t_check("带校验和前缀", has_prefix);

        ecs::manager mgr2;
        full_archive fb(mgr2);
        fb.register_components<position>();
        fb.set_checksum_enabled(true);
        auto r2 = fb.load_from_string(json);
        t_check("带校验和 load", static_cast<bool>(r2));
    }

    t_section(4, "空管理器");
    {
        ecs::manager mgr;
        full_archive fa(mgr);
        fa.register_components<position>();
        fa.set_checksum_enabled(false);
        std::string json;
        auto r = fa.save_to_string(json, archive_format::json);
        t_check("空 manager save", static_cast<bool>(r));

        ecs::manager mgr2;
        full_archive fb(mgr2);
        fb.register_components<position>();
        auto r2 = fb.load_from_string(json);
        t_check("空 manager load", static_cast<bool>(r2));
    }

    t_section(5, "格式自动检测");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, position{1.0f, 2.0f, 3.0f});
        full_archive fa(mgr);
        fa.register_components<position>();
        fa.set_checksum_enabled(false);

        std::string json;
        fa.save_to_string(json, archive_format::json);
        std::string bin;
        fa.save_to_string(bin, archive_format::binary);

        ecs::manager m1;
        ecs::manager m2;
        full_archive f1(m1);
        f1.register_components<position>();
        full_archive f2(m2);
        f2.register_components<position>();
        auto rj = f1.load_from_string(json);
        auto rb = f2.load_from_string(bin);
        t_check("JSON 自动检测", static_cast<bool>(rj));
        t_check("二进制自动检测", static_cast<bool>(rb));
    }

    t_section(6, "Protobuf / FlatBuffer 类型化往返");
    {
        serialize::register_type_name<probe_component>("probe_component");
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, probe_component{1.5f, 7});

        full_archive fa(mgr);
        fa.register_components<probe_component>();
        fa.set_checksum_enabled(false);

        std::string pb;
        auto rpb = fa.save_to_string(pb, archive_format::protobuf);
        t_check("protobuf save 成功", static_cast<bool>(rpb));

        ecs::manager m1;
        full_archive f1(m1);
        f1.register_components<probe_component>();
        auto lpb = f1.load_from_string(pb);
        bool pb_ok = static_cast<bool>(lpb);
        if (pb_ok)
        {
            auto* set = m1.get_single_class_set<probe_component>();
            const auto* pool = set ? set->get_typed_pool_ptr<probe_component>() : nullptr;
            pb_ok = pool != nullptr && set->size() == 1
                    && (*pool)[0].scale == 1.5f && (*pool)[0].id == 7;
        }
        t_check("protobuf 类型化字段还原", pb_ok);

        std::string fb;
        auto rfb = fa.save_to_string(fb, archive_format::flatbuffer);
        t_check("flatbuffer save 成功", static_cast<bool>(rfb));

        ecs::manager m2;
        full_archive f2(m2);
        f2.register_components<probe_component>();
        auto lfb = f2.load_from_string(fb);
        bool fb_ok = static_cast<bool>(lfb);
        if (fb_ok)
        {
            auto* set = m2.get_single_class_set<probe_component>();
            const auto* pool = set ? set->get_typed_pool_ptr<probe_component>() : nullptr;
            fb_ok = pool != nullptr && set->size() == 1
                    && (*pool)[0].scale == 1.5f && (*pool)[0].id == 7;
        }
        t_check("flatbuffer 类型化字段还原", fb_ok);
    }

    t_section(7, "跨语言 schema 导出");
    {
        std::string proto = serialize::schema_export::to_proto<probe_component>();
        std::string fbs = serialize::schema_export::to_fbs<probe_component>();
        t_check("proto 含 syntax 声明", proto.find("syntax = \"proto3\";") != std::string::npos);
        t_check("proto 含组件条目消息", proto.find("message probe_componentEntry") != std::string::npos);
        t_check("proto 含根字段编号", proto.find("string cv = 4;") != std::string::npos);
        t_check("fbs 含 file_identifier", fbs.find("file_identifier \"LCFB\";") != std::string::npos);
        t_check("fbs 含 root_type", fbs.find("root_type Archive;") != std::string::npos);
    }

    t_section(8, "完整 ECS 存档门面 (免 Ts...)");
    {
        auto seed = [](ecs::manager& m) {
            auto e1 = m.create_entity();
            m.add(e1, probe_component{2.5f, 21});
            m.add(e1, position{1.0f, 2.0f, 3.0f});
            m.get_entity_state(e1.parts_.index_).tag = 5;
            auto e2 = m.create_entity();
            m.add(e2, position{4.0f, 5.0f, 6.0f});
            (void)m.create_entity();
            auto e4 = m.create_entity();
            m.add(e4, probe_component{8.0f, 88});
            m.delete_entity(e4);
        };

        auto verify = [](ecs::manager& m) {
            const ecs::entity_manager& em = m.get_entity_manager();
            bool index_ok = em.is_alive(1) && em.is_alive(2) && em.is_alive(3)
                            && !em.is_alive(4);
            auto* pset = m.get_single_class_set<position>();
            auto* qset = m.get_single_class_set<probe_component>();
            bool comp_ok = false;
            if (qset != nullptr && qset->size() == 1) {
                const auto* pool = qset->get_typed_pool_ptr<probe_component>();
                comp_ok = pool != nullptr && (*pool)[0].scale == 2.5f && (*pool)[0].id == 21;
            }
            return index_ok && pset != nullptr && pset->size() == 2 && comp_ok
                   && m.get_entity_state(1).tag == 5;
        };

        const char* names[] = {"json", "binary", "protobuf", "flatbuffer"};
        archive_format fmts[] = {
            archive_format::json,
            archive_format::binary,
            archive_format::protobuf,
            archive_format::flatbuffer,
        };
        for (int i = 0; i < 4; ++i) {
            ecs::manager mgr;
            seed(mgr);
            full_archive fa(mgr);
            fa.register_components<probe_component, position, health>();
            fa.set_checksum_enabled(false);
            std::string data;
            auto rs = fa.save_to_string(data, fmts[i]);
            t_check((std::string(names[i]) + " full_archive save").c_str(), static_cast<bool>(rs));

            ecs::manager m2;
            full_archive fb(m2);
            fb.register_components<probe_component, position, health>();
            auto rl = fb.load_from_string(data);
            t_check((std::string(names[i]) + " full_archive load").c_str(), static_cast<bool>(rl));
            if (rl) {
                t_check((std::string(names[i]) + " 全量状态还原").c_str(), verify(m2));
            }
        }

        ecs::manager mgr;
        full_archive fa(mgr);
        fa.register_components<probe_component, position, health>();
        std::string sp = fa.schema_proto();
        std::string sf = fa.schema_fbs();
        t_check("运行期 proto 含组件条目消息",
                sp.find("message probe_componentEntry") != std::string::npos);
        t_check("运行期 fbs 含组件向量",
                sf.find("probe_component:[probe_componentEntry];") != std::string::npos);
    }

    t_section(9, "JSON 官方格式");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, probe_component{1.5f, 11});
        mgr.add(e, json_payload_component{3, "abc"});

        full_archive fa(mgr);
        fa.register_components<probe_component, json_payload_component>();
        fa.set_checksum_enabled(false);
        fa.set_metadata("k", "v");
        std::string data;
        auto r = fa.save_to_string(data, archive_format::json);
        t_check("json save 成功", static_cast<bool>(r));
        t_check("meta 为嵌套对象", data.find("\"meta\":{") != std::string::npos);
        t_check("反射载荷为嵌套对象", data.find("\"d\":{\"scale\":") != std::string::npos);
        t_check("to_json 载荷为嵌套对象", data.find("\"d\":{\"code\":") != std::string::npos);

        ecs::manager m2;
        full_archive fb(m2);
        fb.register_components<probe_component, json_payload_component>();
        auto r2 = fb.load_from_string(data);
        t_check("json load 成功", static_cast<bool>(r2));
        bool ok = false;
        if (r2)
        {
            auto* set = m2.get_single_class_set<json_payload_component>();
            if (set != nullptr && set->size() == 1)
            {
                const auto* pool = set->get_typed_pool_ptr<json_payload_component>();
                ok = pool != nullptr && (*pool)[0].code == 3 && (*pool)[0].label == "abc";
            }
        }
        t_check("to_json 组件往返", ok);
    }

    t_section(10, "通用对象序列化 (与 ECS 解耦)");
    {
        serialize::object_archive oa;
        oa.bridge.enable_checksum = false;

        item_record one{7, 1.5f, "alpha"};
        std::string js;
        auto rs = oa.save_object(js, archive_format::json, one);
        t_check("对象 save 成功", static_cast<bool>(rs));
        t_check("对象 JSON 为原生对象", js.find("\"items\":[{") != std::string::npos);

        item_record back{};
        auto rl = oa.load_object(js, back);
        t_check("对象 load 成功", static_cast<bool>(rl));
        t_check("对象字段还原", back.id == 7 && back.weight == 1.5f && back.name == "alpha");

        dense<item_record> src;
        for (int i = 0; i < 5; ++i) {
            src.push_back(item_record{i, static_cast<float>(i) * 0.5f, std::string("n") + std::to_string(i)});
        }

        const char* names[] = {"json", "binary", "protobuf", "flatbuffer"};
        archive_format fmts[] = {
            archive_format::json,
            archive_format::binary,
            archive_format::protobuf,
            archive_format::flatbuffer,
        };
        for (int k = 0; k < 4; ++k) {
            std::string data;
            auto r1 = oa.save_array(data, fmts[k], src.data(), src.size());
            dense<item_record> dst;
            auto r2 = oa.load_array(data, dst);
            bool ok = static_cast<bool>(r1) && static_cast<bool>(r2) && dst.size() == src.size();
            for (size_t i = 0; ok && i < src.size(); ++i) {
                ok = dst[i].id == src[i].id && dst[i].weight == src[i].weight
                     && dst[i].name == src[i].name;
            }
            t_check((std::string(names[k]) + " 批量对象往返").c_str(), ok);
        }

        position p{1.0f, 2.0f, 3.0f};
        std::string ps;
        auto rps = oa.save_object(ps, archive_format::json, p);
        position p2{};
        auto rpl = oa.load_object(ps, p2);
        t_check("平凡对象往返", static_cast<bool>(rps) && static_cast<bool>(rpl)
                               && p2.x == 1.0f && p2.y == 2.0f && p2.z == 3.0f);
    }

    t_section(11, "数组字段与不支持类型");
    {
        serialize::object_archive oa;
        oa.bridge.enable_checksum = false;

        arr_record a{};
        a.xs[0] = 1;
        a.xs[1] = 2;
        a.other = 7;
        std::string s;
        auto rs = oa.save_object(s, archive_format::json, a);
        arr_record b{};
        auto rl = oa.load_object(s, b);
        t_check("数组字段往返", static_cast<bool>(rs) && static_cast<bool>(rl)
                && b.xs[0] == 1 && b.xs[1] == 2 && b.other == 7);

        std::string over = R"({"v":1,"items":[{"xs":[1,2,3],"other":9}]})";
        arr_record c{};
        auto rc = oa.load_object(over, c);
        t_check("数组超容量截断且后续字段同步", static_cast<bool>(rc)
                && c.xs[0] == 1 && c.xs[1] == 2 && c.other == 9);

        vec_record v{};
        v.tag = 5;
        v.xs = {1, 2, 3};
        std::string vs;
        auto rv = oa.save_object(vs, archive_format::json, v);
        t_check("不支持字段类型显式报错", !rv && vs.empty());
    }

    return t_end("serialization ");
}
