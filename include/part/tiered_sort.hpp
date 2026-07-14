#pragma once
#include "radix_sort_helper.hpp"
#include "force_inline.hpp"
#include <cstddef>
#include <type_traits>
#include <utility>

namespace detail
{

template <typename T, typename Compare>
void insertion_sort(T* data, size_t n, Compare& cmp) noexcept
{
    for (size_t i = 1; i < n; ++i)
    {
        T key = data[i];
        size_t j = i;
        while (j > 0 && cmp(key, data[j - 1]))
        {
            data[j] = data[j - 1];
            --j;
        }
        data[j] = key;
    }
}

template <typename T>
void insertion_sort_ascending(T* data, size_t n) noexcept
    requires std::is_trivially_copyable_v<T>
{
    for (size_t i = 1; i < n; ++i)
    {
        T key = data[i];
        size_t j = i;
        while (j > 0 && key < data[j - 1])
        {
            data[j] = data[j - 1];
            --j;
        }
        data[j] = key;
    }
}

template <typename T, typename Compare>
void sift_down(T* data, size_t i, size_t n, Compare& cmp) noexcept
{
    while (true)
    {
        size_t left = 2 * i + 1;
        size_t right = left + 1;
        size_t largest = i;

        if (left < n && cmp(data[largest], data[left]))
        {
            largest = left;
        }
        if (right < n && cmp(data[largest], data[right]))
        {
            largest = right;
        }

        if (largest == i)
        {
            break;
        }

        T tmp = data[i];
        data[i] = data[largest];
        data[largest] = tmp;
        i = largest;
    }
}

template <typename T, typename Compare>
void heap_sort(T* data, size_t n, Compare& cmp) noexcept
{
    for (size_t i = n / 2; i-- > 0;)
    {
        sift_down(data, i, n, cmp);
    }

    for (size_t i = n - 1; i > 0; --i)
    {
        T tmp = data[0];
        data[0] = data[i];
        data[i] = tmp;
        sift_down(data, 0, i, cmp);
    }
}

template <typename T, typename Compare>
size_t partition_3way(T* data, size_t lo, size_t hi,
                      size_t& pivot_lo, size_t& pivot_hi,
                      Compare& cmp) noexcept
{
    size_t mid = lo + (hi - lo) / 2;

    if (cmp(data[mid], data[lo]))
    {
        T tmp = data[lo]; data[lo] = data[mid]; data[mid] = tmp;
    }
    if (cmp(data[hi], data[lo]))
    {
        T tmp = data[lo]; data[lo] = data[hi]; data[hi] = tmp;
    }
    if (cmp(data[hi], data[mid]))
    {
        T tmp = data[mid]; data[mid] = data[hi]; data[hi] = tmp;
    }

    T pivot = data[mid];
    data[mid] = data[hi - 1];
    data[hi - 1] = pivot;

    size_t i = lo;
    size_t lt = lo;
    size_t gt = hi - 1;

    while (i <= gt)
    {
        if (cmp(data[i], pivot))
        {
            T tmp = data[lt]; data[lt] = data[i]; data[i] = tmp;
            ++lt;
            ++i;
        }
        else if (cmp(pivot, data[i]))
        {
            T tmp = data[i]; data[i] = data[gt]; data[gt] = tmp;
            --gt;
        }
        else
        {
            ++i;
        }
    }

    pivot_lo = lt;
    pivot_hi = gt;
    return gt;
}

template <typename T, typename Compare>
void pdqsort_impl(T* data, size_t lo, size_t hi, size_t depth, Compare& cmp) noexcept
{
    while (lo < hi)
    {
        if (hi - lo < 32)
        {
            insertion_sort(data + lo, hi - lo + 1, cmp);
            return;
        }

        if (depth == 0)
        {
            heap_sort(data + lo, hi - lo + 1, cmp);
            return;
        }

        size_t pivot_lo, pivot_hi;
        partition_3way(data, lo, hi, pivot_lo, pivot_hi, cmp);

        size_t left_size = pivot_lo - lo;
        size_t right_size = hi - pivot_hi;
        size_t mid_size = pivot_hi - pivot_lo + 1;

        if (mid_size > (hi - lo + 1) / 2)
        {
            if (left_size < right_size)
            {
                if (left_size > 0)
                    pdqsort_impl(data, lo, pivot_lo - 1, depth - 1, cmp);
                lo = pivot_hi + 1;
            }
            else
            {
                if (right_size > 0)
                    pdqsort_impl(data, pivot_hi + 1, hi, depth - 1, cmp);
                if (left_size > 0)
                    hi = pivot_lo - 1;
                else
                    return;
            }
        }
        else
        {
            if (left_size < right_size)
            {
                if (left_size > 0)
                    pdqsort_impl(data, lo, pivot_lo - 1, depth - 1, cmp);
                if (right_size > 0)
                    pdqsort_impl(data, pivot_hi + 1, hi, depth - 1, cmp);
                return;
            }
            else
            {
                if (right_size > 0)
                    pdqsort_impl(data, pivot_hi + 1, hi, depth - 1, cmp);
                if (left_size > 0)
                    pdqsort_impl(data, lo, pivot_lo - 1, depth - 1, cmp);
                return;
            }
        }
    }
}

inline size_t pdqsort_depth_limit(size_t n) noexcept
{
    size_t limit = 0;
    while (n > 1)
    {
        n >>= 1;
        ++limit;
    }
    return 2 * limit;
}

template <typename T>
void insertion_sort_indices(size_t* indices, const T* values, size_t n) noexcept
{
    for (size_t i = 1; i < n; ++i)
    {
        size_t key_idx = indices[i];
        T key_val = values[key_idx];
        size_t j = i;
        while (j > 0 && key_val < values[indices[j - 1]])
        {
            indices[j] = indices[j - 1];
            --j;
        }
        indices[j] = key_idx;
    }
}

template <typename T>
size_t partition_3way_indices(size_t* indices, size_t lo, size_t hi,
                              size_t& pivot_lo, size_t& pivot_hi,
                              const T* values) noexcept
{
    size_t mid = lo + (hi - lo) / 2;

    if (values[indices[mid]] < values[indices[lo]])
    {
        size_t tmp = indices[lo]; indices[lo] = indices[mid]; indices[mid] = tmp;
    }
    if (values[indices[hi]] < values[indices[lo]])
    {
        size_t tmp = indices[lo]; indices[lo] = indices[hi]; indices[hi] = tmp;
    }
    if (values[indices[hi]] < values[indices[mid]])
    {
        size_t tmp = indices[mid]; indices[mid] = indices[hi]; indices[hi] = tmp;
    }

    T pivot = values[indices[mid]];

    {
        size_t tmp = indices[mid];
        indices[mid] = indices[hi - 1];
        indices[hi - 1] = tmp;
    }

    pivot = values[indices[hi - 1]];
    size_t i = lo;
    size_t lt = lo;
    size_t gt = hi - 1;

    while (i <= gt)
    {
        if (values[indices[i]] < pivot)
        {
            size_t tmp = indices[lt]; indices[lt] = indices[i]; indices[i] = tmp;
            ++lt;
            ++i;
        }
        else if (pivot < values[indices[i]])
        {
            size_t tmp = indices[i]; indices[i] = indices[gt]; indices[gt] = tmp;
            --gt;
        }
        else
        {
            ++i;
        }
    }

    pivot_lo = lt;
    pivot_hi = gt;
    return gt;
}

template <typename T>
void sift_down_indices(size_t* indices, size_t i, size_t n,
                       const T* values) noexcept
{
    while (true)
    {
        size_t left = 2 * i + 1;
        size_t right = left + 1;
        size_t largest = i;

        if (left < n && values[indices[largest]] < values[indices[left]])
        {
            largest = left;
        }
        if (right < n && values[indices[largest]] < values[indices[right]])
        {
            largest = right;
        }

        if (largest == i)
        {
            break;
        }

        size_t tmp = indices[i];
        indices[i] = indices[largest];
        indices[largest] = tmp;
        i = largest;
    }
}

template <typename T>
void heap_sort_indices(size_t* indices, size_t n, const T* values) noexcept
{
    for (size_t i = n / 2; i-- > 0;)
    {
        sift_down_indices(indices, i, n, values);
    }

    for (size_t i = n - 1; i > 0; --i)
    {
        size_t tmp = indices[0];
        indices[0] = indices[i];
        indices[i] = tmp;
        sift_down_indices(indices, 0, i, values);
    }
}

template <typename T>
void pdqsort_indices_impl(size_t* indices, size_t lo, size_t hi,
                           size_t depth, const T* values) noexcept
{
    while (lo < hi)
    {
        if (hi - lo < 32)
        {
            insertion_sort_indices(indices + lo, values, hi - lo + 1);
            return;
        }

        if (depth == 0)
        {
            heap_sort_indices(indices + lo, hi - lo + 1, values);
            return;
        }

        size_t pivot_lo, pivot_hi;
        partition_3way_indices(indices, lo, hi, pivot_lo, pivot_hi, values);

        size_t left_size = pivot_lo - lo;
        size_t right_size = hi - pivot_hi;
        size_t mid_size = pivot_hi - pivot_lo + 1;

        if (mid_size > (hi - lo + 1) / 2)
        {
            if (left_size < right_size)
            {
                if (left_size > 0)
                    pdqsort_indices_impl(indices, lo, pivot_lo - 1, depth - 1, values);
                lo = pivot_hi + 1;
            }
            else
            {
                if (right_size > 0)
                    pdqsort_indices_impl(indices, pivot_hi + 1, hi, depth - 1, values);
                if (left_size > 0)
                    hi = pivot_lo - 1;
                else
                    return;
            }
        }
        else
        {
            if (left_size < right_size)
            {
                if (left_size > 0)
                    pdqsort_indices_impl(indices, lo, pivot_lo - 1, depth - 1, values);
                if (right_size > 0)
                    pdqsort_indices_impl(indices, pivot_hi + 1, hi, depth - 1, values);
                return;
            }
            else
            {
                if (right_size > 0)
                    pdqsort_indices_impl(indices, pivot_hi + 1, hi, depth - 1, values);
                if (left_size > 0)
                    pdqsort_indices_impl(indices, lo, pivot_lo - 1, depth - 1, values);
                return;
            }
        }
    }
}

struct net_pair
{
    size_t i;
    size_t j;
};

template <size_t N>
constexpr size_t batcher_network_count() noexcept
{
    size_t count = 0;
    for (size_t p = 1; p < N; p *= 2)
    {
        for (size_t k = p; k > 0; k /= 2)
        {
            for (size_t j = k % p; j + k < N; j += 2 * k)
            {
                for (size_t i = 0; i + j + k < N && i < k; ++i)
                {
                    if ((i + j) / (2 * p) == (i + j + k) / (2 * p))
                    {
                        ++count;
                    }
                }
            }
        }
    }
    return count;
}

template <size_t N, size_t Count>
struct network_storage
{
    net_pair data[Count];
};

template <size_t N>
constexpr auto batcher_network_generate() noexcept
{
    constexpr size_t count = batcher_network_count<N>();
    network_storage<N, count> storage{};
    size_t idx = 0;
    for (size_t p = 1; p < N; p *= 2)
    {
        for (size_t k = p; k > 0; k /= 2)
        {
            for (size_t j = k % p; j + k < N; j += 2 * k)
            {
                for (size_t i = 0; i + j + k < N && i < k; ++i)
                {
                    if ((i + j) / (2 * p) == (i + j + k) / (2 * p))
                    {
                        storage.data[idx++] = {i + j, i + j + k};
                    }
                }
            }
        }
    }
    return storage;
}

template <size_t N>
constexpr auto batcher_network_v = batcher_network_generate<N>();

template <typename T, typename Compare>
FORCE_INLINE void net_cmp_swap(T* data, size_t i, size_t j, Compare& cmp) noexcept
{
    if (cmp(data[j], data[i]))
    {
        T tmp = std::move(data[i]);
        data[i] = std::move(data[j]);
        data[j] = std::move(tmp);
    }
}

template <size_t N, typename T, typename Compare>
FORCE_INLINE void sorting_network(T* data, Compare& cmp) noexcept
{
    if constexpr (N <= 1)
    {
        return;
    }
    else
    {
        constexpr auto net = batcher_network_v<N>;
        [&]<size_t... Is>(std::index_sequence<Is...>)
        {
            (net_cmp_swap(data, net.data[Is].i, net.data[Is].j, cmp), ...);
        }(std::make_index_sequence<batcher_network_count<N>()>{});
    }
}

template <typename T>
FORCE_INLINE void net_cmp_swap_indices(size_t* indices, const T* values,
                                       size_t i, size_t j) noexcept
{
    if (values[indices[j]] < values[indices[i]])
    {
        size_t tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }
}

template <size_t N, typename T>
FORCE_INLINE void sorting_network_indices(size_t* indices, const T* values) noexcept
{
    if constexpr (N <= 1)
    {
        return;
    }
    else
    {
        constexpr auto net = batcher_network_v<N>;
        [&]<size_t... Is>(std::index_sequence<Is...>)
        {
            (net_cmp_swap_indices(indices, values, net.data[Is].i, net.data[Is].j), ...);
        }(std::make_index_sequence<batcher_network_count<N>()>{});
    }
}

template <typename T, typename Compare>
void dispatch_sorting_network(T* data, size_t n, Compare& cmp) noexcept
{
    switch (n)
    {
    case 0:
    case 1:
        return;
    case 2: sorting_network<2>(data, cmp); return;
    case 3: sorting_network<3>(data, cmp); return;
    case 4: sorting_network<4>(data, cmp); return;
    case 5: sorting_network<5>(data, cmp); return;
    case 6: sorting_network<6>(data, cmp); return;
    case 7: sorting_network<7>(data, cmp); return;
    case 8: sorting_network<8>(data, cmp); return;
    case 9: sorting_network<9>(data, cmp); return;
    case 10: sorting_network<10>(data, cmp); return;
    case 11: sorting_network<11>(data, cmp); return;
    case 12: sorting_network<12>(data, cmp); return;
    case 13: sorting_network<13>(data, cmp); return;
    case 14: sorting_network<14>(data, cmp); return;
    case 15: sorting_network<15>(data, cmp); return;
    case 16: sorting_network<16>(data, cmp); return;
    default: insertion_sort(data, n, cmp); return;
    }
}

template <typename T>
void dispatch_sorting_network_indices(size_t* indices, const T* values, size_t n) noexcept
{
    switch (n)
    {
    case 0:
    case 1:
        return;
    case 2: sorting_network_indices<2>(indices, values); return;
    case 3: sorting_network_indices<3>(indices, values); return;
    case 4: sorting_network_indices<4>(indices, values); return;
    case 5: sorting_network_indices<5>(indices, values); return;
    case 6: sorting_network_indices<6>(indices, values); return;
    case 7: sorting_network_indices<7>(indices, values); return;
    case 8: sorting_network_indices<8>(indices, values); return;
    case 9: sorting_network_indices<9>(indices, values); return;
    case 10: sorting_network_indices<10>(indices, values); return;
    case 11: sorting_network_indices<11>(indices, values); return;
    case 12: sorting_network_indices<12>(indices, values); return;
    case 13: sorting_network_indices<13>(indices, values); return;
    case 14: sorting_network_indices<14>(indices, values); return;
    case 15: sorting_network_indices<15>(indices, values); return;
    case 16: sorting_network_indices<16>(indices, values); return;
    default: insertion_sort_indices(indices, values, n); return;
    }
}

}

template <typename T, typename Compare>
void pdqsort(T* data, size_t n, Compare&& cmp) noexcept
    requires std::is_trivially_copyable_v<T>
{
    if (n < 2)
    {
        return;
    }
    auto& cmp_ref = cmp;
    detail::pdqsort_impl(data, 0, n - 1, detail::pdqsort_depth_limit(n), cmp_ref);
}

template <typename T, typename Compare>
void tiered_sort(T* data, size_t n, Compare&& cmp) noexcept
{
    if (n < 2)
    {
        return;
    }
    if (n <= 16)
    {
        detail::dispatch_sorting_network(data, n, cmp);
        return;
    }
    if (n < 1024)
    {
        detail::pdqsort_impl(data, 0, n - 1,
                             detail::pdqsort_depth_limit(n), cmp);
    }
    else
    {
        if constexpr (is_radix_sortable_v<T>)
        {
            struct sort_entry { T key; size_t index; };
            auto* entries = static_cast<sort_entry*>(
                ::operator new(n * sizeof(sort_entry),
                               std::align_val_t{alignof(sort_entry)},
                               std::nothrow));
            if (!entries) [[unlikely]]
            {
                detail::pdqsort_impl(data, 0, n - 1,
                                     detail::pdqsort_depth_limit(n), cmp);
                return;
            }

            for (size_t i = 0; i < n; ++i)
            {
                new (&entries[i]) sort_entry{data[i], i};
            }

            radix_sort_entries<T>(entries, n);

            for (size_t i = 0; i < n; ++i)
            {
                data[i] = entries[i].key;
            }

            ::operator delete(entries, n * sizeof(sort_entry),
                              std::align_val_t{alignof(sort_entry)});
        }
        else
        {
            detail::pdqsort_impl(data, 0, n - 1,
                                 detail::pdqsort_depth_limit(n), cmp);
        }
    }
}

template <typename T>
void tiered_sort_indices(size_t* indices, const T* values, size_t n) noexcept
{
    if (n < 2)
    {
        return;
    }
    if (n <= 16)
    {
        detail::dispatch_sorting_network_indices(indices, values, n);
        return;
    }
    if (n < 1024)
    {
        detail::pdqsort_indices_impl(indices, 0, n - 1,
                                     detail::pdqsort_depth_limit(n), values);
    }
    else
    {
        if constexpr (is_radix_sortable_v<T>)
        {
            auto* temp_buf = static_cast<size_t*>(
                ::operator new(n * sizeof(size_t), std::align_val_t{alignof(size_t)},
                               std::nothrow));
            if (!temp_buf) [[unlikely]]
            {
                detail::pdqsort_indices_impl(indices, 0, n - 1,
                                             detail::pdqsort_depth_limit(n), values);
                return;
            }
            radix_sort_indices<T>(indices, values, n, temp_buf);
            ::operator delete(temp_buf, n * sizeof(size_t),
                              std::align_val_t{alignof(size_t)});
        }
        else
        {
            detail::pdqsort_indices_impl(indices, 0, n - 1,
                                         detail::pdqsort_depth_limit(n), values);
        }
    }
}

// 统一排序接口: 自动判断数量级选择算法
template <typename T, typename Compare = std::less<T>>
inline void sort(T* data, size_t n, Compare&& cmp = {}) noexcept
{
    tiered_sort(data, n, std::forward<Compare>(cmp));
}

template <typename T>
inline void sort_indices(size_t* indices, const T* values, size_t n) noexcept
{
    tiered_sort_indices(indices, values, n);
}

// 编译期已知 N 的零开销排序接口
template <size_t N, typename T, typename Compare = std::less<T>>
inline void sort_n(T* data, Compare&& cmp = {}) noexcept
{
    if constexpr (N <= 1)
    {
        return;
    }
    else if constexpr (N <= 16)
    {
        detail::sorting_network<N>(data, cmp);
    }
    else if constexpr (N < 32)
    {
        detail::insertion_sort(data, N, cmp);
    }
    else if constexpr (N < 1024)
    {
        detail::pdqsort_impl(data, 0, N - 1,
                             detail::pdqsort_depth_limit(N), cmp);
    }
    else
    {
        tiered_sort(data, N, std::forward<Compare>(cmp));
    }
}

template <size_t N, typename T>
inline void sort_indices_n(size_t* indices, const T* values) noexcept
{
    if constexpr (N <= 1)
    {
        return;
    }
    else if constexpr (N <= 16)
    {
        detail::sorting_network_indices<N>(indices, values);
    }
    else if constexpr (N < 32)
    {
        detail::insertion_sort_indices(indices, values, N);
    }
    else if constexpr (N < 1024)
    {
        detail::pdqsort_indices_impl(indices, 0, N - 1,
                                     detail::pdqsort_depth_limit(N), values);
    }
    else
    {
        tiered_sort_indices(indices, values, N);
    }
}
