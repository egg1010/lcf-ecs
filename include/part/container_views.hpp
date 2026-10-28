#pragma once
#include <span>
#include <concepts>
#include <utility>
#include <functional>
#include <iterator>
#include <bit>
#include <cstdint>
#include <cstring>
#include "force_inline.hpp"
#include <type_traits>

// 容器特征: 稀疏/稠密判定与存活计数, 由特化提供, 不要求容器公开同名接口
template <typename C>
struct container_traits
{
    static constexpr bool always_dense = false;
    static constexpr bool is_dense(const C& c) noexcept { return c.is_dense(); }
    static constexpr size_t count(const C& c) noexcept { return c.count(); }
    static constexpr bool is_constructed_at(const C& c, size_t i) noexcept { return c.is_constructed_at(i); }
};

template <typename C>
[[nodiscard]] constexpr bool container_is_dense(const C& c) noexcept
{
    return container_traits<std::remove_cvref_t<C>>::is_dense(c);
}

template <typename C>
[[nodiscard]] constexpr size_t container_count(const C& c) noexcept
{
    return container_traits<std::remove_cvref_t<C>>::count(c);
}

template <typename C>
[[nodiscard]] constexpr bool container_is_constructed_at(const C& c, size_t i) noexcept
{
    return container_traits<std::remove_cvref_t<C>>::is_constructed_at(c, i);
}

// 容器视图概念: 约束 data/size/is_dense/is_constructed_at/begin/end
// 及算法体内实际使用的 operator[]/count/span/rbegin/rend
template <typename C>
concept viewable_container = requires(C& c, const C& cc, size_t i) {
    typename C::value_type;
    { c.data() };
    { c.size() } -> std::convertible_to<size_t>;
    { container_is_dense(c) } -> std::convertible_to<bool>;
    { container_is_constructed_at(c, i) } -> std::convertible_to<bool>;
    { c.begin() };
    { c.end() };
    { c[i] };
    { container_count(c) } -> std::convertible_to<size_t>;
    { c.span() };
    { c.rbegin() };
    { c.rend() };
    { cc.crbegin() };
    { cc.crend() };
};

// 通用视图 (全局命名空间, dense 与 class_pool 共用)
template <viewable_container C, typename F>
    requires std::invocable<F, typename C::value_type&>
LCF_FLATTEN inline void for_each(C& c, F&& f) noexcept
{
	for (auto it = c.begin(); it != c.end(); ++it)
	{
		f(*it);
	}
}

template <viewable_container C, typename F>
    requires std::invocable<F, const typename C::value_type&>
LCF_FLATTEN inline void for_each(const C& c, F&& f) noexcept
{
	for (auto it = c.cbegin(); it != c.cend(); ++it)
	{
		f(*it);
	}
}

template <viewable_container C, typename Dst, typename Pred>
    requires std::predicate<Pred, const typename C::value_type&>
inline void filter_indices_to(const C& c, Dst& dst, Pred pred) noexcept
{
	for (auto it = c.cbegin(); it != c.cend(); ++it)
	{
		if (pred(*it)) [[likely]]
		{
			const size_t idx = static_cast<size_t>(&(*it) - c.data());
			dst.emplace_back(idx);
		}
	}
}

// 子范围切片 (与 dense::subspan / std::span::subspan 命名一致)
template <viewable_container C>
[[nodiscard]] inline std::span<typename C::value_type> subspan(C& c, size_t offset, size_t count) noexcept
{
	using V = typename C::value_type;
	if (offset >= c.size()) [[unlikely]]
	{
		return std::span<V>(static_cast<V*>(nullptr), 0);
	}
	const size_t avail = c.size() - offset;
	return std::span<V>(c.data() + offset, count > avail ? avail : count);
}

template <viewable_container C>
[[nodiscard]] inline std::span<const typename C::value_type> subspan(const C& c, size_t offset, size_t count) noexcept
{
	using V = typename C::value_type;
	if (offset >= c.size()) [[unlikely]]
	{
		return std::span<const V>(static_cast<const V*>(nullptr), 0);
	}
	const size_t avail = c.size() - offset;
	return std::span<const V>(c.data() + offset, count > avail ? avail : count);
}

// 单参数 subspan: 取 [offset, size()) 范围
template <viewable_container C>
[[nodiscard]] inline std::span<typename C::value_type> subspan(C& c, size_t offset) noexcept
{
	using V = typename C::value_type;
	if (offset >= c.size()) [[unlikely]]
	{
		return std::span<V>(static_cast<V*>(nullptr), 0);
	}
	return std::span<V>(c.data() + offset, c.size() - offset);
}

template <viewable_container C>
[[nodiscard]] inline std::span<const typename C::value_type> subspan(const C& c, size_t offset) noexcept
{
	using V = typename C::value_type;
	if (offset >= c.size()) [[unlikely]]
	{
		return std::span<const V>(static_cast<const V*>(nullptr), 0);
	}
	return std::span<const V>(c.data() + offset, c.size() - offset);
}

template <viewable_container C>
[[nodiscard]] inline std::span<typename C::value_type> first(C& c, size_t n) noexcept
{
	return subspan(c, 0, n);
}

template <viewable_container C>
[[nodiscard]] inline std::span<const typename C::value_type> first(const C& c, size_t n) noexcept
{
	return subspan(c, 0, n);
}

template <viewable_container C>
[[nodiscard]] inline std::span<typename C::value_type> last(C& c, size_t n) noexcept
{
	using V = typename C::value_type;
	if (n >= c.size()) [[unlikely]]
	{
		return std::span<V>(c.data(), c.size());
	}
	return std::span<V>(c.data() + c.size() - n, n);
}

template <viewable_container C>
[[nodiscard]] inline std::span<const typename C::value_type> last(const C& c, size_t n) noexcept
{
	using V = typename C::value_type;
	if (n >= c.size()) [[unlikely]]
	{
		return std::span<const V>(c.data(), c.size());
	}
	return std::span<const V>(c.data() + c.size() - n, n);
}

// 动态长度 span: 容器不足 N 时截断, 避免 span<T,N>(ptr, count<N) 违反前置条件
template <viewable_container C, typename F>
LCF_FLATTEN inline void reverse_for_each(C& c, F&& f) noexcept
{
	for (auto it = c.rbegin(); it != c.rend(); ++it)
	{
		f(*it);
	}
}

template <viewable_container C, typename F>
LCF_FLATTEN inline void reverse_for_each(const C& c, F&& f) noexcept
{
	for (auto it = c.crbegin(); it != c.crend(); ++it)
	{
		f(*it);
	}
}

// 步进槽位遍历: 稀疏模式越过空洞时自动 is_constructed_at 检查
template <viewable_container C, typename F>
LCF_FLATTEN inline void strided_for_each(C& c, size_t start, size_t step, F&& f) noexcept
{
	using V = typename C::value_type;
	if (container_is_dense(c)) [[likely]]
	{
		V* LCF_RESTRICT p = c.data() + start;
		const size_t avail = c.size() > start ? c.size() - start : 0;
		const size_t cnt = (avail + step - 1) / step;
		for (size_t i = 0; i < cnt; ++i)
		{
			if constexpr (sizeof(V) >= 16)
			{
				if (i + 8 < cnt) [[likely]]
				{
					LCF_PREFETCH_R(p + 8 * step);
				}
			}
			f(*p);
			p += step;
		}
	}
	else
	{
		const size_t avail = c.size() > start ? c.size() - start : 0;
		const size_t cnt = (avail + step - 1) / step;
		for (size_t i = 0; i < cnt; ++i)
		{
			const size_t slot = start + i * step;
			if (container_is_constructed_at(c, slot)) [[likely]]
			{
				f(c[slot]);
			}
		}
	}
}

template <viewable_container C, typename F>
LCF_FLATTEN inline void strided_for_each(const C& c, size_t start, size_t step, F&& f) noexcept
{
	using V = typename C::value_type;
	if (container_is_dense(c)) [[likely]]
	{
		const V* LCF_RESTRICT p = c.data() + start;
		const size_t avail = c.size() > start ? c.size() - start : 0;
		const size_t cnt = (avail + step - 1) / step;
		for (size_t i = 0; i < cnt; ++i)
		{
			if constexpr (sizeof(V) >= 16)
			{
				if (i + 8 < cnt) [[likely]]
				{
					LCF_PREFETCH_R(p + 8 * step);
				}
			}
			f(*p);
			p += step;
		}
	}
	else
	{
		const size_t avail = c.size() > start ? c.size() - start : 0;
		const size_t cnt = (avail + step - 1) / step;
		for (size_t i = 0; i < cnt; ++i)
		{
			const size_t slot = start + i * step;
			if (container_is_constructed_at(c, slot)) [[likely]]
			{
				f(c[slot]);
			}
		}
	}
}

template <size_t Step, viewable_container C, typename F>
    requires (Step > 0)
LCF_FLATTEN inline void strided_for_each(C& c, F&& f) noexcept
{
	if constexpr (Step == 1)
	{
		// 步长 1 退化为 for_each (复用 begin/end)
		for (auto it = c.begin(); it != c.end(); ++it)
		{
			f(*it);
		}
	}
	else
	{
		strided_for_each(c, 0, Step, std::forward<F>(f));
	}
}

template <size_t Step, viewable_container C, typename F>
    requires (Step > 0)
LCF_FLATTEN inline void strided_for_each(const C& c, F&& f) noexcept
{
	if constexpr (Step == 1)
	{
		for (auto it = c.cbegin(); it != c.cend(); ++it)
		{
			f(*it);
		}
	}
	else
	{
		strided_for_each(c, 0, Step, std::forward<F>(f));
	}
}

// 变换融合遍历: 对每个 v 调用 con(tr(v)), 避免中间临时容器
template <viewable_container C, typename FTransform, typename FConsume>
    requires std::invocable<FTransform, typename C::value_type&> && std::invocable<FConsume, typename C::value_type>
LCF_FLATTEN inline void transform_for_each(C& c, FTransform&& tr, FConsume&& con) noexcept
{
	for (auto it = c.begin(); it != c.end(); ++it)
	{
		con(tr(*it));
	}
}

template <viewable_container C, typename FTransform, typename FConsume>
    requires std::invocable<FTransform, const typename C::value_type&> && std::invocable<FConsume, typename C::value_type>
LCF_FLATTEN inline void transform_for_each(const C& c, FTransform&& tr, FConsume&& con) noexcept
{
	for (auto it = c.cbegin(); it != c.cend(); ++it)
	{
		con(tr(*it));
	}
}

// dst 必须为未构造内存 (placement new 语义, 非平凡类型覆盖活对象会泄漏)
template <viewable_container C, typename R, typename F>
    requires std::invocable<F, const typename C::value_type&>
inline void transform_to(const C& c, R* dst, size_t count, F&& tr) noexcept
{
	using V = typename C::value_type;
	const size_t n = count < c.size() ? count : c.size();
	if (container_is_dense(c)) [[likely]]
	{
		const V* LCF_RESTRICT src = c.data();
		for (size_t i = 0; i < n; ++i)
		{
			new (&dst[i]) R(tr(src[i]));
		}
	}
	else
	{
		size_t di = 0;
		for (auto it = c.cbegin(); it != c.cend() && di < n; ++it, ++di)
		{
			new (&dst[di]) R(tr(*it));
		}
	}
}

template <viewable_container C>
[[nodiscard]] inline typename C::value_type* find(C& c, const typename C::value_type& value) noexcept
{
	for (auto it = c.begin(); it != c.end(); ++it)
	{
		if (*it == value) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <viewable_container C>
[[nodiscard]] inline const typename C::value_type* find(const C& c, const typename C::value_type& value) noexcept
{
	for (auto it = c.cbegin(); it != c.cend(); ++it)
	{
		if (*it == value) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <viewable_container C, typename Pred>
    requires std::predicate<Pred, const typename C::value_type&>
[[nodiscard]] inline typename C::value_type* find_if(C& c, Pred pred) noexcept
{
	for (auto it = c.begin(); it != c.end(); ++it)
	{
		if (pred(*it)) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <viewable_container C, typename Pred>
    requires std::predicate<Pred, const typename C::value_type&>
[[nodiscard]] inline const typename C::value_type* find_if(const C& c, Pred pred) noexcept
{
	for (auto it = c.cbegin(); it != c.cend(); ++it)
	{
		if (pred(*it)) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <viewable_container C, typename Pred>
    requires std::predicate<Pred, const typename C::value_type&>
[[nodiscard]] inline typename C::value_type* find_if_not(C& c, Pred pred) noexcept
{
	for (auto it = c.begin(); it != c.end(); ++it)
	{
		if (!pred(*it)) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <viewable_container C, typename Pred>
    requires std::predicate<Pred, const typename C::value_type&>
[[nodiscard]] inline const typename C::value_type* find_if_not(const C& c, Pred pred) noexcept
{
	for (auto it = c.cbegin(); it != c.cend(); ++it)
	{
		if (!pred(*it)) [[unlikely]]
		{
			return &(*it);
		}
	}
	return nullptr;
}

template <viewable_container C, typename Pred>
    requires std::predicate<Pred, const typename C::value_type&>
[[nodiscard]] inline size_t count_if(const C& c, Pred pred) noexcept
{
	size_t cnt = 0;
	for (auto it = c.cbegin(); it != c.cend(); ++it)
	{
		if (pred(*it)) [[likely]]
		{
			++cnt;
		}
	}
	return cnt;
}

template <viewable_container C, typename Pred, typename F>
    requires std::predicate<Pred, const typename C::value_type&> && std::invocable<F, typename C::value_type&>
LCF_FLATTEN inline void filter_for_each(C& c, Pred pred, F&& f) noexcept
{
	for (auto it = c.begin(); it != c.end(); ++it)
	{
		if (pred(*it)) [[likely]]
		{
			f(*it);
		}
	}
}

template <viewable_container C, typename Pred, typename F>
    requires std::predicate<Pred, const typename C::value_type&> && std::invocable<F, const typename C::value_type&>
LCF_FLATTEN inline void filter_for_each(const C& c, Pred pred, F&& f) noexcept
{
	for (auto it = c.cbegin(); it != c.cend(); ++it)
	{
		if (pred(*it)) [[likely]]
		{
			f(*it);
		}
	}
}

template <viewable_container C, typename F, typename U = typename C::value_type>
    requires std::invocable<F, U, const typename C::value_type&>
[[nodiscard]] inline U reduce(const C& c, F&& f, U init) noexcept
{
	for (auto it = c.cbegin(); it != c.cend(); ++it)
	{
		init = f(std::move(init), *it);
	}
	return init;
}

template <viewable_container C>
[[nodiscard]] inline typename C::value_type* min_element(C& c) noexcept
{
	auto it = c.begin();
	auto end = c.end();
	if (it == end) [[unlikely]]
	{
		return nullptr;
	}
	typename C::value_type* best = &(*it);
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

template <viewable_container C>
[[nodiscard]] inline const typename C::value_type* min_element(const C& c) noexcept
{
	auto it = c.cbegin();
	auto end = c.cend();
	if (it == end) [[unlikely]]
	{
		return nullptr;
	}
	const typename C::value_type* best = &(*it);
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

template <viewable_container C>
[[nodiscard]] inline typename C::value_type* max_element(C& c) noexcept
{
	auto it = c.begin();
	auto end = c.end();
	if (it == end) [[unlikely]]
	{
		return nullptr;
	}
	typename C::value_type* best = &(*it);
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

template <viewable_container C>
[[nodiscard]] inline const typename C::value_type* max_element(const C& c) noexcept
{
	auto it = c.cbegin();
	auto end = c.cend();
	if (it == end) [[unlikely]]
	{
		return nullptr;
	}
	const typename C::value_type* best = &(*it);
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

template <viewable_container C>
[[nodiscard]] inline std::pair<typename C::value_type*, typename C::value_type*> minmax_element(C& c) noexcept
{
	auto it = c.begin();
	auto end = c.end();
	if (it == end) [[unlikely]]
	{
		return {nullptr, nullptr};
	}
	typename C::value_type* mn = &(*it);
	typename C::value_type* mx = &(*it);
	++it;
	for (; it != end; ++it)
	{
		if (*it < *mn) [[unlikely]]
		{
			mn = &(*it);
		}
		else if (*mx < *it) [[unlikely]]
		{
			mx = &(*it);
		}
	}
	return {mn, mx};
}

template <viewable_container C>
[[nodiscard]] inline std::pair<const typename C::value_type*, const typename C::value_type*> minmax_element(const C& c) noexcept
{
	auto it = c.cbegin();
	auto end = c.cend();
	if (it == end) [[unlikely]]
	{
		return {nullptr, nullptr};
	}
	const typename C::value_type* mn = &(*it);
	const typename C::value_type* mx = &(*it);
	++it;
	for (; it != end; ++it)
	{
		if (*it < *mn) [[unlikely]]
		{
			mn = &(*it);
		}
		else if (*mx < *it) [[unlikely]]
		{
			mx = &(*it);
		}
	}
	return {mn, mx};
}

template <viewable_container C>
    requires std::is_arithmetic_v<typename C::value_type>
[[nodiscard]] inline typename C::value_type sum(const C& c) noexcept
{
	using V = typename C::value_type;
	if (container_is_dense(c)) [[likely]]
	{
		const V* LCF_RESTRICT p = c.data();
		const size_t n = c.size();
		V acc = V{};
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
		V acc = V{};
		for (auto it = c.cbegin(); it != c.cend(); ++it)
		{
			acc += *it;
		}
		return acc;
	}
}

template <viewable_container C>
    requires std::is_arithmetic_v<typename C::value_type>
[[nodiscard]] inline typename C::value_type dot_product(const C& c, const typename C::value_type* other, size_t count) noexcept
{
	using V = typename C::value_type;
	const size_t n = count < c.size() ? count : c.size();
	if (container_is_dense(c)) [[likely]]
	{
		const V* LCF_RESTRICT p = c.data();
		V acc = V{};
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
		V acc = V{};
		size_t oi = 0;
		for (auto it = c.cbegin(); it != c.cend() && oi < n; ++it, ++oi)
		{
			acc += (*it) * other[oi];
		}
		return acc;
	}
}

template <size_t N, viewable_container C, typename F>
    requires (N > 0) && std::invocable<F, std::span<typename C::value_type, N>>
LCF_FLATTEN inline void for_each_window(C& c, F&& f) noexcept
{
	using V = typename C::value_type;
	if (c.size() < N) [[unlikely]]
	{
		return;
	}
	if (container_is_dense(c)) [[likely]]
	{
		V* p = c.data();
		const size_t end = c.size() - N + 1;
		for (size_t i = 0; i < end; ++i)
		{
			f(std::span<V, N>(p + i, N));
		}
	}
	else
	{
		for (size_t i = 0; i + N <= c.size(); ++i)
		{
			bool valid = true;
			for (size_t j = 0; j < N; ++j)
			{
				if (!container_is_constructed_at(c, i + j))
				{
					valid = false;
					break;
				}
			}
			if (valid) [[likely]]
			{
				f(std::span<V, N>(c.data() + i, N));
			}
		}
	}
}

template <size_t N, viewable_container C, typename F>
    requires (N > 0) && std::invocable<F, std::span<const typename C::value_type, N>>
LCF_FLATTEN inline void for_each_window(const C& c, F&& f) noexcept
{
	using V = typename C::value_type;
	if (c.size() < N) [[unlikely]]
	{
		return;
	}
	if (container_is_dense(c)) [[likely]]
	{
		const V* p = c.data();
		const size_t end = c.size() - N + 1;
		for (size_t i = 0; i < end; ++i)
		{
			f(std::span<const V, N>(p + i, N));
		}
	}
	else
	{
		for (size_t i = 0; i + N <= c.size(); ++i)
		{
			bool valid = true;
			for (size_t j = 0; j < N; ++j)
			{
				if (!container_is_constructed_at(c, i + j))
				{
					valid = false;
					break;
				}
			}
			if (valid) [[likely]]
			{
				f(std::span<const V, N>(c.data() + i, N));
			}
		}
	}
}

template <size_t N, viewable_container C, typename F>
    requires (N > 0) && std::invocable<F, std::span<typename C::value_type, N>>
LCF_FLATTEN inline void for_each_chunk(C& c, F&& f) noexcept
{
	using V = typename C::value_type;
	if (container_is_dense(c)) [[likely]]
	{
		V* p = c.data();
		const size_t full_chunks = c.size() / N;
		for (size_t i = 0; i < full_chunks; ++i)
		{
			f(std::span<V, N>(p + i * N, N));
		}
	}
	else
	{
		const size_t full_chunks = c.size() / N;
		for (size_t i = 0; i < full_chunks; ++i)
		{
			bool valid = true;
			for (size_t j = 0; j < N; ++j)
			{
				if (!container_is_constructed_at(c, i * N + j))
				{
					valid = false;
					break;
				}
			}
			if (valid) [[likely]]
			{
				f(std::span<V, N>(c.data() + i * N, N));
			}
		}
	}
}

template <size_t N, viewable_container C, typename F>
    requires (N > 0) && std::invocable<F, std::span<const typename C::value_type, N>>
LCF_FLATTEN inline void for_each_chunk(const C& c, F&& f) noexcept
{
	using V = typename C::value_type;
	if (container_is_dense(c)) [[likely]]
	{
		const V* p = c.data();
		const size_t full_chunks = c.size() / N;
		for (size_t i = 0; i < full_chunks; ++i)
		{
			f(std::span<const V, N>(p + i * N, N));
		}
	}
	else
	{
		const size_t full_chunks = c.size() / N;
		for (size_t i = 0; i < full_chunks; ++i)
		{
			bool valid = true;
			for (size_t j = 0; j < N; ++j)
			{
				if (!container_is_constructed_at(c, i * N + j))
				{
					valid = false;
					break;
				}
			}
			if (valid) [[likely]]
			{
				f(std::span<const V, N>(c.data() + i * N, N));
			}
		}
	}
}

template <size_t N, viewable_container C>
    requires (N > 0)
[[nodiscard]] inline std::span<typename C::value_type> window_span(C& c, size_t offset) noexcept
{
	using V = typename C::value_type;
	const size_t sz = c.size();
	const size_t use = (offset < sz && offset + N <= sz) ? N : (offset < sz ? sz - offset : 0);
	return std::span<V>(c.data() + offset, use);
}

template <size_t N, viewable_container C>
    requires (N > 0)
[[nodiscard]] inline std::span<const typename C::value_type> window_span(const C& c, size_t offset) noexcept
{
	using V = typename C::value_type;
	const size_t sz = c.size();
	const size_t use = (offset < sz && offset + N <= sz) ? N : (offset < sz ? sz - offset : 0);
	return std::span<const V>(c.data() + offset, use);
}

template <viewable_container C, typename F>
    requires std::invocable<F, size_t, typename C::value_type&>
LCF_FLATTEN inline void for_each_enumerated(C& c, F&& f) noexcept
{
	size_t idx = 0;
	for (auto it = c.begin(); it != c.end(); ++it, ++idx)
	{
		f(idx, *it);
	}
}

template <viewable_container C, typename F>
    requires std::invocable<F, size_t, const typename C::value_type&>
LCF_FLATTEN inline void for_each_enumerated(const C& c, F&& f) noexcept
{
	size_t idx = 0;
	for (auto it = c.cbegin(); it != c.cend(); ++it, ++idx)
	{
		f(idx, *it);
	}
}

template <viewable_container C, viewable_container D, typename F>
    requires std::invocable<F, typename C::value_type&, typename D::value_type&>
LCF_FLATTEN inline void for_each_zip(C& a, D& b, F&& f) noexcept
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

template <viewable_container C, viewable_container D, typename F>
    requires std::invocable<F, const typename C::value_type&, const typename D::value_type&>
LCF_FLATTEN inline void for_each_zip(const C& a, const D& b, F&& f) noexcept
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

template <viewable_container C, typename U, typename F>
    requires std::invocable<F, typename C::value_type&, U&>
LCF_FLATTEN inline void for_each_zip(C& a, U* b, size_t count, F&& f) noexcept
{
	using V = typename C::value_type;
	const size_t n = count < a.size() ? count : a.size();
	if (container_is_dense(a)) [[likely]]
	{
		V* LCF_RESTRICT pa = a.data();
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

template <viewable_container C, typename U, typename F>
    requires std::invocable<F, const typename C::value_type&, const U&>
LCF_FLATTEN inline void for_each_zip(const C& a, const U* b, size_t count, F&& f) noexcept
{
	using V = typename C::value_type;
	const size_t n = count < a.size() ? count : a.size();
	if (container_is_dense(a)) [[likely]]
	{
		const V* LCF_RESTRICT pa = a.data();
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

// dst 必须为未构造内存
template <viewable_container C, typename U, typename R, typename F>
    requires std::invocable<F, const typename C::value_type&, const U&>
inline void zip_with_to(const C& a, const U* b, R* dst, size_t count, F&& f) noexcept
{
	using V = typename C::value_type;
	const size_t n = count < a.size() ? count : a.size();
	if (container_is_dense(a)) [[likely]]
	{
		const V* LCF_RESTRICT pa = a.data();
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

template <viewable_container C, viewable_container D>
[[nodiscard]] inline bool equal(const C& a, const D& b) noexcept
{
	if (container_count(a) != container_count(b)) [[unlikely]]
	{
		return false;
	}
	auto ia = a.cbegin();
	auto ib = b.cbegin();
	const auto ea = a.cend();
	while (ia != ea)
	{
		if (!(*ia == *ib)) [[unlikely]]
		{
			return false;
		}
		++ia;
		++ib;
	}
	return true;
}

// 与裸指针比较: 按活跃元素顺序逐个对比, 稀疏模式跳过空洞
template <viewable_container C>
[[nodiscard]] inline bool equal(const C& a, const typename C::value_type* other, size_t count) noexcept
{
	using V = typename C::value_type;
	if (container_count(a) != count) [[unlikely]]
	{
		return false;
	}
	if constexpr (std::is_trivially_copyable_v<V>)
	{
		if (container_is_dense(a)) [[likely]]
		{
			return std::memcmp(a.data(), other, container_count(a) * sizeof(V)) == 0;
		}
	}
	auto ia = a.cbegin();
	size_t bi = 0;
	const auto ea = a.cend();
	while (ia != ea)
	{
		if (!(*ia == other[bi])) [[unlikely]]
		{
			return false;
		}
		++ia;
		++bi;
	}
	return true;
}

template <viewable_container C>
[[nodiscard]] inline bool equal(const C& a, std::span<const typename C::value_type> b) noexcept
{
	return equal(a, b.data(), b.size());
}

// 向量化遍历: 要求 trivially copyable 且 sizeof <= 32, 稀疏模式退化为 for_each
template <viewable_container C, typename F>
    requires std::is_trivially_copyable_v<typename C::value_type> && std::invocable<F, typename C::value_type&>
LCF_FLATTEN inline void simd_for_each(C& c, F&& f) noexcept
{
	using V = typename C::value_type;
	if (container_is_dense(c) && sizeof(V) <= 32) [[likely]]
	{
		V* LCF_RESTRICT p = c.data();
		const size_t n = c.size();
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
		// 稀疏模式退化为 for_each
		for (auto it = c.begin(); it != c.end(); ++it)
		{
			f(*it);
		}
	}
}

template <viewable_container C, typename F>
    requires std::is_trivially_copyable_v<typename C::value_type> && std::invocable<F, const typename C::value_type&>
LCF_FLATTEN inline void simd_for_each(const C& c, F&& f) noexcept
{
	using V = typename C::value_type;
	if (container_is_dense(c) && sizeof(V) <= 32) [[likely]]
	{
		const V* LCF_RESTRICT p = c.data();
		const size_t n = c.size();
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
		for (auto it = c.cbegin(); it != c.cend(); ++it)
		{
			f(*it);
		}
	}
}

// 32 字节对齐处理后的尾部起始偏移
template <viewable_container C>
[[nodiscard]] inline size_t unaligned_tail_offset(const C& c) noexcept
{
	using V = typename C::value_type;
	const size_t n = c.size();
	constexpr size_t align_bytes = 32;
	constexpr size_t elems_per_ymm = align_bytes / sizeof(V);
	if constexpr (elems_per_ymm == 0)
	{
		return n;
	}
	return n - (n % elems_per_ymm);
}

// dst 必须为未构造内存
template <viewable_container C>
inline void copy_to(const C& c, typename C::value_type* dst, size_t count) noexcept
{
	using V = typename C::value_type;
	const size_t n = count < c.size() ? count : c.size();
	if (container_is_dense(c)) [[likely]]
	{
		if constexpr (std::is_trivially_copyable_v<V>)
		{
			if (n > 0) [[likely]]
			{
				std::memcpy(dst, c.data(), n * sizeof(V));
			}
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[i]) V(c.data()[i]);
			}
		}
	}
	else
	{
		size_t di = 0;
		for (auto it = c.cbegin(); it != c.cend() && di < n; ++it, ++di)
		{
			new (&dst[di]) V(*it);
		}
	}
}

template <viewable_container C>
inline void copy_to(const C& c, std::span<typename C::value_type> dst) noexcept
{
	copy_to(c, dst.data(), dst.size());
}

// dst 必须为未构造内存
template <viewable_container C>
inline void move_to(C& c, typename C::value_type* dst, size_t count) noexcept
{
	using V = typename C::value_type;
	const size_t n = count < c.size() ? count : c.size();
	if (container_is_dense(c)) [[likely]]
	{
		if constexpr (std::is_trivially_copyable_v<V>)
		{
			if (n > 0) [[likely]]
			{
				std::memcpy(dst, c.data(), n * sizeof(V));
			}
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[i]) V(std::move(c.data()[i]));
			}
		}
	}
	else
	{
		size_t di = 0;
		for (auto it = c.begin(); it != c.end() && di < n; ++it, ++di)
		{
			new (&dst[di]) V(std::move(*it));
		}
	}
}

template <viewable_container C>
inline void move_to(C& c, std::span<typename C::value_type> dst) noexcept
{
	move_to(c, dst.data(), dst.size());
}

template <viewable_container C>
inline void reverse_copy_to(const C& c, typename C::value_type* dst, size_t count) noexcept
{
	using V = typename C::value_type;
	const size_t n = count < c.size() ? count : c.size();
	if (container_is_dense(c)) [[likely]]
	{
		if constexpr (std::is_trivially_copyable_v<V>)
		{
			const V* src = c.data();
			for (size_t i = 0; i < n; ++i)
			{
				std::memcpy(&dst[i], &src[n - 1 - i], sizeof(V));
			}
		}
		else
		{
			const V* src = c.data();
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[i]) V(src[n - 1 - i]);
			}
		}
	}
	else
	{
		size_t di = 0;
		for (auto it = c.crbegin(); it != c.crend() && di < n; ++it, ++di)
		{
			new (&dst[di]) V(*it);
		}
	}
}

template <viewable_container C>
inline void reverse_copy_to(const C& c, std::span<typename C::value_type> dst) noexcept
{
	reverse_copy_to(c, dst.data(), dst.size());
}

// 步进视图: 持有容器指针与 {start, step, count}, 两容器共用
template <typename C>
struct container_strided_span
{
	using bare_type = std::remove_const_t<C>;
	using value_type = typename bare_type::value_type;
	using element_pointer = decltype(std::declval<C&>().data());
	using element_reference = std::remove_pointer_t<element_pointer>&;

	class iterator
	{
		element_pointer ptr_{nullptr};
		size_t step_{0};
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = typename bare_type::value_type;
		using difference_type = std::ptrdiff_t;
		using pointer = element_pointer;
		using reference = element_reference;

		constexpr iterator() noexcept = default;
		constexpr iterator(element_pointer p, size_t step) noexcept : ptr_(p), step_(step) {}

		[[nodiscard]] constexpr reference operator*() const noexcept { return *ptr_; }
		[[nodiscard]] constexpr pointer operator->() const noexcept { return ptr_; }

		constexpr iterator& operator++() noexcept
		{
			ptr_ += step_;
			return *this;
		}

		constexpr iterator operator++(int) noexcept
		{
			iterator tmp = *this;
			ptr_ += step_;
			return tmp;
		}

		[[nodiscard]] friend constexpr bool operator==(const iterator& a, const iterator& b) noexcept
		{
			return a.ptr_ == b.ptr_;
		}

		[[nodiscard]] friend constexpr bool operator!=(const iterator& a, const iterator& b) noexcept
		{
			return a.ptr_ != b.ptr_;
		}
	};

	C* container_{nullptr};
	size_t start_{0};
	size_t step_{0};
	size_t count_{0};

	constexpr container_strided_span(C* c, size_t start, size_t step, size_t count) noexcept
		: container_(c), start_(start), step_(step), count_(count) {}

	[[nodiscard]] constexpr size_t size() const noexcept { return count_; }
	[[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }
	[[nodiscard]] constexpr size_t step() const noexcept { return step_; }

	[[nodiscard]] constexpr element_pointer data() const noexcept
	{
		return container_->data() + start_;
	}

	[[nodiscard]] constexpr element_reference operator[](size_t i) const noexcept
	{
		return (*container_)[start_ + i * step_];
	}

	[[nodiscard]] constexpr iterator begin() const noexcept { return iterator(data(), step_); }
	[[nodiscard]] constexpr iterator end() const noexcept { return iterator(data() + count_ * step_, step_); }

	template <typename F>
	LCF_FLATTEN void for_each(F&& f) const noexcept
	{
		if constexpr (container_traits<bare_type>::always_dense)
		{
			auto* p = data();
			const size_t step = step_;
			const size_t n = count_;
			// 预取距离 8 (与项目 LCF_PREFETCH_R pd=8 规则一致)
			for (size_t i = 0; i < n; ++i)
			{
				if constexpr (sizeof(value_type) >= 16)
				{
					if (i + 8 < n) [[likely]] { LCF_PREFETCH_R(p + 8 * step); }
				}
				f(*p);
				p += step;
			}
		}
		else
		{
			::strided_for_each(*container_, start_, step_, std::forward<F>(f));
		}
	}
};

template <viewable_container C>
[[nodiscard]] inline container_strided_span<C> strided_span_view(C& c, size_t start, size_t step, size_t count) noexcept
{
	const size_t size = c.size();
	const size_t s = start > size ? size : start;
	const size_t max_cnt = (step > 0) ? (size - s + step - 1) / step : 0;
	const size_t cnt = count > max_cnt ? max_cnt : count;
	return container_strided_span<C>(&c, s, step ? step : 1, cnt);
}

template <viewable_container C>
[[nodiscard]] inline container_strided_span<const C> strided_span_view(const C& c, size_t start, size_t step, size_t count) noexcept
{
	const size_t size = c.size();
	const size_t s = start > size ? size : start;
	const size_t max_cnt = (step > 0) ? (size - s + step - 1) / step : 0;
	const size_t cnt = count > max_cnt ? max_cnt : count;
	return container_strided_span<const C>(&c, s, step ? step : 1, cnt);
}
