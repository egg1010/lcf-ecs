// test_module_dense.cpp - 模块功能测试: dense<T>
#include "test_common.hpp"
#include "include/part/dense.hpp"
#include <string>
#include <utility>
#include <iterator>

// container_strided_span::iterator 应满足 forward_iterator (默认构造)
static_assert(std::forward_iterator<container_strided_span<dense<int>>::iterator>);

// 非平凡类型 (跟踪生命周期)
struct nt_item
{
    static inline int alive = 0;
    std::string name;
    int value;

    nt_item() : name(), value(0) { ++alive; }
    nt_item(const char* n, int v) : name(n), value(v) { ++alive; }
    nt_item(const nt_item& o) : name(o.name), value(o.value) { ++alive; }
    nt_item(nt_item&& o) noexcept : name(std::move(o.name)), value(o.value) { ++alive; }
    nt_item& operator=(const nt_item&) = default;
    nt_item& operator=(nt_item&&) noexcept = default;
    ~nt_item() { --alive; }

    bool operator==(const nt_item& o) const { return name == o.name && value == o.value; }
};

// 持堆资源类型 (析构不置空指针, 用于验证 erase 不移动赋值到已析构对象)
struct res_item
{
    static inline int alive = 0;
    int* p;
    explicit res_item(int v) : p(new int(v)) { ++alive; }
    res_item(const res_item& o) : p(new int(*o.p)) { ++alive; }
    res_item(res_item&& o) noexcept : p(o.p) { o.p = nullptr; }
    res_item& operator=(const res_item& o) { if (this != &o) { delete p; p = new int(*o.p); } return *this; }
    res_item& operator=(res_item&& o) noexcept { if (this != &o) { delete p; p = o.p; o.p = nullptr; } return *this; }
    ~res_item() { delete p; --alive; }
};

int main()
{
    t_begin("test_module_dense — 模块功能测试: dense<T>");

    t_section(1, "构造与容量");
    {
        dense<int> d0;
        t_check("默认构造为空", d0.empty() && d0.size() == 0);

        dense<int> d1(16);
        t_check("指定容量构造 capacity>=16", d1.capacity() >= 16 && d1.size() == 0);

        dense<int> d2(5, 7);
        bool ok = d2.size() == 5;
        for (size_t i = 0; i < d2.size(); ++i)
        {
            if (d2[i] != 7) { ok = false; }
        }
        t_check("count+value 构造", ok);

        dense<int> d3{1, 2, 3, 4};
        t_check("initializer_list 构造", d3.size() == 4 && d3[0] == 1 && d3[3] == 4);

        int arr[] = {9, 8, 7};
        dense<int> d4(arr, arr + 3);
        t_check("迭代器区间构造", d4.size() == 3 && d4[1] == 8);
    }

    t_section(2, "追加与访问");
    {
        dense<int> d;
        for (int i = 0; i < 10; ++i)
        {
            d.emplace_back(i * i);
        }
        t_check("push_back 计数", d.size() == 10);
        t_check("operator[] 取值", d[3] == 9 && d[9] == 81);
        t_check("front/back", d.front() == 0 && d.back() == 81);
        t_check("data 连续可读", d.data()[5] == 25);

        d.emplace_back(100);
        t_check("emplace_back 追加", d.back() == 100);

        int sum = 0;
        for (int v : d)
        {
            sum += v;
        }
        t_check("范围 for 遍历求和", sum == 285 + 100);
    }

    t_section(3, "容量管理");
    {
        dense<int> d;
        d.increase_capacity(100);
        size_t cap = d.capacity();
        t_check("increase_capacity 扩容", cap >= 100);

        d.emplace_back(1);
        d.increase_capacity(500);
        t_check("increase_capacity 扩容", d.capacity() >= 500 && d.size() == 1 && d[0] == 1);

        d.reduce_capacity(d.size());
        t_check("reduce_capacity 收缩到 size", d.capacity() >= 1 && d.size() == 1);

        d.reduce_capacity(8);
        t_check("reduce_capacity", d.capacity() >= 1 && d[0] == 1);
    }

    t_section(4, "插入与删除");
    {
        dense<int> d{1, 2, 3, 4, 5};
        d.pop_back();
        t_check("pop_back", d.size() == 4 && d.back() == 4);

        d.emplace(d.begin() + 1, 99);
        t_check("insert 中间", d.size() == 5 && d[1] == 99 && d[2] == 2);

        d.erase(d.begin(), d.begin() + 1);
        t_check("erase 单元素", d.size() == 4 && d[0] == 99);

        d.erase(d.begin(), d.begin() + 2);
        t_check("erase 区间", d.size() == 2 && d[0] == 3);

        d.clear();
        t_check("clear 清空", d.empty() && d.size() == 0);
    }

    t_section(5, "批量操作");
    {
        dense<int> d;
        d.fill_bulk(3, d.size(), 4);
        bool ok = d.size() == 4;
        for (size_t i = 0; i < d.size(); ++i)
        {
            if (d[i] != 3) { ok = false; }
        }
        t_check("append_n 重复填充", ok);

        int src[] = {10, 20, 30};
        d.append_bulk(src, 3);
        t_check("append_bulk 追加数组", d.size() == 7 && d[4] == 10 && d[6] == 30);

        int counter = 0;
        d.append_generated(3, [&counter]() { return ++counter; });
        t_check("append_generated 生成器", d.size() == 10 && d[9] == 3);

        d.fill_bulk(0, 0, 10);
        bool zero = true;
        for (size_t i = 0; i < d.size(); ++i)
        {
            if (d[i] != 0) { zero = false; }
        }
        t_check("fill_bulk 区间填充", zero);
    }

    t_section(6, "迭代与算法");
    {
        dense<int> d{1, 2, 3, 4, 5, 6};

        int sum = 0;
        d.for_each([&sum](int v) { sum += v; });
        t_check("for_each 求和", sum == 21);

        int rsum = 0;
        d.reverse_for_each([&rsum](int v) { rsum = rsum * 10 + v; });
        t_check("reverse_for_each 逆序", rsum == 654321);

        int ssum = 0;
        d.strided_for_each(0, 2, [&ssum](int v) { ssum += v; });
        t_check("strided_for_each 步进", ssum == 1 + 3 + 5);

        int esum = 0;
        d.filter_for_each([](int v) { return v % 2 == 0; }, [&esum](int v) { esum += v; });
        t_check("filter_for_each 过滤", esum == 2 + 4 + 6);

        dense<size_t> idx;
        d.filter_indices_to(idx, [](int v) { return v > 3; });
        t_check("filter_indices_to 索引", idx.size() == 3 && idx[0] == 3 && idx[2] == 5);

        int zsum = 0;
        dense<int> other{10, 20, 30, 40, 50, 60};
        d.for_each_zip(other.data(), 6, [&zsum](int a, int b) { zsum += a * b; });
        t_check("for_each_zip 双序列", zsum == 1 * 10 + 2 * 20 + 3 * 30 + 4 * 40 + 5 * 50 + 6 * 60);
    }

    t_section(7, "非平凡类型生命周期");
    {
        {
            dense<nt_item> d;
            d.emplace_back("a", 1);
            d.emplace_back("b", 2);
            d.emplace_back(nt_item("c", 3));
            t_check("非平凡对象构造数", nt_item::alive == 3);
            t_check("非平凡对象内容", d[1].name == "b" && d[1].value == 2);

            d.pop_back();
            t_check("pop_back 后析构", nt_item::alive == 2);

            d.clear();
            t_check("clear 后全部析构", nt_item::alive == 0);
        }
        t_check("dense 析构后无存活", nt_item::alive == 0);
    }

    t_section(8, "拷贝与移动");
    {
        dense<int> a{1, 2, 3};
        dense<int> b = a;
        t_check("拷贝构造独立", b.size() == 3 && b[2] == 3 && b.data() != a.data());

        dense<int> c = std::move(a);
        t_check("移动构造接管", c.size() == 3 && c[0] == 1);

        dense<int> e;
        e = b;
        t_check("拷贝赋值", e.size() == 3 && e[1] == 2);

        dense<int> f;
        f = std::move(c);
        t_check("移动赋值", f.size() == 3 && f[2] == 3);

        a.swap(e);
        t_check("swap 交换", a.size() == 3 && a[0] == 1 && e.size() == 0);
    }

    t_section(9, "回归: 非平凡类型中间删除");
    {
        // erase 区间: 先移动尾部再析构, 不得对已析构对象移动赋值
        dense<nt_item> d;
        d.emplace_back("a", 1);
        d.emplace_back("b", 2);
        d.emplace_back("c", 3);
        d.emplace_back("d", 4);
        d.emplace_back("e", 5);
        d.erase(d.begin() + 1, d.begin() + 3);
        bool ok = d.size() == 3 && d[0].name == "a" && d[1].name == "d" && d[2].name == "e";
        t_check("非平凡 erase 区间内容", ok);
        t_check("非平凡 erase 区间存活数", nt_item::alive == 3);
        d.clear();
        t_check("clear 后全部析构", nt_item::alive == 0);

        // 持堆资源类型 (析构不置空指针): 移动赋值目标必须存活
        dense<res_item> r;
        for (int i = 0; i < 6; ++i) { r.emplace_back(i); }
        r.erase(r.begin() + 1, r.begin() + 4);
        t_check("资源型 erase 区间不崩溃", r.size() == 3 && res_item::alive == 3);
        r.clear();
        t_check("资源型 erase 后无泄漏", res_item::alive == 0);
    }

    t_section(10, "回归: 大元素与特殊尺寸");
    {
        // sizeof=288 (32倍数且>256): AVX2 广播 vals 数组必须不越界
        struct alignas(32) Big { char data[288]; };
        dense<Big> d;
        Big v;
        std::memset(v.data, 0xAB, sizeof(v));
        d.fill_bulk(v, d.size(), 100);
        bool ok = d.size() == 100;
        for (size_t i = 0; i < d.size() && ok; ++i)
        {
            for (size_t j = 0; j < sizeof(Big); ++j)
            {
                if (static_cast<unsigned char>(d[i].data[j]) != 0xAB) { ok = false; break; }
            }
        }
        t_check("288B 元素 AVX2 广播填充无栈越界", ok);

        // 1 字节自定义 trivially copyable 类型: fill_bulk 不可强转
        struct Byte { char c; };
        dense<Byte> b;
        b.fill_bulk(Byte{'X'}, 0, 10);
        t_check("1 字节自定义类型 fill_bulk", b.size() == 10 && b[9].c == 'X');
    }

    t_section(11, "回归: 定长 span 截断");
    {
        dense<int> d{1, 2, 3};
        t_check("first_fixed 不足 N 截断", d.first(8).size() == 3);
        t_check("last_fixed 不足 N 截断", d.last(8).size() == 3);
        t_check("window_span 不足 N 截断", d.window_span<8>(0).size() == 3);
        t_check("chunk_span 不足 N 截断", d.window_span<8>(0).size() == 3);
        auto sp = d.first(8);
        t_check("截断 span 内容有效", sp.size() == 3 && sp[0] == 1 && sp[2] == 3);
    }

    t_section(12, "回归: 访问保护与容量");
    {
        dense<int> d{1, 2, 3};
        t_check("get 越界降级", d.get(5, 1) == 2);
        t_check("get 双越界回落 0", d.get(5, 100) == 1);

        // resize_fill: 增长 size 并填充
        dense<int> f;
        f.resize_fill(10, 7);
        t_check("resize_fill 增长并填充", f.size() == 10 && f[0] == 7 && f[9] == 7);
        f.resize_fill(5, 0);
        t_check("resize_fill 小于 size 不操作", f.size() == 10 && f[0] == 7);

        // reduce_capacity 自引用: 不得死循环/越界
        dense<int> r;
        r.fill_bulk(1, r.size(), 10);
        r.reduce_capacity(4, r);
        t_check("reduce_capacity 自引用安全", r.size() == 10);

        // 前向迭代器区间构造; istream_iterator 等单遍迭代器应被 forward_iterator 约束拒绝
        int arr[] = {9, 8, 7};
        dense<int> it_d(arr, arr + 3);
        t_check("前向迭代器区间构造", it_d.size() == 3 && it_d[1] == 8);
    }


    t_section(13, "无序删除");
    {
        dense<int> d;
        d.emplace_back(1);
        d.emplace_back(2);
        d.emplace_back(3);
        const size_t moved = d.erase_unordered(0);
        t_check("返回被移动的末下标", moved == 2);
        t_check("末尾元素搬到删除位", d.size() == 2 && d[0] == 3 && d[1] == 2);
        const size_t last_moved = d.erase_unordered(1);
        t_check("删除末位返回自身", last_moved == 1 && d.size() == 1 && d[0] == 3);
        t_check("越界删除不改动", d.erase_unordered(5) == d.size());
    }

    return t_end("dense ");
}
