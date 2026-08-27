// test_type_def_set.cpp - single_class_set def 池 (运行期注册类型) 功能测试
// 覆盖: 添加/查询/覆盖/移除/软删/级联删除/非平凡生命周期/掩码查询/混合模板/对齐/回收
#include "test_common.hpp"

#include <cstring>

// 非平凡 def 的析构计数 (全局)
static int g_dtor_calls = 0;
static void counting_destruct(void*) noexcept { ++g_dtor_calls; }
static void noop_construct(void*) noexcept {}

// 对齐 32 的数据载荷
struct alignas(32) WidePayload
{
    float v[6];
};

static type_def make_trivial_def(size_t size, size_t alignment)
{
    type_def d;
    d.size = size;
    d.alignment = alignment;
    d.trivially_copyable = true;
    return d;
}

int main()
{
    std::printf("============================================================\n");
    std::printf("  single_class_set def 池 功能测试\n");
    std::printf("============================================================\n");

    // === 1. 基础添加/查询 ===
    print_section(1, "基础添加与查询");
    {
        ecs::manager mgr;
        const int id = type_id::register_type_def("DefPos", make_trivial_def(12, 4));
        print_item("注册 def 类型", id > 0);

        entity e = mgr.create_entity();
        const float src[3] = {1.5f, 2.5f, 3.5f};
        operating_message om = mgr.add_def(e, id, src);
        print_item("add_def 成功", static_cast<bool>(om));

        const float* p = static_cast<const float*>(mgr.get_def_ptr(e, id));
        print_item("get_def_ptr 非空", p != nullptr);
        print_item("数据读回一致", p && p[0] == 1.5f && p[1] == 2.5f && p[2] == 3.5f);

        // 按名重载
        const float* p2 = static_cast<const float*>(mgr.get_def_ptr(e, "DefPos"));
        print_item("按名 get_def_ptr 一致", p2 == p);

        // 原地修改
        float* p3 = static_cast<float*>(mgr.get_def_ptr(e, id));
        p3[0] = 9.0f;
        print_item("修改后读回生效",
                    static_cast<const float*>(mgr.get_def_ptr(e, id))[0] == 9.0f);

        // 未持有实体查询
        entity e2 = mgr.create_entity();
        print_item("未添加实体查询返回 nullptr", mgr.get_def_ptr(e2, id) == nullptr);
    }

    // === 2. 覆盖已存在 (slow path) ===
    print_section(2, "覆盖已存在");
    {
        ecs::manager mgr;
        const int id = type_id::register_type_def("DefOvr", make_trivial_def(8, 8));
        entity e = mgr.create_entity();
        uint64_t v1 = 0x1111111111111111ULL;
        uint64_t v2 = 0x2222222222222222ULL;
        mgr.add_def(e, id, &v1);
        operating_message om = mgr.add_def(e, id, &v2);
        print_item("覆盖添加成功", static_cast<bool>(om));
        print_item("覆盖后数据为新值",
                    *static_cast<const uint64_t*>(mgr.get_def_ptr(e, id)) == v2);
        // 掩码不应重复置位导致尺寸异常: 组件数仍为 1
        const single_class_set* set = mgr.get_single_class_set_by_id(id);
        print_item("覆盖后池内仍为 1 个元素", set && set->size() == 1);
    }

    // === 3. hard_remove / 掩码联动 ===
    print_section(3, "hard_remove 与掩码联动");
    {
        ecs::manager mgr;
        const int id = type_id::register_type_def("DefRm", make_trivial_def(4, 4));
        int data = 42;
        entity e = mgr.create_entity();
        mgr.add_def(e, id, &data);

        // runtime_view 能看到 def 组件
        {
            ecs::runtime_view rv = mgr.runtime_view_create(std::array<int,1>{id});
            print_item("添加后 runtime_view 命中 1 实体", rv.count() == 1);
        }

        operating_message om = mgr.hard_remove_def(e, id);
        print_item("hard_remove_def 成功", static_cast<bool>(om));
        print_item("移除后指针为空", mgr.get_def_ptr(e, id) == nullptr);
        {
            ecs::runtime_view rv = mgr.runtime_view_create(std::array<int,1>{id});
            print_item("移除后 runtime_view 命中 0 实体", rv.count() == 0);
        }
        print_item("按名 hard_remove_def 未注册名返回失败",
                    !static_cast<bool>(mgr.hard_remove_def(e, "NopeRM")));
    }

    // === 4. 软删除 + 复用 ===
    print_section(4, "软删除与死槽复用");
    {
        ecs::manager mgr;
        const int id = type_id::register_type_def("DefSoft", make_trivial_def(4, 4));
        int v1 = 1, v2 = 2, v3 = 3;
        entity e1 = mgr.create_entity();
        entity e2 = mgr.create_entity();
        entity e3 = mgr.create_entity();
        mgr.add_def(e1, id, &v1);
        mgr.add_def(e2, id, &v2);
        mgr.add_def(e3, id, &v3);

        operating_message om = mgr.soft_remove_def(e2, id);
        print_item("soft_remove_def 成功", static_cast<bool>(om));
        print_item("软删后指针为空", mgr.get_def_ptr(e2, id) == nullptr);

        int reused = 99;
        mgr.add_def(e2, id, &reused);
        print_item("复用后数据正确",
                    *static_cast<const int*>(mgr.get_def_ptr(e2, id)) == 99);
        const single_class_set* set = mgr.get_single_class_set_by_id(id);
        print_item("复用未增长物理槽位 (<=3)", set && set->size() <= 3);
    }

    // === 5. delete_entity 级联删除 ===
    print_section(5, "delete_entity 级联删除");
    {
        ecs::manager mgr;
        const int id = type_id::register_type_def("DefDel", make_trivial_def(4, 4));
        int data = 7;
        entity e = mgr.create_entity();
        mgr.add_def(e, id, &data);
        mgr.delete_entity(e);
        print_item("实体删除后 def 池为空",
                    mgr.get_single_class_set_by_id(id)->size() == 0);
        entity e2 = mgr.create_entity();
        print_item("新实体 (版本递增) 查询为空", mgr.get_def_ptr(e2, id) == nullptr);
    }

    // === 6. 非平凡 def (析构计数) ===
    print_section(6, "非平凡 def 生命周期");
    {
        g_dtor_calls = 0;
        type_def d;
        d.size = 8;
        d.alignment = 8;
        d.trivially_copyable = false;
        d.construct = noop_construct;
        d.destruct = counting_destruct;
        const int id = type_id::register_type_def("DefNT", d);

        {
            ecs::manager mgr;
            entity es[4];
            uint64_t vs[4] = {1, 2, 3, 4};
            for (int i = 0; i < 4; ++i)
            {
                es[i] = mgr.create_entity();
                mgr.add_def(es[i], id, &vs[i]);
            }
            print_item("添加期间无析构", g_dtor_calls == 0);

            mgr.hard_remove_def(es[0], id);           // 析构 1 (尾部搬运)
            print_item("hard_remove 析构 1 次", g_dtor_calls == 1);

            mgr.soft_remove_def(es[1], id);           // 软删不析构
            print_item("soft_remove 不析构", g_dtor_calls == 1);

            mgr.clear();                              // 剩余 3 个析构 (含墓碑)
            print_item("clear 析构剩余 3 次", g_dtor_calls == 4);
        }
        // manager 析构: 池已空, 无额外析构
        print_item("manager 析构无额外析构", g_dtor_calls == 4);

        // 二次场景: 直接析构 (未 clear)
        g_dtor_calls = 0;
        {
            ecs::manager mgr;
            entity e = mgr.create_entity();
            uint64_t v = 5;
            mgr.add_def(e, id, &v);
        }
        print_item("manager 直接析构调用 1 次 destruct", g_dtor_calls == 1);
    }

    // === 7. 掩码混合查询 (模板 + def) ===
    print_section(7, "模板与 def 混合");
    {
        ecs::manager mgr;
        const int id = type_id::register_type_def("DefMix", make_trivial_def(4, 4));
        entity e1 = mgr.create_entity();
        entity e2 = mgr.create_entity();
        entity e3 = mgr.create_entity();
        int d1 = 10, d2 = 20;
        mgr.add_def(e1, id, &d1);
        mgr.add_def(e2, id, &d2);
        mgr.add(e3, Position(1, 2, 3));

        // AND: def + 模板
        {
            ecs::runtime_view rv = mgr.runtime_view_create(std::array<int,2>{id, type_id::get_type_id<Position>()});
            print_item("def+模板 AND 查询 0 实体 (无交集)", rv.count() == 0);
        }
        entity e4 = mgr.create_entity();
        mgr.add_def(e4, id, &d1);
        mgr.add(e4, Position(4, 5, 6));
        {
            ecs::runtime_view rv = mgr.runtime_view_create(std::array<int,2>{id, type_id::get_type_id<Position>()});
            print_item("def+模板 AND 查询 1 实体 (e4)", rv.count() == 1);
        }
        // 单 def 查询
        {
            ecs::runtime_view rv = mgr.runtime_view_create(std::array<int,1>{id});
            print_item("def 单查询 3 实体", rv.count() == 3);
            // 遍历所有命中实体
            size_t hits = 0;
            rv.for_each([&](entity) { ++hits; });
            print_item("for_each 遍历 def 命中", hits == 3);
        }
        // 模板路径不受 def 存在影响
        {
            auto view = mgr.view<Position>();
            print_item("模板 view 仍为 2 实体", view.size() == 2);
        }
    }

    // === 8. 大对齐 def (alignas 32) ===
    print_section(8, "大对齐 def (32B)");
    {
        ecs::manager mgr;
        type_def d;
        d.size = sizeof(WidePayload);
        d.alignment = alignof(WidePayload);
        d.trivially_copyable = true;
        const int id = type_id::register_type_def("DefWide", d);

        WidePayload src{};
        for (int i = 0; i < 6; ++i) src.v[i] = float(i) * 1.25f;
        entity e = mgr.create_entity();
        mgr.add_def(e, id, &src);

        const WidePayload* p = static_cast<const WidePayload*>(mgr.get_def_ptr(e, id));
        print_item("指针 32B 对齐", (reinterpret_cast<uintptr_t>(p) % 32) == 0);
        bool same = true;
        for (int i = 0; i < 6; ++i)
        {
            if (p->v[i] != float(i) * 1.25f) same = false;
        }
        print_item("数据一致", same);

        // 多元素扩容后仍对齐且数据保持 (reserve 触发重定位)
        for (int k = 0; k < 200; ++k)
        {
            entity ek = mgr.create_entity();
            WidePayload w{};
            w.v[0] = float(k);
            mgr.add_def(ek, id, &w);
        }
        const WidePayload* p0 = static_cast<const WidePayload*>(mgr.get_def_ptr(e, id));
        print_item("扩容后首元素数据保持", p0->v[1] == 1.25f);
        print_item("扩容后仍 32B 对齐", (reinterpret_cast<uintptr_t>(p0) % 32) == 0);
    }

    // === 9. 回收 (compact, memcpy 重定位路径) ===
    print_section(9, "密度回收");
    {
        ecs::manager mgr;
        const int id = type_id::register_type_def("DefCmp", make_trivial_def(8, 8));
        constexpr int N = 300;
        entity es[N];
        for (int i = 0; i < N; ++i)
        {
            es[i] = mgr.create_entity();
            uint64_t v = uint64_t(i) * 3 + 1;
            mgr.add_def(es[i], id, &v);
        }
        // 软删一半 (触发自动回收阈值)
        for (int i = 0; i < N; i += 2)
        {
            mgr.soft_remove_def(es[i], id);
        }
        // 幸存者数据完好
        bool ok = true;
        for (int i = 1; i < N; i += 2)
        {
            const uint64_t* p = static_cast<const uint64_t*>(mgr.get_def_ptr(es[i], id));
            if (!p || *p != uint64_t(i) * 3 + 1) ok = false;
        }
        print_item("回收后幸存者数据完好", ok);
        {
            ecs::runtime_view rv = mgr.runtime_view_create(std::array<int,1>{id});
            print_item("回收后命中 150 实体", rv.count() == 150);
        }
    }

    // === 10. 错误路径 ===
    print_section(10, "错误路径");
    {
        ecs::manager mgr;
        entity e = mgr.create_entity();
        int data = 1;
        print_item("未知 id 添加失败", !static_cast<bool>(mgr.add_def(e, 99999, &data)));
        print_item("未注册名添加失败", !static_cast<bool>(mgr.add_def(e, "NoSuchDef", &data)));
        print_item("未知 id 查询返回 nullptr", mgr.get_def_ptr(e, 99999) == nullptr);
        print_item("未注册名查询返回 nullptr", mgr.get_def_ptr(e, "NoSuchDef") == nullptr);
        print_item("未知 id 移除失败", !static_cast<bool>(mgr.hard_remove_def(e, 99999)));
        print_item("无效实体添加失败",
                    !static_cast<bool>(mgr.add_def(entity{}, 1, &data)));

        // 模板类型 id 不能用于 def 接口 (语义互斥)
        const int tid = type_id::get_type_id<Position>();
        print_item("模板类型 id 走 def 接口添加失败",
                    !static_cast<bool>(mgr.add_def(e, tid, &data)));
    }

    // === 11. manager 移动语义 ===
    print_section(11, "manager 移动语义");
    {
        ecs::manager mgr;
        const int id = type_id::register_type_def("DefMv", make_trivial_def(4, 4));
        entity e = mgr.create_entity();
        int data = 55;
        mgr.add_def(e, id, &data);

        ecs::manager mgr2(std::move(mgr));
        const int* p = static_cast<const int*>(mgr2.get_def_ptr(e, id));
        print_item("移动后 def 数据可读", p && *p == 55);

        operating_message om = mgr2.hard_remove_def(e, id);
        print_item("移动后可移除", static_cast<bool>(om));
    }

    // === 12. append_preallocated_entities 联动 ===
    print_section(12, "预分配扩容联动");
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(100);
        const int id = type_id::register_type_def("DefPre", make_trivial_def(4, 4));
        entity e = mgr.create_entity();
        int data = 3;
        mgr.add_def(e, id, &data);
        print_item("预分配后添加成功",
                    *static_cast<const int*>(mgr.get_def_ptr(e, id)) == 3);
    }

    print_summary("功能测试");
    return test_stats::g_fail_count == 0 ? 0 : 1;
}
