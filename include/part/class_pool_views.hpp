#pragma once
#include "class_pool.hpp"
#include "container_views.hpp"
#include "force_inline.hpp"
#include <span>
#include <concepts>
#include <utility>
#include <functional>
#include <iterator>
#include <bit>
#include <cstdint>

template <typename T>
struct pool_strided_span
{
	class_pool<T>* pool_{nullptr};
	size_t start_{0};
	size_t step_{0};
	size_t count_{0};

	constexpr pool_strided_span(class_pool<T>* p, size_t s, size_t st, size_t c) noexcept
		: pool_(p), start_(s), step_(st), count_(c) {}

	[[nodiscard]] constexpr size_t size() const noexcept { return count_; }
	[[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

	[[nodiscard]] inline T& operator[](size_t i) noexcept
	{
		return (*pool_)[start_ + i * step_];
	}

	template <typename F>
	LCF_FLATTEN void for_each(F&& f) noexcept
	{
		::strided_for_each(*pool_, start_, step_, std::forward<F>(f));
	}

	template <typename F>
	LCF_FLATTEN void for_each(F&& f) const noexcept
	{
		::strided_for_each(*pool_, start_, step_, std::forward<F>(f));
	}
};

template <typename T>
[[nodiscard]] inline pool_strided_span<T> strided_span_view(
    class_pool<T>& pool, size_t start, size_t step, size_t count) noexcept
{
	// start 越界收敛, step=0 视为空视图, count 收敛到可用槽位数
	const size_t size = pool.size();
	const size_t s = start > size ? size : start;
	const size_t max_cnt = (step > 0) ? (size - s) / step : 0;
	const size_t cnt = count > max_cnt ? max_cnt : count;
	return pool_strided_span<T>(&pool, s, step ? step : 1, cnt);
}


template <typename T, typename Pred>
    requires std::predicate<Pred, const T&>
inline void filter_indices_to(class_pool<T>& pool, class_pool<size_t>& dst, Pred pred) noexcept
{
	for (auto it = pool.begin(); it != pool.end(); ++it)
	{
		if (pred(*it)) [[likely]]
		{
			const size_t idx = static_cast<size_t>(&(*it) - pool.data());
			dst.push_back_unchecked(idx);
		}
	}
}


template <typename T>
[[nodiscard]] inline size_t compact_to(const class_pool<T>& pool, T* dst, size_t count) noexcept
{
	size_t di = 0;
	const size_t cap = count;
	for (auto it = pool.cbegin(); it != pool.cend() && di < cap; ++it, ++di)
	{
		new (&dst[di]) T(*it);
	}
	return di;
}

// 活跃元素数 (语义等价 pool.count()), class_pool 独有
template <typename T>
[[nodiscard]] inline size_t live_count(const class_pool<T>& pool) noexcept
{
	return pool.count();
}

// 空洞数 = 高水位 - 活跃数, class_pool 独有
template <typename T>
[[nodiscard]] inline size_t holes_count(const class_pool<T>& pool) noexcept
{
	return pool.size() - pool.count();
}
