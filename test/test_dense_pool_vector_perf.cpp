// test_dense_pool_vector_perf.cpp - dense vs class_pool vs std::vector 三容器性能对比
// 覆盖: 构造/访问/追加/插入/删除/批量/查找
// class_pool 使用 dense 模式 (无空洞) 以保证公平对比
#include "perf_common.hpp"
#include "include/part/dense.hpp"
#include "include/part/class_pool.hpp"
#include <vector>
#include <algorithm>
#include <cstdio>
#include <fstream>

using namespace std;

// === 测试组件 ===
struct POD128 { float a[32]; };

inline bool operator==(const POD4& a, const POD4& b) noexcept { return a.v == b.v; }
inline bool operator!=(const POD4& a, const POD4& b) noexcept { return a.v != b.v; }
inline bool operator<(const POD4& a, const POD4& b) noexcept { return a.v < b.v; }

inline bool operator==(const POD32& a, const POD32& b) noexcept
{
	for (int i = 0; i < 8; ++i) { if (a.a[i] != b.a[i]) return false; }
	return true;
}
inline bool operator!=(const POD32& a, const POD32& b) noexcept { return !(a == b); }
inline bool operator<(const POD32& a, const POD32& b) noexcept
{
	for (int i = 0; i < 8; ++i) { if (a.a[i] != b.a[i]) return a.a[i] < b.a[i]; }
	return false;
}

inline bool operator==(const POD128& a, const POD128& b) noexcept
{
	for (int i = 0; i < 32; ++i) { if (a.a[i] != b.a[i]) return false; }
	return true;
}
inline bool operator!=(const POD128& a, const POD128& b) noexcept { return !(a == b); }
inline bool operator<(const POD128& a, const POD128& b) noexcept
{
	for (int i = 0; i < 32; ++i) { if (a.a[i] != b.a[i]) return a.a[i] < b.a[i]; }
	return false;
}

template <typename T>
static T make_value(uint32_t i) noexcept
{
	if constexpr (is_same_v<T, POD4>)  return {i};
	else if constexpr (is_same_v<T, POD12>) return {static_cast<float>(i), static_cast<float>(i + 1), static_cast<float>(i + 2)};
	else if constexpr (is_same_v<T, POD32>) { POD32 p; for (int k = 0; k < 8; ++k) p.a[k] = static_cast<float>(i + k); return p; }
	else if constexpr (is_same_v<T, POD128>) { POD128 p; for (int k = 0; k < 32; ++k) p.a[k] = static_cast<float>(i + k); return p; }
	else return T{};
}

// 全局 volatile sink
static volatile size_t g_sink = 0;

// 强类型 sink: memcpy 写入防 DCE
template <typename T>
inline void sink_write(const T& x) noexcept
{
	alignas(alignof(T)) static char buf[sizeof(T)];
	std::memcpy(buf, &x, sizeof(T));
	volatile uint8_t v = static_cast<volatile uint8_t*>(static_cast<void*>(buf))[0];
	(void)v;
}

// === 三容器对比输出 ===
inline void print_triple(const char* label, size_t n,
                          double dense_ns, double pool_ns, double vec_ns) noexcept
{
	const char* verdict_d = (dense_ns < vec_ns * 0.95) ? "[W]" : (vec_ns < dense_ns * 0.95) ? "[L]" : "[T]";
	const char* verdict_p = (pool_ns < vec_ns * 0.95) ? "[W]" : (vec_ns < pool_ns * 0.95) ? "[L]" : "[T]";
	cout << "  " << left << setw(28) << label
	     << " | dense: " << fixed << setprecision(3) << setw(9) << dense_ns
	     << " " << verdict_d
	     << " | pool:  " << setw(9) << pool_ns
	     << " " << verdict_p
	     << " | vector: " << setw(9) << vec_ns
	     << "\n";
}

// === Section 1: 构造 ===
template <typename T>
static void test_construct(size_t n)
{
	print_header(("Section 1: construct (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
	constexpr int REPEAT = 5;

	// 默认构造
	{
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; compiler_barrier(); g_sink = d.size(); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; compiler_barrier(); g_sink = p.size(); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; compiler_barrier(); g_sink = v.size(); return g_sink; });
		print_triple("default ctor", 1, d_ns, p_ns, v_ns);
	}

	// 预分配容量
	{
		volatile size_t vn = n;
		for (int w = 0; w < 3; ++w) { dense<T> dw(vn); class_pool<T> pw(vn); vector<T> vw; vw.reserve(vn); }
		double d_ns = best_ns(9, [&]() { dense<T> d(vn); compiler_barrier(); touch_ptr(d.data()); g_sink = d.capacity(); return g_sink; });
		double p_ns = best_ns(9, [&]() { class_pool<T> p(vn); compiler_barrier(); touch_ptr(p.data()); g_sink = p.capacity(); return g_sink; });
		double v_ns = best_ns(9, [&]() { vector<T> v; v.reserve(vn); compiler_barrier(); touch_ptr(v.data()); g_sink = v.capacity(); return g_sink; });
		print_triple("reserve(n)", 1, d_ns, p_ns, v_ns);
	}

	// count 个 value 副本
	{
		T val = make_value<T>(0);
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d(n, val); compiler_barrier(); sink_write<T>(d.back()); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p(n, val); compiler_barrier(); sink_write<T>(p.back()); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v(n, val); compiler_barrier(); sink_write<T>(v.back()); return g_sink; });
		print_triple("count-value ctor", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// 移动构造
	{
		dense<T> d_src(n);
		class_pool<T> p_src(n);
		vector<T> v_src(n);
		double d_ns = best_ns(REPEAT, [&]() { dense<T> tmp = std::move(d_src); dense<T> r = std::move(tmp); touch_ptr(r.data()); g_sink = r.size(); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> tmp = std::move(p_src); class_pool<T> r = std::move(tmp); touch_ptr(r.data()); g_sink = r.size(); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> tmp = std::move(v_src); vector<T> r = std::move(tmp); touch_ptr(r.data()); g_sink = r.size(); return g_sink; });
		print_triple("move ctor", 1, d_ns, p_ns, v_ns);
	}

	// 拷贝构造
	{
		dense<T> d_src(n);
		for (size_t i = 0; i < n; ++i) d_src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
		class_pool<T> p_src(n);
		for (size_t i = 0; i < n; ++i) p_src.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i)));
		vector<T> v_src(n);
		for (size_t i = 0; i < n; ++i) v_src[i] = make_value<T>(static_cast<uint32_t>(i));

		double d_ns = best_ns(REPEAT, [&]() { dense<T> d(d_src); compiler_barrier(); sink_write<T>(d.back()); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p(p_src); compiler_barrier(); sink_write<T>(p.back()); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v(v_src); compiler_barrier(); sink_write<T>(v.back()); return g_sink; });
		print_triple("copy ctor", n, d_ns / n, p_ns / n, v_ns / n);
	}

	print_footer();
}

// === Section 2: 元素访问 ===
template <typename T>
static void test_access(size_t n)
{
	print_header(("Section 2: access (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());

	dense<T> d; d.increase_capacity(n);
	class_pool<T> p; p.increase_capacity(n);
	vector<T> v; v.reserve(n);
	for (size_t i = 0; i < n; ++i) {
		T val = make_value<T>(static_cast<uint32_t>(i));
		d.push_back_unchecked(val);
		p.push_back_unchecked(val);
		v.push_back(val);
	}

	constexpr int REPEAT = 5;

	// operator[]
	{
		double d_ns = best_ns(REPEAT, [&]() { for (size_t i = 0; i < n; ++i) sink_write<T>(d[opaque(i)]); compiler_barrier(); g_sink = opaque(0); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { for (size_t i = 0; i < n; ++i) sink_write<T>(p[opaque(i)]); compiler_barrier(); g_sink = opaque(0); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { for (size_t i = 0; i < n; ++i) sink_write<T>(v[opaque(i)]); compiler_barrier(); g_sink = opaque(0); return g_sink; });
		print_triple("operator[]", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// range-for 遍历
	{
		double d_ns = best_ns(REPEAT, [&]() { for (const auto& x : d) sink_write<T>(x); compiler_barrier(); g_sink = opaque(0); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { for (const auto& x : p) sink_write<T>(x); compiler_barrier(); g_sink = opaque(0); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { for (const auto& x : v) sink_write<T>(x); compiler_barrier(); g_sink = opaque(0); return g_sink; });
		print_triple("range-for iter", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// data() 指针访问
	{
		double d_ns = best_ns(REPEAT, [&]() { T* ptr = d.data(); touch_ptr(ptr); sink_write<T>(ptr[0]); g_sink = reinterpret_cast<size_t>(ptr); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { T* ptr = p.data(); touch_ptr(ptr); sink_write<T>(ptr[0]); g_sink = reinterpret_cast<size_t>(ptr); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { T* ptr = v.data(); touch_ptr(ptr); sink_write<T>(ptr[0]); g_sink = reinterpret_cast<size_t>(ptr); return g_sink; });
		print_triple("data()", 1, d_ns, p_ns, v_ns);
	}

	print_footer();
}

// === Section 3: 追加操作 ===
template <typename T>
static void test_append(size_t n)
{
	print_header(("Section 3: append (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
	constexpr int REPEAT = 3;

	// push_back (预分配)
	{
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; d.increase_capacity(n); for (size_t i = 0; i < n; ++i) d.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(d.back()); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; p.increase_capacity(n); for (size_t i = 0; i < n; ++i) p.push_back_unchecked(make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(p.back()); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; v.reserve(n); for (size_t i = 0; i < n; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(v.back()); return g_sink; });
		print_triple("push_back (reserved)", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// push_back (自动扩容)
	{
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; for (size_t i = 0; i < n; ++i) d.push_back(make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(d.back()); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; for (size_t i = 0; i < n; ++i) p.push_back(make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(p.back()); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; for (size_t i = 0; i < n; ++i) v.push_back(make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(v.back()); return g_sink; });
		print_triple("push_back (grow)", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// emplace_back (预分配)
	{
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; d.increase_capacity(n); for (size_t i = 0; i < n; ++i) d.emplace_back_unchecked(make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(d.back()); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; p.increase_capacity(n); for (size_t i = 0; i < n; ++i) p.emplace_back_unchecked(make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(p.back()); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; v.reserve(n); for (size_t i = 0; i < n; ++i) v.emplace_back(make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(v.back()); return g_sink; });
		print_triple("emplace_back (reserved)", n, d_ns / n, p_ns / n, v_ns / n);
	}

	print_footer();
}

// === Section 4: 插入/删除 ===
template <typename T>
static void test_insert_erase(size_t n)
{
	print_header(("Section 4: insert/erase (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
	constexpr int REPEAT = 3;

	// insert(begin) 头部插入
	{
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; d.increase_capacity(n); for (size_t i = 0; i < n; ++i) d.insert(d.begin(), make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(d.back()); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; p.increase_capacity(n); for (size_t i = 0; i < n; ++i) p.insert(p.begin(), make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(p.back()); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; v.reserve(n); for (size_t i = 0; i < n; ++i) v.insert(v.begin(), make_value<T>(static_cast<uint32_t>(i))); compiler_barrier(); sink_write<T>(v.back()); return g_sink; });
		print_triple("insert(begin)", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// erase(begin) 头部删除
	{
		T val = make_value<T>(0);
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; d.increase_capacity(n, val); for (size_t i = 0; i < n; ++i) d.erase(d.begin()); compiler_barrier(); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; p.increase_capacity(n, val); for (size_t i = 0; i < n; ++i) p.erase(p.begin()); compiler_barrier(); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; v.assign(n, val); for (size_t i = 0; i < n; ++i) v.erase(v.begin()); compiler_barrier(); return g_sink; });
		print_triple("erase(begin)", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// pop_back 尾部删除
	{
		T val = make_value<T>(0);
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; d.increase_capacity(n, val); for (size_t i = 0; i < n; ++i) d.pop_back(); compiler_barrier(); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; p.increase_capacity(n, val); for (size_t i = 0; i < n; ++i) p.pop_back(); compiler_barrier(); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; v.assign(n, val); for (size_t i = 0; i < n; ++i) v.pop_back(); compiler_barrier(); return g_sink; });
		print_triple("pop_back", n, d_ns / n, p_ns / n, v_ns / n);
	}

	print_footer();
}

// === Section 5: 批量操作 ===
template <typename T>
static void test_bulk(size_t n)
{
	print_header(("Section 5: bulk (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
	constexpr int REPEAT = 3;

	// fill (n, val)
	{
		T val = make_value<T>(42);
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; d.increase_capacity(n); d.fill_bulk(val, 0, n); compiler_barrier(); sink_write<T>(d.back()); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; p.increase_capacity(n); p.fill_bulk(val, 0, n); compiler_barrier(); sink_write<T>(p.back()); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; v.resize(n); std::fill(v.begin(), v.end(), val); compiler_barrier(); sink_write<T>(v.back()); return g_sink; });
		print_triple("fill (n, val)", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// append_bulk / insert range (class_pool append_bulk 是 private, 用 push_back 循环替代)
	{
		vector<T> src(n);
		for (size_t i = 0; i < n; ++i) src[i] = make_value<T>(static_cast<uint32_t>(i));

		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; d.increase_capacity(n); d.append_bulk(src.data(), n); compiler_barrier(); sink_write<T>(d.back()); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; p.increase_capacity(n); for (size_t i = 0; i < n; ++i) p.push_back_unchecked(src[i]); compiler_barrier(); sink_write<T>(p.back()); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; v.reserve(n); v.insert(v.end(), src.begin(), src.end()); compiler_barrier(); sink_write<T>(v.back()); return g_sink; });
		print_triple("append_bulk / insert range", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// clear
	{
		T val = make_value<T>(0);
		double d_ns = best_ns(REPEAT, [&]() { dense<T> d; d.increase_capacity(n, val); d.clear(); compiler_barrier(); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> p; p.increase_capacity(n, val); p.clear(); compiler_barrier(); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> v; v.assign(n, val); v.clear(); compiler_barrier(); return g_sink; });
		print_triple("clear", 1, d_ns, p_ns, v_ns);
	}

	print_footer();
}

// === Section 6: 查找/算法 ===
template <typename T>
static void test_algo(size_t n)
{
	print_header(("Section 6: algo (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());

	dense<T> d; d.increase_capacity(n);
	class_pool<T> p; p.increase_capacity(n);
	vector<T> v; v.reserve(n);
	for (size_t i = 0; i < n; ++i) {
		T val = make_value<T>(static_cast<uint32_t>(i));
		d.push_back_unchecked(val);
		p.push_back_unchecked(val);
		v.push_back(val);
	}

	constexpr int REPEAT = 5;
	T target = make_value<T>(static_cast<uint32_t>(n / 2));

	// std::find
	{
		double d_ns = best_ns(REPEAT, [&]() { auto it = std::find(d.begin(), d.end(), target); touch_ptr(&*it); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { auto it = std::find(p.begin(), p.end(), target); touch_ptr(&*it); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { auto it = std::find(v.begin(), v.end(), target); touch_ptr(&*it); return g_sink; });
		print_triple("std::find (mid hit)", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// std::accumulate
	{
		double d_ns = best_ns(REPEAT, [&]() {
			const T* ptr = d.data(); T acc{};
			for (size_t i = 0; i < n; ++i) {
				if constexpr (is_same_v<T, POD4>) acc.v += ptr[i].v;
				else if constexpr (is_same_v<T, POD32>) { for (int k = 0; k < 8; ++k) acc.a[k] += ptr[i].a[k]; }
				else if constexpr (is_same_v<T, POD128>) { for (int k = 0; k < 32; ++k) acc.a[k] += ptr[i].a[k]; }
			}
			compiler_barrier(); sink_write<T>(acc); return g_sink;
		});
		double p_ns = best_ns(REPEAT, [&]() {
			const T* ptr = p.data(); T acc{};
			for (size_t i = 0; i < n; ++i) {
				if constexpr (is_same_v<T, POD4>) acc.v += ptr[i].v;
				else if constexpr (is_same_v<T, POD32>) { for (int k = 0; k < 8; ++k) acc.a[k] += ptr[i].a[k]; }
				else if constexpr (is_same_v<T, POD128>) { for (int k = 0; k < 32; ++k) acc.a[k] += ptr[i].a[k]; }
			}
			compiler_barrier(); sink_write<T>(acc); return g_sink;
		});
		double v_ns = best_ns(REPEAT, [&]() {
			const T* ptr = v.data(); T acc{};
			for (size_t i = 0; i < n; ++i) {
				if constexpr (is_same_v<T, POD4>) acc.v += ptr[i].v;
				else if constexpr (is_same_v<T, POD32>) { for (int k = 0; k < 8; ++k) acc.a[k] += ptr[i].a[k]; }
				else if constexpr (is_same_v<T, POD128>) { for (int k = 0; k < 32; ++k) acc.a[k] += ptr[i].a[k]; }
			}
			compiler_barrier(); sink_write<T>(acc); return g_sink;
		});
		print_triple("accumulate (sum)", n, d_ns / n, p_ns / n, v_ns / n);
	}

	// std::sort (用 data() 指针, 三容器公平)
	{
		double d_ns = best_ns(REPEAT, [&]() { dense<T> tmp(d); std::sort(tmp.data(), tmp.data() + tmp.size()); compiler_barrier(); sink_write<T>(tmp.back()); return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { class_pool<T> tmp(p); std::sort(tmp.data(), tmp.data() + tmp.size()); compiler_barrier(); sink_write<T>(tmp.back()); return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { vector<T> tmp(v); std::sort(tmp.data(), tmp.data() + tmp.size()); compiler_barrier(); sink_write<T>(tmp.back()); return g_sink; });
		print_triple("std::sort", n, d_ns / n, p_ns / n, v_ns / n);
	}

	print_footer();
}

// === Section 7: 容量管理 ===
template <typename T>
static void test_capacity(size_t n)
{
	print_header(("Section 7: capacity (T=" + to_string(sizeof(T)) + "B, N=" + to_string(n) + ")").c_str());
	constexpr int REPEAT = 5;

	// size / capacity / empty 查询
	{
		dense<T> d; d.increase_capacity(n);
		class_pool<T> p; p.increase_capacity(n);
		vector<T> v; v.reserve(n);
		const size_t OPS = 1000000;

		double d_ns = best_ns(REPEAT, [&]() { volatile size_t s = 0; for (size_t i = 0; i < OPS; ++i) { s += d.size(); s += d.capacity(); s += d.empty() ? 1 : 0; } (void)s; return g_sink; });
		double p_ns = best_ns(REPEAT, [&]() { volatile size_t s = 0; for (size_t i = 0; i < OPS; ++i) { s += p.size(); s += p.capacity(); s += p.empty() ? 1 : 0; } (void)s; return g_sink; });
		double v_ns = best_ns(REPEAT, [&]() { volatile size_t s = 0; for (size_t i = 0; i < OPS; ++i) { s += v.size(); s += v.capacity(); s += v.empty() ? 1 : 0; } (void)s; return g_sink; });
		print_triple("size/cap/empty", OPS, d_ns / OPS, p_ns / OPS, v_ns / OPS);
	}

	print_footer();
}

// === 主函数 ===
int main()
{
	// 直接用 ofstream 写文件, 规避 stdout 重定向在 PowerShell 下的缓冲问题
	static std::ofstream perf_out("triple_perf_result.txt");
	std::cout.rdbuf(perf_out.rdbuf());
	cout << "============================================================\n";
	cout << "  dense vs class_pool vs std::vector 三容器性能对比\n";
	cout << "  class_pool 使用 dense 模式 (无空洞) 公平对比\n";
	cout << "  编译: -O2 + AVX2 (MinGW 仅 SSE2 + 函数级 AVX2)\n";
	cout << "  判定: [W]=赢  [L]=输  [T]=平手 (vs vector, 阈值 5%)\n";
	cout << "============================================================\n";

	const size_t N = 1 << 16;  // 64K (insert/erase 为 O(N^2), 64K 已足够且可控)

	test_construct<POD4>(N);
	test_construct<POD32>(N);
	test_construct<POD128>(N);

	test_access<POD4>(N);
	test_access<POD32>(N);
	test_access<POD128>(N);

	test_append<POD4>(N);
	test_append<POD32>(N);
	test_append<POD128>(N);

	test_insert_erase<POD4>(N);
	test_insert_erase<POD32>(N);

	test_bulk<POD4>(N);
	test_bulk<POD32>(N);
	test_bulk<POD128>(N);

	test_algo<POD4>(N);
	test_algo<POD32>(N);

	test_capacity<POD4>(N);

	cout << "\n============================================================\n";
	cout << "  测试完成\n";
	cout << "============================================================\n";
	return 0;
}
