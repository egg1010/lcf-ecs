#include "test_common.hpp"
#include "include/part/dense.hpp"

// ============================================================
// 功能测试 - 验证 dense<T> 通用容器接口正确性
// ============================================================
int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "========================================================\n"
              << "  dense<T> 通用容器功能测试\n"
              << "========================================================\n";

    // ========================================================
    // 1. 构造函数
    // ========================================================
    print_section(1, "构造函数");
    {
        dense<int> d_def;
        print_item("默认构造 empty()", d_def.empty());
        print_item("默认构造 size()==0", d_def.size() == 0);
        print_item("默认构造 valid()==false", !d_def.valid());

        dense<int> d_cap(64);
        print_item("dense(capacity)", d_cap.capacity() >= 64);
        print_item("dense(capacity) empty", d_cap.empty());

        dense<int> d_fill(static_cast<size_t>(5), 42);
        print_item("dense(count, value) size", d_fill.size() == 5);
        print_item("dense(count, value) [0]", d_fill[0] == 42);
        print_item("dense(count, value) [4]", d_fill[4] == 42);

        std::vector<int> vec = {10, 20, 30};
        dense<int> d_it(vec.begin(), vec.end());
        print_item("dense(InputIt, InputIt) size", d_it.size() == 3);
        print_item("dense(InputIt, InputIt) [0]", d_it[0] == 10);
        print_item("dense(InputIt, InputIt) [2]", d_it[2] == 30);

        dense<int> d_init = {100, 200, 300};
        print_item("dense(initializer_list) size", d_init.size() == 3);
        print_item("dense(initializer_list) [1]", d_init[1] == 200);

        dense<int> d_copy(d_init);
        d_init[0] = 999;
        print_item("拷贝构造 深拷贝 size", d_copy.size() == 3);
        print_item("拷贝构造 深拷贝 隔离", d_copy[0] == 100);

        dense<int> d_move_src = {7, 8, 9};
        dense<int> d_move_dst(std::move(d_move_src));
        print_item("移动构造 size", d_move_dst.size() == 3);
        print_item("移动构造 [0]", d_move_dst[0] == 7);
        print_item("移动构造 源对象 empty", d_move_src.empty());
    }

    // ========================================================
    // 2. 赋值
    // ========================================================
    print_section(2, "赋值");
    {
        dense<int> a = {1, 2, 3};
        dense<int> b;
        b = a;
        a[0] = 999;
        print_item("拷贝赋值 深拷贝 size", b.size() == 3);
        print_item("拷贝赋值 深拷贝 隔离", b[0] == 1);

        dense<int> c;
        dense<int> d = {5, 6};
        c = std::move(d);
        print_item("移动赋值 size", c.size() == 2);
        print_item("移动赋值 [0]", c[0] == 5);
        print_item("移动赋值 源对象 empty", d.empty());

        // 自赋值
        dense<int> e = {1, 2, 3};
        e = e;
        print_item("自赋值 size", e.size() == 3);
        print_item("自赋值 [1]", e[1] == 2);

        dense<int> f = {1, 2, 3};
        f = std::move(f);
        print_item("自移动 size", f.size() == 3);
    }

    // ========================================================
    // 3. 元素访问
    // ========================================================
    print_section(3, "元素访问");
    {
        dense<int> d = {10, 20, 30, 40, 50};
        print_item("operator[]", d[2] == 30);
        print_item("front()", d.front() == 10);
        print_item("back()", d.back() == 50);
        print_item("data()", (d.data() != nullptr && d.data()[0] == 10));

        std::span<int> sp = d.span();
        print_item("span() size", sp.size() == 5);
        print_item("span() [2]", sp[2] == 30);

        const dense<int>& cd = d;
        std::span<const int> csp = cd.span();
        print_item("span() const size", csp.size() == 5);
        print_item("span() const [2]", csp[2] == 30);

        print_item("get(size_t)", d.get(2) == d[2]);
        print_item("get(size_t) const", cd.get(3) == cd[3]);

        // 越界保护访问: size()==5, index=100 越界 -> 访问 error_index=0
        print_item("get(100, 0) 越界回退", d.get(100, 0) == d[0]);
        print_item("get(3, 0) 合法不回退", d.get(3, 0) == d[3]);
        print_item("get(100, 1) const 越界回退", cd.get(100, 1) == cd[1]);
        print_item("get(2, 1) const 合法不回退", cd.get(2, 1) == cd[2]);

        // 修改元素
        d[2] = 999;
        print_item("operator[] 写入", d[2] == 999);
        d.get(2) = 30;
        print_item("get() 写入", d[2] == 30);
    }

    // ========================================================
    // 4. 迭代器
    // ========================================================
    print_section(4, "迭代器");
    {
        dense<int> d = {1, 2, 3, 4, 5};
        print_item("begin()", *d.begin() == 1);
        print_item("end() - begin() == size", d.end() - d.begin() == 5);

        size_t count = 0;
        for (auto it = d.begin(); it != d.end(); ++it) { ++count; }
        print_item("正向遍历 count", count == 5);

        int sum = 0;
        for (auto v : d) { sum += v; }
        print_item("range-for 求和", sum == 15);

        // const 迭代器
        const dense<int>& cd = d;
        int csum = 0;
        for (auto it = cd.begin(); it != cd.end(); ++it) { csum += *it; }
        print_item("const begin/end 求和", csum == 15);

        int cbsum = 0;
        for (auto it = cd.cbegin(); it != cd.cend(); ++it) { cbsum += *it; }
        print_item("cbegin/cend 求和", cbsum == 15);

        // 修改
        for (auto& v : d) { v *= 2; }
        print_item("range-for 写入", d[0] == 2 && d[4] == 10);
    }

    // ========================================================
    // 5. 容量与状态查询
    // ========================================================
    print_section(5, "容量与状态查询");
    {
        dense<int> d;
        print_item("默认 empty()", d.empty());
        print_item("默认 size()==0", d.size() == 0);
        print_item("默认 valid()==false", !d.valid());
        print_item("默认 capacity()==0", d.capacity() == 0);
        print_item("size_bytes()==0", d.size_bytes() == 0);
        print_item("capacity_bytes()==0", d.capacity_bytes() == 0);
        print_item("max_size() > 0", d.max_size() > 0);
        print_item("count()==size()", d.count() == d.size());

        d.emplace_back(42);
        print_item("emplace_back 后 !empty", !d.empty());
        print_item("emplace_back 后 size()==1", d.size() == 1);
        print_item("emplace_back 后 valid()", d.valid());
        print_item("emplace_back 后 count()==1", d.count() == 1);
        print_item("size_bytes()==sizeof(int)", d.size_bytes() == sizeof(int));
        print_item("capacity_bytes()>=size_bytes()", d.capacity_bytes() >= d.size_bytes());
    }

    // ========================================================
    // 6. increase_capacity / reserve_exact
    // ========================================================
    print_section(6, "increase_capacity / reserve_exact");
    {
        dense<int> d;
        d.increase_capacity(100);
        print_item("increase_capacity 后 capacity>=100", d.capacity() >= 100);
        print_item("increase_capacity 不改 size", d.size() == 0);

        size_t cap_before = d.capacity();
        d.increase_capacity(50);
        print_item("increase_capacity 只增不减", d.capacity() == cap_before);

        d.reserve_exact(500);
        print_item("reserve_exact 后 capacity>=500", d.capacity() >= 500);

        // increase_capacity(n, value): 扩容并填充到 n
        dense<int> e;
        e.emplace_back(1);
        e.increase_capacity(5, 99);
        print_item("increase_capacity(n, value) size==5", e.size() == 5);
        print_item("increase_capacity(n, value) 原元素保留", e[0] == 1);
        print_item("increase_capacity(n, value) 新元素填充", e[1] == 99 && e[4] == 99);

        // increase_capacity(n, value) 当 n<=size 时直接返回
        dense<int> f = {1, 2, 3, 4, 5};
        f.increase_capacity(3, 99);
        print_item("increase_capacity(n<=size) 不变", f.size() == 5 && f[2] == 3);
    }

    // ========================================================
    // 7. resize / shrink_to_fit / reduce_capacity
    // ========================================================
    print_section(7, "resize / shrink_to_fit / reduce_capacity");
    {
        // resize 增长
        dense<int> d = {1, 2, 3};
        d.resize(6, 7);
        print_item("resize 增长 size==6", d.size() == 6);
        print_item("resize 增长 原元素保留", d[0] == 1 && d[2] == 3);
        print_item("resize 增长 新元素填充", d[3] == 7 && d[5] == 7);

        // resize 缩小
        d.resize(2, 0);
        print_item("resize 缩小 size==2", d.size() == 2);
        print_item("resize 缩小 保留前 2", d[0] == 1 && d[1] == 2);

        // resize 到同大小
        d.resize(2, 0);
        print_item("resize 同大小", d.size() == 2);

        // shrink_to_fit
        dense<int> e;
        e.increase_capacity(1000);
        for (int i = 0; i < 10; ++i) { e.emplace_back(i); }
        e.shrink_to_fit();
        print_item("shrink_to_fit 后 capacity==size", e.capacity() == e.size());
        print_item("shrink_to_fit 数据保留", e[0] == 0 && e[9] == 9);

        // shrink_to_fit 空容器
        dense<int> empty_d;
        empty_d.increase_capacity(100);
        empty_d.shrink_to_fit();
        print_item("shrink_to_fit 空容器 valid()==false", !empty_d.valid());
        print_item("shrink_to_fit 空容器 capacity==0", empty_d.capacity() == 0);

        // reduce_capacity 截断
        dense<int> g;
        for (int i = 0; i < 20; ++i) { g.emplace_back(i); }
        g.reduce_capacity(10);
        print_item("reduce_capacity 截断 size==10", g.size() == 10);
        print_item("reduce_capacity 截断 数据保留", g[0] == 0 && g[9] == 9);
        print_item("reduce_capacity capacity==10", g.capacity() == 10);

        // reduce_capacity 到 0
        dense<int> h = {1, 2, 3};
        h.reduce_capacity(0);
        print_item("reduce_capacity(0) size==0", h.size() == 0);
        print_item("reduce_capacity(0) capacity==0", h.capacity() == 0);
        print_item("reduce_capacity(0) !valid()", !h.valid());

        // reduce_capacity 不缩小 (new_cap >= cap)
        dense<int> k;
        k.increase_capacity(100);
        size_t k_cap = k.capacity();
        k.reduce_capacity(k_cap + 100);
        print_item("reduce_capacity(>=cap) 不变", k.capacity() == k_cap);

        // reduce_capacity(n, dst): 截断元素迁移到 dst
        dense<int> src_m = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        dense<int> dst_m = {100, 200};
        src_m.reduce_capacity(7, dst_m);
        print_item("reduce_capacity(n,dst) src size==7", src_m.size() == 7);
        print_item("reduce_capacity(n,dst) src [6]==7", src_m[6] == 7);
        print_item("reduce_capacity(n,dst) dst size==5", dst_m.size() == 5);
        print_item("reduce_capacity(n,dst) dst [0] 保留", dst_m[0] == 100);
        print_item("reduce_capacity(n,dst) dst [2]==8", dst_m[2] == 8);
        print_item("reduce_capacity(n,dst) dst [4]==10", dst_m[4] == 10);
        print_item("reduce_capacity(n,dst) src capacity==7", src_m.capacity() == 7);

        // reduce_capacity(0, dst): 全部迁移
        dense<int> src_all = {1, 2, 3};
        dense<int> dst_all;
        src_all.reduce_capacity(0, dst_all);
        print_item("reduce_capacity(0,dst) src empty", src_all.empty());
        print_item("reduce_capacity(0,dst) src !valid()", !src_all.valid());
        print_item("reduce_capacity(0,dst) dst size==3", dst_all.size() == 3);
        print_item("reduce_capacity(0,dst) dst [0]==1", dst_all[0] == 1);
        print_item("reduce_capacity(0,dst) dst [2]==3", dst_all[2] == 3);

        // reduce_capacity(n, dst) 当 n >= size 时无操作
        dense<int> src_noop = {1, 2, 3};
        dense<int> dst_noop = {99};
        src_noop.reduce_capacity(5, dst_noop);
        print_item("reduce_capacity(>=size,dst) src 不变", src_noop.size() == 3);
        print_item("reduce_capacity(>=size,dst) dst 不变", dst_noop.size() == 1);

        // 非平凡类型 reduce_capacity(n, dst)
        dense<std::string> src_s = {"a", "b", "c", "d", "e"};
        dense<std::string> dst_s = {"x"};
        src_s.reduce_capacity(2, dst_s);
        print_item("string reduce_capacity(n,dst) src size==2", src_s.size() == 2);
        print_item("string reduce_capacity(n,dst) src [1]==b", src_s[1] == "b");
        print_item("string reduce_capacity(n,dst) dst size==4", dst_s.size() == 4);
        print_item("string reduce_capacity(n,dst) dst [0] 保留", dst_s[0] == "x");
        print_item("string reduce_capacity(n,dst) dst [1]==c", dst_s[1] == "c");
        print_item("string reduce_capacity(n,dst) dst [3]==e", dst_s[3] == "e");
    }

    // ========================================================
    // 8. emplace_back / push_back_unchecked / emplace_back_unchecked
    // ========================================================
    print_section(8, "emplace_back / push_back_unchecked");
    {
        dense<int> d;
        d.emplace_back(1);
        d.emplace_back(2);
        d.emplace_back(3);
        print_item("emplace_back size==3", d.size() == 3);
        print_item("emplace_back [2]==3", d[2] == 3);

        // 自动扩容
        dense<int> e;
        for (int i = 0; i < 1000; ++i) { e.emplace_back(i); }
        print_item("emplace_back 1000 次自动扩容", e.size() == 1000);
        print_item("emplace_back 数据正确", e[0] == 0 && e[999] == 999);

        // push_back_unchecked (需预分配)
        dense<int> f;
        f.increase_capacity(10);
        f.push_back_unchecked(10);
        f.push_back_unchecked(20);
        print_item("push_back_unchecked size==2", f.size() == 2);
        print_item("push_back_unchecked [1]==20", f[1] == 20);

        // emplace_back_unchecked (需预分配)
        dense<int> g;
        g.increase_capacity(5);
        g.emplace_back_unchecked(100);
        g.emplace_back_unchecked(200);
        print_item("emplace_back_unchecked size==2", g.size() == 2);
        print_item("emplace_back_unchecked [1]==200", g[1] == 200);

        // emplace_back_dense_unchecked (等价 emplace_back_unchecked)
        dense<int> h;
        h.increase_capacity(5);
        h.emplace_back_dense_unchecked(7);
        print_item("emplace_back_dense_unchecked", h.size() == 1 && h[0] == 7);

        // 非平凡类型 emplace_back
        dense<std::string> s;
        s.emplace_back("hello");
        s.emplace_back("world");
        print_item("emplace_back string size==2", s.size() == 2);
        print_item("emplace_back string [0]", s[0] == "hello");
        print_item("emplace_back string [1]", s[1] == "world");
    }

    // ========================================================
    // 9. append_n / append_bulk / append_bulk_move
    // ========================================================
    print_section(9, "append_n / append_bulk / append_bulk_move");
    {
        // append_n
        dense<int> d0;
        d0.append_n(0, 42);
        print_item("append_n(0)", d0.size() == 0);

        dense<int> d1;
        d1.append_n(1, 42);
        print_item("append_n(1)", d1.size() == 1 && d1[0] == 42);

        dense<int> d64;
        d64.append_n(64, 7);
        print_item("append_n(64)", d64.size() == 64 && d64[63] == 7);

        dense<int> d128;
        d128.append_n(128, 9);
        print_item("append_n(128) 跨扩容", d128.size() == 128 && d128[127] == 9);

        dense<int> d_partial;
        d_partial.append_n(60, 1);
        d_partial.append_n(70, 2);
        bool ok = (d_partial.size() == 130);
        for (size_t i = 0; i < 60 && ok; ++i) ok = (d_partial[i] == 1);
        for (size_t i = 60; i < 130 && ok; ++i) ok = (d_partial[i] == 2);
        print_item("append_n 二次追加", ok);

        dense<int> d_grow;
        d_grow.append_n(10000, 5);
        print_item("append_n 自动扩容", d_grow.size() == 10000 && d_grow[9999] == 5);

        // append_bulk
        int src[] = {100, 200, 300, 400, 500};
        dense<int> db;
        db.append_bulk(src, 5);
        print_item("append_bulk size==5", db.size() == 5);
        print_item("append_bulk [0]==100", db[0] == 100);
        print_item("append_bulk [4]==500", db[4] == 500);

        // append_bulk 空数组
        dense<int> db_empty;
        db_empty.append_bulk(static_cast<const int*>(nullptr), 0);
        print_item("append_bulk(0)", db_empty.size() == 0);

        // append_bulk_move
        int src_move[] = {10, 20, 30};
        dense<int> dm;
        dm.append_bulk_move(src_move, 3);
        print_item("append_bulk_move size==3", dm.size() == 3);
        print_item("append_bulk_move [2]==30", dm[2] == 30);

        // append_bulk 非平凡类型
        std::string str_src[] = {"a", "b", "c"};
        dense<std::string> ds;
        ds.append_bulk(str_src, 3);
        print_item("append_bulk string size==3", ds.size() == 3);
        print_item("append_bulk string [1]==b", ds[1] == "b");
    }

    // ========================================================
    // 10. append_incrementing / append_generated
    // ========================================================
    print_section(10, "append_incrementing / append_generated");
    {
        // append_incrementing
        dense<uint32_t> d;
        uint64_t counter = 100;
        d.append_incrementing(5, counter);
        print_item("append_incrementing size==5", d.size() == 5);
        print_item("append_incrementing [0]==101", d[0] == 101);
        print_item("append_incrementing [4]==105", d[4] == 105);
        print_item("append_incrementing counter==105", counter == 105);

        // append_incrementing 0 个
        dense<uint32_t> e;
        uint64_t c0 = 50;
        e.append_incrementing(0, c0);
        print_item("append_incrementing(0)", e.size() == 0 && c0 == 50);

        // append_generated
        dense<int> g;
        int gen_val = 0;
        g.append_generated(5, [&gen_val]() { return gen_val++; });
        print_item("append_generated size==5", g.size() == 5);
        print_item("append_generated [0]==0", g[0] == 0);
        print_item("append_generated [4]==4", g[4] == 4);

        // append_generated 非平凡类型
        dense<std::string> gs;
        int i = 0;
        gs.append_generated(3, [&i]() { return std::string("item-") + std::to_string(i++); });
        print_item("append_generated string size==3", gs.size() == 3);
        print_item("append_generated string [0]", gs[0] == "item-0");
        print_item("append_generated string [2]", gs[2] == "item-2");
    }

    // ========================================================
    // 11. fill_bulk
    // ========================================================
    print_section(11, "fill_bulk");
    {
        // 基本填充
        dense<int> d;
        d.increase_capacity(20);
        d.fill_bulk(42, 0, 5);
        print_item("fill_bulk 基本 size==5", d.size() == 5);
        print_item("fill_bulk [0]==42", d[0] == 42);
        print_item("fill_bulk [4]==42", d[4] == 42);

        // fill_bulk count==0
        dense<int> d0;
        d0.fill_bulk(99, 0, 0);
        print_item("fill_bulk(0)", d0.size() == 0);

        // fill_bulk 覆盖已有元素
        dense<int> d_over = {1, 2, 3, 4, 5};
        d_over.fill_bulk(99, 1, 3);
        print_item("fill_bulk 覆盖 size 不变", d_over.size() == 5);
        print_item("fill_bulk 覆盖 [0] 保留", d_over[0] == 1);
        print_item("fill_bulk 覆盖 [1]==99", d_over[1] == 99);
        print_item("fill_bulk 覆盖 [3]==99", d_over[3] == 99);
        print_item("fill_bulk 覆盖 [4] 保留", d_over[4] == 5);

        // fill_bulk 自动扩容 + 中间填充默认值
        dense<int> d_gap;
        d_gap.emplace_back(1);  // index_=1
        d_gap.fill_bulk(77, 5, 3);  // start=5, count=3, end=8 > index_=1
        print_item("fill_bulk 中间填充 size==8", d_gap.size() == 8);
        print_item("fill_bulk 中间 [0] 保留", d_gap[0] == 1);
        print_item("fill_bulk 中间 [1] 默认", d_gap[1] == 0);
        print_item("fill_bulk 中间 [4] 默认", d_gap[4] == 0);
        print_item("fill_bulk 中间 [5]==77", d_gap[5] == 77);
        print_item("fill_bulk 中间 [7]==77", d_gap[7] == 77);

        // fill_bulk 非平凡类型
        dense<std::string> ds;
        ds.increase_capacity(10);
        ds.fill_bulk(std::string("x"), 0, 5);
        print_item("fill_bulk string size==5", ds.size() == 5);
        print_item("fill_bulk string [0]==x", ds[0] == "x");
        print_item("fill_bulk string [4]==x", ds[4] == "x");

        // fill_bulk 非平凡类型覆盖
        dense<std::string> ds_over = {"a", "b", "c", "d", "e"};
        ds_over.fill_bulk(std::string("z"), 1, 3);
        print_item("fill_bulk string 覆盖 size", ds_over.size() == 5);
        print_item("fill_bulk string 覆盖 [0] 保留", ds_over[0] == "a");
        print_item("fill_bulk string 覆盖 [1]==z", ds_over[1] == "z");
        print_item("fill_bulk string 覆盖 [4] 保留", ds_over[4] == "e");
    }

    // ========================================================
    // 12. pop_back
    // ========================================================
    print_section(12, "pop_back");
    {
        dense<int> d = {1, 2, 3, 4, 5};
        d.pop_back();
        print_item("pop_back size==4", d.size() == 4);
        print_item("pop_back back()==4", d.back() == 4);

        d.pop_back();
        d.pop_back();
        d.pop_back();
        d.pop_back();
        print_item("pop_back 到空", d.empty());

        // 空 pop_back 不崩溃
        d.pop_back();
        print_item("pop_back 空容器安全", d.empty());

        // 非平凡类型 pop_back
        dense<std::string> s = {"a", "b", "c"};
        s.pop_back();
        print_item("pop_back string size==2", s.size() == 2);
        print_item("pop_back string back()==b", s.back() == "b");
    }

    // ========================================================
    // 13. emplace / insert
    // ========================================================
    print_section(13, "emplace / insert");
    {
        dense<int> d = {1, 2, 4, 5};
        d.emplace(d.begin() + 2, 3);
        print_item("emplace 中间 size==5", d.size() == 5);
        print_item("emplace 中间 [2]==3", d[2] == 3);
        print_item("emplace 中间 [3]==4", d[3] == 4);

        // emplace 头部
        dense<int> d2 = {2, 3, 4};
        d2.emplace(d2.begin(), 1);
        print_item("emplace 头部 [0]==1", d2[0] == 1);
        print_item("emplace 头部 [3]==4", d2[3] == 4);

        // emplace 尾部
        dense<int> d3 = {1, 2, 3};
        d3.emplace(d3.end(), 4);
        print_item("emplace 尾部 size==4", d3.size() == 4);
        print_item("emplace 尾部 [3]==4", d3[3] == 4);

        // insert const T&
        dense<int> d4 = {1, 2, 4};
        int val = 99;
        d4.insert(d4.begin() + 2, val);
        print_item("insert const& [2]==99", d4[2] == 99);

        // insert T&&
        dense<int> d5 = {1, 2, 4};
        d5.insert(d5.begin() + 2, 99);
        print_item("insert && [2]==99", d5[2] == 99);

        // 非平凡类型 emplace
        dense<std::string> s = {"a", "c", "d"};
        s.emplace(s.begin() + 1, "b");
        print_item("emplace string size==4", s.size() == 4);
        print_item("emplace string [1]==b", s[1] == "b");
        print_item("emplace string [3]==d", s[3] == "d");
    }

    // ========================================================
    // 14. erase
    // ========================================================
    print_section(14, "erase");
    {
        // erase 单元素 中间
        dense<int> d = {1, 2, 3, 4, 5};
        auto it = d.erase(d.begin() + 2);
        print_item("erase 单元素 size==4", d.size() == 4);
        print_item("erase 单元素 [2]==4", d[2] == 4);
        print_item("erase 返回迭代器", *it == 4);

        // erase 头部
        dense<int> d2 = {1, 2, 3, 4};
        d2.erase(d2.begin());
        print_item("erase 头部 [0]==2", d2[0] == 2);

        // erase 尾部
        dense<int> d3 = {1, 2, 3, 4};
        d3.erase(d3.end() - 1);
        print_item("erase 尾部 size==3", d3.size() == 3);
        print_item("erase 尾部 back()==3", d3.back() == 3);

        // erase 越界
        dense<int> d4 = {1, 2, 3};
        auto it4 = d4.erase(d4.end());
        print_item("erase 越界返回 end", it4 == d4.end());
        print_item("erase 越界 size 不变", d4.size() == 3);

        // erase 区间
        dense<int> d5 = {1, 2, 3, 4, 5, 6, 7};
        auto it5 = d5.erase(d5.begin() + 1, d5.begin() + 4);
        print_item("erase 区间 size==4", d5.size() == 4);
        print_item("erase 区间 [0]==1", d5[0] == 1);
        print_item("erase 区间 [1]==5", d5[1] == 5);
        print_item("erase 区间 [3]==7", d5[3] == 7);
        print_item("erase 区间 返回迭代器", *it5 == 5);

        // erase 全部
        dense<int> d6 = {1, 2, 3};
        d6.erase(d6.begin(), d6.end());
        print_item("erase 全部 empty", d6.empty());

        // erase 空区间
        dense<int> d7 = {1, 2, 3};
        auto it7 = d7.erase(d7.begin() + 1, d7.begin() + 1);
        print_item("erase 空区间 size 不变", d7.size() == 3);
        print_item("erase 空区间 返回", *it7 == 2);

        // 非平凡类型 erase
        dense<std::string> s = {"a", "b", "c", "d", "e"};
        s.erase(s.begin() + 1);
        print_item("erase string size==4", s.size() == 4);
        print_item("erase string [1]==c", s[1] == "c");

        dense<std::string> s2 = {"a", "b", "c", "d", "e"};
        s2.erase(s2.begin() + 1, s2.begin() + 3);
        print_item("erase string 区间 size==3", s2.size() == 3);
        print_item("erase string 区间 [1]==d", s2[1] == "d");
    }

    // ========================================================
    // 15. clear / swap
    // ========================================================
    print_section(15, "clear / swap");
    {
        dense<int> d = {1, 2, 3, 4, 5};
        d.clear();
        print_item("clear 后 empty", d.empty());
        print_item("clear 后 size()==0", d.size() == 0);
        print_item("clear 后 capacity 保留", d.capacity() > 0);

        // clear 后仍可使用
        d.emplace_back(99);
        print_item("clear 后 emplace_back", d.size() == 1 && d[0] == 99);

        // swap
        dense<int> a = {1, 2, 3};
        dense<int> b = {10, 20, 30, 40};
        a.swap(b);
        print_item("swap a.size==4", a.size() == 4);
        print_item("swap a[0]==10", a[0] == 10);
        print_item("swap b.size==3", b.size() == 3);
        print_item("swap b[0]==1", b[0] == 1);

        // 全局 swap
        dense<int> c = {100, 200};
        dense<int> e = {300, 400, 500};
        swap(c, e);
        print_item("全局 swap c.size==3", c.size() == 3);
        print_item("全局 swap e.size==2", e.size() == 2);
        print_item("全局 swap c[0]==300", c[0] == 300);

        // swap 空容器
        dense<int> f = {1, 2, 3};
        dense<int> g;
        f.swap(g);
        print_item("swap 空容器 f.empty", f.empty());
        print_item("swap 空容器 g.size==3", g.size() == 3);

        // swap 自身
        dense<int> h = {1, 2, 3};
        h.swap(h);
        print_item("swap 自身 size==3", h.size() == 3);
    }

    // ========================================================
    // 16. for_each
    // ========================================================
    print_section(16, "for_each");
    {
        dense<int> d = {1, 2, 3, 4, 5};

        int sum = 0;
        d.for_each([&sum](int& v) { sum += v; });
        print_item("for_each 求和", sum == 15);

        const dense<int>& cd = d;
        int csum = 0;
        cd.for_each([&csum](const int& v) { csum += v; });
        print_item("for_each const 求和", csum == 15);

        // for_each 修改
        d.for_each([](int& v) { v *= 2; });
        print_item("for_each 修改 [0]==2", d[0] == 2);
        print_item("for_each 修改 [4]==10", d[4] == 10);

        // 空容器 for_each
        dense<int> empty;
        int empty_sum = 0;
        empty.for_each([&empty_sum](int& v) { empty_sum += v; });
        print_item("for_each 空容器", empty_sum == 0);
    }

    // ========================================================
    // 17. 非平凡类型全面验证
    // ========================================================
    print_section(17, "非平凡类型 (std::string) 全面验证");
    {
        // 构造
        dense<std::string> d = {"alpha", "beta", "gamma"};
        print_item("string 构造 size==3", d.size() == 3);
        print_item("string [0]==alpha", d[0] == "alpha");

        // 拷贝构造
        dense<std::string> d_copy(d);
        d[0] = "modified";
        print_item("string 拷贝构造 隔离", d_copy[0] == "alpha");

        // 移动构造
        dense<std::string> d_move(std::move(d_copy));
        print_item("string 移动构造 size==3", d_move.size() == 3);
        print_item("string 移动构造 [0]==alpha", d_move[0] == "alpha");
        print_item("string 移动构造 源 empty", d_copy.empty());

        // resize
        dense<std::string> d_resize = {"a", "b", "c"};
        d_resize.resize(5, "x");
        print_item("string resize 增长 size==5", d_resize.size() == 5);
        print_item("string resize [3]==x", d_resize[3] == "x");

        d_resize.resize(2, "");
        print_item("string resize 缩小 size==2", d_resize.size() == 2);
        print_item("string resize [1]==b", d_resize[1] == "b");

        // shrink_to_fit
        dense<std::string> d_shrink;
        d_shrink.increase_capacity(1000);
        d_shrink.emplace_back("hello");
        d_shrink.emplace_back("world");
        d_shrink.shrink_to_fit();
        print_item("string shrink_to_fit capacity==size", d_shrink.capacity() == d_shrink.size());
        print_item("string shrink_to_fit 数据保留", d_shrink[0] == "hello");

        // reduce_capacity
        dense<std::string> d_reduce;
        for (int i = 0; i < 10; ++i) {
            d_reduce.emplace_back(std::string("item") + std::to_string(i));
        }
        d_reduce.reduce_capacity(5);
        print_item("string reduce_capacity size==5", d_reduce.size() == 5);
        print_item("string reduce_capacity [0]", d_reduce[0] == "item0");
        print_item("string reduce_capacity [4]", d_reduce[4] == "item4");

        // emplace / erase
        dense<std::string> d_em = {"a", "c", "d"};
        d_em.emplace(d_em.begin() + 1, "b");
        print_item("string emplace [1]==b", d_em[1] == "b");

        d_em.erase(d_em.begin() + 2);
        print_item("string erase size==3", d_em.size() == 3);
        print_item("string erase [2]==d", d_em[2] == "d");

        // erase 区间
        dense<std::string> d_erange = {"a", "b", "c", "d", "e"};
        d_erange.erase(d_erange.begin() + 1, d_erange.begin() + 3);
        print_item("string erase 区间 size==3", d_erange.size() == 3);
        print_item("string erase 区间 [1]==d", d_erange[1] == "d");

        // append_bulk 非平凡
        std::string src[] = {"x", "y", "z"};
        dense<std::string> d_bulk;
        d_bulk.append_bulk(src, 3);
        print_item("string append_bulk [2]==z", d_bulk[2] == "z");

        // swap
        dense<std::string> s1 = {"a", "b"};
        dense<std::string> s2 = {"c", "d", "e"};
        s1.swap(s2);
        print_item("string swap s1.size==3", s1.size() == 3);
        print_item("string swap s2.size==2", s2.size() == 2);

        // clear
        s1.clear();
        print_item("string clear empty", s1.empty());

        // pop_back
        dense<std::string> d_pop = {"a", "b", "c"};
        d_pop.pop_back();
        print_item("string pop_back size==2", d_pop.size() == 2);
        print_item("string pop_back back()==b", d_pop.back() == "b");
    }

    // ========================================================
    // 18. 自定义类型验证
    // ========================================================
    print_section(18, "自定义类型 (Position) 验证");
    {
        dense<Position> d;
        for (int i = 0; i < 100; ++i) {
            d.emplace_back(static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3));
        }
        print_item("Position emplace_back 100 次", d.size() == 100);
        print_item("Position [50].x==50", d[50].x == 50.0f);
        print_item("Position [50].y==100", d[50].y == 100.0f);
        print_item("Position [50].z==150", d[50].z == 150.0f);

        // for_each 求和
        float sx = 0, sy = 0, sz = 0;
        d.for_each([&](Position& p) { sx += p.x; sy += p.y; sz += p.z; });
        print_item("Position for_each x 求和", sx > 0);
        print_item("Position for_each y 求和", sy > 0);

        // range-for
        float rx = 0;
        for (auto& p : d) { rx += p.x; }
        print_item("Position range-for 求和", rx == sx);

        // erase
        d.erase(d.begin() + 50);
        print_item("Position erase size==99", d.size() == 99);

        // resize
        d.resize(50, Position(0, 0, 0));
        print_item("Position resize 缩小", d.size() == 50);

        // shrink_to_fit
        d.shrink_to_fit();
        print_item("Position shrink_to_fit", d.capacity() == d.size());

        // 拷贝
        dense<Position> d_copy(d);
        print_item("Position 拷贝构造 size==50", d_copy.size() == 50);
        print_item("Position 拷贝构造 [0].x", d_copy[0].x == d[0].x);

        // 移动
        dense<Position> d_move(std::move(d_copy));
        print_item("Position 移动构造 size==50", d_move.size() == 50);
        print_item("Position 移动构造 源 empty", d_copy.empty());
    }

    // ========================================================
    // 19. 边界条件
    // ========================================================
    print_section(19, "边界条件");
    {
        // 0 容量
        dense<int> d0;
        print_item("0 容量 empty", d0.empty());
        print_item("0 容量 size_bytes==0", d0.size_bytes() == 0);
        print_item("0 容量 data()==nullptr", d0.data() == nullptr);

        // 单元素
        dense<int> d1;
        d1.emplace_back(42);
        print_item("单元素 size==1", d1.size() == 1);
        print_item("单元素 front==back", d1.front() == d1.back());

        // 大量元素 (触发多次扩容)
        dense<int> dbig;
        for (int i = 0; i < 100000; ++i) {
            dbig.emplace_back(i);
        }
        print_item("100K 元素 size", dbig.size() == 100000);
        print_item("100K 元素 [0]==0", dbig[0] == 0);
        print_item("100K 元素 [99999]==99999", dbig[99999] == 99999);

        // 大量元素后 shrink
        dbig.shrink_to_fit();
        print_item("100K shrink_to_fit capacity==size", dbig.capacity() == dbig.size());

        // 大量元素 reduce
        dbig.reduce_capacity(50000);
        print_item("100K reduce 截断 size==50000", dbig.size() == 50000);
        print_item("100K reduce [49999]==49999", dbig[49999] == 49999);

        // 空容器各种操作
        dense<int> dempty;
        dempty.clear();
        print_item("空 clear 安全", dempty.empty());
        dempty.shrink_to_fit();
        print_item("空 shrink_to_fit 安全", !dempty.valid());
        dempty.reduce_capacity(0);
        print_item("空 reduce_capacity(0) 安全", !dempty.valid());

        // 空 erase
        dense<int> de;
        auto it = de.erase(de.begin());
        print_item("空 erase 返回 end", it == de.end());

        // 空 pop_back
        de.pop_back();
        print_item("空 pop_back 安全", de.empty());
    }

    print_summary("dense<T> 功能测试");
    return 0;
}
