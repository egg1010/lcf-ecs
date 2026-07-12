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
#if defined(__AVX2__) || defined(__BMI__) || defined(_MSC_VER)
#include <immintrin.h>
#endif

// Cross-platform prefetch macro (MSVC + Clang/GCC)
#ifdef _MSC_VER
#include <intrin.h>
#define PREFETCH_R(ptr) _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0)
#else
#define PREFETCH_R(ptr) __builtin_prefetch(ptr, 0, 3)
#endif

// 清除最低设置位 (BMI1 BLSR 指令, 不可用时回退到标量)
[[nodiscard]] static inline uint64_t clear_lowest_bit(uint64_t x) noexcept
{
#if defined(__BMI__) || defined(_MSC_VER)
    return _blsr_u64(x);
#else
    return x & (x - 1);
#endif
}

template <typename T>
class class_pool
{
	friend class single_class_set;

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

	static void bitmap_shift_right_one(uint64_t* bits, size_t start, size_t end) noexcept {
		if (start >= end) { return; }

		const size_t start_word = start / BITS_PER_WORD;
		const size_t start_bit  = start % BITS_PER_WORD;
		const size_t end_word   = end / BITS_PER_WORD;
		const size_t end_bit    = end % BITS_PER_WORD;

		if (start_word == end_word) [[unlikely]] {
			uint64_t word = bits[start_word];
			uint64_t mask = ((1ull << (end_bit + 1)) - 1) ^ ((1ull << start_bit) - 1);
			uint64_t preserve_below = word & ((1ull << start_bit) - 1);
			uint64_t preserve_above = word & ~((1ull << (end_bit + 1)) - 1);
			uint64_t start_bit_val = word & (1ull << start_bit);
			uint64_t range = word & mask;
			uint64_t shifted = ((range << 1) & mask) | start_bit_val;
			bits[start_word] = preserve_below | shifted | preserve_above;
			return;
		}

		uint64_t carry;
		{
			uint64_t word = bits[start_word];
			carry = (word >> 63) & 1ull;
			uint64_t mask = ~((start_bit == 0) ? 0ull : ((1ull << start_bit) - 1));
			uint64_t in_range = word & mask;
			uint64_t shifted = (in_range << 1) & mask;
			uint64_t start_bit_val = word & (1ull << start_bit);
			bits[start_word] = (word & ~mask) | shifted | start_bit_val;
		}

		for (size_t w = start_word + 1; w < end_word; ++w) {
			uint64_t word = bits[w];
			uint64_t new_carry = (word >> 63) & 1ull;
			bits[w] = (word << 1) | carry;
			carry = new_carry;
		}

		if (end_bit == 0) [[unlikely]] {
			if (carry) { bits[end_word] |= 1ull; }
			else       { bits[end_word] &= ~1ull; }
		}
		else {
			uint64_t word = bits[end_word];
			uint64_t mask = (1ull << end_bit) - 1;
			uint64_t in_range = word & mask;
			uint64_t shifted = in_range << 1;
			if (carry) { shifted |= 1ull; }
			uint64_t clear_mask = (end_bit < 63) ? ((1ull << (end_bit + 1)) - 1) : ~0ull;
			bits[end_word] = (word & ~clear_mask) | (shifted & clear_mask);
		}
	}

	static void bitmap_zero_words(uint64_t* bits, size_t start, size_t end) noexcept {
		size_t w = start;
#ifdef __AVX2__
		const __m256i zero = _mm256_setzero_si256();
		for (; w + 4 <= end; w += 4)
		{
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(bits + w), zero);
		}
#endif
		for (; w < end; ++w)
		{
			bits[w] = 0;
		}
	}

	static void bitmap_shift_left(uint64_t* bits, size_t start, size_t end, size_t shift) noexcept {
		if (shift == 0 || start >= end) { return; }
		const size_t new_end = end - shift;
		if (start >= new_end) {
			const size_t start_word = start / BITS_PER_WORD;
			const size_t start_bit = start % BITS_PER_WORD;
			const size_t end_word = end / BITS_PER_WORD;
			const size_t end_bit = end % BITS_PER_WORD;

			if (start_word == end_word) {
				uint64_t mask = ((end_bit == 0 ? 0ull : (1ull << end_bit)) - (1ull << start_bit));
				bits[start_word] &= ~mask;
			}
			else {
				if (start_bit != 0) {
					bits[start_word] &= (1ull << start_bit) - 1;
				}
				bitmap_zero_words(bits, start_word + 1, end_word);
				if (end_bit != 0) {
					bits[end_word] &= ~((1ull << end_bit) - 1);
				}
			}
			return;
		}

		const size_t shift_words = shift / BITS_PER_WORD;
		const size_t shift_bits = shift % BITS_PER_WORD;

		const size_t first_dst_word = start / BITS_PER_WORD;
		const size_t first_dst_bit = start % BITS_PER_WORD;
		const size_t last_dst_word = (new_end - 1) / BITS_PER_WORD;
		const size_t last_dst_bit = (new_end - 1) % BITS_PER_WORD;

		if (shift_bits == 0) {
			if (first_dst_word == last_dst_word) {
				uint64_t low_mask = (1ull << first_dst_bit) - 1;
				uint64_t high_mask = ~((1ull << (last_dst_bit + 1)) - 1);
				uint64_t mid_mask = ~(low_mask | high_mask);
				bits[first_dst_word] = (bits[first_dst_word] & (low_mask | high_mask)) |
				                       (bits[first_dst_word + shift_words] & mid_mask);
			}
			else {
				if (first_dst_bit != 0) {
					uint64_t low_mask = (1ull << first_dst_bit) - 1;
					bits[first_dst_word] = (bits[first_dst_word] & low_mask) |
					                       (bits[first_dst_word + shift_words] & ~low_mask);
				}
				else {
					bits[first_dst_word] = bits[first_dst_word + shift_words];
				}
				{
				// 左移: 数据从高地址移向低地址, 必须向上迭代 (先写低, 后读高)
				// 原向下迭代在 shift_words∈[1,7] 时 AVX2 读取范围与上一轮写入范围重叠, 导致数据损坏
				size_t dw = first_dst_word + 1;
#ifdef __AVX2__
				if (shift_words >= 4)
				{
					for (; dw + 3 <= last_dst_word; dw += 4)
					{
						__m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bits + dw + shift_words));
						_mm256_storeu_si256(reinterpret_cast<__m256i*>(bits + dw), v);
					}
				}
#endif
				for (; dw <= last_dst_word; ++dw) {
					bits[dw] = bits[dw + shift_words];
				}
			}
				if (last_dst_bit != BITS_PER_WORD - 1) {
					uint64_t high_mask = ~((1ull << (last_dst_bit + 1)) - 1);
					bits[last_dst_word] = (bits[last_dst_word] & high_mask) |
					                      (bits[last_dst_word + shift_words] & ~high_mask);
				}
			}
		}
		else {
			if (shift_words > 0) {
				// 向上迭代: 先写低地址 (destination), 读高地址 (source 未被覆盖)
				for (size_t dw = first_dst_word; dw <= last_dst_word; ++dw) {
					size_t dst_bit_offset = (dw == first_dst_word) ? first_dst_bit : 0;
					size_t bits_in_dst_word = (dw == last_dst_word)
						? ((new_end - 1) % BITS_PER_WORD + 1 - dst_bit_offset)
						: (BITS_PER_WORD - dst_bit_offset);

					size_t src_pos = dw * BITS_PER_WORD + dst_bit_offset + shift_words * BITS_PER_WORD;
					size_t sw = src_pos / BITS_PER_WORD;
					size_t sb = src_pos % BITS_PER_WORD;

					uint64_t val = 0;
					size_t bits_read = 0;
					while (bits_read < bits_in_dst_word) {
						size_t avail = BITS_PER_WORD - sb;
						size_t to_read = std::min(avail, bits_in_dst_word - bits_read);
						uint64_t chunk = bits[sw] >> sb;
						if (to_read < BITS_PER_WORD) {
							chunk &= (1ull << to_read) - 1;
						}
						val |= chunk << bits_read;
						bits_read += to_read;
						sb = 0;
						++sw;
					}

					uint64_t mask = (bits_in_dst_word == BITS_PER_WORD)
						? ~0ull
						: ((1ull << bits_in_dst_word) - 1);
					bits[dw] = (bits[dw] & ~(mask << dst_bit_offset)) | ((val & mask) << dst_bit_offset);
				}
			}

			// bit 级移位: carry 从高字传向低字 (左移数据向低索引移动)
			uint64_t carry = 0;
			for (size_t w = last_dst_word + 1; w > first_dst_word; )
			{
				--w;
				uint64_t word = bits[w];
				uint64_t shifted = (word >> shift_bits) | carry;
				carry = word << (BITS_PER_WORD - shift_bits);

				if (w == first_dst_word && first_dst_bit != 0) {
					uint64_t low_mask = (1ull << first_dst_bit) - 1;
					bits[w] = (word & low_mask) | (shifted & ~low_mask);
				}
				else {
					bits[w] = shifted;
				}
			}
		}

		{
			const size_t clear_start_word = new_end / BITS_PER_WORD;
			const size_t clear_start_bit = new_end % BITS_PER_WORD;
			const size_t clear_end_word = end / BITS_PER_WORD;
			const size_t clear_end_bit = end % BITS_PER_WORD;

			if (clear_start_word == clear_end_word) {
				uint64_t mask = ((clear_end_bit == 0 ? 0ull : (1ull << clear_end_bit)) - (1ull << clear_start_bit));
				bits[clear_start_word] &= ~mask;
			}
			else {
				if (clear_start_bit != 0) {
					bits[clear_start_word] &= (1ull << clear_start_bit) - 1;
				}
				bitmap_zero_words(bits, clear_start_word + 1, clear_end_word);
				if (clear_end_bit != 0) {
					bits[clear_end_word] &= ~((1ull << clear_end_bit) - 1);
				}
			}
		}
	}

	void destroy_dense_range(size_t first, size_t last) noexcept {
		if constexpr (!std::is_trivially_destructible_v<T>) {
			if (first < last) {
				std::destroy_n(data_ptr_ + first, last - first);
			}
		}
	}

	void destroy_sparse_range(size_t first, size_t last) noexcept {
		if constexpr (std::is_trivially_destructible_v<T>) { return; }
		if (first >= last) { return; }

		const size_t start_word = first / BITS_PER_WORD;
		const size_t start_bit = first % BITS_PER_WORD;
		const size_t end_word = last / BITS_PER_WORD;
		const size_t end_bit = last % BITS_PER_WORD;

		if (start_word == end_word) {
			uint64_t mask = (end_bit == 0 ? 0ull : ((1ull << end_bit) - 1)) & ~((1ull << start_bit) - 1);
			uint64_t word = sparse_bits_[start_word] & mask;
			while (word != 0) {
				const size_t offset = std::countr_zero(word);
				data_ptr_[start_word * BITS_PER_WORD + offset].~T();
				word = clear_lowest_bit(word);
			}
			return;
		}

		if (start_bit != 0) {
			uint64_t word = sparse_bits_[start_word] & ~((1ull << start_bit) - 1);
			while (word != 0) {
				const size_t offset = std::countr_zero(word);
				data_ptr_[start_word * BITS_PER_WORD + offset].~T();
				word = clear_lowest_bit(word);
			}
		}

		for (size_t w = start_word + (start_bit != 0 ? 1 : 0); w < end_word; ++w) {
			if (w + 4 < end_word) {
				PREFETCH_R(&sparse_bits_[w + 4]);
			}
			uint64_t word = sparse_bits_[w];
			while (word != 0) {
				const size_t offset = std::countr_zero(word);
				data_ptr_[w * BITS_PER_WORD + offset].~T();
				word = clear_lowest_bit(word);
			}
		}

		if (end_bit != 0) {
			uint64_t word = sparse_bits_[end_word] & ((1ull << end_bit) - 1);
			while (word != 0) {
				const size_t offset = std::countr_zero(word);
				data_ptr_[end_word * BITS_PER_WORD + offset].~T();
				word = clear_lowest_bit(word);
			}
		}
	}

	void destroy_all() noexcept {
		if constexpr (std::is_trivially_destructible_v<T>) { return; }
		if (index_ == 0 || sparse_bits_ == nullptr) { return; }

		if (is_dense()) [[likely]] {
			destroy_dense_range(0, index_);
		}
		else {
			destroy_sparse_range(0, index_);
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

	template <bool MoveAndDestroy>
	static void relocate_sparse(T* dst, const T* src, const uint64_t* src_bits, size_t count) noexcept {
		const size_t num_full_words = count / BITS_PER_WORD;
		constexpr size_t pf_data_offset = 4 * BITS_PER_WORD;
		for (size_t w = 0; w < num_full_words; ++w) {
			if (w + 4 < num_full_words) {
				PREFETCH_R(&src_bits[w + 4]);
				const size_t pf_idx = (w + 4) * BITS_PER_WORD;
				if (pf_idx < count) {
					PREFETCH_R(&src[pf_idx]);
					PREFETCH_R(&dst[pf_idx]);
				}
			}
			uint64_t word = src_bits[w];
			while (word != 0) {
				const size_t offset = std::countr_zero(word);
				const size_t i = w * BITS_PER_WORD + offset;
				if constexpr (MoveAndDestroy) {
					new (&dst[i]) T(std::move(const_cast<T&>(src[i])));
					const_cast<T&>(src[i]).~T();
				}
				else {
					new (&dst[i]) T(src[i]);
				}
				word = clear_lowest_bit(word);
			}
		}
		const size_t tail = count % BITS_PER_WORD;
		if (tail != 0) {
			uint64_t word = src_bits[num_full_words] & ((1ull << tail) - 1);
			while (word != 0) {
				const size_t offset = std::countr_zero(word);
				const size_t i = num_full_words * BITS_PER_WORD + offset;
				if constexpr (MoveAndDestroy) {
					new (&dst[i]) T(std::move(const_cast<T&>(src[i])));
					const_cast<T&>(src[i]).~T();
				}
				else {
					new (&dst[i]) T(src[i]);
				}
				word = clear_lowest_bit(word);
			}
		}
	}

	static void copy_trivial_data(T* dst, const T* src, size_t count) noexcept {
		const size_t bytes = count * sizeof(T);
#ifdef __AVX2__
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
#endif
		if (bytes != 0) [[likely]]
		{
			std::memcpy(std::assume_aligned<alignof(T)>(dst),
			            std::assume_aligned<alignof(T)>(src),
			            bytes);
		}
	}

	void grow_data_and_bitmap(size_t new_capacity) noexcept {
		if (new_capacity <= maximum_quantity_) [[likely]] {
			return;
		}

		T* new_data = allocate_data(new_capacity);
		uint64_t* new_bits = obtain_bitmap(new_capacity);
		const bool was_inline = is_inline_bitmap();

		if (data_ptr_ != nullptr) [[likely]] {
			if constexpr (std::is_trivially_copyable_v<T>) {
				copy_trivial_data(new_data, data_ptr_, index_);
			}
			else if (is_dense()) {
				uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
			}
			else {
				relocate_sparse<true>(new_data, data_ptr_, sparse_bits_, index_);
			}
			copy_bitmap(new_bits, sparse_bits_, maximum_quantity_);
			deallocate_data(data_ptr_, maximum_quantity_);
			if (!was_inline) {
				deallocate_bitmap(sparse_bits_, maximum_quantity_);
			}
		}

		data_ptr_ = new_data;
		sparse_bits_ = new_bits;
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
		using iterator_category = std::forward_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = Ptr;
		using reference = Ref;

		basic_iterator() noexcept : ptr_(nullptr), end_(nullptr), bits_(nullptr), origin_(nullptr) {}

		// MinGW AVX2 codegen 变通: 标量拷贝构造防止 256-bit 结构体复制的 codegen bug
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
				PREFETCH_R(ptr_ + pf_dist);
			}
			return *this;
		}

		basic_iterator operator++(int) noexcept {
			basic_iterator tmp = *this;
			++*this;
			return tmp;
		}

		bool operator==(const basic_iterator& other) const noexcept { return ptr_ == other.ptr_; }
		bool operator!=(const basic_iterator& other) const noexcept { return ptr_ != other.ptr_; }
	};

	using iterator = basic_iterator<false>;
	using const_iterator = basic_iterator<true>;

	constexpr class_pool() noexcept = default;

	explicit class_pool(size_t capacity) noexcept
		: data_ptr_(nullptr)
		, sparse_bits_(nullptr)
		, maximum_quantity_(capacity)
		, index_(0) {
		if (capacity > 0) [[likely]] {
			data_ptr_ = allocate_data(capacity);
			sparse_bits_ = obtain_bitmap(capacity);
		}
	}

	class_pool(size_t count, const T& value) noexcept
		: data_ptr_(nullptr)
		, sparse_bits_(nullptr)
		, maximum_quantity_(round_up_to_default(count))
		, index_(0) {
		if (count > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			sparse_bits_ = obtain_bitmap(maximum_quantity_);
			for (size_t i = 0; i < count; ++i) {
				new (&data_ptr_[i]) T(value);
				bitmap_set(sparse_bits_, i);
			}
			index_ = count;
		}
	}

	template <typename InputIt>
	class_pool(InputIt first, InputIt last) noexcept
		: data_ptr_(nullptr)
		, sparse_bits_(nullptr)
		, maximum_quantity_(0)
		, index_(0) {
		const size_t count = static_cast<size_t>(std::distance(first, last));
		maximum_quantity_ = round_up_to_default(count);

		if (count > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			sparse_bits_ = obtain_bitmap(maximum_quantity_);
			size_t i = 0;
			for (auto it = first; it != last; ++it, ++i) {
				new (&data_ptr_[i]) T(*it);
				bitmap_set(sparse_bits_, i);
			}
			index_ = count;
		}
	}

	class_pool(std::initializer_list<T> init) noexcept
		: class_pool(init.begin(), init.end()) {}

	class_pool(const class_pool& other) noexcept
		: data_ptr_(nullptr)
		, sparse_bits_(nullptr)
		, maximum_quantity_(other.maximum_quantity_)
		, index_(other.index_)
		, is_dense_(other.is_dense_)
		, count_cache_(other.count_cache_)
		, hole_count_(other.hole_count_)
		, inline_bits_(other.inline_bits_) {
		if (maximum_quantity_ > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			if (other.is_inline_bitmap()) {
				sparse_bits_ = &inline_bits_;
			}
			else {
				sparse_bits_ = allocate_bitmap(maximum_quantity_);
			}

			if constexpr (std::is_trivially_copyable_v<T>) {
				const size_t bytes = other.index_ * sizeof(T);
				if (bytes != 0) [[likely]] {
					std::memcpy(std::assume_aligned<alignof(T)>(data_ptr_),
					            std::assume_aligned<alignof(T)>(other.data_ptr_),
					            bytes);
				}
			}
			else if (other.is_dense_) [[likely]] {
				std::uninitialized_copy(other.data_ptr_, other.data_ptr_ + other.index_, data_ptr_);
			}
			else {
				relocate_sparse<false>(data_ptr_, other.data_ptr_, other.sparse_bits_, other.index_);
			}
			copy_bitmap(sparse_bits_, other.sparse_bits_, other.maximum_quantity_);
		}
	}

	class_pool(class_pool&& other) noexcept
		: data_ptr_(other.data_ptr_)
		, sparse_bits_(other.sparse_bits_)
		, maximum_quantity_(other.maximum_quantity_)
		, index_(other.index_)
		, is_dense_(other.is_dense_)
		, count_cache_(other.count_cache_)
		, hole_count_(other.hole_count_)
		, inline_bits_(other.inline_bits_) {
		if (other.is_inline_bitmap()) {
			sparse_bits_ = &inline_bits_;
		}
		other.data_ptr_ = nullptr;
		other.sparse_bits_ = nullptr;
		other.maximum_quantity_ = 0;
		other.index_ = 0;
		other.is_dense_ = true;
		other.count_cache_ = static_cast<size_t>(-1);
		other.hole_count_ = 0;
		other.inline_bits_ = 0;
	}

	class_pool& operator=(const class_pool& other) noexcept {
		if (this == &other) [[unlikely]] { return *this; }
		destroy_all();
		release_bitmap();
		deallocate_data(data_ptr_, maximum_quantity_);

		maximum_quantity_ = other.maximum_quantity_;
		index_ = other.index_;
		is_dense_ = other.is_dense_;
		count_cache_ = other.count_cache_;
		hole_count_ = other.hole_count_;
		inline_bits_ = other.inline_bits_;

		if (maximum_quantity_ > 0) [[likely]] {
			data_ptr_ = allocate_data(maximum_quantity_);
			if (other.is_inline_bitmap()) {
				sparse_bits_ = &inline_bits_;
			}
			else {
				sparse_bits_ = allocate_bitmap(maximum_quantity_);
			}

			if constexpr (std::is_trivially_copyable_v<T>) {
				const size_t bytes = other.index_ * sizeof(T);
				if (bytes != 0) [[likely]] {
					std::memcpy(std::assume_aligned<alignof(T)>(data_ptr_),
					            std::assume_aligned<alignof(T)>(other.data_ptr_),
					            bytes);
				}
			}
			else if (other.is_dense_) [[likely]] {
				std::uninitialized_copy(other.data_ptr_, other.data_ptr_ + other.index_, data_ptr_);
			}
			else {
				relocate_sparse<false>(data_ptr_, other.data_ptr_, other.sparse_bits_, other.index_);
			}
			copy_bitmap(sparse_bits_, other.sparse_bits_, other.maximum_quantity_);
		}
		else {
			data_ptr_ = nullptr;
			sparse_bits_ = nullptr;
		}
		return *this;
	}

	class_pool& operator=(class_pool&& other) noexcept {
		if (this == &other) [[unlikely]] { return *this; }
		destroy_all();
		release_bitmap();
		deallocate_data(data_ptr_, maximum_quantity_);

		data_ptr_ = other.data_ptr_;
		maximum_quantity_ = other.maximum_quantity_;
		index_ = other.index_;
		is_dense_ = other.is_dense_;
		count_cache_ = other.count_cache_;
		hole_count_ = other.hole_count_;
		inline_bits_ = other.inline_bits_;

		if (other.is_inline_bitmap()) {
			sparse_bits_ = &inline_bits_;
		}
		else {
			sparse_bits_ = other.sparse_bits_;
		}

		other.data_ptr_ = nullptr;
		other.sparse_bits_ = nullptr;
		other.maximum_quantity_ = 0;
		other.index_ = 0;
		other.is_dense_ = true;
		other.count_cache_ = static_cast<size_t>(-1);
		other.hole_count_ = 0;
		other.inline_bits_ = 0;
		return *this;
	}

	~class_pool() noexcept {
		destroy_all();
		release_bitmap();
		deallocate_data(data_ptr_, maximum_quantity_);
	}

	template <typename... Args>
	inline void emplace_back(Args&&... args) noexcept {
		invalidate_count_cache();
		static_assert(std::is_constructible_v<T, Args...>,
		             "T must be constructible from the provided arguments");

		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
		}
		new (&data_ptr_[index_]) T(std::forward<Args>(args)...);
		if (is_dense_) [[likely]]
		{
			sparse_bits_[index_ / BITS_PER_WORD] |= (1ull << (index_ % BITS_PER_WORD));
		}
		else
		{
			bitmap_set(sparse_bits_, index_);
			--hole_count_;
		}
		++index_;
	}

	inline void push_back_unchecked(const T& value) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
		}
		new (&data_ptr_[index_]) T(value);
		++index_;
	}

	template <typename... Args>
	inline void emplace_back_unchecked(Args&&... args) noexcept {
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
		}
		new (&data_ptr_[index_]) T(std::forward<Args>(args)...);
		++index_;
	}

	template <typename... Args>
	inline void emplace_back_dense_unchecked(Args&&... args) noexcept {
		invalidate_count_cache();
		new (&data_ptr_[index_]) T(std::forward<Args>(args)...);
		sparse_bits_[index_ / BITS_PER_WORD] |= (1ull << (index_ % BITS_PER_WORD));
		++index_;
	}

	inline void clear() noexcept {
		destroy_all();
		invalidate_count_cache();
		if (sparse_bits_ != nullptr && maximum_quantity_ > 0) [[likely]] {
			const size_t words = bitmap_word_count(maximum_quantity_);
			std::memset(std::assume_aligned<alignof(uint64_t)>(sparse_bits_), 0,
			            words * sizeof(uint64_t));
		}
		index_ = 0;
		is_dense_ = true;
		hole_count_ = 0;
	}

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

	[[nodiscard]] size_type count() const noexcept {
		if (count_cache_ != static_cast<size_t>(-1)) [[likely]] {
			return count_cache_;
		}
		if (is_dense_) [[likely]] {
			count_cache_ = index_;
			return index_;
		}
		if (sparse_bits_ == nullptr || index_ == 0) { count_cache_ = 0; return 0; }
		const size_t full_words = index_ / BITS_PER_WORD;
		size_type total = 0;
		size_t i = 0;

#if defined(__AVX512VPOPCNTDQ__)
		for (; i + 8 <= full_words; i += 8)
		{
			__m512i v = _mm512_loadu_si512(sparse_bits_ + i);
			total += _mm512_reduce_add_epi64(_mm512_popcnt_epi64(v));
		}
#elif defined(__AVX2__)
		{
			const __m256i lut = _mm256_setr_epi8(
				0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,
				0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4);
			const __m256i nibble_mask = _mm256_set1_epi8(0x0F);
			for (; i + 4 <= full_words; i += 4)
			{
				__m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sparse_bits_ + i));
				__m256i lo = _mm256_and_si256(v, nibble_mask);
				__m256i hi = _mm256_and_si256(_mm256_srli_epi16(v, 4), nibble_mask);
				__m256i cnt = _mm256_add_epi8(_mm256_shuffle_epi8(lut, lo), _mm256_shuffle_epi8(lut, hi));
				__m256i sad = _mm256_sad_epu8(cnt, _mm256_setzero_si256());
				total += static_cast<size_t>(_mm256_extract_epi64(sad, 0));
				total += static_cast<size_t>(_mm256_extract_epi64(sad, 1));
				total += static_cast<size_t>(_mm256_extract_epi64(sad, 2));
				total += static_cast<size_t>(_mm256_extract_epi64(sad, 3));
			}
		}
#endif
		for (; i < full_words; ++i) {
			total += std::popcount(sparse_bits_[i]);
		}
		const size_t tail = index_ % BITS_PER_WORD;
		if (tail != 0) {
			const uint64_t mask = (1ull << tail) - 1;
			total += std::popcount(sparse_bits_[full_words] & mask);
		}
		count_cache_ = total;
		return total;
	}

	void invalidate_count_cache() noexcept {
		count_cache_ = static_cast<size_t>(-1);
	}

	[[nodiscard]] constexpr pointer data() noexcept {
		return std::assume_aligned<alignof(T)>(data_ptr_);
	}

	[[nodiscard]] constexpr const_pointer data() const noexcept {
		return std::assume_aligned<alignof(T)>(data_ptr_);
	}

	inline void increase_capacity(size_t new_capacity) noexcept {
		if (new_capacity > maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_growth_for_reserve(new_capacity));
		}
	}

	inline void increase_capacity(size_t new_capacity, const T& value) noexcept {
		invalidate_count_cache();
		if (new_capacity <= index_) [[likely]] {
			return;
		}

		if (new_capacity > maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_growth_for_reserve(new_capacity));
		}

		if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8) {
			for (size_t i = index_; i < new_capacity; ++i) {
				std::memcpy(&data_ptr_[i], &value, sizeof(T));
				bitmap_set(sparse_bits_, i);
			}
		}
		else {
			for (size_t i = index_; i < new_capacity; ++i) {
				new (&data_ptr_[i]) T(value);
				bitmap_set(sparse_bits_, i);
			}
		}
		index_ = new_capacity;
	}

	inline void shrink_to_fit() noexcept {
		if (index_ == 0 && data_ptr_ != nullptr) [[unlikely]] {
			release_bitmap();
			deallocate_data(data_ptr_, maximum_quantity_);
			data_ptr_ = nullptr;
			sparse_bits_ = nullptr;
			maximum_quantity_ = 0;
			hole_count_ = 0;
			return;
		}

		if (index_ < maximum_quantity_ && index_ > 0) [[likely]] {
			T* new_data = allocate_data(index_);
			uint64_t* new_bits = obtain_bitmap(index_);
			const bool was_inline = is_inline_bitmap();

			if constexpr (std::is_trivially_copyable_v<T>) {
				copy_trivial_data(new_data, data_ptr_, index_);
			}
			else if (is_dense()) {
				uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
			}
			else {
				relocate_sparse<true>(new_data, data_ptr_, sparse_bits_, index_);
			}
			copy_bitmap(new_bits, sparse_bits_, index_);

			deallocate_data(data_ptr_, maximum_quantity_);
			if (!was_inline) {
				deallocate_bitmap(sparse_bits_, maximum_quantity_);
			}

			data_ptr_ = new_data;
			sparse_bits_ = new_bits;
			maximum_quantity_ = index_;
		}
	}

	inline void reduce_capacity(size_t new_capacity) noexcept {
		invalidate_count_cache();
		if (new_capacity >= maximum_quantity_) [[likely]] {
			return;
		}

		if (new_capacity == 0) {
			destroy_all();
			release_bitmap();
			deallocate_data(data_ptr_, maximum_quantity_);
			data_ptr_ = nullptr;
			sparse_bits_ = nullptr;
			maximum_quantity_ = 0;
			index_ = 0;
			is_dense_ = true;
			hole_count_ = 0;
			return;
		}

		if (new_capacity < index_) {
			destroy_dense_range(new_capacity, index_);
			for (size_t i = new_capacity; i < index_; ++i) {
				bitmap_reset(sparse_bits_, i);
			}
			index_ = new_capacity;
			recompute_is_dense();
		}

		T* new_data = allocate_data(new_capacity);
		uint64_t* new_bits = obtain_bitmap(new_capacity);
		const bool was_inline = is_inline_bitmap();

		if constexpr (std::is_trivially_copyable_v<T>) {
			const size_t bytes = index_ * sizeof(T);
			if (bytes != 0) [[likely]] {
				std::memcpy(std::assume_aligned<alignof(T)>(new_data),
				            std::assume_aligned<alignof(T)>(data_ptr_),
				            bytes);
			}
		}
		else if (is_dense()) {
			uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
		}
		else {
			relocate_sparse<true>(new_data, data_ptr_, sparse_bits_, index_);
		}
		copy_bitmap(new_bits, sparse_bits_, new_capacity);

		deallocate_data(data_ptr_, maximum_quantity_);
		if (!was_inline) {
			deallocate_bitmap(sparse_bits_, maximum_quantity_);
		}

		data_ptr_ = new_data;
		sparse_bits_ = new_bits;
		maximum_quantity_ = new_capacity;
	}

	inline void reduce_capacity(size_t new_capacity, class_pool<T>& dst) noexcept {
		invalidate_count_cache();
		if (new_capacity >= index_) [[likely]] {
			return;
		}

		const size_t move_count = index_ - new_capacity;
		dst.increase_capacity(dst.size() + move_count);

		if (is_dense()) {
			for (size_t i = new_capacity; i < index_; ++i) {
				dst.emplace_back(std::move(data_ptr_[i]));
			}
		}
		else {
			for (size_t i = new_capacity; i < index_; ++i) {
				if (bitmap_test(sparse_bits_, i)) {
					dst.emplace_back(std::move(data_ptr_[i]));
				}
			}
		}

		destroy_dense_range(new_capacity, index_);
		for (size_t i = new_capacity; i < index_; ++i) {
			bitmap_reset(sparse_bits_, i);
		}
		index_ = new_capacity;
		recompute_is_dense();

		if (new_capacity < maximum_quantity_) {
			T* new_data = allocate_data(new_capacity);
			uint64_t* new_bits = obtain_bitmap(new_capacity);
			const bool was_inline = is_inline_bitmap();

			if constexpr (std::is_trivially_copyable_v<T>) {
				copy_trivial_data(new_data, data_ptr_, index_);
			}
			else if (is_dense()) {
				uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
			}
			else {
				relocate_sparse<true>(new_data, data_ptr_, sparse_bits_, index_);
			}
			copy_bitmap(new_bits, sparse_bits_, new_capacity);

			deallocate_data(data_ptr_, maximum_quantity_);
			if (!was_inline) {
				deallocate_bitmap(sparse_bits_, maximum_quantity_);
			}

			data_ptr_ = new_data;
			sparse_bits_ = new_bits;
			maximum_quantity_ = new_capacity;
		}
	}

	[[nodiscard]] inline T& at(size_t index) noexcept {
		if (index >= index_) [[unlikely]] {
			std::terminate();
		}
		return data_ptr_[index];
	}

	[[nodiscard]] inline const T& at(size_t index) const noexcept {
		if (index >= index_) [[unlikely]] {
			std::terminate();
		}
		return data_ptr_[index];
	}

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

	inline void reserve_exact(size_t new_capacity) noexcept {
		invalidate_count_cache();
		grow_data_and_bitmap(new_capacity);
	}

	inline void resize(size_t new_size, const T& value) noexcept {
		invalidate_count_cache();
		if (new_size <= index_) [[unlikely]] {
			if constexpr (!std::is_trivially_destructible_v<T>) {
				for (size_t i = new_size; i < index_; ++i) {
					data_ptr_[i].~T();
				}
			}
			for (size_t i = new_size; i < index_; ++i) {
				bitmap_reset(sparse_bits_, i);
			}
			index_ = new_size;
			hole_count_ = static_cast<size_t>(-1);
			update_dense_status();
			return;
		}

		if (new_size > maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_growth_for_reserve(new_size));
		}

		if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8) {
			for (size_t i = index_; i < new_size; ++i) {
				std::memcpy(&data_ptr_[i], &value, sizeof(T));
				bitmap_set(sparse_bits_, i);
			}
		}
		else {
			for (size_t i = index_; i < new_size; ++i) {
				new (&data_ptr_[i]) T(value);
				bitmap_set(sparse_bits_, i);
			}
		}
		index_ = new_size;
	}

	template <typename... Args>
	inline iterator emplace(const_iterator pos, Args&&... args) noexcept {
		invalidate_count_cache();
		const size_t index = static_cast<size_t>(pos.ptr_ - data_ptr_);
		if (index_ >= maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
		}

		if (index < index_) [[likely]] {
			const bool dense = is_dense();

			if constexpr (std::is_trivially_copyable_v<T>) {
				std::memmove(std::assume_aligned<alignof(T)>(data_ptr_ + index + 1),
				             std::assume_aligned<alignof(T)>(data_ptr_ + index),
				             (index_ - index) * sizeof(T));
			}
			else if (dense) [[likely]] {
				new (&data_ptr_[index_]) T(std::move(data_ptr_[index_ - 1]));
				if (index < index_ - 1) {
					std::move_backward(data_ptr_ + index, data_ptr_ + index_ - 1,
					                   data_ptr_ + index_);
				}
				data_ptr_[index].~T();
			}
			else {
				const size_t w_begin = index / BITS_PER_WORD;
				const size_t w_end = (index_ - 1) / BITS_PER_WORD;

				for (size_t w = w_end + 1; w-- > w_begin; ) {
					uint64_t word = sparse_bits_[w];
					if (word == 0) { continue; }

					size_t lo = (w == w_begin) ? (index % BITS_PER_WORD) : 0;
					size_t hi = (w == w_end) ? ((index_ - 1) % BITS_PER_WORD) : (BITS_PER_WORD - 1);
					uint64_t mask = (~0ull << lo);
					if (hi < 63) { mask &= (1ull << (hi + 1)) - 1; }
					uint64_t bits = word & mask;

					while (bits != 0) {
						const size_t bit_pos = static_cast<size_t>(63 - std::countl_zero(bits));
						const size_t i = w * BITS_PER_WORD + bit_pos;

						if (i + 1 < index_) {
							if (bitmap_test(sparse_bits_, i + 1)) {
								data_ptr_[i + 1] = std::move(data_ptr_[i]);
							}
							else {
								new (&data_ptr_[i + 1]) T(std::move(data_ptr_[i]));
							}
						}
						else {
							new (&data_ptr_[index_]) T(std::move(data_ptr_[i]));
						}

						if (i > index && !bitmap_test(sparse_bits_, i - 1)) {
							data_ptr_[i].~T();
						}

						bits &= ~(1ull << bit_pos);
					}
				}

				if (bitmap_test(sparse_bits_, index)) {
					data_ptr_[index].~T();
				}
			}

			if (dense) [[likely]] {
				bitmap_set(sparse_bits_, index_);
			}
			else {
				bitmap_shift_right_one(sparse_bits_, index, index_);
				hole_count_ = static_cast<size_t>(-1);
			}
		}

		new (data_ptr_ + index) T(std::forward<Args>(args)...);
		bitmap_set(sparse_bits_, index);
		++index_;
		return iterator(data_ptr_ + index, data_ptr_ + index_, nullptr, data_ptr_);
	}

	inline iterator insert(const_iterator pos, const T& value) noexcept {
		return emplace(pos, value);
	}

	inline iterator insert(const_iterator pos, T&& value) noexcept {
		return emplace(pos, std::move(value));
	}

	inline iterator erase(const_iterator pos) noexcept {
		invalidate_count_cache();
		const size_t index = static_cast<size_t>(pos.ptr_ - data_ptr_);
		if (index >= index_) [[unlikely]] {
			return end();
		}

		const bool dense = is_dense();

		if (index < index_ - 1) [[likely]] {
			if constexpr (std::is_trivially_copyable_v<T>) {
				std::memmove(std::assume_aligned<alignof(T)>(data_ptr_ + index),
				             std::assume_aligned<alignof(T)>(data_ptr_ + index + 1),
				             (index_ - index - 1) * sizeof(T));
			}
			else if (dense) [[likely]] {
				std::move(data_ptr_ + index + 1, data_ptr_ + index_, data_ptr_ + index);
				data_ptr_[index_ - 1].~T();
			}
			else {
				if (bitmap_test(sparse_bits_, index)) {
					data_ptr_[index].~T();
				}
				size_t dst = index;
				size_t src = index + 1;
				const size_t total_words = (index_ + BITS_PER_WORD - 1) / BITS_PER_WORD;
				while (src < index_) {
					size_t word_idx = src / BITS_PER_WORD;
					size_t bit_in_word = src % BITS_PER_WORD;
					uint64_t word = sparse_bits_[word_idx] >> bit_in_word;
					if (word == 0) {
						size_t next_word = word_idx + 1;
						if (bit_in_word == 0) {
#ifdef __AVX2__
							for (; next_word + 4 <= total_words; next_word += 4)
							{
								__m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sparse_bits_ + next_word));
								if (!_mm256_testz_si256(v, v)) { break; }
							}
#endif
						}
						src = next_word * BITS_PER_WORD;
						continue;
					}
					size_t offset = std::countr_zero(word);
					src = word_idx * BITS_PER_WORD + bit_in_word + offset;
					if (src >= index_) { break; }
					if (dst != src) {
						new (&data_ptr_[dst]) T(std::move(data_ptr_[src]));
						data_ptr_[src].~T();
					}
					++dst;
					++src;
				}
			}

			if (dense) [[likely]] {
				bitmap_reset(sparse_bits_, index_ - 1);
			}
			else {
				bitmap_shift_left(sparse_bits_, index, index_, 1);
				hole_count_ = static_cast<size_t>(-1);
			}
		}
		else {
			if (bitmap_test(sparse_bits_, index)) {
				data_ptr_[index].~T();
			}
			bitmap_reset(sparse_bits_, index);
		}

		--index_;
		update_dense_status();
		return iterator(data_ptr_ + index, data_ptr_ + index_,
		                dense ? nullptr : sparse_bits_, data_ptr_);
	}

	inline iterator erase(const_iterator first, const_iterator last) noexcept {
		invalidate_count_cache();
		const size_t start_index = static_cast<size_t>(first.ptr_ - data_ptr_);
		const size_t end_index = static_cast<size_t>(last.ptr_ - data_ptr_);

		if (start_index >= index_) [[unlikely]] {
			return end();
		}

		const size_t real_end = end_index > index_ ? index_ : end_index;
		if (start_index >= real_end) [[unlikely]] {
			return iterator(data_ptr_ + start_index, data_ptr_ + index_,
			                is_dense_ ? nullptr : sparse_bits_, data_ptr_);
		}

		const size_t gap = real_end - start_index;
		const bool dense = is_dense();

		if (real_end < index_) [[likely]] {
			if constexpr (std::is_trivially_copyable_v<T>) {
				std::memmove(std::assume_aligned<alignof(T)>(data_ptr_ + start_index),
				             std::assume_aligned<alignof(T)>(data_ptr_ + real_end),
				             (index_ - real_end) * sizeof(T));
			}
			else if (dense) [[likely]] {
				std::move(data_ptr_ + real_end, data_ptr_ + index_, data_ptr_ + start_index);
				std::destroy_n(data_ptr_ + index_ - gap, gap);
			}
			else {
				for (size_t i = start_index; i < real_end; ++i) {
					if (bitmap_test(sparse_bits_, i)) {
						data_ptr_[i].~T();
					}
				}
				for (size_t i = 0; i < index_ - real_end; ++i) {
					size_t dst = start_index + i;
					size_t src = real_end + i;
					if (bitmap_test(sparse_bits_, src)) {
						new (&data_ptr_[dst]) T(std::move(data_ptr_[src]));
						data_ptr_[src].~T();
					}
				}
			}

			if (dense) [[likely]] {
				for (size_t i = index_ - gap; i < index_; ++i) {
					bitmap_reset(sparse_bits_, i);
				}
			}
			else {
				bitmap_shift_left(sparse_bits_, start_index, index_, gap);
				hole_count_ = static_cast<size_t>(-1);
			}

			index_ -= gap;
		}
		else {
			for (size_t i = start_index; i < real_end; ++i) {
				if (bitmap_test(sparse_bits_, i)) {
					data_ptr_[i].~T();
				}
				bitmap_reset(sparse_bits_, i);
			}
			index_ -= gap;
		}

		update_dense_status();
		return iterator(data_ptr_ + start_index, data_ptr_ + index_,
		                dense ? nullptr : sparse_bits_, data_ptr_);
	}

	inline void swap(class_pool& other) noexcept {
		std::swap(data_ptr_, other.data_ptr_);
		std::swap(sparse_bits_, other.sparse_bits_);
		std::swap(maximum_quantity_, other.maximum_quantity_);
		std::swap(index_, other.index_);
		std::swap(is_dense_, other.is_dense_);
		std::swap(count_cache_, other.count_cache_);
		std::swap(hole_count_, other.hole_count_);
		std::swap(inline_bits_, other.inline_bits_);

		if (is_inline_bitmap()) {
			sparse_bits_ = &inline_bits_;
		}
		if (other.is_inline_bitmap()) {
			other.sparse_bits_ = &other.inline_bits_;
		}
	}

	inline void pop_back() noexcept {
		invalidate_count_cache();
		if (index_ > 0) [[likely]] {
			const size_t idx = index_ - 1;
			if (is_dense_) [[likely]] {
				data_ptr_[idx].~T();
				bitmap_reset(sparse_bits_, idx);
				--index_;
				return;
			}
			if (bitmap_test(sparse_bits_, idx)) {
				data_ptr_[idx].~T();
				bitmap_reset(sparse_bits_, idx);
			}
			else {
				--hole_count_;
			}
			--index_;
		}
		update_dense_status();
	}

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
		                nullptr, data_ptr_);
	}

	const_iterator begin() const noexcept {
		return const_iterator(data_ptr_, data_ptr_ + index_,
		                      is_dense_ ? nullptr : sparse_bits_, data_ptr_);
	}

	const_iterator end() const noexcept {
		return const_iterator(data_ptr_ + index_, data_ptr_ + index_,
		                      nullptr, data_ptr_);
	}

	const_iterator cbegin() const noexcept {
		return const_iterator(data_ptr_, data_ptr_ + index_,
		                      is_dense_ ? nullptr : sparse_bits_, data_ptr_);
	}

	const_iterator cend() const noexcept {
		return const_iterator(data_ptr_ + index_, data_ptr_ + index_,
		                      nullptr, data_ptr_);
	}

	template <typename... Args>
	inline T& emplace_at(size_t index, Args&&... args) noexcept {
		invalidate_count_cache();
		static_assert(std::is_constructible_v<T, Args...>,
			"T must be constructible from the provided arguments");

		if (index >= maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_growth_for_reserve(index + 1));
		}

		const bool extended = (index >= index_);
		const size_t old_usage = index_;
		if (extended) [[unlikely]] {
			index_ = index + 1;
			hole_count_ += (index - old_usage);
			if (index > old_usage) {
				is_dense_ = false;
			}
		}

		if (is_dense_ && !extended) [[likely]] {
			return data_ptr_[index];
		}

		if (bitmap_test(sparse_bits_, index)) [[likely]] {
			return data_ptr_[index];
		}

		new (&data_ptr_[index]) T(std::forward<Args>(args)...);
		bitmap_set(sparse_bits_, index);
		if (!extended) {
			--hole_count_;
		}
		update_dense_status();
		return data_ptr_[index];
	}

	template <typename... Args>
	inline T& sparse_emplace_at(size_t index, Args&&... args) noexcept {
		invalidate_count_cache();
		static_assert(std::is_constructible_v<T, Args...>,
			"T must be constructible from the provided arguments");

		if (index >= maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_growth_for_reserve(index + 1));
		}

		const bool extended = (index >= index_);
		const size_t old_usage = index_;
		if (extended) {
			index_ = index + 1;
			hole_count_ += (index - old_usage);
			if (index > old_usage) {
				is_dense_ = false;
			}
		}

		if (is_dense_ && !extended) [[likely]] {
			if constexpr (!std::is_trivially_destructible_v<T>) {
				data_ptr_[index].~T();
			}
			new (&data_ptr_[index]) T(std::forward<Args>(args)...);
			bitmap_set(sparse_bits_, index);
			return data_ptr_[index];
		}

		if (bitmap_test(sparse_bits_, index)) {
			data_ptr_[index].~T();
		}
		else if (!extended) {
			--hole_count_;
		}

		new (&data_ptr_[index]) T(std::forward<Args>(args)...);
		bitmap_set(sparse_bits_, index);
		update_dense_status();
		return data_ptr_[index];
	}

	inline void sparse_erase_at(size_t index) noexcept {
		invalidate_count_cache();
		if (index < maximum_quantity_ && bitmap_test(sparse_bits_, index)) {
			data_ptr_[index].~T();
			bitmap_reset(sparse_bits_, index);
			++hole_count_;
			is_dense_ = false;
		}
	}

	[[nodiscard]] constexpr bool is_constructed_at(size_t index) const noexcept {
		if (index >= maximum_quantity_) { return false; }
		return bitmap_test(sparse_bits_, index);
	}

	[[nodiscard]] bool is_dense() const noexcept {
		return is_dense_;
	}

	// 填洞或追加
	// 有空洞: emplace_at 填第一个空洞, 自动 --hole_count_
	// 无空洞: emplace_back 末尾追加
	// fast path(hole_count_==0): find_first_hole_ 短路, 仅一次比较
	// slow path(有洞): bitmap 扫描找首个 0 位, 平均 first word 命中
	// 不新增成员变量, 不影响其他接口性能
	template <typename... Args>
	inline T& fill_the_hole(Args&&... args) noexcept {
		static_assert(std::is_constructible_v<T, Args...>,
			"T must be constructible from the provided arguments");
		const size_t idx = find_first_hole_();
		if (idx == static_cast<size_t>(-1)) {
			emplace_back(std::forward<Args>(args)...);
			return back();
		}
		return emplace_at(idx, std::forward<Args>(args)...);
	}

private:
	// 找 [0, index_) 范围内第一个空洞(bitmap=0), 无洞返回 npos
	// sparse_bits_ 始终有效(inline 或外部), 无需区分
	// std::countr_one 单指令定位首个 0 位
	[[nodiscard]] size_t find_first_hole_() const noexcept {
		if (hole_count_ == 0) [[likely]] {
			return static_cast<size_t>(-1);
		}
		const size_t full_words = index_ / BITS_PER_WORD;
		for (size_t w = 0; w < full_words; ++w) {
			const uint64_t word = sparse_bits_[w];
			if (word != ~0ull) {
				return w * BITS_PER_WORD + std::countr_one(word);
			}
		}
		const size_t tail = index_ % BITS_PER_WORD;
		if (tail != 0) {
			const uint64_t mask = (1ull << tail) - 1;
			const uint64_t valid = sparse_bits_[full_words] & mask;
			if (valid != mask) {
				return full_words * BITS_PER_WORD + std::countr_one(valid);
			}
		}
		return static_cast<size_t>(-1);
	}

	void recompute_is_dense() noexcept {
		if (index_ == 0) { is_dense_ = true; hole_count_ = 0; return; }

		size_t holes = 0;
		bool dense = true;
		const size_t full_words = index_ / BITS_PER_WORD;
		size_t i = 0;

#if defined(__AVX512F__)
		for (; i + 8 <= full_words; i += 8) {
			__m512i v = _mm512_loadu_si512(sparse_bits_ + i);
			__mmask8 cmp = _mm512_cmpeq_epi64_mask(v, _mm512_set1_epi64(~0ll));
			if (cmp != 0xFF) {
				dense = false;
				for (int j = 0; j < 8; ++j) {
					if (!((cmp >> j) & 1)) {
						holes += BITS_PER_WORD - std::popcount(sparse_bits_[i + j]);
					}
				}
			}
		}
#elif defined(__AVX2__)
		const __m256i all_ones = _mm256_set1_epi64x(~0ll);
		for (; i + 4 <= full_words; i += 4) {
			__m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sparse_bits_ + i));
			__m256i cmp = _mm256_cmpeq_epi64(v, all_ones);
			int mask = _mm256_movemask_epi8(cmp);
			if (mask != 0xFFFFFFFF) {
				dense = false;
				for (int j = 0; j < 4; ++j) {
					int word_mask = (mask >> (j * 8)) & 0xFF;
					if (word_mask != 0xFF) {
						holes += BITS_PER_WORD - std::popcount(sparse_bits_[i + j]);
					}
				}
			}
		}
#endif

		for (; i < full_words; ++i) {
			if (sparse_bits_[i] != ~0ull) {
				dense = false;
				holes += BITS_PER_WORD - std::popcount(sparse_bits_[i]);
			}
		}

		const size_t tail = index_ % BITS_PER_WORD;
		if (tail != 0) {
			const uint64_t mask = (1ull << tail) - 1;
			const uint64_t word = sparse_bits_[full_words] & mask;
			if (word != mask) {
				dense = false;
				holes += tail - std::popcount(word);
			}
		}

		is_dense_ = dense;
		hole_count_ = holes;
	}

	void bulk_set_bits(size_t start, size_t end) noexcept {
		size_t sw = start / BITS_PER_WORD;
		size_t ew = (end - 1) / BITS_PER_WORD;
		if (sw == ew) {
			uint64_t mask = ((1ull << (end % BITS_PER_WORD)) - 1) ^ ((1ull << (start % BITS_PER_WORD)) - 1);
			sparse_bits_[sw] |= mask;
		}
		else {
			if (start % BITS_PER_WORD != 0) {
				sparse_bits_[sw] |= ~((1ull << (start % BITS_PER_WORD)) - 1);
			}
			size_t w = sw + 1;
#ifdef __AVX2__
			{
				const __m256i all_ones = _mm256_set1_epi64x(~0ull);
				for (; w + 4 <= ew; w += 4)
				{
					_mm256_storeu_si256(reinterpret_cast<__m256i*>(sparse_bits_ + w), all_ones);
				}
			}
#endif
			for (; w < ew; ++w) {
				sparse_bits_[w] = ~0ull;
			}
			if (end % BITS_PER_WORD != 0) {
				sparse_bits_[ew] |= (1ull << (end % BITS_PER_WORD)) - 1;
			}
		}
	}

	void append_bulk(const T* src, size_t count) noexcept {
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			increase_capacity(index_ + count);
		}
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(data_ptr_ + index_, src, count * sizeof(T));
		}
		else {
			for (size_t i = 0; i < count; ++i) {
				new (data_ptr_ + index_ + i) T(src[i]);
			}
		}
		size_t end = index_ + count;
		bulk_set_bits(index_, end);
		index_ = end;
		is_dense_ = true;
		hole_count_ = 0;
	}

	void append_bulk_move(T* src, size_t count) noexcept {
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			increase_capacity(index_ + count);
		}
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(data_ptr_ + index_, src, count * sizeof(T));
		}
		else {
			for (size_t i = 0; i < count; ++i) {
				new (data_ptr_ + index_ + i) T(std::move(src[i]));
			}
		}
		size_t end = index_ + count;
		bulk_set_bits(index_, end);
		index_ = end;
		is_dense_ = true;
		hole_count_ = 0;
	}

	void append_incrementing(size_t count, uint64_t& counter) noexcept {
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			increase_capacity(index_ + count);
		}
		for (size_t i = 0; i < count; ++i) {
			data_ptr_[index_ + i] = ++counter;
		}
		size_t end = index_ + count;
		bulk_set_bits(index_, end);
		index_ = end;
		is_dense_ = true;
		hole_count_ = 0;
	}

	template <typename F>
	void append_generated(size_t count, F&& generator) noexcept {
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			increase_capacity(index_ + count);
		}
		for (size_t i = 0; i < count; ++i) {
			new (&data_ptr_[index_ + i]) T(generator());
		}
		size_t end = index_ + count;
		bulk_set_bits(index_, end);
		index_ = end;
		is_dense_ = true;
		hole_count_ = 0;
	}

	template <typename EntityLike>
	void append_indices_from(const EntityLike* entities, size_t count) noexcept {
		if (count == 0) { return; }
		if (index_ + count > maximum_quantity_) [[unlikely]] {
			increase_capacity(index_ + count);
		}
		for (size_t i = 0; i < count; ++i) {
			data_ptr_[index_ + i] = static_cast<T>(entities[i].parts_.index_);
		}
		size_t end = index_ + count;
		bulk_set_bits(index_, end);
		index_ = end;
		is_dense_ = true;
		hole_count_ = 0;
	}

public:
	void fill_bulk(const T& value, size_t start, size_t count) noexcept {
		if (count == 0) { return; }
		size_t end = start + count;
		if (end > maximum_quantity_) [[unlikely]] {
			increase_capacity(end);
		}
		if (end > index_) {
			for (size_t i = index_; i < start; ++i) {
				bitmap_set(sparse_bits_, i);
				data_ptr_[i] = T{};
			}
			index_ = end;
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
				broadcast = _mm256_set1_epi16(*reinterpret_cast<const int16_t*>(&value));
			}
			else if constexpr (sizeof(T) == 4) {
				broadcast = _mm256_set1_epi32(*reinterpret_cast<const int32_t*>(&value));
			}
			else {
				broadcast = _mm256_set1_epi64x(*reinterpret_cast<const int64_t*>(&value));
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
			for (size_t i = 0; i < count; ++i) {
				data_ptr_[start + i] = value;
			}
		}
		bulk_set_bits(start, end);
		is_dense_ = true;
		hole_count_ = 0;
	}

	void prepare_dense(size_t new_size) noexcept {
		invalidate_count_cache();
		if (new_size > maximum_quantity_) [[unlikely]] {
			grow_data_and_bitmap(calculate_growth_for_reserve(new_size));
		}
		if (new_size > index_) {
			bulk_set_bits(index_, new_size);
			index_ = new_size;
		}
		is_dense_ = true;
		hole_count_ = 0;
	}

private:
	void update_dense_status() noexcept {
		if (is_dense_) { return; }
		if (hole_count_ == static_cast<size_t>(-1)) {
			recompute_is_dense();
		}
		else if (hole_count_ == 0) {
			is_dense_ = true;
		}
	}
};

template <typename T>
inline void swap(class_pool<T>& a, class_pool<T>& b) noexcept {
	a.swap(b);
}