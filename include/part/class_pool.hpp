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
#if defined(__AVX2__) || defined(__BMI__) || (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
#include <immintrin.h>
#endif


#include "force_inline.hpp"

// 跨平台预取/属性宏: 集中定义于 force_inline.hpp (PREFETCH_R/PREFETCH_NTA/NOINLINE/FORCE_INLINE 等)

namespace ecs { class single_class_set; }

// 前置声明: pool_strided_span 定义于 class_pool_views.hpp
// 用于 strided_span_view 成员函数返回值声明
template <typename T>
struct pool_strided_span;

// 清除最低设置位 (BMI1 BLSR 指令, 不可用时回退到标量)
[[nodiscard]] static inline uint64_t clear_lowest_bit(uint64_t x) noexcept
{
#if defined(__BMI__) || (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
    return _blsr_u64(x);
#else
    return x & (x - 1);
#endif
}

template <typename T>
class class_pool
{
	friend class ecs::single_class_set;

private:
	static constexpr size_t BITS_PER_WORD = 64;
	static constexpr size_t INLINE_CAPACITY = 64;

	T* data_ptr_{nullptr};
	uint64_t* sparse_bits_{nullptr};
	uint64_t* live_bits_{nullptr};
	size_t maximum_quantity_{0};
	size_t index_{0};
	uint8_t is_dense_{1};
	mutable size_t count_cache_{static_cast<size_t>(-1)};
	size_t hole_count_{0};
	size_t first_hole_hint_{0};
	uint64_t inline_bits_{0};
	uint64_t inline_live_bits_{0};

	static constexpr size_t DEFAULT_CAPACITY = 64;
	static constexpr size_t SMALL_CAPACITY_THRESHOLD = 1024;
	static constexpr size_t MEDIUM_CAPACITY_THRESHOLD = 65536;

	// 返回真实容量: 虚标容量而不分配会让追加解引用空指针
	[[nodiscard]] static constexpr size_t round_up_to_default(size_t n) noexcept {
		return n;
	}

	[[nodiscard]] static constexpr size_t calculate_new_capacity(size_t current) noexcept {
		if (current == 0) { return DEFAULT_CAPACITY; }
		return current * 4;
	}

	[[nodiscard]] static constexpr size_t calculate_growth_for_reserve(size_t required) noexcept {
		if (required <= DEFAULT_CAPACITY) { return DEFAULT_CAPACITY; }
		size_t capacity = DEFAULT_CAPACITY;
		while (capacity < required) {
			capacity = calculate_new_capacity(capacity);
		}
		return capacity;
	}

	[[nodiscard]] static constexpr size_t bitmap_word_count(size_t num_bits) noexcept {
		return (num_bits + BITS_PER_WORD - 1) / BITS_PER_WORD;
	}

	[[nodiscard]] bool is_inline_bitmap() const noexcept {
		return maximum_quantity_ > 0 && maximum_quantity_ <= INLINE_CAPACITY;
	}

	static constexpr size_t bitmap_align = 32;

	[[nodiscard]] static uint64_t* allocate_bitmap(size_t num_bits) noexcept {
		if (num_bits == 0) { return nullptr; }
		const size_t words = bitmap_word_count(num_bits);
		const size_t bytes = words * sizeof(uint64_t);
		uint64_t* ptr = static_cast<uint64_t*>(
			::operator new(bytes, std::align_val_t{bitmap_align}, std::nothrow));
		if (ptr == nullptr) [[unlikely]] {
			std::abort();
		}
		std::memset(std::assume_aligned<bitmap_align>(ptr), 0, bytes);
		return ptr;
	}

	static void deallocate_bitmap(uint64_t* ptr, size_t num_bits) noexcept {
		if (ptr != nullptr) {
			::operator delete(ptr, bitmap_word_count(num_bits) * sizeof(uint64_t),
			                  std::align_val_t{bitmap_align});
		}
	}

	void release_bitmap() noexcept {
		if (sparse_bits_ == nullptr) { return; }
		if (!is_inline_bitmap()) {
			deallocate_bitmap(sparse_bits_, maximum_quantity_);
			deallocate_bitmap(live_bits_, maximum_quantity_);
		}
		sparse_bits_ = nullptr;
		live_bits_ = nullptr;
	}

	uint64_t* obtain_bitmap(size_t capacity) noexcept {
		if (capacity <= INLINE_CAPACITY) {
			inline_bits_ = 0;
			inline_live_bits_ = 0;
			return &inline_bits_;
		}
		return allocate_bitmap(capacity);
	}

	uint64_t* obtain_live_bitmap(size_t capacity) noexcept {
		if (capacity <= INLINE_CAPACITY) {
			return &inline_live_bits_;
		}
		return allocate_bitmap(capacity);
	}

	static void copy_bitmap(uint64_t* dst, const uint64_t* src, size_t num_bits) noexcept {
		if (num_bits == 0) { return; }
		const size_t words = bitmap_word_count(num_bits);
		const size_t bytes = words * sizeof(uint64_t);
#ifdef __AVX2__
		if (bytes >= 2048)
		{
			const __m256i* s = static_cast<const __m256i*>(static_cast<const void*>(src));
			__m256i* d = static_cast<__m256i*>(static_cast<void*>(dst));
			const size_t ymm_count = words / 4;
			size_t i = 0;
			for (; i + 4 <= ymm_count; i += 4)
			{
				__m256i v0 = _mm256_loadu_si256(s + i);
				__m256i v1 = _mm256_loadu_si256(s + i + 1);
				__m256i v2 = _mm256_loadu_si256(s + i + 2);
				__m256i v3 = _mm256_loadu_si256(s + i + 3);
				_mm256_storeu_si256(d + i, v0);
				_mm256_storeu_si256(d + i + 1, v1);
				_mm256_storeu_si256(d + i + 2, v2);
				_mm256_storeu_si256(d + i + 3, v3);
			}
			for (; i < ymm_count; ++i)
			{
				_mm256_storeu_si256(d + i, _mm256_loadu_si256(s + i));
			}
			const size_t processed = ymm_count * 4;
			for (size_t w = processed; w < words; ++w)
			{
				dst[w] = src[w];
			}
			return;
		}
#endif
		std::memcpy(std::assume_aligned<bitmap_align>(dst),
		            std::assume_aligned<bitmap_align>(src),
		            bytes);
	}

	static constexpr size_t cache_line = 64;
	[[nodiscard]] static T* allocate_data(size_t count) noexcept {
		if (count == 0) { return nullptr; }
		constexpr size_t align = alignof(T) > cache_line ? alignof(T) : cache_line;
		T* ptr = static_cast<T*>(
			::operator new(count * sizeof(T), std::align_val_t{align}, std::nothrow));
		if (ptr == nullptr) [[unlikely]] {
			std::abort();
		}
		return std::assume_aligned<align>(ptr);
	}

	static void deallocate_data(T* ptr, size_t count) noexcept {
		if (ptr != nullptr) {
			constexpr size_t align = alignof(T) > cache_line ? alignof(T) : cache_line;
			::operator delete(ptr, count * sizeof(T), std::align_val_t{align});
		}
	}

	[[nodiscard]] static constexpr bool bitmap_test(const uint64_t* bits, size_t index) noexcept {
		return (bits[index / BITS_PER_WORD] >> (index % BITS_PER_WORD)) & 1ull;
	}

	static constexpr void bitmap_set(uint64_t* bits, size_t index) noexcept {
		bits[index / BITS_PER_WORD] |= (1ull << (index % BITS_PER_WORD));
	}

	static constexpr void bitmap_reset(uint64_t* bits, size_t index) noexcept {
		bits[index / BITS_PER_WORD] &= ~(1ull << (index % BITS_PER_WORD));
	}

	static void bitmap_shift_right_one(uint64_t* bits, size_t start, size_t end) noexcept;
	static void bitmap_zero_words(uint64_t* bits, size_t start, size_t end) noexcept;
	static void bitmap_shift_left(uint64_t* bits, size_t start, size_t end, size_t shift) noexcept;
	void destroy_dense_range(size_t first, size_t last) noexcept;
	void destroy_sparse_range(size_t first, size_t last) noexcept;
	void destroy_all() noexcept;
	void uninitialized_move_dense(T* src_first, T* src_last, T* dst) noexcept;

	template <bool MoveAndDestroy>
	static void relocate_sparse(T* dst, const T* src, const uint64_t* src_bits, size_t count) noexcept;

	static void copy_trivial_data(T* __restrict dst, const T* __restrict src, size_t count) noexcept;
	void grow_data_and_bitmap(size_t new_capacity) noexcept;

public:
	using value_type = T;
	using pointer = T*;
	using const_pointer = const T*;
	using reference = T&;
	using const_reference = const T&;
	using size_type = size_t;
	using difference_type = std::ptrdiff_t;

	template <bool IsConst>
	class basic_iterator
	{
		using Ptr = std::conditional_t<IsConst, const T*, T*>;
		using Ref = std::conditional_t<IsConst, const T&, T&>;

		// __restrict: MSVC 不支持类型别名后使用, 仅 GCC/Clang 启用
#if defined(_MSC_VER)
		Ptr ptr_;
		Ptr end_;
		const uint64_t* bits_;
		Ptr origin_;
#else
		Ptr __restrict ptr_;
		Ptr __restrict end_;
		const uint64_t* __restrict bits_;
		Ptr __restrict origin_;
#endif

		friend class basic_iterator<true>;
		friend class basic_iterator<false>;
		friend class class_pool;

	public:
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = Ptr;
		using reference = Ref;

		basic_iterator() noexcept : ptr_(nullptr), end_(nullptr), bits_(nullptr), origin_(nullptr) {}

		// 规避 MinGW AVX2 codegen bug: 标量拷贝构造
#if defined(__MINGW32__) || defined(__MINGW64__)
		basic_iterator(const basic_iterator& other) noexcept
			: ptr_(other.ptr_), end_(other.end_), bits_(other.bits_), origin_(other.origin_) {}

		basic_iterator& operator=(const basic_iterator& other) noexcept
		{
			ptr_ = other.ptr_;
			end_ = other.end_;
			bits_ = other.bits_;
			origin_ = other.origin_;
			return *this;
		}
#else
		basic_iterator(const basic_iterator&) noexcept = default;
		basic_iterator& operator=(const basic_iterator&) noexcept = default;
#endif

		basic_iterator(Ptr ptr, Ptr end, const uint64_t* bits, Ptr origin) noexcept
			: ptr_(ptr), end_(end), bits_(bits), origin_(origin) {
			if (bits_ != nullptr && ptr_ != end_) {
				skip_to_next_valid();
			}
		}

		basic_iterator(const basic_iterator<false>& other) noexcept
			requires (IsConst)
			: ptr_(other.ptr_), end_(other.end_), bits_(other.bits_), origin_(other.origin_) {}

		Ref operator*() const noexcept { return *ptr_; }
		Ptr operator->() const noexcept { return ptr_; }

		// 返回 >= start 的下一个设置位位置, 没有则返回 total
		// 属性 pure/noinline 由 LCF_PURE/NOINLINE 宏跨平台处理 (见 force_inline.hpp)
		LCF_PURE static NOINLINE size_t find_next_set_bit(
			const uint64_t* bits, size_t start, size_t total) noexcept
		{
			if (start >= total) { return total; }

			size_t word_idx = start / BITS_PER_WORD;
			size_t bit_in_word = start % BITS_PER_WORD;
			const size_t total_words = (total + BITS_PER_WORD - 1) / BITS_PER_WORD;

			uint64_t word = bits[word_idx] >> bit_in_word;
			if (word != 0)
			{
				return word_idx * BITS_PER_WORD + bit_in_word + std::countr_zero(word);
			}

			++word_idx;
#ifdef __AVX2__
			for (; word_idx + 4 <= total_words; word_idx += 4)
			{
				__m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bits + word_idx));
				if (!_mm256_testz_si256(v, v))
				{
					for (size_t j = 0; j < 4; ++j)
					{
						uint64_t w = bits[word_idx + j];
						if (w != 0)
						{
							return (word_idx + j) * BITS_PER_WORD + std::countr_zero(w);
						}
					}
				}
			}
#endif
			for (; word_idx < total_words; ++word_idx)
			{
				word = bits[word_idx];
				if (word != 0)
				{
					return word_idx * BITS_PER_WORD + std::countr_zero(word);
				}
			}
			return total;
		}

		void skip_to_next_valid() noexcept {
			const size_t idx = static_cast<size_t>(ptr_ - origin_);
			const size_t total = static_cast<size_t>(end_ - origin_);
			ptr_ = origin_ + find_next_set_bit(bits_, idx, total);
		}

		// 强制内联: 由 FORCE_INLINE 宏跨平台处理 (见 force_inline.hpp)
		FORCE_INLINE basic_iterator& operator++() noexcept {
			const uint64_t* const bits = bits_;
			if (bits == nullptr) [[likely]]
			{
				++ptr_;
				return *this;
			}
			++ptr_;
			if (ptr_ != end_)
			{
				constexpr size_t pf_dist = (sizeof(T) < 64) ? (64 / sizeof(T)) : 1;
				PREFETCH_R(ptr_ + pf_dist);
				const size_t idx = static_cast<size_t>(ptr_ - origin_);
				const size_t total = static_cast<size_t>(end_ - origin_);
				ptr_ = origin_ + find_next_set_bit(bits, idx, total);
			}
			return *this;
		}

		basic_iterator operator++(int) noexcept {
			basic_iterator tmp = *this;
			++*this;
			return tmp;
		}

		void skip_to_prev_valid() noexcept {
			if (ptr_ < origin_) { return; }
			const size_t idx = static_cast<size_t>(ptr_ - origin_);
			const size_t total = static_cast<size_t>(end_ - origin_);
			if (idx >= total) { ptr_ = origin_ - 1; return; }

			size_t word_idx = idx / BITS_PER_WORD;
			size_t bit_in_word = idx % BITS_PER_WORD;

			uint64_t mask = (bit_in_word == 63) ? ~0ull : ((1ull << (bit_in_word + 1)) - 1);
			uint64_t word = bits_[word_idx] & mask;
			if (word != 0) {
				ptr_ = origin_ + word_idx * BITS_PER_WORD + (63 - std::countl_zero(word));
				return;
			}

			while (word_idx > 0) {
				--word_idx;
				word = bits_[word_idx];
				if (word != 0) {
					ptr_ = origin_ + word_idx * BITS_PER_WORD + (63 - std::countl_zero(word));
					return;
				}
			}
			ptr_ = origin_ - 1;
		}

		basic_iterator& operator--() noexcept {
			if (bits_ != nullptr) {
				--ptr_;
				skip_to_prev_valid();
			}
			else {
				--ptr_;
			}
			return *this;
		}

		basic_iterator operator--(int) noexcept {
			basic_iterator tmp = *this;
			--*this;
			return tmp;
		}

		bool operator==(const basic_iterator& other) const noexcept { return ptr_ == other.ptr_; }
		bool operator!=(const basic_iterator& other) const noexcept { return ptr_ != other.ptr_; }
	};

	using iterator = basic_iterator<false>;
	using const_iterator = basic_iterator<true>;

	constexpr class_pool() noexcept = default;

	explicit class_pool(size_t capacity) noexcept;
	class_pool(size_t count, const T& value) noexcept;
	// 迭代器约束: 防止 pool(count, value) 的整型参数被误推导为迭代器
	template <typename InputIt> requires std::input_iterator<InputIt>
	class_pool(InputIt first, InputIt last) noexcept;
	class_pool(std::initializer_list<T> init) noexcept;
	class_pool(const class_pool& other) noexcept;
	class_pool(class_pool&& other) noexcept;
	class_pool& operator=(const class_pool& other) noexcept;
	class_pool& operator=(class_pool&& other) noexcept;
	~class_pool() noexcept;

	template <typename... Args>
	void emplace_back(Args&&... args) noexcept;
	void push_back(const T& value) noexcept;
	void push_back(T&& value) noexcept;
	void push_back_unchecked(const T& value) noexcept;
	void push_back_unchecked(T&& value) noexcept;
	template <typename... Args>
	void emplace_back_unchecked(Args&&... args) noexcept;
	template <typename... Args>
	void emplace_back_dense_unchecked(Args&&... args) noexcept;
	void append_n(size_t n, const T& value) noexcept;
	void clear() noexcept;

	// 下标访问 (等价 operator[], 无边界检查)
	[[nodiscard]] constexpr T& get(size_t index) noexcept
	{
		return data_ptr_[index];
	}

	[[nodiscard]] constexpr const T& get(size_t index) const noexcept
	{
		return data_ptr_[index];
	}

	// 越界保护访问: index 越界时改访问 error_index
	[[nodiscard]] constexpr T& get(size_t index, size_t error_index) noexcept
	{
		return data_ptr_[index < index_ ? index : error_index];
	}

	[[nodiscard]] constexpr const T& get(size_t index, size_t error_index) const noexcept
	{
		return data_ptr_[index < index_ ? index : error_index];
	}

	[[nodiscard]] constexpr size_type capacity() const noexcept { return maximum_quantity_; }
	[[nodiscard]] constexpr size_type sparse_capacity() const noexcept { return maximum_quantity_; }
	[[nodiscard]] constexpr size_type size() const noexcept { return index_; }
	[[nodiscard]] constexpr bool empty() const noexcept { return index_ == 0; }

	[[nodiscard]] size_type count() const noexcept;

	void invalidate_count_cache() noexcept {
		count_cache_ = static_cast<size_t>(-1);
	}

	[[nodiscard]] constexpr pointer data() noexcept {
		return data_ptr_;
	}

	[[nodiscard]] constexpr const_pointer data() const noexcept {
		return data_ptr_;
	}

	void increase_capacity(size_t new_capacity) noexcept;
	void increase_capacity(size_t new_capacity, const T& value) noexcept;
	void shrink_to_fit() noexcept;
	void reduce_capacity(size_t new_capacity) noexcept;
	void reduce_capacity(size_t new_capacity, class_pool<T>& dst) noexcept;

	[[nodiscard]] constexpr T& front() noexcept {
		return data_ptr_[0];
	}

	[[nodiscard]] constexpr const T& front() const noexcept {
		return data_ptr_[0];
	}

	[[nodiscard]] constexpr T& back() noexcept {
		return data_ptr_[index_ - 1];
	}

	[[nodiscard]] constexpr const T& back() const noexcept {
		return data_ptr_[index_ - 1];
	}
	
	void reserve_exact(size_t new_capacity) noexcept;

	template <typename... Args>
	iterator emplace(const_iterator pos, Args&&... args) noexcept;
	iterator insert(const_iterator pos, const T& value) noexcept;
	iterator insert(const_iterator pos, T&& value) noexcept;
	iterator erase(const_iterator pos) noexcept;
	iterator erase(const_iterator first, const_iterator last) noexcept;
	void swap(class_pool& other) noexcept;
	void pop_back() noexcept;

	[[nodiscard]] constexpr bool valid() const noexcept { return data_ptr_ != nullptr; }
	[[nodiscard]] constexpr size_type size_bytes() const noexcept { return index_ * sizeof(T); }
	[[nodiscard]] constexpr size_type capacity_bytes() const noexcept { return maximum_quantity_ * sizeof(T); }

	[[nodiscard]] constexpr std::span<T> span() noexcept { return std::span<T>(data_ptr_, index_); }
	[[nodiscard]] constexpr std::span<const T> span() const noexcept { return std::span<const T>(data_ptr_, index_); }

	[[nodiscard]] constexpr T& operator[](size_t index) noexcept {
		return data_ptr_[index];
	}

	[[nodiscard]] constexpr const T& operator[](size_t index) const noexcept {
		return data_ptr_[index];
	}

	iterator begin() noexcept {
		return iterator(data_ptr_, data_ptr_ + index_,
		                is_dense_ ? nullptr : sparse_bits_, data_ptr_);
	}

	iterator end() noexcept {
		return iterator(data_ptr_ + index_, data_ptr_ + index_,
		                is_dense_ ? nullptr : sparse_bits_, data_ptr_);
	}

	const_iterator begin() const noexcept {
		return const_iterator(data_ptr_, data_ptr_ + index_,
		                      is_dense_ ? nullptr : sparse_bits_, data_ptr_);
	}

	const_iterator end() const noexcept {
		return const_iterator(data_ptr_ + index_, data_ptr_ + index_,
		                      is_dense_ ? nullptr : sparse_bits_, data_ptr_);
	}

	const_iterator cbegin() const noexcept {
		return const_iterator(data_ptr_, data_ptr_ + index_,
		                      is_dense_ ? nullptr : sparse_bits_, data_ptr_);
	}

	const_iterator cend() const noexcept {
		return const_iterator(data_ptr_ + index_, data_ptr_ + index_,
		                      is_dense_ ? nullptr : sparse_bits_, data_ptr_);
	}

	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
	reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
	const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
	const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
	const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
	const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

	template <typename... Args>
	T& emplace_at(size_t index, Args&&... args) noexcept;
	template <typename... Args>
	T& sparse_emplace_at(size_t index, Args&&... args) noexcept;
	void sparse_erase_at(size_t index) noexcept;
	void soft_sparse_delete(size_t index) noexcept;
	void soft_dense_delete(size_t start, size_t end) noexcept;

	[[nodiscard]] constexpr bool is_constructed_at(size_t index) const noexcept {
		if (index >= maximum_quantity_) { return false; }
		return bitmap_test(sparse_bits_, index);
	}

	[[nodiscard]] bool is_dense() const noexcept {
		return is_dense_;
	}

	// 填洞或追加: 有空洞填第一个洞, 无空洞末尾追加
	template <typename... Args>
	T& fill_the_hole(Args&&... args) noexcept;

	// 与 fill_the_hole 语义等价, 但返回被填补位置的索引 (填洞或追加)
	template <typename... Args>
	size_t fill_the_hole_at(Args&&... args) noexcept;

	// 批量追加接口 (与 dense 对齐)
	void append_bulk(const T* src, size_t count) noexcept;
	void append_bulk_move(T* src, size_t count) noexcept;
	void append_incrementing(size_t count, uint64_t& counter) noexcept;

	template <typename F>
	void append_generated(size_t count, F&& generator) noexcept;

	[[nodiscard]] constexpr size_type max_size() const noexcept { return static_cast<size_t>(-1) / sizeof(T); }

	void fill_bulk(const T& value, size_t start, size_t count) noexcept;
	void prepare_dense(size_t new_size) noexcept;

	// 视图成员函数: 转发至 container_views.hpp 全局函数 (与 dense 接口对齐)

	template <typename F>
	void for_each(F&& f) noexcept
	{
		for (auto it = begin(); it != end(); ++it)
		{
			f(*it);
		}
	}
	template <typename F>
	void for_each(F&& f) const noexcept
	{
		for (auto it = cbegin(); it != cend(); ++it)
		{
			f(*it);
		}
	}

	template <typename F> void reverse_for_each(F&& f) noexcept;
	template <typename F> void reverse_for_each(F&& f) const noexcept;

	[[nodiscard]] std::span<T> subspan(size_t offset, size_t count) noexcept;
	[[nodiscard]] std::span<const T> subspan(size_t offset, size_t count) const noexcept;
	[[nodiscard]] std::span<T> subspan(size_t offset) noexcept;
	[[nodiscard]] std::span<const T> subspan(size_t offset) const noexcept;
	[[nodiscard]] std::span<T> first(size_t n) noexcept;
	[[nodiscard]] std::span<const T> first(size_t n) const noexcept;
	[[nodiscard]] std::span<T> last(size_t n) noexcept;
	[[nodiscard]] std::span<const T> last(size_t n) const noexcept;
	template <size_t N> [[nodiscard]] std::span<T, N> first_fixed() noexcept;
	template <size_t N> [[nodiscard]] std::span<const T, N> first_fixed() const noexcept;
	template <size_t N> [[nodiscard]] std::span<T, N> last_fixed() noexcept;
	template <size_t N> [[nodiscard]] std::span<const T, N> last_fixed() const noexcept;

	[[nodiscard]] pool_strided_span<T> strided_span_view(size_t start, size_t step, size_t count) noexcept;
	template <typename F> void strided_for_each(size_t start, size_t step, F&& f) noexcept;
	template <typename F> void strided_for_each(size_t start, size_t step, F&& f) const noexcept;
	template <size_t Step, typename F> void strided_for_each(F&& f) noexcept;
	template <size_t Step, typename F> void strided_for_each(F&& f) const noexcept;

	template <typename FTransform, typename FConsume>
	void transform_for_each(FTransform&& tr, FConsume&& con) noexcept;
	template <typename FTransform, typename FConsume>
	void transform_for_each(FTransform&& tr, FConsume&& con) const noexcept;
	template <typename R, typename F>
	void transform_to(R* dst, size_t count, F&& tr) const noexcept;

	[[nodiscard]] T* find(const T& value) noexcept;
	[[nodiscard]] const T* find(const T& value) const noexcept;
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] T* find_if(Pred pred) noexcept;
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] const T* find_if(Pred pred) const noexcept;
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] T* find_if_not(Pred pred) noexcept;
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] const T* find_if_not(Pred pred) const noexcept;
	[[nodiscard]] bool contains(const T& value) const noexcept;
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] size_t count_if(Pred pred) const noexcept;
	template <typename Pred, typename F>
	requires std::predicate<Pred, const T&> && std::invocable<F, T&>
	void filter_for_each(Pred pred, F&& f) noexcept;
	template <typename Pred, typename F>
	requires std::predicate<Pred, const T&> && std::invocable<F, const T&>
	void filter_for_each(Pred pred, F&& f) const noexcept;
	template <typename Pred> requires std::predicate<Pred, const T&>
	void filter_indices_to(class_pool<size_t>& dst, Pred pred) noexcept;

	template <typename F, typename U = T>
	[[nodiscard]] U reduce(F&& f, U init) const noexcept;
	template <typename F, typename U = T>
	[[nodiscard]] U reduce_pairwise(F&& f, U init) const noexcept;
	[[nodiscard]] T* min_element() noexcept;
	[[nodiscard]] const T* min_element() const noexcept;
	[[nodiscard]] T* max_element() noexcept;
	[[nodiscard]] const T* max_element() const noexcept;
	[[nodiscard]] std::pair<T*, T*> minmax_element() noexcept;
	[[nodiscard]] std::pair<const T*, const T*> minmax_element() const noexcept;
	template <typename U = T> requires std::is_arithmetic_v<U>
	[[nodiscard]] U sum() const noexcept;
	template <typename U = T> requires std::is_arithmetic_v<U>
	[[nodiscard]] U dot_product(const U* other, size_t count) const noexcept;

	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<T, N>>
	void for_each_window(F&& f) noexcept;
	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<const T, N>>
	void for_each_window(F&& f) const noexcept;
	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<T, N>>
	void for_each_chunk(F&& f) noexcept;
	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<const T, N>>
	void for_each_chunk(F&& f) const noexcept;
	template <size_t N> requires (N > 0)
	[[nodiscard]] std::span<T, N> window_span(size_t offset) noexcept;
	template <size_t N> requires (N > 0)
	[[nodiscard]] std::span<const T, N> window_span(size_t offset) const noexcept;
	template <size_t N> requires (N > 0)
	[[nodiscard]] std::span<T, N> chunk_span(size_t chunk_idx) noexcept;
	template <size_t N> requires (N > 0)
	[[nodiscard]] std::span<const T, N> chunk_span(size_t chunk_idx) const noexcept;

	template <typename F> requires std::invocable<F, size_t, T&>
	void for_each_enumerated(F&& f) noexcept;
	template <typename F> requires std::invocable<F, size_t, const T&>
	void for_each_enumerated(F&& f) const noexcept;

	template <typename U, typename F>
	requires std::invocable<F, T&, U&>
	void for_each_zip(class_pool<U>& other, F&& f) noexcept;
	template <typename U, typename F>
	requires std::invocable<F, const T&, const U&>
	void for_each_zip(const class_pool<U>& other, F&& f) const noexcept;
	template <typename U, typename F>
	requires std::invocable<F, T&, U&>
	void for_each_zip(U* other, size_t count, F&& f) noexcept;
	template <typename U, typename F>
	requires std::invocable<F, const T&, const U&>
	void for_each_zip(const U* other, size_t count, F&& f) const noexcept;
	template <typename U, typename F>
	requires std::invocable<F, T&, U&>
	void for_each_zip(std::span<U> other, F&& f) noexcept;
	template <typename U, typename F>
	requires std::invocable<F, const T&, const U&>
	void for_each_zip(std::span<const U> other, F&& f) const noexcept;
	template <typename U, typename R, typename F>
	requires std::invocable<F, const T&, const U&>
	void zip_with_to(R* dst, const U* other, size_t count, F&& f) const noexcept;

	[[nodiscard]] bool equal(const T* other, size_t count) const noexcept;
	template <typename U>
	[[nodiscard]] bool equal(const class_pool<U>& other) const noexcept;
	template <typename U>
	[[nodiscard]] bool equal(std::span<const U> other) const noexcept;

	[[nodiscard]] T* aligned_data() noexcept;
	[[nodiscard]] const T* aligned_data() const noexcept;
	[[nodiscard]] std::span<T> aligned_span() noexcept;
	[[nodiscard]] std::span<const T> aligned_span() const noexcept;
	template <typename F> requires std::is_trivially_copyable_v<T> && std::invocable<F, T&>
	void simd_for_each(F&& f) noexcept;
	template <typename F> requires std::is_trivially_copyable_v<T> && std::invocable<F, const T&>
	void simd_for_each(F&& f) const noexcept;
	[[nodiscard]] size_t unaligned_tail_offset() const noexcept;

	void copy_to(T* dst, size_t count) const noexcept;
	void copy_to(std::span<T> dst) const noexcept;
	void move_to(T* dst, size_t count) noexcept;
	void move_to(std::span<T> dst) noexcept;
	void reverse_copy_to(T* dst, size_t count) const noexcept;
	void reverse_copy_to(std::span<T> dst) const noexcept;

	// class_pool 独有接口
	[[nodiscard]] size_t compact_to(T* dst, size_t count) const noexcept;
	[[nodiscard]] size_t live_count() const noexcept;
	[[nodiscard]] size_t holes_count() const noexcept;

private:
	[[nodiscard]] size_t find_first_hole_() noexcept;
	void recompute_is_dense() noexcept;
	void bulk_set_bits(size_t start, size_t end) noexcept;

	template <typename EntityLike>
	void append_indices_from(const EntityLike* entities, size_t count) noexcept;

	void update_dense_status() noexcept;
};

template <typename T>
void swap(class_pool<T>& a, class_pool<T>& b) noexcept;

#include "class_pool_impl.hpp"
#include "class_pool_views.hpp"

// 视图成员函数定义: 转发至 container_views.hpp 全局函数

template <typename T>
template <typename F>
inline void class_pool<T>::reverse_for_each(F&& f) noexcept
{
	::reverse_for_each(*this, std::forward<F>(f));
}
template <typename T>
template <typename F>
inline void class_pool<T>::reverse_for_each(F&& f) const noexcept
{
	::reverse_for_each(*this, std::forward<F>(f));
}

template <typename T>
inline std::span<T> class_pool<T>::subspan(size_t offset, size_t count) noexcept
{
	return ::subspan(*this, offset, count);
}
template <typename T>
inline std::span<const T> class_pool<T>::subspan(size_t offset, size_t count) const noexcept
{
	return ::subspan(*this, offset, count);
}
template <typename T>
inline std::span<T> class_pool<T>::subspan(size_t offset) noexcept
{
	return ::subspan(*this, offset);
}
template <typename T>
inline std::span<const T> class_pool<T>::subspan(size_t offset) const noexcept
{
	return ::subspan(*this, offset);
}
template <typename T>
inline std::span<T> class_pool<T>::first(size_t n) noexcept
{
	return ::first(*this, n);
}
template <typename T>
inline std::span<const T> class_pool<T>::first(size_t n) const noexcept
{
	return ::first(*this, n);
}
template <typename T>
inline std::span<T> class_pool<T>::last(size_t n) noexcept
{
	return ::last(*this, n);
}
template <typename T>
inline std::span<const T> class_pool<T>::last(size_t n) const noexcept
{
	return ::last(*this, n);
}
template <typename T>
template <size_t N>
inline std::span<T, N> class_pool<T>::first_fixed() noexcept
{
	return ::first_fixed<N>(*this);
}
template <typename T>
template <size_t N>
inline std::span<const T, N> class_pool<T>::first_fixed() const noexcept
{
	return ::first_fixed<N>(*this);
}
template <typename T>
template <size_t N>
inline std::span<T, N> class_pool<T>::last_fixed() noexcept
{
	return ::last_fixed<N>(*this);
}
template <typename T>
template <size_t N>
inline std::span<const T, N> class_pool<T>::last_fixed() const noexcept
{
	return ::last_fixed<N>(*this);
}

template <typename T>
inline pool_strided_span<T> class_pool<T>::strided_span_view(
    size_t start, size_t step, size_t count) noexcept
{
	return ::strided_span_view(*this, start, step, count);
}
template <typename T>
template <typename F>
inline void class_pool<T>::strided_for_each(size_t start, size_t step, F&& f) noexcept
{
	::strided_for_each(*this, start, step, std::forward<F>(f));
}
template <typename T>
template <typename F>
inline void class_pool<T>::strided_for_each(size_t start, size_t step, F&& f) const noexcept
{
	::strided_for_each(*this, start, step, std::forward<F>(f));
}
template <typename T>
template <size_t Step, typename F>
inline void class_pool<T>::strided_for_each(F&& f) noexcept
{
	::strided_for_each<Step>(*this, std::forward<F>(f));
}
template <typename T>
template <size_t Step, typename F>
inline void class_pool<T>::strided_for_each(F&& f) const noexcept
{
	::strided_for_each<Step>(*this, std::forward<F>(f));
}

template <typename T>
template <typename FTransform, typename FConsume>
inline void class_pool<T>::transform_for_each(FTransform&& tr, FConsume&& con) noexcept
{
	::transform_for_each(*this, std::forward<FTransform>(tr), std::forward<FConsume>(con));
}
template <typename T>
template <typename FTransform, typename FConsume>
inline void class_pool<T>::transform_for_each(FTransform&& tr, FConsume&& con) const noexcept
{
	::transform_for_each(*this, std::forward<FTransform>(tr), std::forward<FConsume>(con));
}
template <typename T>
template <typename R, typename F>
inline void class_pool<T>::transform_to(R* dst, size_t count, F&& tr) const noexcept
{
	::transform_to(*this, dst, count, std::forward<F>(tr));
}

template <typename T>
inline T* class_pool<T>::find(const T& value) noexcept
{
	return ::find(*this, value);
}
template <typename T>
inline const T* class_pool<T>::find(const T& value) const noexcept
{
	return ::find(*this, value);
}
template <typename T>
template <typename Pred> requires std::predicate<Pred, const T&>
inline T* class_pool<T>::find_if(Pred pred) noexcept
{
	return ::find_if(*this, pred);
}
template <typename T>
template <typename Pred> requires std::predicate<Pred, const T&>
inline const T* class_pool<T>::find_if(Pred pred) const noexcept
{
	return ::find_if(*this, pred);
}
template <typename T>
template <typename Pred> requires std::predicate<Pred, const T&>
inline T* class_pool<T>::find_if_not(Pred pred) noexcept
{
	return ::find_if_not(*this, pred);
}
template <typename T>
template <typename Pred> requires std::predicate<Pred, const T&>
inline const T* class_pool<T>::find_if_not(Pred pred) const noexcept
{
	return ::find_if_not(*this, pred);
}
template <typename T>
inline bool class_pool<T>::contains(const T& value) const noexcept
{
	return ::contains(*this, value);
}
template <typename T>
template <typename Pred> requires std::predicate<Pred, const T&>
inline size_t class_pool<T>::count_if(Pred pred) const noexcept
{
	return ::count_if(*this, pred);
}
template <typename T>
template <typename Pred, typename F>
requires std::predicate<Pred, const T&> && std::invocable<F, T&>
inline void class_pool<T>::filter_for_each(Pred pred, F&& f) noexcept
{
	::filter_for_each(*this, pred, std::forward<F>(f));
}
template <typename T>
template <typename Pred, typename F>
requires std::predicate<Pred, const T&> && std::invocable<F, const T&>
inline void class_pool<T>::filter_for_each(Pred pred, F&& f) const noexcept
{
	::filter_for_each(*this, pred, std::forward<F>(f));
}
template <typename T>
template <typename Pred> requires std::predicate<Pred, const T&>
inline void class_pool<T>::filter_indices_to(class_pool<size_t>& dst, Pred pred) noexcept
{
	::filter_indices_to(*this, dst, pred);
}

template <typename T>
template <typename F, typename U>
inline U class_pool<T>::reduce(F&& f, U init) const noexcept
{
	return ::reduce(*this, std::forward<F>(f), std::move(init));
}
template <typename T>
template <typename F, typename U>
inline U class_pool<T>::reduce_pairwise(F&& f, U init) const noexcept
{
	return ::reduce_pairwise(*this, std::forward<F>(f), std::move(init));
}
template <typename T>
inline T* class_pool<T>::min_element() noexcept
{
	return ::min_element(*this);
}
template <typename T>
inline const T* class_pool<T>::min_element() const noexcept
{
	return ::min_element(*this);
}
template <typename T>
inline T* class_pool<T>::max_element() noexcept
{
	return ::max_element(*this);
}
template <typename T>
inline const T* class_pool<T>::max_element() const noexcept
{
	return ::max_element(*this);
}
template <typename T>
inline std::pair<T*, T*> class_pool<T>::minmax_element() noexcept
{
	return ::minmax_element(*this);
}
template <typename T>
inline std::pair<const T*, const T*> class_pool<T>::minmax_element() const noexcept
{
	return ::minmax_element(*this);
}
template <typename T>
template <typename U> requires std::is_arithmetic_v<U>
inline U class_pool<T>::sum() const noexcept
{
	return ::sum(*this);
}
template <typename T>
template <typename U> requires std::is_arithmetic_v<U>
inline U class_pool<T>::dot_product(const U* other, size_t count) const noexcept
{
	return ::dot_product(*this, other, count);
}

template <typename T>
template <size_t N, typename F>
requires (N > 0) && std::invocable<F, std::span<T, N>>
inline void class_pool<T>::for_each_window(F&& f) noexcept
{
	::for_each_window<N>(*this, std::forward<F>(f));
}
template <typename T>
template <size_t N, typename F>
requires (N > 0) && std::invocable<F, std::span<const T, N>>
inline void class_pool<T>::for_each_window(F&& f) const noexcept
{
	::for_each_window<N>(*this, std::forward<F>(f));
}
template <typename T>
template <size_t N, typename F>
requires (N > 0) && std::invocable<F, std::span<T, N>>
inline void class_pool<T>::for_each_chunk(F&& f) noexcept
{
	::for_each_chunk<N>(*this, std::forward<F>(f));
}
template <typename T>
template <size_t N, typename F>
requires (N > 0) && std::invocable<F, std::span<const T, N>>
inline void class_pool<T>::for_each_chunk(F&& f) const noexcept
{
	::for_each_chunk<N>(*this, std::forward<F>(f));
}
template <typename T>
template <size_t N> requires (N > 0)
inline std::span<T, N> class_pool<T>::window_span(size_t offset) noexcept
{
	return ::window_span<N>(*this, offset);
}
template <typename T>
template <size_t N> requires (N > 0)
inline std::span<const T, N> class_pool<T>::window_span(size_t offset) const noexcept
{
	return ::window_span<N>(*this, offset);
}
template <typename T>
template <size_t N> requires (N > 0)
inline std::span<T, N> class_pool<T>::chunk_span(size_t chunk_idx) noexcept
{
	return ::chunk_span<N>(*this, chunk_idx);
}
template <typename T>
template <size_t N> requires (N > 0)
inline std::span<const T, N> class_pool<T>::chunk_span(size_t chunk_idx) const noexcept
{
	return ::chunk_span<N>(*this, chunk_idx);
}

template <typename T>
template <typename F> requires std::invocable<F, size_t, T&>
inline void class_pool<T>::for_each_enumerated(F&& f) noexcept
{
	::for_each_enumerated(*this, std::forward<F>(f));
}
template <typename T>
template <typename F> requires std::invocable<F, size_t, const T&>
inline void class_pool<T>::for_each_enumerated(F&& f) const noexcept
{
	::for_each_enumerated(*this, std::forward<F>(f));
}

template <typename T>
template <typename U, typename F>
requires std::invocable<F, T&, U&>
inline void class_pool<T>::for_each_zip(class_pool<U>& other, F&& f) noexcept
{
	::for_each_zip(*this, other, std::forward<F>(f));
}
template <typename T>
template <typename U, typename F>
requires std::invocable<F, const T&, const U&>
inline void class_pool<T>::for_each_zip(const class_pool<U>& other, F&& f) const noexcept
{
	::for_each_zip(*this, other, std::forward<F>(f));
}
template <typename T>
template <typename U, typename F>
requires std::invocable<F, T&, U&>
inline void class_pool<T>::for_each_zip(U* other, size_t count, F&& f) noexcept
{
	::for_each_zip(*this, other, count, std::forward<F>(f));
}
template <typename T>
template <typename U, typename F>
requires std::invocable<F, const T&, const U&>
inline void class_pool<T>::for_each_zip(const U* other, size_t count, F&& f) const noexcept
{
	::for_each_zip(*this, other, count, std::forward<F>(f));
}
template <typename T>
template <typename U, typename F>
requires std::invocable<F, T&, U&>
inline void class_pool<T>::for_each_zip(std::span<U> other, F&& f) noexcept
{
	::for_each_zip(*this, other.data(), other.size(), std::forward<F>(f));
}
template <typename T>
template <typename U, typename F>
requires std::invocable<F, const T&, const U&>
inline void class_pool<T>::for_each_zip(std::span<const U> other, F&& f) const noexcept
{
	::for_each_zip(*this, other.data(), other.size(), std::forward<F>(f));
}
template <typename T>
template <typename U, typename R, typename F>
requires std::invocable<F, const T&, const U&>
inline void class_pool<T>::zip_with_to(R* dst, const U* other, size_t count, F&& f) const noexcept
{
	::zip_with_to(*this, other, dst, count, std::forward<F>(f));
}

template <typename T>
inline bool class_pool<T>::equal(const T* other, size_t count) const noexcept
{
	return ::equal(*this, other, count);
}
template <typename T>
template <typename U>
inline bool class_pool<T>::equal(const class_pool<U>& other) const noexcept
{
	if (count() != other.count()) [[unlikely]] { return false; }
	auto ia = cbegin();
	auto ib = other.cbegin();
	const auto ea = cend();
	while (ia != ea)
	{
		if (!(*ia == *ib)) [[unlikely]] { return false; }
		++ia;
		++ib;
	}
	return true;
}
template <typename T>
template <typename U>
inline bool class_pool<T>::equal(std::span<const U> other) const noexcept
{
	return equal(other.data(), other.size());
}

template <typename T>
inline T* class_pool<T>::aligned_data() noexcept
{
	return ::aligned_data(*this);
}
template <typename T>
inline const T* class_pool<T>::aligned_data() const noexcept
{
	return ::aligned_data(*this);
}
template <typename T>
inline std::span<T> class_pool<T>::aligned_span() noexcept
{
	return ::aligned_span(*this);
}
template <typename T>
inline std::span<const T> class_pool<T>::aligned_span() const noexcept
{
	return ::aligned_span(*this);
}
template <typename T>
template <typename F> requires std::is_trivially_copyable_v<T> && std::invocable<F, T&>
inline void class_pool<T>::simd_for_each(F&& f) noexcept
{
	::simd_for_each(*this, std::forward<F>(f));
}
template <typename T>
template <typename F> requires std::is_trivially_copyable_v<T> && std::invocable<F, const T&>
inline void class_pool<T>::simd_for_each(F&& f) const noexcept
{
	::simd_for_each(*this, std::forward<F>(f));
}
template <typename T>
inline size_t class_pool<T>::unaligned_tail_offset() const noexcept
{
	return ::unaligned_tail_offset(*this);
}

template <typename T>
inline void class_pool<T>::copy_to(T* dst, size_t count) const noexcept
{
	::copy_to(*this, dst, count);
}
template <typename T>
inline void class_pool<T>::copy_to(std::span<T> dst) const noexcept
{
	::copy_to(*this, dst);
}
template <typename T>
inline void class_pool<T>::move_to(T* dst, size_t count) noexcept
{
	::move_to(*this, dst, count);
}
template <typename T>
inline void class_pool<T>::move_to(std::span<T> dst) noexcept
{
	::move_to(*this, dst);
}
template <typename T>
inline void class_pool<T>::reverse_copy_to(T* dst, size_t count) const noexcept
{
	::reverse_copy_to(*this, dst, count);
}
template <typename T>
inline void class_pool<T>::reverse_copy_to(std::span<T> dst) const noexcept
{
	::reverse_copy_to(*this, dst);
}

// class_pool 独有接口
template <typename T>
inline size_t class_pool<T>::compact_to(T* dst, size_t count) const noexcept
{
	return ::compact_to(*this, dst, count);
}
template <typename T>
inline size_t class_pool<T>::live_count() const noexcept
{
	return ::live_count(*this);
}
template <typename T>
inline size_t class_pool<T>::holes_count() const noexcept
{
	return ::holes_count(*this);
}