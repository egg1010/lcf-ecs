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

#if defined(__GNUC__) || defined(__clang__)
#define DENSE_PREFETCH_R(ptr) __builtin_prefetch(ptr, 0, 3)
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define DENSE_PREFETCH_R(ptr) _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0)
#elif defined(_MSC_VER)
#include <intrin.h>
#define DENSE_PREFETCH_R(ptr) __prefetch(ptr)
#else
#define DENSE_PREFETCH_R(ptr) ((void)0)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define DENSE_ALWAYS_INLINE [[gnu::always_inline]] inline
#define DENSE_FLATTEN [[gnu::flatten]]
#define DENSE_RESTRICT __restrict
#elif defined(_MSC_VER)
#define DENSE_ALWAYS_INLINE __forceinline
#define DENSE_FLATTEN
#define DENSE_RESTRICT __restrict
#else
#define DENSE_ALWAYS_INLINE inline
#define DENSE_FLATTEN
#define DENSE_RESTRICT
#endif

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

	[[nodiscard]] static constexpr size_t round_up_to_default(size_t n) noexcept {
		return n == 0 ? DEFAULT_CAPACITY : n;
	}

	[[nodiscard]] static constexpr size_t calculate_new_capacity(size_t current) noexcept {
		if (current == 0) { return DEFAULT_CAPACITY; }

		// L1 启动区 [0, 1K): 4x 快速启动
		if (current < SMALL_CAPACITY_THRESHOLD) { return current * 4; }

		// L2 工作区 [1K, 64K): 大元素降倍率控制浪费
		if (current < MEDIUM_CAPACITY_THRESHOLD) {
			if constexpr (sizeof(T) <= 64) {
				return current * 4;           // 小/中元素: 4x 保持速度
			}
			else {
				return current * 2;           // 大元素: 2x 控制浪费
			}
		}

		// L3 大块区 [64K, ∞): 64B 对齐大块分配成本高, 按 sizeof(T) 分流
		if constexpr (sizeof(T) <= 64) {
			return current * 4;               // 小/中元素: 4x 减少分配次数
		}
		else if constexpr (sizeof(T) <= 256) {
			return current * 2;               // 大元素: 2x 平衡
		}
		else {
			return current + current / 2;     // 超大元素: 1.5x 减少浪费
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

	void destroy_range(size_t first, size_t last) noexcept {
		if constexpr (!std::is_trivially_destructible_v<T>) {
			if (first < last) {
				std::destroy_n(data_ptr_ + first, last - first);
			}
		}
	}

	void destroy_all() noexcept {
		if constexpr (std::is_trivially_destructible_v<T>) { return; }
		if (index_ == 0) { return; }
		std::destroy_n(data_ptr_, index_);
	}

	void uninitialized_move_dense(T* src_first, T* src_last, T* dst) noexcept {
		if constexpr (std::is_trivially_copyable_v<T>) {
			const size_t n = static_cast<size_t>(src_last - src_first);
			if (n != 0) [[likely]] {
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

	static void copy_trivial_data(T* DENSE_RESTRICT dst, const T* DENSE_RESTRICT src, size_t count) noexcept {
		const size_t bytes = count * sizeof(T);
#ifdef __AVX2__
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
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
#endif
		if (bytes != 0) [[likely]]
		{
			std::memcpy(std::assume_aligned<alignof(T)>(dst),
			            std::assume_aligned<alignof(T)>(src),
			            bytes);
		}
	}

	void grow_data(size_t new_capacity) noexcept {
		if (new_capacity <= maximum_quantity_) [[likely]] {
			return;
		}

		T* new_data = allocate_data(new_capacity);
		if (data_ptr_ != nullptr && index_ > 0) [[likely]] {
			if constexpr (std::is_trivially_copyable_v<T>) {
				copy_trivial_data(new_data, data_ptr_, index_);
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
			for (size_t i = 0; i < count; ++i) {
				new (&data_ptr_[i]) T(value);
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

	dense(const dense& other) noexcept
		: data_ptr_(nullptr)
		, maximum_quantity_(other.maximum_quantity_)
		, index_(other.index_) {
		if (maximum_quantity_ > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			if constexpr (std::is_trivially_copyable_v<T>) {
				const size_t bytes = other.index_ * sizeof(T);
				if (bytes != 0) [[likely]] {
					std::memcpy(std::assume_aligned<alignof(T)>(data_ptr_),
					            std::assume_aligned<alignof(T)>(other.data_ptr_),
					            bytes);
				}
			}
			else {
				std::uninitialized_copy(other.data_ptr_, other.data_ptr_ + other.index_, data_ptr_);
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

	dense& operator=(const dense& other) noexcept {
		if (this == &other) [[unlikely]] { return *this; }
		destroy_all();
		deallocate_data(data_ptr_, maximum_quantity_);

		maximum_quantity_ = other.maximum_quantity_;
		index_ = other.index_;

		if (maximum_quantity_ > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			if constexpr (std::is_trivially_copyable_v<T>) {
				const size_t bytes = other.index_ * sizeof(T);
				if (bytes != 0) [[likely]] {
					std::memcpy(std::assume_aligned<alignof(T)>(data_ptr_),
					            std::assume_aligned<alignof(T)>(other.data_ptr_),
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

	[[nodiscard]] constexpr T& operator[](size_t index) noexcept {
		return data_ptr_[index];
	}

	[[nodiscard]] constexpr const T& operator[](size_t index) const noexcept {
		return data_ptr_[index];
	}

	[[nodiscard]] constexpr T& get(size_t index) noexcept {
		return data_ptr_[index];
	}

	[[nodiscard]] constexpr const T& get(size_t index) const noexcept {
		return data_ptr_[index];
	}

	[[nodiscard]] constexpr T& get(size_t index, size_t error_index) noexcept {
		return data_ptr_[index < index_ ? index : error_index];
	}

	[[nodiscard]] constexpr const T& get(size_t index, size_t error_index) const noexcept {
		return data_ptr_[index < index_ ? index : error_index];
	}

	[[nodiscard]] constexpr T& front() noexcept { return data_ptr_[0]; }
	[[nodiscard]] constexpr const T& front() const noexcept { return data_ptr_[0]; }
	[[nodiscard]] constexpr T& back() noexcept { return data_ptr_[index_ - 1]; }
	[[nodiscard]] constexpr const T& back() const noexcept { return data_ptr_[index_ - 1]; }

	[[nodiscard]] constexpr pointer data() noexcept { return data_ptr_; }
	[[nodiscard]] constexpr const_pointer data() const noexcept { return data_ptr_; }

	[[nodiscard]] constexpr iterator begin() noexcept { return data_ptr_; }
	[[nodiscard]] constexpr const_iterator begin() const noexcept { return data_ptr_; }
	[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return data_ptr_; }
	[[nodiscard]] constexpr iterator end() noexcept { return data_ptr_ + index_; }
	[[nodiscard]] constexpr const_iterator end() const noexcept { return data_ptr_ + index_; }
	[[nodiscard]] constexpr const_iterator cend() const noexcept { return data_ptr_ + index_; }

	[[nodiscard]] constexpr size_type capacity() const noexcept { return maximum_quantity_; }
	[[nodiscard]] constexpr size_type size() const noexcept { return index_; }
	[[nodiscard]] constexpr bool empty() const noexcept { return index_ == 0; }
	[[nodiscard]] constexpr bool valid() const noexcept { return data_ptr_ != nullptr; }
	[[nodiscard]] constexpr size_type max_size() const noexcept { return static_cast<size_t>(-1) / sizeof(T); }
	[[nodiscard]] constexpr size_type size_bytes() const noexcept { return index_ * sizeof(T); }
	[[nodiscard]] constexpr size_type capacity_bytes() const noexcept { return maximum_quantity_ * sizeof(T); }

	[[nodiscard]] constexpr size_type count() const noexcept { return index_; }

	[[nodiscard]] constexpr std::span<T> span() noexcept { return std::span<T>(data_ptr_, index_); }
	[[nodiscard]] constexpr std::span<const T> span() const noexcept { return std::span<const T>(data_ptr_, index_); }

	void clear() noexcept {
		destroy_all();
		index_ = 0;
	}

	void increase_capacity(size_t new_capacity) noexcept {
		if (new_capacity > maximum_quantity_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(new_capacity));
		}
	}

	void increase_capacity(size_t new_capacity, const T& value) noexcept {
		if (new_capacity <= index_) [[likely]] {
			return;
		}

		if (new_capacity > maximum_quantity_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(new_capacity));
		}

		if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8) {
			for (size_t i = index_; i < new_capacity; ++i) {
				std::memcpy(&data_ptr_[i], &value, sizeof(T));
			}
		}
		else {
			for (size_t i = index_; i < new_capacity; ++i) {
				new (&data_ptr_[i]) T(value);
			}
		}
		index_ = new_capacity;
	}

	void reserve_exact(size_t new_capacity) noexcept {
		grow_data(new_capacity);
	}

	void resize(size_t new_size, const T& value) noexcept {
		if (new_size <= index_) [[unlikely]] {
			destroy_range(new_size, index_);
			index_ = new_size;
			return;
		}

		if (new_size > maximum_quantity_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(new_size));
		}

		if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8) {
			for (size_t i = index_; i < new_size; ++i) {
				std::memcpy(&data_ptr_[i], &value, sizeof(T));
			}
		}
		else {
			for (size_t i = index_; i < new_size; ++i) {
				new (&data_ptr_[i]) T(value);
			}
		}
		index_ = new_size;
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
				copy_trivial_data(new_data, data_ptr_, index_);
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
			const size_t bytes = index_ * sizeof(T);
			if (bytes != 0) [[likely]] {
				std::memcpy(std::assume_aligned<alignof(T)>(new_data),
				            std::assume_aligned<alignof(T)>(data_ptr_),
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

		for (size_t i = new_capacity; i < index_; ++i) {
			new (&dst.data_ptr_[dst.index_]) T(std::move(data_ptr_[i]));
			++dst.index_;
			if constexpr (!std::is_trivially_destructible_v<T>) {
				data_ptr_[i].~T();
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
				const size_t bytes = index_ * sizeof(T);
				if (bytes != 0) [[likely]] {
					std::memcpy(std::assume_aligned<alignof(T)>(new_data),
					            std::assume_aligned<alignof(T)>(data_ptr_),
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
	}

	template <typename... Args>
	void emplace_back(Args&&... args) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data(calculate_new_capacity(maximum_quantity_));
		}
		new (&data_ptr_[index_]) T(std::forward<Args>(args)...);
		++index_;
	}

	void push_back_unchecked(const T& value) noexcept {
		new (&data_ptr_[index_]) T(value);
		++index_;
	}

	template <typename... Args>
	void emplace_back_unchecked(Args&&... args) noexcept {
		new (&data_ptr_[index_]) T(std::forward<Args>(args)...);
		++index_;
	}

	template <typename... Args>
	void emplace_back_dense_unchecked(Args&&... args) noexcept {
		new (&data_ptr_[index_]) T(std::forward<Args>(args)...);
		++index_;
	}

	void append_n(size_t n, const T& value) noexcept {
		if (n == 0) [[unlikely]] { return; }
		if (n > maximum_quantity_ - index_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(index_ + n));
		}

		if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8)
		{
			for (size_t i = 0; i < n; ++i)
			{
				std::memcpy(&data_ptr_[index_ + i], &value, sizeof(T));
			}
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				new (&data_ptr_[index_ + i]) T(value);
			}
		}
		index_ += n;
	}

	void append_bulk(const T* src, size_t count) noexcept {
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			grow_data(calculate_growth_for_reserve(index_ + count));
		}
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(data_ptr_ + index_, src, count * sizeof(T));
		}
		else {
			for (size_t i = 0; i < count; ++i) {
				new (data_ptr_ + index_ + i) T(src[i]);
			}
		}
		index_ += count;
	}

	void append_bulk_move(T* src, size_t count) noexcept {
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
			grow_data(calculate_growth_for_reserve(end));
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
			const size_t elem_per_ymm = 32 / sizeof(T);
			const size_t chunks = count / elem_per_ymm;
			const size_t remainder = count - chunks * elem_per_ymm;
#ifdef __AVX2__
			__m256i broadcast;
			if constexpr (sizeof(T) == 2) {
				broadcast = _mm256_set1_epi16(std::bit_cast<int16_t>(value));
			}
			else if constexpr (sizeof(T) == 4) {
				broadcast = _mm256_set1_epi32(std::bit_cast<int32_t>(value));
			}
			else {
				broadcast = _mm256_set1_epi64x(std::bit_cast<int64_t>(value));
			}
			__m256i* d = static_cast<__m256i*>(static_cast<void*>(data_ptr_ + start));
			for (size_t i = 0; i < chunks; ++i)
			{
				_mm256_storeu_si256(d + i, broadcast);
			}
#else
			for (size_t i = 0; i < chunks; ++i) {
				std::memcpy(data_ptr_ + start + i * elem_per_ymm, &value, elem_per_ymm * sizeof(T));
			}
#endif
			for (size_t i = 0; i < remainder; ++i) {
				data_ptr_[start + chunks * elem_per_ymm + i] = value;
			}
		}
		else if constexpr (std::is_trivially_copyable_v<T>) {
			for (size_t i = 0; i < count; ++i) {
				data_ptr_[start + i] = value;
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

	void pop_back() noexcept {
		if (index_ > 0) [[likely]] {
			const size_t idx = index_ - 1;
			if constexpr (!std::is_trivially_destructible_v<T>) {
				data_ptr_[idx].~T();
			}
			--index_;
		}
	}

	template <typename... Args>
	iterator emplace(const_iterator pos, Args&&... args) noexcept {
		const size_t index = static_cast<size_t>(pos - data_ptr_);
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data(calculate_new_capacity(maximum_quantity_));
		}

		if (index < index_) [[likely]] {
			if constexpr (std::is_trivially_copyable_v<T>) {
				std::memmove(std::assume_aligned<alignof(T)>(data_ptr_ + index + 1),
				             std::assume_aligned<alignof(T)>(data_ptr_ + index),
				             (index_ - index) * sizeof(T));
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

	iterator erase(const_iterator pos) noexcept {
		const size_t index = static_cast<size_t>(pos - data_ptr_);
		if (index >= index_) [[unlikely]] {
			return end();
		}

		if (index < index_ - 1) [[likely]] {
			if constexpr (std::is_trivially_copyable_v<T>) {
				std::memmove(std::assume_aligned<alignof(T)>(data_ptr_ + index),
				             std::assume_aligned<alignof(T)>(data_ptr_ + index + 1),
				             (index_ - index - 1) * sizeof(T));
			}
			else {
				std::move(data_ptr_ + index + 1, data_ptr_ + index_, data_ptr_ + index);
				data_ptr_[index_ - 1].~T();
			}
		}
		else {
			if constexpr (!std::is_trivially_destructible_v<T>) {
				data_ptr_[index].~T();
			}
		}

		--index_;
		return data_ptr_ + index;
	}

	iterator erase(const_iterator first, const_iterator last) noexcept {
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
			if constexpr (std::is_trivially_copyable_v<T>) {
				std::memmove(std::assume_aligned<alignof(T)>(data_ptr_ + start_index),
				             std::assume_aligned<alignof(T)>(data_ptr_ + real_end),
				             (index_ - real_end) * sizeof(T));
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
	DENSE_FLATTEN void for_each(F&& f) noexcept
	{
		T* DENSE_RESTRICT p = std::assume_aligned<alignof(T)>(data_ptr_);
		T* DENSE_RESTRICT e = p + index_;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC ivdep
#endif
		while (p != e)
		{
			f(*p);
			++p;
		}
	}

	template <typename F>
	DENSE_FLATTEN void for_each(F&& f) const noexcept
	{
		const T* DENSE_RESTRICT p = std::assume_aligned<alignof(T)>(data_ptr_);
		const T* DENSE_RESTRICT e = p + index_;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC ivdep
#endif
		while (p != e)
		{
			f(*p);
			++p;
		}
	}
};

template <typename T>
void swap(dense<T>& a, dense<T>& b) noexcept {
	a.swap(b);
}
