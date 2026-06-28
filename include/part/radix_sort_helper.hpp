#pragma once
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace ecs
{

// ======================== radix sort helper ========================
template <typename T>
constexpr bool is_radix_sortable_v =
    std::is_integral_v<T> || std::is_floating_point_v<T>;

template <typename T>
[[nodiscard]] inline auto radix_key(T val) noexcept
{
    if constexpr (std::is_integral_v<T>)
    {
        using U = std::make_unsigned_t<T>;
        U u = static_cast<U>(val);
        if constexpr (std::is_signed_v<T>)
            u ^= (U{1} << (sizeof(U) * 8 - 1));
        return u;
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        uint32_t u;
        std::memcpy(&u, &val, sizeof(u));
        u ^= (u >> 31) ? 0xFFFFFFFF : 0x80000000;
        return u;
    }
    else if constexpr (std::is_same_v<T, double>)
    {
        uint64_t u;
        std::memcpy(&u, &val, sizeof(u));
        u ^= (u >> 63) ? 0xFFFFFFFFFFFFFFFF : 0x8000000000000000;
        return u;
    }
}

template <typename KeyType>
inline void radix_sort_entries(void* entries_data, size_t n) noexcept
    requires is_radix_sortable_v<KeyType>
{
    if (n <= 1) return;
    using U = decltype(radix_key(std::declval<KeyType>()));
    constexpr size_t key_bytes = sizeof(U);
    constexpr size_t radix_bits = 8;
    constexpr size_t bucket_count = 1 << radix_bits;
    constexpr size_t passes = key_bytes; // 8 bits per pass

    struct sort_entry { KeyType key; size_t index; };
    auto* entries = static_cast<sort_entry*>(entries_data);
    auto* temp = static_cast<sort_entry*>(::operator new(n * sizeof(sort_entry), std::align_val_t{alignof(sort_entry)}, std::nothrow));
    if (!temp) [[unlikely]]
    {
        std::sort(entries, entries + n, [](const sort_entry& a, const sort_entry& b) {
            return a.key < b.key;
        });
        return;
    }

    for (size_t pass = 0; pass < passes; ++pass)
    {
        size_t count[bucket_count] = {};
        size_t shift = pass * radix_bits;

        for (size_t i = 0; i < n; ++i)
        {
            U k = radix_key(entries[i].key);
            ++count[(k >> shift) & (bucket_count - 1)];
        }

        size_t total = 0;
        for (size_t i = 0; i < bucket_count; ++i)
        {
            size_t c = count[i];
            count[i] = total;
            total += c;
        }

        for (size_t i = 0; i < n; ++i)
        {
            U k = radix_key(entries[i].key);
            size_t bucket = (k >> shift) & (bucket_count - 1);
            new (&temp[count[bucket]]) sort_entry(std::move(entries[i]));
            ++count[bucket];
        }

        std::swap(entries, temp);
    }

    // 奇数趟后 entries 指向 temp,需拷贝回 entries_data
    if (passes % 2 == 1)
    {
        for (size_t i = 0; i < n; ++i)
            new (&static_cast<sort_entry*>(entries_data)[i]) sort_entry(std::move(entries[i]));
    }

    ::operator delete(temp, n * sizeof(sort_entry), std::align_val_t{alignof(sort_entry)});
}

template <typename KeyType>
inline void radix_sort_indices(size_t* indices, const KeyType* keys, size_t n,
                               size_t* temp_buf) noexcept
    requires is_radix_sortable_v<KeyType>
{
    if (n <= 1) return;
    using U = decltype(radix_key(std::declval<KeyType>()));
    constexpr size_t radix_bits = 8;
    constexpr size_t bucket_count = 1 << radix_bits;
    constexpr size_t passes = sizeof(U);

    size_t* src = indices;
    size_t* dst = temp_buf;

    for (size_t pass = 0; pass < passes; ++pass)
    {
        size_t count[bucket_count] = {};
        size_t shift = pass * radix_bits;

        for (size_t i = 0; i < n; ++i)
        {
            U k = radix_key(keys[src[i]]);
            ++count[(k >> shift) & (bucket_count - 1)];
        }

        size_t total = 0;
        for (size_t i = 0; i < bucket_count; ++i)
        {
            size_t c = count[i];
            count[i] = total;
            total += c;
        }

        for (size_t i = 0; i < n; ++i)
        {
            U k = radix_key(keys[src[i]]);
            size_t bucket = (k >> shift) & (bucket_count - 1);
            dst[count[bucket]] = src[i];
            ++count[bucket];
        }

        std::swap(src, dst);
    }

    if (passes % 2 == 1)
    {
        for (size_t i = 0; i < n; ++i)
            indices[i] = src[i];
        (void)temp_buf;
    }
}

}
