// 测试补访队列溢出回退到堆缓冲
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

    // 创建 50 个实体, id 从 0~49
    ecs::entity ents[50];
    for (int i = 0; i < 50; ++i)
    {
        ents[i] = mgr.create_entity();
        mgr.add<Health>(ents[i], Health{100 + i, i});
    }

    // 在迭代到 id=40 时, 批量删除 idx=0~29 (30 个已访问区之前的实体)
    //   每次 hard_remove 把 back 移到已访问区, 需补访
    //   30 > INLINE_PENDING_CAP(16), 触发堆溢出
    printf("=== 测试 pending 溢出 ===\n");
    int visit_count = 0;
    bool triggered = false;

    mgr.view<Health>().for_each_safe([&](ecs::entity e, Health& h) {
        ++visit_count;
        if (!triggered && h.id == 40)
        {
            triggered = true;
            // 立即批量删除 id=0~29
            for (int i = 0; i < 30; ++i)
            {
                mgr.hard_remove<Health>(ents[i]);
            }
        }
    });

    printf("total visited: %d (expect >= 50)\n", visit_count);
    printf("remaining: %zu (expect 20)\n", mgr.view<Health>().size());

    // 连续删除可能产生重访, 但保证不漏访
    //   remaining==20 证明 30 个被正确删除
    if (visit_count >= 50 && mgr.view<Health>().size() == 20)
        printf("PASS: 溢出回退正确, 无漏访\n");
    else
        printf("FAIL\n");

    return 0;
}
