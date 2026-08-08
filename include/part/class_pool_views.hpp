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
		if (pool_->is_dense()) [[likely]]
		{
			T* LCF_RESTRICT p = pool_->data() + start_;
			const size_t step = step_;
			const size_t n = count_;
			for (size_t i = 0; i < n; ++i)
			{
				if constexpr (sizeof(T) >= 16)
				{
					if (i + 8 < n) [[likely]] { LCF_PREFETCH_R(p + 8 * step); }
				}
				f(*p);
				p += step;
			}
		}
		else
		{
			const size_t n = count_;
			for (size_t i = 0; i < n; ++i)
			{
				const size_t slot = start_ + i * step_;
				if (pool_->is_constructed_at(slot)) [[likely]]
				{
					f((*pool_)[slot]);
				}
			}
		}
	}
};

template <typename T>
[[nodiscard]] inline pool_strided_span<T> strided_span_view(
    class_pool<T>& pool, size_t start, size_t step, size_t count) noexcept
{
	return pool_strided_span<T>(&pool, start, step, count);
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
