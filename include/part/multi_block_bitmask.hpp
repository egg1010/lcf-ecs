#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <bit>
#include <new>
#include "dense.hpp"

class entity_mask_manager
{
private:
	struct mask_entry
	{
		uint64_t inline_bits;
		uint64_t* overflow;
	};

	dense<mask_entry> entries_;
	uint32_t overflow_block_count_{0};
	size_t   overflow_entity_count_{0};

	[[nodiscard]] static uint64_t* alloc_overflow(uint32_t block_count) noexcept
	{
		if (block_count == 0) [[unlikely]] return nullptr;
		const size_t bytes = static_cast<size_t>(block_count) * sizeof(uint64_t);
		uint64_t* p = static_cast<uint64_t*>(
			::operator new(bytes, std::align_val_t{32}, std::nothrow));
		if (p == nullptr) [[unlikely]] std::abort();
		std::memset(p, 0, bytes);
		return p;
	}

	static void free_overflow(uint64_t* p) noexcept
	{
		if (p != nullptr)
			::operator delete(p, std::align_val_t{32});
	}

	void free_all_overflow_() noexcept
	{
		for (size_t i = 0; i < entries_.size(); ++i)
		{
			if (entries_[i].overflow != nullptr)
			{
				free_overflow(entries_[i].overflow);
				entries_[i].overflow = nullptr;
			}
		}
		overflow_entity_count_ = 0;
	}

	void free_overflow_range_(size_t first, size_t last) noexcept
	{
		for (size_t i = first; i < last && i < entries_.size(); ++i)
		{
			if (entries_[i].overflow != nullptr)
			{
				free_overflow(entries_[i].overflow);
				entries_[i].overflow = nullptr;
				--overflow_entity_count_;
			}
		}
	}

public:
	entity_mask_manager() noexcept = default;

	~entity_mask_manager() noexcept
	{
		free_all_overflow_();
	}

	entity_mask_manager(entity_mask_manager&& o) noexcept
		: entries_(std::move(o.entries_))
		, overflow_block_count_(o.overflow_block_count_)
		, overflow_entity_count_(o.overflow_entity_count_)
	{
		o.overflow_block_count_ = 0;
		o.overflow_entity_count_ = 0;
	}

	entity_mask_manager& operator=(entity_mask_manager&& o) noexcept
	{
		if (this != &o)
		{
			free_all_overflow_();
			entries_ = std::move(o.entries_);
			overflow_block_count_ = o.overflow_block_count_;
			overflow_entity_count_ = o.overflow_entity_count_;
			o.overflow_block_count_ = 0;
			o.overflow_entity_count_ = 0;
		}
		return *this;
	}

	entity_mask_manager(const entity_mask_manager&) = delete;
	entity_mask_manager& operator=(const entity_mask_manager&) = delete;

	void reserve_blocks(uint32_t num_blocks) noexcept
	{
		uint32_t needed = (num_blocks > 0) ? num_blocks - 1 : 0;
		if (needed <= overflow_block_count_) [[unlikely]] return;
		for (size_t i = 0; i < entries_.size(); ++i)
		{
			if (entries_[i].overflow != nullptr)
			{
				uint64_t* new_ovf = alloc_overflow(needed);
				std::memcpy(new_ovf, entries_[i].overflow,
				            static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t));
				free_overflow(entries_[i].overflow);
				entries_[i].overflow = new_ovf;
			}
		}
		overflow_block_count_ = needed;
	}

	[[nodiscard]] uint32_t num_blocks() const noexcept
	{
		return 1 + overflow_block_count_;
	}

	void ensure_entity(uint32_t entity_index) noexcept
	{
		size_t required = static_cast<size_t>(entity_index) + 1;
		if (required > entries_.size()) [[unlikely]]
			entries_.resize(required, mask_entry{0, nullptr});
	}

	void set_bit(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (entity_index >= entries_.size()) [[unlikely]]
			ensure_entity(entity_index);
		mask_entry& entry = entries_[entity_index];
		if (block_idx == 0) [[likely]]
		{
			entry.inline_bits |= (1ULL << bit_offset);
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		if (entry.overflow == nullptr) [[unlikely]]
		{
			entry.overflow = alloc_overflow(overflow_block_count_);
			++overflow_entity_count_;
		}
		entry.overflow[block_idx - 1] |= (1ULL << bit_offset);
	}

	void clear_bit(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (entity_index >= entries_.size()) [[unlikely]] return;
		mask_entry& entry = entries_[entity_index];
		if (block_idx == 0) [[likely]]
		{
			entry.inline_bits &= ~(1ULL << bit_offset);
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		if (entry.overflow == nullptr) return;
		entry.overflow[block_idx - 1] &= ~(1ULL << bit_offset);
	}

	[[nodiscard]] uint64_t get_block(uint32_t entity_index, uint32_t block_idx) const noexcept
	{
		if (entity_index >= entries_.size()) [[unlikely]] return 0;
		const mask_entry& entry = entries_[entity_index];
		if (block_idx == 0) [[likely]]
			return entry.inline_bits;
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return 0;
		if (entry.overflow == nullptr) return 0;
		return entry.overflow[block_idx - 1];
	}

	void set_bit_no_check(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (block_idx == 0) [[likely]]
		{
			entries_[entity_index].inline_bits |= (1ULL << bit_offset);
			return;
		}
		mask_entry& entry = entries_[entity_index];
		if (entry.overflow == nullptr) [[unlikely]]
		{
			if (overflow_block_count_ == 0) [[unlikely]] return;
			entry.overflow = alloc_overflow(overflow_block_count_);
			++overflow_entity_count_;
		}
		entry.overflow[block_idx - 1] |= (1ULL << bit_offset);
	}

	void clear_bit_no_check(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (block_idx == 0) [[likely]]
		{
			entries_[entity_index].inline_bits &= ~(1ULL << bit_offset);
			return;
		}
		mask_entry& entry = entries_[entity_index];
		if (entry.overflow != nullptr)
			entry.overflow[block_idx - 1] &= ~(1ULL << bit_offset);
	}

	void clear_entity(uint32_t entity_index) noexcept
	{
		if (entity_index >= entries_.size()) [[unlikely]] return;
		mask_entry& entry = entries_[entity_index];
		entry.inline_bits = 0;
		if (entry.overflow != nullptr)
			std::memset(entry.overflow, 0,
			            static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t));
	}

	void resize_entities(uint32_t new_count) noexcept
	{
		if (new_count > entries_.size()) [[unlikely]]
			entries_.resize(new_count, mask_entry{0, nullptr});
	}

	void increase_capacity(size_t new_entity_capacity) noexcept
	{
		entries_.increase_capacity(new_entity_capacity);
	}

	void reserve_exact(size_t new_entity_capacity) noexcept
	{
		entries_.reserve_exact(new_entity_capacity);
	}

	void shrink_to_fit() noexcept
	{
		entries_.shrink_to_fit();
	}

	void reduce_capacity(size_t new_entity_capacity) noexcept
	{
		if (new_entity_capacity < entries_.size())
			free_overflow_range_(new_entity_capacity, entries_.size());
		entries_.reduce_capacity(new_entity_capacity);
	}

	void clear() noexcept
	{
		free_all_overflow_();
		entries_.clear();
	}

	[[nodiscard]] size_t size() const noexcept
	{
		return entries_.size();
	}

	[[nodiscard]] size_t capacity() const noexcept
	{
		return entries_.capacity();
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return entries_.empty();
	}

	[[nodiscard]] size_t size_bytes() const noexcept
	{
		return entries_.size() * sizeof(mask_entry)
		     + overflow_entity_count_ * static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t);
	}

	[[nodiscard]] size_t capacity_bytes() const noexcept
	{
		return entries_.capacity() * sizeof(mask_entry);
	}

	template <typename Func>
	void for_each_set_bit(uint32_t entity_index, Func&& func) const noexcept
	{
		if (entity_index >= entries_.size()) [[unlikely]] return;
		const mask_entry& entry = entries_[entity_index];

		uint64_t block = entry.inline_bits;
		while (block)
		{
			uint32_t offset = static_cast<uint32_t>(std::countr_zero(block));
			func(0, offset);
			block &= block - 1;
		}

		if (entry.overflow != nullptr)
		{
			for (uint32_t b = 0; b < overflow_block_count_; ++b)
			{
				uint64_t ob = entry.overflow[b];
				if (ob == 0) continue;
				do
				{
					uint32_t offset = static_cast<uint32_t>(std::countr_zero(ob));
					func(b + 1, offset);
					ob &= ob - 1;
				} while (ob);
			}
		}
	}
};
