// test_push_asm.cpp - 验证 push_back 性能差异根因
#include "perf_common.hpp"
#include "include/part/dense.hpp"
#include <vector>

using namespace std;

// V1: 当前 dense 的表示 (data_ptr + index + capacity)
struct v1 {
    POD32* data_;
    size_t cap_;
    size_t size_;
    void init(size_t n) { data_ = static_cast<POD32*>(::operator new(n * sizeof(POD32), std::nothrow)); cap_ = n; size_ = 0; }
    void destroy() { ::operator delete(data_, cap_ * sizeof(POD32)); }
    DENSE_ALWAYS_INLINE void push(const POD32& v) noexcept {
        data_[size_] = v;
        ++size_;
    }
};

// V2: 使用 finish 指针 (类似 std::vector)
struct v2 {
    POD32* begin_;
    POD32* end_;
    POD32* cap_;
    void init(size_t n) { begin_ = static_cast<POD32*>(::operator new(n * sizeof(POD32), std::nothrow)); end_ = begin_; cap_ = begin_ + n; }
    void destroy() { ::operator delete(begin_, (cap_ - begin_) * sizeof(POD32)); }
    DENSE_ALWAYS_INLINE void push(const POD32& v) noexcept {
        *end_ = v;
        ++end_;
    }
};

static POD32 make_p(uint32_t i) noexcept {
    POD32 p; for (int k = 0; k < 8; ++k) p.a[k] = static_cast<float>(i + k); return p;
}

static volatile size_t g_sink = 0;

// 匹配 perf test 模式: 每次迭代创建新容器
template <typename Container>
static double bench_push_alloc(size_t n) {
    double best = 1e18;
    for (int r = 0; r < 3; ++r) {
        timer t;
        Container c; c.init(n);
        for (size_t i = 0; i < n; ++i) c.push(make_p(static_cast<uint32_t>(i)));
        compiler_barrier();
        if constexpr (is_same_v<Container, v2>) { g_sink = c.end_ - c.begin_; }
        else { g_sink = c.size_; }
        c.destroy();
        double ns = t.elapsed_nanoseconds();
        if (ns < best) best = ns;
    }
    return best;
}

static double bench_dense_alloc(size_t n) {
    double best = 1e18;
    for (int r = 0; r < 3; ++r) {
        timer t;
        dense<POD32> d; d.increase_capacity(n);
        for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_p(static_cast<uint32_t>(i)));
        compiler_barrier();
        g_sink = d.size();
        double ns = t.elapsed_nanoseconds();
        if (ns < best) best = ns;
    }
    return best;
}

static double bench_vector_alloc(size_t n) {
    double best = 1e18;
    for (int r = 0; r < 3; ++r) {
        timer t;
        vector<POD32> v; v.reserve(n);
        for (size_t i = 0; i < n; ++i) v.push_back(make_p(static_cast<uint32_t>(i)));
        compiler_barrier();
        g_sink = v.size();
        double ns = t.elapsed_nanoseconds();
        if (ns < best) best = ns;
    }
    return best;
}

// 仅 push (无分配开销, 复用容器)
template <typename Container>
static double bench_push_only(size_t n) {
    Container c; c.init(n);
    double best = 1e18;
    for (int r = 0; r < 5; ++r) {
        if constexpr (is_same_v<Container, v2>) { c.end_ = c.begin_; }
        else { c.size_ = 0; }
        timer t;
        for (size_t i = 0; i < n; ++i) c.push(make_p(static_cast<uint32_t>(i)));
        double ns = t.elapsed_nanoseconds();
        if (ns < best) best = ns;
    }
    c.destroy();
    return best;
}

static double bench_dense_only(size_t n) {
    dense<POD32> d; d.increase_capacity(n);
    double best = 1e18;
    for (int r = 0; r < 5; ++r) {
        d.clear();
        timer t;
        for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_p(static_cast<uint32_t>(i)));
        double ns = t.elapsed_nanoseconds();
        if (ns < best) best = ns;
    }
    return best;
}

static double bench_vector_only(size_t n) {
    vector<POD32> v; v.reserve(n);
    double best = 1e18;
    for (int r = 0; r < 5; ++r) {
        v.clear();
        timer t;
        for (size_t i = 0; i < n; ++i) v.push_back(make_p(static_cast<uint32_t>(i)));
        double ns = t.elapsed_nanoseconds();
        if (ns < best) best = ns;
    }
    return best;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    const size_t N = 1 << 18;
    cout << "POD32 push_back (N=" << N << "):\n\n";
    cout << "[With alloc+dealloc per iteration]:\n";
    cout << "  v1 (data+size+cap):  " << fixed << setprecision(0) << bench_push_alloc<v1>(N) << " ns\n";
    cout << "  v2 (begin+end+cap): " << bench_push_alloc<v2>(N) << " ns\n";
    cout << "  dense<POD32>:       " << bench_dense_alloc(N) << " ns\n";
    cout << "  std::vector:        " << bench_vector_alloc(N) << " ns\n";
    cout << "\n[Push only (warm cache, no alloc)]:\n";
    cout << "  v1 (data+size+cap):  " << bench_push_only<v1>(N) << " ns\n";
    cout << "  v2 (begin+end+cap): " << bench_push_only<v2>(N) << " ns\n";
    cout << "  dense<POD32>:       " << bench_dense_only(N) << " ns\n";
    cout << "  std::vector:        " << bench_vector_only(N) << " ns\n";
    return 0;
}
