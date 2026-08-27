#include "test_common.hpp"

// 容器漏洞猎手测试 - 边界用例探测 dense / class_pool 缺陷
// 布局约定: 逻辑探测在前, 崩溃探针最后 (崩溃前的输出即为定位依据)

static bool dense_values_ok(const dense<int>& d, const std::vector<int>& expect)
{
    if (d.size() != expect.size())
    {
        return false;
    }
    for (size_t i = 0; i < expect.size(); ++i)
    {
        if (d[i] != expect[i])
        {
            return false;
        }
    }
    return true;
}

static std::vector<int> collect_pool_values(const class_pool<int>& p)
{
    std::vector<int> vals;
    for (int v : p)
    {
        vals.push_back(v);
    }
    return vals;
}

int main()
{
    // 崩溃探针依赖崩溃前输出可见
    std::cout << std::unitbuf;
    std::cout << "========================================================\n"
              << "  容器漏洞猎手 (dense / class_pool)\n"
              << "========================================================\n";

    // 1. dense 边界正确性
    print_section(1, "dense 边界正确性");
    {
        dense<int> d1;
        d1.fill_bulk(9, 2, 3);
        print_item("fill_bulk 跳跃填充 gap 补零", dense_values_ok(d1, {0, 0, 9, 9, 9}));

        dense<int> d2{1, 2, 3, 4, 5};
        d2.fill_bulk(7, 1, 3);
        print_item("fill_bulk 区间覆盖", dense_values_ok(d2, {1, 7, 7, 7, 5}));

        dense<int> d3{1, 2, 3, 4, 5};
        dense<int> dst3{7, 8};
        d3.reduce_capacity(2, dst3);
        print_item("reduce_capacity+dst 源截断", dense_values_ok(d3, {1, 2}));
        print_item("reduce_capacity+dst 尾部搬运", dense_values_ok(dst3, {7, 8, 3, 4, 5}));

        dense<int> d4{1, 2, 3};
        dense<int> dst4;
        d4.reduce_capacity(0, dst4);
        print_item("reduce_capacity(0,dst) 源清空", (d4.size() == 0 && !d4.valid()));
        print_item("reduce_capacity(0,dst) 全部搬运", dense_values_ok(dst4, {1, 2, 3}));

        dense<int> d5{1, 2, 3, 4, 5};
        d5.reduce_capacity(3);
        print_item("reduce_capacity 缩容销毁", dense_values_ok(d5, {1, 2, 3}) && d5.capacity() == 3);

        dense<int> d6{1, 2, 3, 4};
        auto it6 = d6.emplace(d6.begin() + 1, 99);
        print_item("emplace 满容量中间插入", dense_values_ok(d6, {1, 99, 2, 3, 4}) && *it6 == 99);

        dense<int> d7;
        bool ok7 = true;
        for (int i = 0; i < 300; ++i)
        {
            d7.push_back(i);
        }
        for (int i = 0; i < 300; ++i)
        {
            if (d7[i] != i)
            {
                ok7 = false;
            }
        }
        print_item("push_back 跨容量边界值完整", ok7 && d7.size() == 300);

        // {1,2,3,4,5} 去头去尾得 {2,3,4}, 再删 [1,3) 得 {2}
        dense<int> d8{1, 2, 3, 4, 5};
        d8.erase(d8.begin());
        d8.erase(d8.end() - 1);
        d8.erase(d8.begin() + 1, d8.begin() + 3);
        print_item("erase 头/尾/区间", dense_values_ok(d8, {2}));

        dense<int> d9{1, 2, 3};
        dense<int> d9c(d9);
        d9c.push_back(4);
        print_item("拷贝后 push_back 扩容", dense_values_ok(d9c, {1, 2, 3, 4}));

        dense<int> d10{1, 2, 3};
        dense<int>* d10_ptr = &d10;
        d10 = *d10_ptr;
        print_item("自赋值防护", dense_values_ok(d10, {1, 2, 3}));

        dense<int> d11{1, 2, 3};
        dense<int> d11m(std::move(d11));
        d11.push_back(7);
        print_item("move 后源复用", dense_values_ok(d11m, {1, 2, 3}) && dense_values_ok(d11, {7}));

        dense<int> a12{1, 2}, b12{3, 4, 5};
        a12.swap(b12);
        print_item("swap 双向状态", dense_values_ok(a12, {3, 4, 5}) && dense_values_ok(b12, {1, 2}));

        dense<int> d13;
        d13.shrink_to_fit();
        print_item("shrink_to_fit 空容器", d13.size() == 0 && d13.capacity() == 0);

        dense<int> d14{1, 2, 3};
        d14.increase_capacity(6, 0);
        print_item("increase_capacity(n,v) size 扩展", dense_values_ok(d14, {1, 2, 3, 0, 0, 0}));

        dense<int> d15{1};
        d15.append_bulk(nullptr, 0);
        d15.append_n(0, 5);
        print_item("append 零大小无操作", dense_values_ok(d15, {1}));

        dense<int> d16{1, 2, 3};
        auto sp16a = d16.strided_span_view(3, 1, 5);
        auto sp16b = d16.strided_span_view(0, 0, 5);
        print_item("strided_span_view 越界 start 空", sp16a.empty());
        print_item("strided_span_view step=0 防护", sp16b.empty());
    }

    // 2. 空范围构造不变量 (修复前 capacity>0 而 data 为空, 追加即崩溃)
    print_section(2, "空范围构造不变量");
    {
        dense<int> e1(size_t(0), 42);
        print_item("dense(0,v) capacity/data 一致", !(e1.capacity() > 0 && e1.data() == nullptr));

        std::vector<int> empty_vec;
        dense<int> e2(empty_vec.begin(), empty_vec.end());
        print_item("dense(空范围) capacity/data 一致", !(e2.capacity() > 0 && e2.data() == nullptr));

        class_pool<int> e3(size_t(0), 42);
        print_item("class_pool(0,v) capacity/data 一致", !(e3.capacity() > 0 && e3.data() == nullptr));

        class_pool<int> e4(empty_vec.begin(), empty_vec.end());
        print_item("class_pool(空范围) capacity/data 一致", !(e4.capacity() > 0 && e4.data() == nullptr));

        dense<int> e5;
        print_item("dense 默认构造一致", e5.capacity() == 0 && e5.data() == nullptr);
        class_pool<int> e6;
        print_item("class_pool 默认构造一致", e6.capacity() == 0 && e6.data() == nullptr);
    }

    // 3. class_pool 稀疏状态一致性 (修复前 append 系列误置 is_dense_)
    print_section(3, "class_pool 稀疏状态一致性");
    {
        class_pool<int> p1{10, 20, 30, 40, 50};
        p1.sparse_erase_at(1);
        p1.push_back(60);
        print_item("sparse_erase 后 push_back 保持稀疏", !p1.is_dense() && p1.count() == 5);

        class_pool<int> p2{10, 20, 30, 40, 50};
        p2.sparse_erase_at(1);
        p2.append_n(2, 60);
        print_item("sparse_erase 后 append_n 保持稀疏", !p2.is_dense() && p2.count() == 6);

        class_pool<int> p3{10, 20, 30, 40, 50};
        p3.sparse_erase_at(1);
        int src3[2] = {60, 70};
        p3.append_bulk(src3, 2);
        print_item("append_bulk 保持稀疏状态", !p3.is_dense() && p3.count() == 6);

        std::vector<int> vals3 = collect_pool_values(p3);
        std::vector<int> expect3 = {10, 30, 40, 50, 60, 70};
        print_item("append_bulk 后遍历跳洞", vals3 == expect3);

        class_pool<int> p4{10, 20, 30, 40, 50};
        p4.sparse_erase_at(1);
        int src4[2] = {60, 70};
        p4.append_bulk(src4, 2);
        size_t hole_idx4 = p4.fill_the_hole_at(99);
        print_item("append_bulk 后 fill_the_hole_at 填洞", hole_idx4 == 1);

        class_pool<int> p5{10, 20, 30, 40, 50};
        p5.sparse_erase_at(1);
        int src5[2] = {60, 70};
        p5.append_bulk_move(src5, 2);
        print_item("append_bulk_move 保持稀疏", !p5.is_dense() && p5.count() == 6);

        class_pool<int> p6{10, 20, 30, 40, 50};
        p6.sparse_erase_at(1);
        p6.fill_bulk(60, 5, 2);
        print_item("fill_bulk 尾部保持稀疏", !p6.is_dense() && p6.count() == 6);

        // 删 2 剩 3 个, 填 [3,5) 覆盖洞 idx3 与 idx4, 洞 1 仍在
        class_pool<int> p7{10, 20, 30, 40, 50};
        p7.sparse_erase_at(1);
        p7.sparse_erase_at(3);
        p7.fill_bulk(88, 3, 2);
        print_item("fill_bulk 区间填洞后仍稀疏", !p7.is_dense() && p7.count() == 4);

        class_pool<int> p8{10, 20, 30};
        p8.sparse_erase_at(1);
        p8.emplace_at(1, 99);
        print_item("emplace_at 填洞恢复 dense", p8.is_dense() && p8.count() == 3 && p8[1] == 99);

        class_pool<int> p9{10, 20, 30};
        p9.emplace_at(10, 99);
        print_item("emplace_at 跳跃扩展产生洞", !p9.is_dense() && p9.size() == 11 && p9.count() == 4 && p9[10] == 99);
    }

    // 4. hole_count 计数一致性 (修复前 soft_dense_delete 重复计数)
    print_section(4, "hole_count 计数一致性");
    {
        class_pool<int> q1{0, 1, 2, 3};
        q1.soft_sparse_delete(1);
        q1.soft_dense_delete(0, 4);
        for (int i = 0; i < 4; ++i)
        {
            q1.fill_the_hole_at(100 + i);
        }
        print_item("soft_dense_delete 后填回恢复 dense", q1.is_dense() && q1.count() == 4);

        class_pool<int> q2{0, 1};
        q2.soft_dense_delete(10, 20);
        print_item("soft_dense_delete 未用区间无影响", q2.is_dense() && q2.count() == 2);

        class_pool<int> q3{0, 1, 2, 3};
        q3.soft_sparse_delete(1);
        q3.soft_sparse_delete(2);
        q3.fill_the_hole_at(9);
        q3.fill_the_hole_at(9);
        print_item("soft_sparse_delete 填回恢复 dense", q3.is_dense() && q3.count() == 4);

        class_pool<int> q4{1, 2, 3};
        q4.sparse_erase_at(2);
        q4.push_back(4);
        print_item("尾部洞后追加 count 正确", q4.count() == 3 && q4.size() == 4);
    }

    // 5. 软删除密度回收 (soft_remove 墓碑 → add 复用 → compact)
    print_section(5, "软删除密度回收");
    {
        // 基础: soft_remove 后 size 虚高, live_count/tombstone_count 正确
        single_class_set s1;
        entity a1(1, 1), a2(2, 1), a3(3, 1);
        s1.add(a1, 10);
        s1.add(a2, 20);
        s1.add(a3, 30);
        s1.soft_remove(a2);
        print_item("soft_remove 后 size 虚高", s1.size() == 3 && s1.live_count() == 2 && s1.tombstone_count() == 1);

        // add 复用死槽: 不增长物理槽位
        entity a4(4, 1);
        s1.add(a4, 40);
        print_item("add 复用死槽不增长", s1.size() == 3 && s1.live_count() == 3 && s1.tombstone_count() == 0);
        print_item("复用后 a4 可查询", s1.get_ptr<int>(a4) != nullptr && *s1.get_ptr<int>(a4) == 40);
        print_item("复用后旧条目仍死", s1.get_ptr<int>(a2) == nullptr && !s1.contains_entity(a2));

        // compact: 全墓碑场景回收后 size == live_count
        single_class_set s2;
        entity b1(10, 1), b2(20, 1), b3(30, 1), b4(40, 1), b5(50, 1);
        s2.add(b1, 1);
        s2.add(b2, 2);
        s2.add(b3, 3);
        s2.add(b4, 4);
        s2.add(b5, 5);
        s2.soft_remove(b1);
        s2.soft_remove(b3);
        s2.soft_remove(b5);
        operating_message om = s2.compact();
        print_item("compact 回收 3 墓碑", (bool)om && s2.size() == 2 && s2.live_count() == 2);
        print_item("compact 后活条目可查", s2.get_ptr<int>(b2) != nullptr && s2.get_ptr<int>(b4) != nullptr);
        print_item("compact 后墓碑条目死", s2.get_ptr<int>(b1) == nullptr && s2.get_ptr<int>(b3) == nullptr
                                              && s2.get_ptr<int>(b5) == nullptr);
        print_item("compact 后组件值正确", *s2.get_ptr<int>(b2) == 2 && *s2.get_ptr<int>(b4) == 4);

        // 幽灵场景: 尾部墓碑被 hard_remove 搬运, 不得复活
        single_class_set s3;
        entity c1(100, 1), c2(200, 1), c3(300, 1);
        s3.add(c1, 11);
        s3.add(c2, 22);
        s3.add(c3, 33);
        s3.soft_remove(c3);            // 尾部成墓碑
        s3.hard_remove(c1);            // 尾部墓碑被搬到 c1 的槽位
        print_item("墓碑搬运不复活", s3.get_ptr<int>(c3) == nullptr && !s3.contains_entity(c3));
        print_item("搬运后活条目完好", s3.get_ptr<int>(c2) != nullptr && *s3.get_ptr<int>(c2) == 22);
        print_item("搬运后计数正确", s3.live_count() == 1);
        om = s3.compact();
        print_item("幽灵场景 compact 回收", (bool)om && s3.size() == 1 && s3.live_count() == 1);

        // remove→add 循环: 物理槽位有界
        single_class_set s4;
        entity d1(1000, 1);
        s4.add(d1, 7);
        size_t max_size = 0;
        for (int i = 0; i < 500; ++i)
        {
            s4.soft_remove(d1);
            s4.add(d1, i);
            if (s4.size() > max_size)
            {
                max_size = s4.size();
            }
        }
        print_item("remove→add 循环有界", max_size <= 2 && s4.get_ptr<int>(d1) != nullptr && *s4.get_ptr<int>(d1) == 499);

        // 非平凡类型: compact 时孤儿析构
        {
            int destroyed = 0;
            struct tracker
            {
                int* sink;
                int v;
                tracker(int* s, int x) : sink(s), v(x) {}
                tracker(const tracker&) = default;
                tracker& operator=(const tracker&) = default;
                ~tracker() { if (sink) { ++*sink; } }
            };
            single_class_set s5;
            entity e1(2000, 1), e2(2001, 1), e3(2002, 1);
            s5.add(e1, tracker(&destroyed, 1));
            s5.add(e2, tracker(&destroyed, 2));
            s5.add(e3, tracker(&destroyed, 3));
            destroyed = 0;
            s5.soft_remove(e2);
            om = s5.compact();
            // e2 孤儿在 compact 析构; 其余两个仍存活
            print_item("非平凡孤儿 compact 析构", (bool)om && destroyed == 1 && s5.live_count() == 2);
        }

        // 阈值自动回收: 墓碑数 >= max(n/4, 64) 时自动触发
        single_class_set s6;
        for (uint32_t i = 0; i < 200; ++i)
        {
            s6.add(entity(i + 3000, 1), static_cast<int>(i));
        }
        for (uint32_t i = 0; i < 100; ++i)
        {
            s6.soft_remove(entity(i + 3000, 1));
        }
        // 第 64 次 soft_remove 时触发 (64 >= max(200/4, 64)), 回收后 36 个新墓碑低于阈值不再触发
        print_item("阈值自动回收触发", s6.size() == 136 && s6.live_count() == 100 && s6.tombstone_count() == 36);
        print_item("自动回收后值完好", s6.get_ptr<int>(entity(3100, 1)) != nullptr
                                       && *s6.get_ptr<int>(entity(3100, 1)) == 100);

        // 乱序 add: 远端 index 先落位, 间隙内 index 后落位/查询
        //   回归: 曾因预抬升 sparse_size_ 致间隙未初始化, checked_ 读垃圾误判为活条目而崩溃
        single_class_set s7;
        entity far_e(5000, 1), gap_a(100, 1), gap_b(3000, 1), gap_c(4999, 1);
        s7.add(far_e, 1);
        s7.add(gap_a, 2);
        s7.add(gap_b, 3);
        s7.add(gap_c, 4);
        print_item("间隙槽位判死", s7.get_ptr<int>(entity(200, 1)) == nullptr
                                    && s7.get_ptr<int>(entity(4999, 1)) != nullptr);
        print_item("间隙写入可查", *s7.get_ptr<int>(gap_a) == 2 && *s7.get_ptr<int>(gap_b) == 3
                                    && *s7.get_ptr<int>(gap_c) == 4 && *s7.get_ptr<int>(far_e) == 1);
        print_item("间隙场景计数", s7.live_count() == 4 && s7.tombstone_count() == 0);
        om = s7.compact();
        print_item("间隙场景 compact", (bool)om && s7.size() == 4 && s7.live_count() == 4);
    }

    std::cout << "\n  以上为逻辑探测结果, 崩溃探针开始 (若在此之后崩溃即为空构造缺陷)\n\n";

    // 6. 崩溃探针: 空范围构造后追加
    print_section(6, "崩溃探针 (空构造后追加)");
    {
        std::cout << "  探针 1/4: dense(0,v).push_back\n";
        dense<int> c1(size_t(0), 42);
        c1.push_back(1);
        print_item("dense(0,v) 追加存活", c1.size() == 1 && c1[0] == 1);

        std::cout << "  探针 2/4: dense(空范围).push_back\n";
        std::vector<int> empty_vec;
        dense<int> c2(empty_vec.begin(), empty_vec.end());
        c2.push_back(2);
        print_item("dense(空范围) 追加存活", c2.size() == 1 && c2[0] == 2);

        std::cout << "  探针 3/4: class_pool(0,v).push_back\n";
        class_pool<int> c3(size_t(0), 42);
        c3.push_back(3);
        print_item("class_pool(0,v) 追加存活", c3.size() == 1 && c3[0] == 3);

        std::cout << "  探针 4/4: class_pool(空范围).push_back\n";
        class_pool<int> c4(empty_vec.begin(), empty_vec.end());
        c4.push_back(4);
        print_item("class_pool(空范围) 追加存活", c4.size() == 1 && c4[0] == 4);
    }

    print_summary("功能测试");
    return 0;
}
