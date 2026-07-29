// test_dense_perf.cpp - dense<T> 独立性能测试 (含视图接口), 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/dense.hpp"

using namespace std;

// === 测试组件 (POD4/POD12/POD32 来自 perf_common.hpp, 此处仅补 POD128) ===
struct POD128 { float a[32]; };

// POD4/POD32 比较运算符 (非成员, ADL 可见, 视图测试需要)
inline bool operator==(const POD4& a, const POD4& b) noexcept { return a.v == b.v; }
inline bool operator!=(const POD4& a, const POD4& b) noexcept { return a.v != b.v; }
inline bool operator<(const POD4& a, const POD4& b) noexcept { return a.v < b.v; }

inline bool operator==(const POD32& a, const POD32& b) noexcept
{
    for (int i = 0; i < 8; ++i)
    {
        if (a.a[i] != b.a[i]) { return false; }
    }
    return true;
}
inline bool operator!=(const POD32& a, const POD32& b) noexcept { return !(a == b); }
inline bool operator<(const POD32& a, const POD32& b) noexcept
{
    for (int i = 0; i < 8; ++i)
    {
        if (a.a[i] != b.a[i]) { return a.a[i] < b.a[i]; }
    }
    return false;
}

template <typename T>
static T make_value(uint32_t i) noexcept
{
    if constexpr (is_same_v<T, POD4>)  return {i};
    else if constexpr (is_same_v<T, POD12>) return {static_cast<float>(i), static_cast<float>(i + 1), static_cast<float>(i + 2)};
    else if constexpr (is_same_v<T, POD32>) { POD32 p; for (int k = 0; k < 8; ++k) p.a[k] = static_cast<float>(i + k); return p; }
    else if constexpr (is_same_v<T, POD128>) { POD128 p; for (int k = 0; k < 32; ++k) p.a[k] = static_cast<float>(i + k); return p; }
    else return T{};
}

// 从 T 提取一个 uint32_t 校验和, 用于阻止死代码消除
template <typename T>
static uint32_t checksum(const T& v) noexcept
{
    if constexpr (is_same_v<T, POD4>) return v.v;
    else if constexpr (is_same_v<T, POD12>) return static_cast<uint32_t>(v.x + v.y + v.z);
    else if constexpr (is_same_v<T, POD32>)
    {
        float s = 0;
        for (int i = 0; i < 8; ++i) s += v.a[i];
        return static_cast<uint32_t>(s);
    }
    else if constexpr (is_same_v<T, POD128>)
    {
        float s = 0;
        for (int i = 0; i < 32; ++i) s += v.a[i];
        return static_cast<uint32_t>(s);
    }
    else return 0;
}

// 性能阈值检查
static void perf_check(const char* label, double ns)
{
    if (ns > 10000.0)
    {
        cout << "    [性能检查] " << label << " 单次延迟 " << ns
             << " ns 超过 10000ns 阈值, 需关注\n";
    }
}

// === Section 1: 构造与赋值 ===
template <typename T>
static void test_construct(size_t n)
{
    print_header(("Section 1: construct (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 1.1 默认构造
    {
        double ns = best_ns(REPEAT, [&]() { dense<T> d; compiler_barrier(); return d.size(); });
        print_ns("default ctor", 1, ns);
    }

    // 1.2 预分配容量
    {
        double ns = best_ns(REPEAT, [&]() { dense<T> d(n); compiler_barrier(); return d.capacity(); });
        print_ns("reserve ctor", 1, ns);
    }

    // 1.3 count 个 value 副本
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() { dense<T> d(n, v); compiler_barrier(); return d.size(); });
        print_ns("count-value ctor", n, ns / static_cast<double>(n));
    }

    // 1.4 移动构造
    {
        dense<T> src(n);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> tmp = std::move(src);
            compiler_barrier();
            dense<T> r = std::move(tmp);
            return r.size();
        });
        print_ns("move ctor", 1, ns);
    }

    // 1.5 拷贝构造
    {
        dense<T> src(n);
        for (size_t i = 0; i < n; ++i) src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() { dense<T> d(src); compiler_barrier(); return d.size(); });
        print_ns("copy ctor", n, ns / static_cast<double>(n));
    }

    // 1.6 initializer_list 构造
    {
        // 仅测小数组
        double ns = best_ns(REPEAT, [&]() { dense<T> d{T{}, T{}, T{}}; compiler_barrier(); return d.size(); });
        print_ns("init_list ctor", 3, ns / 3.0);
    }

    print_footer();
}

// === Section 2: 元素访问 ===
template <typename T>
static void test_access(size_t n)
{
    print_header(("Section 2: access (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // 2.1 operator[]
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = d[opaque(i)]; }
            compiler_barrier();
            (void)sink;
        });
        print_ns("operator[]", n, ns / static_cast<double>(n));
    }

    // 2.2 get (含越界回退版)
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = d.get(opaque(i)); }
            compiler_barrier();
            (void)sink;
        });
        print_ns("get(idx)", n, ns / static_cast<double>(n));
    }

    // 2.3 get(idx, error_index) 越界回退版
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = d.get(opaque(i + n), 0); }
            compiler_barrier();
            (void)sink;
        });
        print_ns("get(idx, err_idx)", n, ns / static_cast<double>(n));
    }

    // 2.4 front / back
    {
        double ns = best_ns(REPEAT, [&]() {
            T f{}, b{};
            for (size_t i = 0; i < n; ++i) { f = d.front(); b = d.back(); }
            compiler_barrier();
            (void)f; (void)b;
        });
        print_ns("front/back", 2 * n, ns / static_cast<double>(2 * n));
    }

    // 2.5 data()
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = d.data();
            touch_ptr(p);
            return p;
        });
        print_ns("data()", 1, ns);
    }

    print_footer();
}

// === Section 3: 迭代器 ===
template <typename T>
static void test_iterator(size_t n)
{
    print_header(("Section 3: iterator (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // 3.1 begin/end 顺序遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = d.begin(); it != d.end(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("begin/end iter", n, ns / static_cast<double>(n));
    }

    // 3.2 cbegin/cend const 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = d.cbegin(); it != d.cend(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("cbegin/cend iter", n, ns / static_cast<double>(n));
    }

    // 3.3 for_each (DENSE_FLATTEN + ivdep)
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            d.for_each([&](T& v) { sink = v; });
            compiler_barrier();
            (void)sink;
        });
        print_ns("for_each (ivdep)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 4: 容量查询 ===
template <typename T>
static void test_capacity_query(size_t n)
{
    print_header(("Section 4: capacity query (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    const size_t OPS = 1000000;

    // 4.1 size() / capacity() / empty() / valid() / max_size()
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += d.size(); s += d.capacity(); s += d.empty() ? 1 : 0;
                s += d.valid() ? 1 : 0; s += d.max_size();
            }
            (void)s;
        });
        print_ns("size/cap/empty/valid/max", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 size_bytes / capacity_bytes / count / span
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += d.size_bytes(); s += d.capacity_bytes(); s += d.count();
                auto sp = d.span();
                s += sp.size();
            }
            (void)s;
        });
        print_ns("size_bytes/cap_bytes/count/span", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: 追加类操作 ===
template <typename T>
static void test_append(size_t n)
{
    print_header(("Section 5: append (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 5.1 push_back_unchecked (预分配容量)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("push_back_unchecked", n, ns / static_cast<double>(n));
    }

    // 5.2 push_back (拷贝, 带容量检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
            {
                T v = make_value<T>(static_cast<uint32_t>(i));
                d.push_back(v);
            }
            compiler_barrier();
            return d.size();
        });
        print_ns("push_back (copy)", n, ns / static_cast<double>(n));
    }

    // 5.3 push_back (移动)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
            {
                T v = make_value<T>(static_cast<uint32_t>(i));
                d.push_back(std::move(v));
            }
            compiler_barrier();
            return d.size();
        });
        print_ns("push_back (move)", n, ns / static_cast<double>(n));
    }

    // 5.4 push_back_unchecked (移动)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i)
            {
                T v = make_value<T>(static_cast<uint32_t>(i));
                d.push_back_unchecked(std::move(v));
            }
            compiler_barrier();
            return d.size();
        });
        print_ns("push_back_unchecked (move)", n, ns / static_cast<double>(n));
    }

    // 5.5 emplace_back_unchecked
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.emplace_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("emplace_back_unchecked", n, ns / static_cast<double>(n));
    }

    // 5.6 emplace_back (带容量检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.emplace_back(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("emplace_back", n, ns / static_cast<double>(n));
    }

    // 5.7 append_n (count 个相同值)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            d.append_n(n, v);
            compiler_barrier();
            return d.size();
        });
        print_ns("append_n", n, ns / static_cast<double>(n));
    }

    // 5.8 append_bulk (批量拷贝)
    {
        vector<T> src;
        src.reserve(n);
        for (size_t i = 0; i < n; ++i) src.push_back(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            d.append_bulk(src.data(), n);
            compiler_barrier();
            return d.size();
        });
        print_ns("append_bulk", n, ns / static_cast<double>(n));
    }

    // 5.9 append_bulk_move (批量移动)
    {
        double ns = best_ns(REPEAT, [&]() {
            vector<T> src; src.reserve(n);
            for (size_t i = 0; i < n; ++i) src.push_back(make_value<T>(static_cast<uint32_t>(i)));
            dense<T> d; d.increase_capacity(n);
            d.append_bulk_move(src.data(), n);
            compiler_barrier();
            return d.size();
        });
        print_ns("append_bulk_move", n, ns / static_cast<double>(n));
    }

    // 5.10 append_incrementing (要求 T 可从 uint64_t 赋值, 仅基础整数类型适用)
    // 单独在 main 中用 uint32_t 测试, 此处跳过
    (void)0;

    // 5.11 fill_bulk (区间填充, AVX2 广播)
    {
        T v = make_value<T>(42);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            d.fill_bulk(v, 0, n);
            compiler_barrier();
            return d.size();
        });
        print_ns("fill_bulk (AVX2 broadcast)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 6: 容量调整 ===
template <typename T>
static void test_increase_capacity(size_t n)
{
    print_header(("Section 6: increase_capacity (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 6.1 increase_capacity (扩容, 不初始化)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            compiler_barrier();
            return d.capacity();
        });
        print_ns("increase_capacity", 1, ns);
    }

    // 6.2 increase_capacity + fill
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            compiler_barrier();
            return d.capacity();
        });
        print_ns("increase_capacity+fill", n, ns / static_cast<double>(n));
    }

    // 6.3 reserve_exact
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.reserve_exact(n);
            compiler_barrier();
            return d.capacity();
        });
        print_ns("reserve_exact", 1, ns);
    }

    // 6.4 increase_capacity
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            compiler_barrier();
            return d.size();
        });
        print_ns("increase_capacity", n, ns / static_cast<double>(n));
    }

    // 6.5 shrink_to_fit
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            d.shrink_to_fit();
            compiler_barrier();
            return d.capacity();
        });
        print_ns("shrink_to_fit", 1, ns);
    }

    // 6.6 reduce_capacity
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            d.reduce_capacity(n / 2);
            compiler_barrier();
            return d.capacity();
        });
        print_ns("reduce_capacity", 1, ns);
    }

    // 6.7 reduce_capacity + dst (尾部搬运)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            dense<T> dst;
            d.reduce_capacity(n / 2, dst);
            compiler_barrier();
            return d.capacity() + dst.capacity();
        });
        print_ns("reduce_capacity+dst", 1, ns);
    }

    // 6.8 clear
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            d.clear();
            compiler_barrier();
            return d.size();
        });
        print_ns("clear", 1, ns);
    }

    print_footer();
}

// === Section 7: 插入/删除 ===
template <typename T>
static void test_insert_erase(size_t n)
{
    print_header(("Section 7: insert/erase (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 7.1 emplace (中间位置)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.emplace(d.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("emplace(begin)", n, ns / static_cast<double>(n));
    }

    // 7.2 insert (中间位置, const ref)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.insert(d.begin(), v);
            compiler_barrier();
            return d.size();
        });
        print_ns("insert(begin, const&)", n, ns / static_cast<double>(n));
    }

    // 7.3 insert (中间位置, rvalue)
    {
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) d.insert(d.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return d.size();
        });
        print_ns("insert(begin, &&)", n, ns / static_cast<double>(n));
    }

    // 7.4 erase (单点, 末尾)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            for (size_t i = 0; i < n; ++i) d.erase(d.begin() + (d.size() - 1));
            compiler_barrier();
            return d.size();
        });
        print_ns("erase(end-1)", n, ns / static_cast<double>(n));
    }

    // 7.5 erase (单点, 头部)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            for (size_t i = 0; i < n; ++i) d.erase(d.begin());
            compiler_barrier();
            return d.size();
        });
        print_ns("erase(begin)", n, ns / static_cast<double>(n));
    }

    // 7.6 erase (区间)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            d.erase(d.begin(), d.end());
            compiler_barrier();
            return d.size();
        });
        print_ns("erase(range)", n, ns / static_cast<double>(n));
    }

    // 7.7 pop_back
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> d; d.increase_capacity(n, v);
            for (size_t i = 0; i < n; ++i) d.pop_back();
            compiler_barrier();
            return d.size();
        });
        print_ns("pop_back", n, ns / static_cast<double>(n));
    }

    // 7.8 swap
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            dense<T> a; a.increase_capacity(n, v);
            dense<T> b; b.increase_capacity(n, v);
            a.swap(b);
            compiler_barrier();
            return a.size() + b.size();
        });
        print_ns("swap", 1, ns);
    }

    print_footer();
}

// === Section A: 子范围视图 (subspan/first/last/first_fixed/last_fixed) ===
template <typename T>
static void test_subrange_views(size_t n)
{
    print_header(("Section A: subrange views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // A.1 subspan(offset, count) 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            auto sp = d.subspan(n / 4, n / 2);
            uint32_t sum = 0;
            for (auto& v : sp) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("subspan(off, cnt)", n / 2, ns / static_cast<double>(n / 2));
        perf_check("subspan(off, cnt)", ns / static_cast<double>(n / 2));
    }

    // A.2 subspan(offset) 末段遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            auto sp = d.subspan(n / 2);
            uint32_t sum = 0;
            for (auto& v : sp) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("subspan(off) traverse", n / 2, ns / static_cast<double>(n / 2));
    }

    // A.3 first(n) / last(n) 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            auto f = d.first(n / 4);
            auto l = d.last(n / 4);
            uint32_t sum = 0;
            for (auto& v : f) { sum += checksum(v); }
            for (auto& v : l) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("first(n)/last(n) traverse", n / 2, ns / static_cast<double>(n / 2));
    }

    // A.4 first_fixed<N>() / last_fixed<N>() 遍历
    {
        constexpr size_t FN = 8;
        double ns = best_ns(REPEAT, [&]() {
            auto f = d.template first_fixed<FN>();
            auto l = d.template last_fixed<FN>();
            uint32_t sum = 0;
            for (auto& v : f) { sum += checksum(v); }
            for (auto& v : l) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("first_fixed<N>/last_fixed<N>", 2 * FN, ns / static_cast<double>(2 * FN));
    }

    print_footer();
}

// === Section B: 反向视图 (rbegin/rend/reverse_for_each) ===
template <typename T>
static void test_reverse_views(size_t n)
{
    print_header(("Section B: reverse views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // B.1 rbegin/rend 反向迭代
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for (auto it = d.rbegin(); it != d.rend(); ++it) { sum += checksum(*it); }
            return opaque(sum);
        });
        print_ns("rbegin/rend iter", n, ns / static_cast<double>(n));
    }

    // B.2 crbegin/crend const 反向迭代
    {
        const dense<T>& cd = d;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for (auto it = cd.crbegin(); it != cd.crend(); ++it) { sum += checksum(*it); }
            return opaque(sum);
        });
        print_ns("crbegin/crend iter", n, ns / static_cast<double>(n));
    }

    // B.3 reverse_for_each
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.reverse_for_each([&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("reverse_for_each", n, ns / static_cast<double>(n));
    }

    // B.4 reverse_for_each const
    {
        const dense<T>& cd = d;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            cd.reverse_for_each([&](const T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("reverse_for_each const", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section C: 步进视图 (strided_span_view/strided_for_each) ===
template <typename T>
static void test_strided_views(size_t n)
{
    print_header(("Section C: strided views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    const size_t step = 4;
    const size_t cnt = n / step;

    // C.1 strided_span_view + for_each
    {
        double ns = best_ns(REPEAT, [&]() {
            auto sv = d.strided_span_view(0, step, cnt);
            uint32_t sum = 0;
            sv.for_each([&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("strided_span_view+for_each", cnt, ns / static_cast<double>(cnt));
        perf_check("strided_span_view+for_each", ns / static_cast<double>(cnt));
    }

    // C.2 strided_for_each (运行时步长)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.strided_for_each(0, step, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("strided_for_each (rt step)", cnt, ns / static_cast<double>(cnt));
        perf_check("strided_for_each (rt step)", ns / static_cast<double>(cnt));
    }

    // C.3 strided_for_each const
    {
        const dense<T>& cd = d;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            cd.strided_for_each(0, step, [&](const T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("strided_for_each const", cnt, ns / static_cast<double>(cnt));
    }

    // C.4 strided_for_each<Step> (编译期步长)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.template strided_for_each<4>([&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("strided_for_each<4> (ct step)", cnt, ns / static_cast<double>(cnt));
    }

    // C.5 strided_for_each<1> (应回退到 for_each 快路径)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.template strided_for_each<1>([&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("strided_for_each<1> (fast path)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section D: 变换视图 (transform_for_each) ===
template <typename T>
static void test_transform_views(size_t n)
{
    print_header(("Section D: transform views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // D.1 transform_for_each (融合 transform + consume)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.transform_for_each(
                [](T& v) -> T { return v; },
                [&](T v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("transform_for_each", n, ns / static_cast<double>(n));
    }

    // D.2 transform_for_each const
    {
        const dense<T>& cd = d;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            cd.transform_for_each(
                [](const T& v) -> T { return v; },
                [&](T v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("transform_for_each const", n, ns / static_cast<double>(n));
    }

    // D.3 transform_to (写入目标)
    {
        dense<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            dst.clear();
            dst.increase_capacity(n);
            d.template transform_to<T>(dst.data(), n, [](const T& v) -> T { return v; });
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("transform_to", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section E: 过滤视图 (find/find_if/contains/count_if/filter_for_each) ===
template <typename T>
static void test_filter_views(size_t n)
{
    print_header(("Section E: filter views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    T target = make_value<T>(static_cast<uint32_t>(n / 2));

    // E.1 find (命中中段)
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = d.find(target);
            touch_ptr(p);
            return p;
        });
        print_ns("find (mid hit)", 1, ns);
        perf_check("find (mid hit)", ns);
    }

    // E.2 find (未命中)
    {
        uint32_t miss_seed = 0xFFFFFFFFu;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t seed = opaque(miss_seed);
            T miss = make_value<T>(seed);
            T* p = d.find(miss);
            touch_ptr(p);
            return p;
        });
        print_ns("find (miss)", 1, ns);
        perf_check("find (miss)", ns);
    }

    // E.3 contains
    {
        double ns = best_ns(REPEAT, [&]() {
            bool b = d.contains(target);
            return opaque(b);
        });
        print_ns("contains", 1, ns);
    }

    // E.4 find_if (命中中段)
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = d.find_if([&](const T& v) { return v == target; });
            touch_ptr(p);
            return p;
        });
        print_ns("find_if (mid hit)", 1, ns);
        perf_check("find_if (mid hit)", ns);
    }

    // E.5 find_if_not
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = d.find_if_not([&](const T& v) { return v != target; });
            touch_ptr(p);
            return p;
        });
        print_ns("find_if_not", 1, ns);
    }

    // E.6 count_if (统计偶数)
    {
        uint32_t idx = 0;
        double ns = best_ns(REPEAT, [&]() {
            idx = 0;
            size_t c = d.count_if([&](const T&) { return (idx++ & 1) == 0; });
            return opaque(static_cast<uint32_t>(c));
        });
        print_ns("count_if", n, ns / static_cast<double>(n));
    }

    // E.7 filter_for_each (全部命中)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.filter_for_each([](const T&) { return true; }, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("filter_for_each (all)", n, ns / static_cast<double>(n));
    }

    // E.8 filter_for_each (半数命中)
    {
        size_t idx = 0;
        double ns = best_ns(REPEAT, [&]() {
            idx = 0;
            uint32_t sum = 0;
            d.filter_for_each([&](const T&) { return (idx++ & 1) == 0; }, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("filter_for_each (half)", n / 2, ns / static_cast<double>(n / 2));
    }

    // E.9 filter_indices_to
    {
        dense<size_t> indices;
        indices.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            indices.clear();
            d.filter_indices_to(indices, [](const T&) { return true; });
            uint32_t sum = 0;
            for (size_t i = 0; i < indices.size(); ++i) { sum += static_cast<uint32_t>(indices[i]); }
            return opaque(sum);
        });
        print_ns("filter_indices_to", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section F: 规约/极值 (reduce/reduce_pairwise/min/max/minmax/sum/dot_product) ===
template <typename T>
static void test_reduction_views(size_t n)
{
    print_header(("Section F: reduction views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // F.1 reduce (顺序)
    {
        double ns = best_ns(REPEAT, [&]() {
            T r = d.reduce([](T acc, const T& v) -> T {
                if constexpr (is_same_v<T, POD4>) { return {acc.v + checksum(v)}; }
                else if constexpr (is_same_v<T, POD32>)
                {
                    POD32 out = acc;
                    for (int i = 0; i < 8; ++i) { out.a[i] += v.a[i]; }
                    return out;
                }
                else { return acc; }
            }, T{});
            return opaque(checksum(r));
        });
        print_ns("reduce (sequential)", n, ns / static_cast<double>(n));
    }

    // F.2 reduce_pairwise
    {
        double ns = best_ns(REPEAT, [&]() {
            T r = d.reduce_pairwise([](T acc, const T& v) -> T {
                if constexpr (is_same_v<T, POD4>) { return {acc.v + checksum(v)}; }
                else if constexpr (is_same_v<T, POD32>)
                {
                    POD32 out = acc;
                    for (int i = 0; i < 8; ++i) { out.a[i] += v.a[i]; }
                    return out;
                }
                else { return acc; }
            }, T{});
            return opaque(checksum(r));
        });
        print_ns("reduce_pairwise", n, ns / static_cast<double>(n));
    }

    // F.3 min_element
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = d.min_element();
            touch_ptr(p);
            return p;
        });
        print_ns("min_element", n, ns / static_cast<double>(n));
    }

    // F.4 max_element
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = d.max_element();
            touch_ptr(p);
            return p;
        });
        print_ns("max_element", n, ns / static_cast<double>(n));
    }

    // F.5 minmax_element
    {
        double ns = best_ns(REPEAT, [&]() {
            auto pr = d.minmax_element();
            touch_ptr(pr.first);
            touch_ptr(pr.second);
            return pr.first;
        });
        print_ns("minmax_element", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section F2: 算术专用 (sum/dot_product) ===
static void test_arithmetic_reduction(size_t n)
{
    print_header(("Section F2: arithmetic reduction (uint32_t, N=" + to_string(n) + ")").c_str());
    dense<uint32_t> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(static_cast<uint32_t>(i));

    dense<uint32_t> other;
    other.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) other.push_back_unchecked(static_cast<uint32_t>(i * 2));

    constexpr int REPEAT = 5;

    // F2.1 sum (ivdep)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t s = d.sum();
            return opaque(s);
        });
        print_ns("sum (uint32_t ivdep)", n, ns / static_cast<double>(n));
    }

    // F2.2 dot_product
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t s = d.dot_product(other.data(), n);
            return opaque(s);
        });
        print_ns("dot_product", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section G: 窗口/分块视图 (for_each_window/for_each_chunk/window_span/chunk_span) ===
template <typename T>
static void test_window_chunk_views(size_t n)
{
    print_header(("Section G: window/chunk views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    constexpr size_t WN = 4;

    // G.1 for_each_window<N> (滑动窗口)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.template for_each_window<WN>([&](std::span<T, WN> w) { sum += checksum(w[0]); });
            return opaque(sum);
        });
        print_ns("for_each_window<4>", n - WN + 1, ns / static_cast<double>(n - WN + 1));
    }

    // G.2 for_each_window<N> const
    {
        const dense<T>& cd = d;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            cd.template for_each_window<WN>([&](std::span<const T, WN> w) { sum += checksum(w[0]); });
            return opaque(sum);
        });
        print_ns("for_each_window<4> const", n - WN + 1, ns / static_cast<double>(n - WN + 1));
    }

    // G.3 for_each_chunk<N> (不重叠分块)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.template for_each_chunk<WN>([&](std::span<T, WN> c) { sum += checksum(c[0]); });
            return opaque(sum);
        });
        print_ns("for_each_chunk<4>", n / WN, ns / static_cast<double>(n / WN));
    }

    // G.4 window_span<N> (单次取窗口)
    {
        double ns = best_ns(REPEAT, [&]() {
            auto w = d.template window_span<WN>(n / 2);
            uint32_t sum = 0;
            for (auto& v : w) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("window_span<4>(offset)", WN, ns / static_cast<double>(WN));
    }

    // G.5 chunk_span<N> (单次取分块)
    {
        double ns = best_ns(REPEAT, [&]() {
            auto c = d.template chunk_span<WN>(n / (WN * 2));
            uint32_t sum = 0;
            for (auto& v : c) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("chunk_span<4>(idx)", WN, ns / static_cast<double>(WN));
    }

    print_footer();
}

// === Section H: 枚举视图 (for_each_enumerated) ===
template <typename T>
static void test_enumerated_views(size_t n)
{
    print_header(("Section H: enumerated views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // H.1 for_each_enumerated
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.for_each_enumerated([&](size_t i, T& v) { sum += checksum(v) + static_cast<uint32_t>(i); });
            return opaque(sum);
        });
        print_ns("for_each_enumerated", n, ns / static_cast<double>(n));
    }

    // H.2 for_each_enumerated const
    {
        const dense<T>& cd = d;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            cd.for_each_enumerated([&](size_t i, const T& v) { sum += checksum(v) + static_cast<uint32_t>(i); });
            return opaque(sum);
        });
        print_ns("for_each_enumerated const", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section I: 双容器同步 (for_each_zip/zip_with_to/equal) ===
template <typename T>
static void test_zip_views(size_t n)
{
    print_header(("Section I: zip views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> a, b;
    a.increase_capacity(n);
    b.increase_capacity(n);
    for (size_t i = 0; i < n; ++i)
    {
        a.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        b.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i * 2)));
    }

    constexpr int REPEAT = 5;

    // I.1 for_each_zip (裸指针)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            a.for_each_zip(b.data(), n, [&](T& x, T& y) { sum += checksum(x) + checksum(y); });
            return opaque(sum);
        });
        print_ns("for_each_zip (ptr)", n, ns / static_cast<double>(n));
    }

    // I.2 for_each_zip (dense&)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            a.for_each_zip(b, [&](T& x, T& y) { sum += checksum(x) + checksum(y); });
            return opaque(sum);
        });
        print_ns("for_each_zip (dense&)", n, ns / static_cast<double>(n));
    }

    // I.3 for_each_zip (span)
    {
        std::span<T> sp = b.span();
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            a.for_each_zip(sp, [&](T& x, T& y) { sum += checksum(x) + checksum(y); });
            return opaque(sum);
        });
        print_ns("for_each_zip (span)", n, ns / static_cast<double>(n));
    }

    // I.4 for_each_zip const
    {
        const dense<T>& ca = a;
        const dense<T>& cb = b;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            ca.for_each_zip(cb, [&](const T& x, const T& y) { sum += checksum(x) + checksum(y); });
            return opaque(sum);
        });
        print_ns("for_each_zip const", n, ns / static_cast<double>(n));
    }

    // I.5 zip_with_to
    {
        dense<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            dst.clear();
            dst.increase_capacity(n);
            T* dp = dst.data();
            T* bp = b.data();
            a.template zip_with_to<T, T>(dp, bp, n,
                [](const T& x, const T& y) -> T { return x; });
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("zip_with_to", n, ns / static_cast<double>(n));
    }

    // I.6 equal (true)
    {
        dense<T> c = a;
        double ns = best_ns(REPEAT, [&]() {
            bool r = a.equal(c);
            return opaque(r);
        });
        print_ns("equal (true)", n, ns / static_cast<double>(n));
    }

    // I.7 equal (false)
    {
        double ns = best_ns(REPEAT, [&]() {
            bool r = a.equal(b);
            return opaque(r);
        });
        print_ns("equal (false)", n, ns / static_cast<double>(n));
    }

    // I.8 equal (span)
    {
        std::span<const T> sp = a.span();
        double ns = best_ns(REPEAT, [&]() {
            bool r = a.equal(sp);
            return opaque(r);
        });
        print_ns("equal (span)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section J: SIMD/对齐视图 (aligned_data/aligned_span/simd_for_each/unaligned_tail_offset) ===
template <typename T>
static void test_simd_views(size_t n)
{
    print_header(("Section J: simd/aligned views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // J.1 aligned_data 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            T* p = d.aligned_data();
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(p[i]); }
            return opaque(sum);
        });
        print_ns("aligned_data traverse", n, ns / static_cast<double>(n));
    }

    // J.2 aligned_span 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            auto sp = d.aligned_span();
            uint32_t sum = 0;
            for (auto& v : sp) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("aligned_span traverse", n, ns / static_cast<double>(n));
    }

    // J.3 simd_for_each (仅 sizeof(T) <= 32 启用)
    if constexpr (sizeof(T) <= 32)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            d.simd_for_each([&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("simd_for_each", n, ns / static_cast<double>(n));
    }
    else
    {
        cout << "  simd_for_each                      skipped (sizeof(T) > 32)\n";
    }

    // J.4 simd_for_each const
    if constexpr (sizeof(T) <= 32)
    {
        const dense<T>& cd = d;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            cd.simd_for_each([&](const T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("simd_for_each const", n, ns / static_cast<double>(n));
    }

    // J.5 unaligned_tail_offset
    {
        double ns = best_ns(REPEAT, [&]() {
            size_t t = d.unaligned_tail_offset();
            return opaque(static_cast<uint32_t>(t));
        });
        print_ns("unaligned_tail_offset", 1, ns);
    }

    print_footer();
}

// === Section K: 拷贝/移动视图 (copy_to/move_to/reverse_copy_to) ===
template <typename T>
static void test_copy_move_views(size_t n)
{
    print_header(("Section K: copy/move views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    dense<T> d;
    d.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 3;

    // K.1 copy_to (SIMD 路径)
    {
        dense<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            d.copy_to(dst.data(), n);
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("copy_to (AVX2 path)", n, ns / static_cast<double>(n));
    }

    // K.2 copy_to (span)
    {
        dense<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            std::span<T> sp = dst.span();
            d.copy_to(sp);
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("copy_to (span)", n, ns / static_cast<double>(n));
    }

    // K.3 move_to
    {
        dense<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            d.move_to(dst.data(), n);
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("move_to", n, ns / static_cast<double>(n));
    }

    // K.4 reverse_copy_to
    {
        dense<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            d.reverse_copy_to(dst.data(), n);
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("reverse_copy_to", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  dense<T> 独立性能测试 (含视图接口)\n";
    cout << "  覆盖: 1-8 基础接口 | A 子范围 | B 反向 | C 步进 | D 变换\n";
    cout << "        E 过滤 | F 规约 | G 窗口/分块 | H 枚举 | I zip\n";
    cout << "        J SIMD | K 拷贝/移动\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K

    test_construct<POD4>(N);
    test_construct<POD12>(N);
    test_construct<POD32>(N);

    test_access<POD4>(N);
    test_access<POD32>(N);

    test_iterator<POD4>(N);
    test_iterator<POD32>(N);

    test_capacity_query<POD4>(N);

    test_append<POD4>(N);
    test_append<POD12>(N);
    test_append<POD32>(N);
    test_append<POD128>(N);

    test_increase_capacity<POD4>(N);
    test_increase_capacity<POD32>(N);

    test_insert_erase<POD4>(N);
    test_insert_erase<POD32>(N);

    // append_incrementing 专用测试 (要求 T 可从 uint64_t 赋值)
    {
        print_header("Section 8: append_incrementing (uint32_t)");
        constexpr int REPEAT = 3;
        const size_t n = N;
        uint64_t counter = 0;
        double ns = best_ns(REPEAT, [&]() {
            dense<uint32_t> d; d.increase_capacity(n);
            d.append_incrementing(n, counter);
            compiler_barrier();
            return d.size();
        });
        print_ns("append_incrementing<uint32_t>", n, ns / static_cast<double>(n));
        print_footer();
    }

    // === 视图接口测试 ===
    test_subrange_views<POD4>(N);
    test_subrange_views<POD32>(N);

    test_reverse_views<POD4>(N);
    test_reverse_views<POD32>(N);

    test_strided_views<POD4>(N);
    test_strided_views<POD32>(N);

    test_transform_views<POD4>(N);
    test_transform_views<POD32>(N);

    test_filter_views<POD4>(N);
    test_filter_views<POD32>(N);

    test_reduction_views<POD4>(N);
    test_reduction_views<POD32>(N);

    test_arithmetic_reduction(N);

    test_window_chunk_views<POD4>(N);
    test_window_chunk_views<POD32>(N);

    test_enumerated_views<POD4>(N);
    test_enumerated_views<POD32>(N);

    test_zip_views<POD4>(N);
    test_zip_views<POD32>(N);

    test_simd_views<POD4>(N);
    test_simd_views<POD32>(N);

    test_copy_move_views<POD4>(N);
    test_copy_move_views<POD32>(N);

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
