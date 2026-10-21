#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "dense.hpp"

// id_table - 以整型 id 为键的稠密条目表（单线程）
// 条目连续存储, 额外维护 id → 下标 与 下标 → id 两张索引

template <typename T>
class id_table
{
public:
    id_table() noexcept = default;

    id_table(const id_table&) = delete;
    id_table& operator=(const id_table&) = delete;

    // 按 id 插入或更新；返回是否新建
    template <typename U> bool ensure(int id, U&& value) noexcept
    {
        if (id < 0)
        {
            return false;
        }
        grow_index(id);
        const int32_t existing = index_[static_cast<size_t>(id)];
        if (existing >= 0)
        {
            values_[static_cast<size_t>(existing)] = T(std::forward<U>(value));
            return false;
        }
        const size_t next = values_.size();
        values_.push_back(T(std::forward<U>(value)));
        ids_.push_back(id);
        index_[static_cast<size_t>(id)] = static_cast<int32_t>(next);
        return true;
    }

    T* find(int id) noexcept
    {
        const int32_t at = index_of(id);
        return at >= 0 ? &values_[static_cast<size_t>(at)] : nullptr;
    }

    const T* find(int id) const noexcept
    {
        const int32_t at = index_of(id);
        return at >= 0 ? &values_[static_cast<size_t>(at)] : nullptr;
    }

    int32_t index_of(int id) const noexcept
    {
        if (id < 0 || static_cast<size_t>(id) >= index_.size())
        {
            return -1;
        }
        return index_[static_cast<size_t>(id)];
    }

    int id_at(size_t index) const noexcept
    {
        return index < ids_.size() ? ids_[index] : -1;
    }

    const T& at(size_t index) const noexcept
    {
        return values_[index];
    }

    size_t size() const noexcept
    {
        return values_.size();
    }

    bool empty() const noexcept
    {
        return values_.empty();
    }

    void clear() noexcept
    {
        values_.clear();
        ids_.clear();
        index_.clear();
    }

    template <typename F> void for_each(F&& f) const noexcept
    {
        for (size_t i = 0; i < values_.size(); ++i)
        {
            f(ids_[i], values_[i]);
        }
    }

private:
    void grow_index(int id) noexcept
    {
        const size_t need = static_cast<size_t>(id) + 1;
        if (index_.size() < need)
        {
            index_.resize_fill(need, -1);
        }
    }

    dense<T> values_{};
    dense<int32_t> ids_{};
    dense<int32_t> index_{};
};
