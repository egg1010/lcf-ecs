// test_class_pool_perf.cpp - class_pool<T> 独立性能测试 (含视图接口), 使用 time.hpp
#include "perf_common.hpp"
#include "include/part/class_pool.hpp"
#include "include/part/class_pool_views.hpp"

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
        double ns = best_ns(REPEAT, [&]() { class_pool<T> p; compiler_barrier(); return p.size(); });
        print_ns("default ctor", 1, ns);
    }

    // 1.2 预分配容量
    {
        double ns = best_ns(REPEAT, [&]() { class_pool<T> p(n); compiler_barrier(); return p.capacity(); });
        print_ns("reserve ctor", 1, ns);
    }

    // 1.3 count 个 value 副本
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() { class_pool<T> p(n, v); compiler_barrier(); return p.size(); });
        print_ns("count-value ctor", n, ns / static_cast<double>(n));
    }

    // 1.4 拷贝构造
    {
        class_pool<T> src(n);
        for (size_t i = 0; i < n; ++i) src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() { class_pool<T> p(src); compiler_barrier(); return p.size(); });
        print_ns("copy ctor", n, ns / static_cast<double>(n));
    }

    // 1.5 移动构造
    {
        class_pool<T> src(n);
        for (size_t i = 0; i < n; ++i) src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> tmp = std::move(src);
            compiler_barrier();
            class_pool<T> r = std::move(tmp);
            return r.size();
        });
        print_ns("move ctor", 1, ns);
    }

    print_footer();
}

// === Section 2: 元素访问 ===
template <typename T>
static void test_access(size_t n)
{
    print_header(("Section 2: access (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // 2.1 operator[]
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = p[opaque(i)]; }
            compiler_barrier();
            (void)sink;
        });
        print_ns("operator[]", n, ns / static_cast<double>(n));
    }

    // 2.2 get(idx)
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = p.get(opaque(i)); }
            compiler_barrier();
            (void)sink;
        });
        print_ns("get(idx)", n, ns / static_cast<double>(n));
    }

    // 2.3 get(idx, err_idx) 越界回退
    {
        double ns = best_ns(REPEAT, [&]() {
            T sink{};
            for (size_t i = 0; i < n; ++i) { sink = p.get(opaque(i + n), 0); }
            compiler_barrier();
            (void)sink;
        });
        print_ns("get(idx, err_idx)", n, ns / static_cast<double>(n));
    }

    // 2.4 front / back
    {
        double ns = best_ns(REPEAT, [&]() {
            T f{}, b{};
            for (size_t i = 0; i < n; ++i) { f = p.front(); b = p.back(); }
            compiler_barrier();
            (void)f; (void)b;
        });
        print_ns("front/back", 2 * n, ns / static_cast<double>(2 * n));
    }

    // 2.5 data()
    {
        double ns = best_ns(REPEAT, [&]() {
            T* ptr = p.data();
            touch_ptr(ptr);
            return ptr;
        });
        print_ns("data()", 1, ns);
    }

    print_footer();
}

// === Section 3: 迭代器 (含稀疏模式) ===
template <typename T>
static void test_iterator(size_t n)
{
    print_header(("Section 3: iterator (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // 3.1 begin/end 顺序遍历 (稠密模式)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = p.begin(); it != p.end(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("begin/end iter (dense)", n, ns / static_cast<double>(n));
    }

    // 3.2 cbegin/cend
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = p.cbegin(); it != p.cend(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("cbegin/cend iter", n, ns / static_cast<double>(n));
    }

    // 3.3 rbegin/rend 反向遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = p.rbegin(); it != p.rend(); ++it) { sink += opaque(0); touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("rbegin/rend iter", n, ns / static_cast<double>(n));
    }

    // 3.4 稀疏模式遍历 (隔一个删一个)
    {
        class_pool<T> ps(n);
        for (size_t i = 0; i < n; ++i) ps.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        for (size_t i = 0; i < n; i += 2) ps.soft_sparse_delete(i);
        size_t live = n / 2;

        double ns = best_ns(REPEAT, [&]() {
            volatile size_t sink = 0;
            for (auto it = ps.begin(); it != ps.end(); ++it) { sink += 1; touch_ptr(&*it); }
            (void)sink;
        });
        print_ns("begin/end iter (sparse 50%)", live, ns / static_cast<double>(live));
    }

    print_footer();
}

// === Section 4: 容量查询 ===
template <typename T>
static void test_capacity_query(size_t n)
{
    print_header(("Section 4: capacity query (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    const size_t OPS = 1000000;

    // 4.1 size / capacity / empty / valid (class_pool 无 max_size)
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += p.size(); s += p.capacity(); s += p.empty() ? 1 : 0;
                s += p.valid() ? 1 : 0;
            }
            (void)s;
        });
        print_ns("size/cap/empty/valid", OPS, ns / static_cast<double>(OPS));
    }

    // 4.2 size_bytes / capacity_bytes / count / span
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += p.size_bytes(); s += p.capacity_bytes();
                s += p.count();
                auto sp = p.span();
                s += sp.size();
            }
            (void)s;
        });
        print_ns("size_bytes/cap_bytes/count/span", OPS, ns / static_cast<double>(OPS));
    }

    // 4.3 is_dense / is_constructed_at / sparse_capacity
    {
        double ns = best_ns(REPEAT, [&]() {
            volatile size_t s = 0;
            for (size_t i = 0; i < OPS; ++i)
            {
                s += p.is_dense() ? 1 : 0;
                s += p.is_constructed_at(opaque(static_cast<size_t>(i % n))) ? 1 : 0;
                s += p.sparse_capacity();
            }
            (void)s;
        });
        print_ns("is_dense/is_constructed_at/sparse_cap", OPS, ns / static_cast<double>(OPS));
    }

    print_footer();
}

// === Section 5: 追加类操作 ===
template <typename T>
static void test_append(size_t n)
{
    print_header(("Section 5: append (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 5.1 push_back (带容量检查 + count_cache 维护)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.push_back(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("push_back", n, ns / static_cast<double>(n));
    }

    // 5.2 push_back(T&&) 移动追加 (带容量检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.push_back(std::move(make_value<T>(static_cast<uint32_t>(i))));
            compiler_barrier();
            return p.size();
        });
        print_ns("push_back(T&&)", n, ns / static_cast<double>(n));
    }

    // 5.3 push_back_unchecked
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("push_back_unchecked", n, ns / static_cast<double>(n));
    }

    // 5.4 push_back_unchecked(T&&) 移动追加
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.push_back_unchecked(std::move(make_value<T>(static_cast<uint32_t>(i))));
            compiler_barrier();
            return p.size();
        });
        print_ns("push_back_unchecked(T&&)", n, ns / static_cast<double>(n));
    }

    // 5.5 emplace_back_unchecked
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.emplace_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("emplace_back_unchecked", n, ns / static_cast<double>(n));
    }

    // 5.6 emplace_back (带容量检查)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.emplace_back(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("emplace_back", n, ns / static_cast<double>(n));
    }

    // 5.7 append_n
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            p.append_n(n, v);
            compiler_barrier();
            return p.size();
        });
        print_ns("append_n", n, ns / static_cast<double>(n));
    }

    // 5.8 fill_bulk (区间填充, AVX2 广播)
    {
        T v = make_value<T>(42);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            p.fill_bulk(v, 0, n);
            compiler_barrier();
            return p.size();
        });
        print_ns("fill_bulk (AVX2)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section 6: 容量调整 ===
template <typename T>
static void test_increase_capacity(size_t n)
{
    print_header(("Section 6: increase_capacity (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 6.1 increase_capacity
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            compiler_barrier();
            return p.capacity();
        });
        print_ns("increase_capacity", 1, ns);
    }

    // 6.2 increase_capacity + fill
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            compiler_barrier();
            return p.capacity();
        });
        print_ns("increase_capacity+fill", n, ns / static_cast<double>(n));
    }

    // 6.3 reserve_exact
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.reserve_exact(n);
            compiler_barrier();
            return p.capacity();
        });
        print_ns("reserve_exact", 1, ns);
    }

    // 6.4 increase_capacity
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            compiler_barrier();
            return p.size();
        });
        print_ns("increase_capacity", n, ns / static_cast<double>(n));
    }

    // 6.5 shrink_to_fit
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            p.shrink_to_fit();
            compiler_barrier();
            return p.capacity();
        });
        print_ns("shrink_to_fit", 1, ns);
    }

    // 6.6 reduce_capacity
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            p.reduce_capacity(n / 2);
            compiler_barrier();
            return p.capacity();
        });
        print_ns("reduce_capacity", 1, ns);
    }

    // 6.7 reduce_capacity + dst
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            class_pool<T> dst;
            p.reduce_capacity(n / 2, dst);
            compiler_barrier();
            return p.capacity() + dst.capacity();
        });
        print_ns("reduce_capacity+dst", 1, ns);
    }

    // 6.8 clear
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            p.clear();
            compiler_barrier();
            return p.size();
        });
        print_ns("clear", 1, ns);
    }

    print_footer();
}

// === Section 7: 稀疏专用接口 ===
template <typename T>
static void test_sparse(size_t n)
{
    print_header(("Section 7: sparse (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 7.1 emplace_at (原位构造)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.emplace_at(i, make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("emplace_at", n, ns / static_cast<double>(n));
    }

    // 7.2 sparse_emplace_at
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.sparse_emplace_at(i, make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("sparse_emplace_at", n, ns / static_cast<double>(n));
    }

    // 7.3 sparse_erase_at (稀疏擦除)
    {
        class_pool<T> p(n);
        for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> tmp; tmp.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) tmp.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < n; ++i) tmp.sparse_erase_at(i);
            compiler_barrier();
            return tmp.size();
        });
        print_ns("sparse_erase_at", n, ns / static_cast<double>(n));
    }

    // 7.4 soft_sparse_delete (软删除)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < n; ++i) p.soft_sparse_delete(i);
            compiler_barrier();
            return p.size();
        });
        print_ns("soft_sparse_delete", n, ns / static_cast<double>(n));
    }

    // 7.5 soft_dense_delete (区间软删除)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
            p.soft_dense_delete(0, n);
            compiler_barrier();
            return p.size();
        });
        print_ns("soft_dense_delete(range)", n, ns / static_cast<double>(n));
    }

    // 7.6 fill_the_hole (填洞或追加)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.fill_the_hole(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("fill_the_hole", n, ns / static_cast<double>(n));
    }

    // 7.7 fill_the_hole_at (返回索引)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.fill_the_hole_at(make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("fill_the_hole_at", n, ns / static_cast<double>(n));
    }

    // 7.8 prepare_dense (预备稠密段)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            p.prepare_dense(n);
            compiler_barrier();
            return p.size();
        });
        print_ns("prepare_dense", 1, ns);
    }

    // 7.9 invalidate_count_cache
    {
        class_pool<T> p(n);
        for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        const size_t OPS = 1000000;
        double ns = best_ns(REPEAT, [&]() {
            for (size_t i = 0; i < OPS; ++i) p.invalidate_count_cache();
            compiler_barrier();
            return OPS;
        });
        print_ns("invalidate_count_cache", OPS, ns / static_cast<double>(OPS));
    }

    // 7.10 count (缓存失效后重算)
    {
        class_pool<T> p(n);
        for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        p.invalidate_count_cache();
        double ns = best_ns(REPEAT, [&]() {
            p.invalidate_count_cache();
            size_t c = p.count();
            compiler_barrier();
            return c;
        });
        print_ns("count (cache miss)", 1, ns);
    }

    print_footer();
}

// === Section 8: 插入/删除 (中间位置) ===
template <typename T>
static void test_insert_erase(size_t n)
{
    print_header(("Section 8: insert/erase (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    constexpr int REPEAT = 3;

    // 8.1 emplace (中间位置)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.emplace(p.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("emplace(begin)", n, ns / static_cast<double>(n));
    }

    // 8.2 insert (中间位置, const ref)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.insert(p.begin(), v);
            compiler_barrier();
            return p.size();
        });
        print_ns("insert(begin, const&)", n, ns / static_cast<double>(n));
    }

    // 8.3 insert (中间位置, rvalue)
    {
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n);
            for (size_t i = 0; i < n; ++i) p.insert(p.begin(), make_value<T>(static_cast<uint32_t>(i)));
            compiler_barrier();
            return p.size();
        });
        print_ns("insert(begin, &&)", n, ns / static_cast<double>(n));
    }

    // 8.4 erase (单点, 末尾) — class_pool iterator 不支持 operator+, 用 pop_back 等价
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            for (size_t i = 0; i < n; ++i) p.pop_back();
            compiler_barrier();
            return p.size();
        });
        print_ns("erase(end-1) [pop_back]", n, ns / static_cast<double>(n));
    }

    // 8.5 erase (单点, 头部)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            for (size_t i = 0; i < n; ++i) p.erase(p.begin());
            compiler_barrier();
            return p.size();
        });
        print_ns("erase(begin)", n, ns / static_cast<double>(n));
    }

    // 8.6 erase (区间)
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            p.erase(p.begin(), p.end());
            compiler_barrier();
            return p.size();
        });
        print_ns("erase(range)", n, ns / static_cast<double>(n));
    }

    // 8.7 pop_back
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> p; p.increase_capacity(n, v);
            for (size_t i = 0; i < n; ++i) p.pop_back();
            compiler_barrier();
            return p.size();
        });
        print_ns("pop_back", n, ns / static_cast<double>(n));
    }

    // 8.8 swap
    {
        T v = make_value<T>(0);
        double ns = best_ns(REPEAT, [&]() {
            class_pool<T> a; a.increase_capacity(n, v);
            class_pool<T> b; b.increase_capacity(n, v);
            a.swap(b);
            compiler_barrier();
            return a.size() + b.size();
        });
        print_ns("swap", 1, ns);
    }

    print_footer();
}

// === Section A: 子范围视图 ===
template <typename T>
static void test_subrange_views(size_t n)
{
    print_header(("Section A: subrange views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // A.1 subspan(offset, count) 遍历 (与 dense::subspan 命名一致)
    {
        double ns = best_ns(REPEAT, [&]() {
            auto sp = subspan(p, n / 4, n / 2);
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
            auto sp = subspan(p, n / 2);
            uint32_t sum = 0;
            for (auto& v : sp) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("subspan(off) traverse", n / 2, ns / static_cast<double>(n / 2));
    }

    // A.3 first(n) / last(n) 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            auto f = first(p, n / 4);
            auto l = last(p, n / 4);
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
            auto f = first_fixed<T, FN>(p);
            auto l = last_fixed<T, FN>(p);
            uint32_t sum = 0;
            for (auto& v : f) { sum += checksum(v); }
            for (auto& v : l) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("first_fixed<N>/last_fixed<N>", 2 * FN, ns / static_cast<double>(2 * FN));
    }

    print_footer();
}

// === Section B: 反向视图 ===
template <typename T>
static void test_reverse_views(size_t n)
{
    print_header(("Section B: reverse views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // B.1 rbegin/rend 反向迭代
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for (auto it = p.rbegin(); it != p.rend(); ++it) { sum += checksum(*it); }
            return opaque(sum);
        });
        print_ns("rbegin/rend iter", n, ns / static_cast<double>(n));
    }

    // B.2 crbegin/crend const 反向迭代
    {
        const class_pool<T>& cp = p;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for (auto it = cp.crbegin(); it != cp.crend(); ++it) { sum += checksum(*it); }
            return opaque(sum);
        });
        print_ns("crbegin/crend iter", n, ns / static_cast<double>(n));
    }

    // B.3 reverse_for_each
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            reverse_for_each(p, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("reverse_for_each", n, ns / static_cast<double>(n));
    }

    // B.4 reverse_for_each const
    {
        const class_pool<T>& cp = p;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            reverse_for_each(cp, [&](const T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("reverse_for_each const", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section C: 步进视图 ===
template <typename T>
static void test_strided_views(size_t n)
{
    print_header(("Section C: strided views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    const size_t step = 4;
    const size_t cnt = n / step;

    // C.1 strided_span_view + for_each
    {
        double ns = best_ns(REPEAT, [&]() {
            auto sv = strided_span_view(p, 0, step, cnt);
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
            strided_for_each(p, 0, step, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("strided_for_each (rt step)", cnt, ns / static_cast<double>(cnt));
        perf_check("strided_for_each (rt step)", ns / static_cast<double>(cnt));
    }

    // C.3 strided_for_each const
    {
        const class_pool<T>& cp = p;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            strided_for_each(cp, 0, step, [&](const T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("strided_for_each const", cnt, ns / static_cast<double>(cnt));
    }

    // C.4 strided_for_each<Step> (编译期步长)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            strided_for_each<T, 4>(p, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("strided_for_each<4> (ct step)", cnt, ns / static_cast<double>(cnt));
    }

    // C.5 strided_for_each<1> (快路径退化)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            strided_for_each<T, 1>(p, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("strided_for_each<1> (fast path)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section D: 变换视图 ===
template <typename T>
static void test_transform_views(size_t n)
{
    print_header(("Section D: transform views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // D.1 transform_for_each (融合 transform + consume)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            transform_for_each(
                p,
                [](T& v) -> T { return v; },
                [&](T v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("transform_for_each", n, ns / static_cast<double>(n));
    }

    // D.2 transform_for_each const
    {
        const class_pool<T>& cp = p;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            transform_for_each(
                cp,
                [](const T& v) -> T { return v; },
                [&](T v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("transform_for_each const", n, ns / static_cast<double>(n));
    }

    // D.3 transform_to (写入目标)
    {
        class_pool<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            transform_to<T, T>(p, dst.data(), n, [](const T& v) -> T { return v; });
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("transform_to", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section E: 过滤与查找 ===
template <typename T>
static void test_filter_views(size_t n)
{
    print_header(("Section E: filter views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    T target = make_value<T>(static_cast<uint32_t>(n / 2));

    // E.1 find (命中中段)
    {
        double ns = best_ns(REPEAT, [&]() {
            T* r = find(p, target);
            touch_ptr(r);
            return r;
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
            T* r = find(p, miss);
            return r;
        });
        print_ns("find (miss)", 1, ns);
        perf_check("find (miss)", ns);
    }

    // E.3 contains
    {
        double ns = best_ns(REPEAT, [&]() {
            bool b = contains(p, target);
            return opaque(b);
        });
        print_ns("contains", 1, ns);
    }

    // E.4 find_if (命中中段)
    {
        double ns = best_ns(REPEAT, [&]() {
            T* r = find_if(p, [&](const T& v) { return v == target; });
            touch_ptr(r);
            return r;
        });
        print_ns("find_if (mid hit)", 1, ns);
        perf_check("find_if (mid hit)", ns);
    }

    // E.5 find_if_not
    {
        double ns = best_ns(REPEAT, [&]() {
            T* r = find_if_not(p, [&](const T& v) { return v != target; });
            touch_ptr(r);
            return r;
        });
        print_ns("find_if_not", 1, ns);
    }

    // E.6 count_if
    {
        uint32_t idx = 0;
        double ns = best_ns(REPEAT, [&]() {
            idx = 0;
            size_t c = count_if(p, [&](const T&) { return (idx++ & 1) == 0; });
            return opaque(static_cast<uint32_t>(c));
        });
        print_ns("count_if", n, ns / static_cast<double>(n));
    }

    // E.7 filter_for_each (全部命中)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            filter_for_each(p, [](const T&) { return true; }, [&](T& v) { sum += checksum(v); });
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
            filter_for_each(p, [&](const T&) { return (idx++ & 1) == 0; }, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("filter_for_each (half)", n / 2, ns / static_cast<double>(n / 2));
    }

    // E.9 filter_indices_to
    {
        class_pool<size_t> indices;
        indices.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            indices.clear();
            filter_indices_to(p, indices, [](const T&) { return true; });
            uint32_t sum = 0;
            for (size_t i = 0; i < indices.size(); ++i) { sum += static_cast<uint32_t>(indices[i]); }
            return opaque(sum);
        });
        print_ns("filter_indices_to", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section F: 规约与极值 ===
template <typename T>
static void test_reduction_views(size_t n)
{
    print_header(("Section F: reduction views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // F.1 reduce (顺序)
    {
        double ns = best_ns(REPEAT, [&]() {
            T r = reduce(p, [](T acc, const T& v) -> T {
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
            T r = reduce_pairwise(p, [](T acc, const T& v) -> T {
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
            T* r = min_element(p);
            touch_ptr(r);
            return r;
        });
        print_ns("min_element", n, ns / static_cast<double>(n));
    }

    // F.4 max_element
    {
        double ns = best_ns(REPEAT, [&]() {
            T* r = max_element(p);
            touch_ptr(r);
            return r;
        });
        print_ns("max_element", n, ns / static_cast<double>(n));
    }

    // F.5 minmax_element
    {
        double ns = best_ns(REPEAT, [&]() {
            auto pr = minmax_element(p);
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
    class_pool<uint32_t> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(static_cast<uint32_t>(i));

    class_pool<uint32_t> other;
    other.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) other.push_back_unchecked(static_cast<uint32_t>(i * 2));

    constexpr int REPEAT = 5;

    // F2.1 sum (ivdep)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t s = sum(p);
            return opaque(s);
        });
        print_ns("sum (uint32_t ivdep)", n, ns / static_cast<double>(n));
    }

    // F2.2 dot_product
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t s = dot_product(p, other.data(), n);
            return opaque(s);
        });
        print_ns("dot_product", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section G: 窗口与分块 ===
template <typename T>
static void test_window_chunk_views(size_t n)
{
    print_header(("Section G: window/chunk views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;
    constexpr size_t WN = 4;

    // G.1 for_each_window<N> (滑动窗口)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for_each_window<T, WN>(p, [&](std::span<T, WN> w) { sum += checksum(w[0]); });
            return opaque(sum);
        });
        print_ns("for_each_window<4>", n - WN + 1, ns / static_cast<double>(n - WN + 1));
    }

    // G.2 for_each_window<N> const
    {
        const class_pool<T>& cp = p;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for_each_window<T, WN>(cp, [&](std::span<const T, WN> w) { sum += checksum(w[0]); });
            return opaque(sum);
        });
        print_ns("for_each_window<4> const", n - WN + 1, ns / static_cast<double>(n - WN + 1));
    }

    // G.3 for_each_chunk<N> (不重叠分块)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for_each_chunk<T, WN>(p, [&](std::span<T, WN> c) { sum += checksum(c[0]); });
            return opaque(sum);
        });
        print_ns("for_each_chunk<4>", n / WN, ns / static_cast<double>(n / WN));
    }

    // G.4 window_span<N>
    {
        double ns = best_ns(REPEAT, [&]() {
            auto w = window_span<T, WN>(p, n / 2);
            uint32_t sum = 0;
            for (auto& v : w) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("window_span<4>(offset)", WN, ns / static_cast<double>(WN));
    }

    // G.5 chunk_span<N>
    {
        double ns = best_ns(REPEAT, [&]() {
            auto c = chunk_span<T, WN>(p, n / (WN * 2));
            uint32_t sum = 0;
            for (auto& v : c) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("chunk_span<4>(idx)", WN, ns / static_cast<double>(WN));
    }

    print_footer();
}

// === Section H: 枚举视图 ===
template <typename T>
static void test_enumerated_views(size_t n)
{
    print_header(("Section H: enumerated views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // H.1 for_each_enumerated
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for_each_enumerated(p, [&](size_t i, T& v) { sum += checksum(v) + static_cast<uint32_t>(i); });
            return opaque(sum);
        });
        print_ns("for_each_enumerated", n, ns / static_cast<double>(n));
    }

    // H.2 for_each_enumerated const
    {
        const class_pool<T>& cp = p;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for_each_enumerated(cp, [&](size_t i, const T& v) { sum += checksum(v) + static_cast<uint32_t>(i); });
            return opaque(sum);
        });
        print_ns("for_each_enumerated const", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section I: 双容器同步 ===
template <typename T>
static void test_zip_views(size_t n)
{
    print_header(("Section I: zip views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> a, b;
    a.increase_capacity(n);
    b.increase_capacity(n);
    for (size_t i = 0; i < n; ++i)
    {
        a.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        b.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i * 2)));
    }

    constexpr int REPEAT = 5;

    // I.1 for_each_zip (dense&)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for_each_zip(a, b, [&](T& x, T& y) { sum += checksum(x) + checksum(y); });
            return opaque(sum);
        });
        print_ns("for_each_zip (pool&)", n, ns / static_cast<double>(n));
    }

    // I.2 for_each_zip (ptr)
    {
        T* bp = b.data();
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for_each_zip(a, bp, n, [&](T& x, T& y) { sum += checksum(x) + checksum(y); });
            return opaque(sum);
        });
        print_ns("for_each_zip (ptr)", n, ns / static_cast<double>(n));
    }

    // I.3 for_each_zip const
    {
        const class_pool<T>& ca = a;
        const class_pool<T>& cb = b;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for_each_zip(ca, cb, [&](const T& x, const T& y) { sum += checksum(x) + checksum(y); });
            return opaque(sum);
        });
        print_ns("for_each_zip const", n, ns / static_cast<double>(n));
    }

    // I.4 zip_with_to
    {
        class_pool<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            zip_with_to<T, T, T>(a, b.data(), dst.data(), n,
                [](const T& x, const T& y) -> T { (void)y; return x; });
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("zip_with_to", n, ns / static_cast<double>(n));
    }

    // I.5 equal (true)
    {
        class_pool<T> c = a;
        double ns = best_ns(REPEAT, [&]() {
            bool r = equal(a, c);
            return opaque(r);
        });
        print_ns("equal (true)", n, ns / static_cast<double>(n));
    }

    // I.6 equal (false)
    {
        double ns = best_ns(REPEAT, [&]() {
            bool r = equal(a, b);
            return opaque(r);
        });
        print_ns("equal (false)", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section J: SIMD/对齐视图 ===
template <typename T>
static void test_simd_views(size_t n)
{
    print_header(("Section J: simd/aligned views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 5;

    // J.1 aligned_data 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            T* ptr = aligned_data(p);
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(ptr[i]); }
            return opaque(sum);
        });
        print_ns("aligned_data traverse", n, ns / static_cast<double>(n));
    }

    // J.2 aligned_span 遍历
    {
        double ns = best_ns(REPEAT, [&]() {
            auto sp = aligned_span(p);
            uint32_t sum = 0;
            for (auto& v : sp) { sum += checksum(v); }
            return opaque(sum);
        });
        print_ns("aligned_span traverse", n, ns / static_cast<double>(n));
    }

    // J.3 simd_for_each (仅 sizeof <= 32)
    if constexpr (sizeof(T) <= 32)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            simd_for_each(p, [&](T& v) { sum += checksum(v); });
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
        const class_pool<T>& cp = p;
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            simd_for_each(cp, [&](const T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        print_ns("simd_for_each const", n, ns / static_cast<double>(n));
    }

    // J.5 unaligned_tail_offset
    {
        double ns = best_ns(REPEAT, [&]() {
            size_t t = unaligned_tail_offset(p);
            return opaque(static_cast<uint32_t>(t));
        });
        print_ns("unaligned_tail_offset", 1, ns);
    }

    print_footer();
}

// === Section K: 拷贝/移动/压缩视图 ===
template <typename T>
static void test_copy_move_views(size_t n)
{
    print_header(("Section K: copy/move views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    constexpr int REPEAT = 3;

    // K.1 copy_to (SIMD 路径)
    {
        class_pool<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            copy_to(p, dst.data(), n);
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("copy_to (SIMD path)", n, ns / static_cast<double>(n));
    }

    // K.2 copy_to (span)
    {
        class_pool<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            std::span<T> sp(dst.data(), n);
            copy_to(p, sp);
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("copy_to (span)", n, ns / static_cast<double>(n));
    }

    // K.3 move_to
    {
        class_pool<T> src;
        src.increase_capacity(n);
        for (size_t i = 0; i < n; ++i) src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
        class_pool<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            move_to(src, dst.data(), n);
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("move_to", n, ns / static_cast<double>(n));
    }

    // K.4 reverse_copy_to
    {
        class_pool<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            reverse_copy_to(p, dst.data(), n);
            uint32_t sum = 0;
            for (size_t i = 0; i < n; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        print_ns("reverse_copy_to", n, ns / static_cast<double>(n));
    }

    print_footer();
}

// === Section L: class_pool 独有 - 稀疏模式视图 ===
template <typename T>
static void test_sparse_views(size_t n)
{
    print_header(("Section L: sparse mode views (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
    class_pool<T> p;
    p.increase_capacity(n);
    for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));

    // 制造 25% 空洞 (稀疏模式)
    for (size_t i = 0; i < n; i += 4)
    {
        p.sparse_erase_at(i);
    }

    constexpr int REPEAT = 5;

    // L.1 稀疏模式基本统计
    {
        cout << "  [稀疏模式] size=" << p.size()
             << " count=" << p.count()
             << " holes=" << holes_count(p)
             << " is_dense=" << p.is_dense() << "\n";
    }

    // L.2 稀疏 for_each (位图扫描)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            for (auto it = p.begin(); it != p.end(); ++it) { sum += checksum(*it); }
            return opaque(sum);
        });
        const size_t live = p.count();
        print_ns("sparse for_each (begin/end)", live, live > 0 ? ns / static_cast<double>(live) : 0);
    }

    // L.3 稀疏 simd_for_each (退化为 for_each)
    if constexpr (sizeof(T) <= 32)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            simd_for_each(p, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        const size_t live = p.count();
        print_ns("sparse simd_for_each (degraded)", live, live > 0 ? ns / static_cast<double>(live) : 0);
    }

    // L.4 稀疏 filter_for_each
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            filter_for_each(p, [](const T&) { return true; }, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        const size_t live = p.count();
        print_ns("sparse filter_for_each", live, live > 0 ? ns / static_cast<double>(live) : 0);
    }

    // L.5 稀疏 compact_to (压缩为密集数组)
    {
        class_pool<T> dst;
        dst.increase_capacity(n);
        double ns = best_ns(REPEAT, [&]() {
            size_t c = compact_to(p, dst.data(), n);
            uint32_t sum = 0;
            for (size_t i = 0; i < c; ++i) { sum += checksum(dst[i]); }
            return opaque(sum);
        });
        const size_t live = p.count();
        print_ns("sparse compact_to", live, live > 0 ? ns / static_cast<double>(live) : 0);
        perf_check("sparse compact_to", live > 0 ? ns / static_cast<double>(live) : 0);
    }

    // L.6 稀疏 strided_for_each (跳过空洞)
    {
        double ns = best_ns(REPEAT, [&]() {
            uint32_t sum = 0;
            strided_for_each(p, 0, 4, [&](T& v) { sum += checksum(v); });
            return opaque(sum);
        });
        const size_t cnt = n / 4;
        print_ns("sparse strided_for_each", cnt, cnt > 0 ? ns / static_cast<double>(cnt) : 0);
    }

    print_footer();
}

// === 主函数 ===
int main()
{
    cout << "============================================================\n";
    cout << "  class_pool<T> 独立性能测试 (含视图接口)\n";
    cout << "  覆盖: 1-8 基础接口 | A 子范围 | B 反向 | C 步进 | D 变换\n";
    cout << "        E 过滤 | F 规约 | G 窗口/分块 | H 枚举 | I zip\n";
    cout << "        J SIMD | K 拷贝/移动 | L 稀疏模式独有\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K

    test_construct<POD4>(N);
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

    test_sparse<POD4>(N);
    test_sparse<POD32>(N);

    test_insert_erase<POD4>(N);
    test_insert_erase<POD32>(N);

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

    // 稀疏模式独有测试
    test_sparse_views<POD4>(N);
    test_sparse_views<POD32>(N);

    cout << "\n============================================================\n";
    cout << "  测试完成\n";
    cout << "============================================================\n";
    return 0;
}
