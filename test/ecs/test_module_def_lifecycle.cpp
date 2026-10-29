// test_module_def_lifecycle.cpp - 模块功能测试: 非平凡 def 组件生命周期
#include "test_common.hpp"
#include "include/ecs/manager.hpp"
#include "include/part/type/type_id.hpp"
#include "include/serialization/manager.hpp"

#include <new>
#include <string>

struct def_blob
{
    std::string text;
    int value;

    static inline int live_count = 0;

    def_blob() noexcept
        : value(0)
    {
        ++live_count;
    }

    def_blob(std::string text_value, int number)
        : text(std::move(text_value))
        , value(number)
    {
        ++live_count;
    }

    def_blob(const def_blob& other)
        : text(other.text)
        , value(other.value)
    {
        ++live_count;
    }

    def_blob& operator=(const def_blob& other)
    {
        text = other.text;
        value = other.value;
        return *this;
    }

    ~def_blob()
    {
        --live_count;
    }
};

static void def_blob_default_construct(void* p) noexcept
{
    new (p) def_blob();
}

static void def_blob_copy_construct(void* dst, const void* src) noexcept
{
    new (dst) def_blob(*static_cast<const def_blob*>(src));
}

static void def_blob_destroy(void* p) noexcept
{
    static_cast<def_blob*>(p)->~def_blob();
}

int main()
{
    t_begin("test_module_def_lifecycle — 模块功能测试: 非平凡 def 生命周期");

    type_def def{};
    def.size = sizeof(def_blob);
    def.alignment = alignof(def_blob);
    def.trivially_copyable = false;
    def.construct = def_blob_default_construct;
    def.copy_construct = def_blob_copy_construct;
    def.destruct = def_blob_destroy;

    const int did = type_id::register_type_def("test_def_blob_lifecycle", def);
    t_check("非平凡 def 注册成功", did > 0);

    const int live_base = def_blob::live_count;

    t_section(1, "追加与扩容");
    {
        ecs::manager mgr;
        ecs::entity entities[200]{};
        for (int i = 0; i < 200; ++i)
        {
            entities[i] = mgr.create_entity();
            def_blob source(std::string("payload-") + std::to_string(i), i);
            (void)mgr.add_def(entities[i], did, &source);
        }

        bool values_ok = true;
        for (int i = 0; i < 200; ++i)
        {
            auto* value = static_cast<def_blob*>(mgr.get_def_ptr(entities[i], did));
            if (value == nullptr || value->value != i
                || value->text != std::string("payload-") + std::to_string(i))
            {
                values_ok = false;
                break;
            }
        }
        t_check("扩容后全部对象数据完整", values_ok);
    }
    t_check("追加和扩容无泄漏", def_blob::live_count == live_base);

    t_section(2, "覆盖与软删除复用");
    {
        ecs::manager mgr;
        auto first = mgr.create_entity();
        def_blob alpha("alpha", 1);
        (void)mgr.add_def(first, did, &alpha);

        def_blob beta("beta-with-longer-text", 2);
        (void)mgr.add_def(first, did, &beta);
        auto* replaced = static_cast<def_blob*>(mgr.get_def_ptr(first, did));
        t_check("覆盖后值更新", replaced != nullptr && replaced->value == 2
            && replaced->text == "beta-with-longer-text");

        (void)mgr.soft_remove_def(first, did);
        t_check("软删除后不可访问", mgr.get_def_ptr(first, did) == nullptr);

        auto second = mgr.create_entity();
        def_blob reused("reused", 3);
        (void)mgr.add_def(second, did, &reused);
        auto* reused_ptr = static_cast<def_blob*>(mgr.get_def_ptr(second, did));
        t_check("软删除槽复用值正确", reused_ptr != nullptr && reused_ptr->value == 3
            && reused_ptr->text == "reused");
    }
    t_check("覆盖与复用无泄漏", def_blob::live_count == live_base);

    t_section(3, "硬删除 swap-pop 与 compact");
    {
        ecs::manager mgr;
        ecs::entity entities[96]{};
        for (int i = 0; i < 96; ++i)
        {
            entities[i] = mgr.create_entity();
            def_blob source(std::string("item-") + std::to_string(i), i);
            (void)mgr.add_def(entities[i], did, &source);
        }

        (void)mgr.hard_remove_def(entities[10], did);
        auto* moved = static_cast<def_blob*>(mgr.get_def_ptr(entities[95], did));
        t_check("硬删除后尾部对象完整", moved != nullptr && moved->value == 95
            && moved->text == "item-95");

        for (int i = 0; i < 32; ++i)
        {
            (void)mgr.soft_remove_def(entities[i + 20], did);
        }
        auto* set = mgr.get_single_class_set_by_id(did);
        if (set != nullptr)
        {
            (void)set->compact();
        }

        auto* last = static_cast<def_blob*>(mgr.get_def_ptr(entities[95], did));
        t_check("compact 后存活对象完整", last != nullptr && last->value == 95);
        t_check("compact 后墓碑不可访问", mgr.get_def_ptr(entities[20], did) == nullptr);
    }
    t_check("删除与回收无泄漏", def_blob::live_count == live_base);

    t_section(4, "非法 def 被拒绝");
    {
        type_def invalid{};
        invalid.size = sizeof(def_blob);
        invalid.alignment = alignof(def_blob);
        invalid.trivially_copyable = false;
        invalid.destruct = def_blob_destroy;
        t_check("缺少复制构造回调时注册失败",
            type_id::register_type_def("test_def_blob_invalid", invalid) == -1);

        type_def misaligned{};
        misaligned.size = sizeof(int);
        misaligned.alignment = alignof(double);
        misaligned.trivially_copyable = true;
        t_check("元素大小未对齐时注册失败",
            type_id::register_type_def("test_def_misaligned", misaligned) == -1);
    }

    t_section(5, "非平凡 def 禁止原始字节存档");
    {
        ecs::manager mgr;
        auto e = mgr.create_entity();
        def_blob source("serialize-me", 9);
        (void)mgr.add_def(e, did, &source);

        serialize::manager writer(mgr);
        writer.set_checksum(false);
        std::string data;
        const operating_message error = writer.save_world(data);
        t_check("非平凡 def 存档显式失败", !error && data.empty()
            && error.is_code(om_err_unsupported_operation));
    }
    t_check("存档拒绝路径无泄漏", def_blob::live_count == live_base);

    return t_end("test_module_def_lifecycle");
}
