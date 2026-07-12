#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <bit>
#include <type_traits>
#include <utility>

namespace ecs
{

namespace detail
{
// MinGW+AVX2 下 std::sort+lambda 会崩溃, 提供内联堆排序作为分配失败回退
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
    static constexpr size_t digit_bits = 11;
    static constexpr size_t passes = 3;
    static constexpr size_t digit_counts[passes] = {11, 11, 10};
    static constexpr size_t digit_shifts[passes] = {0, 11, 22};
    static constexpr size_t bucket_counts[passes] = {2048, 2048, 1024};
};

template <>
struct radix_config<uint64_t>
{
    static constexpr size_t digit_bits = 11;
    static constexpr size_t passes = 6;
    static constexpr size_t digit_counts[passes] = {11, 11, 11, 11, 11, 9};
    static constexpr size_t digit_shifts[passes] = {0, 11, 22, 33, 44, 55};
    static constexpr size_t bucket_counts[passes] = {2048, 2048, 2048, 2048, 2048, 512};
};

inline constexpr size_t max_buckets = 2048;

template <typename U>
inline void radix_count_pass(size_t* count, const U* keys, size_t n,
                             size_t shift, size_t mask) noexcept
{
    constexpr size_t prefetch_dist = 8;
    for (size_t i = 0; i < n; ++i)
    {
        if (i + prefetch_dist < n) [[likely]]
        {
            PREFETCH_R(&keys[i + prefetch_dist]);
        }
        ++count[(keys[i] >> shift) & mask];
    }
}

template <typename Entry>
inline void radix_scatter_pass(Entry* dst, const Entry* src, size_t n,
                               const size_t* count, const auto* keys,
                               size_t shift, size_t mask) noexcept
{
    constexpr size_t prefetch_dist = 8;
    for (size_t i = 0; i < n; ++i)
    {
        if (i + prefetch_dist < n) [[likely]]
        {
            PREFETCH_R(&src[i + prefetch_dist]);
        }
        auto k = radix_key(src[i].key);
        size_t bucket = (k >> shift) & mask;
        new (&dst[count[bucket]]) Entry(std::move(src[i]));
        ++const_cast<size_t&>(count[bucket]);
    }
}

template <typename KeyType>
void radix_sort_entries_impl(void* entries_data, size_t n) noexcept
{
    using U = decltype(radix_key(std::declval<KeyType>()));
    using cfg = radix_config<U>;
    using entry_t = struct { KeyType key; size_t index; };

    auto* entries = static_cast<entry_t*>(entries_data);
    auto* temp = static_cast<entry_t*>(
        ::operator new(n * sizeof(entry_t), std::align_val_t{alignof(entry_t)}, std::nothrow));
    if (!temp) [[unlikely]]
    {
        detail::fallback_heap_sort(entries, n);
        return;
    }

    auto* keys_buf = static_cast<U*>(
        ::operator new(n * sizeof(U), std::align_val_t{alignof(U)}, std::nothrow));
    if (!keys_buf) [[unlikely]]
    {
        ::operator delete(temp, n * sizeof(entry_t), std::align_val_t{alignof(entry_t)});
        detail::fallback_heap_sort(entries, n);
        return;
    }

    for (size_t i = 0; i < n; ++i)
    {
        keys_buf[i] = radix_key(entries[i].key);
    }

    entry_t* src = entries;
    entry_t* dst = temp;

    for (size_t pass = 0; pass < cfg::passes; ++pass)
    {
        const size_t shift = cfg::digit_shifts[pass];
        const size_t bc = cfg::bucket_counts[pass];
        const size_t mask = bc - 1;

        size_t count[max_buckets] = {};
        radix_count_pass(count, keys_buf, n, shift, mask);

        size_t total = 0;
        for (size_t i = 0; i < bc; ++i)
        {
            size_t c = count[i];
            count[i] = total;
            total += c;
        }

        radix_scatter_pass(dst, src, n, count, keys_buf, shift, mask);

        for (size_t i = 0; i < n; ++i)
        {
            keys_buf[i] = radix_key(dst[i].key);
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

    ::operator delete(keys_buf, n * sizeof(U), std::align_val_t{alignof(U)});
    ::operator delete(temp, n * sizeof(entry_t), std::align_val_t{alignof(entry_t)});
}

template <typename KeyType>
void radix_sort_indices_impl(size_t* indices, const KeyType* keys, size_t n,
                             size_t* temp_buf) noexcept
{
    using U = decltype(radix_key(std::declval<KeyType>()));
    using cfg = radix_config<U>;

    auto* keys_buf = static_cast<U*>(
        ::operator new(n * sizeof(U), std::align_val_t{alignof(U)}, std::nothrow));
    if (!keys_buf) [[unlikely]]
    {
        return;
    }

    for (size_t i = 0; i < n; ++i)
    {
        keys_buf[i] = radix_key(keys[indices[i]]);
    }

    size_t* src = indices;
    size_t* dst = temp_buf;

    for (size_t pass = 0; pass < cfg::passes; ++pass)
    {
        const size_t shift = cfg::digit_shifts[pass];
        const size_t bc = cfg::bucket_counts[pass];
        const size_t mask = bc - 1;

        size_t count[max_buckets] = {};
        constexpr size_t prefetch_dist = 8;
        for (size_t i = 0; i < n; ++i)
        {
            if (i + prefetch_dist < n) [[likely]]
            {
                PREFETCH_R(&keys_buf[i + prefetch_dist]);
            }
            ++count[(keys_buf[i] >> shift) & mask];
        }

        size_t total = 0;
        for (size_t i = 0; i < bc; ++i)
        {
            size_t c = count[i];
            count[i] = total;
            total += c;
        }

        for (size_t i = 0; i < n; ++i)
        {
            size_t bucket = (keys_buf[i] >> shift) & mask;
            dst[count[bucket]] = src[i];
            ++count[bucket];
        }

        for (size_t i = 0; i < n; ++i)
        {
            keys_buf[i] = radix_key(keys[dst[i]]);
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

    ::operator delete(keys_buf, n * sizeof(U), std::align_val_t{alignof(U)});
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

}
