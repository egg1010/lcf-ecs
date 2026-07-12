#pragma once
#include "radix_sort_helper.hpp"
#include <cstddef>
#include <type_traits>

namespace ecs
{

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
    if (n < 32)
    {
        detail::insertion_sort(data, n, cmp);
    }
    else if (n < 256)
    {
        if constexpr (is_radix_sortable_v<T>)
        {
            detail::insertion_sort_ascending(data, n);
        }
        else
        {
            detail::pdqsort_impl(data, 0, n - 1,
                                 detail::pdqsort_depth_limit(n), cmp);
        }
    }
    else if (n < 4096)
    {
        detail::pdqsort_impl(data, 0, n - 1,
                             detail::pdqsort_depth_limit(n), cmp);
    }
    else if (n < 65536)
    {
        if constexpr (is_radix_sortable_v<T>)
        {
            struct sort_entry { T key; size_t index; };
            auto* buf = static_cast<sort_entry*>(
                ::operator new(n * sizeof(sort_entry) * 2,
                               std::align_val_t{alignof(sort_entry)},
                               std::nothrow));
            if (!buf) [[unlikely]]
            {
                detail::pdqsort_impl(data, 0, n - 1,
                                     detail::pdqsort_depth_limit(n), cmp);
                return;
            }
            auto* entries = buf;
            auto* temp = buf + n;

            for (size_t i = 0; i < n; ++i)
            {
                new (&entries[i]) sort_entry{data[i], i};
            }

            radix_sort_entries<T>(entries, n);

            for (size_t i = 0; i < n; ++i)
            {
                data[i] = entries[i].key;
            }

            ::operator delete(buf, n * sizeof(sort_entry) * 2,
                              std::align_val_t{alignof(sort_entry)});
        }
        else
        {
            detail::pdqsort_impl(data, 0, n - 1,
                                 detail::pdqsort_depth_limit(n), cmp);
        }
    }
    else
    {
        if constexpr (is_radix_sortable_v<T>)
        {
            struct sort_entry { T key; size_t index; };
            auto* buf = static_cast<sort_entry*>(
                ::operator new(n * sizeof(sort_entry) * 2,
                               std::align_val_t{alignof(sort_entry)},
                               std::nothrow));
            if (!buf) [[unlikely]]
            {
                detail::pdqsort_impl(data, 0, n - 1,
                                     detail::pdqsort_depth_limit(n), cmp);
                return;
            }
            auto* entries = buf;
            auto* temp = buf + n;

            for (size_t i = 0; i < n; ++i)
            {
                new (&entries[i]) sort_entry{data[i], i};
            }

            radix_sort_entries<T>(entries, n);

            for (size_t i = 0; i < n; ++i)
            {
                data[i] = entries[i].key;
            }

            ::operator delete(buf, n * sizeof(sort_entry) * 2,
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
    if (n < 32)
    {
        detail::insertion_sort_indices(indices, values, n);
    }
    else if (n < 4096)
    {
        detail::pdqsort_indices_impl(indices, 0, n - 1,
                                     detail::pdqsort_depth_limit(n), values);
    }
    else if (n < 65536)
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

}
