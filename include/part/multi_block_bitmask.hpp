#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <bit>
#include <new>
#include <span>
#include <utility>
#include <concepts>
#include "dense.hpp"

// 多块位掩码存储: 每槽 1+ 个 64 位块
// 块 0 内嵌于 inline_bits_, 块 1+ 通过 overflows_ 间接寻址按需分配
class multi_block_bitmask
{
private:
	dense<uint64_t> inline_bits_;
	dense<uint64_t*> overflows_;
	uint32_t overflow_block_count_{0};
	size_t   overflow_entity_count_{0};

	static constexpr size_t overflow_align = 32;

	[[nodiscard]] static uint64_t* alloc_overflow(uint32_t block_count) noexcept
	{
		if (block_count == 0) [[unlikely]] return nullptr;
		const size_t bytes = static_cast<size_t>(block_count) * sizeof(uint64_t);
		uint64_t* p = static_cast<uint64_t*>(
			::operator new(bytes, std::align_val_t{overflow_align}, std::nothrow));
		if (p == nullptr) [[unlikely]] std::abort();
		std::memset(p, 0, bytes);
		return p;
	}

	static void free_overflow(uint64_t* p) noexcept
	{
		if (p != nullptr)
			::operator delete(p, std::align_val_t{overflow_align});
	}

	void free_all_overflow_() noexcept
	{
		for (size_t i = 0; i < overflows_.size(); ++i)
		{
			if (overflows_[i] != nullptr)
			{
				free_overflow(overflows_[i]);
				overflows_[i] = nullptr;
			}
		}
		overflow_entity_count_ = 0;
	}

	void free_overflow_range_(size_t first, size_t last) noexcept
	{
		for (size_t i = first; i < last && i < overflows_.size(); ++i)
		{
			if (overflows_[i] != nullptr)
			{
				free_overflow(overflows_[i]);
				overflows_[i] = nullptr;
				--overflow_entity_count_;
			}
		}
	}

	void resize_both_(size_t new_size) noexcept
	{
		if (new_size > inline_bits_.size()) [[unlikely]]
			inline_bits_.increase_capacity(new_size, 0);
		if (new_size > overflows_.size()) [[unlikely]]
			overflows_.increase_capacity(new_size, nullptr);
	}

	// 深拷贝辅助
	void copy_from_(const multi_block_bitmask& o) noexcept
	{
		free_all_overflow_();
		inline_bits_ = o.inline_bits_;
		overflow_block_count_ = o.overflow_block_count_;
		if (o.overflows_.size() > 0)
		{
			overflows_.clear();
			overflows_.increase_capacity(o.overflows_.size(), nullptr);
			for (size_t i = 0; i < o.overflows_.size(); ++i)
			{
				if (o.overflows_[i] != nullptr)
				{
					overflows_[i] = alloc_overflow(overflow_block_count_);
					std::memcpy(overflows_[i], o.overflows_[i],
					            static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t));
					++overflow_entity_count_;
				}
				else
				{
					overflows_[i] = nullptr;
				}
			}
		}
		else
		{
			overflows_.clear();
		}
	}

public:
	static constexpr uint32_t bits_per_block = 64;

	// 给定位数算所需块数
	[[nodiscard]] static constexpr uint32_t block_count_for_bits(size_t bit_count) noexcept
	{
		return static_cast<uint32_t>((bit_count + bits_per_block - 1) / bits_per_block);
	}

	multi_block_bitmask() noexcept = default;

	~multi_block_bitmask() noexcept
	{
		free_all_overflow_();
	}

	multi_block_bitmask(const multi_block_bitmask& o) noexcept
	{
		copy_from_(o);
	}

	multi_block_bitmask& operator=(const multi_block_bitmask& o) noexcept
	{
		if (this != &o) [[likely]]
			copy_from_(o);
		return *this;
	}

	multi_block_bitmask(multi_block_bitmask&& o) noexcept
		: inline_bits_(std::move(o.inline_bits_))
		, overflows_(std::move(o.overflows_))
		, overflow_block_count_(o.overflow_block_count_)
		, overflow_entity_count_(o.overflow_entity_count_)
	{
		o.overflow_block_count_ = 0;
		o.overflow_entity_count_ = 0;
	}

	multi_block_bitmask& operator=(multi_block_bitmask&& o) noexcept
	{
		if (this != &o) [[likely]]
		{
			free_all_overflow_();
			inline_bits_ = std::move(o.inline_bits_);
			overflows_ = std::move(o.overflows_);
			overflow_block_count_ = o.overflow_block_count_;
			overflow_entity_count_ = o.overflow_entity_count_;
			o.overflow_block_count_ = 0;
			o.overflow_entity_count_ = 0;
		}
		return *this;
	}

	void swap(multi_block_bitmask& o) noexcept
	{
		inline_bits_.swap(o.inline_bits_);
		overflows_.swap(o.overflows_);
		std::swap(overflow_block_count_, o.overflow_block_count_);
		std::swap(overflow_entity_count_, o.overflow_entity_count_);
	}

	[[nodiscard]] multi_block_bitmask clone() const noexcept
	{
		return multi_block_bitmask(*this);
	}

	// === 块管理 ===

	void reserve_blocks(uint32_t num_blocks) noexcept
	{
		uint32_t needed = (num_blocks > 0) ? num_blocks - 1 : 0;
		if (needed <= overflow_block_count_) [[unlikely]] return;
		for (size_t i = 0; i < overflows_.size(); ++i)
		{
			if (overflows_[i] != nullptr)
			{
				uint64_t* new_ovf = alloc_overflow(needed);
				std::memcpy(new_ovf, overflows_[i],
				            static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t));
				free_overflow(overflows_[i]);
				overflows_[i] = new_ovf;
			}
		}
		overflow_block_count_ = needed;
	}

	[[nodiscard]] uint32_t num_blocks() const noexcept
	{
		return 1 + overflow_block_count_;
	}

	[[nodiscard]] size_t overflow_entity_count() const noexcept
	{
		return overflow_entity_count_;
	}

	// === 容量管理 ===

	void ensure_entity(uint32_t slot) noexcept
	{
		size_t required = static_cast<size_t>(slot) + 1;
		if (required > inline_bits_.size()) [[unlikely]]
			resize_both_(required);
	}

	void resize_entities(uint32_t new_count) noexcept
	{
		if (new_count > inline_bits_.size()) [[unlikely]]
			resize_both_(new_count);
	}

	void increase_capacity(size_t new_slot_capacity) noexcept
	{
		inline_bits_.increase_capacity(new_slot_capacity);
		overflows_.increase_capacity(new_slot_capacity);
	}

	void reserve_exact(size_t new_slot_capacity) noexcept
	{
		inline_bits_.reserve_exact(new_slot_capacity);
		overflows_.reserve_exact(new_slot_capacity);
	}

	void shrink_to_fit() noexcept
	{
		inline_bits_.shrink_to_fit();
		overflows_.shrink_to_fit();
	}

	void reduce_capacity(size_t new_slot_capacity) noexcept
	{
		if (new_slot_capacity < overflows_.size())
			free_overflow_range_(new_slot_capacity, overflows_.size());
		inline_bits_.reduce_capacity(new_slot_capacity);
		overflows_.reduce_capacity(new_slot_capacity);
	}

	void clear() noexcept
	{
		free_all_overflow_();
		inline_bits_.clear();
		overflows_.clear();
	}

	[[nodiscard]] size_t size() const noexcept
	{
		return inline_bits_.size();
	}

	[[nodiscard]] size_t capacity() const noexcept
	{
		return inline_bits_.capacity();
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return inline_bits_.empty();
	}

	[[nodiscard]] size_t size_bytes() const noexcept
	{
		return inline_bits_.size() * sizeof(uint64_t)
		     + overflows_.size() * sizeof(uint64_t*)
		     + overflow_entity_count_ * static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t);
	}

	[[nodiscard]] size_t capacity_bytes() const noexcept
	{
		return inline_bits_.capacity() * sizeof(uint64_t)
		     + overflows_.capacity() * sizeof(uint64_t*);
	}

	// === 单位写入 (带边界检查) ===

	void set_bit(uint32_t slot, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]]
			ensure_entity(slot);
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] |= (1ULL << bit_offset);
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		uint64_t*& ovf = overflows_[slot];
		if (ovf == nullptr) [[unlikely]]
		{
			ovf = alloc_overflow(overflow_block_count_);
			++overflow_entity_count_;
		}
		ovf[block_idx - 1] |= (1ULL << bit_offset);
	}

	void clear_bit(uint32_t slot, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return;
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] &= ~(1ULL << bit_offset);
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		uint64_t* ovf = overflows_[slot];
		if (ovf == nullptr) return;
		ovf[block_idx - 1] &= ~(1ULL << bit_offset);
	}

	// === 单位写入 (无边界检查, 调用方保证 slot 已分配且 block_idx < num_blocks()) ===

	void set_bit_no_check(uint32_t slot, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] |= (1ULL << bit_offset);
			return;
		}
		uint64_t*& ovf = overflows_[slot];
		if (ovf == nullptr) [[unlikely]]
		{
			if (overflow_block_count_ == 0) [[unlikely]] return;
			ovf = alloc_overflow(overflow_block_count_);
			++overflow_entity_count_;
		}
		ovf[block_idx - 1] |= (1ULL << bit_offset);
	}

	void clear_bit_no_check(uint32_t slot, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] &= ~(1ULL << bit_offset);
			return;
		}
		uint64_t* ovf = overflows_[slot];
		if (ovf != nullptr)
			ovf[block_idx - 1] &= ~(1ULL << bit_offset);
	}

	// === 整槽清零 ===

	void clear_entity(uint32_t slot) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return;
		inline_bits_[slot] = 0;
		uint64_t* ovf = overflows_[slot];
		if (ovf != nullptr)
			std::memset(ovf, 0,
			            static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t));
	}

	// === 整块写入 (替代读-改-写) ===

	void set_block_value(uint32_t slot, uint32_t block_idx, uint64_t value) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]]
		{
			if (block_idx == 0) ensure_entity(slot);
			else return;
		}
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] = value;
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		uint64_t*& ovf = overflows_[slot];
		if (ovf == nullptr) [[unlikely]]
		{
			ovf = alloc_overflow(overflow_block_count_);
			++overflow_entity_count_;
		}
		ovf[block_idx - 1] = value;
	}

	void or_block_value(uint32_t slot, uint32_t block_idx, uint64_t mask) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return;
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] |= mask;
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		uint64_t* ovf = overflows_[slot];
		if (ovf != nullptr)
			ovf[block_idx - 1] |= mask;
	}

	void and_block_value(uint32_t slot, uint32_t block_idx, uint64_t mask) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return;
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] &= mask;
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		uint64_t* ovf = overflows_[slot];
		if (ovf != nullptr)
			ovf[block_idx - 1] &= mask;
	}

	void xor_block_value(uint32_t slot, uint32_t block_idx, uint64_t mask) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return;
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] ^= mask;
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		uint64_t* ovf = overflows_[slot];
		if (ovf != nullptr)
			ovf[block_idx - 1] ^= mask;
	}

	// === 批量位操作 (同块多位) ===

	void set_bits_at(uint32_t slot, uint32_t block_idx, std::span<const uint32_t> offsets) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]]
		{
			if (block_idx == 0) ensure_entity(slot);
			else return;
		}
		uint64_t mask = 0;
		for (uint32_t off : offsets) mask |= (1ULL << off);
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] |= mask;
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		uint64_t*& ovf = overflows_[slot];
		if (ovf == nullptr) [[unlikely]]
		{
			ovf = alloc_overflow(overflow_block_count_);
			++overflow_entity_count_;
		}
		ovf[block_idx - 1] |= mask;
	}

	void clear_bits_at(uint32_t slot, uint32_t block_idx, std::span<const uint32_t> offsets) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return;
		uint64_t mask = 0;
		for (uint32_t off : offsets) mask |= (1ULL << off);
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] &= ~mask;
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		uint64_t* ovf = overflows_[slot];
		if (ovf != nullptr)
			ovf[block_idx - 1] &= ~mask;
	}

	void toggle_bits_at(uint32_t slot, uint32_t block_idx, std::span<const uint32_t> offsets) noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return;
		uint64_t mask = 0;
		for (uint32_t off : offsets) mask |= (1ULL << off);
		if (block_idx == 0) [[likely]]
		{
			inline_bits_[slot] ^= mask;
			return;
		}
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return;
		uint64_t* ovf = overflows_[slot];
		if (ovf != nullptr)
			ovf[block_idx - 1] ^= mask;
	}

	// === 整槽多块写入/读取 ===

	// 从 data 写入 slot 的所有块 (data[0] -> block 0, data[1] -> block 1, ...)
	// 若 n > num_blocks() 会自动 reserve_blocks(n)
	void assign_slot(uint32_t slot, std::span<const uint64_t> data) noexcept
	{
		if (data.size() == 0) return;
		if (slot >= inline_bits_.size()) [[unlikely]]
			ensure_entity(slot);
		if (data.size() > num_blocks()) [[unlikely]]
			reserve_blocks(static_cast<uint32_t>(data.size()));
		inline_bits_[slot] = data[0];
		if (data.size() > 1)
		{
			uint64_t*& ovf = overflows_[slot];
			if (ovf == nullptr) [[unlikely]]
			{
				ovf = alloc_overflow(overflow_block_count_);
				++overflow_entity_count_;
			}
			std::memcpy(ovf, data.data() + 1,
			            static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t));
		}
	}

	// 读取 slot 的所有块到 dst (调用方保证 dst 容量 >= num_blocks())
	void copy_slot_to(uint32_t slot, std::span<uint64_t> dst) const noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]]
		{
			std::memset(dst.data(), 0, dst.size_bytes());
			return;
		}
		if (dst.size() == 0) return;
		dst[0] = inline_bits_[slot];
		if (dst.size() > 1)
		{
			const uint64_t* ovf = (slot < overflows_.size()) ? overflows_[slot] : nullptr;
			if (ovf != nullptr)
			{
				uint32_t copy_blocks = (dst.size() - 1 < overflow_block_count_)
				                     ? static_cast<uint32_t>(dst.size() - 1)
				                     : overflow_block_count_;
				std::memcpy(dst.data() + 1, ovf,
				            static_cast<size_t>(copy_blocks) * sizeof(uint64_t));
				if (copy_blocks < dst.size() - 1)
					std::memset(dst.data() + 1 + copy_blocks, 0,
					            (dst.size() - 1 - copy_blocks) * sizeof(uint64_t));
			}
			else
			{
				std::memset(dst.data() + 1, 0, (dst.size() - 1) * sizeof(uint64_t));
			}
		}
	}

	// === 查询接口 ===

	[[nodiscard]] uint64_t get_block(uint32_t slot, uint32_t block_idx) const noexcept
	{
		if (block_idx == 0) [[likely]]
		{
			if (slot >= inline_bits_.size()) [[unlikely]] return 0;
			return inline_bits_[slot];
		}
		if (slot >= overflows_.size()) [[unlikely]] return 0;
		if (block_idx - 1 >= overflow_block_count_) [[unlikely]] return 0;
		const uint64_t* ovf = overflows_[slot];
		if (ovf == nullptr) return 0;
		return ovf[block_idx - 1];
	}

	[[nodiscard]] bool test_bit(uint32_t slot, uint32_t block_idx, uint32_t bit_offset) const noexcept
	{
		return (get_block(slot, block_idx) & (1ULL << bit_offset)) != 0;
	}

	[[nodiscard]] bool any_set_in_block(uint32_t slot, uint32_t block_idx) const noexcept
	{
		return get_block(slot, block_idx) != 0;
	}

	[[nodiscard]] bool any_set(uint32_t slot) const noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return false;
		if (inline_bits_[slot] != 0) [[likely]] return true;
		if (slot < overflows_.size())
		{
			const uint64_t* ovf = overflows_[slot];
			if (ovf != nullptr)
			{
				for (uint32_t b = 0; b < overflow_block_count_; ++b)
					if (ovf[b] != 0) return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool is_zero(uint32_t slot) const noexcept
	{
		return !any_set(slot);
	}

	[[nodiscard]] uint32_t count_set_bits(uint32_t slot) const noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return 0;
		uint32_t cnt = static_cast<uint32_t>(std::popcount(inline_bits_[slot]));
		if (slot < overflows_.size())
		{
			const uint64_t* ovf = overflows_[slot];
			if (ovf != nullptr)
			{
				for (uint32_t b = 0; b < overflow_block_count_; ++b)
					cnt += static_cast<uint32_t>(std::popcount(ovf[b]));
			}
		}
		return cnt;
	}

	// 返回是否找到, 结果写入 out_block / out_offset
	[[nodiscard]] bool find_first_set(uint32_t slot, uint32_t& out_block, uint32_t& out_offset) const noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return false;
		uint64_t b0 = inline_bits_[slot];
		if (b0 != 0)
		{
			out_block = 0;
			out_offset = static_cast<uint32_t>(std::countr_zero(b0));
			return true;
		}
		if (slot < overflows_.size())
		{
			const uint64_t* ovf = overflows_[slot];
			if (ovf != nullptr)
			{
				for (uint32_t b = 0; b < overflow_block_count_; ++b)
				{
					if (ovf[b] != 0)
					{
						out_block = b + 1;
						out_offset = static_cast<uint32_t>(std::countr_zero(ovf[b]));
						return true;
					}
				}
			}
		}
		return false;
	}

	[[nodiscard]] bool find_last_set(uint32_t slot, uint32_t& out_block, uint32_t& out_offset) const noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return false;
		// 从最高块向下找
		if (slot < overflows_.size())
		{
			const uint64_t* ovf = overflows_[slot];
			if (ovf != nullptr)
			{
				for (uint32_t b = overflow_block_count_; b > 0; --b)
				{
					uint64_t v = ovf[b - 1];
					if (v != 0)
					{
						out_block = b;
						out_offset = static_cast<uint32_t>(63 - std::countl_zero(v));
						return true;
					}
				}
			}
		}
		uint64_t b0 = inline_bits_[slot];
		if (b0 != 0)
		{
			out_block = 0;
			out_offset = static_cast<uint32_t>(63 - std::countl_zero(b0));
			return true;
		}
		return false;
	}

	// 从 (after_block, after_offset) 之后找下一个置位
	// 起始位置 after_block=0, after_offset=UINT32_MAX 表示从头开始
	[[nodiscard]] bool find_next_set(uint32_t slot, uint32_t after_block, uint32_t after_offset,
	                                 uint32_t& out_block, uint32_t& out_offset) const noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return false;

		// 处理 block 0
		if (after_block == 0)
		{
			uint64_t b0 = inline_bits_[slot];
			uint64_t mask = (after_offset >= 63) ? ~0ULL : ((~0ULL) << (after_offset + 1));
			uint64_t masked = b0 & mask;
			if (masked != 0)
			{
				out_block = 0;
				out_offset = static_cast<uint32_t>(std::countr_zero(masked));
				return true;
			}
		}

		// 处理 overflow 块
		if (slot < overflows_.size())
		{
			const uint64_t* ovf = overflows_[slot];
			if (ovf != nullptr)
			{
				uint32_t start_b = (after_block == 0) ? 0 : after_block - 1;
				for (uint32_t b = start_b; b < overflow_block_count_; ++b)
				{
					uint64_t v = ovf[b];
					uint64_t mask;
					if (after_block == 0 || b > after_block - 1)
					{
						mask = ~0ULL;
					}
					else
					{
						// b == after_block - 1
						mask = (after_offset >= 63) ? 0ULL : ((~0ULL) << (after_offset + 1));
					}
					uint64_t masked = v & mask;
					if (masked != 0)
					{
						out_block = b + 1;
						out_offset = static_cast<uint32_t>(std::countr_zero(masked));
						return true;
					}
				}
			}
		}
		return false;
	}

	// === 遍历接口 ===

	template <typename Func>
	requires std::invocable<Func, uint32_t, uint32_t>
	void for_each_set_bit(uint32_t slot, Func&& func) const noexcept
	{
		if (slot >= inline_bits_.size()) [[unlikely]] return;

		uint64_t block = inline_bits_[slot];
		while (block)
		{
			uint32_t offset = static_cast<uint32_t>(std::countr_zero(block));
			func(0, offset);
			block &= block - 1;
		}

		if (slot < overflows_.size())
		{
			const uint64_t* ovf = overflows_[slot];
			if (ovf != nullptr)
			{
				for (uint32_t b = 0; b < overflow_block_count_; ++b)
				{
					uint64_t ob = ovf[b];
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
	}

	// 遍历所有非空 slot, 回调签名 func(uint32_t slot)
	template <typename Func>
	requires std::invocable<Func, uint32_t>
	void for_each_set_slot(Func&& func) const noexcept
	{
		for (size_t i = 0; i < inline_bits_.size(); ++i)
		{
			if (inline_bits_[i] != 0) [[likely]]
			{
				func(static_cast<uint32_t>(i));
				continue;
			}
			// inline 为零, 检查 overflow
			if (i < overflows_.size())
			{
				const uint64_t* ovf = overflows_[i];
				if (ovf != nullptr)
				{
					for (uint32_t b = 0; b < overflow_block_count_; ++b)
					{
						if (ovf[b] != 0)
						{
							func(static_cast<uint32_t>(i));
							break;
						}
					}
				}
			}
		}
	}

	// 全局遍历所有置位, 回调签名 func(uint32_t slot, uint32_t block_idx, uint32_t bit_offset)
	template <typename Func>
	requires std::invocable<Func, uint32_t, uint32_t, uint32_t>
	void for_each_set_bit_global(Func&& func) const noexcept
	{
		for (size_t i = 0; i < inline_bits_.size(); ++i)
		{
			uint32_t slot = static_cast<uint32_t>(i);
			uint64_t b0 = inline_bits_[i];
			while (b0)
			{
				uint32_t offset = static_cast<uint32_t>(std::countr_zero(b0));
				func(slot, 0, offset);
				b0 &= b0 - 1;
			}
			if (i < overflows_.size())
			{
				const uint64_t* ovf = overflows_[i];
				if (ovf != nullptr)
				{
					for (uint32_t b = 0; b < overflow_block_count_; ++b)
					{
						uint64_t ob = ovf[b];
						if (ob == 0) continue;
						do
						{
							uint32_t offset = static_cast<uint32_t>(std::countr_zero(ob));
							func(slot, b + 1, offset);
							ob &= ob - 1;
						} while (ob);
					}
				}
			}
		}
	}

	[[nodiscard]] size_t count_set_bits_global() const noexcept
	{
		size_t total = 0;
		for (size_t i = 0; i < inline_bits_.size(); ++i)
		{
			total += std::popcount(inline_bits_[i]);
			if (i < overflows_.size())
			{
				const uint64_t* ovf = overflows_[i];
				if (ovf != nullptr)
				{
					for (uint32_t b = 0; b < overflow_block_count_; ++b)
						total += std::popcount(ovf[b]);
				}
			}
		}
		return total;
	}

	// === 视图接口 ===

	[[nodiscard]] std::span<uint64_t> inline_span() noexcept
	{
		return std::span<uint64_t>(inline_bits_.data(), inline_bits_.size());
	}

	[[nodiscard]] std::span<const uint64_t> inline_span() const noexcept
	{
		return std::span<const uint64_t>(inline_bits_.data(), inline_bits_.size());
	}

	// 返回某 slot 的 overflow 块视图 (block 1+), 未分配则返回空 span
	[[nodiscard]] std::span<uint64_t> overflow_span(uint32_t slot) noexcept
	{
		if (slot >= overflows_.size()) [[unlikely]] return {};
		uint64_t* ovf = overflows_[slot];
		if (ovf == nullptr) return {};
		return std::span<uint64_t>(ovf, overflow_block_count_);
	}

	[[nodiscard]] std::span<const uint64_t> overflow_span(uint32_t slot) const noexcept
	{
		if (slot >= overflows_.size()) [[unlikely]] return {};
		const uint64_t* ovf = overflows_[slot];
		if (ovf == nullptr) return {};
		return std::span<const uint64_t>(ovf, overflow_block_count_);
	}

	// === 集合运算 (原地, 处理共同 slot 与共同块) ===

	// this &= o (仅处理双方共同 slot 与共同块)
	void and_with(const multi_block_bitmask& o) noexcept
	{
		size_t common = (inline_bits_.size() < o.inline_bits_.size())
		              ? inline_bits_.size() : o.inline_bits_.size();
		for (size_t i = 0; i < common; ++i)
			inline_bits_[i] &= o.inline_bits_[i];
		// 超出 o 的 slot 清零 (集合与运算下, this 中 o 不存在的 slot 应清零)
		for (size_t i = common; i < inline_bits_.size(); ++i)
			inline_bits_[i] = 0;
		// overflow 块
		uint32_t common_ovf = (overflow_block_count_ < o.overflow_block_count_)
		                    ? overflow_block_count_ : o.overflow_block_count_;
		for (size_t i = 0; i < common && i < overflows_.size(); ++i)
		{
			uint64_t* ovf = overflows_[i];
			if (ovf == nullptr) continue;
			if (i >= o.overflows_.size() || o.overflows_[i] == nullptr)
			{
				// o 无 overflow, this 的 overflow 应清零
				std::memset(ovf, 0,
				            static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t));
				continue;
			}
			const uint64_t* o_ovf = o.overflows_[i];
			for (uint32_t b = 0; b < common_ovf; ++b)
				ovf[b] &= o_ovf[b];
			// 超出 o 的 overflow 块清零
			for (uint32_t b = common_ovf; b < overflow_block_count_; ++b)
				ovf[b] = 0;
		}
	}

	// this |= o (仅处理双方共同 slot 与共同块, 不扩容)
	void or_with(const multi_block_bitmask& o) noexcept
	{
		size_t common = (inline_bits_.size() < o.inline_bits_.size())
		              ? inline_bits_.size() : o.inline_bits_.size();
		for (size_t i = 0; i < common; ++i)
			inline_bits_[i] |= o.inline_bits_[i];
		uint32_t common_ovf = (overflow_block_count_ < o.overflow_block_count_)
		                    ? overflow_block_count_ : o.overflow_block_count_;
		if (common_ovf == 0) return;
		for (size_t i = 0; i < common && i < overflows_.size(); ++i)
		{
			if (i >= o.overflows_.size() || o.overflows_[i] == nullptr) continue;
			uint64_t*& ovf = overflows_[i];
			if (ovf == nullptr) [[unlikely]]
			{
				ovf = alloc_overflow(overflow_block_count_);
				++overflow_entity_count_;
			}
			const uint64_t* o_ovf = o.overflows_[i];
			for (uint32_t b = 0; b < common_ovf; ++b)
				ovf[b] |= o_ovf[b];
		}
	}

	// this ^= o
	void xor_with(const multi_block_bitmask& o) noexcept
	{
		size_t common = (inline_bits_.size() < o.inline_bits_.size())
		              ? inline_bits_.size() : o.inline_bits_.size();
		for (size_t i = 0; i < common; ++i)
			inline_bits_[i] ^= o.inline_bits_[i];
		uint32_t common_ovf = (overflow_block_count_ < o.overflow_block_count_)
		                    ? overflow_block_count_ : o.overflow_block_count_;
		if (common_ovf == 0) return;
		for (size_t i = 0; i < common && i < overflows_.size(); ++i)
		{
			if (i >= o.overflows_.size() || o.overflows_[i] == nullptr) continue;
			uint64_t*& ovf = overflows_[i];
			if (ovf == nullptr) [[unlikely]]
			{
				ovf = alloc_overflow(overflow_block_count_);
				++overflow_entity_count_;
			}
			const uint64_t* o_ovf = o.overflows_[i];
			for (uint32_t b = 0; b < common_ovf; ++b)
				ovf[b] ^= o_ovf[b];
		}
	}

	// this &= ~o (差集)
	void subtract(const multi_block_bitmask& o) noexcept
	{
		size_t common = (inline_bits_.size() < o.inline_bits_.size())
		              ? inline_bits_.size() : o.inline_bits_.size();
		for (size_t i = 0; i < common; ++i)
			inline_bits_[i] &= ~o.inline_bits_[i];
		uint32_t common_ovf = (overflow_block_count_ < o.overflow_block_count_)
		                    ? overflow_block_count_ : o.overflow_block_count_;
		for (size_t i = 0; i < common && i < overflows_.size(); ++i)
		{
			uint64_t* ovf = overflows_[i];
			if (ovf == nullptr) continue;
			if (i >= o.overflows_.size() || o.overflows_[i] == nullptr) continue;
			const uint64_t* o_ovf = o.overflows_[i];
			for (uint32_t b = 0; b < common_ovf; ++b)
				ovf[b] &= ~o_ovf[b];
		}
	}

	// 是否与 o 有任意共同置位
	[[nodiscard]] bool overlaps(const multi_block_bitmask& o) const noexcept
	{
		size_t common = (inline_bits_.size() < o.inline_bits_.size())
		              ? inline_bits_.size() : o.inline_bits_.size();
		for (size_t i = 0; i < common; ++i)
			if ((inline_bits_[i] & o.inline_bits_[i]) != 0) return true;
		uint32_t common_ovf = (overflow_block_count_ < o.overflow_block_count_)
		                    ? overflow_block_count_ : o.overflow_block_count_;
		if (common_ovf == 0) return false;
		for (size_t i = 0; i < common && i < overflows_.size(); ++i)
		{
			const uint64_t* ovf = overflows_[i];
			if (ovf == nullptr) continue;
			if (i >= o.overflows_.size() || o.overflows_[i] == nullptr) continue;
			const uint64_t* o_ovf = o.overflows_[i];
			for (uint32_t b = 0; b < common_ovf; ++b)
				if ((ovf[b] & o_ovf[b]) != 0) return true;
		}
		return false;
	}

	// 是否包含 o 的所有置位 (this 是 o 的超集)
	[[nodiscard]] bool contains_all(const multi_block_bitmask& o) const noexcept
	{
		// o 中存在但 this 中不存在的 slot -> false
		if (o.inline_bits_.size() > inline_bits_.size()) return false;
		for (size_t i = 0; i < o.inline_bits_.size(); ++i)
		{
			if ((o.inline_bits_[i] & ~inline_bits_[i]) != 0) return false;
		}
		// overflow 块
		if (o.overflow_block_count_ > overflow_block_count_) return false;
		for (size_t i = 0; i < o.overflows_.size() && i < overflows_.size(); ++i)
		{
			const uint64_t* o_ovf = o.overflows_[i];
			if (o_ovf == nullptr) continue;
			const uint64_t* ovf = overflows_[i];
			if (ovf == nullptr)
			{
				// o 有 overflow 但 this 无 -> 检查 o 是否全零
				for (uint32_t b = 0; b < o.overflow_block_count_; ++b)
					if (o_ovf[b] != 0) return false;
				continue;
			}
			for (uint32_t b = 0; b < o.overflow_block_count_; ++b)
			{
				if ((o_ovf[b] & ~ovf[b]) != 0) return false;
			}
		}
		// 检查 o 中超出 this overflows_.size() 的 slot
		for (size_t i = overflows_.size(); i < o.overflows_.size(); ++i)
		{
			const uint64_t* o_ovf = o.overflows_[i];
			if (o_ovf == nullptr) continue;
			for (uint32_t b = 0; b < o.overflow_block_count_; ++b)
				if (o_ovf[b] != 0) return false;
		}
		return true;
	}

	[[nodiscard]] bool equals(const multi_block_bitmask& o) const noexcept
	{
		if (inline_bits_.size() != o.inline_bits_.size()) return false;
		if (overflow_block_count_ != o.overflow_block_count_) return false;
		for (size_t i = 0; i < inline_bits_.size(); ++i)
			if (inline_bits_[i] != o.inline_bits_[i]) return false;
		for (size_t i = 0; i < overflows_.size(); ++i)
		{
			const uint64_t* ovf = overflows_[i];
			const uint64_t* o_ovf = (i < o.overflows_.size()) ? o.overflows_[i] : nullptr;
			if (ovf == nullptr && o_ovf == nullptr) continue;
			if (ovf == nullptr || o_ovf == nullptr)
			{
				// 一方有, 一方无: 检查非空一方是否全零
				const uint64_t* nonnull = (ovf != nullptr) ? ovf : o_ovf;
				for (uint32_t b = 0; b < overflow_block_count_; ++b)
					if (nonnull[b] != 0) return false;
				continue;
			}
			if (std::memcmp(ovf, o_ovf,
			                static_cast<size_t>(overflow_block_count_) * sizeof(uint64_t)) != 0)
				return false;
		}
		return true;
	}

	// === 内存压缩 ===

	// 若 slot 的 overflow 全零则释放
	void compact_slot(uint32_t slot) noexcept
	{
		if (slot >= overflows_.size()) [[unlikely]] return;
		uint64_t* ovf = overflows_[slot];
		if (ovf == nullptr) return;
		for (uint32_t b = 0; b < overflow_block_count_; ++b)
			if (ovf[b] != 0) return;
		free_overflow(ovf);
		overflows_[slot] = nullptr;
		--overflow_entity_count_;
	}

	void compact_all() noexcept
	{
		for (size_t i = 0; i < overflows_.size(); ++i)
		{
			uint64_t* ovf = overflows_[i];
			if (ovf == nullptr) continue;
			bool all_zero = true;
			for (uint32_t b = 0; b < overflow_block_count_; ++b)
			{
				if (ovf[b] != 0) { all_zero = false; break; }
			}
			if (all_zero)
			{
				free_overflow(ovf);
				overflows_[i] = nullptr;
				--overflow_entity_count_;
			}
		}
	}
};

// 自由 swap
inline void swap(multi_block_bitmask& a, multi_block_bitmask& b) noexcept
{
	a.swap(b);
}
