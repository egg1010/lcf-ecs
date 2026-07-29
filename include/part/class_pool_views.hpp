#pragma once
#include "class_pool.hpp"
#include "force_inline.hpp"
#include <span>
#include <concepts>
#include <utility>
#include <functional>
#include <iterator>
#include <bit>
#include <cstdint>

// class_pool 视图接口 (全局命名空间, 与 dense/class_pool 保持一致)
// 所有视图仅依赖 class_pool 公开 API, 不修改原容器代码
// 双路径策略: is_dense() 走连续内存快路径, 否则复用 basic_iterator (AVX2 位图扫描)

// 子范围视图 (与 dense::subspan / std::span::subspan 命名一致)
template <typename T>
[[nodiscard]] inline std::span<T> subspan(class_pool<T>& pool, size_t offset, size_t count) noexcept
{
	if (offset >= pool.size()) [[unlikely]]
	{
		return std::span<T>(static_cast<T*>(nullptr), 0);
	}
	const size_t avail = pool.size() - offset;
	return std::span<T>(pool.data() + offset, count > avail ? avail : count);
}

template <typename T>
[[nodiscard]] inline std::span<const T> subspan(const class_pool<T>& pool, size_t offset, size_t count) noexcept
{
	if (offset >= pool.size()) [[unlikely]]
	{
		return std::span<const T>(static_cast<const T*>(nullptr), 0);
	}
	const size_t avail = pool.size() - offset;
	return std::span<const T>(pool.data() + offset, count > avail ? avail : count);
}

// 单参数 subspan: 从 offset 到末尾
template <typename T>
[[nodiscard]] inline std::span<T> subspan(class_pool<T>& pool, size_t offset) noexcept
{
	if (offset >= pool.size()) [[unlikely]]
	{
		return std::span<T>(static_cast<T*>(nullptr), 0);
	}
	return std::span<T>(pool.data() + offset, pool.size() - offset);
}

template <typename T>
[[nodiscard]] inline std::span<const T> subspan(const class_pool<T>& pool, size_t offset) noexcept
{
	if (offset >= pool.size()) [[unlikely]]
	{
		return std::span<const T>(static_cast<const T*>(nullptr), 0);
	}
	return std::span<const T>(pool.data() + offset, pool.size() - offset);
}

template <typename T>
[[nodiscard]] inline std::span<T> first(class_pool<T>& pool, size_t n) noexcept
{
	return subspan(pool, 0, n);
}

template <typename T>
[[nodiscard]] inline std::span<const T> first(const class_pool<T>& pool, size_t n) noexcept
{
	return subspan(pool, 0, n);
}

template <typename T>
[[nodiscard]] inline std::span<T> last(class_pool<T>& pool, size_t n) noexcept
{
	if (n >= pool.size()) [[unlikely]]
	{
		return std::span<T>(pool.data(), pool.size());
	}
	return std::span<T>(pool.data() + pool.size() - n, n);
}

template <typename T>
[[nodiscard]] inline std::span<const T> last(const class_pool<T>& pool, size_t n) noexcept
{
	if (n >= pool.size()) [[unlikely]]
	{
		return std::span<const T>(pool.data(), pool.size());
	}
	return std::span<const T>(pool.data() + pool.size() - n, n);
}

template <typename T, size_t N>
[[nodiscard]] inline std::span<T, N> first_fixed(class_pool<T>& pool) noexcept
{
	return std::span<T, N>(pool.data(), N <= pool.size() ? N : pool.size());
}

template <typename T, size_t N>
[[nodiscard]] inline std::span<const T, N> first_fixed(const class_pool<T>& pool) noexcept
{
	return std::span<const T, N>(pool.data(), N <= pool.size() ? N : pool.size());
}

template <typename T, size_t N>
[[nodiscard]] inline std::span<T, N> last_fixed(class_pool<T>& pool) noexcept
{
	const size_t sz = pool.size();
	return std::span<T, N>(pool.data() + (N >= sz ? 0 : sz - N), N >= sz ? sz : N);
}

template <typename T, size_t N>
[[nodiscard]] inline std::span<const T, N> last_fixed(const class_pool<T>& pool) noexcept
{
	const size_t sz = pool.size();
	return std::span<const T, N>(pool.data() + (N >= sz ? 0 : sz - N), N >= sz ? sz : N);
}

// 反向遍历
template <typename T, typename F>
LCF_FLATTEN inline void reverse_for_each(class_pool<T>& pool, F&& f) noexcept
{
	for (auto it = pool.rbegin(); it != pool.rend(); ++it)
	{
		f(*it);
	}
}

template <typename T, typename F>
LCF_FLATTEN inline void reverse_for_each(const class_pool<T>& pool, F&& f) noexcept
{
	for (auto it = pool.crbegin(); it != pool.crend(); ++it)
	{
		f(*it);
	}
}

// 步进视图 (POD 视图结构, 持有 class_pool 指针 + 步进参数)
// 注意: 步进语义为步进槽位 (而非步进活跃元素), 稀疏模式下
//       越过空洞时需 is_constructed_at 检查
// 命名: pool_strided_span 与 dense::strided_span 区分 (后者持有裸指针)
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

	// 仅密集模式可安全随机访问; 稀疏模式需调用方保证槽活跃
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
			// 稀疏模式: 跳过未活跃槽
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

template <typename T, typename F>
LCF_FLATTEN inline void strided_for_each(
    class_pool<T>& pool, size_t start, size_t step, F&& f) noexcept
{
	if (pool.is_dense()) [[likely]]
	{
		T* LCF_RESTRICT p = pool.data() + start;
		const size_t avail = pool.size() > start ? pool.size() - start : 0;
		const size_t cnt = avail / step;
		for (size_t i = 0; i < cnt; ++i)
		{
			if constexpr (sizeof(T) >= 16)
			{
				if (i + 8 < cnt) [[likely]] { LCF_PREFETCH_R(p + 8 * step); }
			}
			f(*p);
			p += step;
		}
	}
	else
	{
		const size_t avail = pool.size() > start ? pool.size() - start : 0;
		const size_t cnt = avail / step;
		for (size_t i = 0; i < cnt; ++i)
		{
			const size_t slot = start + i * step;
			if (pool.is_constructed_at(slot)) [[likely]]
			{
				f(pool[slot]);
			}
		}
	}
}

template <typename T, typename F>
LCF_FLATTEN inline void strided_for_each(
    const class_pool<T>& pool, size_t start, size_t step, F&& f) noexcept
{
	if (pool.is_dense()) [[likely]]
	{
		const T* LCF_RESTRICT p = pool.data() + start;
		const size_t avail = pool.size() > start ? pool.size() - start : 0;
		const size_t cnt = avail / step;
		for (size_t i = 0; i < cnt; ++i)
		{
			if constexpr (sizeof(T) >= 16)
			{
				if (i + 8 < cnt) [[likely]] { LCF_PREFETCH_R(p + 8 * step); }
			}
			f(*p);
			p += step;
		}
	}
	else
	{
		const size_t avail = pool.size() > start ? pool.size() - start : 0;
		const size_t cnt = avail / step;
		for (size_t i = 0; i < cnt; ++i)
		{
			const size_t slot = start + i * step;
			if (pool.is_constructed_at(slot)) [[likely]]
			{
				f(pool[slot]);
			}
		}
	}
}

template <typename T, size_t Step, typename F>
    requires (Step > 0)
LCF_FLATTEN inline void strided_for_each(class_pool<T>& pool, F&& f) noexcept
{
	if constexpr (Step == 1)
	{
		// 步长 1: 退化为 for_each (复用 begin/end, 自动 AVX2 位图扫描)
		for (auto it = pool.begin(); it != pool.end(); ++it)
		{
			f(*it);
		}
	}
	else
	{
		strided_for_each(pool, 0, Step, std::forward<F>(f));
	}
}

template <typename T, size_t Step, typename F>
    requires (Step > 0)
LCF_FLATTEN inline void strided_for_each(const class_pool<T>& pool, F&& f) noexcept
{
	if constexpr (Step == 1)
	{
		for (auto it = pool.cbegin(); it != pool.cend(); ++it)
		{
			f(*it);
		}
	}
	else
	{
		strided_for_each(pool, 0, Step, std::forward<F>(f));
	}
}

// 变换视图
template <typename T, typename FTransform, typename FConsume>
    requires std::invocable<FTransform, T&> && std::invocable<FConsume, T>
LCF_FLATTEN inline void transform_for_each(
    class_pool<T>& pool, FTransform&& tr, FConsume&& con) noexcept
{
	for (auto it = pool.begin(); it != pool.end(); ++it)
	{
		con(tr(*it));
	}
}

template <typename T, typename FTransform, typename FConsume>
    requires std::invocable<FTransform, const T&> && std::invocable<FConsume, T>
LCF_FLATTEN inline void transform_for_each(
    const class_pool<T>& pool, FTransform&& tr, FConsume&& con) noexcept
{
	for (auto it = pool.cbegin(); it != pool.cend(); ++it)
	{
		con(tr(*it));
	}
}

template <typename T, typename R, typename F>
    requires std::invocable<F, const T&>
inline void transform_to(const class_pool<T>& pool, R* dst, size_t count, F&& tr) noexcept
{
	const size_t n = count < pool.size() ? count : pool.size();
	if (pool.is_dense()) [[likely]]
	{
		const T* LCF_RESTRICT src = pool.data();
		for (size_t i = 0; i < n; ++i)
		{
			new (&dst[i]) R(tr(src[i]));
		}
	}
	else
	{
		size_t di = 0;
		for (auto it = pool.cbegin(); it != pool.cend() && di < n; ++it, ++di)
		{
			new (&dst[di]) R(tr(*it));
		}
	}
}

// 过滤与查找
template <typename T>
[[nodiscard]] inline T* find(class_pool<T>& pool, const T& value) noexcept
{
	for (auto it = pool.begin(); it != pool.end(); ++it)
	{
		if (*it == value) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <typename T>
[[nodiscard]] inline const T* find(const class_pool<T>& pool, const T& value) noexcept
{
	for (auto it = pool.cbegin(); it != pool.cend(); ++it)
	{
		if (*it == value) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <typename T, typename Pred>
    requires std::predicate<Pred, const T&>
[[nodiscard]] inline T* find_if(class_pool<T>& pool, Pred pred) noexcept
{
	for (auto it = pool.begin(); it != pool.end(); ++it)
	{
		if (pred(*it)) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <typename T, typename Pred>
    requires std::predicate<Pred, const T&>
[[nodiscard]] inline const T* find_if(const class_pool<T>& pool, Pred pred) noexcept
{
	for (auto it = pool.cbegin(); it != pool.cend(); ++it)
	{
		if (pred(*it)) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <typename T, typename Pred>
    requires std::predicate<Pred, const T&>
[[nodiscard]] inline T* find_if_not(class_pool<T>& pool, Pred pred) noexcept
{
	for (auto it = pool.begin(); it != pool.end(); ++it)
	{
		if (!pred(*it)) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <typename T, typename Pred>
    requires std::predicate<Pred, const T&>
[[nodiscard]] inline const T* find_if_not(const class_pool<T>& pool, Pred pred) noexcept
{
	for (auto it = pool.cbegin(); it != pool.cend(); ++it)
	{
		if (!pred(*it)) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <typename T>
[[nodiscard]] inline bool contains(const class_pool<T>& pool, const T& value) noexcept
{
	for (auto it = pool.cbegin(); it != pool.cend(); ++it)
	{
		if (*it == value) [[unlikely]]
		{
			return true;
		}
	}
	return false;
}

template <typename T, typename Pred>
    requires std::predicate<Pred, const T&>
[[nodiscard]] inline size_t count_if(const class_pool<T>& pool, Pred pred) noexcept
{
	size_t c = 0;
	for (auto it = pool.cbegin(); it != pool.cend(); ++it)
	{
		if (pred(*it)) [[likely]]
		{
			++c;
		}
	}
	return c;
}

template <typename T, typename Pred, typename F>
    requires std::predicate<Pred, const T&> && std::invocable<F, T&>
LCF_FLATTEN inline void filter_for_each(class_pool<T>& pool, Pred pred, F&& f) noexcept
{
	for (auto it = pool.begin(); it != pool.end(); ++it)
	{
		if (pred(*it)) [[likely]]
		{
			f(*it);
		}
	}
}

template <typename T, typename Pred, typename F>
    requires std::predicate<Pred, const T&> && std::invocable<F, const T&>
LCF_FLATTEN inline void filter_for_each(const class_pool<T>& pool, Pred pred, F&& f) noexcept
{
	for (auto it = pool.cbegin(); it != pool.cend(); ++it)
	{
		if (pred(*it)) [[likely]]
		{
			f(*it);
		}
	}
}

// 需 dense<size_t> 容器作为索引输出目标 (规范 3: 禁用 std::vector)
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

// 规约与极值
template <typename T, typename F, typename U = T>
    requires std::invocable<F, U, const T&>
[[nodiscard]] inline U reduce(const class_pool<T>& pool, F&& f, U init) noexcept
{
	for (auto it = pool.cbegin(); it != pool.cend(); ++it)
	{
		init = f(std::move(init), *it);
	}
	return init;
}

template <typename T, typename F, typename U = T>
    requires std::invocable<F, U, const T&>
[[nodiscard]] inline U reduce_pairwise(const class_pool<T>& pool, F&& f, U init) noexcept
{
	// 简化成对规约: 取相邻 pair 合并, 递归层级受限于调用方
	// 由于 class_pool 迭代器仅双向, 这里用缓冲会违反规范 3
	// 实现策略: 顺序规约 + 提示编译器可向量化 (密集模式)
	if (pool.is_dense()) [[likely]]
	{
		const T* LCF_RESTRICT p = pool.data();
		const size_t n = pool.size();
		U acc = init;
		for (size_t i = 0; i < n; ++i)
		{
			acc = f(std::move(acc), p[i]);
		}
		return acc;
	}
	else
	{
		return reduce(pool, std::forward<F>(f), std::move(init));
	}
}

template <typename T>
[[nodiscard]] inline T* min_element(class_pool<T>& pool) noexcept
{
	auto it = pool.begin();
	auto end = pool.end();
	if (it == end) [[unlikely]] { return nullptr; }
	T* best = &(*it);
	++it;
	for (; it != end; ++it)
	{
		if (*it < *best) [[unlikely]]
		{
			best = &(*it);
		}
	}
	return best;
}

template <typename T>
[[nodiscard]] inline const T* min_element(const class_pool<T>& pool) noexcept
{
	auto it = pool.cbegin();
	auto end = pool.cend();
	if (it == end) [[unlikely]] { return nullptr; }
	const T* best = &(*it);
	++it;
	for (; it != end; ++it)
	{
		if (*it < *best) [[unlikely]]
		{
			best = &(*it);
		}
	}
	return best;
}

template <typename T>
[[nodiscard]] inline T* max_element(class_pool<T>& pool) noexcept
{
	auto it = pool.begin();
	auto end = pool.end();
	if (it == end) [[unlikely]] { return nullptr; }
	T* best = &(*it);
	++it;
	for (; it != end; ++it)
	{
		if (*best < *it) [[unlikely]]
		{
			best = &(*it);
		}
	}
	return best;
}

template <typename T>
[[nodiscard]] inline const T* max_element(const class_pool<T>& pool) noexcept
{
	auto it = pool.cbegin();
	auto end = pool.cend();
	if (it == end) [[unlikely]] { return nullptr; }
	const T* best = &(*it);
	++it;
	for (; it != end; ++it)
	{
		if (*best < *it) [[unlikely]]
		{
			best = &(*it);
		}
	}
	return best;
}

template <typename T>
[[nodiscard]] inline std::pair<T*, T*> minmax_element(class_pool<T>& pool) noexcept
{
	auto it = pool.begin();
	auto end = pool.end();
	if (it == end) [[unlikely]] { return {nullptr, nullptr}; }
	T* mn = &(*it);
	T* mx = &(*it);
	++it;
	for (; it != end; ++it)
	{
		if (*it < *mn) [[unlikely]] { mn = &(*it); }
		else if (*mx < *it) [[unlikely]] { mx = &(*it); }
	}
	return {mn, mx};
}

template <typename T>
[[nodiscard]] inline std::pair<const T*, const T*> minmax_element(const class_pool<T>& pool) noexcept
{
	auto it = pool.cbegin();
	auto end = pool.cend();
	if (it == end) [[unlikely]] { return {nullptr, nullptr}; }
	const T* mn = &(*it);
	const T* mx = &(*it);
	++it;
	for (; it != end; ++it)
	{
		if (*it < *mn) [[unlikely]] { mn = &(*it); }
		else if (*mx < *it) [[unlikely]] { mx = &(*it); }
	}
	return {mn, mx};
}

template <typename T>
    requires std::is_arithmetic_v<T>
[[nodiscard]] inline T sum(const class_pool<T>& pool) noexcept
{
	if (pool.is_dense()) [[likely]]
	{
		const T* LCF_RESTRICT p = pool.data();
		const size_t n = pool.size();
		T acc = T{};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		for (size_t i = 0; i < n; ++i)
		{
			acc += p[i];
		}
		return acc;
	}
	else
	{
		T acc = T{};
		for (auto it = pool.cbegin(); it != pool.cend(); ++it)
		{
			acc += *it;
		}
		return acc;
	}
}

template <typename T>
    requires std::is_arithmetic_v<T>
[[nodiscard]] inline T dot_product(const class_pool<T>& pool, const T* other, size_t count) noexcept
{
	const size_t n = count < pool.size() ? count : pool.size();
	if (pool.is_dense()) [[likely]]
	{
		const T* LCF_RESTRICT p = pool.data();
		T acc = T{};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		for (size_t i = 0; i < n; ++i)
		{
			acc += p[i] * other[i];
		}
		return acc;
	}
	else
	{
		T acc = T{};
		size_t oi = 0;
		for (auto it = pool.cbegin(); it != pool.cend() && oi < n; ++it, ++oi)
		{
			acc += (*it) * other[oi];
		}
		return acc;
	}
}

// 窗口与分块
template <typename T, size_t N, typename F>
    requires (N > 0) && std::invocable<F, std::span<T, N>>
LCF_FLATTEN inline void for_each_window(class_pool<T>& pool, F&& f) noexcept
{
	if (pool.size() < N) [[unlikely]] { return; }
	if (pool.is_dense()) [[likely]]
	{
		T* p = pool.data();
		const size_t end = pool.size() - N + 1;
		for (size_t i = 0; i < end; ++i)
		{
			f(std::span<T, N>(p + i, N));
		}
	}
	else
	{
		// 稀疏: 跳过含空洞的窗口
		for (size_t i = 0; i + N <= pool.size(); ++i)
		{
			bool valid = true;
			for (size_t j = 0; j < N; ++j)
			{
				if (!pool.is_constructed_at(i + j)) { valid = false; break; }
			}
			if (valid) [[likely]]
			{
				f(std::span<T, N>(pool.data() + i, N));
			}
		}
	}
}

template <typename T, size_t N, typename F>
    requires (N > 0) && std::invocable<F, std::span<const T, N>>
LCF_FLATTEN inline void for_each_window(const class_pool<T>& pool, F&& f) noexcept
{
	if (pool.size() < N) [[unlikely]] { return; }
	if (pool.is_dense()) [[likely]]
	{
		const T* p = pool.data();
		const size_t end = pool.size() - N + 1;
		for (size_t i = 0; i < end; ++i)
		{
			f(std::span<const T, N>(p + i, N));
		}
	}
	else
	{
		for (size_t i = 0; i + N <= pool.size(); ++i)
		{
			bool valid = true;
			for (size_t j = 0; j < N; ++j)
			{
				if (!pool.is_constructed_at(i + j)) { valid = false; break; }
			}
			if (valid) [[likely]]
			{
				f(std::span<const T, N>(pool.data() + i, N));
			}
		}
	}
}

template <typename T, size_t N, typename F>
    requires (N > 0) && std::invocable<F, std::span<T, N>>
LCF_FLATTEN inline void for_each_chunk(class_pool<T>& pool, F&& f) noexcept
{
	if (pool.is_dense()) [[likely]]
	{
		T* p = pool.data();
		const size_t full_chunks = pool.size() / N;
		for (size_t i = 0; i < full_chunks; ++i)
		{
			f(std::span<T, N>(p + i * N, N));
		}
	}
	else
	{
		const size_t full_chunks = pool.size() / N;
		for (size_t i = 0; i < full_chunks; ++i)
		{
			bool valid = true;
			for (size_t j = 0; j < N; ++j)
			{
				if (!pool.is_constructed_at(i * N + j)) { valid = false; break; }
			}
			if (valid) [[likely]]
			{
				f(std::span<T, N>(pool.data() + i * N, N));
			}
		}
	}
}

template <typename T, size_t N, typename F>
    requires (N > 0) && std::invocable<F, std::span<const T, N>>
LCF_FLATTEN inline void for_each_chunk(const class_pool<T>& pool, F&& f) noexcept
{
	if (pool.is_dense()) [[likely]]
	{
		const T* p = pool.data();
		const size_t full_chunks = pool.size() / N;
		for (size_t i = 0; i < full_chunks; ++i)
		{
			f(std::span<const T, N>(p + i * N, N));
		}
	}
	else
	{
		const size_t full_chunks = pool.size() / N;
		for (size_t i = 0; i < full_chunks; ++i)
		{
			bool valid = true;
			for (size_t j = 0; j < N; ++j)
			{
				if (!pool.is_constructed_at(i * N + j)) { valid = false; break; }
			}
			if (valid) [[likely]]
			{
				f(std::span<const T, N>(pool.data() + i * N, N));
			}
		}
	}
}

template <typename T, size_t N>
    requires (N > 0)
[[nodiscard]] inline std::span<T, N> window_span(class_pool<T>& pool, size_t offset) noexcept
{
	const size_t sz = pool.size();
	const size_t use = (offset < sz && offset + N <= sz) ? N : (offset < sz ? sz - offset : 0);
	return std::span<T, N>(pool.data() + offset, use);
}

template <typename T, size_t N>
    requires (N > 0)
[[nodiscard]] inline std::span<const T, N> window_span(const class_pool<T>& pool, size_t offset) noexcept
{
	const size_t sz = pool.size();
	const size_t use = (offset < sz && offset + N <= sz) ? N : (offset < sz ? sz - offset : 0);
	return std::span<const T, N>(pool.data() + offset, use);
}

template <typename T, size_t N>
    requires (N > 0)
[[nodiscard]] inline std::span<T, N> chunk_span(class_pool<T>& pool, size_t chunk_idx) noexcept
{
	const size_t offset = chunk_idx * N;
	const size_t sz = pool.size();
	const size_t use = (offset < sz && offset + N <= sz) ? N : (offset < sz ? sz - offset : 0);
	return std::span<T, N>(pool.data() + offset, use);
}

template <typename T, size_t N>
    requires (N > 0)
[[nodiscard]] inline std::span<const T, N> chunk_span(const class_pool<T>& pool, size_t chunk_idx) noexcept
{
	const size_t offset = chunk_idx * N;
	const size_t sz = pool.size();
	const size_t use = (offset < sz && offset + N <= sz) ? N : (offset < sz ? sz - offset : 0);
	return std::span<const T, N>(pool.data() + offset, use);
}

// 枚举视图
template <typename T, typename F>
    requires std::invocable<F, size_t, T&>
LCF_FLATTEN inline void for_each_enumerated(class_pool<T>& pool, F&& f) noexcept
{
	size_t idx = 0;
	for (auto it = pool.begin(); it != pool.end(); ++it, ++idx)
	{
		f(idx, *it);
	}
}

template <typename T, typename F>
    requires std::invocable<F, size_t, const T&>
LCF_FLATTEN inline void for_each_enumerated(const class_pool<T>& pool, F&& f) noexcept
{
	size_t idx = 0;
	for (auto it = pool.cbegin(); it != pool.cend(); ++it, ++idx)
	{
		f(idx, *it);
	}
}

// 双容器同步
template <typename T, typename U, typename F>
    requires std::invocable<F, T&, U&>
LCF_FLATTEN inline void for_each_zip(
    class_pool<T>& a, class_pool<U>& b, F&& f) noexcept
{
	auto ia = a.begin();
	auto ib = b.begin();
	auto ea = a.end();
	auto eb = b.end();
	while (ia != ea && ib != eb)
	{
		f(*ia, *ib);
		++ia;
		++ib;
	}
}

template <typename T, typename U, typename F>
    requires std::invocable<F, const T&, const U&>
LCF_FLATTEN inline void for_each_zip(
    const class_pool<T>& a, const class_pool<U>& b, F&& f) noexcept
{
	auto ia = a.cbegin();
	auto ib = b.cbegin();
	auto ea = a.cend();
	auto eb = b.cend();
	while (ia != ea && ib != eb)
	{
		f(*ia, *ib);
		++ia;
		++ib;
	}
}

template <typename T, typename U, typename F>
    requires std::invocable<F, T&, U&>
LCF_FLATTEN inline void for_each_zip(
    class_pool<T>& a, U* b, size_t count, F&& f) noexcept
{
	const size_t n = count < a.size() ? count : a.size();
	if (a.is_dense()) [[likely]]
	{
		T* LCF_RESTRICT pa = a.data();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		for (size_t i = 0; i < n; ++i)
		{
			f(pa[i], b[i]);
		}
	}
	else
	{
		size_t bi = 0;
		for (auto it = a.begin(); it != a.end() && bi < n; ++it, ++bi)
		{
			f(*it, b[bi]);
		}
	}
}

template <typename T, typename U, typename F>
    requires std::invocable<F, const T&, const U&>
LCF_FLATTEN inline void for_each_zip(
    const class_pool<T>& a, const U* b, size_t count, F&& f) noexcept
{
	const size_t n = count < a.size() ? count : a.size();
	if (a.is_dense()) [[likely]]
	{
		const T* LCF_RESTRICT pa = a.data();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		for (size_t i = 0; i < n; ++i)
		{
			f(pa[i], b[i]);
		}
	}
	else
	{
		size_t bi = 0;
		for (auto it = a.cbegin(); it != a.cend() && bi < n; ++it, ++bi)
		{
			f(*it, b[bi]);
		}
	}
}

template <typename T, typename U, typename R, typename F>
    requires std::invocable<F, const T&, const U&>
inline void zip_with_to(
    const class_pool<T>& a, const U* b, R* dst, size_t count, F&& f) noexcept
{
	const size_t n = count < a.size() ? count : a.size();
	if (a.is_dense()) [[likely]]
	{
		const T* LCF_RESTRICT pa = a.data();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		for (size_t i = 0; i < n; ++i)
		{
			new (&dst[i]) R(f(pa[i], b[i]));
		}
	}
	else
	{
		size_t di = 0;
		for (auto it = a.cbegin(); it != a.cend() && di < n; ++it, ++di)
		{
			new (&dst[di]) R(f(*it, b[di]));
		}
	}
}

template <typename T>
[[nodiscard]] inline bool equal(const class_pool<T>& a, const class_pool<T>& b) noexcept
{
	if (a.count() != b.count()) [[unlikely]] { return false; }
	auto ia = a.cbegin();
	auto ib = b.cbegin();
	const auto ea = a.cend();
	while (ia != ea)
	{
		if (!(*ia == *ib)) [[unlikely]] { return false; }
		++ia;
		++ib;
	}
	return true;
}

// equal(ptr, count): 与 dense::equal(const T*, size_t) 命名一致
// 按活跃元素顺序比较 (稀疏模式跳过空洞)
template <typename T>
[[nodiscard]] inline bool equal(const class_pool<T>& a, const T* other, size_t count) noexcept
{
	if (a.count() != count) [[unlikely]] { return false; }
	if constexpr (std::is_trivially_copyable_v<T>)
	{
		if (a.is_dense()) [[likely]]
		{
			return std::memcmp(a.data(), other, a.count() * sizeof(T)) == 0;
		}
	}
	auto ia = a.cbegin();
	size_t bi = 0;
	const auto ea = a.cend();
	while (ia != ea)
	{
		if (!(*ia == other[bi])) [[unlikely]] { return false; }
		++ia;
		++bi;
	}
	return true;
}

template <typename T>
[[nodiscard]] inline bool equal(const class_pool<T>& a, std::span<const T> b) noexcept
{
	return equal(a, b.data(), b.size());
}

// SIMD / 对齐视图
template <typename T>
[[nodiscard]] inline T* aligned_data(class_pool<T>& pool) noexcept
{
	return pool.data();
}

template <typename T>
[[nodiscard]] inline const T* aligned_data(const class_pool<T>& pool) noexcept
{
	return pool.data();
}

template <typename T>
[[nodiscard]] inline std::span<T> aligned_span(class_pool<T>& pool) noexcept
{
	return pool.span();
}

template <typename T>
[[nodiscard]] inline std::span<const T> aligned_span(const class_pool<T>& pool) noexcept
{
	return pool.span();
}

// simd_for_each: 仅 trivially copyable 且 sizeof <= 32 时启用 SIMD 路径
template <typename T, typename F>
    requires std::is_trivially_copyable_v<T> && std::invocable<F, T&>
LCF_FLATTEN inline void simd_for_each(class_pool<T>& pool, F&& f) noexcept
{
	if (pool.is_dense() && sizeof(T) <= 32) [[likely]]
	{
		T* LCF_RESTRICT p = pool.data();
		const size_t n = pool.size();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		for (size_t i = 0; i < n; ++i)
		{
			f(p[i]);
		}
	}
	else
	{
		// 稀疏模式退化为 for_each (复用 begin/end 位图扫描)
		for (auto it = pool.begin(); it != pool.end(); ++it)
		{
			f(*it);
		}
	}
}

template <typename T, typename F>
    requires std::is_trivially_copyable_v<T> && std::invocable<F, const T&>
LCF_FLATTEN inline void simd_for_each(const class_pool<T>& pool, F&& f) noexcept
{
	if (pool.is_dense() && sizeof(T) <= 32) [[likely]]
	{
		const T* LCF_RESTRICT p = pool.data();
		const size_t n = pool.size();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		for (size_t i = 0; i < n; ++i)
		{
			f(p[i]);
		}
	}
	else
	{
		for (auto it = pool.cbegin(); it != pool.cend(); ++it)
		{
			f(*it);
		}
	}
}

// 32 字节对齐后的尾部偏移 (AVX2 YMM 无法处理的尾部)
template <typename T>
[[nodiscard]] inline size_t unaligned_tail_offset(const class_pool<T>& pool) noexcept
{
	const size_t n = pool.size();
	constexpr size_t align_bytes = 32;
	constexpr size_t elems_per_ymm = align_bytes / sizeof(T);
	if constexpr (elems_per_ymm == 0)
	{
		return n;
	}
	return n - (n % elems_per_ymm);
}

// 拷贝 / 移动 / 压缩视图
template <typename T>
inline void copy_to(const class_pool<T>& pool, T* dst, size_t count) noexcept
{
	const size_t n = count < pool.size() ? count : pool.size();
	if (pool.is_dense()) [[likely]]
	{
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			if (n > 0) [[likely]]
			{
				std::memcpy(dst, pool.data(), n * sizeof(T));
			}
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[i]) T(pool.data()[i]);
			}
		}
	}
	else
	{
		size_t di = 0;
		for (auto it = pool.cbegin(); it != pool.cend() && di < n; ++it, ++di)
		{
			new (&dst[di]) T(*it);
		}
	}
}

template <typename T>
inline void copy_to(const class_pool<T>& pool, std::span<T> dst) noexcept
{
	copy_to(pool, dst.data(), dst.size());
}

template <typename T>
inline void move_to(class_pool<T>& pool, T* dst, size_t count) noexcept
{
	const size_t n = count < pool.size() ? count : pool.size();
	if (pool.is_dense()) [[likely]]
	{
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			if (n > 0) [[likely]]
			{
				std::memcpy(dst, pool.data(), n * sizeof(T));
			}
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[i]) T(std::move(pool.data()[i]));
			}
		}
	}
	else
	{
		size_t di = 0;
		for (auto it = pool.begin(); it != pool.end() && di < n; ++it, ++di)
		{
			new (&dst[di]) T(std::move(*it));
		}
	}
}

template <typename T>
inline void move_to(class_pool<T>& pool, std::span<T> dst) noexcept
{
	move_to(pool, dst.data(), dst.size());
}

template <typename T>
inline void reverse_copy_to(const class_pool<T>& pool, T* dst, size_t count) noexcept
{
	const size_t n = count < pool.size() ? count : pool.size();
	if (pool.is_dense()) [[likely]]
	{
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			const T* src = pool.data();
			for (size_t i = 0; i < n; ++i)
			{
				std::memcpy(&dst[i], &src[n - 1 - i], sizeof(T));
			}
		}
		else
		{
			const T* src = pool.data();
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[i]) T(src[n - 1 - i]);
			}
		}
	}
	else
	{
		// 稀疏反向拷贝: 用反向迭代器
		size_t di = 0;
		for (auto it = pool.crbegin(); it != pool.crend() && di < n; ++it, ++di)
		{
			new (&dst[di]) T(*it);
		}
	}
}

template <typename T>
inline void reverse_copy_to(const class_pool<T>& pool, std::span<T> dst) noexcept
{
	reverse_copy_to(pool, dst.data(), dst.size());
}

// class_pool 独有: 压缩稀疏池为密集数组 (消除空洞)
// 将所有活跃元素连续写入 dst, 返回写入元素数
// 用于 ECS 视图重建 / 序列化 / 跨池迁移
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

// class_pool 独有: 活跃元素数 (语义等价 pool.count())
template <typename T>
[[nodiscard]] inline size_t live_count(const class_pool<T>& pool) noexcept
{
	return pool.count();
}

// class_pool 独有: 空洞数 = 高水位 - 活跃数
template <typename T>
[[nodiscard]] inline size_t holes_count(const class_pool<T>& pool) noexcept
{
	return pool.size() - pool.count();
}
