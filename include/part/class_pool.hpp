#pragma once
#include <new>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <type_traits>
#include <memory>
#include <iterator>
#include <limits>
#include <bit>
#include <span>
#include <utility>
#if defined(__AVX2__) || defined(__BMI__) || (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)))
#include <immintrin.h>
#endif

// 跨平台预取宏
#if defined(__GNUC__) || defined(__clang__)
#define PREFETCH_R(ptr) __builtin_prefetch(ptr, 0, 3)
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define PREFETCH_R(ptr) _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0)
#elif defined(_MSC_VER)
#include <intrin.h>
#define PREFETCH_R(ptr) __prefetch(ptr)
#else
#define PREFETCH_R(ptr) ((void)0)
#endif

namespace ecs { class single_class_set; }

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
	size_t maximum_quantity_{0};
	size_t index_{0};
	bool is_dense_{true};
	mutable size_t count_cache_{static_cast<size_t>(-1)};
	size_t hole_count_{0};
	size_t first_hole_hint_{0};
	uint64_t inline_bits_{0};

	static constexpr size_t DEFAULT_CAPACITY = 8;
	static constexpr size_t SMALL_CAPACITY_THRESHOLD = 1024;
	static constexpr size_t MEDIUM_CAPACITY_THRESHOLD = 65536;

	[[nodiscard]] static constexpr size_t round_up_to_default(size_t n) noexcept {
		return n == 0 ? DEFAULT_CAPACITY : n;
	}

	[[nodiscard]] static constexpr size_t calculate_new_capacity(size_t current) noexcept {
		if (current == 0) { return DEFAULT_CAPACITY; }
		if (current < SMALL_CAPACITY_THRESHOLD) { return current * 2; }
		if (current < MEDIUM_CAPACITY_THRESHOLD) { return current + (current >> 1); }
		return current + (current >> 2);
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
			std::terminate();
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
		}
		sparse_bits_ = nullptr;
	}

	uint64_t* obtain_bitmap(size_t capacity) noexcept {
		if (capacity <= INLINE_CAPACITY) {
			inline_bits_ = 0;
			return &inline_bits_;
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
			std::terminate();
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

		Ptr ptr_;
		Ptr end_;
		const uint64_t* bits_;
		Ptr origin_;

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

		// MinGW AVX2 codegen 变通: 标量拷贝构造防止 256-bit 结构体复制的 codegen bug
		// 仅 MinGW 启用, MSVC/Clang 用默认拷贝以保留向量优化
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

		void skip_to_next_valid() noexcept {
			const size_t idx = static_cast<size_t>(ptr_ - origin_);
			const size_t total = static_cast<size_t>(end_ - origin_);
			if (idx >= total) { ptr_ = end_; return; }

			size_t word_idx = idx / BITS_PER_WORD;
			size_t bit_in_word = idx % BITS_PER_WORD;
			const size_t total_words = (total + BITS_PER_WORD - 1) / BITS_PER_WORD;

			uint64_t word = bits_[word_idx] >> bit_in_word;
			if (word != 0) {
				ptr_ = origin_ + word_idx * BITS_PER_WORD + bit_in_word + std::countr_zero(word);
				return;
			}

			++word_idx;
#ifdef __AVX2__
			for (; word_idx + 4 <= total_words; word_idx += 4)
			{
				__m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bits_ + word_idx));
				if (!_mm256_testz_si256(v, v))
				{
					for (size_t j = 0; j < 4; ++j)
					{
						uint64_t w = bits_[word_idx + j];
						if (w != 0)
						{
							ptr_ = origin_ + (word_idx + j) * BITS_PER_WORD + std::countr_zero(w);
							return;
						}
					}
				}
			}
#endif
			for (; word_idx < total_words; ++word_idx) {
				word = bits_[word_idx];
				if (word != 0) {
					ptr_ = origin_ + word_idx * BITS_PER_WORD + std::countr_zero(word);
					return;
				}
			}
			ptr_ = end_;
		}

		basic_iterator& operator++() noexcept {
			constexpr size_t pf_dist = (sizeof(T) < 64) ? (64 / sizeof(T)) : 1;
			if (bits_ != nullptr) {
				++ptr_;
				if (ptr_ != end_) {
					PREFETCH_R(ptr_ + pf_dist);
					skip_to_next_valid();
				}
			}
			else {
				++ptr_;
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
	template <typename InputIt>
	class_pool(InputIt first, InputIt last) noexcept;
	class_pool(std::initializer_list<T> init) noexcept;
	class_pool(const class_pool& other) noexcept;
	class_pool(class_pool&& other) noexcept;
	class_pool& operator=(const class_pool& other) noexcept;
	class_pool& operator=(class_pool&& other) noexcept;
	~class_pool() noexcept;

	template <typename... Args>
	void emplace_back(Args&&... args) noexcept;
	void push_back_unchecked(const T& value) noexcept;
	template <typename... Args>
	void emplace_back_unchecked(Args&&... args) noexcept;
	template <typename... Args>
	void emplace_back_dense_unchecked(Args&&... args) noexcept;
	void append_n(size_t n, const T& value) noexcept;
	void clear() noexcept;

	[[nodiscard]] constexpr T* get(size_t index) noexcept {
		return &data_ptr_[index];
	}

	[[nodiscard]] constexpr const T* get(size_t index) const noexcept {
		return &data_ptr_[index];
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
		return std::assume_aligned<alignof(T)>(data_ptr_);
	}

	[[nodiscard]] constexpr const_pointer data() const noexcept {
		return std::assume_aligned<alignof(T)>(data_ptr_);
	}

	void increase_capacity(size_t new_capacity) noexcept;
	void increase_capacity(size_t new_capacity, const T& value) noexcept;
	void shrink_to_fit() noexcept;
	void reduce_capacity(size_t new_capacity) noexcept;
	void reduce_capacity(size_t new_capacity, class_pool<T>& dst) noexcept;

	[[nodiscard]] T& at(size_t index) noexcept;
	[[nodiscard]] const T& at(size_t index) const noexcept;

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
	void resize(size_t new_size, const T& value) noexcept;

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

	// 密集遍历视图: 返回原始指针范围, 跳过迭代器开销
	struct dense_view
	{
		T* begin_ptr;
		T* end_ptr;
		[[nodiscard]] T* begin() const noexcept
		{
			return begin_ptr;
		}
		[[nodiscard]] T* end() const noexcept
		{
			return end_ptr;
		}
	};

	struct const_dense_view
	{
		const T* begin_ptr;
		const T* end_ptr;
		[[nodiscard]] const T* begin() const noexcept
		{
			return begin_ptr;
		}
		[[nodiscard]] const T* end() const noexcept
		{
			return end_ptr;
		}
	};

	// 获取密集遍历视图, 仅在 is_dense() 为 true 时使用
	[[nodiscard]] dense_view dense_view() noexcept
	{
		return {data_ptr_, data_ptr_ + index_};
	}

	[[nodiscard]] const_dense_view dense_view() const noexcept
	{
		return {data_ptr_, data_ptr_ + index_};
	}

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

	template <typename F>
	void for_each(F&& f) noexcept;
	template <typename F>
	void for_each(F&& f) const noexcept;
	template <typename F>
	void for_each_sparse(F&& f) noexcept;
	template <typename F>
	void for_each_sparse(F&& f) const noexcept;

	template <typename... Args>
	T& emplace_at(size_t index, Args&&... args) noexcept;
	template <typename... Args>
	T& sparse_emplace_at(size_t index, Args&&... args) noexcept;
	void sparse_erase_at(size_t index) noexcept;

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
	// 调用方可通过 operator[](idx) 或 data_ptr_[idx] 访问元素
	template <typename... Args>
	size_t fill_the_hole_at(Args&&... args) noexcept;

private:
	[[nodiscard]] size_t find_first_hole_() noexcept;
	void recompute_is_dense() noexcept;
	void bulk_set_bits(size_t start, size_t end) noexcept;
	void append_bulk(const T* src, size_t count) noexcept;
	void append_bulk_move(T* src, size_t count) noexcept;
	void append_incrementing(size_t count, uint64_t& counter) noexcept;

	template <typename F>
	void append_generated(size_t count, F&& generator) noexcept;

	template <typename EntityLike>
	void append_indices_from(const EntityLike* entities, size_t count) noexcept;

public:
	void fill_bulk(const T& value, size_t start, size_t count) noexcept;
	void prepare_dense(size_t new_size) noexcept;

private:
	void update_dense_status() noexcept;
};

template <typename T>
void swap(class_pool<T>& a, class_pool<T>& b) noexcept;

#include "class_pool.inc.hpp"