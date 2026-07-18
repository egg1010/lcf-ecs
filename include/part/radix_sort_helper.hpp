#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <bit>
#include <type_traits>
#include <utility>
#if defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
#include <immintrin.h>
#define LCF_HAS_SSE2 1
#else
#define LCF_HAS_SSE2 0
#endif

namespace detail
{
template <typename Entry>
void fallback_heap_sort(Entry* data, size_t n) noexcept
{
    if (n < 2) return;
    auto sift_down = [](Entry* d, size_t i, size_t count) noexcept
    {
        while (true)
        {
            size_t left = 2 * i + 1;
            size_t right = left + 1;
            size_t largest = i;
            if (left < count && d[left].key > d[largest].key) largest = left;
            if (right < count && d[right].key > d[largest].key) largest = right;
            if (largest == i) break;
            Entry tmp = d[i]; d[i] = d[largest]; d[largest] = tmp;
            i = largest;
        }
    };
    for (size_t i = n / 2; i-- > 0;) sift_down(data, i, n);
    for (size_t i = n - 1; i > 0; --i)
    {
        Entry tmp = data[0]; data[0] = data[i]; data[i] = tmp;
        sift_down(data, 0, i);
    }
}
}

template <typename T>
constexpr bool is_radix_sortable_v =
    std::is_integral_v<T> || std::is_floating_point_v<T>;

template <typename T>
[[nodiscard]] inline auto radix_key(T val) noexcept
{
    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, char>
        || std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char>
        || std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t>)
    {
        return static_cast<unsigned>(static_cast<uint8_t>(val));
    }
    else if constexpr (std::is_integral_v<T>)
    {
        using U = std::make_unsigned_t<T>;
        U u = static_cast<U>(val);
        if constexpr (std::is_signed_v<T>)
        {
            u ^= (U{1} << (sizeof(U) * 8 - 1));
        }
        return u;
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        uint32_t u = std::bit_cast<uint32_t>(val);
        u ^= (u >> 31) ? 0xFFFFFFFF : 0x80000000;
        return u;
    }
    else if constexpr (std::is_same_v<T, double>)
    {
        uint64_t u = std::bit_cast<uint64_t>(val);
        u ^= (u >> 63) ? 0xFFFFFFFFFFFFFFFF : 0x8000000000000000;
        return u;
    }
}

namespace detail
{

template <typename U>
struct radix_config;

template <>
struct radix_config<uint32_t>
{
    static constexpr size_t passes = 3;
    static constexpr size_t digit_shifts[passes] = {0, 11, 22};
    static constexpr size_t bucket_counts[passes] = {2048, 2048, 1024};
};

template <>
struct radix_config<uint64_t>
{
    static constexpr size_t passes = 6;
    static constexpr size_t digit_shifts[passes] = {0, 11, 22, 33, 44, 55};
    static constexpr size_t bucket_counts[passes] = {2048, 2048, 2048, 2048, 2048, 512};
};

// 大数量级配置: 16-bit 数字位, 减少趟数, 适用于 DRAM 驻留数据
template <typename U>
struct radix_config_large;

template <>
struct radix_config_large<uint32_t>
{
    static constexpr size_t passes = 2;
    static constexpr size_t digit_shifts[passes] = {0, 16};
    static constexpr size_t bucket_counts[passes] = {65536, 65536};
};

template <>
struct radix_config_large<uint64_t>
{
    static constexpr size_t passes = 4;
    static constexpr size_t digit_shifts[passes] = {0, 16, 32, 48};
    static constexpr size_t bucket_counts[passes] = {65536, 65536, 65536, 65536};
};

inline constexpr size_t max_buckets = 2048;
inline constexpr size_t max_buckets_large = 65536;
inline constexpr size_t large_sub_count = 8;

// n ≥ 1M 时 entries 数据超出典型 L3 (16MB), 启用大数量级存储策略
inline constexpr size_t large_scale_threshold = size_t{1} << 20;
// n ≥ 5M 时 16-bit 直方图预热开销 < 10%, 2-pass 胜出
inline constexpr size_t large_pass_threshold = size_t{5} << 20;

template <typename U>
inline void radix_count_pass(size_t* count, const U* keys, size_t n,
                             size_t shift, size_t mask, size_t bc) noexcept
{
    constexpr size_t prefetch_dist = 8;

    if (bc <= 512)
    {
        size_t h0[512] = {}, h1[512] = {}, h2[512] = {}, h3[512] = {};
        size_t i = 0;
        for (; i + 3 < n; i += 4)
        {
            if (i + prefetch_dist < n) [[likely]]
            {
                PREFETCH_R(&keys[i + prefetch_dist]);
            }
            ++h0[(keys[i]     >> shift) & mask];
            ++h1[(keys[i + 1] >> shift) & mask];
            ++h2[(keys[i + 2] >> shift) & mask];
            ++h3[(keys[i + 3] >> shift) & mask];
        }
        for (; i < n; ++i)
        {
            ++h0[(keys[i] >> shift) & mask];
        }
        for (size_t b = 0; b < bc; ++b)
        {
            count[b] = h0[b] + h1[b] + h2[b] + h3[b];
        }
    }
    else if (bc <= 1024)
    {
        size_t h0[1024] = {}, h1[1024] = {};
        size_t i = 0;
        for (; i + 1 < n; i += 2)
        {
            if (i + prefetch_dist < n) [[likely]]
            {
                PREFETCH_R(&keys[i + prefetch_dist]);
            }
            ++h0[(keys[i]     >> shift) & mask];
            ++h1[(keys[i + 1] >> shift) & mask];
        }
        for (; i < n; ++i)
        {
            ++h0[(keys[i] >> shift) & mask];
        }
        for (size_t b = 0; b < bc; ++b)
        {
            count[b] = h0[b] + h1[b];
        }
    }
    else
    {
        size_t i = 0;
        for (; i + 3 < n; i += 4)
        {
            if (i + prefetch_dist < n) [[likely]]
            {
                PREFETCH_R(&keys[i + prefetch_dist]);
            }
            ++count[(keys[i]     >> shift) & mask];
            ++count[(keys[i + 1] >> shift) & mask];
            ++count[(keys[i + 2] >> shift) & mask];
            ++count[(keys[i + 3] >> shift) & mask];
        }
        for (; i < n; ++i)
        {
            ++count[(keys[i] >> shift) & mask];
        }
    }
}

// 大数量级 count pass: 65536 桶, 8 路子直方图 (堆分配)
template <typename U>
inline void radix_count_pass_large(size_t* count, const U* keys, size_t n,
                                    size_t shift, size_t mask,
                                    size_t* sub_hists) noexcept
{
    constexpr size_t prefetch_dist = 8;
    const size_t bc = mask + 1;

    std::memset(sub_hists, 0, large_sub_count * max_buckets_large * sizeof(size_t));

    size_t i = 0;
    for (; i + 7 < n; i += 8)
    {
        if (i + prefetch_dist < n) [[likely]]
        {
            PREFETCH_R(&keys[i + prefetch_dist]);
        }
        ++sub_hists[0 * max_buckets_large + ((keys[i]     >> shift) & mask)];
        ++sub_hists[1 * max_buckets_large + ((keys[i + 1] >> shift) & mask)];
        ++sub_hists[2 * max_buckets_large + ((keys[i + 2] >> shift) & mask)];
        ++sub_hists[3 * max_buckets_large + ((keys[i + 3] >> shift) & mask)];
        ++sub_hists[4 * max_buckets_large + ((keys[i + 4] >> shift) & mask)];
        ++sub_hists[5 * max_buckets_large + ((keys[i + 5] >> shift) & mask)];
        ++sub_hists[6 * max_buckets_large + ((keys[i + 6] >> shift) & mask)];
        ++sub_hists[7 * max_buckets_large + ((keys[i + 7] >> shift) & mask)];
    }
    for (; i < n; ++i)
    {
        ++sub_hists[0 * max_buckets_large + ((keys[i] >> shift) & mask)];
    }

    for (size_t b = 0; b < bc; ++b)
    {
        size_t total = 0;
        for (size_t s = 0; s < large_sub_count; ++s)
        {
            total += sub_hists[s * max_buckets_large + b];
        }
        count[b] = total;
    }
}

// 小数量级散射: keys 散射 + 普通 store
template <typename Entry, typename U>
inline void scatter_small(Entry* dst, const Entry* src, size_t n,
                          size_t* count, const U* k_src, U* k_dst,
                          size_t shift, size_t mask) noexcept
{
    constexpr size_t prefetch_dist = 8;
    for (size_t i = 0; i < n; ++i)
    {
        if (i + prefetch_dist < n) [[likely]]
        {
            PREFETCH_R(&src[i + prefetch_dist]);
        }
        U k = k_src[i];
        size_t bucket = (k >> shift) & mask;
        size_t pos = count[bucket];
        if (pos + 1 < n) [[likely]]
        {
            PREFETCH_R(&dst[pos + 1]);
        }
        new (&dst[pos]) Entry(std::move(src[i]));
        k_dst[pos] = k;
        ++count[bucket];
    }
}

// 大数量级散射: NT store 消除 RFO
template <typename Entry, typename U>
inline void scatter_large(Entry* dst, const Entry* src, size_t n,
                          size_t* count, const U* k_src,
                          size_t shift, size_t mask) noexcept
{
    constexpr size_t prefetch_dist = 8;
    for (size_t i = 0; i < n; ++i)
    {
        if (i + prefetch_dist < n) [[likely]]
        {
            PREFETCH_R(&src[i + prefetch_dist]);
        }
        U k = k_src[i];
        size_t bucket = (k >> shift) & mask;
        size_t pos = count[bucket];
        if (pos + 1 < n) [[likely]]
        {
            PREFETCH_R(&dst[pos + 1]);
        }
        if constexpr (sizeof(Entry) == 16 && LCF_HAS_SSE2)
        {
            __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&src[i]));
            _mm_stream_si128(reinterpret_cast<__m128i*>(&dst[pos]), v);
        }
        else
        {
            new (&dst[pos]) Entry(std::move(src[i]));
        }
        ++count[bucket];
    }
#if LCF_HAS_SSE2
    _mm_sfence();
#endif
}

template <typename KeyType, typename Cfg>
void radix_sort_entries_with_cfg(void* entries_data, size_t n,
                                  bool small_scale, bool large_pass) noexcept
{
    using U = decltype(radix_key(std::declval<KeyType>()));
    using cfg = Cfg;
    using entry_t = struct { KeyType key; size_t index; };

    auto* entries = static_cast<entry_t*>(entries_data);
    auto* temp = static_cast<entry_t*>(
        ::operator new(n * sizeof(entry_t), std::align_val_t{alignof(entry_t)}, std::nothrow));
    if (!temp) [[unlikely]]
    {
        detail::fallback_heap_sort(entries, n);
        return;
    }

    const size_t keys_alloc = small_scale ? (n * 2) : n;
    auto* keys_combined = static_cast<U*>(
        ::operator new(keys_alloc * sizeof(U), std::align_val_t{alignof(U)}, std::nothrow));
    if (!keys_combined) [[unlikely]]
    {
        ::operator delete(temp, n * sizeof(entry_t), std::align_val_t{alignof(entry_t)});
        detail::fallback_heap_sort(entries, n);
        return;
    }

    U* k_src = keys_combined;
    U* k_dst = small_scale ? (keys_combined + n) : nullptr;

    size_t* hist_buf = nullptr;
    if (large_pass)
    {
        const size_t hist_size = (1 + large_sub_count) * max_buckets_large;
        hist_buf = static_cast<size_t*>(
            ::operator new(hist_size * sizeof(size_t), std::align_val_t{alignof(size_t)}, std::nothrow));
        if (!hist_buf) [[unlikely]]
        {
            ::operator delete(keys_combined, keys_alloc * sizeof(U), std::align_val_t{alignof(U)});
            ::operator delete(temp, n * sizeof(entry_t), std::align_val_t{alignof(entry_t)});
            detail::fallback_heap_sort(entries, n);
            return;
        }
    }

    for (size_t i = 0; i < n; ++i)
    {
        k_src[i] = radix_key(entries[i].key);
    }

    entry_t* src = entries;
    entry_t* dst = temp;

    for (size_t pass = 0; pass < cfg::passes; ++pass)
    {
        const size_t shift = cfg::digit_shifts[pass];
        const size_t bc = cfg::bucket_counts[pass];
        const size_t mask = bc - 1;

        size_t count_stack[max_buckets] = {};
        size_t* count = count_stack;
        if (large_pass)
        {
            count = hist_buf;
            size_t* sub_hists = hist_buf + max_buckets_large;
            std::memset(hist_buf, 0, (1 + large_sub_count) * max_buckets_large * sizeof(size_t));
            radix_count_pass_large(count, k_src, n, shift, mask, sub_hists);
        }
        else
        {
            std::memset(count, 0, max_buckets * sizeof(size_t));
            radix_count_pass(count, k_src, n, shift, mask, bc);
        }

        size_t total = 0;
        for (size_t i = 0; i < bc; ++i)
        {
            size_t c = count[i];
            count[i] = total;
            total += c;
        }

        if (small_scale)
        {
            scatter_small(dst, src, n, count, k_src, k_dst, shift, mask);
            std::swap(k_src, k_dst);
        }
        else
        {
            scatter_large(dst, src, n, count, k_src, shift, mask);
            for (size_t i = 0; i < n; ++i)
            {
                k_src[i] = radix_key(dst[i].key);
            }
        }

        std::swap(src, dst);
    }

    if constexpr (cfg::passes % 2 == 1)
    {
        for (size_t i = 0; i < n; ++i)
        {
            new (&static_cast<entry_t*>(entries_data)[i]) entry_t(std::move(src[i]));
        }
    }

    if (hist_buf)
    {
        const size_t hist_size = (1 + large_sub_count) * max_buckets_large;
        ::operator delete(hist_buf, hist_size * sizeof(size_t), std::align_val_t{alignof(size_t)});
    }
    ::operator delete(keys_combined, keys_alloc * sizeof(U), std::align_val_t{alignof(U)});
    ::operator delete(temp, n * sizeof(entry_t), std::align_val_t{alignof(entry_t)});
}

template <typename KeyType>
void radix_sort_entries_impl(void* entries_data, size_t n) noexcept
{
    using U = decltype(radix_key(std::declval<KeyType>()));
    const bool small_scale = (n < large_scale_threshold);
    const bool large_pass = (n >= large_pass_threshold);

    if (large_pass)
    {
        radix_sort_entries_with_cfg<KeyType, radix_config_large<U>>(entries_data, n, small_scale, large_pass);
    }
    else
    {
        radix_sort_entries_with_cfg<KeyType, radix_config<U>>(entries_data, n, small_scale, large_pass);
    }
}

template <typename KeyType, typename Cfg>
void radix_sort_indices_with_cfg(size_t* indices, const KeyType* keys, size_t n,
                                  size_t* temp_buf, bool small_scale, bool large_pass) noexcept
{
    using U = decltype(radix_key(std::declval<KeyType>()));
    using cfg = Cfg;

    const size_t keys_alloc = small_scale ? (n * 2) : n;
    auto* keys_combined = static_cast<U*>(
        ::operator new(keys_alloc * sizeof(U), std::align_val_t{alignof(U)}, std::nothrow));
    if (!keys_combined) [[unlikely]]
    {
        return;
    }

    U* k_src = keys_combined;
    U* k_dst = small_scale ? (keys_combined + n) : nullptr;

    size_t* hist_buf = nullptr;
    if (large_pass)
    {
        const size_t hist_size = (1 + large_sub_count) * max_buckets_large;
        hist_buf = static_cast<size_t*>(
            ::operator new(hist_size * sizeof(size_t), std::align_val_t{alignof(size_t)}, std::nothrow));
        if (!hist_buf) [[unlikely]]
        {
            ::operator delete(keys_combined, keys_alloc * sizeof(U), std::align_val_t{alignof(U)});
            return;
        }
    }

    for (size_t i = 0; i < n; ++i)
    {
        k_src[i] = radix_key(keys[indices[i]]);
    }

    size_t* src = indices;
    size_t* dst = temp_buf;

    for (size_t pass = 0; pass < cfg::passes; ++pass)
    {
        const size_t shift = cfg::digit_shifts[pass];
        const size_t bc = cfg::bucket_counts[pass];
        const size_t mask = bc - 1;

        size_t count_stack[max_buckets] = {};
        size_t* count = count_stack;
        if (large_pass)
        {
            count = hist_buf;
            size_t* sub_hists = hist_buf + max_buckets_large;
            std::memset(hist_buf, 0, (1 + large_sub_count) * max_buckets_large * sizeof(size_t));
            radix_count_pass_large(count, k_src, n, shift, mask, sub_hists);
        }
        else
        {
            std::memset(count, 0, max_buckets * sizeof(size_t));
            radix_count_pass(count, k_src, n, shift, mask, bc);
        }

        size_t total = 0;
        for (size_t i = 0; i < bc; ++i)
        {
            size_t c = count[i];
            count[i] = total;
            total += c;
        }

        constexpr size_t prefetch_dist = 8;
        if (small_scale)
        {
            for (size_t i = 0; i < n; ++i)
            {
                if (i + prefetch_dist < n) [[likely]]
                {
                    PREFETCH_R(&src[i + prefetch_dist]);
                }
                U k = k_src[i];
                size_t bucket = (k >> shift) & mask;
                size_t pos = count[bucket];
                if (pos + 1 < n) [[likely]]
                {
                    PREFETCH_R(&dst[pos + 1]);
                }
                dst[pos] = src[i];
                k_dst[pos] = k;
                ++count[bucket];
            }
            std::swap(k_src, k_dst);
        }
        else
        {
            for (size_t i = 0; i < n; ++i)
            {
                if (i + prefetch_dist < n) [[likely]]
                {
                    PREFETCH_R(&src[i + prefetch_dist]);
                }
                U k = k_src[i];
                size_t bucket = (k >> shift) & mask;
                size_t pos = count[bucket];
                if (pos + 1 < n) [[likely]]
                {
                    PREFETCH_R(&dst[pos + 1]);
                }
                dst[pos] = src[i];
                ++count[bucket];
            }
            for (size_t i = 0; i < n; ++i)
            {
                k_src[i] = radix_key(keys[dst[i]]);
            }
        }

        std::swap(src, dst);
    }

    if constexpr (cfg::passes % 2 == 1)
    {
        for (size_t i = 0; i < n; ++i)
        {
            indices[i] = src[i];
        }
    }

    if (hist_buf)
    {
        const size_t hist_size = (1 + large_sub_count) * max_buckets_large;
        ::operator delete(hist_buf, hist_size * sizeof(size_t), std::align_val_t{alignof(size_t)});
    }
    ::operator delete(keys_combined, keys_alloc * sizeof(U), std::align_val_t{alignof(U)});
}

template <typename KeyType>
void radix_sort_indices_impl(size_t* indices, const KeyType* keys, size_t n,
                             size_t* temp_buf) noexcept
{
    using U = decltype(radix_key(std::declval<KeyType>()));
    const bool small_scale = (n < large_scale_threshold);
    const bool large_pass = (n >= large_pass_threshold);

    if (large_pass)
    {
        radix_sort_indices_with_cfg<KeyType, radix_config_large<U>>(indices, keys, n, temp_buf, small_scale, large_pass);
    }
    else
    {
        radix_sort_indices_with_cfg<KeyType, radix_config<U>>(indices, keys, n, temp_buf, small_scale, large_pass);
    }
}

}

template <typename KeyType>
inline void radix_sort_entries(void* entries_data, size_t n) noexcept
    requires is_radix_sortable_v<KeyType>
{
    if (n <= 1)
    {
        return;
    }
    detail::radix_sort_entries_impl<KeyType>(entries_data, n);
}

template <typename KeyType>
inline void radix_sort_indices(size_t* indices, const KeyType* keys, size_t n,
                               size_t* temp_buf) noexcept
    requires is_radix_sortable_v<KeyType>
{
    if (n <= 1)
    {
        return;
    }
    detail::radix_sort_indices_impl<KeyType>(indices, keys, n, temp_buf);
}
