// 测试迭代期安全删除
#include "../include/component.hpp"
#include <cstdio>

struct Health
{
    int hp;
    int id;
};

int main()
{
    ecs::manager mgr;

    // 创建 10 个实体, 各加 Health 组件
    ecs::entity ents[10];
    for (int i = 0; i < 10; ++i)
    {
        ents[i] = mgr.create_entity();
        mgr.add<Health>(ents[i], Health{100 + i, i});
    }

    // 测试 for_each_safe 迭代期 hard_remove
    printf("=== 测试 for_each_safe ===\n");
    int visit_count = 0;
    mgr.view<Health>().for_each_safe([&](ecs::entity e, Health& h) {
        ++visit_count;
        // 注意: 先记录 id/hp, hard_remove 后 h 引用可能已失效 (swap_pop)
        int visited_id = h.id;
        int visited_hp = h.hp;
        printf("visit id=%d hp=%d\n", visited_id, visited_hp);
        // 删除 hp 为偶数的实体 (测试 swap_pop 兜底)
        if (visited_hp % 2 == 0)
        {
            mgr.hard_remove<Health>(e);
            printf("  -> removed id=%d\n", visited_id);
        }
    });
    printf("total visited: %d (expect 10)\n", visit_count);
    printf("remaining: %zu (expect 5)\n", mgr.view<Health>().size());

    return 0;
}
