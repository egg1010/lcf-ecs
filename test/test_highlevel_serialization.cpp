// test_highlevel_serialization.cpp - 高层封装测试: 序列化
#include "test_common.hpp"
#include "include/serialization/serialization.hpp"
#include <string>

using serialize::serialization;

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

        serialization s(mgr);
        s.set_checksum_enabled(false);
        std::string json;
        auto r = s.save_to_string<position, health>(json, serialization::format::json);
        t_check("save_to_string 成功", static_cast<bool>(r));
        t_check("json 非空", !json.empty());

        ecs::manager mgr2;
        auto r2 = serialization(mgr2).load_from_string<position, health>(json);
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

        serialization s(mgr);
        s.set_checksum_enabled(false);
        std::string bin;
        auto r = s.save_to_string<position>(bin, serialization::format::binary);
        t_check("二进制 save 成功", static_cast<bool>(r));
        bool magic_ok = bin.size() >= 3 && bin[0] == 'L' && bin[1] == 'C' && bin[2] == 'E';
        t_check("二进制 magic", magic_ok);

        ecs::manager mgr2;
        auto r2 = serialization(mgr2).load_from_string<position>(bin);
        t_check("二进制 load 成功", static_cast<bool>(r2));
        auto* pset = mgr2.get_single_class_set<position>();
        t_check("二进制 position 数量 == 1", pset != nullptr && pset->size() == 1);
    }

    t_section(3, "校验和");
    {
        ecs::manager mgr;
        auto e1 = mgr.create_entity();
        mgr.add(e1, position{1.0f, 1.0f, 1.0f});

        serialization s(mgr);
        s.set_checksum_enabled(true);
        std::string json;
        auto r = s.save_to_string<position>(json, serialization::format::json);
        t_check("带校验和 save", static_cast<bool>(r));

        ecs::manager mgr2;
        serialization s2(mgr2);
        s2.set_checksum_enabled(true);
        auto r2 = s2.load_from_string<position>(json);
        t_check("带校验和 load", static_cast<bool>(r2));
    }

    t_section(4, "空管理器");
    {
        ecs::manager mgr;
        serialization s(mgr);
        s.set_checksum_enabled(false);
        std::string json;
        auto r = s.save_to_string<position>(json, serialization::format::json);
        t_check("空 manager save", static_cast<bool>(r));

        ecs::manager mgr2;
        auto r2 = serialization(mgr2).load_from_string<position>(json);
        t_check("空 manager load", static_cast<bool>(r2));
    }

    t_section(5, "格式自动检测");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        mgr.add(e, position{1.0f, 2.0f, 3.0f});
        serialization s(mgr);
        s.set_checksum_enabled(false);

        std::string json;
        s.save_to_string<position>(json, serialization::format::json);
        std::string bin;
        s.save_to_string<position>(bin, serialization::format::binary);

        ecs::manager m1;
        ecs::manager m2;
        auto rj = serialization(m1).load_from_string<position>(json);
        auto rb = serialization(m2).load_from_string<position>(bin);
        t_check("JSON 自动检测", static_cast<bool>(rj));
        t_check("二进制自动检测", static_cast<bool>(rb));
    }

    return t_end("serialization ");
}
