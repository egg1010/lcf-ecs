// test_module_id_table.cpp - 模块功能测试: 稠密 id 表
#include "test_common.hpp"
#include "include/part/id_table.hpp"

#include <cstdint>

namespace
{
struct entry
{
    int id{0};
    int value{0};
};
} // namespace

int main()
{
    t_begin("test_module_id_table — 模块功能测试: id_table");

    t_section(1, "插入与查找");
    {
        id_table<entry> table;
        t_check("初始为空", table.empty() && table.size() == 0);
        t_check("插入返回新建", table.ensure(3, entry{3, 30}));
        const entry* found = table.find(3);
        t_check("按 id 找到", found != nullptr && found->value == 30);
        t_check("未注册 id 为空", table.find(4) == nullptr);
        t_check("负 id 拒绝", !table.ensure(-1, entry{-1, 0}));
        t_check("负 id 查找为空", table.find(-1) == nullptr);
    }

    t_section(2, "更新");
    {
        id_table<entry> table;
        (void)table.ensure(1, entry{1, 10});
        t_check("重复 id 返回更新", !table.ensure(1, entry{1, 11}));
        t_check("值已更新", table.find(1)->value == 11);
        t_check("条目数不变", table.size() == 1);
    }

    t_section(3, "稀疏 id");
    {
        id_table<entry> table;
        (void)table.ensure(1000, entry{1000, 1});
        t_check("大 id 可插入", table.find(1000) != nullptr);
        t_check("小 id 仍为空", table.find(999) == nullptr);
        t_check("索引映射正确", table.index_of(1000) == 0);
    }

    t_section(4, "遍历");
    {
        id_table<entry> table;
        (void)table.ensure(2, entry{2, 20});
        (void)table.ensure(5, entry{5, 50});
        int sum = 0;
        int count = 0;
        table.for_each([&sum, &count](int id, const entry& e) noexcept
        {
            ++count;
            sum += id + e.value;
        });
        t_check("遍历条目数", count == 2);
        t_check("遍历内容正确", sum == 2 + 20 + 5 + 50);
        t_check("按索引取 id", table.id_at(0) == 2 && table.id_at(1) == 5);
        t_check("按索引取值", table.at(1).value == 50);
    }

    t_section(5, "清空");
    {
        id_table<entry> table;
        (void)table.ensure(7, entry{7, 70});
        table.clear();
        t_check("清空后为空", table.empty());
        t_check("清空后查找为空", table.find(7) == nullptr);
        t_check("清空后可重新插入", table.ensure(7, entry{7, 71}));
    }

    return t_end("test_module_id_table");
}
