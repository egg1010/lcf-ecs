#pragma once
#include <new>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <type_traits>
#include <memory>
#include <iterator>
#include <bit>
#include <span>
#include <utility>
#include <concepts>
#include <functional>
#if defined(__AVX2__) || defined(__BMI__) || (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
#include <immintrin.h>
#endif

#include "force_inline.hpp"
// 跨平台宏 (DENSE_PREFETCH_R / DENSE_ALWAYS_INLINE / DENSE_FLATTEN / DENSE_RESTRICT):
// 集中定义于 force_inline.hpp, 此处通过别名宏直接可用
#include "container_views.hpp"

// 步进视图: 零分配 POD 结构, 持有 {指针, 步长, 数量}
template <typename T>
struct strided_span
{
    T* data_{nullptr};
    size_t step_{0};
    size_t count_{0};

    constexpr strided_span(T* data, size_t step, size_t count) noexcept
        : data_(data), step_(step), count_(count) {}

    [[nodiscard]] constexpr T& operator[](size_t i) noexcept { return data_[i * step_]; }
    [[nodiscard]] constexpr const T& operator[](size_t i) const noexcept { return data_[i * step_]; }
    [[nodiscard]] constexpr size_t size() const noexcept { return count_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr T* data() noexcept { return data_; }
    [[nodiscard]] constexpr const T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_t step() const noexcept { return step_; }

    class iterator
    {
        T* ptr_;
        size_t step_;
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        constexpr iterator(T* p, size_t step) noexcept : ptr_(p), step_(step) {}
        [[nodiscard]] constexpr reference operator*() const noexcept { return *ptr_; }
        constexpr iterator& operator++() noexcept { ptr_ += step_; return *this; }
        constexpr iterator operator++(int) noexcept { auto t = *this; ptr_ += step_; return t; }
        [[nodiscard]] constexpr bool operator!=(const iterator& o) const noexcept { return ptr_ != o.ptr_; }
        [[nodiscard]] constexpr bool operator==(const iterator& o) const noexcept { return ptr_ == o.ptr_; }
    };

    [[nodiscard]] constexpr iterator begin() noexcept { return iterator(data_, step_); }
    [[nodiscard]] constexpr iterator end() noexcept { return iterator(data_ + step_ * count_, step_); }

    template <typename F>
    DENSE_FLATTEN void for_each(F&& f) noexcept
    {
        T* DENSE_RESTRICT p = data_;
        const size_t step = step_;
        const size_t n = count_;
        // 预取距离 8 (与项目 PREFETCH_R pd=8 规则一致)
        for (size_t i = 0; i < n; ++i)
        {
            if constexpr (sizeof(T) >= 16)
            {
                if (i + 8 < n) [[likely]] { DENSE_PREFETCH_R(p + 8 * step); }
            }
            f(*p);
            p += step;
        }
    }

    template <typename F>
    DENSE_FLATTEN void for_each(F&& f) const noexcept
    {
        const T* DENSE_RESTRICT p = data_;
        const size_t step = step_;
        const size_t n = count_;
        for (size_t i = 0; i < n; ++i)
        {
            if constexpr (sizeof(T) >= 16)
            {
                if (i + 8 < n) [[likely]] { DENSE_PREFETCH_R(p + 8 * step); }
            }
            f(*p);
            p += step;
        }
    }
};

template <typename T>
class dense
{
private:
	T* data_ptr_{nullptr};
	size_t maximum_quantity_{0};
	size_t index_{0};

	static constexpr size_t DEFAULT_CAPACITY = 64;
	static constexpr size_t SMALL_CAPACITY_THRESHOLD = 1024;
	static constexpr size_t MEDIUM_CAPACITY_THRESHOLD = 65536;
	static constexpr size_t cache_line = 64;
	// 实际分配对齐: malloc 返回 >= __STDCPP_DEFAULT_NEW_ALIGNMENT__ (16B)
	// 比 alignof(T) 更严格, 使 GCC 可用对齐 SIMD 指令
	static constexpr size_t alloc_align =
		(alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) ? alignof(T) : __STDCPP_DEFAULT_NEW_ALIGNMENT__;

	[[nodiscard]] static constexpr size_t round_up_to_default(size_t n) noexcept {
		return n == 0 ? DEFAULT_CAPACITY : n;
	}

	[[nodiscard]] static constexpr size_t calculate_new_capacity(size_t current) noexcept {
		if (current == 0) { return DEFAULT_CAPACITY; }

		// 启动区 L1 [0, 1K): 4x 快速启动
		if (current < SMALL_CAPACITY_THRESHOLD) { return current * 4; }

		// 工作区 L2 [1K, 64K): 大元素降倍率控制浪费
		if (current < MEDIUM_CAPACITY_THRESHOLD) {
			if constexpr (sizeof(T) <= 64) {
				return current * 4;           // 保持速度
			}
			else if constexpr (sizeof(T) <= 256) {
				return current * 2;           // 平衡
			}
			else {
				return current + current / 2; // 减少浪费
			}
		}

		// 大块区 L3 [64K, ∞): 64B 对齐大块分配成本高, 按 sizeof(T) 分流
		if constexpr (sizeof(T) <= 64) {
			return current * 4;               // 减少分配次数
		}
		else if constexpr (sizeof(T) <= 256) {
			// 2x: malloc 次数少 vs 1.5x: 拷贝总量少
			return current * 2;
		}
		else {
			return current + current / 2;     // 减少浪费
		}
	}

	[[nodiscard]] static constexpr size_t calculate_growth_for_reserve(size_t required) noexcept {
		if (required <= DEFAULT_CAPACITY) { return DEFAULT_CAPACITY; }
		if (required >= MEDIUM_CAPACITY_THRESHOLD) { return required; }
		size_t capacity = DEFAULT_CAPACITY;
		while (capacity < required) {
			capacity = calculate_new_capacity(capacity);
		}
		return capacity;
	}

	// 内存分配用 throwing new: 成功路径无 EH 开销, 比 malloc 快
	// 用 assume_aligned 使 GCC 生成对齐 SIMD 指令 (vmovdqa 而非 vmovdqu)
	[[nodiscard]] DENSE_ALWAYS_INLINE static T* allocate_data(size_t count) noexcept {
		if (count == 0) { return nullptr; }
		if constexpr (alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
			T* ptr = static_cast<T*>(::operator new(count * sizeof(T)));
			return std::assume_aligned<alloc_align>(ptr);
		}
		else {
			constexpr size_t align = alignof(T);
			T* ptr = static_cast<T*>(
				::operator new(count * sizeof(T), std::align_val_t{align}));
			return std::assume_aligned<alloc_align>(ptr);
		}
	}

	static void deallocate_data(T* ptr, size_t count) noexcept {
		if (ptr != nullptr) {
			if constexpr (alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
				// 用 sized delete 与 std::vector 一致, 比 unsized delete 快
				// 分配器无需查找块大小, 直接用 size hint
				::operator delete(ptr, count * sizeof(T));
			}
			else {
				constexpr size_t align = alignof(T);
				::operator delete(ptr, count * sizeof(T), std::align_val_t{align});
			}
		}
	}

	void destroy_range(size_t first, size_t last) noexcept {
		if constexpr (!std::is_trivially_destructible_v<T>) {
			if (first < last) {
				std::destroy_n(data_ptr_ + first, last - first);
			}
		}
	}

	void destroy_all() noexcept {
		if constexpr (std::is_trivially_destructible_v<T>) {
		}
		else
		{
			if (index_ == 0) { return; }
			std::destroy_n(data_ptr_, index_);
		}
	}

	void uninitialized_move_dense(T* src_first, T* src_last, T* dst) noexcept {
		if constexpr (std::is_trivially_copyable_v<T>) {
			const size_t n = static_cast<size_t>(src_last - src_first);
			if (n != 0) [[likely]] {
				// 参数可能为偏移指针, 用 alignof(T) (自然对齐, 始终安全)
				std::memcpy(std::assume_aligned<alignof(T)>(dst),
				            std::assume_aligned<alignof(T)>(src_first),
				            n * sizeof(T));
			}
		}
		else {
			std::uninitialized_move(src_first, src_last, dst);
			std::destroy(src_first, src_last);
		}
	}

	// 数据移动统一走 std::memcpy/std::memmove (MinGW 下):
	// 运行时通过 ifunc 选择最优实现 (libc 含 ERMS / AVX2 / AVX-512),
	// 比手动 AVX2 循环 + 函数调用 (target 属性无法内联) 更快更简单.

	// 函数级定向 AVX2: MinGW 不全局启用 -mavx2 (32B 栈对齐崩溃),
	// 但 vmovdqu (非对齐) 安全, 通过 target 属性仅在此函数内启用 AVX2.
	// 仅用于 erase 等大块重叠移动场景 (libc memmove 的 4x 展开收益有限).
#if defined(__GNUC__) && defined(__x86_64__) && !defined(__AVX2__) && !defined(__clang__)
	[[gnu::target("avx2")]]
	static void copy_trivial_data_avx2(char* dst, const char* src, size_t bytes) noexcept {
		const __m256i* s = static_cast<const __m256i*>(static_cast<const void*>(src));
		__m256i* d = static_cast<__m256i*>(static_cast<void*>(dst));
		const size_t ymm_count = bytes / 32;
		size_t i = 0;
		for (; i + 4 <= ymm_count; i += 4)
		{
			_mm256_storeu_si256(d + i, _mm256_loadu_si256(s + i));
			_mm256_storeu_si256(d + i + 1, _mm256_loadu_si256(s + i + 1));
			_mm256_storeu_si256(d + i + 2, _mm256_loadu_si256(s + i + 2));
			_mm256_storeu_si256(d + i + 3, _mm256_loadu_si256(s + i + 3));
		}
		for (; i < ymm_count; ++i)
		{
			_mm256_storeu_si256(d + i, _mm256_loadu_si256(s + i));
		}
		const size_t processed = ymm_count * 32;
		if (processed < bytes)
		{
			std::memcpy(dst + processed, src + processed, bytes - processed);
		}
	}
#endif

	// 广播填充用 AVX2: 比 GCC SSE2 自动向量化快 ~2x
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
	template <typename U>
		requires (sizeof(U) <= 8 && std::is_trivially_copyable_v<U>)
#if !defined(__AVX2__) && defined(__GNUC__) && !defined(__clang__)
	[[gnu::target("avx2")]]
#endif
	static void fill_small_trivial_avx2(U* dst, const U& value, size_t count) noexcept
	{
		__m256i broadcast;
		size_t elem_per_ymm;
		if constexpr (sizeof(U) == 1)
		{
			broadcast = _mm256_set1_epi8(*reinterpret_cast<const int8_t*>(&value));
			elem_per_ymm = 32;
		}
		else if constexpr (sizeof(U) == 2)
		{
			int16_t v; __builtin_memcpy(&v, &value, 2);
			broadcast = _mm256_set1_epi16(v);
			elem_per_ymm = 16;
		}
		else if constexpr (sizeof(U) == 4)
		{
			int32_t v; __builtin_memcpy(&v, &value, 4);
			broadcast = _mm256_set1_epi32(v);
			elem_per_ymm = 8;
		}
		else
		{
			int64_t v; __builtin_memcpy(&v, &value, 8);
			broadcast = _mm256_set1_epi64x(v);
			elem_per_ymm = 4;
		}
		const size_t chunks = count / elem_per_ymm;
		const size_t remainder = count - chunks * elem_per_ymm;
		__m256i* d = static_cast<__m256i*>(static_cast<void*>(dst));
		size_t i = 0;
		for (; i + 4 <= chunks; i += 4)
		{
			_mm256_storeu_si256(d + i, broadcast);
			_mm256_storeu_si256(d + i + 1, broadcast);
			_mm256_storeu_si256(d + i + 2, broadcast);
			_mm256_storeu_si256(d + i + 3, broadcast);
		}
		for (; i < chunks; ++i)
		{
			_mm256_storeu_si256(d + i, broadcast);
		}
		for (size_t j = 0; j < remainder; ++j)
		{
			dst[chunks * elem_per_ymm + j] = value;
		}
	}

	// 广播填充用 AVX2: 仅写 DRAM, 倍增法需读+写 = 2x DRAM 流量
#if !defined(__AVX2__) && defined(__GNUC__) && !defined(__clang__)
	[[gnu::target("avx2")]]
#endif
	static void fill_medium_trivial_avx2(char* dst, const char& value,
	                                      size_t bytes, size_t elem_size) noexcept
	{
		__m256i* d = static_cast<__m256i*>(static_cast<void*>(dst));
		const size_t total_ymm = bytes / 32;

		if (elem_size == 16)
		{
			__m128i v128;
			std::memcpy(&v128, &value, 16);
			__m256i broadcast = _mm256_broadcastsi128_si256(v128);
			size_t i = 0;
			for (; i + 4 <= total_ymm; i += 4)
			{
				_mm256_storeu_si256(d + i, broadcast);
				_mm256_storeu_si256(d + i + 1, broadcast);
				_mm256_storeu_si256(d + i + 2, broadcast);
				_mm256_storeu_si256(d + i + 3, broadcast);
			}
			for (; i < total_ymm; ++i)
			{
				_mm256_storeu_si256(d + i, broadcast);
			}
		}
		else
		{
			const size_t ymm_per_elem = elem_size / 32;
			__m256i vals[8];
			for (size_t k = 0; k < ymm_per_elem; ++k)
			{
				std::memcpy(&vals[k], &value + k * 32, 32);
			}
			const size_t unroll = 4 * ymm_per_elem;
			size_t i = 0;
			for (; i + unroll <= total_ymm; i += unroll)
			{
				for (size_t k = 0; k < ymm_per_elem; ++k)
				{
					_mm256_storeu_si256(d + i + k, vals[k]);
					_mm256_storeu_si256(d + i + ymm_per_elem + k, vals[k]);
					_mm256_storeu_si256(d + i + 2 * ymm_per_elem + k, vals[k]);
					_mm256_storeu_si256(d + i + 3 * ymm_per_elem + k, vals[k]);
				}
			}
			for (; i + ymm_per_elem <= total_ymm; i += ymm_per_elem)
			{
				for (size_t k = 0; k < ymm_per_elem; ++k)
				{
					_mm256_storeu_si256(d + i + k, vals[k]);
				}
			}
		}
		const size_t processed = total_ymm * 32;
		if (processed < bytes)
		{
			std::memmove(dst + processed, &value, bytes - processed);
		}
	}
#endif

	// 大块填充 ERMS 比 AVX2 store 快; AVX2 广播对小~中块 (64-8K) 更优 (无 memmove 启动开销)
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
	template <typename U>
		requires (sizeof(U) <= 8 && std::is_trivially_copyable_v<U>)
	static void fill_small_trivial(U* dst, const U& value, size_t count) noexcept
	{
		if (count < 64) [[unlikely]]
		{
			for (size_t i = 0; i < count; ++i) { dst[i] = value; }
			return;
		}
#if !defined(__AVX2__) && defined(__GNUC__) && !defined(__clang__)
		if (count < 8192 && __builtin_cpu_supports("avx2"))
		{
			fill_small_trivial_avx2(dst, value, count);
			return;
		}
#elif defined(__AVX2__)
		if (count < 8192)
		{
			fill_small_trivial_avx2(dst, value, count);
			return;
		}
#endif
		// 倍增填充法: 先填 32B, 再用 memmove (ERMS) 倍增
		// 倍增法 O(log n) 次 memmove (每次走 ERMS rep movsb), 大块吞吐 > AVX2 store
		const size_t initial = 32 / sizeof(U);
		for (size_t i = 0; i < initial; ++i) { dst[i] = value; }
		size_t filled = initial;
		while (filled * 2 <= count)
		{
			__builtin_memmove(dst + filled, dst, filled * sizeof(U));
			filled *= 2;
		}
		if (filled < count)
		{
			__builtin_memmove(dst + filled, dst, (count - filled) * sizeof(U));
		}
	}
#else
	template <typename U>
		requires (sizeof(U) <= 8 && std::is_trivially_copyable_v<U>)
	static void fill_small_trivial(U* dst, const U& value, size_t count) noexcept
	{
		for (size_t i = 0; i < count; ++i) { dst[i] = value; }
	}
#endif

	// 非临时存储 NTS 绕过 cache 直写 DRAM: 1x vs 2x DRAM 流量
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
	template <typename U>
		requires (sizeof(U) >= 16 && sizeof(U) % 16 == 0 && std::is_trivially_copyable_v<U>)
	static void fill_large_trivial_nts(U* dst, const U& value, size_t count) noexcept
	{
		constexpr size_t xmm_per_elem = sizeof(U) / 16;
		const char* val_ptr = reinterpret_cast<const char*>(&value);

		__m128i vals[xmm_per_elem];
		for (size_t k = 0; k < xmm_per_elem; ++k)
		{
			std::memcpy(&vals[k], val_ptr + k * 16, 16);
		}

		__m128i* d = static_cast<__m128i*>(static_cast<void*>(dst));
		const size_t total_xmm = count * xmm_per_elem;

		const size_t unroll_xmm = 4 * xmm_per_elem;
		size_t i = 0;
		for (; i + unroll_xmm <= total_xmm; i += unroll_xmm)
		{
			for (size_t e = 0; e < 4; ++e)
			{
				for (size_t k = 0; k < xmm_per_elem; ++k)
				{
					_mm_stream_si128(&d[i + e * xmm_per_elem + k], vals[k]);
				}
			}
		}
		for (; i + xmm_per_elem <= total_xmm; i += xmm_per_elem)
		{
			for (size_t k = 0; k < xmm_per_elem; ++k)
			{
				_mm_stream_si128(&d[i + k], vals[k]);
			}
		}
		_mm_sfence();
	}

	template <typename U>
		requires (sizeof(U) >= 32 && sizeof(U) % 32 == 0 && std::is_trivially_copyable_v<U>)
	static void fill_large_trivial(U* dst, const U& value, size_t count) noexcept
	{
		if (count < 64) [[unlikely]]
		{
			for (size_t i = 0; i < count; ++i) { dst[i] = value; }
			return;
		}
		const size_t bytes = count * sizeof(U);
		if (bytes >= 4 * 1024 * 1024)
		{
			fill_large_trivial_nts(dst, value, count);
			return;
		}
#if defined(__AVX2__)
		fill_medium_trivial_avx2(reinterpret_cast<char*>(dst),
		                         reinterpret_cast<const char&>(value),
		                         count * sizeof(U), sizeof(U));
#elif defined(__GNUC__) && !defined(__clang__)
		if (__builtin_cpu_supports("avx2"))
		{
			fill_medium_trivial_avx2(reinterpret_cast<char*>(dst),
			                         reinterpret_cast<const char&>(value),
			                         count * sizeof(U), sizeof(U));
		}
		else
		{
			dst[0] = value;
			size_t filled = 1;
			while (filled * 2 <= count)
			{
				__builtin_memmove(dst + filled, dst, filled * sizeof(U));
				filled *= 2;
			}
			if (filled < count)
			{
				__builtin_memmove(dst + filled, dst, (count - filled) * sizeof(U));
			}
		}
#else
		dst[0] = value;
		size_t filled = 1;
		while (filled * 2 <= count)
		{
			__builtin_memmove(dst + filled, dst, filled * sizeof(U));
			filled *= 2;
		}
		if (filled < count)
		{
			__builtin_memmove(dst + filled, dst, (count - filled) * sizeof(U));
		}
#endif
	}
#else
	template <typename U>
		requires (sizeof(U) >= 32 && sizeof(U) % 32 == 0 && std::is_trivially_copyable_v<U>)
	static void fill_large_trivial(U* dst, const U& value, size_t count) noexcept
	{
		if (count < 64) [[unlikely]]
		{
			for (size_t i = 0; i < count; ++i) { dst[i] = value; }
			return;
		}
		dst[0] = value;
		size_t filled = 1;
		while (filled * 2 <= count)
		{
			__builtin_memmove(dst + filled, dst, filled * sizeof(U));
			filled *= 2;
		}
		if (filled < count)
		{
			__builtin_memmove(dst + filled, dst, (count - filled) * sizeof(U));
		}
	}
#endif

	static void copy_trivial_data(T* DENSE_RESTRICT dst, const T* DENSE_RESTRICT src, size_t count) noexcept {
		const size_t bytes = count * sizeof(T);
#ifdef __AVX2__
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
		// 大块拷贝 AVX2 对 >= 2KB 数据有优势; 32MB 仍优于 ERMS
		if (bytes >= 2048)
		{
			const __m256i* s = static_cast<const __m256i*>(static_cast<const void*>(src));
			__m256i* d = static_cast<__m256i*>(static_cast<void*>(dst));
			const size_t ymm_count = bytes / 32;
			size_t i = 0;
			for (; i + 4 <= ymm_count; i += 4)
			{
				_mm256_storeu_si256(d + i, _mm256_loadu_si256(s + i));
				_mm256_storeu_si256(d + i + 1, _mm256_loadu_si256(s + i + 1));
				_mm256_storeu_si256(d + i + 2, _mm256_loadu_si256(s + i + 2));
				_mm256_storeu_si256(d + i + 3, _mm256_loadu_si256(s + i + 3));
			}
			for (; i < ymm_count; ++i)
			{
				_mm256_storeu_si256(d + i, _mm256_loadu_si256(s + i));
			}
			const size_t processed = ymm_count * 32;
			if (processed < bytes)
			{
				std::memcpy(reinterpret_cast<char*>(dst) + processed,
				            reinterpret_cast<const char*>(src) + processed,
				            bytes - processed);
			}
			return;
		}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#elif defined(__GNUC__) && defined(__x86_64__) && !defined(__clang__)
		// 运行时检测 AVX2 (MinGW), 仅用非对齐指令 (vmovdqu), 规避 32B 栈对齐问题
		if (bytes >= 2048 && __builtin_cpu_supports("avx2"))
		{
			copy_trivial_data_avx2(reinterpret_cast<char*>(dst),
			                       reinterpret_cast<const char*>(src), bytes);
			return;
		}
#endif
		if (bytes != 0) [[likely]]
		{
			// 参数可能为偏移指针 (如 emplace 的 new_data+index+1), 用 alignof(T)
			std::memcpy(std::assume_aligned<alignof(T)>(dst),
			            std::assume_aligned<alignof(T)>(src),
			            bytes);
		}
	}

	// --- 重叠区域移动 (替代 std::memmove 的方向已知版本) ---
	// 已知方向, 跳过 std::memmove 运行时方向检测开销

	// 前向移动 (dst < src, 从前向后扫描): 用于 erase
#if defined(__GNUC__) && defined(__x86_64__) && !defined(__AVX2__) && !defined(__clang__)
	[[gnu::target("avx2")]]
#endif
	static void move_trivial_forward(T* dst, const T* src, size_t count) noexcept {
		if (count == 0) { return; }
		const size_t bytes = count * sizeof(T);
#ifdef __AVX2__
		if (bytes >= 2048)
		{
			const char* s = reinterpret_cast<const char*>(src);
			char* d = reinterpret_cast<char*>(dst);
			const __m256i* s256 = static_cast<const __m256i*>(static_cast<const void*>(s));
			__m256i* d256 = static_cast<__m256i*>(static_cast<void*>(d));
			const size_t ymm_count = bytes / 32;
			// 前向 4x 展开: 减少循环开销, 提升指令级并行
			size_t i = 0;
			for (; i + 4 <= ymm_count; i += 4)
			{
				_mm256_storeu_si256(d256 + i, _mm256_loadu_si256(s256 + i));
				_mm256_storeu_si256(d256 + i + 1, _mm256_loadu_si256(s256 + i + 1));
				_mm256_storeu_si256(d256 + i + 2, _mm256_loadu_si256(s256 + i + 2));
				_mm256_storeu_si256(d256 + i + 3, _mm256_loadu_si256(s256 + i + 3));
			}
			for (; i < ymm_count; ++i)
			{
				_mm256_storeu_si256(d256 + i, _mm256_loadu_si256(s256 + i));
			}
			const size_t processed = ymm_count * 32;
			if (processed < bytes)
			{
				std::memmove(d + processed, s + processed, bytes - processed);
			}
			return;
		}
#elif defined(__GNUC__) && defined(__x86_64__) && !defined(__clang__)
		if (bytes >= 2048 && __builtin_cpu_supports("avx2"))
		{
			move_trivial_forward_avx2_helper(dst, src, bytes);
			return;
		}
#endif
		std::memmove(std::assume_aligned<alignof(T)>(dst),
		             std::assume_aligned<alignof(T)>(src), bytes);
	}

#if defined(__GNUC__) && defined(__x86_64__) && !defined(__AVX2__) && !defined(__clang__)
	[[gnu::target("avx2")]]
	static void move_trivial_forward_avx2_helper(T* dst, const T* src, size_t bytes) noexcept {
		const __m256i* s256 = static_cast<const __m256i*>(static_cast<const void*>(src));
		__m256i* d256 = static_cast<__m256i*>(static_cast<void*>(dst));
		const size_t ymm_count = bytes / 32;
		size_t i = 0;
		for (; i + 4 <= ymm_count; i += 4)
		{
			_mm256_storeu_si256(d256 + i, _mm256_loadu_si256(s256 + i));
			_mm256_storeu_si256(d256 + i + 1, _mm256_loadu_si256(s256 + i + 1));
			_mm256_storeu_si256(d256 + i + 2, _mm256_loadu_si256(s256 + i + 2));
			_mm256_storeu_si256(d256 + i + 3, _mm256_loadu_si256(s256 + i + 3));
		}
		for (; i < ymm_count; ++i)
		{
			_mm256_storeu_si256(d256 + i, _mm256_loadu_si256(s256 + i));
		}
		const size_t processed = ymm_count * 32;
		if (processed < bytes)
		{
			std::memmove(reinterpret_cast<char*>(dst) + processed,
			             reinterpret_cast<const char*>(src) + processed, bytes - processed);
		}
	}
#endif

	// 后向不展开 - store-to-load forwarding stall
	// 阈值: sizeof(T) <= 8: 64KB; <= 16: 32KB; > 16: 2KB
#if defined(__GNUC__) && defined(__x86_64__) && !defined(__AVX2__) && !defined(__clang__)
	[[gnu::target("avx2")]]
#endif
	static void move_trivial_backward(T* dst, const T* src, size_t count) noexcept {
		if (count == 0) { return; }
		const size_t bytes = count * sizeof(T);
#ifdef __AVX2__
		constexpr size_t avx2_threshold =
			(sizeof(T) <= 8) ? 65536 : ((sizeof(T) <= 16) ? 32768 : 2048);
		if (bytes >= avx2_threshold)
		{
			const char* s = reinterpret_cast<const char*>(src);
			char* d = reinterpret_cast<char*>(dst);
			const __m256i* s256 = static_cast<const __m256i*>(static_cast<const void*>(s));
			__m256i* d256 = static_cast<__m256i*>(static_cast<void*>(d));
			const size_t ymm_count = bytes / 32;
			// 后向单循环: 重叠移动展开有害, 保持单次 load+store
			for (size_t i = ymm_count; i > 0; --i)
			{
				_mm256_storeu_si256(d256 + i - 1, _mm256_loadu_si256(s256 + i - 1));
			}
			const size_t processed = ymm_count * 32;
			if (processed < bytes)
			{
				// 尾部从前向后处理 (已移到前面, 不重叠)
				std::memmove(d + processed, s + processed, bytes - processed);
			}
			return;
		}
#elif defined(__GNUC__) && defined(__x86_64__) && !defined(__clang__)
		constexpr size_t avx2_threshold =
			(sizeof(T) <= 8) ? 65536 : ((sizeof(T) <= 16) ? 32768 : 2048);
		if (bytes >= avx2_threshold && __builtin_cpu_supports("avx2"))
		{
			move_trivial_backward_avx2_helper(dst, src, bytes);
			return;
		}
#endif
		std::memmove(std::assume_aligned<alignof(T)>(dst),
		             std::assume_aligned<alignof(T)>(src), bytes);
	}

#if defined(__GNUC__) && defined(__x86_64__) && !defined(__AVX2__) && !defined(__clang__)
	[[gnu::target("avx2")]]
	static void move_trivial_backward_avx2_helper(T* dst, const T* src, size_t bytes) noexcept {
		const __m256i* s256 = static_cast<const __m256i*>(static_cast<const void*>(src));
		__m256i* d256 = static_cast<__m256i*>(static_cast<void*>(dst));
		const size_t ymm_count = bytes / 32;
		for (size_t i = ymm_count; i > 0; --i)
		{
			_mm256_storeu_si256(d256 + i - 1, _mm256_loadu_si256(s256 + i - 1));
		}
		const size_t processed = ymm_count * 32;
		if (processed < bytes)
		{
			std::memmove(reinterpret_cast<char*>(dst) + processed,
			             reinterpret_cast<const char*>(src) + processed, bytes - processed);
		}
	}
#endif

	void grow_data(size_t new_capacity) noexcept {
		if (new_capacity <= maximum_quantity_) [[likely]] {
			return;
		}

		T* new_data = allocate_data(new_capacity);
		if (data_ptr_ != nullptr && index_ > 0) [[likely]] {
			if constexpr (std::is_trivially_copyable_v<T>) {
				// 预取新数据区域, 减少 memcpy 的 cache miss
				// 仅对大容量 (>= 64KB) 有效, 小容量预取无意义
				if (new_capacity * sizeof(T) >= 65536) {
					DENSE_PREFETCH_R(new_data);
				}
				// __builtin_memcpy: GCC 内联小拷贝, 大块走 ERMS
				__builtin_memcpy(std::assume_aligned<alloc_align>(new_data),
				                 std::assume_aligned<alloc_align>(data_ptr_),
				                 index_ * sizeof(T));
			}
			else {
				uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
			}
			deallocate_data(data_ptr_, maximum_quantity_);
		}
		else if (data_ptr_ != nullptr) {
			deallocate_data(data_ptr_, maximum_quantity_);
		}

		data_ptr_ = new_data;
		maximum_quantity_ = new_capacity;
	}

public:
	using value_type = T;
	using pointer = T*;
	using const_pointer = const T*;
	using reference = T&;
	using const_reference = const T&;
	using size_type = size_t;
	using difference_type = std::ptrdiff_t;
	using iterator = T*;
	using const_iterator = const T*;

	constexpr dense() noexcept = default;

	explicit dense(size_t capacity) noexcept
		: data_ptr_(nullptr)
		, maximum_quantity_(capacity)
		, index_(0) {
		if (capacity > 0) [[likely]] {
			data_ptr_ = allocate_data(capacity);
		}
	}

	dense(size_t count, const T& value) noexcept
		: data_ptr_(nullptr)
		, maximum_quantity_(round_up_to_default(count))
		, index_(0) {
		if (count > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8) {
				fill_small_trivial(data_ptr_, value, count);
			}
			else if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) >= 32 && sizeof(T) % 32 == 0) {
				fill_large_trivial(data_ptr_, value, count);
			}
			else if constexpr (std::is_trivially_copyable_v<T>) {
				data_ptr_[0] = value;
				size_t filled = 1;
				while (filled * 2 <= count) {
					__builtin_memmove(data_ptr_ + filled, data_ptr_, filled * sizeof(T));
					filled *= 2;
				}
				if (filled < count) {
					__builtin_memmove(data_ptr_ + filled, data_ptr_, (count - filled) * sizeof(T));
				}
			}
			else {
				for (size_t i = 0; i < count; ++i) {
					new (&data_ptr_[i]) T(value);
				}
			}
			index_ = count;
		}
	}

	template <typename InputIt>
	dense(InputIt first, InputIt last) noexcept
		: data_ptr_(nullptr)
		, maximum_quantity_(0)
		, index_(0) {
		const size_t count = static_cast<size_t>(std::distance(first, last));
		maximum_quantity_ = round_up_to_default(count);

		if (count > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			size_t i = 0;
			for (auto it = first; it != last; ++it, ++i) {
				new (&data_ptr_[i]) T(*it);
			}
			index_ = count;
		}
	}

	dense(std::initializer_list<T> init) noexcept
		: dense(init.begin(), init.end()) {}

	// 拷贝构造: 仅分配 index_ (size), 不复制 maximum_quantity_ (capacity)
	// 与 std::vector 语义一致: 副本的 capacity == 源的 size
	dense(const dense& other) noexcept
		: data_ptr_(nullptr)
		, maximum_quantity_(other.index_)
		, index_(other.index_) {
		if (maximum_quantity_ > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			if constexpr (std::is_trivially_copyable_v<T>) {
				__builtin_memcpy(std::assume_aligned<alloc_align>(data_ptr_),
				                 std::assume_aligned<alloc_align>(other.data_ptr_),
				                 index_ * sizeof(T));
			}
			else {
				std::uninitialized_copy(other.data_ptr_, other.data_ptr_ + index_, data_ptr_);
			}
		}
	}

	dense(dense&& other) noexcept
		: data_ptr_(other.data_ptr_)
		, maximum_quantity_(other.maximum_quantity_)
		, index_(other.index_) {
		other.data_ptr_ = nullptr;
		other.maximum_quantity_ = 0;
		other.index_ = 0;
	}

	// 拷贝赋值: 仅分配 index_ (size), 不复制 maximum_quantity_ (capacity)
	dense& operator=(const dense& other) noexcept {
		if (this == &other) [[unlikely]] { return *this; }
		destroy_all();
		deallocate_data(data_ptr_, maximum_quantity_);

		maximum_quantity_ = other.index_;
		index_ = other.index_;

		if (maximum_quantity_ > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			if constexpr (std::is_trivially_copyable_v<T>) {
				const size_t bytes = other.index_ * sizeof(T);
				if (bytes != 0) [[likely]] {
					__builtin_memcpy(std::assume_aligned<alloc_align>(data_ptr_),
					                 std::assume_aligned<alloc_align>(other.data_ptr_),
					                 bytes);
				}
			}
			else {
				std::uninitialized_copy(other.data_ptr_, other.data_ptr_ + other.index_, data_ptr_);
			}
		}
		else {
			data_ptr_ = nullptr;
		}
		return *this;
	}

	dense& operator=(dense&& other) noexcept {
		if (this == &other) [[unlikely]] { return *this; }
		destroy_all();
		deallocate_data(data_ptr_, maximum_quantity_);

		data_ptr_ = other.data_ptr_;
		maximum_quantity_ = other.maximum_quantity_;
		index_ = other.index_;

		other.data_ptr_ = nullptr;
		other.maximum_quantity_ = 0;
		other.index_ = 0;
		return *this;
	}

	~dense() noexcept {
		destroy_all();
		deallocate_data(data_ptr_, maximum_quantity_);
	}

	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr T& operator[](size_t index) noexcept {
		return std::assume_aligned<alloc_align>(data_ptr_)[index];
	}

	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const T& operator[](size_t index) const noexcept {
		return std::assume_aligned<alloc_align>(data_ptr_)[index];
	}

	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr T& get(size_t index) noexcept {
		return std::assume_aligned<alloc_align>(data_ptr_)[index];
	}

	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const T& get(size_t index) const noexcept {
		return std::assume_aligned<alloc_align>(data_ptr_)[index];
	}

	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr T& get(size_t index, size_t error_index) noexcept {
		return data_ptr_[index < index_ ? index : error_index];
	}

	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const T& get(size_t index, size_t error_index) const noexcept {
		return data_ptr_[index < index_ ? index : error_index];
	}

	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr T& front() noexcept { return std::assume_aligned<alloc_align>(data_ptr_)[0]; }
	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const T& front() const noexcept { return std::assume_aligned<alloc_align>(data_ptr_)[0]; }
	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr T& back() noexcept { return std::assume_aligned<alloc_align>(data_ptr_)[index_ - 1]; }
	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const T& back() const noexcept { return std::assume_aligned<alloc_align>(data_ptr_)[index_ - 1]; }

	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr pointer data() noexcept { return std::assume_aligned<alloc_align>(data_ptr_); }
	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const_pointer data() const noexcept { return std::assume_aligned<alloc_align>(data_ptr_); }

	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr iterator begin() noexcept { return std::assume_aligned<alloc_align>(data_ptr_); }
	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const_iterator begin() const noexcept { return std::assume_aligned<alloc_align>(data_ptr_); }
	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const_iterator cbegin() const noexcept { return std::assume_aligned<alloc_align>(data_ptr_); }
	// 不用 assume_aligned: 使编译器可见 end()-data_ptr_==index_, 否则 erase(end-1) 无法向量化
	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr iterator end() noexcept { return data_ptr_ + index_; }
	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const_iterator end() const noexcept { return data_ptr_ + index_; }
	[[nodiscard]] DENSE_ALWAYS_INLINE constexpr const_iterator cend() const noexcept { return data_ptr_ + index_; }

	[[nodiscard]] constexpr size_type capacity() const noexcept { return maximum_quantity_; }
	[[nodiscard]] constexpr size_type size() const noexcept { return index_; }
	[[nodiscard]] constexpr bool empty() const noexcept { return index_ == 0; }
	[[nodiscard]] constexpr bool valid() const noexcept { return data_ptr_ != nullptr; }
	[[nodiscard]] constexpr size_type max_size() const noexcept { return static_cast<size_t>(-1) / sizeof(T); }
	[[nodiscard]] constexpr size_type size_bytes() const noexcept { return index_ * sizeof(T); }
	[[nodiscard]] constexpr size_type capacity_bytes() const noexcept { return maximum_quantity_ * sizeof(T); }

	[[nodiscard]] constexpr size_type count() const noexcept { return index_; }

	// 密集容器恒为密集模式, 槽位恒已构造 (与 class_pool 接口对齐, 供视图函数统一调用)
	[[nodiscard]] constexpr bool is_dense() const noexcept { return true; }
	[[nodiscard]] constexpr bool is_constructed_at(size_t i) const noexcept { return i < index_; }

	[[nodiscard]] constexpr std::span<T> span() noexcept { return std::span<T>(data_ptr_, index_); }
	[[nodiscard]] constexpr std::span<const T> span() const noexcept { return std::span<const T>(data_ptr_, index_); }

	void clear() noexcept {
		destroy_all();
		index_ = 0;
	}

	DENSE_ALWAYS_INLINE void increase_capacity(size_t new_capacity) noexcept {
		if (new_capacity > maximum_quantity_) [[unlikely]] {
			size_t new_cap = calculate_new_capacity(maximum_quantity_);
			if (new_cap < new_capacity) { new_cap = new_capacity; }
			grow_data(new_cap);
		}
	}

	// 只扩容不缩容: new_capacity <= size 时直接返回, 不销毁任何对象
	void increase_capacity(size_t new_capacity, const T& value) noexcept {
		if (new_capacity <= index_) [[likely]] {
			return;
		}
		fill_bulk(value, index_, new_capacity - index_);
	}

	void reserve_exact(size_t new_capacity) noexcept {
		grow_data(new_capacity);
	}

	void shrink_to_fit() noexcept {
		if (index_ == 0 && data_ptr_ != nullptr) [[unlikely]] {
			deallocate_data(data_ptr_, maximum_quantity_);
			data_ptr_ = nullptr;
			maximum_quantity_ = 0;
			return;
		}

		if (index_ < maximum_quantity_ && index_ > 0) [[likely]] {
			T* new_data = allocate_data(index_);
			if constexpr (std::is_trivially_copyable_v<T>) {
				__builtin_memcpy(std::assume_aligned<alloc_align>(new_data),
				                 std::assume_aligned<alloc_align>(data_ptr_),
				                 index_ * sizeof(T));
			}
			else {
				uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
			}
			deallocate_data(data_ptr_, maximum_quantity_);
			data_ptr_ = new_data;
			maximum_quantity_ = index_;
		}
	}

	void reduce_capacity(size_t new_capacity) noexcept {
		if (new_capacity >= maximum_quantity_) [[likely]] {
			return;
		}

		if (new_capacity == 0) {
			destroy_all();
			deallocate_data(data_ptr_, maximum_quantity_);
			data_ptr_ = nullptr;
			maximum_quantity_ = 0;
			index_ = 0;
			return;
		}

		if (new_capacity < index_) {
			destroy_range(new_capacity, index_);
			index_ = new_capacity;
		}

		T* new_data = allocate_data(new_capacity);
		if constexpr (std::is_trivially_copyable_v<T>) {
			// 缩容 ERMS 比 AVX2 4x 展开快 (POD4 1MB 慢 7%)
			const size_t bytes = index_ * sizeof(T);
			if (bytes != 0) [[likely]] {
				__builtin_memcpy(std::assume_aligned<alloc_align>(new_data),
				                 std::assume_aligned<alloc_align>(data_ptr_),
				                 bytes);
			}
		}
		else {
			uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
		}
		deallocate_data(data_ptr_, maximum_quantity_);
		data_ptr_ = new_data;
		maximum_quantity_ = new_capacity;
	}

	// 截断到 new_capacity, 把 [new_capacity, index_) 区间的元素移动到 dst 末尾
	void reduce_capacity(size_t new_capacity, dense<T>& dst) noexcept {
		if (new_capacity >= index_) [[likely]] {
			return;
		}

		const size_t move_count = index_ - new_capacity;
		dst.increase_capacity(dst.index_ + move_count);

		if constexpr (std::is_trivially_copyable_v<T>) {
			if (move_count > 0) [[likely]] {
				std::memcpy(dst.data_ptr_ + dst.index_,
				            data_ptr_ + new_capacity,
				            move_count * sizeof(T));
				dst.index_ += move_count;
			}
		}
		else {
			for (size_t i = new_capacity; i < index_; ++i) {
				new (&dst.data_ptr_[dst.index_]) T(std::move(data_ptr_[i]));
				++dst.index_;
				if constexpr (!std::is_trivially_destructible_v<T>) {
					data_ptr_[i].~T();
				}
			}
		}
		index_ = new_capacity;

		if (new_capacity == 0) {
			deallocate_data(data_ptr_, maximum_quantity_);
			data_ptr_ = nullptr;
			maximum_quantity_ = 0;
			return;
		}

		if (new_capacity < maximum_quantity_) {
			T* new_data = allocate_data(new_capacity);
			if constexpr (std::is_trivially_copyable_v<T>) {
				copy_trivial_data(new_data, data_ptr_, index_);
			}
			else {
				uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
			}
			deallocate_data(data_ptr_, maximum_quantity_);
			data_ptr_ = new_data;
			maximum_quantity_ = new_capacity;
		}
	}

	template <typename... Args>
	DENSE_ALWAYS_INLINE void emplace_back(Args&&... args) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data(calculate_new_capacity(maximum_quantity_));
		}
		new (std::assume_aligned<alloc_align>(data_ptr_) + index_) T(std::forward<Args>(args)...);
		++index_;
	}

	// 非模板重载优先, 避免 placement new 开销
	DENSE_ALWAYS_INLINE void emplace_back(const T& value) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data(calculate_new_capacity(maximum_quantity_));
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = value;
		} else {
			new (p) T(value);
		}
		++index_;
	}

	DENSE_ALWAYS_INLINE void emplace_back(T&& value) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data(calculate_new_capacity(maximum_quantity_));
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = std::move(value);
		} else {
			new (p) T(std::move(value));
		}
		++index_;
	}

	DENSE_ALWAYS_INLINE void push_back(const T& value) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data(calculate_new_capacity(maximum_quantity_));
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = value;
		} else {
			new (p) T(value);
		}
		++index_;
	}

	DENSE_ALWAYS_INLINE void push_back(T&& value) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data(calculate_new_capacity(maximum_quantity_));
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = std::move(value);
		} else {
			new (p) T(std::move(value));
		}
		++index_;
	}

	// 无容量检查的追加 (unchecked), 调用方需确保 capacity 足够
	DENSE_ALWAYS_INLINE void push_back_unchecked(const T& value) noexcept {
		if constexpr (sizeof(T) > 128) {
			if (index_ >= maximum_quantity_) { __builtin_unreachable(); }
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = value;
		} else {
			new (p) T(value);
		}
		++index_;
	}

	DENSE_ALWAYS_INLINE void push_back_unchecked(T&& value) noexcept {
		if constexpr (sizeof(T) > 128) {
			if (index_ >= maximum_quantity_) { __builtin_unreachable(); }
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = std::move(value);
		} else {
			new (p) T(std::move(value));
		}
		++index_;
	}

	template <typename... Args>
	DENSE_ALWAYS_INLINE void emplace_back_unchecked(Args&&... args) noexcept {
		if constexpr (sizeof(T) > 128) {
			if (index_ >= maximum_quantity_) { __builtin_unreachable(); }
		}
		new (std::assume_aligned<alloc_align>(data_ptr_) + index_) T(std::forward<Args>(args)...);
		++index_;
	}

	DENSE_ALWAYS_INLINE void emplace_back_unchecked(const T& value) noexcept {
		if constexpr (sizeof(T) > 128) {
			if (index_ >= maximum_quantity_) { __builtin_unreachable(); }
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = value;
		} else {
			new (p) T(value);
		}
		++index_;
	}

	DENSE_ALWAYS_INLINE void emplace_back_unchecked(T&& value) noexcept {
		if constexpr (sizeof(T) > 128) {
			if (index_ >= maximum_quantity_) { __builtin_unreachable(); }
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = std::move(value);
		} else {
			new (p) T(std::move(value));
		}
		++index_;
	}

	template <typename... Args>
	DENSE_ALWAYS_INLINE void emplace_back_dense_unchecked(Args&&... args) noexcept {
		if constexpr (sizeof(T) > 128) {
			if (index_ >= maximum_quantity_) { __builtin_unreachable(); }
		}
		new (std::assume_aligned<alloc_align>(data_ptr_) + index_) T(std::forward<Args>(args)...);
		++index_;
	}

	DENSE_ALWAYS_INLINE void emplace_back_dense_unchecked(const T& value) noexcept {
		if constexpr (sizeof(T) > 128) {
			if (index_ >= maximum_quantity_) { __builtin_unreachable(); }
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = value;
		} else {
			new (p) T(value);
		}
		++index_;
	}

	DENSE_ALWAYS_INLINE void emplace_back_dense_unchecked(T&& value) noexcept {
		if constexpr (sizeof(T) > 128) {
			if (index_ >= maximum_quantity_) { __builtin_unreachable(); }
		}
		T* DENSE_RESTRICT p = std::assume_aligned<alloc_align>(data_ptr_) + index_;
		if constexpr (std::is_trivially_copyable_v<T>) {
			*p = std::move(value);
		} else {
			new (p) T(std::move(value));
		}
		++index_;
	}

	void append_n(size_t n, const T& value) noexcept {
		if (n == 0) [[unlikely]] { return; }
		if (n > maximum_quantity_ - index_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(index_ + n));
		}

		T* dst = data_ptr_ + index_;
		if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8)
		{
			fill_small_trivial(dst, value, n);
		}
		else if constexpr (std::is_trivially_copyable_v<T>)
		{
			if (n < 64) {
				for (size_t i = 0; i < n; ++i) { dst[i] = value; }
			}
			else if constexpr (sizeof(T) == 16 || sizeof(T) == 32) {
				const size_t bytes = n * sizeof(T);
				if (bytes >= 8192) {
					dst[0] = value;
					size_t filled = 1;
					while (filled * 2 <= n) {
						__builtin_memmove(dst + filled, dst, filled * sizeof(T));
						filled *= 2;
					}
					if (filled < n) {
						__builtin_memmove(dst + filled, dst, (n - filled) * sizeof(T));
					}
				}
				else {
					fill_medium_trivial_avx2(reinterpret_cast<char*>(dst),
					                         reinterpret_cast<const char&>(value),
					                         bytes, sizeof(T));
				}
			}
			else if constexpr (sizeof(T) >= 32 && sizeof(T) % 32 == 0) {
				// 仅写 DRAM, 倍增法需读+写 = 2x DRAM 流量
				fill_large_trivial(dst, value, n);
			}
			else {
				dst[0] = value;
				size_t filled = 1;
				while (filled * 2 <= n) {
					__builtin_memmove(dst + filled, dst, filled * sizeof(T));
					filled *= 2;
				}
				if (filled < n) {
					__builtin_memmove(dst + filled, dst, (n - filled) * sizeof(T));
				}
			}
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[i]) T(value);
			}
		}
		index_ += n;
	}

	DENSE_ALWAYS_INLINE void append_bulk(const T* src, size_t count) noexcept {
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(index_ + count));
		}
		if constexpr (std::is_trivially_copyable_v<T>) {
		// 不用 assume_aligned: LTO 下会让 GCC 内联大块拷贝, 比 ERMS 慢 10%
		std::memcpy(data_ptr_ + index_, src, count * sizeof(T));
		}
		else {
			for (size_t i = 0; i < count; ++i) {
				new (data_ptr_ + index_ + i) T(src[i]);
			}
		}
		index_ += count;
	}

	DENSE_ALWAYS_INLINE void append_bulk_move(T* src, size_t count) noexcept {
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(index_ + count));
		}
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(data_ptr_ + index_, src, count * sizeof(T));
		}
		else {
			for (size_t i = 0; i < count; ++i) {
				new (data_ptr_ + index_ + i) T(std::move(src[i]));
			}
		}
		index_ += count;
	}

	void append_incrementing(size_t count, uint64_t& counter) noexcept {
		static_assert(std::is_trivially_copyable_v<T>,
			"append_incrementing requires trivially copyable T");
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(index_ + count));
		}
		for (size_t i = 0; i < count; ++i) {
			data_ptr_[index_ + i] = ++counter;
		}
		index_ += count;
	}

	template <typename F>
	void append_generated(size_t count, F&& generator) noexcept {
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(index_ + count));
		}
		for (size_t i = 0; i < count; ++i) {
			new (&data_ptr_[index_ + i]) T(generator());
		}
		index_ += count;
	}

	void fill_bulk(const T& value, size_t start, size_t count) noexcept {
		if (count == 0) { return; }
		size_t end = start + count;
		if (end > maximum_quantity_) [[unlikely]] {
			size_t new_cap = calculate_new_capacity(maximum_quantity_);
			if (new_cap < end) { new_cap = end; }
			grow_data(new_cap);
		}

		const size_t old_index = index_;

		if (start > old_index) {
			for (size_t i = old_index; i < start; ++i) {
				new (&data_ptr_[i]) T{};
			}
		}

		if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 1) {
			std::memset(data_ptr_ + start, static_cast<unsigned char>(value), count);
		}
		else if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8) {
			fill_small_trivial(data_ptr_ + start, value, count);
		}
		else if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) >= 32 && sizeof(T) % 32 == 0) {
			fill_large_trivial(data_ptr_ + start, value, count);
		}
		else if constexpr (std::is_trivially_copyable_v<T>) {
			T* dst = data_ptr_ + start;
			dst[0] = value;
			size_t filled = 1;
			while (filled * 2 <= count) {
				__builtin_memmove(dst + filled, dst, filled * sizeof(T));
				filled *= 2;
			}
			if (filled < count) {
				__builtin_memmove(dst + filled, dst, (count - filled) * sizeof(T));
			}
		}
		else {
			const size_t overlap_end = std::min(end, old_index);
			for (size_t i = start; i < overlap_end; ++i) {
				data_ptr_[i].~T();
				new (&data_ptr_[i]) T(value);
			}
			for (size_t i = overlap_end; i < end; ++i) {
				new (&data_ptr_[i]) T(value);
			}
		}

		if (end > index_) {
			index_ = end;
		}
	}

	// 强制 always_inline: 非 inline 时 GCC 不内联, POD32 比 vector 慢 7%
	DENSE_ALWAYS_INLINE void pop_back() noexcept {
		if constexpr (!std::is_trivially_destructible_v<T>) {
			data_ptr_[--index_].~T();
		}
		else {
			--index_;
		}
	}

	template <typename... Args>
	DENSE_FLATTEN iterator emplace(const_iterator pos, Args&&... args) noexcept {
		const size_t index = static_cast<size_t>(pos - data_ptr_);

		// 重新分配路径: 直接在 新缓冲区 中放置元素到最终位置
		if (index_ >= maximum_quantity_) [[unlikely]] {
			const size_t new_cap = calculate_new_capacity(maximum_quantity_);
			T* new_data = allocate_data(new_cap);

			if (index < index_) [[likely]] {
				if constexpr (std::is_trivially_copyable_v<T>) {
					if (index > 0) {
						copy_trivial_data(new_data, data_ptr_, index);
					}
					if (index_ > index) {
						copy_trivial_data(new_data + index + 1, data_ptr_ + index, index_ - index);
					}
				}
				else {
					if (index > 0) {
						std::uninitialized_move(data_ptr_, data_ptr_ + index, new_data);
					}
					if (index_ > index) {
						std::uninitialized_move(data_ptr_ + index, data_ptr_ + index_,
						                        new_data + index + 1);
					}
					std::destroy(data_ptr_, data_ptr_ + index_);
				}
			}
			else {
				if constexpr (std::is_trivially_copyable_v<T>) {
					copy_trivial_data(new_data, data_ptr_, index_);
				}
				else {
					uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
				}
			}

			new (new_data + index) T(std::forward<Args>(args)...);
			deallocate_data(data_ptr_, maximum_quantity_);
			data_ptr_ = new_data;
			maximum_quantity_ = new_cap;
			++index_;
			return data_ptr_ + index;
		}

		if (index < index_) [[likely]] {
			if constexpr (std::is_trivially_copyable_v<T>) {
				// 后向移动 (dst = src+1): AVX2 单循环避免 memmove 方向检测
				const size_t move_count = index_ - index;
				if (move_count <= 8) {
					T* d = data_ptr_ + index;
					for (size_t i = move_count; i > 0; --i) {
						d[i] = d[i - 1];
					}
				}
				else {
					move_trivial_backward(data_ptr_ + index + 1,
					                      data_ptr_ + index, move_count);
				}
			}
			else {
				new (&data_ptr_[index_]) T(std::move(data_ptr_[index_ - 1]));
				if (index < index_ - 1) {
					std::move_backward(data_ptr_ + index, data_ptr_ + index_ - 1,
					                   data_ptr_ + index_);
				}
				data_ptr_[index].~T();
			}
		}

		new (data_ptr_ + index) T(std::forward<Args>(args)...);
		++index_;
		return data_ptr_ + index;
	}

	iterator insert(const_iterator pos, const T& value) noexcept {
		return emplace(pos, value);
	}

	iterator insert(const_iterator pos, T&& value) noexcept {
		return emplace(pos, std::move(value));
	}

	// 末尾快路径等价 pop_back, 可批量优化为 index_ -= n
	// 中间删除 noinline 避免污染快路径
	NOINLINE iterator erase_middle(size_t index, size_t move_count) noexcept
	{
		if constexpr (std::is_trivially_copyable_v<T>) {
			if (move_count <= 8) {
				T* d = data_ptr_ + index;
				for (size_t i = 0; i < move_count; ++i) {
					d[i] = d[i + 1];
				}
			}
			else {
				move_trivial_forward(data_ptr_ + index,
				                     data_ptr_ + index + 1, move_count);
			}
		}
		else {
			std::move(data_ptr_ + index + 1, data_ptr_ + index_, data_ptr_ + index);
			if constexpr (!std::is_trivially_destructible_v<T>) {
				data_ptr_[index_ - 1].~T();
			}
		}
		--index_;
		return data_ptr_ + index;
	}

	DENSE_ALWAYS_INLINE iterator erase(const_iterator pos) noexcept {
		// 指针比较让编译器可见 pop_back == --index_, 可批量优化为 index_ -= n
		if (pos == data_ptr_ + index_ - 1) [[likely]] {
			pop_back();
			return data_ptr_ + index_;
		}

		const size_t index = static_cast<size_t>(pos - data_ptr_);
		return erase_middle(index, index_ - index - 1);
	}

	DENSE_FLATTEN iterator erase(const_iterator first, const_iterator last) noexcept {
		const size_t start_index = static_cast<size_t>(first - data_ptr_);
		const size_t end_index = static_cast<size_t>(last - data_ptr_);

		if (start_index >= index_) [[unlikely]] {
			return end();
		}

		const size_t real_end = end_index > index_ ? index_ : end_index;
		if (start_index >= real_end) [[unlikely]] {
			return data_ptr_ + start_index;
		}

		const size_t gap = real_end - start_index;
		destroy_range(start_index, real_end);

		if (real_end < index_) {
			const size_t tail_count = index_ - real_end;
			if constexpr (std::is_trivially_copyable_v<T>) {
				if (tail_count <= 8) {
					T* d = data_ptr_ + start_index;
					T* s = data_ptr_ + real_end;
					for (size_t i = 0; i < tail_count; ++i) {
						d[i] = s[i];
					}
				}
				else {
					move_trivial_forward(data_ptr_ + start_index,
					                     data_ptr_ + real_end, tail_count);
				}
			}
			else {
				std::move(data_ptr_ + real_end, data_ptr_ + index_, data_ptr_ + start_index);
			}
		}

		index_ -= gap;
		return data_ptr_ + start_index;
	}

	void swap(dense& other) noexcept {
		std::swap(data_ptr_, other.data_ptr_);
		std::swap(maximum_quantity_, other.maximum_quantity_);
		std::swap(index_, other.index_);
	}

	template <typename F>
	void for_each(F&& f) noexcept
	{
		::for_each(*this, std::forward<F>(f));
	}

	template <typename F>
	void for_each(F&& f) const noexcept
	{
		::for_each(*this, std::forward<F>(f));
	}

	// 子范围切片, 返回 std::span
	[[nodiscard]] constexpr std::span<T> subspan(size_t offset, size_t count) noexcept
	{
		return ::subspan(*this, offset, count);
	}
	[[nodiscard]] constexpr std::span<const T> subspan(size_t offset, size_t count) const noexcept
	{
		return ::subspan(*this, offset, count);
	}
	[[nodiscard]] constexpr std::span<T> subspan(size_t offset) noexcept
	{
		return ::subspan(*this, offset);
	}
	[[nodiscard]] constexpr std::span<const T> subspan(size_t offset) const noexcept
	{
		return ::subspan(*this, offset);
	}
	[[nodiscard]] constexpr std::span<T> first(size_t n) noexcept
	{
		return ::first(*this, n);
	}
	[[nodiscard]] constexpr std::span<const T> first(size_t n) const noexcept
	{
		return ::first(*this, n);
	}
	[[nodiscard]] constexpr std::span<T> last(size_t n) noexcept
	{
		return ::last(*this, n);
	}
	[[nodiscard]] constexpr std::span<const T> last(size_t n) const noexcept
	{
		return ::last(*this, n);
	}
	template <size_t N>
	[[nodiscard]] constexpr std::span<T, N> first_fixed() noexcept
	{
		return ::first_fixed<N>(*this);
	}
	template <size_t N>
	[[nodiscard]] constexpr std::span<const T, N> first_fixed() const noexcept
	{
		return ::first_fixed<N>(*this);
	}
	template <size_t N>
	[[nodiscard]] constexpr std::span<T, N> last_fixed() noexcept
	{
		return ::last_fixed<N>(*this);
	}
	template <size_t N>
	[[nodiscard]] constexpr std::span<const T, N> last_fixed() const noexcept
	{
		return ::last_fixed<N>(*this);
	}

	[[nodiscard]] constexpr std::reverse_iterator<iterator> rbegin() noexcept
	{
		return std::reverse_iterator<iterator>(end());
	}
	[[nodiscard]] constexpr std::reverse_iterator<iterator> rend() noexcept
	{
		return std::reverse_iterator<iterator>(begin());
	}
	[[nodiscard]] constexpr std::reverse_iterator<const_iterator> rbegin() const noexcept
	{
		return std::reverse_iterator<const_iterator>(end());
	}
	[[nodiscard]] constexpr std::reverse_iterator<const_iterator> rend() const noexcept
	{
		return std::reverse_iterator<const_iterator>(begin());
	}
	[[nodiscard]] constexpr std::reverse_iterator<const_iterator> crbegin() const noexcept
	{
		return std::reverse_iterator<const_iterator>(cend());
	}
	[[nodiscard]] constexpr std::reverse_iterator<const_iterator> crend() const noexcept
	{
		return std::reverse_iterator<const_iterator>(cbegin());
	}

	template <typename F>
	void reverse_for_each(F&& f) noexcept
	{
		::reverse_for_each(*this, std::forward<F>(f));
	}
	template <typename F>
	void reverse_for_each(F&& f) const noexcept
	{
		::reverse_for_each(*this, std::forward<F>(f));
	}

	[[nodiscard]] constexpr strided_span<T> strided_span_view(size_t start, size_t step, size_t count) noexcept
	{
		const size_t s = start > index_ ? index_ : start;
		const size_t max_cnt = (step > 0) ? (index_ - s + step - 1) / step : 0;
		const size_t cnt = count > max_cnt ? max_cnt : count;
		return strided_span<T>(data_ptr_ + s, step ? step : 1, cnt);
	}
	[[nodiscard]] constexpr strided_span<const T> strided_span_view(size_t start, size_t step, size_t count) const noexcept
	{
		const size_t s = start > index_ ? index_ : start;
		const size_t max_cnt = (step > 0) ? (index_ - s + step - 1) / step : 0;
		const size_t cnt = count > max_cnt ? max_cnt : count;
		return strided_span<const T>(data_ptr_ + s, step ? step : 1, cnt);
	}

	template <typename F>
	void strided_for_each(size_t start, size_t step, F&& f) noexcept
	{
		::strided_for_each(*this, start, step, std::forward<F>(f));
	}
	template <typename F>
	void strided_for_each(size_t start, size_t step, F&& f) const noexcept
	{
		::strided_for_each(*this, start, step, std::forward<F>(f));
	}

	template <size_t Step, typename F>
	void strided_for_each(F&& f) noexcept
	{
		::strided_for_each<Step>(*this, std::forward<F>(f));
	}
	template <size_t Step, typename F>
	void strided_for_each(F&& f) const noexcept
	{
		::strided_for_each<Step>(*this, std::forward<F>(f));
	}

	// 变换融合遍历: 对每个 v 调用 consume(transform(v)), 避免中间临时容器
	template <typename FTransform, typename FConsume>
	void transform_for_each(FTransform&& transform, FConsume&& consume) noexcept
	{
		::transform_for_each(*this, std::forward<FTransform>(transform), std::forward<FConsume>(consume));
	}
	template <typename FTransform, typename FConsume>
	void transform_for_each(FTransform&& transform, FConsume&& consume) const noexcept
	{
		::transform_for_each(*this, std::forward<FTransform>(transform), std::forward<FConsume>(consume));
	}

	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] T* find_if(Pred pred) noexcept
	{
		return ::find_if(*this, pred);
	}
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] const T* find_if(Pred pred) const noexcept
	{
		return ::find_if(*this, pred);
	}
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] T* find_if_not(Pred pred) noexcept
	{
		return ::find_if_not(*this, pred);
	}
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] const T* find_if_not(Pred pred) const noexcept
	{
		return ::find_if_not(*this, pred);
	}

	[[nodiscard]] T* find(const T& value) noexcept
	{
		return ::find(*this, value);
	}
	[[nodiscard]] const T* find(const T& value) const noexcept
	{
		return ::find(*this, value);
	}

	[[nodiscard]] bool contains(const T& value) const noexcept
	{
		return ::contains(*this, value);
	}

	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] size_t count_if(Pred pred) const noexcept
	{
		return ::count_if(*this, pred);
	}

	template <typename Pred, typename F>
	requires std::predicate<Pred, const T&> && std::invocable<F, T&>
	void filter_for_each(Pred pred, F&& f) noexcept
	{
		::filter_for_each(*this, pred, std::forward<F>(f));
	}
	template <typename Pred, typename F>
	requires std::predicate<Pred, const T&> && std::invocable<F, const T&>
	void filter_for_each(Pred pred, F&& f) const noexcept
	{
		::filter_for_each(*this, pred, std::forward<F>(f));
	}

	template <typename Pred> requires std::predicate<Pred, const T&>
	void filter_indices_to(dense<size_t>& dst, Pred pred) noexcept
	{
		dst.increase_capacity(dst.size() + index_);
		for (size_t i = 0; i < index_; ++i)
		{
			if (pred(data_ptr_[i])) [[likely]]
			{
				dst.push_back_unchecked(i);
			}
		}
	}

	template <typename F, typename U = T>
	[[nodiscard]] U reduce(F&& f, U init) const noexcept
	{
		return ::reduce(*this, std::forward<F>(f), std::move(init));
	}

	template <typename F, typename U = T>
	[[nodiscard]] U reduce_pairwise(F&& f, U init) const noexcept
	{
		return ::reduce_pairwise(*this, std::forward<F>(f), std::move(init));
	}

	[[nodiscard]] T* min_element() noexcept
	{
		return ::min_element(*this);
	}
	[[nodiscard]] const T* min_element() const noexcept
	{
		return ::min_element(*this);
	}
	[[nodiscard]] T* max_element() noexcept
	{
		return ::max_element(*this);
	}
	[[nodiscard]] const T* max_element() const noexcept
	{
		return ::max_element(*this);
	}
	[[nodiscard]] std::pair<T*, T*> minmax_element() noexcept
	{
		return ::minmax_element(*this);
	}
	[[nodiscard]] std::pair<const T*, const T*> minmax_element() const noexcept
	{
		return ::minmax_element(*this);
	}

	template <typename U = T> requires std::is_arithmetic_v<U>
	[[nodiscard]] U sum() const noexcept
	{
		return ::sum(*this);
	}

	template <typename U = T> requires std::is_arithmetic_v<U>
	[[nodiscard]] U dot_product(const U* other, size_t count) const noexcept
	{
		return ::dot_product(*this, other, count);
	}

	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<T, N>>
	void for_each_window(F&& f) noexcept
	{
		::for_each_window<N>(*this, std::forward<F>(f));
	}
	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<const T, N>>
	void for_each_window(F&& f) const noexcept
	{
		::for_each_window<N>(*this, std::forward<F>(f));
	}

	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<T, N>>
	void for_each_chunk(F&& f) noexcept
	{
		::for_each_chunk<N>(*this, std::forward<F>(f));
	}
	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<const T, N>>
	void for_each_chunk(F&& f) const noexcept
	{
		::for_each_chunk<N>(*this, std::forward<F>(f));
	}

	template <size_t N>
	requires (N > 0)
	[[nodiscard]] constexpr std::span<T, N> window_span(size_t offset) noexcept
	{
		return ::window_span<N>(*this, offset);
	}
	template <size_t N>
	requires (N > 0)
	[[nodiscard]] constexpr std::span<const T, N> window_span(size_t offset) const noexcept
	{
		return ::window_span<N>(*this, offset);
	}
	template <size_t N>
	requires (N > 0)
	[[nodiscard]] constexpr std::span<T, N> chunk_span(size_t chunk_idx) noexcept
	{
		return ::chunk_span<N>(*this, chunk_idx);
	}
	template <size_t N>
	requires (N > 0)
	[[nodiscard]] constexpr std::span<const T, N> chunk_span(size_t chunk_idx) const noexcept
	{
		return ::chunk_span<N>(*this, chunk_idx);
	}

	template <typename F> requires std::invocable<F, size_t, T&>
	void for_each_enumerated(F&& f) noexcept
	{
		::for_each_enumerated(*this, std::forward<F>(f));
	}
	template <typename F> requires std::invocable<F, size_t, const T&>
	void for_each_enumerated(F&& f) const noexcept
	{
		::for_each_enumerated(*this, std::forward<F>(f));
	}

	template <typename U, typename F>
	requires std::invocable<F, T&, U&>
	void for_each_zip(U* other, size_t count, F&& f) noexcept
	{
		::for_each_zip(*this, other, count, std::forward<F>(f));
	}
	template <typename U, typename F>
	requires std::invocable<F, const T&, const U&>
	void for_each_zip(const U* other, size_t count, F&& f) const noexcept
	{
		::for_each_zip(*this, other, count, std::forward<F>(f));
	}
	template <typename U, typename F>
	requires std::invocable<F, T&, U&>
	void for_each_zip(dense<U>& other, F&& f) noexcept
	{
		::for_each_zip(*this, other, std::forward<F>(f));
	}
	template <typename U, typename F>
	requires std::invocable<F, const T&, const U&>
	void for_each_zip(const dense<U>& other, F&& f) const noexcept
	{
		::for_each_zip(*this, other, std::forward<F>(f));
	}
	template <typename U, typename F>
	requires std::invocable<F, T&, U&>
	void for_each_zip(std::span<U> other, F&& f) noexcept
	{
		::for_each_zip(*this, other.data(), other.size(), std::forward<F>(f));
	}
	template <typename U, typename F>
	requires std::invocable<F, const T&, const U&>
	void for_each_zip(std::span<const U> other, F&& f) const noexcept
	{
		::for_each_zip(*this, other.data(), other.size(), std::forward<F>(f));
	}

	// 双容器变换写入: 将 f(x, y) 写入 dst
	template <typename U, typename R, typename F>
	requires std::invocable<F, const T&, const U&>
	void zip_with_to(R* dst, const U* other, size_t count, F&& f) const noexcept
	{
		::zip_with_to(*this, other, dst, count, std::forward<F>(f));
	}

	[[nodiscard]] bool equal(const T* other, size_t count) const noexcept
	{
		return ::equal(*this, other, count);
	}
	template <typename U>
	[[nodiscard]] bool equal(const dense<U>& other) const noexcept
	{
		return ::equal(*this, other);
	}
	template <typename U>
	[[nodiscard]] bool equal(std::span<const U> other) const noexcept
	{
		return ::equal(*this, other);
	}

	[[nodiscard]] constexpr T* aligned_data() noexcept
	{
		return ::aligned_data(*this);
	}
	[[nodiscard]] constexpr const T* aligned_data() const noexcept
	{
		return ::aligned_data(*this);
	}

	[[nodiscard]] constexpr std::span<T> aligned_span() noexcept
	{
		return ::aligned_span(*this);
	}
	[[nodiscard]] constexpr std::span<const T> aligned_span() const noexcept
	{
		return ::aligned_span(*this);
	}

	template <typename F> requires std::is_trivially_copyable_v<T> && std::invocable<F, T&>
	void simd_for_each(F&& f) noexcept
	{
		::simd_for_each(*this, std::forward<F>(f));
	}
	template <typename F> requires std::is_trivially_copyable_v<T> && std::invocable<F, const T&>
	void simd_for_each(F&& f) const noexcept
	{
		::simd_for_each(*this, std::forward<F>(f));
	}

	[[nodiscard]] constexpr size_t unaligned_tail_offset() const noexcept
	{
		return ::unaligned_tail_offset(*this);
	}

	void copy_to(T* dst, size_t count) noexcept
	{
		::copy_to(*this, dst, count);
	}
	void copy_to(std::span<T> dst) noexcept
	{
		::copy_to(*this, dst);
	}

	void move_to(T* dst, size_t count) noexcept
	{
		::move_to(*this, dst, count);
	}
	void move_to(std::span<T> dst) noexcept
	{
		::move_to(*this, dst);
	}

	void reverse_copy_to(T* dst, size_t count) noexcept
	{
		::reverse_copy_to(*this, dst, count);
	}
	void reverse_copy_to(std::span<T> dst) noexcept
	{
		::reverse_copy_to(*this, dst);
	}

	template <typename R, typename F>
	requires std::invocable<F, const T&>
	void transform_to(R* dst, size_t count, F&& transform) const noexcept
	{
		::transform_to(*this, dst, count, std::forward<F>(transform));
	}
};

template <typename T>
void swap(dense<T>& a, dense<T>& b) noexcept {
	a.swap(b);
}
