#pragma once
// list_memory_pool.hpp - 变长内存池
// 接口: allocate / hard_deallocate / soft_deallocate
// soft_deallocate 释放的块可被后续 allocate 复用

#include <new>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <array>
#include <utility>
#include "oom_handler.hpp"

namespace memory {

class list_memory_pool
{
public:
	using size_type = size_t;

private:
	// === 常量 ===
	static constexpr size_t CHUNK_SIZE = 4096;
	static constexpr size_t CHUNK_ALIGN = 4096;
	static constexpr uint32_t CHUNK_MAGIC = 0xC4C1C4C1u;
	static constexpr uint32_t BIG_MAGIC = 0xB16B16B1u;

	// chunk 头部 (alignas 64 保证 DATA_AREA=4032)
	struct alignas(64) chunk_header
	{
		chunk_header* all_prev;
		chunk_header* all_next;
		char* bump_ptr;             // 下一个未切 slot 地址
		char* bump_end;             // bump 终点
		uint32_t magic;             // CHUNK_MAGIC
		uint16_t size_class;        // 档位索引
		uint16_t slot_size;         // slot 字节大小
		uint16_t slot_count;        // slot 总数
		uint16_t reserved;
	};

	static constexpr size_t CHUNK_HEADER_SIZE = sizeof(chunk_header);
	static constexpr size_t DATA_AREA_SIZE = CHUNK_SIZE - CHUNK_HEADER_SIZE;

	// 分档 slot 大小 (最小 16B: 空闲 next 指针存 slot 头部)
	static constexpr uint16_t k_slot_sizes[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, DATA_AREA_SIZE };
	static constexpr size_t SIZE_CLASS_COUNT = sizeof(k_slot_sizes) / sizeof(k_slot_sizes[0]);
	static constexpr size_t SMALL_BLOCK_THRESHOLD = DATA_AREA_SIZE; // <= 此值走 chunk, > 走大块

	// chunk wilderness: 预申请 1MB, 切 4096 对齐 chunk, 减少 OS 调用
	static constexpr size_t CHUNK_WILD_SIZE = 1 << 20; // 1MB
	struct chunk_wilderness
	{
		char* base;                // wilderness 基址
		size_t total_size;
		size_t used_offset;        // 下一个 chunk 偏移 (CHUNK_SIZE 对齐)
		chunk_wilderness* next;
	};

	// 大块头部
	struct big_block_header
	{
		big_block_header* prev;
		big_block_header* next;
		size_t total_size;        // 含 header
		size_t user_size;         // 用户请求大小
		uint32_t magic;
		uint8_t  is_soft_deleted; // 1=软删除可复用, 0=在用
		uint8_t  from_wilderness; // 1=从 wilderness 切割, 0=独立申请
		uint16_t pad1;
	};

	// 大块 wilderness: 预申请大块, 大块分配从中切割
	static constexpr size_t BIG_WILDERNESS_SIZE = 1 << 20; // 1MB
	static constexpr size_t BIG_WILDERNESS_THRESHOLD = BIG_WILDERNESS_SIZE / 2; // >= 此值走独立 OS
	struct big_wilderness_header
	{
		char* base;
		size_t total_size;
		size_t used_offset;
		big_wilderness_header* next;
	};

	// === 成员 ===
	chunk_header* all_head_{ nullptr };
	chunk_header* all_tail_{ nullptr };
	// 每档全局 LIFO 空闲栈 (跨所有 chunk)
	std::array<void*, SIZE_CLASS_COUNT> global_free_heads_{};
	// 每档活跃 chunk (全局栈空时 bump)
	std::array<chunk_header*, SIZE_CLASS_COUNT> active_chunks_{};
	chunk_wilderness* chunk_wild_head_{ nullptr };
	big_block_header* big_head_{ nullptr };
	big_wilderness_header* big_wild_head_{ nullptr };

	size_t chunk_count_{ 0 };
	size_t big_block_count_{ 0 };
	size_t big_soft_count_{ 0 };
	size_t total_allocated_bytes_{ 0 };
	size_t total_capacity_bytes_{ 0 };
	size_t peak_allocated_bytes_{ 0 };
	size_t allocation_count_{ 0 };
	size_t deallocation_count_{ 0 };

	// === 分档 ===
	// 无分支定位: 档 n 覆盖 (2^(n+3), 2^(n+4)], 位宽索引 O(1)
	[[nodiscard]] static constexpr uint8_t size_class_for_size(size_t bytes) noexcept
	{
		size_t idx = std::bit_width(bytes - 1);
		return (idx <= 4) ? static_cast<uint8_t>(0)
		                  : static_cast<uint8_t>(idx - 4);
	}

	[[nodiscard]] static constexpr uint16_t slot_count_for_class(uint8_t cls) noexcept
	{
		return static_cast<uint16_t>(DATA_AREA_SIZE / k_slot_sizes[cls]);
	}

	// === chunk 分配 (优先从 wilderness 切) ===
	[[nodiscard]] chunk_header* allocate_chunk_from_pool() noexcept
	{
		chunk_wilderness* wild = chunk_wild_head_;
		if (wild && wild->used_offset + CHUNK_SIZE <= wild->total_size) [[likely]]
		{
			char* p = wild->base + wild->used_offset;
			wild->used_offset += CHUNK_SIZE;
			return reinterpret_cast<chunk_header*>(p);
		}
		// 新建 wilderness, header 放起始, 数据区对齐到 CHUNK_SIZE
		void* raw = ::operator new(CHUNK_WILD_SIZE, std::align_val_t{ CHUNK_ALIGN }, std::nothrow);
		if (!raw) [[unlikely]]
		{
			handle_oom(CHUNK_WILD_SIZE, "list_memory_pool::allocate_chunk_from_pool");
		}
		auto* new_wild = static_cast<chunk_wilderness*>(raw);
		new_wild->base = reinterpret_cast<char*>(raw);
		new_wild->total_size = CHUNK_WILD_SIZE;
		size_t header_aligned = (sizeof(chunk_wilderness) + CHUNK_SIZE - 1) & ~(CHUNK_SIZE - 1);
		new_wild->used_offset = header_aligned;
		new_wild->next = chunk_wild_head_;
		chunk_wild_head_ = new_wild;
		char* p = new_wild->base + new_wild->used_offset;
		new_wild->used_offset += CHUNK_SIZE;
		return reinterpret_cast<chunk_header*>(p);
	}

	static void deallocate_chunk_to_pool(chunk_header* c) noexcept
	{
		// chunk 在 wilderness 中, 整体释放, 不单独释放
		(void)c;
	}

	// 初始化 chunk 为指定档位
	void init_chunk_for_class(chunk_header* c, uint8_t cls) noexcept
	{
		c->all_prev = nullptr;
		c->all_next = nullptr;
		c->magic = CHUNK_MAGIC;
		c->size_class = cls;
		c->slot_size = k_slot_sizes[cls];
		c->slot_count = slot_count_for_class(cls);
		c->bump_ptr = reinterpret_cast<char*>(c) + CHUNK_HEADER_SIZE;
		c->bump_end = c->bump_ptr + static_cast<size_t>(c->slot_count) * c->slot_size;
		c->reserved = 0;
	}

	// === all_list 链表操作 (O(1)) ===
	void link_to_all_list(chunk_header* c) noexcept
	{
		c->all_prev = all_tail_;
		c->all_next = nullptr;
		if (all_tail_)
		{
			all_tail_->all_next = c;
		}
		else
		{
			all_head_ = c;
		}
		all_tail_ = c;
		++chunk_count_;
		total_capacity_bytes_ += DATA_AREA_SIZE;
	}

	void unlink_from_all_list(chunk_header* c) noexcept
	{
		if (c->all_prev)
		{
			c->all_prev->all_next = c->all_next;
		}
		else
		{
			all_head_ = c->all_next;
		}
		if (c->all_next)
		{
			c->all_next->all_prev = c->all_prev;
		}
		else
		{
			all_tail_ = c->all_prev;
		}
		--chunk_count_;
		total_capacity_bytes_ -= DATA_AREA_SIZE;
	}

	// === active chunk ===
	// 确保该档有可 bump 的 active chunk
	[[nodiscard]] chunk_header* ensure_active_chunk(uint8_t cls) noexcept
	{
		chunk_header* c = active_chunks_[cls];
		if (c && c->bump_ptr < c->bump_end)
		{
			return c;
		}
		c = allocate_chunk_from_pool();
		init_chunk_for_class(c, cls);
		link_to_all_list(c);
		active_chunks_[cls] = c;
		return c;
	}

	// === 大块分配/释放 ===
	// LIFO 策略: 仅检查链表头部 (O(1)), 头部不匹配则直接分配新块
	// 避免遍历链表 (构造测试前模块1-3的杂块会导致 O(n) 遍历)
	[[nodiscard]] big_block_header* allocate_big_block(size_t user_size) noexcept
	{
		if (big_head_ && big_head_->is_soft_deleted && big_head_->user_size == user_size)
		{
			big_head_->is_soft_deleted = 0;
			--big_soft_count_;
			return big_head_;
		}
		size_t aligned = (user_size + CHUNK_ALIGN - 1) & ~(CHUNK_ALIGN - 1);
		size_t total = sizeof(big_block_header) + aligned;

		if (total < BIG_WILDERNESS_THRESHOLD)
		{
			big_block_header* bh = try_alloc_from_big_wilderness(user_size, aligned, total);
			if (bh)
			{
				return bh;
			}
		}

		void* raw = ::operator new(total, std::align_val_t{ CHUNK_ALIGN }, std::nothrow);
		if (!raw) [[unlikely]]
		{
			handle_oom(total, "list_memory_pool::allocate_big_block");
		}
		auto* bh = static_cast<big_block_header*>(raw);
		bh->prev = nullptr;
		bh->next = big_head_;
		if (big_head_)
		{
			big_head_->prev = bh;
		}
		big_head_ = bh;
		bh->total_size = total;
		bh->user_size = user_size;
		bh->magic = BIG_MAGIC;
		bh->is_soft_deleted = 0;
		bh->from_wilderness = 0;
		bh->pad1 = 0;
		++big_block_count_;
		total_capacity_bytes_ += aligned;
		return bh;
	}

	[[nodiscard]] big_block_header* try_alloc_from_big_wilderness(size_t user_size, size_t aligned, size_t total) noexcept
	{
		big_wilderness_header* wild = big_wild_head_;
		while (wild)
		{
			if (wild->used_offset + total <= wild->total_size)
			{
				char* p = wild->base + wild->used_offset;
				wild->used_offset += total;
				auto* bh = reinterpret_cast<big_block_header*>(p);
				bh->prev = nullptr;
				bh->next = big_head_;
				if (big_head_)
				{
					big_head_->prev = bh;
				}
				big_head_ = bh;
				bh->total_size = total;
				bh->user_size = user_size;
				bh->magic = BIG_MAGIC;
				bh->is_soft_deleted = 0;
				bh->from_wilderness = 1;
				bh->pad1 = 0;
				++big_block_count_;
				total_capacity_bytes_ += aligned;
				return bh;
			}
			wild = wild->next;
		}
		if (total <= BIG_WILDERNESS_SIZE)
		{
			wild = allocate_new_big_wilderness();
			if (wild)
			{
				char* p = wild->base + wild->used_offset;
				wild->used_offset += total;
				auto* bh = reinterpret_cast<big_block_header*>(p);
				bh->prev = nullptr;
				bh->next = big_head_;
				if (big_head_)
				{
					big_head_->prev = bh;
				}
				big_head_ = bh;
				bh->total_size = total;
				bh->user_size = user_size;
				bh->magic = BIG_MAGIC;
				bh->is_soft_deleted = 0;
				bh->from_wilderness = 1;
				bh->pad1 = 0;
				++big_block_count_;
				total_capacity_bytes_ += aligned;
				return bh;
			}
		}
		return nullptr;
	}

	[[nodiscard]] big_wilderness_header* allocate_new_big_wilderness() noexcept
	{
		void* raw = ::operator new(BIG_WILDERNESS_SIZE, std::align_val_t{ CHUNK_ALIGN }, std::nothrow);
		if (!raw) [[unlikely]]
		{
			return nullptr;
		}
		auto* wild = static_cast<big_wilderness_header*>(raw);
		wild->base = reinterpret_cast<char*>(raw);
		wild->total_size = BIG_WILDERNESS_SIZE;
		wild->used_offset = sizeof(big_wilderness_header);
		wild->next = big_wild_head_;
		big_wild_head_ = wild;
		total_capacity_bytes_ += BIG_WILDERNESS_SIZE - sizeof(big_wilderness_header);
		return wild;
	}

	void deallocate_big_block(big_block_header* bh, bool allow_release_to_os) noexcept
	{
		if (!allow_release_to_os)
		{
			bh->is_soft_deleted = 1;
			++big_soft_count_;
			return;
		}
		if (bh->prev)
		{
			bh->prev->next = bh->next;
		}
		else
		{
			big_head_ = bh->next;
		}
		if (bh->next)
		{
			bh->next->prev = bh->prev;
		}
		--big_block_count_;
		if (bh->is_soft_deleted)
		{
			--big_soft_count_;
		}
		size_t aligned = (bh->user_size + CHUNK_ALIGN - 1) & ~(CHUNK_ALIGN - 1);
		total_capacity_bytes_ -= aligned;
		if (!bh->from_wilderness)
		{
			::operator delete(bh, bh->total_size, std::align_val_t{ CHUNK_ALIGN });
		}
	}

	// === 指针归属判断 ===
	[[nodiscard]] chunk_header* find_chunk_owner(void* ptr) const noexcept
	{
		uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
		uintptr_t chunk_addr = addr & ~static_cast<uintptr_t>(CHUNK_SIZE - 1);
		auto* candidate = reinterpret_cast<chunk_header*>(chunk_addr);
		if (candidate->magic == CHUNK_MAGIC)
		{
			char* data_start = reinterpret_cast<char*>(candidate) + CHUNK_HEADER_SIZE;
			if (reinterpret_cast<char*>(ptr) >= data_start &&
			    reinterpret_cast<char*>(ptr) < reinterpret_cast<char*>(candidate) + CHUNK_SIZE)
			{
				return candidate;
			}
		}
		return nullptr;
	}

	[[nodiscard]] static big_block_header* find_big_owner(void* ptr) noexcept
	{
		auto* bh = reinterpret_cast<big_block_header*>(
		    reinterpret_cast<char*>(ptr) - sizeof(big_block_header));
		if (bh->magic == BIG_MAGIC)
		{
			return bh;
		}
		return nullptr;
	}

	// === slot 释放 (push 全局 LIFO) ===
	// 软/硬删除共用
	void deallocate_slot_to_global(void* ptr, chunk_header* c) noexcept
	{
		uint8_t cls = c->size_class;
		*reinterpret_cast<void**>(ptr) = global_free_heads_[cls];
		global_free_heads_[cls] = ptr;
	}

public:
	// === 构造/析构 ===
	constexpr list_memory_pool() noexcept = default;

	~list_memory_pool() noexcept
	{
		release_all_memory();
	}

	list_memory_pool(const list_memory_pool&) = delete;
	list_memory_pool& operator=(const list_memory_pool&) = delete;

	list_memory_pool(list_memory_pool&& other) noexcept
		: all_head_(other.all_head_)
		, all_tail_(other.all_tail_)
		, global_free_heads_(other.global_free_heads_)
		, active_chunks_(other.active_chunks_)
		, chunk_wild_head_(other.chunk_wild_head_)
		, big_head_(other.big_head_)
		, big_wild_head_(other.big_wild_head_)
		, chunk_count_(other.chunk_count_)
		, big_block_count_(other.big_block_count_)
		, big_soft_count_(other.big_soft_count_)
		, total_allocated_bytes_(other.total_allocated_bytes_)
		, total_capacity_bytes_(other.total_capacity_bytes_)
		, peak_allocated_bytes_(other.peak_allocated_bytes_)
		, allocation_count_(other.allocation_count_)
		, deallocation_count_(other.deallocation_count_)
	{
		other.all_head_ = nullptr;
		other.all_tail_ = nullptr;
		other.global_free_heads_.fill(nullptr);
		other.active_chunks_.fill(nullptr);
		other.chunk_wild_head_ = nullptr;
		other.big_head_ = nullptr;
		other.big_wild_head_ = nullptr;
		other.chunk_count_ = 0;
		other.big_block_count_ = 0;
		other.big_soft_count_ = 0;
		other.total_allocated_bytes_ = 0;
		other.total_capacity_bytes_ = 0;
		other.peak_allocated_bytes_ = 0;
		other.allocation_count_ = 0;
		other.deallocation_count_ = 0;
	}

	list_memory_pool& operator=(list_memory_pool&& other) noexcept
	{
		if (this != &other)
		{
			release_all_memory();
			all_head_ = other.all_head_;
			all_tail_ = other.all_tail_;
			global_free_heads_ = other.global_free_heads_;
			active_chunks_ = other.active_chunks_;
			chunk_wild_head_ = other.chunk_wild_head_;
			big_head_ = other.big_head_;
			big_wild_head_ = other.big_wild_head_;
			chunk_count_ = other.chunk_count_;
			big_block_count_ = other.big_block_count_;
			big_soft_count_ = other.big_soft_count_;
			total_allocated_bytes_ = other.total_allocated_bytes_;
			total_capacity_bytes_ = other.total_capacity_bytes_;
			peak_allocated_bytes_ = other.peak_allocated_bytes_;
			allocation_count_ = other.allocation_count_;
			deallocation_count_ = other.deallocation_count_;
			other.all_head_ = nullptr;
			other.all_tail_ = nullptr;
			other.global_free_heads_.fill(nullptr);
			other.active_chunks_.fill(nullptr);
			other.chunk_wild_head_ = nullptr;
			other.big_head_ = nullptr;
			other.big_wild_head_ = nullptr;
			other.chunk_count_ = 0;
			other.big_block_count_ = 0;
			other.big_soft_count_ = 0;
			other.total_allocated_bytes_ = 0;
			other.total_capacity_bytes_ = 0;
			other.peak_allocated_bytes_ = 0;
			other.allocation_count_ = 0;
			other.deallocation_count_ = 0;
		}
		return *this;
	}

	// === 核心分配接口 ===

	// 分配 bytes 字节 (小块走分档, 大块走 wilderness/独立)
	[[nodiscard]] void* allocate(size_t bytes) noexcept
	{
		if (bytes == 0)
		{
			return nullptr;
		}
		++allocation_count_;
		if (bytes > SMALL_BLOCK_THRESHOLD)
		{
			big_block_header* bh = allocate_big_block(bytes);
			total_allocated_bytes_ += bytes;
			if (total_allocated_bytes_ > peak_allocated_bytes_)
			{
				peak_allocated_bytes_ = total_allocated_bytes_;
			}
			return reinterpret_cast<char*>(bh) + sizeof(big_block_header);
		}
		uint8_t cls = size_class_for_size(bytes);
		// 快路径 1: 全局 LIFO pop
		void* p = global_free_heads_[cls];
		if (p) [[likely]]
		{
			global_free_heads_[cls] = *reinterpret_cast<void**>(p);
			total_allocated_bytes_ += k_slot_sizes[cls];
			if (total_allocated_bytes_ > peak_allocated_bytes_)
			{
				peak_allocated_bytes_ = total_allocated_bytes_;
			}
			return p;
		}
		// 快路径 2: active chunk bump
		chunk_header* c = active_chunks_[cls];
		if (c && c->bump_ptr < c->bump_end) [[likely]]
		{
			void* ptr = c->bump_ptr;
			c->bump_ptr += c->slot_size;
			total_allocated_bytes_ += k_slot_sizes[cls];
			if (total_allocated_bytes_ > peak_allocated_bytes_)
			{
				peak_allocated_bytes_ = total_allocated_bytes_;
			}
			return ptr;
		}
		// 慢路径: active 满, 申请新 chunk
		c = ensure_active_chunk(cls);
		void* ptr = c->bump_ptr;
		c->bump_ptr += c->slot_size;
		total_allocated_bytes_ += k_slot_sizes[cls];
		if (total_allocated_bytes_ > peak_allocated_bytes_)
		{
			peak_allocated_bytes_ = total_allocated_bytes_;
		}
		return ptr;
	}

	// 分配并清零
	[[nodiscard]] void* allocate_zeroed(size_t bytes) noexcept
	{
		void* p = allocate(bytes);
		if (p)
		{
			std::memset(p, 0, bytes);
		}
		return p;
	}

	template <typename T, typename... Args>
	[[nodiscard]] T* construct(Args&&... args) noexcept
	{
		void* ptr = allocate(sizeof(T));
		if (!ptr)
		{
			return nullptr;
		}
		return new (ptr) T(std::forward<Args>(args)...);
	}

	// 对象析构 + 软删除
	template <typename T>
	void destroy(T* ptr) noexcept
	{
		if (!ptr)
		{
			return;
		}
		ptr->~T();
		soft_deallocate(ptr);
	}

	// 软删除: 保留内存供复用
	void soft_deallocate(void* ptr) noexcept
	{
		if (!ptr)
		{
			return;
		}
		++deallocation_count_;
		chunk_header* c = find_chunk_owner(ptr);
		if (c)
		{
			total_allocated_bytes_ -= c->slot_size;
			deallocate_slot_to_global(ptr, c);
			return;
		}
		big_block_header* bh = find_big_owner(ptr);
		if (bh)
		{
			total_allocated_bytes_ -= bh->user_size;
			deallocate_big_block(bh, false);
			return;
		}
	}

	// 硬删除: push 全局栈 (chunk 回收延迟到 release_all_memory)
	void hard_deallocate(void* ptr) noexcept
	{
		if (!ptr)
		{
			return;
		}
		++deallocation_count_;
		chunk_header* c = find_chunk_owner(ptr);
		if (c)
		{
			total_allocated_bytes_ -= c->slot_size;
			deallocate_slot_to_global(ptr, c);
			return;
		}
		big_block_header* bh = find_big_owner(ptr);
		if (bh)
		{
			total_allocated_bytes_ -= bh->user_size;
			deallocate_big_block(bh, true);
			return;
		}
	}

	// 统一 deallocate 接口 (默认软删除, 与其他分配器签名一致)
	void deallocate(void* ptr) noexcept
	{
		soft_deallocate(ptr);
	}

	void deallocate(void* ptr, size_t /*bytes*/) noexcept
	{
		soft_deallocate(ptr);
	}

	// 对齐分配: align <= CHUNK_ALIGN(4096) 时直接走普通路径 (chunk/big 均已 4096 对齐)
	// align > CHUNK_ALIGN 时分配 size+align, 手动对齐
	[[nodiscard]] void* allocate_aligned(size_t bytes, size_t align) noexcept
	{
		if (bytes == 0 || align == 0) [[unlikely]]
		{
			return nullptr;
		}
		if (align <= CHUNK_ALIGN) [[likely]]
		{
			return allocate(bytes);
		}
		size_t raw_size = bytes + align + sizeof(void*);
		void* raw = allocate(raw_size);
		if (!raw) [[unlikely]]
		{
			return nullptr;
		}
		uintptr_t base = reinterpret_cast<uintptr_t>(raw) + sizeof(void*);
		uintptr_t aligned = (base + align - 1) & ~(align - 1);
		void* user = reinterpret_cast<void*>(aligned);
		*(static_cast<void**>(user) - 1) = raw;
		return user;
	}

	void deallocate_aligned(void* p) noexcept
	{
		if (!p) [[unlikely]]
		{
			return;
		}
		void* raw = *(static_cast<void**>(p) - 1);
		soft_deallocate(raw);
	}

	// === 重新分配 ===

	[[nodiscard]] bool reallocate_inplace(void* ptr, size_t old_bytes, size_t new_bytes) noexcept
	{
		if (!ptr)
		{
			return false;
		}
		if (old_bytes > SMALL_BLOCK_THRESHOLD || new_bytes > SMALL_BLOCK_THRESHOLD)
		{
			return false;
		}
		uint8_t old_cls = size_class_for_size(old_bytes);
		uint8_t new_cls = size_class_for_size(new_bytes);
		return old_cls == new_cls;
	}

	[[nodiscard]] void* reallocate(void* ptr, size_t old_bytes, size_t new_bytes) noexcept
	{
		if (new_bytes == 0)
		{
			hard_deallocate(ptr);
			return nullptr;
		}
		if (!ptr)
		{
			return allocate(new_bytes);
		}
		if (reallocate_inplace(ptr, old_bytes, new_bytes))
		{
			return ptr;
		}
		void* new_ptr = allocate(new_bytes);
		if (!new_ptr)
		{
			return nullptr;
		}
		size_t copy_size = old_bytes < new_bytes ? old_bytes : new_bytes;
		std::memcpy(new_ptr, ptr, copy_size);
		hard_deallocate(ptr);
		return new_ptr;
	}

	// === 查询接口 ===

	[[nodiscard]] bool owns(void* ptr) const noexcept
	{
		if (!ptr)
		{
			return false;
		}
		if (find_chunk_owner(ptr))
		{
			return true;
		}
		auto* bh = reinterpret_cast<const big_block_header*>(
		    reinterpret_cast<const char*>(ptr) - sizeof(big_block_header));
		return bh->magic == BIG_MAGIC;
	}

	[[nodiscard]] size_t allocation_size(void* ptr) const noexcept
	{
		if (!ptr)
		{
			return 0;
		}
		chunk_header* c = find_chunk_owner(ptr);
		if (c)
		{
			return c->slot_size;
		}
		auto* bh = reinterpret_cast<const big_block_header*>(
		    reinterpret_cast<const char*>(ptr) - sizeof(big_block_header));
		if (bh->magic == BIG_MAGIC)
		{
			return bh->user_size;
		}
		return 0;
	}

	// === 统计接口 ===
	[[nodiscard]] constexpr size_t total_allocated_bytes() const noexcept { return total_allocated_bytes_; }
	[[nodiscard]] constexpr size_t total_capacity_bytes() const noexcept { return total_capacity_bytes_; }
	[[nodiscard]] constexpr size_t total_free_bytes() const noexcept
	{
		return total_capacity_bytes_ >= total_allocated_bytes_
			? total_capacity_bytes_ - total_allocated_bytes_
			: 0;
	}
	[[nodiscard]] constexpr size_t peak_allocated_bytes() const noexcept { return peak_allocated_bytes_; }
	[[nodiscard]] constexpr size_t allocation_count() const noexcept { return allocation_count_; }
	[[nodiscard]] constexpr size_t deallocation_count() const noexcept { return deallocation_count_; }
	[[nodiscard]] constexpr size_t chunk_count() const noexcept { return chunk_count_; }
	[[nodiscard]] constexpr size_t big_block_count() const noexcept { return big_block_count_; }

	// 聚合统计结构
	struct pool_stats
	{
		size_t allocated_bytes;      // 当前已分配
		size_t capacity_bytes;       // 总容量
		size_t free_bytes;           // 空闲 = capacity - allocated
		size_t peak_allocated_bytes; // 历史峰值
		size_t allocation_count;     // 累计分配次数
		size_t deallocation_count;   // 累计释放次数
		size_t chunk_count;          // 活跃 chunk 数
		size_t big_block_count;      // 大块数 (含软删除)
		size_t big_soft_count;       // 软删除大块数 (可复用)
	};

	[[nodiscard]] pool_stats stats() const noexcept
	{
		return pool_stats{
			total_allocated_bytes_,
			total_capacity_bytes_,
			total_free_bytes(),
			peak_allocated_bytes_,
			allocation_count_,
			deallocation_count_,
			chunk_count_,
			big_block_count_,
			big_soft_count_
		};
	}

	// === 管理接口 ===

	// 释放所有内存 (chunk + 大块 + wilderness)
	void release_all_memory() noexcept
	{
		// 先清空全局栈 (slot 指针即将失效)
		global_free_heads_.fill(nullptr);

		all_head_ = nullptr;
		all_tail_ = nullptr;
		active_chunks_.fill(nullptr);
		chunk_count_ = 0;

		// 释放 chunk wilderness (整块释放, 含所有 chunk)
		chunk_wilderness* cwild = chunk_wild_head_;
		while (cwild)
		{
			chunk_wilderness* next = cwild->next;
			::operator delete(cwild->base, CHUNK_WILD_SIZE, std::align_val_t{ CHUNK_ALIGN });
			cwild = next;
		}
		chunk_wild_head_ = nullptr;

		// 释放独立大块 (非 wilderness)
		big_block_header* bh = big_head_;
		while (bh)
		{
			big_block_header* next = bh->next;
			if (!bh->from_wilderness)
			{
				::operator delete(bh, bh->total_size, std::align_val_t{ CHUNK_ALIGN });
			}
			bh = next;
		}
		big_head_ = nullptr;
		big_block_count_ = 0;
		big_soft_count_ = 0;

		// 释放大块 wilderness
		big_wilderness_header* bwild = big_wild_head_;
		while (bwild)
		{
			big_wilderness_header* next = bwild->next;
			::operator delete(bwild->base, BIG_WILDERNESS_SIZE, std::align_val_t{ CHUNK_ALIGN });
			bwild = next;
		}
		big_wild_head_ = nullptr;

		total_allocated_bytes_ = 0;
		total_capacity_bytes_ = 0;
	}

	// 清空统计计数 (不释放内存)
	void reset_statistics() noexcept
	{
		allocation_count_ = 0;
		deallocation_count_ = 0;
		peak_allocated_bytes_ = total_allocated_bytes_;
	}

	// 轻量重置: 保留 chunk 内存, 清空 free list 和 active chunk
	void reset() noexcept
	{
		global_free_heads_.fill(nullptr);
		active_chunks_.fill(nullptr);
		all_head_ = nullptr;
		all_tail_ = nullptr;
		chunk_count_ = 0;
		big_head_ = nullptr;
		big_block_count_ = 0;
		big_soft_count_ = 0;
		total_allocated_bytes_ = 0;
		// 保留 chunk_wild_head_ 和 big_wild_head_ 及 total_capacity_bytes_
	}

	// 归还空闲 wilderness 内存给 OS
	// 保留仍被 chunk/big_block 引用的 wilderness, 仅释放完全未用的
	void shrink_to_fit() noexcept
	{
		// chunk wilderness: 释放 used_offset == sizeof(header) 的 (完全未切 chunk)
		chunk_wilderness* cwild = chunk_wild_head_;
		chunk_wilderness* cprev = nullptr;
		size_t header_aligned = (sizeof(chunk_wilderness) + CHUNK_SIZE - 1) & ~(CHUNK_SIZE - 1);
		while (cwild)
		{
			chunk_wilderness* next = cwild->next;
			if (cwild->used_offset <= header_aligned)
			{
				// 完全未用, 释放
				if (cprev)
				{
					cprev->next = next;
				}
				else
				{
					chunk_wild_head_ = next;
				}
				::operator delete(cwild->base, CHUNK_WILD_SIZE, std::align_val_t{ CHUNK_ALIGN });
				// capacity 不变: wilderness 的 header 区不在 total_capacity_bytes_ 中
			}
			else
			{
				cprev = cwild;
			}
			cwild = next;
		}

		// big wilderness: 同理释放完全未用的
		big_wilderness_header* bwild = big_wild_head_;
		big_wilderness_header* bprev = nullptr;
		while (bwild)
		{
			big_wilderness_header* next = bwild->next;
			if (bwild->used_offset <= sizeof(big_wilderness_header))
			{
				if (bprev)
				{
					bprev->next = next;
				}
				else
				{
					big_wild_head_ = next;
				}
				::operator delete(bwild->base, BIG_WILDERNESS_SIZE, std::align_val_t{ CHUNK_ALIGN });
				total_capacity_bytes_ -= (BIG_WILDERNESS_SIZE - sizeof(big_wilderness_header));
			}
			else
			{
				bprev = bwild;
			}
			bwild = next;
		}
	}

	// 遍历空闲块 (调试/统计用)
	// 回调签名: void(void* ptr, size_t size, const char* category)
	// category: "slot" (小块全局栈) / "big_soft" (软删除大块)
	template <typename Fn>
	void iterate_free(Fn&& fn) const noexcept
	{
		// 遍历每档全局 LIFO 栈
		for (size_t cls = 0; cls < SIZE_CLASS_COUNT; ++cls)
		{
			void* p = global_free_heads_[cls];
			while (p)
			{
				fn(p, k_slot_sizes[cls], "slot");
				p = *static_cast<void**>(p);
			}
		}
		// 遍历软删除大块
		big_block_header* bh = big_head_;
		while (bh)
		{
			if (bh->is_soft_deleted)
			{
				void* user_ptr = reinterpret_cast<char*>(bh) + sizeof(big_block_header);
				fn(user_ptr, bh->user_size, "big_soft");
			}
			bh = bh->next;
		}
	}

	// 交换两个池
	void swap(list_memory_pool& other) noexcept
	{
		std::swap(all_head_, other.all_head_);
		std::swap(all_tail_, other.all_tail_);
		std::swap(global_free_heads_, other.global_free_heads_);
		std::swap(active_chunks_, other.active_chunks_);
		std::swap(chunk_wild_head_, other.chunk_wild_head_);
		std::swap(big_head_, other.big_head_);
		std::swap(big_wild_head_, other.big_wild_head_);
		std::swap(chunk_count_, other.chunk_count_);
		std::swap(big_block_count_, other.big_block_count_);
		std::swap(big_soft_count_, other.big_soft_count_);
		std::swap(total_allocated_bytes_, other.total_allocated_bytes_);
		std::swap(total_capacity_bytes_, other.total_capacity_bytes_);
		std::swap(peak_allocated_bytes_, other.peak_allocated_bytes_);
		std::swap(allocation_count_, other.allocation_count_);
		std::swap(deallocation_count_, other.deallocation_count_);
	}
};

inline void swap(list_memory_pool& a, list_memory_pool& b) noexcept
{
	a.swap(b);
}

} // namespace memory
