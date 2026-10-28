// test_module_ring_buffer.cpp - 模块功能测试: ring_buffer
#include "test_common.hpp"
#include "include/part/ring_buffer.hpp"
#include <string>
#include <utility>

struct move_only
{
    int v;
    explicit move_only(int x) noexcept : v(x) {}
    move_only(move_only&& o) noexcept : v(o.v) { o.v = -1; }
    move_only& operator=(move_only&& o) noexcept { v = o.v; o.v = -1; return *this; }
    move_only(const move_only&) = delete;
    move_only& operator=(const move_only&) = delete;
};

int main()
{
    t_begin("test_module_ring_buffer — 模块功能测试: ring_buffer");

    t_section(1, "try_push / front / pop");
    {
        ring_buffer<int, 8> rb;
        t_check("初始 empty", rb.empty() && rb.size() == 0);
        t_check("try_push 成功", rb.try_push(1) && rb.try_push(2));
        t_check("size == 2", rb.size() == 2);
        t_check("front 队首", rb.front() && *rb.front() == 1);
        t_check("pop 成功", rb.pop());
        t_check("front 变化", rb.front() && *rb.front() == 2);
        t_check("pop 后 size==1", rb.size() == 1);
        t_check("pop 最后元素", rb.pop());
        t_check("空 pop 返回 false", !rb.pop());
        t_check("空 front 返回 nullptr", rb.front() == nullptr);
    }

    t_section(2, "满拒绝 (容量取整后)");
    {
        ring_buffer<int, 5> rb;
        t_check("capacity 取整到 8", rb.capacity() == 8);
        bool all_ok = true;
        for (int i = 0; i < 8; ++i)
        {
            if (!rb.try_push(i)) { all_ok = false; }
        }
        t_check("8 个全入队", all_ok && rb.size() == 8);
        t_check("第 9 个 try_push 拒绝", !rb.try_push(99));
        t_check("拒绝后队列不变", rb.size() == 8);
        t_check("try_emplace 满拒绝", rb.try_emplace(1) == nullptr);
    }

    t_section(3, "覆盖最旧 (push_overwrite / emplace_overwrite)");
    {
        ring_buffer<int, 4> rb;
        for (int i = 0; i < 4; ++i)
        {
            t_check("覆盖计数 0", rb.push_overwrite(i) == 0);
        }
        t_check("dropped_count 为 0", rb.dropped_count() == 0);
        t_check("入满后覆盖计数 1", rb.push_overwrite(100) == 1);
        t_check("dropped_count 累计 1", rb.dropped_count() == 1);
        t_check("覆盖后仍满", rb.size() == 4);
        t_check("最旧被淘汰", *rb.front() == 1);
        (void)rb.emplace_overwrite(200);
        t_check("emplace_overwrite 覆盖", *rb.front() == 2 && rb.dropped_count() == 2);
    }

    t_section(4, "回绕跨越 (head/tail 分处两段)");
    {
        ring_buffer<int, 8> rb;
        for (int i = 0; i < 3; ++i) { (void)rb.try_push(i); }
        for (int i = 0; i < 2; ++i) { (void)rb.pop(); }
        for (int i = 3; i < 8; ++i) { (void)rb.try_push(i); }
        t_check("跨回绕 size==6", rb.size() == 6);
        int expect = 2;
        bool order_ok = true;
        rb.drain([&](int& v) {
            if (v != expect) { order_ok = false; }
            ++expect;
        });
        t_check("跨回绕顺序 2..7", order_ok && expect == 8);
    }

    t_section(5, "try_pop 值移出");
    {
        ring_buffer<int, 8> rb;
        (void)rb.try_push(42);
        int out = 0;
        t_check("try_pop 移出", rb.try_pop(out) && out == 42);
        t_check("try_pop 后空", rb.empty());
        t_check("空 try_pop 失败", !rb.try_pop(out) && out == 42);
    }

    t_section(6, "front_run / pop_run 批量段");
    {
        ring_buffer<int, 8> rb;
        for (int i = 0; i < 6; ++i) { (void)rb.try_push(i); }
        int* p = nullptr;
        size_t len = 0;
        t_check("front_run 段长 6", rb.front_run(p, len) == 6 && len == 6);
        t_check("段内容 0..5", p && p[0] == 0 && p[5] == 5);
        t_check("pop_run 批量回收", rb.pop_run(6) == 6 && rb.empty());

        for (int i = 0; i < 8; ++i) { (void)rb.try_push(i); }
        for (int i = 0; i < 6; ++i) { (void)rb.pop(); }
        for (int i = 8; i < 11; ++i) { (void)rb.try_push(i); }
        t_check("跨回绕 front_run 首段 4", rb.front_run(p, len) == 4 && len == 4);
        t_check("首段内容 6..9", p && p[0] == 6 && p[3] == 9);
        t_check("pop_run 4 后接次段", rb.pop_run(4) == 4 && rb.front_run(p, len) == 1);
        t_check("次段内容 10", p && p[0] == 10);
        t_check("pop_run 超量截断", rb.pop_run(100) == 1 && rb.empty());
    }

    t_section(7, "visit 只读遍历");
    {
        ring_buffer<int, 8> rb;
        for (int i = 0; i < 5; ++i) { (void)rb.try_push(i); }
        int sum = 0;
        t_check("visit 计数 5", rb.visit([&](const int& v) { sum += v; }) == 5);
        t_check("visit 不消费", rb.size() == 5 && sum == 10);
    }

    t_section(8, "drain 与 drain_with_budget");
    {
        ring_buffer<int, 8> rb;
        for (int i = 0; i < 100; ++i) { (void)rb.try_push(i); }
        t_check("满 8 拒绝 92", rb.size() == 8);
        int sum = 0;
        t_check("drain 计数 8", rb.drain([&](int& v) { sum += v; }) == 8);
        t_check("drain 后空", rb.empty());

        for (int i = 0; i < 6; ++i) { (void)rb.try_push(i); }
        int cnt = 0;
        t_check("budget 限量 3", rb.drain_with_budget(3, [&](int&) { ++cnt; }) == 3);
        t_check("剩余 3 保留", rb.size() == 3);
    }

    t_section(9, "drain 回调内 try_push 重入");
    {
        ring_buffer<int, 8> rb;
        (void)rb.try_push(0);
        int total = 0;
        rb.drain_with_budget(16, [&](int& v) {
            ++total;
            if (v < 5) { (void)rb.try_push(v + 1); }
        });
        t_check("重入元素被消费 6 个", total == 6);
        t_check("最终排空", rb.empty());
    }

    t_section(10, "非平凡类型 (string)");
    {
        ring_buffer<std::string, 4> rb;
        (void)rb.try_push(std::string("hello"));
        (void)rb.try_emplace("world");
        t_check("string 入队 2", rb.size() == 2);
        t_check("front 内容", *rb.front() == "hello");
        std::string out;
        t_check("try_pop 移出 string", rb.try_pop(out) && out == "hello");
        rb.clear();
        t_check("clear 清空", rb.empty());
    }

    t_section(11, "move-only 类型与移动语义");
    {
        ring_buffer<move_only, 4> rb;
        t_check("try_push(move_only) 成功", rb.try_push(move_only(7)));
        t_check("emplace_overwrite 返回槽", rb.emplace_overwrite(8) != nullptr);
        move_only out(0);
        t_check("try_pop 移出 move-only", rb.try_pop(out) && out.v == 7);

        ring_buffer<int, 4> src;
        (void)src.try_push(1);
        (void)src.try_push(2);
        ring_buffer<int, 4> dst(std::move(src));
        t_check("移动后目标完整", dst.size() == 2 && *dst.front() == 1);
        t_check("移动后源置空", src.empty() && src.front() == nullptr);

        ring_buffer<int, 4> assigned;
        assigned = std::move(dst);
        t_check("移动赋值完整", assigned.size() == 2 && *assigned.front() == 1);
        t_check("移动赋值源置空", dst.empty());
        t_check("移动源 clear 安全", (src.clear(), src.empty()));
    }

    t_section(12, "dropped_count 统计");
    {
        ring_buffer<int, 2> rb;
        (void)rb.push_overwrite(1);
        (void)rb.push_overwrite(2);
        (void)rb.push_overwrite(3);
        (void)rb.push_overwrite(4);
        t_check("4 次入队覆盖 2 次", rb.dropped_count() == 2);
        (void)rb.pop();
        (void)rb.push_overwrite(5);
        t_check("有空位不计覆盖", rb.dropped_count() == 2);
    }

    return t_end("ring_buffer ");
}
