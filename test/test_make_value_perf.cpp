// Mimic the main test pattern: make_value + push_back
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>
#include <iostream>
#include "include/part/dense.hpp"

using namespace std;

struct POD32 { float a[8]; };

static volatile size_t g_sink = 0;
alignas(alignof(POD32)) static char g_buf[sizeof(POD32)];

inline void sink_write(const POD32& x) noexcept {
    std::memcpy(g_buf, &x, sizeof(POD32));
    volatile uint8_t v = static_cast<volatile uint8_t*>(static_cast<void*>(g_buf))[0];
    (void)v;
}

template <typename T>
static T make_value(uint32_t i) noexcept
{
    if constexpr (is_same_v<T, POD32>) {
        POD32 p;
        for (int k = 0; k < 8; ++k) p.a[k] = static_cast<float>(i + k);
        return p;
    }
    return T{};
}

int main() {
    constexpr size_t N = 262144;
    constexpr int REPEAT = 5;

    // Dense push_back_unchecked with make_value (same as main test)
    {
        double best = 1e18;
        for (int r = 0; r < REPEAT; ++r) {
            dense<POD32> d; d.increase_capacity(N);
            auto t0 = chrono::high_resolution_clock::now();
            for (size_t i = 0; i < N; ++i) {
                d.push_back_unchecked(make_value<POD32>(static_cast<uint32_t>(i)));
            }
            auto t1 = chrono::high_resolution_clock::now();
            sink_write(d.back());
            double ns = chrono::duration<double, nano>(t1 - t0).count() / N;
            if (ns < best) best = ns;
        }
        cout << "dense push_back_unchecked + make_value: " << best << " ns/elem\n";
    }

    // Vector push_back with make_value (same as main test)
    {
        double best = 1e18;
        for (int r = 0; r < REPEAT; ++r) {
            vector<POD32> v; v.reserve(N);
            auto t0 = chrono::high_resolution_clock::now();
            for (size_t i = 0; i < N; ++i) {
                v.push_back(make_value<POD32>(static_cast<uint32_t>(i)));
            }
            auto t1 = chrono::high_resolution_clock::now();
            sink_write(v.back());
            double ns = chrono::duration<double, nano>(t1 - t0).count() / N;
            if (ns < best) best = ns;
        }
        cout << "vector push_back + make_value:         " << best << " ns/elem\n";
    }

    // Dense emplace_back_unchecked with make_value
    {
        double best = 1e18;
        for (int r = 0; r < REPEAT; ++r) {
            dense<POD32> d; d.increase_capacity(N);
            auto t0 = chrono::high_resolution_clock::now();
            for (size_t i = 0; i < N; ++i) {
                d.emplace_back_unchecked(make_value<POD32>(static_cast<uint32_t>(i)));
            }
            auto t1 = chrono::high_resolution_clock::now();
            sink_write(d.back());
            double ns = chrono::duration<double, nano>(t1 - t0).count() / N;
            if (ns < best) best = ns;
        }
        cout << "dense emplace_back_unchecked + make:   " << best << " ns/elem\n";
    }

    // Vector emplace_back with make_value
    {
        double best = 1e18;
        for (int r = 0; r < REPEAT; ++r) {
            vector<POD32> v; v.reserve(N);
            auto t0 = chrono::high_resolution_clock::now();
            for (size_t i = 0; i < N; ++i) {
                v.emplace_back(make_value<POD32>(static_cast<uint32_t>(i)));
            }
            auto t1 = chrono::high_resolution_clock::now();
            sink_write(v.back());
            double ns = chrono::duration<double, nano>(t1 - t0).count() / N;
            if (ns < best) best = ns;
        }
        cout << "vector emplace_back + make_value:       " << best << " ns/elem\n";
    }

    return 0;
}
