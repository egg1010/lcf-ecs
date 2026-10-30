#pragma once

#include <cstddef>
#include <new>

#include "dense.hpp"
#include "force_inline.hpp"

// stable_array - 分块按需增长数组
// 元素地址在增长后保持不变, 供需要长期持有元素指针的索引表使用
// 仅要求元素可默认构造; 元素永不移动, 析构时逐项销毁并释放分块

template<typename T, size_t chunk_size = 16>
class stable_array
{
public:
	stable_array() noexcept = default;

	~stable_array() noexcept { clear(); }

	stable_array(const stable_array&) = delete;
	stable_array& operator=(const stable_array&) = delete;
	stable_array(stable_array&&) = delete;
	stable_array& operator=(stable_array&&) = delete;

	// 保证前 count 项已构造; 分配失败返回 false, 已保证范围不变
	[[nodiscard]] bool ensure(size_t count) noexcept
	{
		if (count <= size_) { return true; }
		const size_t need_chunks = (count + chunk_size - 1) / chunk_size;
		while (chunks_.size() < need_chunks)
		{
			T* chunk = allocate_chunk();
			if (chunk == nullptr) { return false; }
			chunks_.emplace_back(chunk);
		}
		size_ = count;
		return true;
	}

	[[nodiscard]] size_t size() const noexcept { return size_; }

	[[nodiscard]] LCF_FORCE_INLINE T& operator[](size_t index) noexcept
	{
		return chunks_[index / chunk_size][index % chunk_size];
	}

	[[nodiscard]] LCF_FORCE_INLINE const T& operator[](size_t index) const noexcept
	{
		return chunks_[index / chunk_size][index % chunk_size];
	}

	void clear() noexcept
	{
		for (size_t i = 0; i < chunks_.size(); ++i)
		{
			T* chunk = chunks_[i];
			for (size_t j = 0; j < chunk_size; ++j)
			{
				chunk[j].~T();
			}
			if constexpr (alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__)
			{
				::operator delete(chunk);
			}
			else
			{
				::operator delete(chunk, std::align_val_t{alignof(T)});
			}
		}
		chunks_.clear();
		size_ = 0;
	}

private:
	static T* allocate_chunk() noexcept
	{
		void* raw = nullptr;
		if constexpr (alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__)
		{
			raw = ::operator new(sizeof(T) * chunk_size, std::nothrow);
		}
		else
		{
			raw = ::operator new(sizeof(T) * chunk_size,
			                     std::align_val_t{alignof(T)}, std::nothrow);
		}
		if (raw == nullptr) { return nullptr; }

		T* chunk = static_cast<T*>(raw);
		for (size_t i = 0; i < chunk_size; ++i)
		{
			new (&chunk[i]) T{};
		}
		return chunk;
	}

	dense<T*> chunks_{};
	size_t size_{0};
};
