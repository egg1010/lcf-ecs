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

	// 只扩容不缩容: new_capacity <= size 时直接返回, 不销毁任何对象
	// 复用 fill_bulk 的 AVX2 批量填充路径
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

	// push_back 拷贝 (trivially copyable 走 memcpy 快路径)
	void push_back(const T& value) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data(calculate_new_capacity(maximum_quantity_));
		}
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(&data_ptr_[index_], &value, sizeof(T));
		} else {
			new (&data_ptr_[index_]) T(value);
		}
		++index_;
	}

	// push_back 移动
	void push_back(T&& value) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data(calculate_new_capacity(maximum_quantity_));
		}
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(&data_ptr_[index_], &value, sizeof(T));
		} else {
			new (&data_ptr_[index_]) T(std::move(value));
		}
		++index_;
	}

	void push_back_unchecked(const T& value) noexcept {
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(&data_ptr_[index_], &value, sizeof(T));
		} else {
			new (&data_ptr_[index_]) T(value);
		}
		++index_;
	}

	void push_back_unchecked(T&& value) noexcept {
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(&data_ptr_[index_], &value, sizeof(T));
		} else {
			new (&data_ptr_[index_]) T(std::move(value));
		}
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
				for (size_t j = 0; j < elem_per_ymm; ++j) {
					data_ptr_[start + i * elem_per_ymm + j] = value;
				}
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
#if defined(__GNUC__) && !defined(__clang__)
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
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		while (p != e)
		{
			f(*p);
			++p;
		}
	}

	// A. 子范围视图: 零拷贝切片, 返回 std::span
	[[nodiscard]] constexpr std::span<T> subspan(size_t offset, size_t count) noexcept
	{
		if (offset >= index_) [[unlikely]] { return std::span<T>(); }
		const size_t avail = index_ - offset;
		return std::span<T>(data_ptr_ + offset, count > avail ? avail : count);
	}
	[[nodiscard]] constexpr std::span<const T> subspan(size_t offset, size_t count) const noexcept
	{
		if (offset >= index_) [[unlikely]] { return std::span<const T>(); }
		const size_t avail = index_ - offset;
		return std::span<const T>(data_ptr_ + offset, count > avail ? avail : count);
	}
	[[nodiscard]] constexpr std::span<T> subspan(size_t offset) noexcept
	{
		if (offset >= index_) [[unlikely]] { return std::span<T>(); }
		return std::span<T>(data_ptr_ + offset, index_ - offset);
	}
	[[nodiscard]] constexpr std::span<const T> subspan(size_t offset) const noexcept
	{
		if (offset >= index_) [[unlikely]] { return std::span<const T>(); }
		return std::span<const T>(data_ptr_ + offset, index_ - offset);
	}
	[[nodiscard]] constexpr std::span<T> first(size_t n) noexcept
	{
		return std::span<T>(data_ptr_, n > index_ ? index_ : n);
	}
	[[nodiscard]] constexpr std::span<const T> first(size_t n) const noexcept
	{
		return std::span<const T>(data_ptr_, n > index_ ? index_ : n);
	}
	[[nodiscard]] constexpr std::span<T> last(size_t n) noexcept
	{
		return std::span<T>(n > index_ ? data_ptr_ : data_ptr_ + index_ - n,
		                    n > index_ ? index_ : n);
	}
	[[nodiscard]] constexpr std::span<const T> last(size_t n) const noexcept
	{
		return std::span<const T>(n > index_ ? data_ptr_ : data_ptr_ + index_ - n,
		                          n > index_ ? index_ : n);
	}
	// 编译期固定长度版本 (NTTP)
	template <size_t N>
	[[nodiscard]] constexpr std::span<T, N> first_fixed() noexcept
	{
		return std::span<T, N>(data_ptr_, N);
	}
	template <size_t N>
	[[nodiscard]] constexpr std::span<const T, N> first_fixed() const noexcept
	{
		return std::span<const T, N>(data_ptr_, N);
	}
	template <size_t N>
	[[nodiscard]] constexpr std::span<T, N> last_fixed() noexcept
	{
		return std::span<T, N>(data_ptr_ + index_ - N, N);
	}
	template <size_t N>
	[[nodiscard]] constexpr std::span<const T, N> last_fixed() const noexcept
	{
		return std::span<const T, N>(data_ptr_ + index_ - N, N);
	}

	// B. 反向视图
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
	DENSE_FLATTEN void reverse_for_each(F&& f) noexcept
	{
		T* DENSE_RESTRICT p = std::assume_aligned<alignof(T)>(data_ptr_) + index_;
		T* DENSE_RESTRICT b = std::assume_aligned<alignof(T)>(data_ptr_);
		while (p != b)
		{
			--p;
			f(*p);
		}
	}
	template <typename F>
	DENSE_FLATTEN void reverse_for_each(F&& f) const noexcept
	{
		const T* DENSE_RESTRICT p = std::assume_aligned<alignof(T)>(data_ptr_) + index_;
		const T* DENSE_RESTRICT b = std::assume_aligned<alignof(T)>(data_ptr_);
		while (p != b)
		{
			--p;
			f(*p);
		}
	}

	// C. 步进视图 (稀疏采样)
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
	DENSE_FLATTEN void strided_for_each(size_t start, size_t step, F&& f) noexcept
	{
		if (step == 0) [[unlikely]] { return; }
		T* p = data_ptr_ + (start > index_ ? index_ : start);
		const size_t n = (index_ - (start > index_ ? index_ : start) + step - 1) / step;
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
	DENSE_FLATTEN void strided_for_each(size_t start, size_t step, F&& f) const noexcept
	{
		if (step == 0) [[unlikely]] { return; }
		const T* p = data_ptr_ + (start > index_ ? index_ : start);
		const size_t n = (index_ - (start > index_ ? index_ : start) + step - 1) / step;
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

	// 编译期步长版本
	template <size_t Step, typename F>
	DENSE_FLATTEN void strided_for_each(F&& f) noexcept
	{
		static_assert(Step > 0, "Step must be > 0");
		if constexpr (Step == 1)
		{
			for_each(std::forward<F>(f));
			return;
		}
		T* p = data_ptr_;
		const size_t n = index_ / Step;
		for (size_t i = 0; i < n; ++i)
		{
			f(*p);
			p += Step;
		}
	}
	template <size_t Step, typename F>
	DENSE_FLATTEN void strided_for_each(F&& f) const noexcept
	{
		static_assert(Step > 0, "Step must be > 0");
		if constexpr (Step == 1)
		{
			for_each(std::forward<F>(f));
			return;
		}
		const T* p = data_ptr_;
		const size_t n = index_ / Step;
		for (size_t i = 0; i < n; ++i)
		{
			f(*p);
			p += Step;
		}
	}

	// D. 变换视图 (懒求值, 融合调用避免中间缓冲)
	template <typename FTransform, typename FConsume>
	DENSE_FLATTEN void transform_for_each(FTransform&& transform, FConsume&& consume) noexcept
	{
		T* DENSE_RESTRICT p = std::assume_aligned<alignof(T)>(data_ptr_);
		const T* DENSE_RESTRICT e = p + index_;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		while (p != e)
		{
			consume(transform(*p));
			++p;
		}
	}
	template <typename FTransform, typename FConsume>
	DENSE_FLATTEN void transform_for_each(FTransform&& transform, FConsume&& consume) const noexcept
	{
		const T* DENSE_RESTRICT p = std::assume_aligned<alignof(T)>(data_ptr_);
		const T* DENSE_RESTRICT e = p + index_;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		while (p != e)
		{
			consume(transform(*p));
			++p;
		}
	}

	// E. 过滤视图
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] T* find_if(Pred pred) noexcept
	{
		T* p = data_ptr_;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (pred(*p)) [[unlikely]] { return p; }
			++p;
		}
		return nullptr;
	}
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] const T* find_if(Pred pred) const noexcept
	{
		const T* p = data_ptr_;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (pred(*p)) [[unlikely]] { return p; }
			++p;
		}
		return nullptr;
	}
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] T* find_if_not(Pred pred) noexcept
	{
		T* p = data_ptr_;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (!pred(*p)) [[unlikely]] { return p; }
			++p;
		}
		return nullptr;
	}
	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] const T* find_if_not(Pred pred) const noexcept
	{
		const T* p = data_ptr_;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (!pred(*p)) [[unlikely]] { return p; }
			++p;
		}
		return nullptr;
	}

	[[nodiscard]] T* find(const T& value) noexcept
	{
		if constexpr (sizeof(T) == 1 && std::is_trivially_copyable_v<T>)
		{
			void* r = std::memchr(data_ptr_,
			                     *reinterpret_cast<const unsigned char*>(&value),
			                     index_);
			return static_cast<T*>(r);
		}
		else
		{
			T* p = data_ptr_;
			const T* e = data_ptr_ + index_;
			while (p != e)
			{
				if (*p == value) [[unlikely]] { return p; }
				++p;
			}
			return nullptr;
		}
	}
	[[nodiscard]] const T* find(const T& value) const noexcept
	{
		if constexpr (sizeof(T) == 1 && std::is_trivially_copyable_v<T>)
		{
			const void* r = std::memchr(data_ptr_,
			                            *reinterpret_cast<const unsigned char*>(&value),
			                            index_);
			return static_cast<const T*>(r);
		}
		else
		{
			const T* p = data_ptr_;
			const T* e = data_ptr_ + index_;
			while (p != e)
			{
				if (*p == value) [[unlikely]] { return p; }
				++p;
			}
			return nullptr;
		}
	}

	[[nodiscard]] bool contains(const T& value) const noexcept
	{
		return find(value) != nullptr;
	}

	template <typename Pred> requires std::predicate<Pred, const T&>
	[[nodiscard]] size_t count_if(Pred pred) const noexcept
	{
		size_t c = 0;
		const T* p = data_ptr_;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (pred(*p)) [[unlikely]] { ++c; }
			++p;
		}
		return c;
	}

	template <typename Pred, typename F>
	requires std::predicate<Pred, const T&> && std::invocable<F, T&>
	DENSE_FLATTEN void filter_for_each(Pred pred, F&& f) noexcept
	{
		T* p = data_ptr_;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (pred(*p)) [[likely]] { f(*p); }
			++p;
		}
	}
	template <typename Pred, typename F>
	requires std::predicate<Pred, const T&> && std::invocable<F, const T&>
	DENSE_FLATTEN void filter_for_each(Pred pred, F&& f) const noexcept
	{
		const T* p = data_ptr_;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (pred(*p)) [[likely]] { f(*p); }
			++p;
		}
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

	// F. 规约/极值
	template <typename F, typename U = T>
	[[nodiscard]] U reduce(F&& f, U init) const noexcept
	{
		const T* p = data_ptr_;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			init = f(std::move(init), *p);
			++p;
		}
		return init;
	}

	template <typename F, typename U = T>
	[[nodiscard]] U reduce_pairwise(F&& f, U init) const noexcept
	{
		const T* p = data_ptr_;
		size_t n = index_;
		// 两两归并, 减少关键路径深度
		for (; n >= 2; n -= 2, p += 2)
		{
			init = f(std::move(init), f(p[0], p[1]));
		}
		if (n == 1)
		{
			init = f(std::move(init), p[0]);
		}
		return init;
	}

	[[nodiscard]] T* min_element() noexcept
	{
		if (index_ == 0) [[unlikely]] { return nullptr; }
		T* best = data_ptr_;
		T* p = data_ptr_ + 1;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (*p < *best) [[unlikely]] { best = p; }
			++p;
		}
		return best;
	}
	[[nodiscard]] const T* min_element() const noexcept
	{
		if (index_ == 0) [[unlikely]] { return nullptr; }
		const T* best = data_ptr_;
		const T* p = data_ptr_ + 1;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (*p < *best) [[unlikely]] { best = p; }
			++p;
		}
		return best;
	}
	[[nodiscard]] T* max_element() noexcept
	{
		if (index_ == 0) [[unlikely]] { return nullptr; }
		T* best = data_ptr_;
		T* p = data_ptr_ + 1;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (*best < *p) [[unlikely]] { best = p; }
			++p;
		}
		return best;
	}
	[[nodiscard]] const T* max_element() const noexcept
	{
		if (index_ == 0) [[unlikely]] { return nullptr; }
		const T* best = data_ptr_;
		const T* p = data_ptr_ + 1;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (*best < *p) [[unlikely]] { best = p; }
			++p;
		}
		return best;
	}
	[[nodiscard]] std::pair<T*, T*> minmax_element() noexcept
	{
		if (index_ == 0) [[unlikely]] { return {nullptr, nullptr}; }
		T* mn = data_ptr_;
		T* mx = data_ptr_;
		T* p = data_ptr_ + 1;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (*p < *mn) [[unlikely]] { mn = p; }
			else if (*mx < *p) [[unlikely]] { mx = p; }
			++p;
		}
		return {mn, mx};
	}
	[[nodiscard]] std::pair<const T*, const T*> minmax_element() const noexcept
	{
		if (index_ == 0) [[unlikely]] { return {nullptr, nullptr}; }
		const T* mn = data_ptr_;
		const T* mx = data_ptr_;
		const T* p = data_ptr_ + 1;
		const T* e = data_ptr_ + index_;
		while (p != e)
		{
			if (*p < *mn) [[unlikely]] { mn = p; }
			else if (*mx < *p) [[unlikely]] { mx = p; }
			++p;
		}
		return {mn, mx};
	}

	// 算术类型专用求和
	template <typename U = T> requires std::is_arithmetic_v<U>
	[[nodiscard]] U sum() const noexcept
	{
		U s = U{};
		const U* p = reinterpret_cast<const U*>(data_ptr_);
		const U* e = p + index_;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		while (p != e)
		{
			s += *p;
			++p;
		}
		return s;
	}

	// 点积 (要求算术类型)
	template <typename U = T> requires std::is_arithmetic_v<U>
	[[nodiscard]] U dot_product(const U* other, size_t count) const noexcept
	{
		U s = U{};
		const size_t n = count > index_ ? index_ : count;
		const U* a = reinterpret_cast<const U*>(data_ptr_);
		const U* b = other;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
		for (size_t i = 0; i < n; ++i)
		{
			s += a[i] * b[i];
		}
		return s;
	}

	// G. 窗口/分块视图 (编译期 N)
	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<T, N>>
	DENSE_FLATTEN void for_each_window(F&& f) noexcept
	{
		if (index_ < N) [[unlikely]] { return; }
		const size_t last = index_ - N + 1;
		T* p = data_ptr_;
		for (size_t i = 0; i < last; ++i)
		{
			f(std::span<T, N>(p + i, N));
		}
	}
	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<const T, N>>
	DENSE_FLATTEN void for_each_window(F&& f) const noexcept
	{
		if (index_ < N) [[unlikely]] { return; }
		const size_t last = index_ - N + 1;
		const T* p = data_ptr_;
		for (size_t i = 0; i < last; ++i)
		{
			f(std::span<const T, N>(p + i, N));
		}
	}

	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<T, N>>
	DENSE_FLATTEN void for_each_chunk(F&& f) noexcept
	{
		T* p = data_ptr_;
		const size_t full = index_ / N;
		for (size_t i = 0; i < full; ++i)
		{
			f(std::span<T, N>(p + i * N, N));
		}
	}
	template <size_t N, typename F>
	requires (N > 0) && std::invocable<F, std::span<const T, N>>
	DENSE_FLATTEN void for_each_chunk(F&& f) const noexcept
	{
		const T* p = data_ptr_;
		const size_t full = index_ / N;
		for (size_t i = 0; i < full; ++i)
		{
			f(std::span<const T, N>(p + i * N, N));
		}
	}

	template <size_t N>
	requires (N > 0)
	[[nodiscard]] constexpr std::span<T, N> window_span(size_t offset) noexcept
	{
		return std::span<T, N>(data_ptr_ + offset, N);
	}
	template <size_t N>
	requires (N > 0)
	[[nodiscard]] constexpr std::span<const T, N> window_span(size_t offset) const noexcept
	{
		return std::span<const T, N>(data_ptr_ + offset, N);
	}
	template <size_t N>
	requires (N > 0)
	[[nodiscard]] constexpr std::span<T, N> chunk_span(size_t chunk_idx) noexcept
	{
		return std::span<T, N>(data_ptr_ + chunk_idx * N, N);
	}
	template <size_t N>
	requires (N > 0)
	[[nodiscard]] constexpr std::span<const T, N> chunk_span(size_t chunk_idx) const noexcept
	{
		return std::span<const T, N>(data_ptr_ + chunk_idx * N, N);
	}

	// H. 枚举视图
	template <typename F> requires std::invocable<F, size_t, T&>
	DENSE_FLATTEN void for_each_enumerated(F&& f) noexcept
	{
		T* p = data_ptr_;
		const size_t n = index_;
		for (size_t i = 0; i < n; ++i)
		{
			f(i, p[i]);
		}
	}
	template <typename F> requires std::invocable<F, size_t, const T&>
	DENSE_FLATTEN void for_each_enumerated(F&& f) const noexcept
	{
		const T* p = data_ptr_;
		const size_t n = index_;
		for (size_t i = 0; i < n; ++i)
		{
			f(i, p[i]);
		}
	}

	// I. 双容器同步 (zip)
	template <typename U, typename F>
	requires std::invocable<F, T&, U&>
	DENSE_FLATTEN void for_each_zip(U* other, size_t count, F&& f) noexcept
	{
		const size_t n = count > index_ ? index_ : count;
		T* a = data_ptr_;
		U* b = other;
		for (size_t i = 0; i < n; ++i)
		{
			f(a[i], b[i]);
		}
	}
	template <typename U, typename F>
	requires std::invocable<F, const T&, const U&>
	DENSE_FLATTEN void for_each_zip(const U* other, size_t count, F&& f) const noexcept
	{
		const size_t n = count > index_ ? index_ : count;
		const T* a = data_ptr_;
		const U* b = other;
		for (size_t i = 0; i < n; ++i)
		{
			f(a[i], b[i]);
		}
	}
	template <typename U, typename F>
	requires std::invocable<F, T&, U&>
	DENSE_FLATTEN void for_each_zip(dense<U>& other, F&& f) noexcept
	{
		for_each_zip<U, F>(other.data(), other.size(), std::forward<F>(f));
	}
	template <typename U, typename F>
	requires std::invocable<F, const T&, const U&>
	DENSE_FLATTEN void for_each_zip(const dense<U>& other, F&& f) const noexcept
	{
		for_each_zip<U, F>(other.data(), other.size(), std::forward<F>(f));
	}
	template <typename U, typename F>
	requires std::invocable<F, T&, U&>
	DENSE_FLATTEN void for_each_zip(std::span<U> other, F&& f) noexcept
	{
		for_each_zip<U, F>(other.data(), other.size(), std::forward<F>(f));
	}
	template <typename U, typename F>
	requires std::invocable<F, const T&, const U&>
	DENSE_FLATTEN void for_each_zip(std::span<const U> other, F&& f) const noexcept
	{
		for_each_zip<U, F>(other.data(), other.size(), std::forward<F>(f));
	}

	// zip_with_to: SoA → AoS 写入目标
	template <typename U, typename R, typename F>
	requires std::invocable<F, const T&, const U&>
	void zip_with_to(R* dst, const U* other, size_t count, F&& f) const noexcept
	{
		const size_t n = count > index_ ? index_ : count;
		const T* a = data_ptr_;
		const U* b = other;
		for (size_t i = 0; i < n; ++i)
		{
			new (&dst[i]) R(f(a[i], b[i]));
		}
	}

	[[nodiscard]] bool equal(const T* other, size_t count) const noexcept
	{
		if (count != index_) { return false; }
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			return std::memcmp(data_ptr_, other, index_ * sizeof(T)) == 0;
		}
		else
		{
			const T* a = data_ptr_;
			for (size_t i = 0; i < index_; ++i)
			{
				if (!(a[i] == other[i])) { return false; }
			}
			return true;
		}
	}
	template <typename U>
	[[nodiscard]] bool equal(const dense<U>& other) const noexcept
	{
		return equal(other.data(), other.size());
	}
	template <typename U>
	[[nodiscard]] bool equal(std::span<const U> other) const noexcept
	{
		return equal(other.data(), other.size());
	}

	// J. SIMD/对齐视图
	[[nodiscard]] constexpr T* aligned_data() noexcept
	{
		constexpr size_t align = alignof(T) > cache_line ? alignof(T) : cache_line;
		return std::assume_aligned<align>(data_ptr_);
	}
	[[nodiscard]] constexpr const T* aligned_data() const noexcept
	{
		constexpr size_t align = alignof(T) > cache_line ? alignof(T) : cache_line;
		return std::assume_aligned<align>(data_ptr_);
	}

	[[nodiscard]] constexpr std::span<T> aligned_span() noexcept
	{
		constexpr size_t align = alignof(T) > cache_line ? alignof(T) : cache_line;
		return std::span<T>(std::assume_aligned<align>(data_ptr_), index_);
	}
	[[nodiscard]] constexpr std::span<const T> aligned_span() const noexcept
	{
		constexpr size_t align = alignof(T) > cache_line ? alignof(T) : cache_line;
		return std::span<const T>(std::assume_aligned<align>(data_ptr_), index_);
	}

	// SIMD 遍历: trivially copyable 专用, 32B 块遍历 + 标量 tail
	template <typename F> requires std::is_trivially_copyable_v<T> && std::invocable<F, T&>
	DENSE_FLATTEN void simd_for_each(F&& f) noexcept
	{
		constexpr size_t block_bytes = 32;
		constexpr size_t block_elems = block_bytes / sizeof(T);
		static_assert(sizeof(T) <= block_bytes, "T too large for simd_for_each");
		T* p = data_ptr_;
		const size_t n = index_;
		const size_t full = (block_elems > 0) ? (n / block_elems) : 0;
#if defined(__AVX2__)
		for (size_t i = 0; i < full; ++i)
		{
			if (i + 8 < full) [[likely]] { DENSE_PREFETCH_R(p + 8 * block_elems); }
			for (size_t k = 0; k < block_elems; ++k) { f(p[k]); }
			p += block_elems;
		}
#else
		// ARM64 / 无 AVX: 标量回退, 仍按块组织便于寄存器复用
		for (size_t i = 0; i < full; ++i)
		{
			for (size_t k = 0; k < block_elems; ++k) { f(p[k]); }
			p += block_elems;
		}
#endif
		const size_t tail = n - full * block_elems;
		for (size_t k = 0; k < tail; ++k) { f(p[k]); }
	}
	template <typename F> requires std::is_trivially_copyable_v<T> && std::invocable<F, const T&>
	DENSE_FLATTEN void simd_for_each(F&& f) const noexcept
	{
		constexpr size_t block_bytes = 32;
		constexpr size_t block_elems = block_bytes / sizeof(T);
		static_assert(sizeof(T) <= block_bytes, "T too large for simd_for_each");
		const T* p = data_ptr_;
		const size_t n = index_;
		const size_t full = (block_elems > 0) ? (n / block_elems) : 0;
		for (size_t i = 0; i < full; ++i)
		{
			if (i + 8 < full) [[likely]] { DENSE_PREFETCH_R(p + 8 * block_elems); }
			for (size_t k = 0; k < block_elems; ++k) { f(p[k]); }
			p += block_elems;
		}
		const size_t tail = n - full * block_elems;
		for (size_t k = 0; k < tail; ++k) { f(p[k]); }
	}

	// SIMD 块拷贝: trivially copyable 专用
	[[nodiscard]] constexpr size_t unaligned_tail_offset() const noexcept
	{
		constexpr size_t block_elems = 32 / sizeof(T);
		return (block_elems > 0) ? (index_ % block_elems) : 0;
	}

	// K. 拷贝/移动视图
	void copy_to(T* dst, size_t count) noexcept
	{
		const size_t n = count > index_ ? index_ : count;
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			if (n > 0) [[likely]] { copy_trivial_data(dst, data_ptr_, n); }
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[i]) T(data_ptr_[i]);
			}
		}
	}
	void copy_to(std::span<T> dst) noexcept { copy_to(dst.data(), dst.size()); }

	void move_to(T* dst, size_t count) noexcept
	{
		const size_t n = count > index_ ? index_ : count;
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			if (n > 0) [[likely]]
			{
				std::memcpy(dst, data_ptr_, n * sizeof(T));
			}
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[i]) T(std::move(data_ptr_[i]));
			}
		}
	}
	void move_to(std::span<T> dst) noexcept { move_to(dst.data(), dst.size()); }

	void reverse_copy_to(T* dst, size_t count) noexcept
	{
		const size_t n = count > index_ ? index_ : count;
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			// 反向 memmove 防止重叠
			for (size_t i = 0; i < n; ++i)
			{
				std::memcpy(&dst[n - 1 - i], &data_ptr_[i], sizeof(T));
			}
		}
		else
		{
			for (size_t i = 0; i < n; ++i)
			{
				new (&dst[n - 1 - i]) T(std::move(data_ptr_[i]));
			}
		}
	}
	void reverse_copy_to(std::span<T> dst) noexcept { reverse_copy_to(dst.data(), dst.size()); }

	// transform_to: 写入目标, 融合 transform
	template <typename R, typename F>
	requires std::invocable<F, const T&>
	void transform_to(R* dst, size_t count, F&& transform) const noexcept
	{
		const size_t n = count > index_ ? index_ : count;
		const T* p = data_ptr_;
		for (size_t i = 0; i < n; ++i)
		{
			new (&dst[i]) R(transform(p[i]));
		}
	}
};

template <typename T>
void swap(dense<T>& a, dense<T>& b) noexcept {
	a.swap(b);
}
