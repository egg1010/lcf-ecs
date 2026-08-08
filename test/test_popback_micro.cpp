// test_popback_micro.cpp - 隔离 push_back 和 pop_back 的微基准
// 目标: 找出 POD128 pop_back 比 vector 慢的根因
#include "perf_common.hpp"
#include "include/part/dense.hpp"
#include <vector>

using namespace std;

struct POD128 { float a[32]; };

template <typename T>
static T make_value(uint32_t i) noexcept
{
    if constexpr (is_same_v<T, POD128>) { T p; for (int k = 0; k < 32; ++k) p.a[k] = static_cast<float>(i + k); return p; }
    else return T{};
}

static volatile size_t g_sink = 0;

inline void print_compare(const char* label, size_t n,
                          double dense_ns, double vec_ns) noexcept
{
    double d_tp = (dense_ns > 0 && n > 0) ? static_cast<double>(n) / dense_ns : 0;
    double v_tp = (vec_ns   > 0 && n > 0) ? static_cast<double>(n) / vec_ns   : 0;
    const char* verdict;
    if (dense_ns < vec_ns * 0.95)       verdict = "[dense WIN]";
    else if (vec_ns < dense_ns * 0.95)   verdict = "[vector WIN]";
    else                                verdict = "[TIE]";

    cout << "  " << left << setw(28) << label
         << " | dense: " << fixed << setprecision(3) << setw(9) << dense_ns << " ns"
         << " (" << setprecision(2) << setw(8) << d_tp << " G/s)"
         << " | vector: " << setw(9) << vec_ns << " ns"
         << " (" << setw(8) << v_tp << " G/s)"
         << " " << verdict << "\n";
}

int main()
{
    cout << "============================================================\n";
    cout << "  pop_back micro-benchmark (POD128)\n";
    cout << "============================================================\n";

    const size_t N = 1 << 18;  // 256K
    constexpr int REPEAT = 5;

    // === 1. push_back only (baseline) ===
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<POD128> d; d.increase_capacity(N);
            for (size_t i = 0; i < N; ++i) d.push_back_unchecked(make_value<POD128>(static_cast<uint32_t>(i)));
            compiler_barrier(); g_sink = d.size(); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<POD128> v; v.reserve(N);
            for (size_t i = 0; i < N; ++i) v.push_back(make_value<POD128>(static_cast<uint32_t>(i)));
            compiler_barrier(); g_sink = v.size(); return g_sink;
        });
        print_compare("push_back only", N, d_ns / N, v_ns / N);
    }

    // === 2. pop_back only (pre-filled, measure ONLY pop) ===
    {
        dense<POD128> d_src; d_src.increase_capacity(N);
        for (size_t i = 0; i < N; ++i) d_src.push_back_unchecked(make_value<POD128>(static_cast<uint32_t>(i)));
        vector<POD128> v_src; v_src.reserve(N);
        for (size_t i = 0; i < N; ++i) v_src.push_back(make_value<POD128>(static_cast<uint32_t>(i)));

        double d_ns = best_ns(REPEAT, [&]() {
            dense<POD128> d; d.increase_capacity(N);
            // 快速复制 (memcpy) 避免污染测量
            d.append_bulk(d_src.data(), N);
            for (size_t i = 0; i < N; ++i) d.pop_back();
            compiler_barrier(); g_sink = d.size(); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<POD128> v; v.reserve(N);
            v.insert(v.end(), v_src.begin(), v_src.end());
            for (size_t i = 0; i < N; ++i) v.pop_back();
            compiler_barrier(); g_sink = v.size(); return g_sink;
        });
        print_compare("pop_back only (pre-filled)", N, d_ns / N, v_ns / N);
    }

    // === 3. erase(end-1) only (pre-filled, measure ONLY erase) ===
    {
        dense<POD128> d_src; d_src.increase_capacity(N);
        for (size_t i = 0; i < N; ++i) d_src.push_back_unchecked(make_value<POD128>(static_cast<uint32_t>(i)));
        vector<POD128> v_src; v_src.reserve(N);
        for (size_t i = 0; i < N; ++i) v_src.push_back(make_value<POD128>(static_cast<uint32_t>(i)));

        double d_ns = best_ns(REPEAT, [&]() {
            dense<POD128> d; d.increase_capacity(N);
            d.append_bulk(d_src.data(), N);
            for (size_t i = 0; i < N; ++i) d.erase(d.end() - 1);
            compiler_barrier(); g_sink = d.size(); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<POD128> v; v.reserve(N);
            v.insert(v.end(), v_src.begin(), v_src.end());
            for (size_t i = 0; i < N; ++i) v.erase(v.end() - 1);
            compiler_barrier(); g_sink = v.size(); return g_sink;
        });
        print_compare("erase(end-1) only (pre-filled)", N, d_ns / N, v_ns / N);
    }

    // === 4. push_back + pop_back (combined, original test) ===
    {
        double d_ns = best_ns(REPEAT, [&]() {
            dense<POD128> d; d.increase_capacity(N);
            for (size_t i = 0; i < N; ++i) d.push_back_unchecked(make_value<POD128>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < N; ++i) d.pop_back();
            compiler_barrier(); g_sink = d.size(); return g_sink;
        });
        double v_ns = best_ns(REPEAT, [&]() {
            vector<POD128> v; v.reserve(N);
            for (size_t i = 0; i < N; ++i) v.push_back(make_value<POD128>(static_cast<uint32_t>(i)));
            for (size_t i = 0; i < N; ++i) v.pop_back();
            compiler_barrier(); g_sink = v.size(); return g_sink;
        });
        print_compare("push_back + pop_back (combined)", N, d_ns / N, v_ns / N);
    }

    cout << "============================================================\n";
    return 0;
}
