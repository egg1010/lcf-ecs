#pragma once

#include <cstdint>
#include <cstddef>
#include <bit>
#include "class_pool.hpp"

// 实体掩码管理器 — 无上限位掩码存储
// 存储: class_pool<uint64_t> 扁平化, 每实体 num_blocks_ 个 uint64_t
// 实体 e 第 b 块位置: e * num_blocks_ + b
// num_blocks_==1 快速路径: 省略乘法, 等价于直接数组访问
// 用法: 注册组件前调用 reserve_blocks(n) 预分配, 避免 reshape
class entity_mask_manager
{
private:
	class_pool<uint64_t> masks_;
	uint32_t num_blocks_{1};

	[[nodiscard]] static constexpr size_t index_of(uint32_t e, uint32_t blocks, uint32_t b) noexcept
	{
		return static_cast<size_t>(e) * blocks + b;
	}

	[[nodiscard]] size_t entity_count_impl() const noexcept
	{
		if (num_blocks_ == 0) [[unlikely]] return 0;
		return masks_.size() / num_blocks_;
	}

	// reshape: 从后往前搬移, 新位置始终 ≥ 旧位置, 保证无覆盖
	void reshape(uint32_t new_blocks) noexcept
	{
		if (new_blocks <= num_blocks_) [[unlikely]] return;
		uint32_t old_blocks = num_blocks_;
		uint32_t n = static_cast<uint32_t>(entity_count_impl());
		if (n == 0)
		{
			num_blocks_ = new_blocks;
			return;
		}
		size_t new_size = static_cast<size_t>(n) * new_blocks;
		masks_.resize(new_size, 0);
		for (uint32_t e = n; e > 0; --e)
		{
			uint32_t ei = e - 1;
			for (uint32_t b = old_blocks; b > 0; --b)
			{
				uint32_t bi = b - 1;
				masks_[index_of(ei, new_blocks, bi)] = masks_[index_of(ei, old_blocks, bi)];
			}
		}
		num_blocks_ = new_blocks;
	}

public:
	entity_mask_manager() noexcept = default;

	// 预分配掩码块数 — 注册组件前调用, 避免 reshape
	void reserve_blocks(uint32_t num_blocks) noexcept
	{
		if (num_blocks > num_blocks_) reshape(num_blocks);
	}

	[[nodiscard]] uint32_t num_blocks() const noexcept { return num_blocks_; }

	// 确保实体容量 (与 entity_manager 同步增长)
	void ensure_entity(uint32_t entity_index) noexcept
	{
		if (num_blocks_ == 1) [[likely]]
		{
			size_t required = static_cast<size_t>(entity_index) + 1;
			if (required > masks_.size()) [[unlikely]]
				masks_.resize(required, 0);
			return;
		}
		size_t required = index_of(entity_index + 1, num_blocks_, 0);
		if (required > masks_.size()) [[unlikely]]
			masks_.resize(required, 0);
	}

	// 设置掩码位
	void set_bit(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (block_idx >= num_blocks_) [[unlikely]] return;
		if (num_blocks_ == 1) [[likely]]
		{
			if (entity_index < masks_.size()) [[likely]]
				masks_[entity_index] |= (1ULL << bit_offset);
			else [[unlikely]]
			{
				ensure_entity(entity_index);
				masks_[entity_index] |= (1ULL << bit_offset);
			}
			return;
		}
		size_t idx = index_of(entity_index, num_blocks_, block_idx);
		if (idx < masks_.size()) [[likely]]
			masks_[idx] |= (1ULL << bit_offset);
		else [[unlikely]]
		{
			ensure_entity(entity_index);
			masks_[index_of(entity_index, num_blocks_, block_idx)] |= (1ULL << bit_offset);
		}
	}

	// 清除掩码位
	void clear_bit(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (block_idx >= num_blocks_) [[unlikely]] return;
		if (num_blocks_ == 1) [[likely]]
		{
			if (entity_index < masks_.size()) [[likely]]
				masks_[entity_index] &= ~(1ULL << bit_offset);
			return;
		}
		size_t idx = index_of(entity_index, num_blocks_, block_idx);
		if (idx < masks_.size()) [[likely]]
			masks_[idx] &= ~(1ULL << bit_offset);
	}

	// 获取掩码块 (向后兼容: get_block(e, 0) 等价于原 get_mask)
	[[nodiscard]] uint64_t get_block(uint32_t entity_index, uint32_t block_idx) const noexcept
	{
		if (block_idx >= num_blocks_) [[unlikely]] return 0;
		if (num_blocks_ == 1) [[likely]]
		{
			if (entity_index >= masks_.size()) [[unlikely]] return 0;
			return masks_[entity_index];
		}
		size_t idx = index_of(entity_index, num_blocks_, block_idx);
		if (idx >= masks_.size()) [[unlikely]] return 0;
		return masks_[idx];
	}

	// 无边界检查设置掩码位 (调用方保证 entity_index 有效且 block_idx < num_blocks_)
	void set_bit_no_check(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (num_blocks_ == 1) [[likely]]
		{
			masks_[entity_index] |= (1ULL << bit_offset);
			return;
		}
		masks_[index_of(entity_index, num_blocks_, block_idx)] |= (1ULL << bit_offset);
	}

	// 无边界检查清除掩码位
	void clear_bit_no_check(uint32_t entity_index, uint32_t block_idx, uint32_t bit_offset) noexcept
	{
		if (num_blocks_ == 1) [[likely]]
		{
			masks_[entity_index] &= ~(1ULL << bit_offset);
			return;
		}
		masks_[index_of(entity_index, num_blocks_, block_idx)] &= ~(1ULL << bit_offset);
	}

	// 清零实体所有掩码块
	void clear_entity(uint32_t entity_index) noexcept
	{
		if (num_blocks_ == 1) [[likely]]
		{
			if (entity_index < masks_.size()) [[likely]]
				masks_[entity_index] = 0;
			return;
		}
		size_t base = index_of(entity_index, num_blocks_, 0);
		if (base >= masks_.size()) [[unlikely]] return;
		size_t end = base + num_blocks_;
		if (end > masks_.size()) end = masks_.size();
		for (size_t i = base; i < end; ++i)
			masks_[i] = 0;
	}

	// 与 entity 增长同步 (预分配实体时批量扩容)
	void resize_entities(uint32_t new_count) noexcept
	{
		if (num_blocks_ == 1) [[likely]]
		{
			if (new_count > masks_.size()) [[unlikely]]
				masks_.resize(new_count, 0);
			return;
		}
		size_t required = index_of(new_count, num_blocks_, 0);
		if (required > masks_.size())
			masks_.resize(required, 0);
	}

	// 遍历实体所有置位 bit (block_idx, bit_offset)
	// 性能: O(实体实际置位 bit 数), 不是 O(类型总数)
	// 风险规避:
	//   1) block 是 masks_[idx] 的副本, blsr 操作不影响原掩码, 迭代中可安全修改原掩码
	//   2) while(block) / if(block==0) 保证 countr_zero 入参非零, 无 UB
	//   3) num_blocks_==1 快速路径省略乘法, 等价直接数组访问
	//   4) type_id 越界由调用方在回调内 return 处理
	template <typename Func>
	void for_each_set_bit(uint32_t entity_index, Func&& func) const noexcept
	{
		if (num_blocks_ == 1) [[likely]]
		{
			if (entity_index >= masks_.size()) [[unlikely]] return;
			uint64_t block = masks_[entity_index];
			while (block)
			{
				uint32_t offset = static_cast<uint32_t>(std::countr_zero(block));
				func(0, offset);
				block &= block - 1;
			}
			return;
		}
		size_t base = index_of(entity_index, num_blocks_, 0);
		if (base + num_blocks_ > masks_.size()) [[unlikely]] return;
		for (uint32_t b = 0; b < num_blocks_; ++b)
		{
			uint64_t block = masks_[base + b];
			if (block == 0) continue;
			do
			{
				uint32_t offset = static_cast<uint32_t>(std::countr_zero(block));
				func(b, offset);
				block &= block - 1;
			} while (block);
		}
	}
};