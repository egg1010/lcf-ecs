// class_pool.inc.hpp - class_pool<T> 方法实现
// 由 class_pool.hpp 包含, 复用其头文件; 禁止添加 #pragma once 或 #include

template <typename T>
void class_pool<T>::bitmap_shift_right_one(uint64_t* bits, size_t start, size_t end) noexcept {
	if (start >= end) { return; }

	const size_t start_word = start / BITS_PER_WORD;
	const size_t start_bit  = start % BITS_PER_WORD;
	const size_t end_word   = end / BITS_PER_WORD;
	const size_t end_bit    = end % BITS_PER_WORD;

	if (start_word == end_word) [[unlikely]] {
		uint64_t word = bits[start_word];
		uint64_t mask_lo = (start_bit == 0) ? 0ull : ((1ull << start_bit) - 1);
		uint64_t mask_hi = (end_bit == BITS_PER_WORD - 1)
			? ~0ull
			: ((1ull << (end_bit + 1)) - 1);
		uint64_t mask = mask_hi ^ mask_lo;
		uint64_t preserve_below = word & mask_lo;
		uint64_t preserve_above = word & ~mask_hi;
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

template <typename T>
void class_pool<T>::bitmap_zero_words(uint64_t* bits, size_t start, size_t end) noexcept {
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

template <typename T>
void class_pool<T>::bitmap_shift_left(uint64_t* bits, size_t start, size_t end, size_t shift) noexcept {
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

template <typename T>
void class_pool<T>::destroy_dense_range(size_t first, size_t last) noexcept {
	if constexpr (!std::is_trivially_destructible_v<T>) {
		if (first < last) {
			std::destroy_n(data_ptr_ + first, last - first);
		}
	}
}

template <typename T>
void class_pool<T>::destroy_sparse_range(size_t first, size_t last) noexcept {
	if constexpr (std::is_trivially_destructible_v<T>) { return; }
	if (first >= last) { return; }

	const size_t start_word = first / BITS_PER_WORD;
	const size_t start_bit = first % BITS_PER_WORD;
	const size_t end_word = last / BITS_PER_WORD;
	const size_t end_bit = last % BITS_PER_WORD;

	if (start_word == end_word) {
		uint64_t mask = (end_bit == 0 ? 0ull : ((1ull << end_bit) - 1)) & ~((1ull << start_bit) - 1);
		uint64_t word = live_bits_[start_word] & mask;
		while (word != 0) {
			const size_t offset = std::countr_zero(word);
			data_ptr_[start_word * BITS_PER_WORD + offset].~T();
			word = clear_lowest_bit(word);
		}
		return;
	}

	if (start_bit != 0) {
		uint64_t word = live_bits_[start_word] & ~((1ull << start_bit) - 1);
		while (word != 0) {
			const size_t offset = std::countr_zero(word);
			data_ptr_[start_word * BITS_PER_WORD + offset].~T();
			word = clear_lowest_bit(word);
		}
	}

	for (size_t w = start_word + (start_bit != 0 ? 1 : 0); w < end_word; ++w) {
		if (w + 4 < end_word) {
			PREFETCH_R(&live_bits_[w + 4]);
		}
		uint64_t word = live_bits_[w];
		while (word != 0) {
			const size_t offset = std::countr_zero(word);
			data_ptr_[w * BITS_PER_WORD + offset].~T();
			word = clear_lowest_bit(word);
		}
	}

	if (end_bit != 0) {
		uint64_t word = live_bits_[end_word] & ((1ull << end_bit) - 1);
		while (word != 0) {
			const size_t offset = std::countr_zero(word);
			data_ptr_[end_word * BITS_PER_WORD + offset].~T();
			word = clear_lowest_bit(word);
		}
	}
}

template <typename T>
void class_pool<T>::destroy_all() noexcept {
	if constexpr (std::is_trivially_destructible_v<T>) {
	}
	else
	{
		if (index_ == 0 || live_bits_ == nullptr) { return; }

		if (is_dense()) [[likely]] {
			destroy_dense_range(0, index_);
		}
		else {
			destroy_sparse_range(0, index_);
		}
	}
}

template <typename T>
void class_pool<T>::uninitialized_move_dense(T* src_first, T* src_last, T* dst) noexcept {
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

template <typename T>
template <bool MoveAndDestroy>
void class_pool<T>::relocate_sparse(T* dst, const T* src, const uint64_t* src_bits, size_t count) noexcept {
	const size_t num_full_words = count / BITS_PER_WORD;
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

template <typename T>
void class_pool<T>::copy_trivial_data(T* __restrict dst, const T* __restrict src, size_t count) noexcept {
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

template <typename T>
void class_pool<T>::grow_data_and_bitmap(size_t new_capacity) noexcept {
	if (new_capacity <= maximum_quantity_) [[likely]] {
		return;
	}

	T* new_data = allocate_data(new_capacity);
	const bool was_inline = is_inline_bitmap();
	uint64_t saved_inline = was_inline ? inline_bits_ : 0;
	uint64_t saved_live = was_inline ? inline_live_bits_ : 0;
	uint64_t* new_bits = obtain_bitmap(new_capacity);
	uint64_t* new_live = obtain_live_bitmap(new_capacity);
	if (was_inline) {
		inline_bits_ = saved_inline;
		inline_live_bits_ = saved_live;
	}

	if (data_ptr_ != nullptr && index_ > 0) [[likely]] {
		if constexpr (std::is_trivially_copyable_v<T>) {
			copy_trivial_data(new_data, data_ptr_, index_);
		}
		else if (is_dense()) {
			uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
		}
		else {
			relocate_sparse<true>(new_data, data_ptr_, live_bits_, index_);
		}
		if (new_bits != sparse_bits_) {
			copy_bitmap(new_bits, sparse_bits_, maximum_quantity_);
			copy_bitmap(new_live, live_bits_, maximum_quantity_);
		}
		deallocate_data(data_ptr_, maximum_quantity_);
		if (!was_inline) {
			deallocate_bitmap(sparse_bits_, maximum_quantity_);
			deallocate_bitmap(live_bits_, maximum_quantity_);
		}
	}
	else if (data_ptr_ != nullptr) {
		deallocate_data(data_ptr_, maximum_quantity_);
		if (!was_inline) {
			deallocate_bitmap(sparse_bits_, maximum_quantity_);
			deallocate_bitmap(live_bits_, maximum_quantity_);
		}
	}

	data_ptr_ = new_data;
	sparse_bits_ = new_bits;
	live_bits_ = new_live;
	maximum_quantity_ = new_capacity;
}

template <typename T>
class_pool<T>::class_pool(size_t capacity) noexcept
	: data_ptr_(nullptr)
	, sparse_bits_(nullptr)
	, live_bits_(nullptr)
	, maximum_quantity_(capacity)
	, index_(0) {
	if (capacity > 0) [[likely]] {
		data_ptr_ = allocate_data(capacity);
		sparse_bits_ = obtain_bitmap(capacity);
		live_bits_ = obtain_live_bitmap(capacity);
	}
}

template <typename T>
class_pool<T>::class_pool(size_t count, const T& value) noexcept
	: data_ptr_(nullptr)
	, sparse_bits_(nullptr)
	, live_bits_(nullptr)
	, maximum_quantity_(round_up_to_default(count))
	, index_(0) {
	if (count > 0) [[likely]] {
		data_ptr_ = allocate_data(maximum_quantity_);
		sparse_bits_ = obtain_bitmap(maximum_quantity_);
		live_bits_ = obtain_live_bitmap(maximum_quantity_);
		for (size_t i = 0; i < count; ++i) {
			new (&data_ptr_[i]) T(value);
			bitmap_set(sparse_bits_, i);
			bitmap_set(live_bits_, i);
		}
		index_ = count;
	}
}

template <typename T>
template <typename InputIt>
class_pool<T>::class_pool(InputIt first, InputIt last) noexcept
	: data_ptr_(nullptr)
	, sparse_bits_(nullptr)
	, live_bits_(nullptr)
	, maximum_quantity_(0)
	, index_(0) {
	const size_t count = static_cast<size_t>(std::distance(first, last));
	maximum_quantity_ = round_up_to_default(count);

	if (count > 0) [[likely]] {
		data_ptr_ = allocate_data(maximum_quantity_);
		sparse_bits_ = obtain_bitmap(maximum_quantity_);
		live_bits_ = obtain_live_bitmap(maximum_quantity_);
		size_t i = 0;
		for (auto it = first; it != last; ++it, ++i) {
			new (&data_ptr_[i]) T(*it);
			bitmap_set(sparse_bits_, i);
			bitmap_set(live_bits_, i);
		}
		index_ = count;
	}
}

template <typename T>
class_pool<T>::class_pool(std::initializer_list<T> init) noexcept
	: class_pool(init.begin(), init.end()) {}

template <typename T>
class_pool<T>::class_pool(const class_pool& other) noexcept
	: data_ptr_(nullptr)
	, sparse_bits_(nullptr)
	, live_bits_(nullptr)
	, maximum_quantity_(other.maximum_quantity_)
	, index_(other.index_)
	, is_dense_(other.is_dense_)
	, count_cache_(other.count_cache_)
	, hole_count_(other.hole_count_)
	, inline_bits_(other.inline_bits_)
	, inline_live_bits_(other.inline_live_bits_) {
	if (maximum_quantity_ > 0) [[likely]] {
		data_ptr_ = allocate_data(maximum_quantity_);
		if (other.is_inline_bitmap()) {
			sparse_bits_ = &inline_bits_;
			live_bits_ = &inline_live_bits_;
		}
		else {
			sparse_bits_ = allocate_bitmap(maximum_quantity_);
			live_bits_ = allocate_bitmap(maximum_quantity_);
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
			relocate_sparse<false>(data_ptr_, other.data_ptr_, other.live_bits_, other.index_);
		}
		copy_bitmap(sparse_bits_, other.sparse_bits_, other.maximum_quantity_);
		copy_bitmap(live_bits_, other.live_bits_, other.maximum_quantity_);
	}
}

template <typename T>
class_pool<T>::class_pool(class_pool&& other) noexcept
	: data_ptr_(other.data_ptr_)
	, sparse_bits_(other.sparse_bits_)
	, live_bits_(other.live_bits_)
	, maximum_quantity_(other.maximum_quantity_)
	, index_(other.index_)
	, is_dense_(other.is_dense_)
	, count_cache_(other.count_cache_)
	, hole_count_(other.hole_count_)
	, inline_bits_(other.inline_bits_)
	, inline_live_bits_(other.inline_live_bits_) {
	if (other.is_inline_bitmap()) {
		sparse_bits_ = &inline_bits_;
		live_bits_ = &inline_live_bits_;
	}
	other.data_ptr_ = nullptr;
	other.sparse_bits_ = nullptr;
	other.live_bits_ = nullptr;
	other.maximum_quantity_ = 0;
	other.index_ = 0;
	other.is_dense_ = 1;
	other.count_cache_ = static_cast<size_t>(-1);
	other.hole_count_ = 0;
	other.inline_bits_ = 0;
	other.inline_live_bits_ = 0;
}

template <typename T>
class_pool<T>& class_pool<T>::operator=(const class_pool& other) noexcept {
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
	inline_live_bits_ = other.inline_live_bits_;

	if (maximum_quantity_ > 0) [[likely]] {
		data_ptr_ = allocate_data(maximum_quantity_);
		if (other.is_inline_bitmap()) {
			sparse_bits_ = &inline_bits_;
			live_bits_ = &inline_live_bits_;
		}
		else {
			sparse_bits_ = allocate_bitmap(maximum_quantity_);
			live_bits_ = allocate_bitmap(maximum_quantity_);
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
			relocate_sparse<false>(data_ptr_, other.data_ptr_, other.live_bits_, other.index_);
		}
		copy_bitmap(sparse_bits_, other.sparse_bits_, other.maximum_quantity_);
		copy_bitmap(live_bits_, other.live_bits_, other.maximum_quantity_);
	}
	else {
		data_ptr_ = nullptr;
		sparse_bits_ = nullptr;
		live_bits_ = nullptr;
	}
	return *this;
}

template <typename T>
class_pool<T>& class_pool<T>::operator=(class_pool&& other) noexcept {
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
	inline_live_bits_ = other.inline_live_bits_;

	if (other.is_inline_bitmap()) {
		sparse_bits_ = &inline_bits_;
		live_bits_ = &inline_live_bits_;
	}
	else {
		sparse_bits_ = other.sparse_bits_;
		live_bits_ = other.live_bits_;
	}

	other.data_ptr_ = nullptr;
	other.sparse_bits_ = nullptr;
	other.live_bits_ = nullptr;
	other.maximum_quantity_ = 0;
	other.index_ = 0;
	other.is_dense_ = 1;
	other.count_cache_ = static_cast<size_t>(-1);
	other.hole_count_ = 0;
	other.inline_bits_ = 0;
	other.inline_live_bits_ = 0;
	return *this;
}

template <typename T>
class_pool<T>::~class_pool() noexcept {
	destroy_all();
	release_bitmap();
	deallocate_data(data_ptr_, maximum_quantity_);
}

template <typename T>
template <typename... Args>
void class_pool<T>::emplace_back(Args&&... args) noexcept {
	if (count_cache_ != static_cast<size_t>(-1)) [[likely]] {
		++count_cache_;
	}
	static_assert(std::is_constructible_v<T, Args...>,
	             "T must be constructible from the provided arguments");

	if (index_ >= maximum_quantity_) [[unlikely]] {
		grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
	}
	new (&data_ptr_[index_]) T(std::forward<Args>(args)...);
	bitmap_set(sparse_bits_, index_);
	bitmap_set(live_bits_, index_);
	++index_;
}

template <typename T>
void class_pool<T>::push_back_unchecked(const T& value) noexcept {
	if (index_ >= maximum_quantity_) [[unlikely]] {
		grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
	}
	new (&data_ptr_[index_]) T(value);
	bitmap_set(sparse_bits_, index_);
	bitmap_set(live_bits_, index_);
	++index_;
}

template <typename T>
template <typename... Args>
void class_pool<T>::emplace_back_unchecked(Args&&... args) noexcept {
	if (index_ >= maximum_quantity_) [[unlikely]] {
		grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
	}
	new (&data_ptr_[index_]) T(std::forward<Args>(args)...);
	bitmap_set(sparse_bits_, index_);
	bitmap_set(live_bits_, index_);
	++index_;
}

template <typename T>
template <typename... Args>
void class_pool<T>::emplace_back_dense_unchecked(Args&&... args) noexcept {
	if (count_cache_ != static_cast<size_t>(-1)) [[likely]] {
		++count_cache_;
	}
	if (index_ >= maximum_quantity_) [[unlikely]] {
		grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
	}
	new (&data_ptr_[index_]) T(std::forward<Args>(args)...);
	sparse_bits_[index_ / BITS_PER_WORD] |= (1ull << (index_ % BITS_PER_WORD));
	live_bits_[index_ / BITS_PER_WORD] |= (1ull << (index_ % BITS_PER_WORD));
	++index_;
}

template <typename T>
void class_pool<T>::append_n(size_t n, const T& value) noexcept {
	if (n == 0) [[unlikely]] { return; }

	// 溢出安全扩容检查
	if (n > maximum_quantity_ - index_) [[unlikely]] {
		grow_data_and_bitmap(calculate_growth_for_reserve(index_ + n));
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

	const size_t start = index_;
	const size_t last = start + n - 1;
	const size_t start_word = start / BITS_PER_WORD;
	const size_t start_bit = start % BITS_PER_WORD;
	const size_t end_word = last / BITS_PER_WORD;
	const size_t end_bit = last % BITS_PER_WORD;

	if (start_word == end_word)
	{
		// 同 word: mask = bits [start_bit, end_bit]
		uint64_t mask = (~0ull >> (63 - end_bit)) & (~0ull << start_bit);
		sparse_bits_[start_word] |= mask;
		live_bits_[start_word] |= mask;
	}
	else
	{
		// 跨 word: 首 + 中间 + 尾
		sparse_bits_[start_word] |= (~0ull << start_bit);
		live_bits_[start_word] |= (~0ull << start_bit);
		sparse_bits_[end_word] |= (~0ull >> (63 - end_bit));
		live_bits_[end_word] |= (~0ull >> (63 - end_bit));
		// 中间 word 批量全 1 (这些位置原 bit 必然为 0, 可用 = 覆盖)
		if (end_word > start_word + 1)
		{
			const size_t mid_end = end_word;
#ifdef __AVX2__
			__m256i ones = _mm256_set1_epi64x(~0ull);
			size_t w = start_word + 1;
			for (; w + 4 <= mid_end; w += 4)
			{
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(sparse_bits_ + w), ones);
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(live_bits_ + w), ones);
			}
			for (; w < mid_end; ++w) { sparse_bits_[w] = ~0ull; live_bits_[w] = ~0ull; }
#else
			for (size_t w = start_word + 1; w < mid_end; ++w)
			{
				sparse_bits_[w] = ~0ull;
				live_bits_[w] = ~0ull;
			}
#endif
		}
	}

	// 更新状态 (is_dense_ / hole_count_ 不变: 追加不改变 dense/sparse 状态)
	if (count_cache_ != static_cast<size_t>(-1)) [[likely]] {
		count_cache_ += n;
	}
	index_ += n;
}

template <typename T>
void class_pool<T>::clear() noexcept {
	destroy_all();
	invalidate_count_cache();
	if (sparse_bits_ != nullptr && maximum_quantity_ > 0) [[likely]] {
		const size_t words = bitmap_word_count(maximum_quantity_);
		std::memset(std::assume_aligned<alignof(uint64_t)>(sparse_bits_), 0,
		            words * sizeof(uint64_t));
		std::memset(std::assume_aligned<alignof(uint64_t)>(live_bits_), 0,
		            words * sizeof(uint64_t));
	}
	index_ = 0;
	is_dense_ = 1;
	hole_count_ = 0;
	first_hole_hint_ = 0;
}

template <typename T>
typename class_pool<T>::size_type class_pool<T>::count() const noexcept {
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

template <typename T>
void class_pool<T>::increase_capacity(size_t new_capacity) noexcept {
	if (new_capacity > maximum_quantity_) [[unlikely]] {
		grow_data_and_bitmap(calculate_growth_for_reserve(new_capacity));
	}
}

template <typename T>
void class_pool<T>::increase_capacity(size_t new_capacity, const T& value) noexcept {
	invalidate_count_cache();
	if (new_capacity <= index_) [[likely]] {
		return;
	}

	if (new_capacity > maximum_quantity_) [[unlikely]] {
		grow_data_and_bitmap(calculate_growth_for_reserve(new_capacity));
	}

	const size_t count = new_capacity - index_;
	T* dst = data_ptr_ + index_;

	if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 1) {
		std::memset(dst, static_cast<unsigned char>(value), count);
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
		__m256i* d = static_cast<__m256i*>(static_cast<void*>(dst));
		for (size_t i = 0; i < chunks; ++i) {
			_mm256_storeu_si256(d + i, broadcast);
		}
#else
		for (size_t i = 0; i < chunks; ++i) {
			std::memcpy(dst + i * elem_per_ymm, &value, elem_per_ymm * sizeof(T));
		}
#endif
		for (size_t i = 0; i < remainder; ++i) {
			dst[chunks * elem_per_ymm + i] = value;
		}
	}
	else if constexpr (std::is_trivially_copyable_v<T>) {
		for (size_t i = 0; i < count; ++i) {
			dst[i] = value;
		}
	}
	else {
		for (size_t i = 0; i < count; ++i) {
			new (&dst[i]) T(value);
		}
	}
	bulk_set_bits(index_, new_capacity);
	index_ = new_capacity;
}

template <typename T>
void class_pool<T>::shrink_to_fit() noexcept {
	if (index_ == 0 && data_ptr_ != nullptr) [[unlikely]] {
		release_bitmap();
		deallocate_data(data_ptr_, maximum_quantity_);
		data_ptr_ = nullptr;
		sparse_bits_ = nullptr;
		live_bits_ = nullptr;
		maximum_quantity_ = 0;
		hole_count_ = 0;
		return;
	}

	if (index_ < maximum_quantity_ && index_ > 0) [[likely]] {
		T* new_data = allocate_data(index_);
		uint64_t* new_bits = obtain_bitmap(index_);
		uint64_t* new_live = obtain_live_bitmap(index_);
		const bool was_inline = is_inline_bitmap();

		if constexpr (std::is_trivially_copyable_v<T>) {
			copy_trivial_data(new_data, data_ptr_, index_);
		}
		else if (is_dense()) {
			uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
		}
		else {
			relocate_sparse<true>(new_data, data_ptr_, live_bits_, index_);
		}
		copy_bitmap(new_bits, sparse_bits_, index_);
		copy_bitmap(new_live, live_bits_, index_);

		deallocate_data(data_ptr_, maximum_quantity_);
		if (!was_inline) {
			deallocate_bitmap(sparse_bits_, maximum_quantity_);
			deallocate_bitmap(live_bits_, maximum_quantity_);
		}

		data_ptr_ = new_data;
		sparse_bits_ = new_bits;
		live_bits_ = new_live;
		maximum_quantity_ = index_;
		hole_count_ = static_cast<size_t>(-1);
		update_dense_status();
	}
}

template <typename T>
void class_pool<T>::reduce_capacity(size_t new_capacity) noexcept {
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
		live_bits_ = nullptr;
		maximum_quantity_ = 0;
		index_ = 0;
		is_dense_ = 1;
		hole_count_ = 0;
		return;
	}

	if (new_capacity < index_) {
		if (is_dense()) {
			destroy_dense_range(new_capacity, index_);
		}
		else {
			destroy_sparse_range(new_capacity, index_);
		}
		for (size_t i = new_capacity; i < index_; ++i) {
			bitmap_reset(sparse_bits_, i);
			bitmap_reset(live_bits_, i);
		}
		index_ = new_capacity;
		recompute_is_dense();
	}

	T* new_data = allocate_data(new_capacity);
	uint64_t* new_bits = obtain_bitmap(new_capacity);
	uint64_t* new_live = obtain_live_bitmap(new_capacity);
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
		relocate_sparse<true>(new_data, data_ptr_, live_bits_, index_);
	}
	copy_bitmap(new_bits, sparse_bits_, new_capacity);
	copy_bitmap(new_live, live_bits_, new_capacity);

	deallocate_data(data_ptr_, maximum_quantity_);
	if (!was_inline) {
		deallocate_bitmap(sparse_bits_, maximum_quantity_);
		deallocate_bitmap(live_bits_, maximum_quantity_);
	}

	data_ptr_ = new_data;
	sparse_bits_ = new_bits;
	live_bits_ = new_live;
	maximum_quantity_ = new_capacity;
}

template <typename T>
void class_pool<T>::reduce_capacity(size_t new_capacity, class_pool<T>& dst) noexcept {
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
		destroy_dense_range(new_capacity, index_);
	}
	else {
		for (size_t i = new_capacity; i < index_; ++i) {
			if (bitmap_test(sparse_bits_, i)) {
				dst.emplace_back(std::move(data_ptr_[i]));
			}
		}
		destroy_sparse_range(new_capacity, index_);
	}
	for (size_t i = new_capacity; i < index_; ++i) {
		bitmap_reset(sparse_bits_, i);
		bitmap_reset(live_bits_, i);
	}
	index_ = new_capacity;
	recompute_is_dense();

	if (new_capacity < maximum_quantity_) {
		T* new_data = allocate_data(new_capacity);
		uint64_t* new_bits = obtain_bitmap(new_capacity);
		uint64_t* new_live = obtain_live_bitmap(new_capacity);
		const bool was_inline = is_inline_bitmap();

		if constexpr (std::is_trivially_copyable_v<T>) {
			copy_trivial_data(new_data, data_ptr_, index_);
		}
		else if (is_dense()) {
			uninitialized_move_dense(data_ptr_, data_ptr_ + index_, new_data);
		}
		else {
			relocate_sparse<true>(new_data, data_ptr_, live_bits_, index_);
		}
		copy_bitmap(new_bits, sparse_bits_, new_capacity);
		copy_bitmap(new_live, live_bits_, new_capacity);

		deallocate_data(data_ptr_, maximum_quantity_);
		if (!was_inline) {
			deallocate_bitmap(sparse_bits_, maximum_quantity_);
			deallocate_bitmap(live_bits_, maximum_quantity_);
		}

		data_ptr_ = new_data;
		sparse_bits_ = new_bits;
		live_bits_ = new_live;
		maximum_quantity_ = new_capacity;
	}
}

template <typename T>
void class_pool<T>::reserve_exact(size_t new_capacity) noexcept {
	invalidate_count_cache();
	grow_data_and_bitmap(new_capacity);
}

template <typename T>
template <typename... Args>
typename class_pool<T>::iterator class_pool<T>::emplace(const_iterator pos, Args&&... args) noexcept {
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
			bitmap_reset(live_bits_, index);
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
						if (bitmap_test(live_bits_, i + 1)) {
							data_ptr_[i + 1] = std::move(data_ptr_[i]);
						}
						else {
							new (&data_ptr_[i + 1]) T(std::move(data_ptr_[i]));
						}
						bitmap_set(live_bits_, i + 1);
					}
					else {
						new (&data_ptr_[index_]) T(std::move(data_ptr_[i]));
						bitmap_set(live_bits_, index_);
					}

					if (i > index && !bitmap_test(sparse_bits_, i - 1)) {
						data_ptr_[i].~T();
						bitmap_reset(live_bits_, i);
					}

					bits &= ~(1ull << bit_pos);
				}
			}

			if (bitmap_test(live_bits_, index)) {
				data_ptr_[index].~T();
				bitmap_reset(live_bits_, index);
			}
		}

		if (dense) [[likely]] {
			bitmap_set(sparse_bits_, index_);
			bitmap_set(live_bits_, index_);
		}
		else {
			bitmap_shift_right_one(sparse_bits_, index, index_);
			hole_count_ = static_cast<size_t>(-1);
		}
	}

	new (data_ptr_ + index) T(std::forward<Args>(args)...);
	bitmap_set(sparse_bits_, index);
	bitmap_set(live_bits_, index);
	++index_;
	return iterator(data_ptr_ + index, data_ptr_ + index_,
	                is_dense_ ? nullptr : sparse_bits_, data_ptr_);
}

template <typename T>
typename class_pool<T>::iterator class_pool<T>::insert(const_iterator pos, const T& value) noexcept {
	return emplace(pos, value);
}

template <typename T>
typename class_pool<T>::iterator class_pool<T>::insert(const_iterator pos, T&& value) noexcept {
	return emplace(pos, std::move(value));
}

template <typename T>
typename class_pool<T>::iterator class_pool<T>::erase(const_iterator pos) noexcept {
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
			bitmap_reset(sparse_bits_, index_ - 1);
			bitmap_reset(live_bits_, index_ - 1);
		}
		else if (dense) [[likely]] {
			std::move(data_ptr_ + index + 1, data_ptr_ + index_, data_ptr_ + index);
			data_ptr_[index_ - 1].~T();
			bitmap_reset(sparse_bits_, index_ - 1);
			bitmap_reset(live_bits_, index_ - 1);
		}
		else {
			if (bitmap_test(live_bits_, index)) {
				data_ptr_[index].~T();
				bitmap_reset(live_bits_, index);
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
					if (bitmap_test(live_bits_, dst)) {
						data_ptr_[dst].~T();
						bitmap_reset(live_bits_, dst);
					}
					new (&data_ptr_[dst]) T(std::move(data_ptr_[src]));
					bitmap_set(live_bits_, dst);
					data_ptr_[src].~T();
					bitmap_reset(live_bits_, src);
				}
				++dst;
				++src;
			}

			for (size_t i = index; i < dst; ++i) {
				bitmap_set(sparse_bits_, i);
				bitmap_set(live_bits_, i);
			}
			for (size_t i = dst; i < index_; ++i) {
				bitmap_reset(sparse_bits_, i);
				bitmap_reset(live_bits_, i);
			}
			hole_count_ = static_cast<size_t>(-1);
		}
	}
	else {
		if (bitmap_test(live_bits_, index)) {
			data_ptr_[index].~T();
		}
		bitmap_reset(sparse_bits_, index);
		bitmap_reset(live_bits_, index);
	}

	--index_;
	update_dense_status();
	return iterator(data_ptr_ + index, data_ptr_ + index_,
	                is_dense_ ? nullptr : sparse_bits_, data_ptr_);
}

template <typename T>
typename class_pool<T>::iterator class_pool<T>::erase(const_iterator first, const_iterator last) noexcept {
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
			const size_t clear_start = index_ - gap;
			const size_t clear_end = index_;
			const size_t start_word = clear_start / BITS_PER_WORD;
			const size_t end_word = clear_end / BITS_PER_WORD;
			const size_t start_bit = clear_start % BITS_PER_WORD;
			const size_t end_bit = clear_end % BITS_PER_WORD;

			if (start_word == end_word) {
				uint64_t mask = ~0ull << start_bit;
				if (end_bit != 0) { mask &= (1ull << end_bit) - 1; }
				sparse_bits_[start_word] &= ~mask;
				live_bits_[start_word] &= ~mask;
			}
			else {
				if (start_bit != 0) {
					sparse_bits_[start_word] &= (1ull << start_bit) - 1;
					live_bits_[start_word] &= (1ull << start_bit) - 1;
				} else {
					sparse_bits_[start_word] = 0;
					live_bits_[start_word] = 0;
				}
				for (size_t w = start_word + 1; w < end_word; ++w) {
					sparse_bits_[w] = 0;
					live_bits_[w] = 0;
				}
				if (end_bit != 0) {
					sparse_bits_[end_word] &= ~((1ull << end_bit) - 1);
					live_bits_[end_word] &= ~((1ull << end_bit) - 1);
				}
			}
		}
		else if (dense) [[likely]] {
			std::move(data_ptr_ + real_end, data_ptr_ + index_, data_ptr_ + start_index);
			std::destroy_n(data_ptr_ + index_ - gap, gap);
			const size_t clear_start = index_ - gap;
			const size_t clear_end = index_;
			const size_t start_word = clear_start / BITS_PER_WORD;
			const size_t end_word = clear_end / BITS_PER_WORD;
			const size_t start_bit = clear_start % BITS_PER_WORD;
			const size_t end_bit = clear_end % BITS_PER_WORD;

			if (start_word == end_word) {
				uint64_t mask = ~0ull << start_bit;
				if (end_bit != 0) { mask &= (1ull << end_bit) - 1; }
				sparse_bits_[start_word] &= ~mask;
				live_bits_[start_word] &= ~mask;
			}
			else {
				if (start_bit != 0) {
					sparse_bits_[start_word] &= (1ull << start_bit) - 1;
					live_bits_[start_word] &= (1ull << start_bit) - 1;
				} else {
					sparse_bits_[start_word] = 0;
					live_bits_[start_word] = 0;
				}
				for (size_t w = start_word + 1; w < end_word; ++w) {
					sparse_bits_[w] = 0;
					live_bits_[w] = 0;
				}
				if (end_bit != 0) {
					sparse_bits_[end_word] &= ~((1ull << end_bit) - 1);
					live_bits_[end_word] &= ~((1ull << end_bit) - 1);
				}
			}
		}
		else {
			for (size_t i = start_index; i < real_end; ++i) {
				if (bitmap_test(live_bits_, i)) {
					data_ptr_[i].~T();
					bitmap_reset(live_bits_, i);
				}
			}
			size_t dst = start_index;
			size_t src = real_end;
			const size_t total_words = (index_ + BITS_PER_WORD - 1) / BITS_PER_WORD;
			while (src < index_) {
				size_t word_idx = src / BITS_PER_WORD;
				size_t bit_in_word = src % BITS_PER_WORD;
				uint64_t word = sparse_bits_[word_idx] >> bit_in_word;
				if (word == 0) {
					size_t next_word = word_idx + 1;
					if (bit_in_word == 0) {
#ifdef __AVX2__
						for (; next_word + 4 <= total_words; next_word += 4) {
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
					if (bitmap_test(live_bits_, dst)) {
						data_ptr_[dst].~T();
						bitmap_reset(live_bits_, dst);
					}
					new (&data_ptr_[dst]) T(std::move(data_ptr_[src]));
					bitmap_set(live_bits_, dst);
					data_ptr_[src].~T();
					bitmap_reset(live_bits_, src);
				}
				++dst;
				++src;
			}

			for (size_t i = start_index; i < dst; ++i) {
				bitmap_set(sparse_bits_, i);
				bitmap_set(live_bits_, i);
			}
			for (size_t i = dst; i < index_; ++i) {
				bitmap_reset(sparse_bits_, i);
				bitmap_reset(live_bits_, i);
			}
			hole_count_ = static_cast<size_t>(-1);
		}

		index_ -= gap;
	}
	else {
		for (size_t i = start_index; i < real_end; ++i) {
			if (bitmap_test(live_bits_, i)) {
				data_ptr_[i].~T();
			}
			bitmap_reset(sparse_bits_, i);
			bitmap_reset(live_bits_, i);
		}
		index_ -= gap;
	}

	update_dense_status();
	return iterator(data_ptr_ + start_index, data_ptr_ + index_,
	                is_dense_ ? nullptr : sparse_bits_, data_ptr_);
}

template <typename T>
void class_pool<T>::swap(class_pool& other) noexcept {
	std::swap(data_ptr_, other.data_ptr_);
	std::swap(sparse_bits_, other.sparse_bits_);
	std::swap(live_bits_, other.live_bits_);
	std::swap(maximum_quantity_, other.maximum_quantity_);
	std::swap(index_, other.index_);
	std::swap(is_dense_, other.is_dense_);
	std::swap(count_cache_, other.count_cache_);
	std::swap(hole_count_, other.hole_count_);
	std::swap(first_hole_hint_, other.first_hole_hint_);
	std::swap(inline_bits_, other.inline_bits_);
	std::swap(inline_live_bits_, other.inline_live_bits_);

	if (is_inline_bitmap()) {
		sparse_bits_ = &inline_bits_;
		live_bits_ = &inline_live_bits_;
	}
	if (other.is_inline_bitmap()) {
		other.sparse_bits_ = &other.inline_bits_;
		other.live_bits_ = &other.inline_live_bits_;
	}
}

template <typename T>
void class_pool<T>::pop_back() noexcept {
	invalidate_count_cache();
	if (index_ > 0) [[likely]] {
		const size_t idx = index_ - 1;
		if (is_dense_) [[likely]] {
			data_ptr_[idx].~T();
			bitmap_reset(sparse_bits_, idx);
			bitmap_reset(live_bits_, idx);
			--index_;
			return;
		}
		if (bitmap_test(live_bits_, idx)) {
			data_ptr_[idx].~T();
			bitmap_reset(live_bits_, idx);
		}
		if (bitmap_test(sparse_bits_, idx)) {
			bitmap_reset(sparse_bits_, idx);
		}
		else if (hole_count_ > 0 && hole_count_ != static_cast<size_t>(-1)) {
			--hole_count_;
		}
		else {
			hole_count_ = static_cast<size_t>(-1);
		}
		--index_;
	}
	update_dense_status();
}

template <typename T>
template <typename... Args>
T& class_pool<T>::emplace_at(size_t index, Args&&... args) noexcept {
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
		if (hole_count_ != static_cast<size_t>(-1)) {
			hole_count_ += (index - old_usage);
		}
		if (index > old_usage) {
			is_dense_ = 0;
		}
	}

	if (is_dense_ && !extended) [[likely]] {
		return data_ptr_[index];
	}

	if (bitmap_test(sparse_bits_, index)) [[likely]] {
		return data_ptr_[index];
	}

	if (bitmap_test(live_bits_, index)) {
		data_ptr_[index].~T();
		bitmap_reset(live_bits_, index);
	}
	new (&data_ptr_[index]) T(std::forward<Args>(args)...);
	bitmap_set(sparse_bits_, index);
	bitmap_set(live_bits_, index);
	if (!extended) {
		--hole_count_;
	}
	update_dense_status();
	return data_ptr_[index];
}

template <typename T>
template <typename... Args>
T& class_pool<T>::sparse_emplace_at(size_t index, Args&&... args) noexcept {
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
		if (hole_count_ != static_cast<size_t>(-1)) {
			hole_count_ += (index - old_usage);
		}
		if (index > old_usage) {
			is_dense_ = 0;
		}
	}

	if (is_dense_ && !extended) [[likely]] {
		if constexpr (!std::is_trivially_destructible_v<T>) {
			data_ptr_[index].~T();
			bitmap_reset(live_bits_, index);
		}
		new (&data_ptr_[index]) T(std::forward<Args>(args)...);
		bitmap_set(sparse_bits_, index);
		bitmap_set(live_bits_, index);
		return data_ptr_[index];
	}

	if (bitmap_test(sparse_bits_, index)) {
		data_ptr_[index].~T();
		bitmap_reset(live_bits_, index);
	}
	else if (!extended) {
		--hole_count_;
		if (bitmap_test(live_bits_, index)) {
			data_ptr_[index].~T();
			bitmap_reset(live_bits_, index);
		}
	}

	new (&data_ptr_[index]) T(std::forward<Args>(args)...);
	bitmap_set(sparse_bits_, index);
	bitmap_set(live_bits_, index);
	update_dense_status();
	return data_ptr_[index];
}

template <typename T>
void class_pool<T>::sparse_erase_at(size_t index) noexcept {
	invalidate_count_cache();
	if (index >= maximum_quantity_) { return; }
	const bool was_allocated = bitmap_test(sparse_bits_, index);
	const bool was_alive = bitmap_test(live_bits_, index);
	if (!was_allocated && !was_alive) { return; }
	if (was_alive) {
		data_ptr_[index].~T();
	}
	bitmap_reset(sparse_bits_, index);
	bitmap_reset(live_bits_, index);
	if (was_allocated) {
		++hole_count_;
		is_dense_ = 0;
	}
}

template <typename T>
void class_pool<T>::soft_sparse_delete(size_t index) noexcept {
	invalidate_count_cache();
	if (index < maximum_quantity_ && bitmap_test(sparse_bits_, index)) {
		bitmap_reset(sparse_bits_, index);
		++hole_count_;
		is_dense_ = 0;
	}
}

template <typename T>
void class_pool<T>::soft_dense_delete(size_t start, size_t end) noexcept {
	if (start >= end) { return; }
	invalidate_count_cache();
	if (end > maximum_quantity_) { end = maximum_quantity_; }
	if (start >= end) { return; }

	const size_t start_word = start / BITS_PER_WORD;
	const size_t end_word = (end - 1) / BITS_PER_WORD;

	for (size_t w = start_word; w <= end_word; ++w) {
		uint64_t mask = ~0ull;
		if (w == start_word) {
			mask &= ~((1ull << (start % BITS_PER_WORD)) - 1);
		}
		if (w == end_word) {
			mask &= (end % BITS_PER_WORD == 0) ? ~0ull : ((1ull << (end % BITS_PER_WORD)) - 1);
		}
		sparse_bits_[w] &= ~mask;
	}

	hole_count_ += (end - start);
	is_dense_ = 0;
}

template <typename T>
template <typename... Args>
T& class_pool<T>::fill_the_hole(Args&&... args) noexcept {
	static_assert(std::is_constructible_v<T, Args...>,
		"T must be constructible from the provided arguments");
	const size_t idx = find_first_hole_();
	if (idx == static_cast<size_t>(-1)) {
		emplace_back(std::forward<Args>(args)...);
		return back();
	}
	return emplace_at(idx, std::forward<Args>(args)...);
}

template <typename T>
template <typename... Args>
size_t class_pool<T>::fill_the_hole_at(Args&&... args) noexcept {
	static_assert(std::is_constructible_v<T, Args...>,
		"T must be constructible from the provided arguments");
	const size_t idx = find_first_hole_();
	if (idx == static_cast<size_t>(-1)) {
		emplace_back(std::forward<Args>(args)...);
		return index_ - 1;
	}
	emplace_at(idx, std::forward<Args>(args)...);
	return idx;
}

template <typename T>
size_t class_pool<T>::find_first_hole_() noexcept
{
	if (hole_count_ == 0) [[likely]]
	{
		return static_cast<size_t>(-1);
	}
	const size_t full_words = index_ / BITS_PER_WORD;
	for (size_t w = first_hole_hint_; w < full_words; ++w)
	{
		const uint64_t word = sparse_bits_[w];
		if (word != ~0ull)
		{
			first_hole_hint_ = w;
			return w * BITS_PER_WORD + std::countr_one(word);
		}
	}
	for (size_t w = 0; w < first_hole_hint_ && w < full_words; ++w)
	{
		const uint64_t word = sparse_bits_[w];
		if (word != ~0ull)
		{
			first_hole_hint_ = w;
			return w * BITS_PER_WORD + std::countr_one(word);
		}
	}
	const size_t tail = index_ % BITS_PER_WORD;
	if (tail != 0)
	{
		const uint64_t mask = (1ull << tail) - 1;
		const uint64_t valid = sparse_bits_[full_words] & mask;
		if (valid != mask)
		{
			first_hole_hint_ = full_words;
			return full_words * BITS_PER_WORD + std::countr_one(valid);
		}
	}
	return static_cast<size_t>(-1);
}

template <typename T>
void class_pool<T>::recompute_is_dense() noexcept {
	if (index_ == 0) { is_dense_ = 1; hole_count_ = 0; return; }

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
		if (mask != static_cast<int>(0xFFFFFFFFu)) {
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

template <typename T>
void class_pool<T>::bulk_set_bits(size_t start, size_t end) noexcept {
	size_t sw = start / BITS_PER_WORD;
	size_t ew = (end - 1) / BITS_PER_WORD;
	if (sw == ew) {
		uint64_t mask = ((1ull << (end % BITS_PER_WORD)) - 1) ^ ((1ull << (start % BITS_PER_WORD)) - 1);
		sparse_bits_[sw] |= mask;
		live_bits_[sw] |= mask;
	}
	else {
		if (start % BITS_PER_WORD != 0) {
			uint64_t mask = ~((1ull << (start % BITS_PER_WORD)) - 1);
			sparse_bits_[sw] |= mask;
			live_bits_[sw] |= mask;
		}
		size_t w = sw + 1;
#ifdef __AVX2__
		{
			const __m256i all_ones = _mm256_set1_epi64x(~0ull);
			for (; w + 4 <= ew; w += 4)
			{
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(sparse_bits_ + w), all_ones);
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(live_bits_ + w), all_ones);
			}
		}
#endif
		for (; w < ew; ++w) {
			sparse_bits_[w] = ~0ull;
			live_bits_[w] = ~0ull;
		}
		if (end % BITS_PER_WORD != 0) {
			uint64_t mask = (1ull << (end % BITS_PER_WORD)) - 1;
			sparse_bits_[ew] |= mask;
			live_bits_[ew] |= mask;
		}
	}
}

template <typename T>
void class_pool<T>::append_bulk(const T* src, size_t count) noexcept {
	if (count == 0) { return; }
	invalidate_count_cache();
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
	is_dense_ = 1;
	hole_count_ = 0;
}

template <typename T>
void class_pool<T>::append_bulk_move(T* src, size_t count) noexcept {
	if (count == 0) { return; }
	invalidate_count_cache();
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
	is_dense_ = 1;
	hole_count_ = 0;
}

template <typename T>
void class_pool<T>::append_incrementing(size_t count, uint64_t& counter) noexcept {
	static_assert(std::is_trivially_copyable_v<T>,
		"append_incrementing requires trivially copyable T");
	if (count == 0) { return; }
	invalidate_count_cache();
	if (index_ + count > maximum_quantity_) [[unlikely]] {
		increase_capacity(index_ + count);
	}
	for (size_t i = 0; i < count; ++i) {
		data_ptr_[index_ + i] = ++counter;
	}
	size_t end = index_ + count;
	bulk_set_bits(index_, end);
	index_ = end;
	is_dense_ = 1;
	hole_count_ = 0;
}

template <typename T>
template <typename F>
void class_pool<T>::append_generated(size_t count, F&& generator) noexcept {
	if (count == 0) { return; }
	invalidate_count_cache();
	if (index_ + count > maximum_quantity_) [[unlikely]] {
		increase_capacity(index_ + count);
	}
	for (size_t i = 0; i < count; ++i) {
		new (&data_ptr_[index_ + i]) T(generator());
	}
	size_t end = index_ + count;
	bulk_set_bits(index_, end);
	index_ = end;
	is_dense_ = 1;
	hole_count_ = 0;
}

template <typename T>
template <typename EntityLike>
void class_pool<T>::append_indices_from(const EntityLike* entities, size_t count) noexcept {
	static_assert(std::is_trivially_copyable_v<T>,
		"append_indices_from requires trivially copyable T");
	if (count == 0) { return; }
	invalidate_count_cache();
	if (index_ + count > maximum_quantity_) [[unlikely]] {
		increase_capacity(index_ + count);
	}
	for (size_t i = 0; i < count; ++i) {
		data_ptr_[index_ + i] = static_cast<T>(entities[i].parts_.index_);
	}
	size_t end = index_ + count;
	bulk_set_bits(index_, end);
	index_ = end;
	is_dense_ = 1;
	hole_count_ = 0;
}

template <typename T>
void class_pool<T>::fill_bulk(const T& value, size_t start, size_t count) noexcept {
	if (count == 0) { return; }
	invalidate_count_cache();
	size_t end = start + count;
	if (end > maximum_quantity_) [[unlikely]] {
		increase_capacity(end);
	}
	if (end > index_) {
		for (size_t i = index_; i < start; ++i) {
			bitmap_set(sparse_bits_, i);
			bitmap_set(live_bits_, i);
			new (&data_ptr_[i]) T{};
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
		for (size_t i = 0; i < count; ++i) {
			if (bitmap_test(live_bits_, start + i)) {
				data_ptr_[start + i].~T();
			}
			new (&data_ptr_[start + i]) T(value);
		}
	}
	bulk_set_bits(start, end);
	is_dense_ = 1;
	hole_count_ = 0;
}

template <typename T>
void class_pool<T>::prepare_dense(size_t new_size) noexcept {
	invalidate_count_cache();
	if (new_size > maximum_quantity_) [[unlikely]] {
		grow_data_and_bitmap(calculate_growth_for_reserve(new_size));
	}
	if (new_size > index_) {
		if constexpr (!std::is_trivially_destructible_v<T>) {
			for (size_t i = index_; i < new_size; ++i) {
				new (&data_ptr_[i]) T{};
			}
		}
		bulk_set_bits(index_, new_size);
		index_ = new_size;
	}
	is_dense_ = 1;
	hole_count_ = 0;
}

template <typename T>
void class_pool<T>::update_dense_status() noexcept {
	if (is_dense_) { return; }
	if (hole_count_ == static_cast<size_t>(-1)) {
		recompute_is_dense();
	}
	else if (hole_count_ == 0) {
		is_dense_ = 1;
	}
}

template <typename T>
void swap(class_pool<T>& a, class_pool<T>& b) noexcept {
	a.swap(b);
}