// test_module_signal_queue.cpp - 模块功能测试: signal_queue
#include "test_common.hpp"
#include "include/part/signal_queue.hpp"

struct ev
{
    uint32_t type;
    uint32_t idx;
};

int main()
{
    t_begin("test_module_signal_queue — 模块功能测试: signal_queue");

    t_section(1, "入队与排空");
    {
        signal_queue<ev, 8> q;
        t_check("初始无待处理", !q.has_pending());

        q.push(ev{0, 1});
        q.push(ev{1, 2});
        t_check("push 后 pending", q.has_pending());
        t_check("pending_count == 2", q.pending_count() == 2);

        uint32_t cnt = 0;
        uint32_t sum = 0;
        q.flush([&](const ev& e) {
            ++cnt;
            sum += e.idx;
        });
        t_check("flush 计数与顺序", cnt == 2 && sum == 3);
        t_check("flush 后清空", !q.has_pending());
    }

    t_section(2, "启用开关");
    {
        signal_queue<ev, 8> q;
        q.set_enabled(false);
        t_check("默认可查询 enabled", !q.enabled());
        q.push(ev{0, 1});
        t_check("禁用期间不入队", !q.has_pending());

        q.set_enabled(true);
        t_check("重新启用 enabled", q.enabled());
        q.push(ev{0, 2});
        t_check("启用后入队", q.has_pending());
    }

    t_section(3, "重入守卫");
    {
        signal_queue<ev, 8> q;
        q.push(ev{0, 1});
        uint32_t outer = 0;
        uint32_t inner = 0;
        q.flush([&](const ev&) {
            ++outer;
            q.flush([&](const ev&) { ++inner; });
        });
        t_check("重入 flush 被拦截", outer == 1 && inner == 0);
    }

    t_section(4, "预算与多轮排空");
    {
        signal_queue<ev, 8> q;
        for (uint32_t i = 0; i < 100; ++i)
        {
            q.push(ev{0, i});
        }
        t_check("无界入队 pending_count==100", q.pending_count() == 100);

        uint32_t cnt = 0;
        while (q.has_pending())
        {
            q.flush([&](const ev&) { ++cnt; });
        }
        t_check("多轮 flush 全清", cnt == 100);
    }

    t_section(5, "handler 内追加受预算约束");
    {
        signal_queue<ev, 8> q;
        q.push(ev{0, 0});
        uint32_t processed = 0;
        q.flush([&](const ev&) {
            ++processed;
            if (processed < 64)
            {
                q.push(ev{0, processed});
            }
        });
        t_check("单轮处理受预算约束", processed > 0 && processed <= 8 * 4);
        while (q.has_pending())
        {
            q.flush([](const ev&) {});
        }
        t_check("最终可排空", !q.has_pending());
    }

    t_section(6, "突发超容量零丢事件 + FIFO 顺序");
    {
        signal_queue<ev, 8> q;
        constexpr uint32_t burst = 1000;
        for (uint32_t i = 0; i < burst; ++i)
        {
            q.push(ev{0, i});
        }
        t_check("突发 1000 全保留 (主环 8 + 溢出兜底)", q.pending_count() == burst);

        uint32_t expect = 0;
        bool order_ok = true;
        while (q.has_pending())
        {
            q.flush([&](const ev& e) {
                if (e.idx != expect) { order_ok = false; }
                ++expect;
            });
        }
        t_check("FIFO 顺序 0..999", order_ok && expect == burst);
        t_check("排空后清空", !q.has_pending());
    }

    t_section(7, "flush 期间追加的事件也保留 (溢出路径)");
    {
        signal_queue<ev, 2> q;
        for (uint32_t i = 0; i < 20; ++i)
        {
            q.push(ev{0, i});
        }
        uint32_t consumed = 0;
        q.flush([&](const ev& e) {
            ++consumed;
            if (e.idx == 5)
            {
                for (uint32_t i = 100; i < 110; ++i)
                {
                    q.push(ev{0, i});
                }
            }
        });
        t_check("首轮 flush 受预算约束 (budget=8)", consumed == 8);
        t_check("追加 10 个全保留", q.pending_count() == 22);
        while (q.has_pending())
        {
            q.flush([&](const ev&) { ++consumed; });
        }
        t_check("全部 30 事件最终被消费", consumed == 30);
    }

    return t_end("signal_queue ");
}
