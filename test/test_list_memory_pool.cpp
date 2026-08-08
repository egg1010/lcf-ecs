// test_list_memory_pool.cpp - list_memory_pool 功能测试
// 验证: 分配/硬删除/软删除/分档/大块/指针归属/重新分配/统计/碎片自愈/软删除复用
#include "include/part/memory/list_memory_pool.hpp"
#include "include/part/dense.hpp"
#include <cstdio>
#include <cstring>
#include <cassert>

using namespace memory;
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// 测试计数
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
	if (cond) { ++g_pass; } \
	else { ++g_fail; printf("FAIL: %s (line %d)\n", #cond, __LINE__); } \
} while(0)

static void test_basic_allocate_hard_deallocate()
{
	list_memory_pool pool;

	// 基本分配
	void* p1 = pool.allocate(16);
	CHECK(p1 != nullptr);
	CHECK(pool.owns(p1));
	CHECK(pool.allocation_size(p1) == 16);
	CHECK(pool.total_allocated_bytes() == 16);
	CHECK(pool.allocation_count() == 1);

	// 分配并清零
	void* p2 = pool.allocate_zeroed(64);
	CHECK(p2 != nullptr);
	CHECK(pool.owns(p2));
	for (size_t i = 0; i < 64; ++i)
	{
		CHECK(static_cast<unsigned char*>(p2)[i] == 0);
	}

	// 硬删除
	pool.hard_deallocate(p1);
	CHECK(pool.deallocation_count() == 1);
	CHECK(pool.total_allocated_bytes() == 64);

	pool.hard_deallocate(p2);
	CHECK(pool.deallocation_count() == 2);
	CHECK(pool.total_allocated_bytes() == 0);
}

static void test_size_classes()
{
	list_memory_pool pool;

	// 各档位边界
	struct { size_t bytes; uint16_t expected_slot; } cases[] = {
		{1, 16}, {16, 16},
		{17, 32}, {32, 32},
		{33, 64}, {64, 64},
		{65, 128}, {128, 128},
		{129, 256}, {256, 256},
		{257, 512}, {512, 512},
		{513, 1024}, {1024, 1024},
		{1025, 2048}, {2048, 2048},
		{2049, 4032}, {4032, 4032},
	};

	for (auto& tc : cases)
	{
		void* p = pool.allocate(tc.bytes);
		CHECK(p != nullptr);
		CHECK(pool.allocation_size(p) == tc.expected_slot);
		pool.hard_deallocate(p);
	}
	CHECK(pool.total_allocated_bytes() == 0);
}

static void test_chunk_fill_and_release()
{
	list_memory_pool pool;

	// 填满一个 16B 档 chunk (252 slot, DATA_AREA=4032, 4032/16=252)
	dense<void*> ptrs;
	ptrs.increase_capacity(300);
	for (size_t i = 0; i < 252; ++i)
	{
		void* p = pool.allocate(16);
		CHECK(p != nullptr);
		ptrs[i] = p;
	}
	CHECK(pool.chunk_count() >= 1);

	// 逐个硬删除, 最后一个释放后 chunk 全空进缓存或归还
	for (size_t i = 0; i < 252; ++i)
	{
		pool.hard_deallocate(ptrs[i]);
	}
	// chunk 全空后进缓存或归还, active_chunks_ 清空
	// 重新分配 16B 会从缓存复用或新建 chunk
	void* trigger = pool.allocate(16);
	CHECK(trigger != nullptr);
	// trigger 占 16B
	CHECK(pool.total_allocated_bytes() == 16);
}

static void test_slot_reuse()
{
	list_memory_pool pool;

	// 分配并软删除, 验证 slot 被复用 (全局 LIFO pop)
	void* p1 = pool.allocate(32);
	CHECK(p1 != nullptr);
	pool.soft_deallocate(p1);

	// 再次分配同档, 应复用同一 slot (全局 LIFO pop)
	void* p2 = pool.allocate(32);
	CHECK(p2 == p1);
	pool.hard_deallocate(p2);

	// 硬删除后 chunk 标记 reclaimable, 重新分配触发惰性回收
	void* trigger = pool.allocate(32);
	CHECK(trigger != nullptr);
	pool.hard_deallocate(trigger);
}

static void test_big_block()
{
	list_memory_pool pool;

	// 大块 (>4032)
	void* big1 = pool.allocate(8192);
	CHECK(big1 != nullptr);
	CHECK(pool.owns(big1));
	CHECK(pool.allocation_size(big1) == 8192);
	CHECK(pool.big_block_count() == 1);
	CHECK(pool.chunk_count() == 0);

	// 写入验证
	std::memset(big1, 0xAB, 8192);

	// 另一个大块
	void* big2 = pool.allocate(16384);
	CHECK(big2 != nullptr);
	CHECK(big2 != big1);
	CHECK(pool.big_block_count() == 2);

	// 硬删除
	pool.hard_deallocate(big1);
	CHECK(pool.big_block_count() == 1);
	pool.hard_deallocate(big2);
	CHECK(pool.big_block_count() == 0);
	CHECK(pool.total_allocated_bytes() == 0);
}

static void test_mixed_sizes()
{
	list_memory_pool pool;

	// 混合大小分配
	void* ptrs[10];
	ptrs[0] = pool.allocate(16);
	ptrs[1] = pool.allocate(100);
	ptrs[2] = pool.allocate(500);
	ptrs[3] = pool.allocate(2000);
	ptrs[4] = pool.allocate(50);
	ptrs[5] = pool.allocate(16);
	ptrs[6] = pool.allocate(8192);   // 大块
	ptrs[7] = pool.allocate(128);
	ptrs[8] = pool.allocate(4032);
	ptrs[9] = pool.allocate(32);

	for (int i = 0; i < 10; ++i)
	{
		CHECK(ptrs[i] != nullptr);
		CHECK(pool.owns(ptrs[i]));
	}

	// 乱序硬删除
	pool.hard_deallocate(ptrs[3]);
	pool.hard_deallocate(ptrs[6]);
	pool.hard_deallocate(ptrs[0]);
	pool.hard_deallocate(ptrs[8]);
	pool.hard_deallocate(ptrs[1]);
	pool.hard_deallocate(ptrs[9]);
	pool.hard_deallocate(ptrs[2]);
	pool.hard_deallocate(ptrs[5]);
	pool.hard_deallocate(ptrs[7]);
	pool.hard_deallocate(ptrs[4]);

	CHECK(pool.total_allocated_bytes() == 0);
}

static void test_reallocate_inplace()
{
	list_memory_pool pool;

	// 同档原地扩容
	void* p = pool.allocate(20);  // 32B 档
	CHECK(p != nullptr);
	CHECK(pool.reallocate_inplace(p, 20, 30));   // 30 仍在 32B 档
	CHECK(!pool.reallocate_inplace(p, 30, 100)); // 100 跨档到 128B

	pool.hard_deallocate(p);
}

static void test_reallocate_copy()
{
	list_memory_pool pool;

	// 拷贝式重新分配
	void* p = pool.allocate(16);
	CHECK(p != nullptr);
	std::memcpy(p, "Hello, World!", 14);

	// 扩容到不同档 (16→128), 需拷贝
	void* new_p = pool.reallocate(p, 16, 100);
	CHECK(new_p != nullptr);
	CHECK(new_p != p);
	CHECK(std::memcmp(new_p, "Hello, World!", 14) == 0);

	pool.hard_deallocate(new_p);
}

static void test_ownership_invalid()
{
	list_memory_pool pool;

	// 栈指针不属于池
	int stack_var = 42;
	CHECK(!pool.owns(&stack_var));

	// 空指针
	CHECK(!pool.owns(nullptr));

	// allocation_size 对非池指针返回 0
	CHECK(pool.allocation_size(&stack_var) == 0);
}

static void test_statistics()
{
	list_memory_pool pool;

	// 分配多个
	void* p1 = pool.allocate(16);
	void* p2 = pool.allocate(64);
	void* p3 = pool.allocate(8192);

	CHECK(pool.allocation_count() == 3);
	CHECK(pool.deallocation_count() == 0);
	CHECK(pool.peak_allocated_bytes() >= 16 + 64 + 8192);

	// 硬删除部分
	pool.hard_deallocate(p2);
	CHECK(pool.allocation_count() == 3);
	CHECK(pool.deallocation_count() == 1);

	// reset_statistics 不影响内存
	pool.reset_statistics();
	CHECK(pool.allocation_count() == 0);
	CHECK(pool.deallocation_count() == 0);
	CHECK(pool.total_allocated_bytes() == 16 + 8192);

	pool.hard_deallocate(p1);
	pool.hard_deallocate(p3);
}

static void test_move_semantics()
{
	list_memory_pool pool1;
	void* p = pool1.allocate(64);
	CHECK(p != nullptr);

	// 移动构造
	list_memory_pool pool2(std::move(pool1));
	CHECK(pool2.owns(p));
	CHECK(pool1.chunk_count() == 0);
	CHECK(pool1.big_block_count() == 0);

	// 移动赋值
	list_memory_pool pool3;
	pool3 = std::move(pool2);
	CHECK(pool3.owns(p));
	CHECK(pool2.chunk_count() == 0);

	pool3.hard_deallocate(p);
	CHECK(pool3.total_allocated_bytes() == 0);
}

static void test_fragmentation_healing()
{
	list_memory_pool pool;

	// 制造碎片: 分配 10 个, 硬删除偶数索引
	dense<void*> ptrs;
	ptrs.increase_capacity(10);
	for (size_t i = 0; i < 10; ++i)
	{
		ptrs[i] = pool.allocate(64);
	}
	for (size_t i = 0; i < 10; i += 2)
	{
		pool.hard_deallocate(ptrs[i]);
	}

	// 再分配 5 个, 应复用空洞 (碎片自愈)
	for (size_t i = 0; i < 5; ++i)
	{
		void* p = pool.allocate(64);
		CHECK(p != nullptr);
	}

	// 全部硬删除
	for (size_t i = 1; i < 10; i += 2)
	{
		pool.hard_deallocate(ptrs[i]);
	}
	// 新分配的 5 个仍在池中, 逐个释放
	// (无法直接引用, 通过全空判断)
	CHECK(pool.total_allocated_bytes() > 0);

	pool.release_all_memory();
	CHECK(pool.total_allocated_bytes() == 0);
	CHECK(pool.chunk_count() == 0);
}

static void test_stress_random()
{
	list_memory_pool pool;
	dense<void*> ptrs;
	ptrs.increase_capacity(1000);

	// 随机分配/硬删除
	size_t seed = 12345;
	size_t count = 0;
	for (size_t iter = 0; iter < 10000; ++iter)
	{
		seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
		bool do_alloc = (count == 0) || ((seed & 1) == 0);

		if (do_alloc && count < 1000)
		{
			seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
			size_t size = 16 + (seed % 8192);
			void* p = pool.allocate(size);
			CHECK(p != nullptr);
			ptrs[count] = p;
			++count;
		}
		else
		{
			--count;
			pool.hard_deallocate(ptrs[count]);
		}
	}

	// 释放剩余
	while (count > 0)
	{
		--count;
		pool.hard_deallocate(ptrs[count]);
	}

	CHECK(pool.total_allocated_bytes() == 0);
}

// === 软删除测试 ===

static void test_soft_deallocate_reuse()
{
	list_memory_pool pool;

	// 分配并软删除
	void* p1 = pool.allocate(32);
	CHECK(p1 != nullptr);
	pool.soft_deallocate(p1);
	CHECK(pool.total_allocated_bytes() == 0); // 逻辑已释放
	CHECK(pool.chunk_count() == 1);           // chunk 保留未归还

	// 再次分配同档, 应复用软删除的 slot (per-chunk LIFO pop, active chunk 命中)
	void* p2 = pool.allocate(32);
	CHECK(p2 == p1); // 复用同一 slot

	// 硬删除: chunk 全空但 next_uninit < slot_count, chunk 保留为 active
	pool.hard_deallocate(p2);
	CHECK(pool.total_allocated_bytes() == 0);
	// 重新分配走 bump (active chunk 有空间)
	void* trigger = pool.allocate(32);
	CHECK(trigger != nullptr);
	pool.hard_deallocate(trigger);
}

static void test_big_soft_deallocate_reuse()
{
	list_memory_pool pool;

	// 大块软删除
	void* big1 = pool.allocate(8192);
	CHECK(big1 != nullptr);
	pool.soft_deallocate(big1);
	CHECK(pool.big_block_count() == 1); // 大块保留
	CHECK(pool.total_allocated_bytes() == 0);

	// 再次分配相同大小, 复用软删除大块
	void* big2 = pool.allocate(8192);
	CHECK(big2 == big1); // 复用同一大块

	pool.hard_deallocate(big2);
	CHECK(pool.big_block_count() == 0);
}

static void test_soft_deallocate_preserves_chunk()
{
	list_memory_pool pool;

	// 分配多个 slot 填满 chunk
	dense<void*> ptrs;
	ptrs.increase_capacity(5);
	for (size_t i = 0; i < 5; ++i)
	{
		ptrs[i] = pool.allocate(64);
	}
	CHECK(pool.chunk_count() >= 1);

	// 软删除全部: chunk 应保留 (不归还 OS)
	for (size_t i = 0; i < 5; ++i)
	{
		pool.soft_deallocate(ptrs[i]);
	}
	CHECK(pool.total_allocated_bytes() == 0);
	CHECK(pool.chunk_count() >= 1); // 软删除: chunk 保留

	// 硬删除场景对比: 再分配再硬删除, chunk 标记回收
	for (size_t i = 0; i < 5; ++i)
	{
		ptrs[i] = pool.allocate(64);
	}
	for (size_t i = 0; i < 5; ++i)
	{
		pool.hard_deallocate(ptrs[i]);
	}
	// 硬删除: chunk 全空但 next_uninit < slot_count, chunk 保留为 active
	// 分配走 bump (active chunk 有空间)
	void* trigger = pool.allocate(64);
	CHECK(trigger != nullptr);
	pool.hard_deallocate(trigger);
	CHECK(pool.total_allocated_bytes() == 0);
}

static void test_soft_then_hard_deallocate()
{
	list_memory_pool pool;

	// 软删除后, 再对同一指针硬删除应安全 (软删除后 slot 已成为空洞)
	// 这里验证软删除 + 后续分配复用 + 硬删除的组合
	void* p1 = pool.allocate(128);
	CHECK(p1 != nullptr);
	pool.soft_deallocate(p1);

	// 复用
	void* p2 = pool.allocate(128);
	CHECK(p2 == p1);

	// 硬删除复用后的指针
	pool.hard_deallocate(p2);
	CHECK(pool.total_allocated_bytes() == 0);
}

static void test_soft_deallocate_mixed()
{
	list_memory_pool pool;

	// 混合软删除和硬删除
	void* a = pool.allocate(64);
	void* b = pool.allocate(64);
	void* c = pool.allocate(64);

	// 软删除 a, 硬删除 b (LIFO: b 在栈顶, a 在栈底)
	pool.soft_deallocate(a);
	pool.hard_deallocate(b);

	CHECK(pool.total_allocated_bytes() == 64); // 只剩 c

	// LIFO 语义: 最后释放的 b 在栈顶, 先被复用
	void* d = pool.allocate(64);
	CHECK(d == b); // LIFO pop: 复用最后释放的 slot

	pool.hard_deallocate(c);
	pool.hard_deallocate(d);
	CHECK(pool.total_allocated_bytes() == 0);
}

int main()
{
	// Windows 控制台 UTF-8
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	setvbuf(stdout, nullptr, _IONBF, 0);

	test_basic_allocate_hard_deallocate();
	test_size_classes();
	test_chunk_fill_and_release();
	test_slot_reuse();
	test_big_block();
	test_mixed_sizes();
	test_reallocate_inplace();
	test_reallocate_copy();
	test_ownership_invalid();
	test_statistics();
	test_move_semantics();
	test_fragmentation_healing();
	test_stress_random();

	// 软删除测试
	test_soft_deallocate_reuse();
	test_big_soft_deallocate_reuse();
	test_soft_deallocate_preserves_chunk();
	test_soft_then_hard_deallocate();
	test_soft_deallocate_mixed();

	printf("========================================\n");
	printf("list_memory_pool 功能测试: %d 通过, %d 失败\n", g_pass, g_fail);
	printf("========================================\n");

	return g_fail == 0 ? 0 : 1;
}
