// test_module_class_pool.cpp - 模块功能测试: class_pool<T>
#include "test_common.hpp"
#include "include/part/class_pool.hpp"
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

struct item
{
    int v;
};

// 非平凡类型 (跟踪生命周期)
struct nt_item
{
    static inline int alive = 0;
    int v;

    nt_item() : v(0) { ++alive; }
    nt_item(int x) : v(x) { ++alive; }
    nt_item(const nt_item& o) : v(o.v) { ++alive; }
    nt_item(nt_item&& o) noexcept : v(o.v) { ++alive; }
    nt_item& operator=(const nt_item&) = default;
    nt_item& operator=(nt_item&&) noexcept = default;
    ~nt_item() { --alive; }
};

// 定长视图返回动态 span (不足 N 截断), 与 dense 对齐
static_assert(std::is_same_v<decltype(std::declval<class_pool<int>&>().first_fixed<8>()), std::span<int>>);
static_assert(std::is_same_v<decltype(std::declval<class_pool<int>&>().last_fixed<8>()), std::span<int>>);
static_assert(std::is_same_v<decltype(std::declval<class_pool<int>&>().window_span<8>(0)), std::span<int>>);
static_assert(std::is_same_v<decltype(std::declval<class_pool<int>&>().chunk_span<8>(0)), std::span<int>>);

// 区间构造限定 forward_iterator: 单遍迭代器编译期拒绝
static_assert(!std::is_constructible_v<class_pool<int>, std::istream_iterator<int>, std::istream_iterator<int>>);

int main()
{
    t_begin("test_module_class_pool — 模块功能测试: class_pool<T>");

    t_section(1, "构造与容量");
    {
        class_pool<int> p;
        t_check("默认为空", p.empty() && p.size() == 0);
        p.increase_capacity(64);
        t_check("increase_capacity", p.capacity() >= 64);
        p.reserve_exact(128);
        t_check("reserve_exact", p.capacity() >= 128);
    }

    t_section(2, "追加与访问");
    {
        class_pool<int> p;
        for (int i = 0; i < 10; ++i)
        {
            p.push_back(i);
        }
        t_check("push_back 计数", p.size() == 10);
        t_check("front/back", p.front() == 0 && p.back() == 9);
        t_check("data 可读", p.data()[5] == 5);

        p.emplace_back(99);
        t_check("emplace_back", p.back() == 99);

        int sum = 0;
        for (auto it = p.begin(); it != p.end(); ++it)
        {
            sum += *it;
        }
        t_check("迭代器求和", sum == 45 + 99);
    }

    t_section(3, "修改");
    {
        class_pool<int> p{1, 2, 3, 4, 5};
        p.pop_back();
        t_check("pop_back", p.size() == 4 && p.back() == 4);

        auto it1 = p.begin();
        ++it1;
        p.insert(it1, 88);
        t_check("insert 中间", p.size() == 5 && p[1] == 88);

        p.erase(p.begin());
        t_check("erase 单元素", p.size() == 4 && p[0] == 88);

        p.clear();
        t_check("clear", p.empty());
    }

    t_section(4, "for_each 与 swap");
    {
        class_pool<int> p{1, 2, 3};
        int sum = 0;
        p.for_each([&sum](int v) { sum += v; });
        t_check("for_each 求和", sum == 6);

        class_pool<int> q{9, 8};
        p.swap(q);
        t_check("swap 交换", p.size() == 2 && q.size() == 3 && p[0] == 9);
    }

    t_section(5, "非平凡类型");
    {
        class_pool<item> p;
        p.push_back(item{1});
        p.emplace_back(item{2});
        t_check("非平凡存储", p.size() == 2 && p[1].v == 2);

        class_pool<item> q = p;
        t_check("拷贝构造", q.size() == 2 && q[0].v == 1);
    }

    t_section(6, "批量接口");
    {
        class_pool<int> p;
        int src[100];
        for (int i = 0; i < 100; ++i)
        {
            src[i] = i;
        }
        p.append_bulk(src, 100);
        bool ok = p.size() == 100;
        for (int i = 0; i < 100 && ok; ++i)
        {
            if (p[i] != i) { ok = false; }
        }
        t_check("append_bulk 跨 64 边界", ok && p.count() == 100);

        {
            class_pool<nt_item> m;
            nt_item msrc[3] = {nt_item{1}, nt_item{2}, nt_item{3}};
            m.append_bulk_move(msrc, 3);
            t_check("append_bulk_move 非平凡搬移", m.size() == 3 && m[2].v == 3 && nt_item::alive == 6);
        }
        t_check("append_bulk_move 后无残留", nt_item::alive == 0);

        class_pool<int> g;
        uint64_t counter = 0;
        g.append_incrementing(70, counter);
        t_check("append_incrementing 跨字", g.size() == 70 && g[0] == 1 && g[63] == 64
                && g[64] == 65 && g[69] == 70 && counter == 70);

        int n = 0;
        class_pool<int> gen;
        gen.append_generated(5, [&n]() { return n++ * 2; });
        t_check("append_generated 生成器", gen.size() == 5 && gen[4] == 8);

        class_pool<int> a;
        a.append_n(70, 5);
        t_check("append_n 跨 64 边界", a.size() == 70 && a[0] == 5 && a[63] == 5
                && a[64] == 5 && a[69] == 5);

        // 有洞时追加不得改报 dense: 遍历会踩已销毁槽位
        class_pool<int> sp;
        sp.append_n(5, 1);
        sp.soft_sparse_delete(2);
        sp.append_bulk(src, 3);
        t_check("稀疏模式 append_bulk 保持稀疏", !sp.is_dense() && sp.count() == 7);
        int ssum = 0;
        for (int v : sp)
        {
            ssum += v;
        }
        t_check("稀疏模式追加后迭代正确", ssum == 1 + 1 + 1 + 1 + 0 + 1 + 2);

        class_pool<int> f;
        f.append_n(10, 1);
        f.fill_bulk(7, 2, 5);
        t_check("fill_bulk 区间填充", f.size() == 10 && f[1] == 1 && f[2] == 7
                && f[6] == 7 && f[7] == 1 && f.count() == 10);

        class_pool<int> e;
        e.fill_bulk(3, 5, 4);
        t_check("fill_bulk 跨越高水位", e.size() == 9 && e.count() == 9
                && e[0] == 0 && e[5] == 3 && e[8] == 3);

        class_pool<int> u;
        u.reserve_exact(4);
        u.push_back_unchecked(1);
        u.emplace_back_unchecked(2);
        u.emplace_back_dense_unchecked(3);
        t_check("unchecked 族追加", u.size() == 3 && u[0] == 1 && u[2] == 3);

        {
            class_pool<nt_item> pd;
            pd.prepare_dense(3);
            t_check("prepare_dense 非平凡默认构造", pd.size() == 3 && nt_item::alive == 3 && pd.is_dense());
            pd.clear();
            t_check("prepare_dense 后 clear 析构", nt_item::alive == 0);
        }

        class_pool<int> ph;
        ph.append_n(10, 1);
        ph.soft_sparse_delete(4);
        ph.prepare_dense(20);
        t_check("prepare_dense 扩展不填旧洞", ph.size() == 20 && ph.count() == 19
                && !ph.is_constructed_at(4) && ph.is_constructed_at(10));
    }

    t_section(7, "稀疏接口");
    {
        class_pool<int> p;
        p.append_n(10, 0);
        for (int i = 0; i < 10; ++i)
        {
            p[i] = i;
        }
        t_check("初始 dense", p.is_dense() && p.count() == 10);

        p.soft_sparse_delete(4);
        t_check("软删后非 dense", !p.is_dense());
        t_check("软删 count 减一", p.count() == 9 && p.holes_count() == 1);
        t_check("软删位未构造", !p.is_constructed_at(4) && p.is_constructed_at(5));
        int sum = 0;
        for (int v : p)
        {
            sum += v;
        }
        t_check("迭代跳过软删洞", sum == 41);

        size_t at = p.fill_the_hole_at(40);
        t_check("fill_the_hole_at 填洞", at == 4 && p[4] == 40 && p.count() == 10 && p.is_dense());
        t_check("填洞后 holes 归零", p.holes_count() == 0);

        class_pool<int> h;
        h.push_back(1);
        at = h.fill_the_hole_at(2);
        t_check("无洞 fill_the_hole_at 追加", at == 1 && h.size() == 2 && h[1] == 2);

        {
            class_pool<nt_item> s;
            s.append_n(5, nt_item{7});
            s.sparse_erase_at(2);
            t_check("sparse_erase_at 析构并清位", s.count() == 4 && nt_item::alive == 4 && !s.is_dense());
            s.sparse_erase_at(999);
            t_check("sparse_erase_at 越界安全", s.count() == 4 && nt_item::alive == 4);
            s.fill_the_hole(nt_item{8});
            t_check("fill_the_hole 填洞", s.count() == 5 && s[2].v == 8 && nt_item::alive == 5);
        }
        t_check("稀疏 erase 后无存活", nt_item::alive == 0);

        {
            class_pool<nt_item> q;
            q.append_n(3, nt_item{1});
            q.soft_sparse_delete(1);
            q.sparse_emplace_at(1, nt_item{2});
            t_check("sparse_emplace_at 填软删洞", q.count() == 3 && q[1].v == 2
                    && nt_item::alive == 3 && q.is_dense());
        }
        t_check("sparse_emplace_at 后无存活", nt_item::alive == 0);

        class_pool<int> d;
        d.append_n(70, 1);
        d.soft_dense_delete(5, 70);
        t_check("soft_dense_delete 区间", d.count() == 5 && d.holes_count() == 65 && !d.is_dense());
        t_check("soft_dense_delete 保留头部", d[0] == 1 && d[4] == 1);
        d.soft_dense_delete(10, 5);
        t_check("soft_dense_delete 空区间安全", d.count() == 5);
    }

    t_section(8, "容量管理");
    {
        class_pool<int> p;
        p.append_n(100, 7);
        p.shrink_to_fit();
        t_check("shrink_to_fit 收缩", p.capacity() == 100 && p.size() == 100 && p[99] == 7);

        class_pool<int> sp;
        sp.increase_capacity(200);
        sp.append_n(100, 3);
        sp.soft_sparse_delete(50);
        sp.shrink_to_fit();
        t_check("稀疏 shrink 保留洞", sp.capacity() == 100 && sp.size() == 100
                && sp.count() == 99 && sp[99] == 3);
        int ssum = 0;
        for (int v : sp)
        {
            ssum += v;
        }
        t_check("稀疏 shrink 后迭代", ssum == 99 * 3);

        class_pool<int> q;
        q.increase_capacity(200);
        q.append_n(70, 9);
        q.soft_sparse_delete(10);
        q.reduce_capacity(64);
        t_check("reduce 跨 inline 边界", q.capacity() == 64 && q.size() == 64
                && q.count() == 63 && q[0] == 9 && q[63] == 9);
        int qsum = 0;
        for (int v : q)
        {
            qsum += v;
        }
        t_check("reduce 跨 inline 边界迭代", qsum == 63 * 9);

        class_pool<int> i2;
        i2.append_n(40, 5);
        i2.reduce_capacity(32);
        t_check("inline→inline reduce 保留位图", i2.capacity() == 32 && i2.size() == 32
                && i2[31] == 5 && i2.count() == 32);

        class_pool<int> a;
        a.append_n(10, 0);
        for (int i = 0; i < 10; ++i)
        {
            a[i] = i;
        }
        class_pool<int> b;
        a.reduce_capacity(4, b);
        t_check("reduce_capacity 双参搬移", a.size() == 4 && a[3] == 3
                && b.size() == 6 && b[0] == 4 && b[5] == 9);

        class_pool<int> r;
        r.append_n(10, 1);
        r.reduce_capacity(4, r);
        t_check("reduce_capacity 自引用安全", r.size() == 10 && r[9] == 1);

        class_pool<int> e;
        e.increase_capacity(64);
        e.clear();
        e.shrink_to_fit();
        t_check("空容器 shrink 释放", e.capacity() == 0 && e.empty());

        class_pool<int> z;
        z.append_n(10, 1);
        z.reduce_capacity(0);
        t_check("reduce_capacity(0) 清空", z.size() == 0 && z.capacity() == 0);

        class_pool<int> c;
        c.increase_capacity(200);
        c.append_n(150, 1);
        c.soft_sparse_delete(10);
        c.clear();
        t_check("clear 高水位后状态", c.empty() && c.count() == 0 && c.is_dense() && c.holes_count() == 0);
        c.append_n(70, 2);
        t_check("clear 后重填", c.count() == 70 && c.is_constructed_at(69)
                && !c.is_constructed_at(70) && c[69] == 2);
        int cs = 0;
        for (int v : c)
        {
            cs += v;
        }
        t_check("clear 后重填迭代", cs == 70 * 2);
    }

    t_section(9, "回归: 软删与迭代器 erase 组合");
    {
        class_pool<nt_item> p;
        for (int i = 0; i < 10; ++i)
        {
            p.emplace_back(i);
        }
        p.soft_sparse_delete(3);
        p.soft_sparse_delete(7);
        t_check("双软删后稀疏", !p.is_dense() && p.count() == 8 && p.holes_count() == 2);

        auto it = p.begin();
        ++it;
        ++it;
        p.erase(it);
        t_check("稀疏 erase 单元素", p.size() == 9 && p.count() == 7 && p.holes_count() == 2);
        t_check("稀疏 erase 内容前移", p[2].v == 4 && p[6].v == 9);
        t_check("稀疏 erase 无泄漏", nt_item::alive == 7);

        auto first = p.begin();
        ++first;
        auto last = first;
        ++last;
        ++last;
        p.erase(first, last);
        t_check("稀疏 erase 区间", p.size() == 7 && p.count() == 5 && p.holes_count() == 2);
        t_check("稀疏 erase 区间内容", p[0].v == 0 && p[1].v == 5 && p[4].v == 9);
        t_check("稀疏 erase 区间无泄漏", nt_item::alive == 5);

        p.clear();
        t_check("clear 后全部析构", nt_item::alive == 0);
    }

    t_section(10, "状态查询");
    {
        class_pool<int> p;
        p.append_n(64, 1);
        t_check("64 元素 dense", p.is_dense() && p.count() == 64);
        p.append_n(1, 1);
        t_check("65 元素 count 跨字", p.count() == 65 && p.size() == 65);

        class_pool<int> e;
        e.emplace_at(5, 50);
        t_check("emplace_at 扩展造洞", e.size() == 6 && !e.is_dense() && e.count() == 1);
        t_check("emplace_at 洞位未构造", !e.is_constructed_at(0) && e.is_constructed_at(5));
        t_check("emplace_at holes_count", e.holes_count() == 5);
        e.emplace_at(2, 20);
        t_check("emplace_at 填洞", e.count() == 2 && e[2] == 20 && e.holes_count() == 4);

        {
            class_pool<nt_item> o;
            o.emplace_at(0, 1);
            o.emplace_at(0, 2);
            t_check("emplace_at 覆盖重建", o.size() == 1 && o[0].v == 2 && nt_item::alive == 1);
        }
        t_check("emplace_at 覆盖后无残留", nt_item::alive == 0);

        class_pool<int> c;
        c.append_n(5, 0);
        for (int i = 0; i < 5; ++i)
        {
            c[i] = i;
        }
        c.soft_sparse_delete(1);
        c.soft_sparse_delete(3);
        int buf[8] = {0};
        size_t n = c.compact_to(buf, 8);
        t_check("compact_to 数量与内容", n == 3 && buf[0] == 0 && buf[1] == 2 && buf[2] == 4);
        t_check("compact_to 截断", c.compact_to(buf, 2) == 2 && buf[1] == 2);
        t_check("live_count 等价 count", c.live_count() == 3 && c.live_count() == c.count());
        t_check("holes_count 与 live 互补", c.holes_count() == 2 && c.holes_count() + c.live_count() == c.size());
    }

    t_section(11, "回归: 小尺寸类型填充");
    {
        struct S1 { char c; };
        class_pool<S1> p1;
        p1.resize_fill(100, S1{'x'});
        bool ok1 = p1.size() == 100;
        for (size_t i = 0; i < 100 && ok1; ++i)
        {
            if (p1[i].c != 'x') { ok1 = false; }
        }
        t_check("1 字节类型 resize_fill", ok1);
        p1.fill_bulk(S1{'y'}, 10, 50);
        t_check("1 字节类型 fill_bulk", p1[9].c == 'x' && p1[10].c == 'y'
                && p1[59].c == 'y' && p1[60].c == 'x');
        p1.append_n(4, S1{'z'});
        t_check("1 字节类型 append_n", p1.size() == 104 && p1[100].c == 'z' && p1[103].c == 'z');

        class_pool<uint16_t> p2;
        p2.resize_fill(100, 0x1234);
        bool ok2 = p2.size() == 100;
        for (size_t i = 0; i < 100 && ok2; ++i)
        {
            if (p2[i] != 0x1234) { ok2 = false; }
        }
        t_check("2 字节类型 resize_fill", ok2);
        p2.fill_bulk(0xABCD, 0, 100);
        t_check("2 字节类型 fill_bulk", p2[0] == 0xABCD && p2[99] == 0xABCD);
        p2.append_n(4, static_cast<uint16_t>(0x5678));
        t_check("2 字节类型 append_n", p2.size() == 104 && p2[100] == 0x5678 && p2[103] == 0x5678);

        struct S3 { char c[3]; };
        class_pool<S3> p3;
        p3.resize_fill(100, S3{'a', 'b', 'c'});
        bool ok3 = p3.size() == 100;
        for (size_t i = 0; i < 100 && ok3; ++i)
        {
            if (p3[i].c[0] != 'a' || p3[i].c[1] != 'b' || p3[i].c[2] != 'c') { ok3 = false; }
        }
        t_check("3 字节类型 resize_fill", ok3);
        p3.fill_bulk(S3{'d', 'e', 'f'}, 0, 100);
        bool ok3b = p3.size() == 100;
        for (size_t i = 0; i < 100 && ok3b; ++i)
        {
            if (p3[i].c[0] != 'd' || p3[i].c[1] != 'e' || p3[i].c[2] != 'f') { ok3b = false; }
        }
        t_check("3 字节类型 fill_bulk", ok3b);
        p3.append_n(4, S3{'g', 'h', 'i'});
        t_check("3 字节类型 append_n", p3.size() == 104 && p3[100].c[2] == 'i' && p3[103].c[0] == 'g');

        {
            class_pool<nt_item> pn;
            pn.append_n(3, nt_item{7});
            t_check("非平凡 append_n 构造", pn.size() == 3 && pn[2].v == 7 && nt_item::alive == 3);
        }
        t_check("非平凡 append_n 后无残留", nt_item::alive == 0);
    }

    t_section(12, "回归: 访问保护与视图");
    {
        class_pool<int> p{1, 2, 3};
        t_check("get 越界降级", p.get(5, 1) == 2);
        t_check("get 双越界回落 0", p.get(5, 100) == 1);
        const class_pool<int>& cp = p;
        t_check("const get 越界降级", cp.get(5, 1) == 2);

        t_check("first_fixed 不足 N 截断", p.first_fixed<8>().size() == 3);
        t_check("last_fixed 不足 N 截断", p.last_fixed<8>().size() == 3);
        t_check("window_span 不足 N 截断", p.window_span<8>(0).size() == 3);
        t_check("chunk_span 不足 N 截断", p.chunk_span<8>(0).size() == 3);
        auto sp = p.first_fixed<8>();
        t_check("截断 span 内容有效", sp.size() == 3 && sp[0] == 1 && sp[2] == 3);

        class_pool<int> f;
        f.resize_fill(10, 7);
        t_check("resize_fill 增长并填充", f.size() == 10 && f[0] == 7 && f[9] == 7);
        f.resize_fill(5, 0);
        t_check("resize_fill 小于 size 不操作", f.size() == 10 && f[0] == 7);

        int arr[] = {9, 8, 7};
        class_pool<int> it_p(arr, arr + 3);
        t_check("前向迭代器区间构造", it_p.size() == 3 && it_p[1] == 8);

        class_pool<int> fp{1, 2, 3, 4, 5, 6};
        class_pool<size_t> idx;
        fp.filter_indices_to(idx, [](int v) { return v > 3; });
        t_check("filter_indices_to 索引", idx.size() == 3 && idx[0] == 3 && idx[2] == 5);
    }

    return t_end("class_pool ");
}
