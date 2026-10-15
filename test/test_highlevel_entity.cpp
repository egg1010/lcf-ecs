// test_highlevel_entity.cpp - 高层封装测试: entity / view_tags
#include "test_common.hpp"
#include "include/ecs/entity.hpp"
#include "include/ecs/view_tags.hpp"
#include <type_traits>
#include <unordered_set>

struct tag_a {};
struct tag_b {};

int main()
{
    t_begin("test_highlevel_entity — 高层封装测试: entity / view_tags");

    t_section(1, "entity 基本语义");
    {
        ecs::entity e0;
        t_check("默认构造无效", !e0.is_valid());
        t_check("默认 handle 为 0", e0.parts_.index_ == 0 && e0.parts_.version_ == 0);

        ecs::entity e1(3, 1);
        t_check("(idx,ver) 构造有效", e1.is_valid());
        t_check("index/version 正确", e1.parts_.index_ == 3 && e1.parts_.version_ == 1);

        ecs::entity e2(3, 1);
        t_check("相同实体相等", e1 == e2);

        ecs::entity e3(3, 2);
        t_check("不同版本不等", e1 != e3);

        ecs::entity e4(0, 5);
        t_check("idx=0 ver>0 仍有效", e4.is_valid());
    }

    t_section(2, "entity 容器与哈希");
    {
        std::unordered_set<ecs::entity> s;
        s.insert(ecs::entity(1, 1));
        s.insert(ecs::entity(2, 1));
        s.insert(ecs::entity(1, 1));
        t_check("哈希容器去重", s.size() == 2);
        t_check("find 命中", s.find(ecs::entity(2, 1)) != s.end());
        t_check("find 未命中", s.find(ecs::entity(9, 1)) == s.end());
    }

    t_section(3, "view_tags 标签");
    {
        auto w = ecs::with<tag_a>;
        auto wo = ecs::without<tag_b>;
        auto ow = ecs::owned<tag_a>;
        t_check("with<T> 类型", std::is_same_v<decltype(w), ecs::with_t<tag_a>>);
        t_check("without<T> 类型", std::is_same_v<decltype(wo), ecs::without_t<tag_b>>);
        t_check("owned<T> 类型", std::is_same_v<decltype(ow), ecs::owned_t<tag_a>>);
        t_check("别名 exclude", std::is_same_v<ecs::exclude<tag_a>, ecs::without_t<tag_a>>);
        t_check("别名 get", std::is_same_v<ecs::get<tag_a>, ecs::with_t<tag_a>>);
        t_check("Component 概念成立", ecs::Component<tag_a>);
    }

    return t_end("entity ");
}
