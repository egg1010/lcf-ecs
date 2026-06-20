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

template <typename T>
class class_pool
{
private:
    static constexpr size_t BITS_PER_WORD = 64;

    T* data_ptr_{nullptr};
    uint64_t* sparse_bits_{nullptr};
    size_t maximum_quantity_{0};
    size_t usage_{0};
    bool is_dense_{true};
    mutable size_t count_cache_{static_cast<size_t>(-1)};

    static constexpr size_t DEFAULT_CAPACITY = 8;
    static constexpr size_t SMALL_CAPACITY_THRESHOLD = 1024;
    static constexpr size_t MEDIUM_CAPACITY_THRESHOLD = 65536;

    [[nodiscard]] static constexpr size_t round_up_to_default(size_t n) noexcept
    {
        return n == 0 ? DEFAULT_CAPACITY : n;
    }

    [[nodiscard]] static constexpr size_t calculate_new_capacity(size_t current) noexcept
    {
        if (current == 0) return DEFAULT_CAPACITY;
        if (current < SMALL_CAPACITY_THRESHOLD) return current * 2;
        if (current < MEDIUM_CAPACITY_THRESHOLD) return current + (current >> 1);
        return current + (current >> 2);
    }

    [[nodiscard]] static constexpr size_t calculate_growth_for_reserve(size_t required) noexcept
    {
        if (required <= DEFAULT_CAPACITY) return DEFAULT_CAPACITY;
        size_t capacity = DEFAULT_CAPACITY;
        while (capacity < required)
        {
            capacity = calculate_new_capacity(capacity);
        }
        return capacity;
    }

    [[nodiscard]] static constexpr size_t bitmap_word_count(size_t num_bits) noexcept
    {
        return (num_bits + BITS_PER_WORD - 1) / BITS_PER_WORD;
    }

    [[nodiscard]] static uint64_t* allocate_bitmap(size_t num_bits) noexcept
    {
        if (num_bits == 0) return nullptr;
        const size_t words = bitmap_word_count(num_bits);
        const size_t bytes = words * sizeof(uint64_t);
        uint64_t* ptr = static_cast<uint64_t*>(
            ::operator new(bytes, std::align_val_t{alignof(uint64_t)}, std::nothrow));
        if (ptr == nullptr) [[unlikely]]
        {
            std::terminate();
        }
        std::memset(std::assume_aligned<alignof(uint64_t)>(ptr), 0, bytes);
        return ptr;
    }

    static void deallocate_bitmap(uint64_t* ptr, size_t num_bits) noexcept
    {
        if (ptr != nullptr)
        {
            ::operator delete(ptr, bitmap_word_count(num_bits) * sizeof(uint64_t),
                              std::align_val_t{alignof(uint64_t)});
        }
    }

    static void copy_bitmap(uint64_t* dst, const uint64_t* src, size_t num_bits) noexcept
    {
        if (num_bits == 0) return;
        const size_t words = bitmap_word_count(num_bits);
        std::memcpy(std::assume_aligned<alignof(uint64_t)>(dst),
                    std::assume_aligned<alignof(uint64_t)>(src),
                    words * sizeof(uint64_t));
    }

    [[nodiscard]] static T* allocate_data(size_t count) noexcept
    {
        if (count == 0) return nullptr;
        T* ptr = static_cast<T*>(
            ::operator new(count * sizeof(T), std::align_val_t{alignof(T)}, std::nothrow));
        if (ptr == nullptr) [[unlikely]]
        {
            std::terminate();
        }
        return std::assume_aligned<alignof(T)>(ptr);
    }

    static void deallocate_data(T* ptr, size_t count) noexcept
    {
        if (ptr != nullptr)
        {
            ::operator delete(ptr, count * sizeof(T), std::align_val_t{alignof(T)});
        }
    }

    [[nodiscard]] static constexpr bool bitmap_test(const uint64_t* bits, size_t index) noexcept
    {
        return (bits[index / BITS_PER_WORD] >> (index % BITS_PER_WORD)) & 1ull;
    }

    static constexpr void bitmap_set(uint64_t* bits, size_t index) noexcept
    {
        bits[index / BITS_PER_WORD] |= (1ull << (index % BITS_PER_WORD));
    }

    static constexpr void bitmap_reset(uint64_t* bits, size_t index) noexcept
    {
        bits[index / BITS_PER_WORD] &= ~(1ull << (index % BITS_PER_WORD));
    }

    static void bitmap_shift_right_one(uint64_t* bits, size_t start, size_t end) noexcept
    {
        if (start >= end) return;

        const size_t start_word = start / BITS_PER_WORD;
        const size_t start_bit  = start % BITS_PER_WORD;
        const size_t end_word   = end / BITS_PER_WORD;
        const size_t end_bit    = end % BITS_PER_WORD;

        if (start_word == end_word) [[unlikely]]
        {
            for (size_t i = end; i > start; --i)
            {
                if (bitmap_test(bits, i - 1))
                    bitmap_set(bits, i);
                else
                    bitmap_reset(bits, i);
            }
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

        for (size_t w = start_word + 1; w < end_word; ++w)
        {
            uint64_t word = bits[w];
            uint64_t new_carry = (word >> 63) & 1ull;
            bits[w] = (word << 1) | carry;
            carry = new_carry;
        }

        if (end_bit == 0) [[unlikely]]
        {
            if (carry) bits[end_word] |= 1ull;
            else       bits[end_word] &= ~1ull;
        }
        else
        {
            uint64_t word = bits[end_word];
            uint64_t mask = (1ull << end_bit) - 1;
            uint64_t in_range = word & mask;
            uint64_t shifted = in_range << 1;
            if (carry) shifted |= 1ull;
            uint64_t clear_mask = (end_bit < 63) ? ((1ull << (end_bit + 1)) - 1) : ~0ull;
            bits[end_word] = (word & ~clear_mask) | (shifted & clear_mask);
        }
    }

    static void bitmap_shift_left(uint64_t* bits, size_t start, size_t end, size_t shift) noexcept
    {
        if (shift == 0 || start >= end) return;
        const size_t new_end = end - shift;
        if (start >= new_end)
        {
            const size_t start_word = start / BITS_PER_WORD;
            const size_t start_bit = start % BITS_PER_WORD;
            const size_t end_word = end / BITS_PER_WORD;
            const size_t end_bit = end % BITS_PER_WORD;

            if (start_word == end_word)
            {
                uint64_t mask = ((end_bit == 0 ? 0ull : (1ull << end_bit)) - (1ull << start_bit));
                bits[start_word] &= ~mask;
            }
            else
            {
                if (start_bit != 0)
                    bits[start_word] &= (1ull << start_bit) - 1;
                for (size_t w = start_word + 1; w < end_word; ++w)
                    bits[w] = 0;
                if (end_bit != 0)
                    bits[end_word] &= ~((1ull << end_bit) - 1);
            }
            return;
        }

        const size_t shift_words = shift / BITS_PER_WORD;
        const size_t shift_bits = shift % BITS_PER_WORD;

        const size_t first_dst_word = start / BITS_PER_WORD;
        const size_t first_dst_bit = start % BITS_PER_WORD;
        const size_t last_dst_word = (new_end - 1) / BITS_PER_WORD;

        if (shift_bits == 0)
        {
            for (size_t dw = last_dst_word; dw >= first_dst_word; --dw)
            {
                size_t dst_bit_offset = (dw == first_dst_word) ? first_dst_bit : 0;
                size_t bits_in_dst_word = (dw == last_dst_word)
                    ? ((new_end - 1) % BITS_PER_WORD + 1 - dst_bit_offset)
                    : (BITS_PER_WORD - dst_bit_offset);

                size_t src_pos = dw * BITS_PER_WORD + dst_bit_offset + shift;
                size_t sw = src_pos / BITS_PER_WORD;
                size_t sb = src_pos % BITS_PER_WORD;

                uint64_t val = 0;
                size_t bits_read = 0;
                while (bits_read < bits_in_dst_word)
                {
                    size_t avail = BITS_PER_WORD - sb;
                    size_t to_read = std::min(avail, bits_in_dst_word - bits_read);
                    uint64_t chunk = bits[sw] >> sb;
                    if (to_read < BITS_PER_WORD)
                        chunk &= (1ull << to_read) - 1;
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
        else
        {
            if (shift_words > 0)
            {
                for (size_t dw = last_dst_word; dw >= first_dst_word; --dw)
                {
                    size_t dst_bit_offset = (dw == first_dst_word) ? first_dst_bit : 0;
                    size_t bits_in_dst_word = (dw == last_dst_word)
                        ? ((new_end - 1) % BITS_PER_WORD + 1 - dst_bit_offset)
                        : (BITS_PER_WORD - dst_bit_offset);

                    size_t src_pos = dw * BITS_PER_WORD + dst_bit_offset + shift_words * BITS_PER_WORD;
                    size_t sw = src_pos / BITS_PER_WORD;
                    size_t sb = src_pos % BITS_PER_WORD;

                    uint64_t val = 0;
                    size_t bits_read = 0;
                    while (bits_read < bits_in_dst_word)
                    {
                        size_t avail = BITS_PER_WORD - sb;
                        size_t to_read = std::min(avail, bits_in_dst_word - bits_read);
                        uint64_t chunk = bits[sw] >> sb;
                        if (to_read < BITS_PER_WORD)
                            chunk &= (1ull << to_read) - 1;
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

            uint64_t carry = 0;
            for (size_t w = first_dst_word; w <= last_dst_word; ++w)
            {
                uint64_t word = bits[w];
                uint64_t shifted = (word >> shift_bits) | carry;
                carry = word << (BITS_PER_WORD - shift_bits);

                if (w == first_dst_word && first_dst_bit != 0)
                {
                    uint64_t low_mask = (1ull << first_dst_bit) - 1;
                    bits[w] = (word & low_mask) | (shifted & ~low_mask);
                }
                else
                {
                    bits[w] = shifted;
                }
            }
        }

        {
            const size_t clear_start_word = new_end / BITS_PER_WORD;
            const size_t clear_start_bit = new_end % BITS_PER_WORD;
            const size_t clear_end_word = end / BITS_PER_WORD;
            const size_t clear_end_bit = end % BITS_PER_WORD;

            if (clear_start_word == clear_end_word)
            {
                uint64_t mask = ((clear_end_bit == 0 ? 0ull : (1ull << clear_end_bit)) - (1ull << clear_start_bit));
                bits[clear_start_word] &= ~mask;
            }
            else
            {
                if (clear_start_bit != 0)
                    bits[clear_start_word] &= (1ull << clear_start_bit) - 1;
                for (size_t w = clear_start_word + 1; w < clear_end_word; ++w)
                    bits[w] = 0;
                if (clear_end_bit != 0)
                    bits[clear_end_word] &= ~((1ull << clear_end_bit) - 1);
            }
        }
    }

    void destroy_dense_range(size_t first, size_t last) noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<T>)
        {
            for (size_t i = first; i < last; ++i)
            {
                data_ptr_[i].~T();
            }
        }
    }

    void destroy_sparse_range(size_t first, size_t last) noexcept
    {
        if constexpr (std::is_trivially_destructible_v<T>) return;
        if (first >= last) return;

        const size_t start_word = first / BITS_PER_WORD;
        const size_t start_bit = first % BITS_PER_WORD;
        const size_t end_word = last / BITS_PER_WORD;
        const size_t end_bit = last % BITS_PER_WORD;

        if (start_word == end_word)
        {
            uint64_t mask = (end_bit == 0 ? 0ull : ((1ull << end_bit) - 1)) & ~((1ull << start_bit) - 1);
            uint64_t word = sparse_bits_[start_word] & mask;
            while (word != 0)
            {
                const size_t offset = std::countr_zero(word);
                data_ptr_[start_word * BITS_PER_WORD + offset].~T();
                word &= word - 1;
            }
            return;
        }

        if (start_bit != 0)
        {
            uint64_t word = sparse_bits_[start_word] & ~((1ull << start_bit) - 1);
            while (word != 0)
            {
                const size_t offset = std::countr_zero(word);
                data_ptr_[start_word * BITS_PER_WORD + offset].~T();
                word &= word - 1;
            }
        }

        for (size_t w = start_word + (start_bit != 0 ? 1 : 0); w < end_word; ++w)
        {
            uint64_t word = sparse_bits_[w];
            while (word != 0)
            {
                const size_t offset = std::countr_zero(word);
                data_ptr_[w * BITS_PER_WORD + offset].~T();
                word &= word - 1;
            }
        }

        if (end_bit != 0)
        {
            uint64_t word = sparse_bits_[end_word] & ((1ull << end_bit) - 1);
            while (word != 0)
            {
                const size_t offset = std::countr_zero(word);
                data_ptr_[end_word * BITS_PER_WORD + offset].~T();
                word &= word - 1;
            }
        }
    }

    void destroy_all() noexcept
    {
        if constexpr (std::is_trivially_destructible_v<T>) return;
        if (usage_ == 0 || sparse_bits_ == nullptr) return;

        if (is_dense()) [[likely]]
        {
            destroy_dense_range(0, usage_);
        }
        else
        {
            destroy_sparse_range(0, usage_);
        }
    }

    void uninitialized_move_dense(T* src_first, T* src_last, T* dst) noexcept
    {
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            const size_t n = static_cast<size_t>(src_last - src_first);
            if (n != 0) [[likely]]
            {
                std::memcpy(std::assume_aligned<alignof(T)>(dst),
                            std::assume_aligned<alignof(T)>(src_first),
                            n * sizeof(T));
            }
        }
        else
        {
            std::uninitialized_move(src_first, src_last, dst);
            std::destroy(src_first, src_last);
        }
    }

    template <bool MoveAndDestroy>
    static void relocate_sparse(T* dst, const T* src, const uint64_t* src_bits, size_t count) noexcept
    {
        const size_t num_full_words = count / BITS_PER_WORD;
        for (size_t w = 0; w < num_full_words; ++w)
        {
            uint64_t word = src_bits[w];
            while (word != 0)
            {
                const size_t offset = std::countr_zero(word);
                const size_t i = w * BITS_PER_WORD + offset;
                if constexpr (MoveAndDestroy)
                {
                    new (&dst[i]) T(std::move(const_cast<T&>(src[i])));
                    const_cast<T&>(src[i]).~T();
                }
                else
                {
                    new (&dst[i]) T(src[i]);
                }
                word &= word - 1;
            }
        }
        const size_t tail = count % BITS_PER_WORD;
        if (tail != 0)
        {
            uint64_t word = src_bits[num_full_words] & ((1ull << tail) - 1);
            while (word != 0)
            {
                const size_t offset = std::countr_zero(word);
                const size_t i = num_full_words * BITS_PER_WORD + offset;
                if constexpr (MoveAndDestroy)
                {
                    new (&dst[i]) T(std::move(const_cast<T&>(src[i])));
                    const_cast<T&>(src[i]).~T();
                }
                else
                {
                    new (&dst[i]) T(src[i]);
                }
                word &= word - 1;
            }
        }
    }

    void grow_data_and_bitmap(size_t new_capacity) noexcept
    {
        if (new_capacity <= maximum_quantity_) [[likely]]
        {
            return;
        }

        T* new_data = allocate_data(new_capacity);
        uint64_t* new_bits = allocate_bitmap(new_capacity);

        if (data_ptr_ != nullptr) [[likely]]
        {
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                const size_t bytes = usage_ * sizeof(T);
                if (bytes != 0) [[likely]]
                {
                    std::memcpy(std::assume_aligned<alignof(T)>(new_data),
                                std::assume_aligned<alignof(T)>(data_ptr_),
                                bytes);
                }
            }
            else if (is_dense())
            {
                uninitialized_move_dense(data_ptr_, data_ptr_ + usage_, new_data);
            }
            else
            {
                relocate_sparse<true>(new_data, data_ptr_, sparse_bits_, usage_);
            }
            copy_bitmap(new_bits, sparse_bits_, maximum_quantity_);
            deallocate_data(data_ptr_, maximum_quantity_);
            deallocate_bitmap(sparse_bits_, maximum_quantity_);
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

        basic_iterator(const basic_iterator&) = default;

        basic_iterator(Ptr ptr, Ptr end, const uint64_t* bits, Ptr origin) noexcept
            : ptr_(ptr), end_(end), bits_(bits), origin_(origin)
        {
            if (bits_ != nullptr && ptr_ != end_ && !bitmap_test(bits_, static_cast<size_t>(ptr_ - origin_)))
                ++*this;
        }

        basic_iterator(const basic_iterator<false>& other) noexcept
            requires (IsConst)
            : ptr_(other.ptr_), end_(other.end_), bits_(other.bits_), origin_(other.origin_) {}

        Ref operator*() const noexcept { return *ptr_; }
        Ptr operator->() const noexcept { return ptr_; }

        basic_iterator& operator++() noexcept
        {
            if (bits_ != nullptr)
            {
                do { ++ptr_; } while (ptr_ != end_ && !bitmap_test(bits_, static_cast<size_t>(ptr_ - origin_)));
            }
            else
            {
                ++ptr_;
            }
            return *this;
        }

        basic_iterator operator++(int) noexcept
        {
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
        , usage_(0)
    {
        if (capacity > 0) [[likely]]
        {
            data_ptr_ = allocate_data(capacity);
            sparse_bits_ = allocate_bitmap(capacity);
        }
    }

    class_pool(size_t count, const T& value) noexcept
        : data_ptr_(nullptr)
        , sparse_bits_(nullptr)
        , maximum_quantity_(round_up_to_default(count))
        , usage_(0)
    {
        if (count > 0) [[likely]]
        {
            data_ptr_ = allocate_data(maximum_quantity_);
            sparse_bits_ = allocate_bitmap(maximum_quantity_);
            for (size_t i = 0; i < count; ++i)
            {
                new (&data_ptr_[i]) T(value);
                bitmap_set(sparse_bits_, i);
            }
            usage_ = count;
        }
    }

    template <typename InputIt>
    class_pool(InputIt first, InputIt last) noexcept
        : data_ptr_(nullptr)
        , sparse_bits_(nullptr)
        , maximum_quantity_(0)
        , usage_(0)
    {
        const size_t count = static_cast<size_t>(std::distance(first, last));
        maximum_quantity_ = round_up_to_default(count);

        if (count > 0) [[likely]]
        {
            data_ptr_ = allocate_data(maximum_quantity_);
            sparse_bits_ = allocate_bitmap(maximum_quantity_);
            size_t i = 0;
            for (auto it = first; it != last; ++it, ++i)
            {
                new (&data_ptr_[i]) T(*it);
                bitmap_set(sparse_bits_, i);
            }
            usage_ = count;
        }
    }

    class_pool(std::initializer_list<T> init) noexcept
        : class_pool(init.begin(), init.end()) {}

    class_pool(const class_pool& other) noexcept
        : data_ptr_(nullptr)
        , sparse_bits_(nullptr)
        , maximum_quantity_(other.maximum_quantity_)
        , usage_(other.usage_)
        , is_dense_(other.is_dense_)
        , count_cache_(other.count_cache_)
    {
        if (maximum_quantity_ > 0) [[likely]]
        {
            data_ptr_ = allocate_data(maximum_quantity_);
            sparse_bits_ = allocate_bitmap(maximum_quantity_);

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                const size_t bytes = other.usage_ * sizeof(T);
                if (bytes != 0) [[likely]]
                {
                    std::memcpy(std::assume_aligned<alignof(T)>(data_ptr_),
                                std::assume_aligned<alignof(T)>(other.data_ptr_),
                                bytes);
                }
            }
            else if (other.is_dense_) [[likely]]
            {
                std::uninitialized_copy(other.data_ptr_, other.data_ptr_ + other.usage_, data_ptr_);
            }
            else
            {
                relocate_sparse<false>(data_ptr_, other.data_ptr_, other.sparse_bits_, other.usage_);
            }
            copy_bitmap(sparse_bits_, other.sparse_bits_, other.maximum_quantity_);
        }
    }

    class_pool(class_pool&& other) noexcept
        : data_ptr_(other.data_ptr_)
        , sparse_bits_(other.sparse_bits_)
        , maximum_quantity_(other.maximum_quantity_)
        , usage_(other.usage_)
        , is_dense_(other.is_dense_)
        , count_cache_(other.count_cache_)
    {
        other.data_ptr_ = nullptr;
        other.sparse_bits_ = nullptr;
        other.maximum_quantity_ = 0;
        other.usage_ = 0;
        other.is_dense_ = true;
        other.count_cache_ = static_cast<size_t>(-1);
    }

    class_pool& operator=(const class_pool& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            destroy_all();
            deallocate_data(data_ptr_, maximum_quantity_);
            deallocate_bitmap(sparse_bits_, maximum_quantity_);

            maximum_quantity_ = other.maximum_quantity_;
            usage_ = other.usage_;
            is_dense_ = other.is_dense_;
            count_cache_ = other.count_cache_;

            if (maximum_quantity_ > 0) [[likely]]
            {
                data_ptr_ = allocate_data(maximum_quantity_);
                sparse_bits_ = allocate_bitmap(maximum_quantity_);

                if constexpr (std::is_trivially_copyable_v<T>)
                {
                    const size_t bytes = other.usage_ * sizeof(T);
                    if (bytes != 0) [[likely]]
                    {
                        std::memcpy(std::assume_aligned<alignof(T)>(data_ptr_),
                                    std::assume_aligned<alignof(T)>(other.data_ptr_),
                                    bytes);
                    }
                }
                else if (other.is_dense_) [[likely]]
                {
                    std::uninitialized_copy(other.data_ptr_, other.data_ptr_ + other.usage_, data_ptr_);
                }
                else
                {
                    relocate_sparse<false>(data_ptr_, other.data_ptr_, other.sparse_bits_, other.usage_);
                }
                copy_bitmap(sparse_bits_, other.sparse_bits_, other.maximum_quantity_);
            }
            else
            {
                data_ptr_ = nullptr;
                sparse_bits_ = nullptr;
            }
        }
        return *this;
    }

    class_pool& operator=(class_pool&& other) noexcept
    {
        if (this != &other) [[likely]]
        {
            destroy_all();
            deallocate_data(data_ptr_, maximum_quantity_);
            deallocate_bitmap(sparse_bits_, maximum_quantity_);

            data_ptr_ = other.data_ptr_;
            sparse_bits_ = other.sparse_bits_;
            maximum_quantity_ = other.maximum_quantity_;
            usage_ = other.usage_;
            is_dense_ = other.is_dense_;
            count_cache_ = other.count_cache_;

            other.data_ptr_ = nullptr;
            other.sparse_bits_ = nullptr;
            other.maximum_quantity_ = 0;
            other.usage_ = 0;
            other.is_dense_ = true;
            other.count_cache_ = static_cast<size_t>(-1);
        }
        return *this;
    }

    ~class_pool() noexcept
    {
        destroy_all();
        deallocate_data(data_ptr_, maximum_quantity_);
        deallocate_bitmap(sparse_bits_, maximum_quantity_);
    }

    template <typename... Args>
    inline void emplace_back(Args&&... args) noexcept
    {
        invalidate_count_cache();
        static_assert(std::is_constructible_v<T, Args...>,
                     "T must be constructible from the provided arguments");

        if (usage_ >= maximum_quantity_) [[unlikely]]
        {
            grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
        }
        new (&data_ptr_[usage_]) T(std::forward<Args>(args)...);
        bitmap_set(sparse_bits_, usage_);
        ++usage_;
    }

    inline void clear() noexcept
    {
        destroy_all();
        invalidate_count_cache();
        if (sparse_bits_ != nullptr && maximum_quantity_ > 0) [[likely]]
        {
            const size_t words = bitmap_word_count(maximum_quantity_);
            std::memset(std::assume_aligned<alignof(uint64_t)>(sparse_bits_), 0,
                        words * sizeof(uint64_t));
        }
        usage_ = 0;
        is_dense_ = true;
    }

    [[nodiscard]] constexpr T* get(size_t index) noexcept
    {
        return &data_ptr_[index];
    }

    [[nodiscard]] constexpr const T* get(size_t index) const noexcept
    {
        return &data_ptr_[index];
    }

    [[nodiscard]] constexpr size_type capacity() const noexcept { return maximum_quantity_; }
    [[nodiscard]] constexpr size_type sparse_capacity() const noexcept { return maximum_quantity_; }
    [[nodiscard]] constexpr size_type size() const noexcept { return usage_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return usage_ == 0; }

    [[nodiscard]] size_type count() const noexcept
    {
        if (count_cache_ != static_cast<size_t>(-1)) [[likely]]
        {
            return count_cache_;
        }
        if (sparse_bits_ == nullptr || usage_ == 0) { count_cache_ = 0; return 0; }
        const size_t full_words = usage_ / BITS_PER_WORD;
        size_type total = 0;
        for (size_t i = 0; i < full_words; ++i)
        {
            total += std::popcount(sparse_bits_[i]);
        }
        const size_t tail = usage_ % BITS_PER_WORD;
        if (tail != 0)
        {
            const uint64_t mask = (1ull << tail) - 1;
            total += std::popcount(sparse_bits_[full_words] & mask);
        }
        count_cache_ = total;
        return total;
    }

    void invalidate_count_cache() noexcept
    {
        count_cache_ = static_cast<size_t>(-1);
    }

    [[nodiscard]] constexpr pointer data() noexcept
    {
        return std::assume_aligned<alignof(T)>(data_ptr_);
    }

    [[nodiscard]] constexpr const_pointer data() const noexcept
    {
        return std::assume_aligned<alignof(T)>(data_ptr_);
    }

    inline void increase_capacity(size_t new_capacity) noexcept
    {
        if (new_capacity > maximum_quantity_) [[unlikely]]
        {
            grow_data_and_bitmap(calculate_growth_for_reserve(new_capacity));
        }
    }

    inline void increase_capacity(size_t new_capacity, const T& value) noexcept
    {
        invalidate_count_cache();
        if (new_capacity <= usage_) [[likely]]
        {
            return;
        }

        if (new_capacity > maximum_quantity_) [[unlikely]]
        {
            grow_data_and_bitmap(calculate_growth_for_reserve(new_capacity));
        }

        if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8)
        {
            for (size_t i = usage_; i < new_capacity; ++i)
            {
                std::memcpy(&data_ptr_[i], &value, sizeof(T));
                bitmap_set(sparse_bits_, i);
            }
        }
        else
        {
            for (size_t i = usage_; i < new_capacity; ++i)
            {
                new (&data_ptr_[i]) T(value);
                bitmap_set(sparse_bits_, i);
            }
        }
        usage_ = new_capacity;
    }

    inline void shrink_to_fit() noexcept
    {
        if (usage_ == 0 && data_ptr_ != nullptr) [[unlikely]]
        {
            deallocate_data(data_ptr_, maximum_quantity_);
            deallocate_bitmap(sparse_bits_, maximum_quantity_);
            data_ptr_ = nullptr;
            sparse_bits_ = nullptr;
            maximum_quantity_ = 0;
            return;
        }

        if (usage_ < maximum_quantity_ && usage_ > 0) [[likely]]
        {
            T* new_data = allocate_data(usage_);
            uint64_t* new_bits = allocate_bitmap(usage_);

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                const size_t bytes = usage_ * sizeof(T);
                if (bytes != 0) [[likely]]
                {
                    std::memcpy(std::assume_aligned<alignof(T)>(new_data),
                                std::assume_aligned<alignof(T)>(data_ptr_),
                                bytes);
                }
            }
            else if (is_dense())
            {
                uninitialized_move_dense(data_ptr_, data_ptr_ + usage_, new_data);
            }
            else
            {
                relocate_sparse<true>(new_data, data_ptr_, sparse_bits_, usage_);
            }
            copy_bitmap(new_bits, sparse_bits_, usage_);

            deallocate_data(data_ptr_, maximum_quantity_);
            deallocate_bitmap(sparse_bits_, maximum_quantity_);

            data_ptr_ = new_data;
            sparse_bits_ = new_bits;
            maximum_quantity_ = usage_;
        }
    }

    inline void reduce_capacity(size_t new_capacity) noexcept
    {
        invalidate_count_cache();
        if (new_capacity >= maximum_quantity_) [[likely]]
        {
            return;
        }

        if (new_capacity == 0)
        {
            destroy_all();
            deallocate_data(data_ptr_, maximum_quantity_);
            deallocate_bitmap(sparse_bits_, maximum_quantity_);
            data_ptr_ = nullptr;
            sparse_bits_ = nullptr;
            maximum_quantity_ = 0;
            usage_ = 0;
            is_dense_ = true;
            return;
        }

        if (new_capacity < usage_)
        {
            destroy_dense_range(new_capacity, usage_);
            for (size_t i = new_capacity; i < usage_; ++i)
            {
                bitmap_reset(sparse_bits_, i);
            }
            usage_ = new_capacity;
        }

        T* new_data = allocate_data(new_capacity);
        uint64_t* new_bits = allocate_bitmap(new_capacity);

        if constexpr (std::is_trivially_copyable_v<T>)
        {
            const size_t bytes = usage_ * sizeof(T);
            if (bytes != 0) [[likely]]
            {
                std::memcpy(std::assume_aligned<alignof(T)>(new_data),
                            std::assume_aligned<alignof(T)>(data_ptr_),
                            bytes);
            }
        }
        else if (is_dense())
        {
            uninitialized_move_dense(data_ptr_, data_ptr_ + usage_, new_data);
        }
        else
        {
            relocate_sparse<true>(new_data, data_ptr_, sparse_bits_, usage_);
        }
        copy_bitmap(new_bits, sparse_bits_, new_capacity);

        deallocate_data(data_ptr_, maximum_quantity_);
        deallocate_bitmap(sparse_bits_, maximum_quantity_);

        data_ptr_ = new_data;
        sparse_bits_ = new_bits;
        maximum_quantity_ = new_capacity;
    }

    inline void reduce_capacity(size_t new_capacity, class_pool<T>& dst) noexcept
    {
        invalidate_count_cache();
        if (new_capacity >= usage_) [[likely]]
        {
            return;
        }

        const size_t move_count = usage_ - new_capacity;
        dst.increase_capacity(dst.size() + move_count);

        if (is_dense())
        {
            for (size_t i = new_capacity; i < usage_; ++i)
            {
                dst.emplace_back(std::move(data_ptr_[i]));
            }
        }
        else
        {
            for (size_t i = new_capacity; i < usage_; ++i)
            {
                if (bitmap_test(sparse_bits_, i))
                {
                    dst.emplace_back(std::move(data_ptr_[i]));
                }
            }
        }

        destroy_dense_range(new_capacity, usage_);
        for (size_t i = new_capacity; i < usage_; ++i)
        {
            bitmap_reset(sparse_bits_, i);
        }
        usage_ = new_capacity;

        if (new_capacity < maximum_quantity_)
        {
            T* new_data = allocate_data(new_capacity);
            uint64_t* new_bits = allocate_bitmap(new_capacity);

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                const size_t bytes = usage_ * sizeof(T);
                if (bytes != 0) [[likely]]
                {
                    std::memcpy(std::assume_aligned<alignof(T)>(new_data),
                                std::assume_aligned<alignof(T)>(data_ptr_),
                                bytes);
                }
            }
            else if (is_dense())
            {
                uninitialized_move_dense(data_ptr_, data_ptr_ + usage_, new_data);
            }
            else
            {
                relocate_sparse<true>(new_data, data_ptr_, sparse_bits_, usage_);
            }
            copy_bitmap(new_bits, sparse_bits_, new_capacity);

            deallocate_data(data_ptr_, maximum_quantity_);
            deallocate_bitmap(sparse_bits_, maximum_quantity_);

            data_ptr_ = new_data;
            sparse_bits_ = new_bits;
            maximum_quantity_ = new_capacity;
        }
    }

    [[nodiscard]] inline T& at(size_t index) noexcept
    {
        if (index >= usage_) [[unlikely]]
        {
            std::terminate();
        }
        return data_ptr_[index];
    }

    [[nodiscard]] inline const T& at(size_t index) const noexcept
    {
        if (index >= usage_) [[unlikely]]
        {
            std::terminate();
        }
        return data_ptr_[index];
    }

    [[nodiscard]] constexpr T& front() noexcept
    {
        return data_ptr_[0];
    }

    [[nodiscard]] constexpr const T& front() const noexcept
    {
        return data_ptr_[0];
    }

    [[nodiscard]] constexpr T& back() noexcept
    {
        return data_ptr_[usage_ - 1];
    }

    [[nodiscard]] constexpr const T& back() const noexcept
    {
        return data_ptr_[usage_ - 1];
    }

    inline void resize(size_t new_capacity) noexcept
    {
        invalidate_count_cache();
        grow_data_and_bitmap(new_capacity);
    }

    inline void resize(size_t new_size, const T& value) noexcept
    {
        invalidate_count_cache();
        if (new_size <= usage_) [[unlikely]]
        {
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                for (size_t i = new_size; i < usage_; ++i)
                {
                    data_ptr_[i].~T();
                }
            }
            for (size_t i = new_size; i < usage_; ++i)
            {
                bitmap_reset(sparse_bits_, i);
            }
            usage_ = new_size;
            return;
        }

        if (new_size > maximum_quantity_) [[unlikely]]
        {
            grow_data_and_bitmap(calculate_growth_for_reserve(new_size));
        }

        if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 8)
        {
            for (size_t i = usage_; i < new_size; ++i)
            {
                std::memcpy(&data_ptr_[i], &value, sizeof(T));
                bitmap_set(sparse_bits_, i);
            }
        }
        else
        {
            for (size_t i = usage_; i < new_size; ++i)
            {
                new (&data_ptr_[i]) T(value);
                bitmap_set(sparse_bits_, i);
            }
        }
        usage_ = new_size;
    }

    template <typename... Args>
    inline iterator emplace(const_iterator pos, Args&&... args) noexcept
    {
        invalidate_count_cache();
        const size_t index = static_cast<size_t>(pos.ptr_ - data_ptr_);
        if (usage_ >= maximum_quantity_) [[unlikely]]
        {
            grow_data_and_bitmap(calculate_new_capacity(maximum_quantity_));
        }

        if (index < usage_) [[likely]]
        {
            const bool dense = is_dense();

            if constexpr (std::is_trivially_copyable_v<T>)
            {
                std::memmove(std::assume_aligned<alignof(T)>(data_ptr_ + index + 1),
                             std::assume_aligned<alignof(T)>(data_ptr_ + index),
                             (usage_ - index) * sizeof(T));
            }
            else if (dense) [[likely]]
            {
                new (&data_ptr_[usage_]) T(std::move(data_ptr_[usage_ - 1]));
                if (index < usage_ - 1)
                {
                    std::move_backward(data_ptr_ + index, data_ptr_ + usage_ - 1,
                                       data_ptr_ + usage_);
                }
                data_ptr_[index].~T();
            }
            else
            {
                const size_t w_begin = index / BITS_PER_WORD;
                const size_t w_end = (usage_ - 1) / BITS_PER_WORD;

                for (size_t w = w_end + 1; w-- > w_begin; )
                {
                    uint64_t word = sparse_bits_[w];
                    if (word == 0) continue;

                    size_t lo = (w == w_begin) ? (index % BITS_PER_WORD) : 0;
                    size_t hi = (w == w_end) ? ((usage_ - 1) % BITS_PER_WORD) : (BITS_PER_WORD - 1);
                    uint64_t mask = (~0ull << lo);
                    if (hi < 63) mask &= (1ull << (hi + 1)) - 1;
                    uint64_t bits = word & mask;

                    while (bits != 0)
                    {
                        const size_t bit_pos = static_cast<size_t>(63 - std::countl_zero(bits));
                        const size_t i = w * BITS_PER_WORD + bit_pos;

                        if (i + 1 < usage_)
                        {
                            if (bitmap_test(sparse_bits_, i + 1))
                                data_ptr_[i + 1] = std::move(data_ptr_[i]);
                            else
                                new (&data_ptr_[i + 1]) T(std::move(data_ptr_[i]));
                        }
                        else
                        {
                            new (&data_ptr_[usage_]) T(std::move(data_ptr_[i]));
                        }

                        if (i > index && !bitmap_test(sparse_bits_, i - 1))
                        {
                            data_ptr_[i].~T();
                        }

                        bits &= ~(1ull << bit_pos);
                    }
                }

                if (bitmap_test(sparse_bits_, index))
                {
                    data_ptr_[index].~T();
                }
            }

            if (dense) [[likely]]
            {
                bitmap_set(sparse_bits_, usage_);
            }
            else
            {
                bitmap_shift_right_one(sparse_bits_, index, usage_);
            }
        }

        new (data_ptr_ + index) T(std::forward<Args>(args)...);
        bitmap_set(sparse_bits_, index);
        ++usage_;
        return iterator(data_ptr_ + index, data_ptr_ + usage_, nullptr, data_ptr_);
    }

    inline iterator insert(const_iterator pos, const T& value) noexcept
    {
        return emplace(pos, value);
    }

    inline iterator insert(const_iterator pos, T&& value) noexcept
    {
        return emplace(pos, std::move(value));
    }

    inline iterator erase(const_iterator pos) noexcept
    {
        invalidate_count_cache();
        const size_t index = static_cast<size_t>(pos.ptr_ - data_ptr_);
        if (index >= usage_) [[unlikely]]
        {
            return end();
        }

        const bool dense = is_dense();

        if (index < usage_ - 1) [[likely]]
        {
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                std::memmove(std::assume_aligned<alignof(T)>(data_ptr_ + index),
                             std::assume_aligned<alignof(T)>(data_ptr_ + index + 1),
                             (usage_ - index - 1) * sizeof(T));
            }
            else if (dense) [[likely]]
            {
                std::move(data_ptr_ + index + 1, data_ptr_ + usage_, data_ptr_ + index);
                data_ptr_[usage_ - 1].~T();
            }
            else
            {
                if (bitmap_test(sparse_bits_, index))
                {
                    data_ptr_[index].~T();
                }
                for (size_t i = index; i < usage_ - 1; ++i)
                {
                    if (bitmap_test(sparse_bits_, i + 1))
                    {
                        new (&data_ptr_[i]) T(std::move(data_ptr_[i + 1]));
                        data_ptr_[i + 1].~T();
                    }
                }
            }

            if (dense) [[likely]]
            {
                bitmap_reset(sparse_bits_, usage_ - 1);
            }
            else
            {
                bitmap_shift_left(sparse_bits_, index, usage_, 1);
            }
        }
        else
        {
            if (bitmap_test(sparse_bits_, index))
            {
                data_ptr_[index].~T();
            }
            bitmap_reset(sparse_bits_, index);
        }

        --usage_;
        return iterator(data_ptr_ + index, data_ptr_ + usage_,
                        dense ? nullptr : sparse_bits_, data_ptr_);
    }

    inline iterator erase(const_iterator first, const_iterator last) noexcept
    {
        invalidate_count_cache();
        const size_t start_index = static_cast<size_t>(first.ptr_ - data_ptr_);
        const size_t end_index = static_cast<size_t>(last.ptr_ - data_ptr_);

        if (start_index >= usage_) [[unlikely]]
        {
            return end();
        }

        const size_t real_end = end_index > usage_ ? usage_ : end_index;
        if (start_index >= real_end) [[unlikely]]
        {
            return iterator(data_ptr_ + start_index, data_ptr_ + usage_,
                            is_dense_ ? nullptr : sparse_bits_, data_ptr_);
        }

        const size_t gap = real_end - start_index;
        const bool dense = is_dense();

        if (real_end < usage_) [[likely]]
        {
            if constexpr (std::is_trivially_copyable_v<T>)
            {
                std::memmove(std::assume_aligned<alignof(T)>(data_ptr_ + start_index),
                             std::assume_aligned<alignof(T)>(data_ptr_ + real_end),
                             (usage_ - real_end) * sizeof(T));
            }
            else if (dense) [[likely]]
            {
                std::move(data_ptr_ + real_end, data_ptr_ + usage_, data_ptr_ + start_index);
                for (size_t i = usage_ - gap; i < usage_; ++i)
                {
                    data_ptr_[i].~T();
                }
            }
            else
            {
                for (size_t i = start_index; i < real_end; ++i)
                {
                    if (bitmap_test(sparse_bits_, i))
                    {
                        data_ptr_[i].~T();
                    }
                }
                for (size_t i = 0; i < usage_ - real_end; ++i)
                {
                    size_t dst = start_index + i;
                    size_t src = real_end + i;
                    if (bitmap_test(sparse_bits_, src))
                    {
                        new (&data_ptr_[dst]) T(std::move(data_ptr_[src]));
                        data_ptr_[src].~T();
                    }
                }
            }

            if (dense) [[likely]]
            {
                for (size_t i = usage_ - gap; i < usage_; ++i)
                {
                    bitmap_reset(sparse_bits_, i);
                }
            }
            else
            {
                bitmap_shift_left(sparse_bits_, start_index, usage_, gap);
            }

            usage_ -= gap;
        }
        else
        {
            for (size_t i = start_index; i < real_end; ++i)
            {
                if (bitmap_test(sparse_bits_, i))
                {
                    data_ptr_[i].~T();
                }
                bitmap_reset(sparse_bits_, i);
            }
            usage_ -= gap;
        }

        return iterator(data_ptr_ + start_index, data_ptr_ + usage_,
                        dense ? nullptr : sparse_bits_, data_ptr_);
    }

    inline void swap(class_pool& other) noexcept
    {
        std::swap(data_ptr_, other.data_ptr_);
        std::swap(sparse_bits_, other.sparse_bits_);
        std::swap(maximum_quantity_, other.maximum_quantity_);
        std::swap(usage_, other.usage_);
        std::swap(is_dense_, other.is_dense_);
        std::swap(count_cache_, other.count_cache_);
    }

    inline void pop_back() noexcept
    {
        invalidate_count_cache();
        if (usage_ > 0) [[likely]]
        {
            const size_t idx = usage_ - 1;
            if (bitmap_test(sparse_bits_, idx))
            {
                data_ptr_[idx].~T();
                bitmap_reset(sparse_bits_, idx);
            }
            --usage_;
        }
    }

    [[nodiscard]] constexpr bool valid() const noexcept { return data_ptr_ != nullptr; }
    [[nodiscard]] constexpr size_type size_bytes() const noexcept { return usage_ * sizeof(T); }
    [[nodiscard]] constexpr size_type capacity_bytes() const noexcept { return maximum_quantity_ * sizeof(T); }

    [[nodiscard]] constexpr std::span<T> span() noexcept { return std::span<T>(data_ptr_, usage_); }
    [[nodiscard]] constexpr std::span<const T> span() const noexcept { return std::span<const T>(data_ptr_, usage_); }

    [[nodiscard]] constexpr T& operator[](size_t index) noexcept
    {
        return data_ptr_[index];
    }

    [[nodiscard]] constexpr const T& operator[](size_t index) const noexcept
    {
        return data_ptr_[index];
    }

    iterator begin() noexcept
    {
        return iterator(data_ptr_, data_ptr_ + usage_,
                        is_dense_ ? nullptr : sparse_bits_, data_ptr_);
    }

    iterator end() noexcept
    {
        return iterator(data_ptr_ + usage_, data_ptr_ + usage_,
                        nullptr, data_ptr_);
    }

    const_iterator begin() const noexcept
    {
        return const_iterator(data_ptr_, data_ptr_ + usage_,
                              is_dense_ ? nullptr : sparse_bits_, data_ptr_);
    }

    const_iterator end() const noexcept
    {
        return const_iterator(data_ptr_ + usage_, data_ptr_ + usage_,
                              nullptr, data_ptr_);
    }

    const_iterator cbegin() const noexcept
    {
        return const_iterator(data_ptr_, data_ptr_ + usage_,
                              is_dense_ ? nullptr : sparse_bits_, data_ptr_);
    }

    const_iterator cend() const noexcept
    {
        return const_iterator(data_ptr_ + usage_, data_ptr_ + usage_,
                              nullptr, data_ptr_);
    }

    template <typename... Args>
    inline T& emplace_at(size_t index, Args&&... args) noexcept
    {
        invalidate_count_cache();
        static_assert(std::is_constructible_v<T, Args...>,
            "T must be constructible from the provided arguments");

        if (index >= maximum_quantity_) [[unlikely]]
        {
            grow_data_and_bitmap(calculate_growth_for_reserve(index + 1));
        }

        const bool extended = (index >= usage_);
        if (extended) [[unlikely]]
        {
            usage_ = index + 1;
        }

        if (bitmap_test(sparse_bits_, index)) [[likely]]
        {
            return data_ptr_[index];
        }

        new (&data_ptr_[index]) T(std::forward<Args>(args)...);
        bitmap_set(sparse_bits_, index);
        if (extended && is_dense_) [[unlikely]]
        {
            recompute_is_dense();
        }
        return data_ptr_[index];
    }

    template <typename... Args>
    inline T& sparse_emplace_at(size_t index, Args&&... args) noexcept
    {
        invalidate_count_cache();
        static_assert(std::is_constructible_v<T, Args...>,
            "T must be constructible from the provided arguments");

        if (index >= maximum_quantity_) [[unlikely]]
        {
            grow_data_and_bitmap(calculate_growth_for_reserve(index + 1));
        }

        const bool extended = (index >= usage_);
        if (extended)
        {
            usage_ = index + 1;
        }

        if (bitmap_test(sparse_bits_, index))
        {
            data_ptr_[index].~T();
        }

        new (&data_ptr_[index]) T(std::forward<Args>(args)...);
        bitmap_set(sparse_bits_, index);
        if (extended && is_dense_) [[unlikely]]
        {
            recompute_is_dense();
        }
        return data_ptr_[index];
    }

    inline void sparse_erase_at(size_t index) noexcept
    {
        invalidate_count_cache();
        if (index < maximum_quantity_ && bitmap_test(sparse_bits_, index))
        {
            data_ptr_[index].~T();
            bitmap_reset(sparse_bits_, index);
            is_dense_ = false;
        }
    }

    [[nodiscard]] constexpr bool is_constructed_at(size_t index) const noexcept
    {
        if (index >= maximum_quantity_) return false;
        return bitmap_test(sparse_bits_, index);
    }

    [[nodiscard]] bool is_dense() const noexcept
    {
        return is_dense_;
    }

    void recompute_is_dense() noexcept
    {
        if (usage_ == 0) { is_dense_ = true; return; }
        const size_t full_words = usage_ / BITS_PER_WORD;
        for (size_t i = 0; i < full_words; ++i)
        {
            if (sparse_bits_[i] != ~0ull) { is_dense_ = false; return; }
        }
        const size_t tail = usage_ % BITS_PER_WORD;
        if (tail != 0)
        {
            const uint64_t mask = (1ull << tail) - 1;
            if ((sparse_bits_[full_words] & mask) != mask) { is_dense_ = false; return; }
        }
        is_dense_ = true;
    }
};

template <typename T>
inline void swap(class_pool<T>& a, class_pool<T>& b) noexcept
{
    a.swap(b);
}
