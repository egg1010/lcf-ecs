#include "include/component.hpp"
#include "include/void_any.hpp"
#include "include/memory_pool.hpp"
#include "include/class_pool.hpp"
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// ============================================================
// 测试组件定义
// ============================================================
struct Position {
    float x, y, z;
    Position(float x = 0.0f, float y = 0.0f, float z = 0.0f) : x(x), y(y), z(z) {}
};
struct Velocity {
    float vx, vy, vz;
    Velocity(float vx = 0.0f, float vy = 0.0f, float vz = 0.0f) : vx(vx), vy(vy), vz(vz) {}
};
struct Health {
    int current, max;
    Health(int current = 100, int max = 100) : current(current), max(max) {}
};
struct Name {
    std::string value;
    Name(const std::string& name = "Entity") : value(name) {}
};
struct Damage {
    int amount;
    Damage(int amount = 0) : amount(amount) {}
};
struct Armor {
    int defense;
    Armor(int defense = 0) : defense(defense) {}
};
struct Speed {
    float value;
    Speed(float value = 0.0f) : value(value) {}
};

// ============================================================
// 辅助工具
// ============================================================
class Timer {
    std::chrono::high_resolution_clock::time_point start_time_;
public:
    Timer() { reset(); }
    void reset() { start_time_ = std::chrono::high_resolution_clock::now(); }
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - start_time_).count();
    }
};

static constexpr int COL1 = 42;  // 接口名列宽
static constexpr int COL2 = 12;  // 结果列宽

void print_section(int num, const char* title) {
    std::cout << "\n" << std::string(56, '=') << "\n"
              << "  " << num << ". " << title << "\n"
              << std::string(56, '=') << "\n";
}

void print_sub(const char* title) {
    std::cout << "\n  [ " << title << " ]\n";
}

void print_item(const char* name, const std::string& result) {
    std::cout << "  " << std::left << std::setw(COL1) << name
              << ": " << std::right << result << "\n";
}

void print_item(const char* name, bool pass) {
    std::cout << "  " << std::left << std::setw(COL1) << name
              << ": " << (pass ? "通过" : "失败") << "\n";
}

void print_item(const char* name, const char* result) {
    std::cout << "  " << std::left << std::setw(COL1) << name
              << ": " << result << "\n";
}

void print_perf(const std::string& op, size_t count, double ms) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  " << std::left << std::setw(30) << op
              << ": " << std::right << std::setw(8) << count
              << " 次  " << std::setw(8) << ms << " 毫秒 ("
              << std::setw(10) << (static_cast<double>(count) / ms) << " 次/毫秒, "
              << std::setw(12) << (static_cast<double>(count) / (ms / 1000.0)) << " 次/秒)\n";
}

void print_perf_sub(const char* title) {
    std::cout << "\n  ┌─ " << title << "\n";
}

void print_perf_sep() {
    std::cout << "  ├──────────────────────────────────────────\n";
}

// ============================================================
// 主测试
// ============================================================
int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // ========================================================
    // 1. entity 实体
    // ========================================================
    print_section(1, "entity 实体");
    {
        entity e_def;
        print_item("默认构造 entity()", !e_def.is_valid());

        entity e_params(3, 1);
        std::ostringstream oss;
        oss << "index=" << e_params.parts_.index_ << " version=" << e_params.parts_.version_;
        print_item("带参构造 entity(3,1)", oss.str());

        print_item("is_valid() 有效实体", e_params.is_valid());
        print_item("is_valid() 无效实体", !e_def.is_valid());

        entity e1(5, 2), e2(5, 2), e3(5, 3);
        print_item("operator== 相等", (e1 == e2));
        print_item("operator!= 不等", (e1 != e3));

        oss.str(""); oss << "handle=0x" << std::hex << e_params.handle_;
        print_item("handle_ 字段", oss.str());

        size_t h1 = std::hash<entity>()(e1);
        size_t h2 = std::hash<entity>()(e2);
        print_item("std::hash 相同实体哈希一致", h1 == h2);
    }

    // ========================================================
    // 2. operating_message 操作消息
    // ========================================================
    print_section(2, "operating_message 操作消息");
    {
        bool& dbg = ecs_debug_messages();
        bool old_dbg = dbg;
        dbg = true;

        print_item("ecs_debug_messages() 返回引用", true);

        operating_message om;
        print_item("默认构造 operator bool()", (bool)om);

        om.set_switch_bool(false);
        print_item("set_switch_bool(false) operator bool()", !(bool)om);

        om.reset();
        print_item("reset() 恢复为 true", (bool)om);

        om.write_message(false, "错误消息");
        print_item("write_message(false,...) switch变false", !om.get_switch_bool());

        om.reset();
        om.write_message(false, "测试");
        bool sw_before = om.get_switch_bool();
        om.clear_message();
        bool sw_after = om.get_switch_bool();
        print_item("clear_message() 保留switch状态", (sw_before == false && sw_after == false && om.read_message().empty()));

        om.reset();
        om.set_switch_bool(true);
        print_item("set_switch_bool(true)", om.get_switch_bool());

        om.set_switch_bool(false);
        bool& sw_ref = om.get_switch_bool();
        sw_ref = true;
        print_item("get_switch_bool() 返回引用", om.get_switch_bool());

        const operating_message& com = om;
        const bool& csw = com.get_switch_bool();
        print_item("get_switch_bool() const", csw);

        om.reset();
        om += "hello ";
        om += "world";
        print_item("operator+=(string_view)", om.read_message() == "hello world");

        operating_message om2;
        om2.write_message(true, "om2消息");
        om += std::move(om2);
        print_item("operator+=(operating_message&&)", om.read_message().find("om2消息") != std::string::npos);

        operating_message om3;
        om3.write_message(true, "om3消息");
        om += om3;
        print_item("operator+=(const operating_message&)", om.read_message().find("om3消息") != std::string::npos);

        std::ostringstream os_om;
        om.reset();
        om += "流输出测试";
        os_om << om;
        print_item("operator<<", os_om.str() == "流输出测试");

        om.reset();
        om.write_message(true, "正常消息");
        print_item("write_message(true,...)", (bool)om && om.read_message().find("正常消息") != std::string::npos);

        om.reset();
        om.write_message_fmt(true, "格式化: {} + {} = {}", 1, 2, 3);
        print_item("write_message_fmt()", om.read_message().find("格式化: 1 + 2 = 3") != std::string::npos);

        // 拷贝构造
        operating_message om_src;
        om_src.write_message(true, "拷贝源");
        operating_message om_copy(om_src);
        print_item("拷贝构造", om_copy.read_message() == om_src.read_message());

        // 拷贝赋值
        operating_message om_assign;
        om_assign = om_src;
        print_item("拷贝赋值", om_assign.read_message() == om_src.read_message());

        // 移动构造
        operating_message om_move_src;
        om_move_src.write_message(true, "移动源");
        std::string saved_msg(om_move_src.read_message());
        operating_message om_move_dst(std::move(om_move_src));
        print_item("移动构造", om_move_dst.read_message() == saved_msg);

        // 移动赋值
        operating_message om_moveassign_src;
        om_moveassign_src.write_message(true, "移动赋值源");
        saved_msg = std::string(om_moveassign_src.read_message());
        operating_message om_moveassign_dst;
        om_moveassign_dst = std::move(om_moveassign_src);
        print_item("移动赋值", om_moveassign_dst.read_message() == saved_msg);

        dbg = old_dbg;
    }

    // ========================================================
    // 3. id_allocation<T> ID分配器
    // ========================================================
    print_section(3, "id_allocation<T> ID分配器");
    {
        id_allocation<int> ida;
        int id1 = ida.get_id();
        int id2 = ida.get_id();
        int id3 = ida.get_id();
        print_item("get_id() 递增分配", (id1 == 1 && id2 == 2 && id3 == 3));

        ida.free_id(id2);
        print_item("free_id(2)", true);

        int id4 = ida.get_id();
        print_item("get_id() 回收再分配", id4 == 2);

        std::ostringstream oss;
        oss << "total=" << ida.total_number_of_ids() << " max=" << ida.maximum_id();
        print_item("total_number_of_ids()/maximum_id()", oss.str());
    }

    // ========================================================
    // 4. type_id 类型ID
    // ========================================================
    print_section(4, "type_id 类型ID");
    {
        int id_pos = type_id::get_type_id<Position>();
        int id_vel = type_id::get_type_id<Velocity>();
        print_item("get_type_id<Position>()", std::to_string(id_pos));
        print_item("get_type_id<Velocity>()", std::to_string(id_vel));
        print_item("不同类型ID不同", id_pos != id_vel);
        print_item("相同类型ID一致", type_id::get_type_id<Position>() == id_pos);
    }

    // ========================================================
    // 5. class_pool<T> 组件池
    // ========================================================
    print_section(5, "class_pool<T> 组件池");

    // --- 构造函数 ---
    std::cout << "\n  [构造函数]\n";
    {
        class_pool<int> cp_def;
        print_item("默认构造", cp_def.empty());

        class_pool<int> cp_cap(64);
        print_item("class_pool(size_t capacity)", cp_cap.capacity() >= 64);

        class_pool<int> cp_fill(static_cast<size_t>(5), 42);
        print_item("class_pool(count, value)", (cp_fill.size() == 5 && cp_fill[0] == 42 && cp_fill[4] == 42));

        class_pool<int> vec = {10, 20, 30};
        class_pool<int> cp_it(vec.begin(), vec.end());
        print_item("class_pool(InputIt, InputIt)", (cp_it.size() == 3 && cp_it[0] == 10 && cp_it[2] == 30));

        class_pool<int> cp_init = {100, 200, 300};
        print_item("class_pool(initializer_list)", (cp_init.size() == 3 && cp_init[1] == 200));

        class_pool<int> cp_copy(cp_init);
        cp_init[0] = 999;
        print_item("拷贝构造 深拷贝", (cp_copy.size() == 3 && cp_copy[0] == 100));

        class_pool<int> cp_move_src = {7, 8, 9};
        class_pool<int> cp_move_dst(std::move(cp_move_src));
        print_item("移动构造", (cp_move_dst.size() == 3 && cp_move_dst[0] == 7));
    }

    // --- 赋值 ---
    std::cout << "\n  [赋值]\n";
    {
        class_pool<int> a = {1, 2, 3};
        class_pool<int> b;
        b = a;
        a[0] = 999;
        print_item("拷贝赋值 深拷贝", (b[0] == 1 && b.size() == 3));

        class_pool<int> c;
        class_pool<int> d = {5, 6};
        c = std::move(d);
        print_item("移动赋值", (c.size() == 2 && c[0] == 5));
    }

    // --- 元素访问 ---
    std::cout << "\n  [元素访问]\n";
    {
        class_pool<int> cp = {10, 20, 30, 40, 50};
        print_item("operator[]", cp[2] == 30);
        print_item("at()", cp.at(2) == 30);
        print_item("front()", cp.front() == 10);
        print_item("back()", cp.back() == 50);

        int* p = cp.get(2);
        print_item("get()", (p != nullptr && *p == 30));
        print_item("data()", (cp.data() != nullptr && cp.data()[0] == 10));

        std::span<int> sp = cp.span();
        print_item("span()", (sp.size() == 5 && sp[2] == 30));

        const class_pool<int>& ccp = cp;
        std::span<const int> csp = ccp.span();
        print_item("span() const", (csp.size() == 5 && csp[2] == 30));
    }

    // --- 容量 ---
    std::cout << "\n  [容量]\n";
    {
        class_pool<int> cp = {1, 2, 3};
        print_item("size()", cp.size() == 3);
        print_item("capacity()", cp.capacity() >= 3);
        print_item("sparse_capacity()", cp.sparse_capacity() >= 3);
        print_item("empty() 非空", !cp.empty());
        print_item("count()", cp.count() == 3);
        print_item("valid()", cp.valid());

        class_pool<int> empty_cp;
        print_item("empty() 空池", empty_cp.empty());
        print_item("valid() 空池", !empty_cp.valid());

        std::ostringstream oss;
        oss << cp.size_bytes() << "/" << cp.capacity_bytes();
        print_item("size_bytes()/capacity_bytes()", oss.str());
    }

    // --- 修改器 ---
    std::cout << "\n  [修改器]\n";
    {
        class_pool<int> cp;
        cp.emplace_back(42);
        cp.emplace_back(99);
        print_item("emplace_back()", (cp.size() == 2 && cp.back() == 99));

        cp.clear();
        print_item("clear()", (cp.size() == 0 && cp.empty()));

        cp.increase_capacity(1000);
        print_item("increase_capacity(1000)", cp.capacity() >= 1000);

        class_pool<int> cp2 = {1, 2, 3, 4, 5, 6, 7, 8};
        cp2.shrink_to_fit();
        print_item("shrink_to_fit()", cp2.capacity() == cp2.size());

        class_pool<int> cp3;
        cp3.resize(100);
        print_item("resize(size_t) 仅扩容", cp3.capacity() >= 100);

        class_pool<int> cp4;
        cp4.resize(static_cast<size_t>(5), 77);
        print_item("resize(size_t, value)", (cp4.size() == 5 && cp4[0] == 77 && cp4[4] == 77));

        // increase_capacity(size_t, const T&) 扩容并填充值
        class_pool<int> cp_fill;
        cp_fill.emplace_back(1);
        cp_fill.emplace_back(2);
        cp_fill.increase_capacity(static_cast<size_t>(5), 99);
        print_item("increase_capacity(cap, value)", (cp_fill.size() == 5 && cp_fill[0] == 1 && cp_fill[1] == 2 && cp_fill[2] == 99 && cp_fill[4] == 99));

        // reduce_capacity(size_t) 缩容
        class_pool<int> cp_shrink = {10, 20, 30, 40, 50, 60, 70, 80};
        size_t cap_before = cp_shrink.capacity();
        cp_shrink.reduce_capacity(3);
        print_item("reduce_capacity(cap) 截断", (cp_shrink.size() == 3 && cp_shrink[0] == 10 && cp_shrink[2] == 30 && cp_shrink.capacity() < cap_before));

        // reduce_capacity(size_t, class_pool<T>&) 缩容并迁移元素
        class_pool<int> cp_src = {10, 20, 30, 40, 50};
        class_pool<int> cp_dst;
        cp_src.reduce_capacity(static_cast<size_t>(2), cp_dst);
        print_item("reduce_capacity(cap, dst)", (cp_src.size() == 2 && cp_src[0] == 10 && cp_src[1] == 20 && cp_dst.size() == 3 && cp_dst[0] == 30 && cp_dst[2] == 50));

        class_pool<int> cp5 = {10, 20, 30, 40, 50};
        cp5.emplace(std::next(cp5.begin(), 2), 25);
        print_item("emplace()", (cp5.size() == 6 && cp5[2] == 25 && cp5[3] == 30));

        cp5.erase(std::next(cp5.begin(), 2));
        print_item("erase()", (cp5.size() == 5 && cp5[2] == 30));

        class_pool<int> cp6 = {1, 2, 3};
        class_pool<int> cp7 = {10, 20};
        cp6.swap(cp7);
        print_item("swap()", (cp6.size() == 2 && cp6[0] == 10 && cp7.size() == 3 && cp7[0] == 1));

        class_pool<int> cp8 = {1, 2, 3, 4, 5};
        cp8.pop_back();
        print_item("pop_back()", (cp8.size() == 4 && cp8.back() == 4));

        class_pool<int> cp9;
        cp9.emplace_at(5, 55);
        print_item("emplace_at()", (cp9.size() == 6 && cp9[5] == 55));

        class_pool<int> cp10;
        cp10.sparse_emplace_at(3, 33);
        cp10.sparse_emplace_at(3, 99);
        print_item("sparse_emplace_at()", (cp10[3] == 99));

        class_pool<int> cp11;
        cp11.emplace_back(1);
        cp11.emplace_back(2);
        cp11.emplace_back(3);
        cp11.sparse_erase_at(1);
        print_item("sparse_erase_at()", (!cp11.is_constructed_at(1) && cp11.is_constructed_at(0) && cp11.is_constructed_at(2)));
    }

    // --- 稀疏/位图 ---
    std::cout << "\n  [稀疏/位图]\n";
    {
        class_pool<int> cp;
        cp.emplace_back(1);
        cp.emplace_back(2);
        cp.emplace_back(3);
        print_item("is_constructed_at()", (cp.is_constructed_at(0) && cp.is_constructed_at(2)));

        print_item("is_dense() 密集", cp.is_dense());

        cp.sparse_erase_at(1);
        print_item("is_dense() 有空洞", !cp.is_dense());

        size_t cnt_before = cp.count();
        cp.invalidate_count_cache();
        size_t cnt_after = cp.count();
        print_item("invalidate_count_cache()", cnt_before == cnt_after);

        int sum = 0;
        for (int& v : cp) sum += v;
        print_item("range-for (sparse)", sum == 4);

        const class_pool<int>& ccp = cp;
        int csum = 0;
        for (const int& v : ccp) csum += v;
        print_item("range-for (const sparse)", csum == 4);
    }

    // --- 迭代器 ---
    std::cout << "\n  [迭代器]\n";
    {
        class_pool<int> cp = {10, 20, 30};
        int fwd = 0;
        for (auto it = cp.begin(); it != cp.end(); ++it) fwd += *it;
        print_item("begin/end", fwd == 60);

        int cfwd = 0;
        for (auto it = cp.cbegin(); it != cp.cend(); ++it) cfwd += *it;
        print_item("cbegin/cend", cfwd == 60);
    }

    // --- 自由函数 ---
    std::cout << "\n  [自由函数]\n";
    {
        class_pool<int> a = {1, 2}, b = {3, 4, 5};
        swap(a, b);
        print_item("swap(class_pool&, class_pool&)", (a.size() == 3 && a[0] == 3 && b.size() == 2 && b[0] == 1));
    }

    // ========================================================
    // 6. void_any 类型擦除容器
    // ========================================================
    print_section(6, "void_any 类型擦除容器");
    {
        void_any va_def;
        print_item("默认构造", !va_def.has_value());

        void_any va_int(42);
        print_item("构造 void_any(T&&)", va_int.has_value());

        void_any va_copy_obj(va_int);
        print_item("拷贝构造", (va_copy_obj.has_value() && va_copy_obj.get_ptr<int>() && *va_copy_obj.get_ptr<int>() == 42));

        void_any va_move_src(100);
        void_any va_move_dst(std::move(va_move_src));
        print_item("移动构造", (va_move_dst.has_value() && va_move_dst.get_ptr<int>() && *va_move_dst.get_ptr<int>() == 100));

        void_any va1;
        va1.set(3.14);
        print_item("set()", (va1.has_value() && va1.get_ptr<double>() && *va1.get_ptr<double>() == 3.14));

        print_item("type_id()", va1.type_id() == type_id::get_type_id<double>());

        double* p = va1.get_ptr<double>();
        print_item("get_ptr()", (p && *p == 3.14));

        const void_any& cva1 = va1;
        const double* cp = cva1.get_ptr<double>();
        print_item("get_ptr() const", (cp && *cp == 3.14));

        double* fp = va1.fast_get_ptr<double>();
        print_item("fast_get_ptr()", (fp && *fp == 3.14));

        const double* cfp = cva1.fast_get_ptr<double>();
        print_item("fast_get_ptr() const", (cfp && *cfp == 3.14));

        double* up = va1.get_ptr_unchecked<double>();
        print_item("get_ptr_unchecked()", (up && *up == 3.14));

        const double* cup = cva1.get_ptr_unchecked<double>();
        print_item("get_ptr_unchecked() const", (cup && *cup == 3.14));

        void_any va_get(77);
        int val = va_get.get<int>();
        print_item("get()", val == 77);

        print_item("has_value()", va_get.has_value());

        va_get.reset();
        print_item("reset()", !va_get.has_value());

        void_any va_asn1, va_asn2(55);
        va_asn1 = va_asn2;
        print_item("拷贝赋值", (va_asn1.has_value() && *va_asn1.get_ptr<int>() == 55));

        void_any va_asn3;
        va_asn3 = void_any(66);
        print_item("移动赋值", (va_asn3.has_value() && *va_asn3.get_ptr<int>() == 66));

        // 类型不匹配返回 nullptr
        void_any va_wrong(42);
        print_item("get_ptr 类型不匹配", va_wrong.get_ptr<double>() == nullptr);
    }

    // ========================================================
    // 7. memory_pool 内存池
    // ========================================================
    print_section(7, "memory_pool 内存池");

    // --- memory_block ---
    std::cout << "\n  [memory_block]\n";
    {
        memory_block mb_def;
        print_item("默认构造", (mb_def.data_ == nullptr && mb_def.size_ == 0));

        memory_block mb1(static_cast<uint8_t*>(::operator new(64)), 64);
        print_item("带参构造(data,size)", (mb1.data_ != nullptr && mb1.size_ == 64));

        memory_block mb2(std::move(mb1));
        print_item("移动构造", (mb2.data_ != nullptr && mb1.data_ == nullptr));

        memory_block mb3;
        mb3 = std::move(mb2);
        print_item("移动赋值", (mb3.data_ != nullptr && mb2.data_ == nullptr));
    }

    // --- memory_pool ---
    std::cout << "\n  [memory_pool]\n";
    {
        memory_pool mp(4096);
        print_item("构造 memory_pool(chunk_size)", mp.chunk_size() == 4096);

        void* p1 = mp.allocate(128);
        print_item("allocate(128)", p1 != nullptr);

        mp.deallocate(p1);
        print_item("deallocate()", mp.empty());

        int* obj = mp.construct<int>(42);
        print_item("construct<int>(42)", (obj != nullptr && *obj == 42));

        mp.destroy(obj);
        print_item("destroy<int>()", true);

        mp.deallocate(mp.allocate(64));
        std::ostringstream oss;
        oss << "alloc=" << mp.total_allocated() << " used=" << mp.total_used();
        print_item("total_allocated()/total_used()", oss.str());

        print_item("chunk_size()", mp.chunk_size() == 4096);

        mp.deallocate(mp.allocate(32));
        print_item("empty() 释放后", mp.empty());

        mp.increase_capacity(8192);
        print_item("increase_capacity(8192)", mp.total_allocated() >= 8192);

        mp.reduce_capacity(0);
        print_item("reduce_capacity(0)", (mp.total_allocated() == 0 && mp.total_used() == 0));

        // 移动构造
        memory_pool mp2(2048);
        mp2.deallocate(mp2.allocate(64));
        memory_pool mp3(std::move(mp2));
        print_item("移动构造", mp3.chunk_size() == 2048);

        // 移动赋值
        memory_pool mp4, mp5(1024);
        mp4 = std::move(mp5);
        print_item("移动赋值", mp4.chunk_size() == 1024);
    }

    // ========================================================
    // 8. single_class_set 单类集合
    // ========================================================
    print_section(8, "single_class_set 单类集合");

    // --- sparse ---
    std::cout << "\n  [sparse SOA]\n";
    {
        single_class_set scs;
        print_item("sparse_combined_ empty", scs.get_sparse_combined().empty());
    }

    // --- 构造函数 ---
    std::cout << "\n  [构造函数]\n";
    {
        single_class_set scs_def;
        print_item("默认构造", scs_def.empty());

        single_class_set scs_res(256);
        entity e1(0, 1);
        scs_res.add(e1, Position{1, 2, 3});
        print_item("single_class_set(size_t) 预留构造", (scs_res.get_ptr<Position>(e1) != nullptr));

        single_class_set scs_ent(entity(10, 1), Position{5, 6, 7});
        print_item("single_class_set(entity, T&&)", (scs_ent.get_ptr<Position>(entity(10, 1)) != nullptr));

        single_class_set scs_mv_src(entity(0, 1), Velocity{1, 0, 0});
        single_class_set scs_mv_dst(std::move(scs_mv_src));
        print_item("移动构造", (scs_mv_dst.get_ptr<Velocity>(entity(0, 1)) != nullptr));

        single_class_set scs_ma_src(entity(0, 1), Health{50, 100});
        single_class_set scs_ma_dst;
        scs_ma_dst = std::move(scs_ma_src);
        print_item("移动赋值", (scs_ma_dst.get_ptr<Health>(entity(0, 1)) != nullptr));
    }

    // --- 添加 ---
    std::cout << "\n  [添加]\n";
    {
        single_class_set scs;
        entity e1(0, 1);
        auto msg = scs.add(e1, Position{10, 20, 30});
        print_item("add()", (bool)msg && scs.get_ptr<Position>(e1) != nullptr);

        entity ents[3] = { entity(10, 1), entity(11, 1), entity(12, 1) };
        Position comps[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
        single_class_set scs2;
        scs2.add_batch(std::span<const entity>(ents, 3), std::span<const Position>(comps, 3));
        print_item("add_batch(span)", scs2.size() == 3);

        class_pool<entity> ent_pool;
        class_pool<Position> pos_pool;
        for (int i = 0; i < 3; ++i) {
            ent_pool.emplace_back(entity(20 + i, 1));
            pos_pool.emplace_back(static_cast<float>(i), 0, 0);
        }
        single_class_set scs3;
        scs3.add_batch(ent_pool, pos_pool);
        print_item("add_batch(class_pool&)", scs3.size() == 3);

        single_class_set scs4;
        scs4.add_batch(std::move(ent_pool), std::move(pos_pool));
        print_item("add_batch(class_pool&&)", scs4.size() == 3);
    }

    // --- 获取 ---
    std::cout << "\n  [获取]\n";
    {
        single_class_set scs;
        entity e1(0, 1);
        scs.add(e1, Position{10, 20, 30});

        Position* p = scs.get_ptr<Position>(e1);
        print_item("get_ptr()", (p && p->x == 10));

        const single_class_set& cscs = scs;
        const Position* cp = cscs.get_ptr<Position>(e1);
        print_item("get_ptr() const", (cp && cp->x == 10));

        Position* fp = scs.get_ptr_fast<Position>(e1);
        print_item("get_ptr_fast()", (fp && fp->x == 10));

        const Position* cfp = cscs.get_ptr_fast<Position>(e1);
        print_item("get_ptr_fast() const", (cfp && cfp->x == 10));

        print_item("get_version()", scs.get_version(0) == 1);
        print_item("get_version_unchecked()", scs.get_version_unchecked(0) == 1);
    }

    // --- 删除 ---
    std::cout << "\n  [删除]\n";
    {
        single_class_set scs;
        entity e1(0, 1), e2(1, 1);
        scs.add(e1, Position{1, 0, 0});
        scs.add(e2, Position{2, 0, 0});

        scs.hard_remove(e1);
        print_item("hard_remove()", (scs.get_ptr<Position>(e1) == nullptr && scs.get_ptr<Position>(e2) != nullptr));

        scs.soft_remove(e2);
        print_item("soft_remove()", scs.get_ptr<Position>(e2) == nullptr);

        scs.add(e2, Position{3, 0, 0});
        scs.clear();
        print_item("clear()", scs.empty());
    }

    // --- 其他 ---
    std::cout << "\n  [其他]\n";
    {
        single_class_set scs;
        print_item("get_type_id() 初始", scs.get_type_id() == -1);

        scs.add(entity(0, 1), Health{50, 100});
        print_item("get_type_id() 添加后", scs.get_type_id() == type_id::get_type_id<Health>());

        class_pool<Health>* tpp = scs.get_typed_pool_ptr<Health>();
        print_item("get_typed_pool_ptr()", (tpp && tpp->size() == 1));

        const single_class_set& cscs = scs;
        const class_pool<Health>* ctpp = cscs.get_typed_pool_ptr<Health>();
        print_item("get_typed_pool_ptr() const", (ctpp && ctpp->size() == 1));

        print_item("get_operating_message()", (bool)scs.get_operating_message());

        print_item("size()", scs.size() == 1);
        print_item("empty()", !scs.empty());

        scs.increase_capacity(1024);
        print_item("increase_capacity(1024)", true);

        auto& ei = scs.get_entity_indices();
        print_item("get_entity_indices()", ei.size() == 1);

        const auto& cei = cscs.get_entity_indices();
        print_item("get_entity_indices() const", cei.size() == 1);
    }

    // ========================================================
    // 9. ecs::manager ECS管理器
    // ========================================================
    print_section(9, "ecs::manager ECS管理器");

    // --- 构造 ---
    std::cout << "\n  [构造]\n";
    {
        ecs::manager mgr_def;
        print_item("默认构造", true);

        ecs::manager mgr_mv_src;
        ecs::manager mgr_mv_dst(std::move(mgr_mv_src));
        print_item("移动构造", true);

        ecs::manager mgr_ma_src, mgr_ma_dst;
        mgr_ma_dst = std::move(mgr_ma_src);
        print_item("移动赋值", true);
    }

    // --- 实体管理 ---
    std::cout << "\n  [实体管理]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(10);
        print_item("append_preallocated_entities(10)", true);

        entity e1 = mgr.create_entity();
        entity e2 = mgr.create_entity();
        print_item("create_entity()", (e1.is_valid() && e2.is_valid() && e1 != e2));

        print_item("is_entity_valid() 有效", mgr.is_entity_valid(e1));

        mgr.delete_entity(e1);
        print_item("delete_entity()", !mgr.is_entity_valid(e1));
    }

    // --- 添加组件 ---
    std::cout << "\n  [添加组件]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();

        auto msg1 = mgr.add(e1, Position{1, 2, 3});
        print_item("add(e, T)", (bool)msg1 && mgr.get_ptr<Position>(e1) != nullptr);

        auto msg2 = mgr.add(Velocity{4, 5, 6}, e2);
        print_item("add(T, e)", (bool)msg2 && mgr.get_ptr<Velocity>(e2) != nullptr);

        auto& ref1 = mgr.addc(e1, Health{80, 100});
        print_item("addc(e, T) 返回引用", &ref1 == &mgr);

        auto& ref2 = mgr.addc(Name{"测试"}, e2);
        print_item("addc(T, e) 返回引用", &ref2 == &mgr);

        // add_batch(span)
        ecs::manager mgr2;
        mgr2.append_preallocated_entities(5);
        class_pool<entity> ents;
        class_pool<Position> comps;
        for (size_t i = 0; i < 5; ++i) {
            ents.emplace_back(mgr2.create_entity());
            comps.emplace_back(static_cast<float>(i), 0, 0);
        }
        mgr2.add_batch(std::span<const entity>(ents.data(), ents.size()), std::span<const Position>(comps.data(), comps.size()));
        size_t cnt = 0;
        mgr2.view<Position>().for_each([&cnt](Position&) { cnt++; });
        print_item("add_batch(span)", cnt == 5);

        // add_batch(class_pool&)
        ecs::manager mgr3;
        mgr3.append_preallocated_entities(3);
        class_pool<entity> ents3;
        class_pool<Health> hps3;
        for (size_t i = 0; i < 3; ++i) {
            ents3.emplace_back(mgr3.create_entity());
            hps3.emplace_back(static_cast<int>(i * 10), 100);
        }
        mgr3.add_batch(ents3, hps3);
        print_item("add_batch(class_pool&)", mgr3.get_ptr<Health>(ents3[0]) != nullptr);

        // add_batch(class_pool&&)
        ecs::manager mgr4;
        mgr4.append_preallocated_entities(3);
        class_pool<entity> ents4;
        class_pool<Velocity> vels4;
        for (size_t i = 0; i < 3; ++i) {
            ents4.emplace_back(mgr4.create_entity());
            vels4.emplace_back(static_cast<float>(i), 0, 0);
        }
        mgr4.add_batch(std::move(ents4), std::move(vels4));
        size_t vcnt = 0;
        mgr4.view<Velocity>().for_each([&vcnt](Velocity&) { vcnt++; });
        print_item("add_batch(class_pool&&)", vcnt == 3);
    }

    // --- 获取组件 ---
    std::cout << "\n  [获取组件]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(5);
        auto e = mgr.create_entity();
        mgr.add(e, Position{10, 20, 30});

        Position* p = mgr.get_ptr<Position>(e);
        print_item("get_ptr()", (p && p->x == 10));

        const ecs::manager& cmgr = mgr;
        const Position* cp = cmgr.get_ptr<Position>(e);
        print_item("get_ptr() const", (cp && cp->x == 10));

        Position* fp = mgr.get_ptr_fast<Position>(e);
        print_item("get_ptr_fast()", (fp && fp->x == 10));

        const Position* cfp = cmgr.get_ptr_fast<Position>(e);
        print_item("get_ptr_fast() const", (cfp && cfp->x == 10));
    }

    // --- 删除组件 ---
    std::cout << "\n  [删除组件]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(5);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});

        mgr.soft_remove<Position>(e1);
        print_item("soft_remove()", mgr.get_ptr<Position>(e1) == nullptr);

        mgr.hard_remove<Position>(e2);
        print_item("hard_remove()", mgr.get_ptr<Position>(e2) == nullptr);

        auto e3 = mgr.create_entity();
        mgr.add(e3, Health{50, 100});
        mgr.hard_removec<Health>(e3);
        print_item("hard_removec()", mgr.get_ptr<Health>(e3) == nullptr);

        auto e4 = mgr.create_entity();
        mgr.add(e4, Velocity{1, 0, 0});
        mgr.soft_removec<Velocity>(e4);
        print_item("soft_removec()", mgr.get_ptr<Velocity>(e4) == nullptr);

        auto e5 = mgr.create_entity();
        mgr.add(e5, Name{"测试"});
        mgr.delete_type_container<Name>();
        print_item("delete_type_container()", mgr.get_ptr<Name>(e5) == nullptr);
    }

    // --- 池访问 ---
    std::cout << "\n  [池访问]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(5);
        auto e = mgr.create_entity();
        mgr.add(e, Position{1, 2, 3});

        print_item("get_operating_message()", (bool)mgr.get_operating_message());

        single_class_set* set = mgr.get_single_class_set<Position>();
        print_item("get_single_class_set()", set != nullptr);

        const single_class_set* cset = mgr.get_single_class_set<Position>();
        print_item("get_single_class_set() const", cset != nullptr);

        mgr.reserve_component_capacity<Position>(1024);
        print_item("reserve_component_capacity()", true);

        class_pool<Position>* cv = mgr.get_component_vector<Position>();
        print_item("get_component_vector()", (cv && cv->size() == 1));
    }

    // --- 视图 ---
    std::cout << "\n  [视图]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});
        mgr.add(e1, Health{80, 100});
        mgr.add(e2, Health{90, 100});
        mgr.add(e1, Name{"E1"});

        // single_view
        auto sv = mgr.view<Position>();
        print_item("view<T>() size()", sv.size() == 3);
        print_item("view<T>() empty()", !sv.empty());
        print_item("view<T>() contains()", sv.contains(e1));

        int each_cnt = 0;
        sv.for_each([&each_cnt](Position&) { each_cnt++; });
        print_item("view<T>().for_each() [comp]", each_cnt == 3);

        int use_cnt = 0;
        sv.for_each([&use_cnt](entity, Position&) { use_cnt++; });
        print_item("view<T>().for_each() [ent+comp]", use_cnt == 3);

        // begin/end
        int iter_cnt = 0;
        for (auto it = sv.begin(); it != sv.end(); ++it) iter_cnt++;
        print_item("view<T>() begin/end", iter_cnt == 3);

        // component_begin/component_end
        int comp_cnt = 0;
        for (auto it = sv.component_begin(); it != sv.component_end(); ++it) comp_cnt++;
        print_item("view<T>() component_begin/end", comp_cnt == 3);

        // view<T>(func)
        int func_cnt = 0;
        mgr.view<Position>().for_each([&func_cnt](Position&) { func_cnt++; });
        print_item("view<T>().for_each(func)", func_cnt == 3);

        // multi_view
        auto mv = mgr.view<Position, Velocity>();
        print_item("view<Pos,Vel>() size()", mv.size() == 2);
        print_item("view<Pos,Vel>() empty()", !mv.empty());
        print_item("view<Pos,Vel>() contains()", mv.contains(e1));

        int mv_each = 0;
        mv.for_each([&mv_each](Position&, Velocity&) { mv_each++; });
        print_item("multi_view.for_each() [comp]", mv_each == 2);

        int mv_use = 0;
        mv.for_each([&mv_use](entity, Position&, Velocity&) { mv_use++; });
        print_item("multi_view.for_each() [ent+comp]", mv_use == 2);

        // 三组件
        auto tv = mgr.view<Position, Velocity, Health>();
        int tv_cnt = 0;
        tv.for_each([&tv_cnt](Position&, Velocity&, Health&) { tv_cnt++; });
        print_item("view<Pos,Vel,Hp>()", tv_cnt == 2);

        // exclude
        auto ev = mgr.view<Position>(ecs::without<Velocity>);
        int ev_cnt = 0;
        ev.for_each([&ev_cnt](Position&) { ev_cnt++; });
        print_item("view<Pos>(without<Vel>)", ev_cnt == 1);

        int ev_use = 0;
        ev.for_each([&ev_use](entity, Position&) { ev_use++; });
        print_item("without_view.for_each()", ev_use == 1);

        print_item("without_view.size()", ev.size() == 3);
        print_item("without_view.empty()", !ev.empty());

        // get
        auto gv = mgr.view<Position>(ecs::with<Health>);
        int gv_cnt = 0;
        gv.for_each([&gv_cnt](Position&, Health*) { gv_cnt++; });
        print_item("view<Pos>(with<Hp>)", gv_cnt == 3);

        int gv_use = 0;
        gv.for_each([&gv_use](entity, Position&, Health*) { gv_use++; });
        print_item("with_view.for_each()", gv_use == 3);

        print_item("with_view.size()", gv.size() == 3);
        print_item("with_view.empty()", !gv.empty());
    }

    // --- 新视图：OR / filter / filter_and / filter_or ---
    std::cout << "\n  [新视图: OR / filter / filter_and / filter_or]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        auto e4 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});
        mgr.add(e4, Velocity{40, 0, 0});  // e4 只有 Velocity
        mgr.add(e1, Health{80, 100});
        mgr.add(e2, Health{90, 100});

        // or_view: Position OR Velocity
        {
            auto ov = mgr.view_or<Position, Velocity>();
            int cnt = 0;
            int a_only = 0, b_only = 0, both = 0;
            ov.for_each([&](entity, Position* p, Velocity* v) {
                cnt++;
                if (p && v) both++;
                else if (p) a_only++;
                else if (v) b_only++;
            });
            print_item("view_or<Pos,Vel> 总数", cnt == 4);
            print_item("view_or<Pos,Vel> 仅A", a_only == 1);
            print_item("view_or<Pos,Vel> 仅B", b_only == 1);
            print_item("view_or<Pos,Vel> 两者", both == 2);
        }

        // filter_view: Position.x > 1
        {
            auto fv = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; });
            int cnt = 0;
            fv.for_each([&](Position&) { cnt++; });
            print_item("view_filtered<Pos> size", fv.size() == 2);
            print_item("view_filtered<Pos> for_each", cnt == 2);

            fv.rebuild();
            print_item("view_filtered<Pos> rebuild", fv.size() == 2);
        }

        // filter_and_view: Position.x > 1 AND Health
        {
            auto fav = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; }).and_<Health>();
            int cnt = 0;
            fav.for_each([&](Position&, Health&) { cnt++; });
            print_item("filter_and<Pos,Hp> for_each", cnt == 1);
            print_item("filter_and<Pos,Hp> empty", !fav.empty());
            print_item("filter_and<Pos,Hp> size", fav.size() == 1);
        }

        // filter_or_view: Position.x > 1 OR Velocity
        {
            auto fov = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; }).or_<Velocity>();
            int cnt = 0, a_only = 0, b_only = 0, both2 = 0;
            fov.for_each([&](entity, Position* p, Velocity* v) {
                cnt++;
                if (p && v) both2++;
                else if (p) a_only++;
                else if (v) b_only++;
            });
            // filter: Position.x>1 → e2(2), e3(3)
            // OR Velocity: e1(x=1, not filtered, has V), e4(V only)
            // total: e2(both), e3(a_only), e1(b_only), e4(b_only) = 4
            print_item("filter_or<Pos,Vel> 总数", cnt == 4);
            print_item("filter_or<Pos,Vel> 仅A", a_only == 1);
            print_item("filter_or<Pos,Vel> 仅B", b_only == 2);
            print_item("filter_or<Pos,Vel> 两者", both2 == 1);
        }
    }

    // ========================================================
    // 9.6 新视图: page / sorted_by_component / sorted_by_component_value / track_changes
    // ========================================================
    print_sub("view: page / sorted_by_component / sorted_by_component_value / track_changes");
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(10);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        auto e4 = mgr.create_entity();
        auto e5 = mgr.create_entity();
        mgr.add(e1, Position{10, 0, 0});
        mgr.add(e2, Position{30, 0, 0});
        mgr.add(e3, Position{20, 0, 0});
        mgr.add(e4, Position{50, 0, 0});
        mgr.add(e5, Position{40, 0, 0});
        mgr.add(e1, Velocity{1, 0, 0});
        mgr.add(e2, Velocity{3, 0, 0});
        mgr.add(e3, Velocity{2, 0, 0});
        mgr.add(e4, Velocity{5, 0, 0});
        mgr.add(e5, Velocity{4, 0, 0});
        mgr.add(e1, Health{100});
        mgr.add(e2, Health{90});
        mgr.add(e3, Health{80});

        // page
        {
            size_t cnt = 0;
            auto mv = mgr.view<Position, Velocity>();
            auto pv = mv.page(1, 3);
            pv.for_each([&](Position&, Velocity&) { ++cnt; });
            print_item("page(1,3) 处理数量", cnt == 3);
            print_item("page(1,3) size()", pv.size() == 3);
            print_item("page(1,3) !empty()", !pv.empty());
        }

        // sorted_by_component
        {
            class_pool<float> xs;
            auto mv = mgr.view<Position, Velocity>();
            auto sv = mv.sorted_by_component<Position>(
                [](const Position& a, const Position& b) { return a.x < b.x; });
            sv.for_each([&](Position& p, Velocity&) { xs.emplace_back(p.x); });
            bool sorted = true;
            for (size_t i = 1; i < xs.size(); ++i)
            {
                if (xs[i] < xs[i - 1]) { sorted = false; break; }
            }
            print_item("sorted_by_component<Position> 升序", sorted && xs.size() == 5);
            print_item("sorted_by_component size()", sv.size() == 5);
        }

        // sorted_by_component_value
        {
            auto sv = mgr.view<Position>();
            auto gv = sv.sorted_by_component_value(
                [](Position& p) -> int { return p.x / 20; });
            print_item("sorted_by_component_value size()", gv.size() == 5);
            print_item("sorted_by_component_value group_count()", gv.group_count() == 3);
            size_t groups = 0;
            gv.for_each_group([&](int, size_t, size_t) { ++groups; });
            print_item("sorted_by_component_value for_each_group", groups == 3);
        }

        // track_changes
        {
            auto mv = mgr.view<Position, Velocity>();
            auto cv = mv.track_changes();
            size_t cnt1 = 0;
            cv.for_each([&](Position&, Velocity&) { ++cnt1; });
            print_item("track_changes 首次全量", cnt1 == 5);

            size_t cnt2 = 0;
            cv.for_each([&](Position&, Velocity&) { ++cnt2; });
            print_item("track_changes 无变更返回空", cnt2 == 0);

            mgr.add(e1, Position{999, 0, 0}); // add 触发 pool version 变更
            size_t cnt3 = 0;
            cv.for_each([&](Position&, Velocity&) { ++cnt3; });
            print_item("track_changes 变更后全量", cnt3 == 5);

            cv.reset_tracking();
            size_t cnt4 = 0;
            cv.for_each([&](Position&, Velocity&) { ++cnt4; });
            print_item("track_changes reset后全量", cnt4 == 5);
        }

        // sort_entities_by_component 正确性
        {
            ecs::manager smgr;
            smgr.append_preallocated_entities(10);
            auto a = smgr.create_entity();
            auto b = smgr.create_entity();
            auto c = smgr.create_entity();
            smgr.add(a, Position{30, 0, 0});
            smgr.add(b, Position{10, 0, 0});
            smgr.add(c, Position{20, 0, 0});
            smgr.add(a, Velocity{3, 0, 0});
            smgr.add(b, Velocity{1, 0, 0});
            smgr.add(c, Velocity{2, 0, 0});

            smgr.sort_entities_by_component<Position>(
                [](Position& x, Position& y) { return x.x < y.x; });

            class_pool<float> xs;
            smgr.view<Position>().for_each([&](Position& p) { xs.emplace_back(p.x); });
            bool sorted_ok = xs.size() == 3 && xs[0] == 10 && xs[1] == 20 && xs[2] == 30;
            print_item("sort_entities_by_component 排序正确", sorted_ok);

            Velocity* va = smgr.get_ptr<Velocity>(a);
            Velocity* vb = smgr.get_ptr<Velocity>(b);
            Velocity* vc = smgr.get_ptr<Velocity>(c);
            print_item("sort_entities_by_component 映射保持(a)",
                va != nullptr && va->vx == 3);
            print_item("sort_entities_by_component 映射保持(b)",
                vb != nullptr && vb->vx == 1);
            print_item("sort_entities_by_component 映射保持(c)",
                vc != nullptr && vc->vx == 2);
        }

        // sort_component_container 正确性 + 映射同步
        {
            ecs::manager cmgr;
            cmgr.append_preallocated_entities(10);
            auto a = cmgr.create_entity();
            auto b = cmgr.create_entity();
            auto c = cmgr.create_entity();
            cmgr.add(a, Position{30, 0, 0});
            cmgr.add(b, Position{10, 0, 0});
            cmgr.add(c, Position{20, 0, 0});

            cmgr.sort_component_container<Position>(
                [](Position& x, Position& y) { return x.x < y.x; });

            Position* pa = cmgr.get_ptr<Position>(a);
            Position* pb = cmgr.get_ptr<Position>(b);
            Position* pc = cmgr.get_ptr<Position>(c);
            bool mapping_ok = pa && pb && pc
                && pa->x == 30 && pb->x == 10 && pc->x == 20;
            print_item("sort_component_container 映射同步", mapping_ok);

            class_pool<float> xs;
            cmgr.view<Position>().for_each([&](Position& p) { xs.emplace_back(p.x); });
            bool sorted_ok = xs.size() == 3 && xs[0] == 10 && xs[1] == 20 && xs[2] == 30;
            print_item("sort_component_container 排序正确", sorted_ok);
        }

        // reorder_by_component 正确性
        {
            ecs::manager rmgr;
            rmgr.append_preallocated_entities(10);
            auto a = rmgr.create_entity();
            auto b = rmgr.create_entity();
            auto c = rmgr.create_entity();
            rmgr.add(a, Position{10, 0, 0});
            rmgr.add(b, Position{20, 0, 0});
            rmgr.add(c, Position{30, 0, 0});
            rmgr.add(a, Velocity{1, 0, 0});
            rmgr.add(b, Velocity{2, 0, 0});
            rmgr.add(c, Velocity{3, 0, 0});

            rmgr.reorder_by_component<Position, Velocity>(
                [](Velocity& x, Velocity& y) { return x.vx > y.vx; });

            class_pool<float> xs;
            rmgr.view<Position>().for_each([&](Position& p) { xs.emplace_back(p.x); });
            bool reordered = xs.size() == 3 && xs[0] == 30 && xs[1] == 20 && xs[2] == 10;
            print_item("reorder_by_component 重排正确", reordered);

            Position* pa = rmgr.get_ptr<Position>(a);
            Position* pb = rmgr.get_ptr<Position>(b);
            Position* pc = rmgr.get_ptr<Position>(c);
            bool mapping_ok = pa && pb && pc
                && pa->x == 10 && pb->x == 20 && pc->x == 30;
            print_item("reorder_by_component 映射保持", mapping_ok);
        }
    }

    // ========================================================
    // 9.5 新增 Bevy 对标接口 (filter_changed / filter_added / exactly_one / find_one / view_any_of / iter_over_entities)
    // ========================================================
    std::cout << "\n========================================================\n";
    std::cout << "  9.5 新增 Bevy 对标接口\n";
    std::cout << "========================================================\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        auto e4 = mgr.create_entity();
        auto e5 = mgr.create_entity();

        // --- filter_changed (single_view) ---
        std::cout << "\n  [filter_changed (single_view)]\n";
        {
            mgr.add(e1, Position{1, 0, 0});
            mgr.add(e2, Position{2, 0, 0});
            mgr.add(e3, Position{3, 0, 0});

            auto fcv = mgr.view<Position>().filter_changed();

            // 首次查询：所有实体都是"变更的"
            size_t cnt1 = 0;
            fcv.for_each([&](Position&) { ++cnt1; });
            print_item("首次查询全量返回", cnt1 == 3);

            // 无变更时返回空
            size_t cnt2 = 0;
            fcv.for_each([&](Position&) { ++cnt2; });
            print_item("无变更返回空", cnt2 == 0);

            // 修改一个实体后仅返回该实体
            mgr.add(e1, Position{10, 0, 0}); // add 触发变更
            size_t cnt3 = 0;
            fcv.for_each([&](Position&) { ++cnt3; });
            print_item("修改1个实体后仅返回1个", cnt3 == 1);

            // reset 后重新全量
            fcv.reset_tracking();
            size_t cnt4 = 0;
            fcv.for_each([&](Position&) { ++cnt4; });
            print_item("reset后全量返回", cnt4 == 3);
        }

        // --- filter_added (single_view) ---
        std::cout << "\n  [filter_added (single_view)]\n";
        {
            ecs::manager mgr2;
            mgr2.append_preallocated_entities(10);
            auto a1 = mgr2.create_entity();
            auto a2 = mgr2.create_entity();
            auto a3 = mgr2.create_entity();

            auto fav = mgr2.view<Position>().filter_added();

            // 添加组件后首次查询应返回所有
            mgr2.add(a1, Position{1, 0, 0});
            mgr2.add(a2, Position{2, 0, 0});
            mgr2.add(a3, Position{3, 0, 0});

            size_t cnt1 = 0;
            fav.for_each([&](Position&) { ++cnt1; });
            print_item("首次添加全量返回", cnt1 == 3);

            // 无新添加时返回空
            size_t cnt2 = 0;
            fav.for_each([&](Position&) { ++cnt2; });
            print_item("无新添加返回空", cnt2 == 0);

            // 更新已有组件不触发 added
            mgr2.add(a1, Position{10, 0, 0});
            size_t cnt3 = 0;
            fav.for_each([&](Position&) { ++cnt3; });
            print_item("更新组件不触发added", cnt3 == 0);

            // 新实体添加组件触发 added
            auto a4 = mgr2.create_entity();
            mgr2.add(a4, Position{4, 0, 0});
            size_t cnt4 = 0;
            fav.for_each([&](Position&) { ++cnt4; });
            print_item("新实体添加触发added", cnt4 == 1);
        }

        // --- filter_changed / filter_added (multi_view) ---
        std::cout << "\n  [filter_changed / filter_added (multi_view)]\n";
        {
            ecs::manager mgr3;
            mgr3.append_preallocated_entities(10);
            auto m1 = mgr3.create_entity();
            auto m2 = mgr3.create_entity();
            auto m3 = mgr3.create_entity();

            mgr3.add(m1, Position{1, 0, 0});
            mgr3.add(m2, Position{2, 0, 0});
            mgr3.add(m3, Position{3, 0, 0});
            mgr3.add(m1, Velocity{10, 0, 0});
            mgr3.add(m2, Velocity{20, 0, 0});
            mgr3.add(m3, Velocity{30, 0, 0});

            // filter_changed<Position> on multi_view
            auto mfcv = mgr3.view<Position, Velocity>().filter_changed<Position>();
            size_t cnt1 = 0;
            mfcv.for_each([&](Position&, Velocity&) { ++cnt1; });
            print_item("multi filter_changed 首次全量", cnt1 == 3);

            size_t cnt2 = 0;
            mfcv.for_each([&](Position&, Velocity&) { ++cnt2; });
            print_item("multi filter_changed 无变更返回空", cnt2 == 0);

            mgr3.add(m1, Position{10, 0, 0});
            size_t cnt3 = 0;
            mfcv.for_each([&](Position&, Velocity&) { ++cnt3; });
            print_item("multi filter_changed 仅返回变更实体", cnt3 == 1);

            // filter_added<Position> on multi_view
            auto mfav = mgr3.view<Position, Velocity>().filter_added<Position>();
            size_t cnt4 = 0;
            mfav.for_each([&](Position&, Velocity&) { ++cnt4; });
            print_item("multi filter_added 无新添加返回空", cnt4 == 0);

            auto m4 = mgr3.create_entity();
            mgr3.add(m4, Position{4, 0, 0});
            mgr3.add(m4, Velocity{40, 0, 0});
            size_t cnt5 = 0;
            mfav.for_each([&](Position&, Velocity&) { ++cnt5; });
            print_item("multi filter_added 新实体添加触发", cnt5 == 1);
        }

        // --- exactly_one ---
        std::cout << "\n  [exactly_one]\n";
        {
            ecs::manager mgr4;
            mgr4.append_preallocated_entities(10);
            auto x1 = mgr4.create_entity();
            mgr4.add(x1, Position{42, 0, 0});
            mgr4.add(x1, Velocity{100, 0, 0});

            // single_view::exactly_one
            auto& pos = mgr4.view<Position>().exactly_one();
            print_item("single exactly_one x=42", pos.x == 42);

            // multi_view::exactly_one
            auto [p, v] = mgr4.view<Position, Velocity>().exactly_one();
            print_item("multi exactly_one x=42, vx=100", p.x == 42 && v.vx == 100);
        }

        // --- find_one ---
        std::cout << "\n  [find_one]\n";
        {
            // e1 和 e2 需要同时有 Position 和 Velocity 才能匹配
            mgr.add(e1, Velocity{10, 0, 0});
            mgr.add(e2, Velocity{20, 0, 0});
            auto [p1, v1] = mgr.view<Position, Velocity>().find_one(e1);
            print_item("find_one(e1) 匹配", p1 != nullptr && v1 != nullptr);
            print_item("find_one(e1) x=10", p1 && p1->x == 10);

            // 不匹配的实体返回 nullptr
            mgr.add(e4, Position{4, 0, 0}); // e4 只有 Position，没有 Velocity
            auto [p4, v4] = mgr.view<Position, Velocity>().find_one(e4);
            print_item("find_one(e4) 不匹配返回nullptr", p4 == nullptr && v4 == nullptr);
        }

        // --- view_any_of ---
        std::cout << "\n  [view_any_of (N元OR)]\n";
        {
            ecs::manager mgr5;
            mgr5.append_preallocated_entities(10);
            auto o1 = mgr5.create_entity();
            auto o2 = mgr5.create_entity();
            auto o3 = mgr5.create_entity();
            auto o4 = mgr5.create_entity();

            mgr5.add(o1, Position{1, 0, 0});                      // 仅 Position
            mgr5.add(o2, Velocity{20, 0, 0});                      // 仅 Velocity
            mgr5.add(o3, Position{3, 0, 0});
            mgr5.add(o3, Velocity{30, 0, 0});                      // 两者都有
            mgr5.add(o4, Health{80, 100});                         // 仅 Health

            // 2 元 OR
            {
                auto av = mgr5.view_any_of<Position, Velocity>();
                size_t cnt = 0;
                av.for_each([&](Position* p, Velocity* v) {
                    ++cnt;
                    if (p) assert(p->x > 0);
                    if (v) assert(v->vx > 0);
                });
                print_item("view_any_of<Pos,Vel> 总数", cnt == 3);
            }

            // 3 元 OR
            {
                auto av = mgr5.view_any_of<Position, Velocity, Health>();
                size_t cnt = 0;
                av.for_each([&](Position* p, Velocity* v, Health* h) {
                    ++cnt;
                    (void)p; (void)v; (void)h;
                });
                print_item("view_any_of<Pos,Vel,Hp> 总数", cnt == 4);
            }
        }

        // --- iter_over_entities ---
        std::cout << "\n  [iter_over_entities]\n";
        {
            std::array<entity, 3> targets = {e1, e2, e5}; // e5 没有组件
            auto iov = mgr.view<Position, Velocity>().iter_over_entities(targets);

            size_t cnt = 0;
            iov.for_each([&](Position&, Velocity&) { ++cnt; });
            print_item("iter_over_entities 仅匹配的实体", cnt == 2); // e1, e2
        }
    }

    // ========================================================
    // 10. Group 系统（Non-Owning + Owning）
    // ========================================================
    print_section(10, "Group 系统（Non-Owning + Owning）");
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        auto e4 = mgr.create_entity();
        auto e5 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e4, Position{4, 0, 0});
        mgr.add(e5, Position{5, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});
        mgr.add(e3, Velocity{30, 0, 0});
        mgr.add(e1, Health{80, 100});
        mgr.add(e2, Health{90, 100});
        mgr.add(e3, Health{70, 100});

        // --- Non-OwningGroup (group) ---
        std::cout << "\n  [Non-OwningGroup (group)]\n";
        {
            auto g = mgr.group<Position, Velocity>();
            print_item("group<Pos,Vel>() size()", g.size() == 3);
            print_item("group<Pos,Vel>() empty()", !g.empty());
            print_item("group<Pos,Vel>() contains(e1)", g.contains(e1));
            print_item("group<Pos,Vel>() !contains(e4)", !g.contains(e4));

            int cnt = 0;
            g.for_each([&cnt](Position& p, Velocity& v) { cnt++; (void)p; (void)v; });
            print_item("group<Pos,Vel>.for_each() [comp]", cnt == 3);

            int use_cnt = 0;
            g.for_each([&use_cnt](entity e, Position& p, Velocity& v) {
                use_cnt++; (void)e; (void)p; (void)v;
            });
            print_item("group<Pos,Vel>.for_each() [ent+comp]", use_cnt == 3);

            print_item("group<Pos,Vel>.front()", g.front() == e1 || g.front() == e2 || g.front() == e3);
            print_item("group<Pos,Vel>.back()", g.back() == e1 || g.back() == e2 || g.back() == e3);

            auto* p = g.get<Position>(e1);
            print_item("group<Pos,Vel>.get<Position>(e1)", (p && p->x == 1));

            auto* v = g.get<Velocity>(e2);
            print_item("group<Pos,Vel>.get<Velocity>(e2)", (v && v->vx == 20));

            g.rebuild();
            print_item("group<Pos,Vel>.rebuild()", g.size() == 3);
        }

        // --- Non-OwningGroup 三组件 ---
        std::cout << "\n  [Non-OwningGroup 三组件]\n";
        {
            auto g = mgr.group<Position, Velocity, Health>();
            print_item("group<Pos,Vel,Hp>() size()", g.size() == 3);
            print_item("group<Pos,Vel,Hp>() contains(e2)", g.contains(e2));
            print_item("group<Pos,Vel,Hp>() !contains(e4)", !g.contains(e4));

            int cnt = 0;
            g.for_each([&cnt](Position& p, Velocity& v, Health& h) {
                cnt++; (void)p; (void)v; (void)h;
            });
            print_item("group<Pos,Vel,Hp>.for_each()", cnt == 3);

            int use_cnt = 0;
            g.for_each([&use_cnt](entity e, Position& p, Velocity& v, Health& h) {
                use_cnt++; (void)e; (void)p; (void)v; (void)h;
            });
            print_item("group<Pos,Vel,Hp>.for_each() [ent]", use_cnt == 3);
        }

        // --- OwningGroup ---
        std::cout << "\n  [OwningGroup (group + owned)]\n";
        {
            auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);
            print_item("group<Pos,Vel>(owned<Pos>) size()", og.size() == 3);
            print_item("group<Pos,Vel>(owned<Pos>) empty()", !og.empty());
            print_item("group<Pos,Vel>(owned<Pos>) contains(e1)", og.contains(e1));
            print_item("group<Pos,Vel>(owned<Pos>) !contains(e4)", !og.contains(e4));

            int cnt = 0;
            og.for_each([&cnt](Position& p, Velocity& v) { cnt++; (void)p; (void)v; });
            print_item("owning_group.for_each() [comp]", cnt == 3);

            int use_cnt = 0;
            og.for_each([&use_cnt](entity e, Position& p, Velocity& v) {
                use_cnt++; (void)e; (void)p; (void)v;
            });
            print_item("owning_group.for_each() [ent+comp]", use_cnt == 3);

            print_item("owning_group.front()", og.front() == e1 || og.front() == e2 || og.front() == e3);
            print_item("owning_group.back()", og.back() == e1 || og.back() == e2 || og.back() == e3);

            auto* p = og.get<Position>(e1);
            print_item("owning_group.get<Position>(e1)", (p && p->x == 1));

            og.rebuild();
            print_item("owning_group.rebuild()", og.size() == 3);

            // 验证 owning_group 重排后数据一致性
            class_pool<float> x_values;
            og.for_each([&x_values](Position& p, Velocity&) { x_values.emplace_back(p.x); });
            bool all_match = true;
            for (auto xv : x_values) {
                if (xv != 1 && xv != 2 && xv != 3) { all_match = false; break; }
            }
            print_item("owning_group 数据一致性", all_match);
        }

        // --- OwningGroup 三组件 ---
        std::cout << "\n  [OwningGroup 三组件]\n";
        {
            auto og = mgr.group<Position, Velocity, Health>(ecs::owned<Position>);
            print_item("group<Pos,Vel,Hp>(owned<Pos>) size()", og.size() == 3);

            int cnt = 0;
            og.for_each([&cnt](Position& p, Velocity& v, Health& h) {
                cnt++; (void)p; (void)v; (void)h;
            });
            print_item("owning_group<3>.for_each()", cnt == 3);
        }

        // --- ReorderGroup ---
        std::cout << "\n  [ReorderGroup (group + reorder)]\n";
        {
            auto rg = mgr.group<Position, Velocity>(ecs::reorder<Position>);
            print_item("group<Pos,Vel>(reorder<Pos>) size()", rg.size() == 3);
            print_item("group<Pos,Vel>(reorder<Pos>) empty()", !rg.empty());
            print_item("group<Pos,Vel>(reorder<Pos>) contains(e1)", rg.contains(e1));
            print_item("group<Pos,Vel>(reorder<Pos>) !contains(e4)", !rg.contains(e4));

            int cnt = 0;
            rg.for_each([&cnt](Position& p, Velocity& v) { cnt++; (void)p; (void)v; });
            print_item("reorder_group.for_each() [comp]", cnt == 3);

            int use_cnt = 0;
            rg.for_each([&use_cnt](entity e, Position& p, Velocity& v) {
                use_cnt++; (void)e; (void)p; (void)v;
            });
            print_item("reorder_group.for_each() [ent+comp]", use_cnt == 3);

            print_item("reorder_group.front()", rg.front() == e1 || rg.front() == e2 || rg.front() == e3);
            print_item("reorder_group.back()", rg.back() == e1 || rg.back() == e2 || rg.back() == e3);

            auto* p = rg.get<Position>(e1);
            print_item("reorder_group.get<Position>(e1)", (p && p->x == 1));

            rg.rebuild();
            print_item("reorder_group.rebuild()", rg.size() == 3);

            class_pool<float> x_values;
            rg.for_each([&x_values](Position& p, Velocity&) { x_values.emplace_back(p.x); });
            bool all_match = true;
            for (auto xv : x_values) {
                if (xv != 1 && xv != 2 && xv != 3) { all_match = false; break; }
            }
            print_item("reorder_group 数据一致性", all_match);
        }

        // --- ReorderGroup 三组件 ---
        std::cout << "\n  [ReorderGroup 三组件]\n";
        {
            auto rg = mgr.group<Position, Velocity, Health>(ecs::reorder<Position>);
            print_item("group<Pos,Vel,Hp>(reorder<Pos>) size()", rg.size() == 3);

            int cnt = 0;
            rg.for_each([&cnt](Position& p, Velocity& v, Health& h) {
                cnt++; (void)p; (void)v; (void)h;
            });
            print_item("reorder_group<3>.for_each()", cnt == 3);
        }

        // --- ReorderGroup 共享状态 ---
        std::cout << "\n  [ReorderGroup 共享状态]\n";
        {
            auto rg1 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
            auto rg2 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
            rg2.share_with(rg1);
            print_item("share_with() size一致", rg1.size() == rg2.size());
            print_item("share_with() empty一致", rg1.empty() == rg2.empty());

            int cnt1 = 0, cnt2 = 0;
            rg1.for_each([&cnt1](Position& p, Velocity& v) { cnt1++; (void)p; (void)v; });
            rg2.for_each([&cnt2](Position& p, Velocity& v) { cnt2++; (void)p; (void)v; });
            print_item("share_with() 迭代计数一致", cnt1 == 3 && cnt2 == 3);
        }
    }

    // ========================================================
    // 11. runtime_view 运行时视图
    // ========================================================
    print_section(11, "runtime_view 运行时视图");
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        auto e4 = mgr.create_entity();
        auto e5 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e4, Position{4, 0, 0});
        mgr.add(e5, Position{5, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});
        mgr.add(e3, Velocity{30, 0, 0});
        mgr.add(e1, Health{80, 100});
        mgr.add(e2, Health{90, 100});
        mgr.add(e3, Health{70, 100});

        // 实体掩码验证
        std::cout << "\n  [实体掩码]\n";
        {
            uint64_t mask_e1 = mgr.get_entity_mask(e1);
            uint64_t mask_e4 = mgr.get_entity_mask(e4);
            uint64_t pos_bit = mgr.get_component_bit<Position>();
            uint64_t vel_bit = mgr.get_component_bit<Velocity>();
            uint64_t hp_bit  = mgr.get_component_bit<Health>();

            print_item("e1 有 Position 掩码位", (mask_e1 & pos_bit) != 0);
            print_item("e1 有 Velocity 掩码位", (mask_e1 & vel_bit) != 0);
            print_item("e1 有 Health 掩码位",   (mask_e1 & hp_bit) != 0);
            print_item("e4 有 Position 掩码位", (mask_e4 & pos_bit) != 0);
            print_item("e4 无 Velocity 掩码位", (mask_e4 & vel_bit) == 0);
            print_item("e4 无 Health 掩码位",   (mask_e4 & hp_bit) == 0);
        }

        // 双组件运行时视图
        std::cout << "\n  [双组件 runtime_view]\n";
        {
            auto rv = mgr.runtime_view_create({
                type_id::get_type_id<Position>(),
                type_id::get_type_id<Velocity>()
            });
            print_item("runtime_view<Pos+Vel> size()", rv.size() >= 3);
            print_item("runtime_view<Pos+Vel> empty()", !rv.empty());
            print_item("runtime_view<Pos+Vel> contains(e1)", rv.contains(e1));
            print_item("runtime_view<Pos+Vel> !contains(e4)", !rv.contains(e4));

            int cnt = 0;
            rv.for_each([&cnt, &rv](entity e) {
                cnt++;
                auto* p = rv.get_ptr<Position>(e);
                auto* v = rv.get_ptr<Velocity>(e);
                (void)p; (void)v;
            });
            print_item("runtime_view<Pos+Vel>.for_each()", cnt == 3);

            auto* p = rv.get_ptr<Position>(e1);
            print_item("runtime_view.get_ptr<Position>(e1)", (p && p->x == 1));

            auto* v = rv.get_ptr<Velocity>(e2);
            print_item("runtime_view.get_ptr<Velocity>(e2)", (v && v->vx == 20));

            rv.rebuild();
            print_item("runtime_view<Pos+Vel>.rebuild()", rv.size() >= 3);
        }

        // 三组件运行时视图
        std::cout << "\n  [三组件 runtime_view]\n";
        {
            auto rv = mgr.runtime_view_create({
                type_id::get_type_id<Position>(),
                type_id::get_type_id<Velocity>(),
                type_id::get_type_id<Health>()
            });
            int cnt = 0;
            rv.for_each([&cnt](entity e) {
                cnt++;
                (void)e;
            });
            print_item("runtime_view<Pos+Vel+Hp>.for_each()", cnt == 3);
            print_item("runtime_view<Pos+Vel+Hp> contains(e2)", rv.contains(e2));
            print_item("runtime_view<Pos+Vel+Hp> !contains(e4)", !rv.contains(e4));
        }

        // 排除视图
        std::cout << "\n  [排除 runtime_view]\n";
        {
            auto rv = mgr.runtime_view_create(
                { type_id::get_type_id<Position>() },
                { type_id::get_type_id<Velocity>() }
            );
            int cnt = 0;
            rv.for_each([&cnt](entity e) {
                cnt++;
                (void)e;
            });
            print_item("runtime_view<Pos excl Vel> for_each", cnt == 2);
            print_item("runtime_view<Pos excl Vel> !contains(e1)", !rv.contains(e1));
            print_item("runtime_view<Pos excl Vel> contains(e4)", rv.contains(e4));
        }

        // 删除后掩码更新
        std::cout << "\n  [删除后掩码更新]\n";
        {
            mgr.hard_remove<Velocity>(e1);
            uint64_t mask_e1 = mgr.get_entity_mask(e1);
            uint64_t vel_bit = mgr.get_component_bit<Velocity>();
            print_item("hard_remove<Vel>(e1) 后掩码清除", (mask_e1 & vel_bit) == 0);

            auto rv = mgr.runtime_view_create({
                type_id::get_type_id<Position>(),
                type_id::get_type_id<Velocity>()
            });
            int cnt = 0;
            rv.for_each([&cnt](entity) { cnt++; });
            print_item("删除后 runtime_view<Pos+Vel> 数量", cnt == 2);
        }
    }

    // ========================================================
    // 12. 持久化视图测试（自动同步，无需手动 rebuild）
    // ========================================================
    std::cout << "\n";
    print_section(12, "持久化视图（自动同步）");

    // Non-Owning Group 持久化
    std::cout << "\n  [Non-Owning Group 持久化]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        auto e3 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});

        auto g = mgr.group<Position, Velocity>();
        print_item("初始 size()", g.size() == 2);

        auto e4 = mgr.create_entity();
        mgr.add(e4, Position{4, 0, 0});
        mgr.add(e4, Velocity{40, 0, 0});
        print_item("add 后 自动同步 size()=3", g.size() == 3);

        mgr.hard_remove<Velocity>(e2);
        print_item("remove 后 自动同步 size()=2", g.size() == 2);

        int cnt = 0;
        g.for_each([&cnt](Position& p, Velocity& v) { cnt++; (void)p; (void)v; });
        print_item("for_each 自动同步 cnt=2", cnt == 2);
    }

    // Owning Group 持久化
    std::cout << "\n  [Owning Group 持久化]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});

        auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);
        print_item("初始 size()", og.size() == 2);

        auto e3 = mgr.create_entity();
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e3, Velocity{30, 0, 0});
        print_item("add 后 自动同步 size()=3", og.size() == 3);

        mgr.hard_remove<Position>(e1);
        print_item("remove 后 自动同步 size()=2", og.size() == 2);

        int cnt = 0;
        og.for_each([&cnt](Position& p, Velocity& v) { cnt++; (void)p; (void)v; });
        print_item("for_each 自动同步 cnt=2", cnt == 2);
    }

    // Reorder Group 持久化
    std::cout << "\n  [Reorder Group 持久化]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});

        auto rg = mgr.group<Position, Velocity>(ecs::reorder<Position>);
        print_item("初始 size()", rg.size() == 2);

        auto e3 = mgr.create_entity();
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e3, Velocity{30, 0, 0});
        print_item("add 后 自动同步 size()=3", rg.size() == 3);

        mgr.hard_remove<Position>(e1);
        print_item("remove 后 自动同步 size()=2", rg.size() == 2);

        int cnt = 0;
        rg.for_each([&cnt](Position& p, Velocity& v) { cnt++; (void)p; (void)v; });
        print_item("for_each 自动同步 cnt=2", cnt == 2);
    }

    // Runtime View 持久化
    std::cout << "\n  [Runtime View 持久化]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});

        auto rv = mgr.runtime_view_create({
            type_id::get_type_id<Position>(),
            type_id::get_type_id<Velocity>()
        });
        print_item("初始 size()", rv.size() == 2);

        auto e3 = mgr.create_entity();
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e3, Velocity{30, 0, 0});
        print_item("add 后 自动同步 size()=3", rv.size() == 3);

        mgr.hard_remove<Velocity>(e2);
        print_item("remove 后 primary set 仍为 3（上限）", rv.size() == 3);

        int cnt = 0;
        rv.for_each([&cnt](entity) { cnt++; });
        print_item("for_each 自动同步 cnt=2", cnt == 2);
    }

    // 多次连续变更
    std::cout << "\n  [多次连续变更]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);
        auto e1 = mgr.create_entity();
        auto e2 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e2, Position{2, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.add(e2, Velocity{20, 0, 0});

        auto g = mgr.group<Position, Velocity>();
        print_item("当前 size()", g.size() == 2);

        for (int i = 0; i < 5; ++i)
        {
            auto e = mgr.create_entity();
            mgr.add(e, Position{10.0f + static_cast<float>(i), 0, 0});
            mgr.add(e, Velocity{10.0f + static_cast<float>(i), 0, 0});
        }
        print_item("连续 5 次 add 后 size()=7", g.size() == 7);

        int cnt = 0;
        g.for_each([&cnt](Position& p, Velocity& v) { cnt++; (void)p; (void)v; });
        print_item("for_each 自动同步 cnt=7", cnt == 7);
    }

    // ========================================================
    // 13. 生命周期信号测试
    // ========================================================
    std::cout << "\n";
    print_section(13, "生命周期信号");

    // 即时信号：实体创建/销毁
    std::cout << "\n  [即时信号：实体级]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);

        int created_count = 0;
        int destroyed_count = 0;

        auto on_created = [](entity, void* data) noexcept {
            *static_cast<int*>(data) += 1;
            };
        auto on_destroyed = [](entity, void* data) noexcept {
            *static_cast<int*>(data) += 1;
            };

        mgr.set_on_entity_created(+on_created, &created_count);
        mgr.set_on_entity_destroyed(+on_destroyed, &destroyed_count);

        auto _e1 = mgr.create_entity(); (void)_e1;
        print_item("create_entity 触发即时信号", created_count == 1);

        auto _e2 = mgr.create_entity(); (void)_e2;
        auto e2 = mgr.create_entity();
        print_item("3 次 create 触发 3 次信号", created_count == 3);

        mgr.delete_entity(e2);
        print_item("delete_entity 触发即时信号", destroyed_count == 1);
        print_item("destroy 后 created 不变", created_count == 3);
    }

    // 即时信号：组件添加/移除
    std::cout << "\n  [即时信号：组件级]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);

        int pos_added = 0;
        int pos_removed = 0;
        int vel_added = 0;
        int vel_removed = 0;

        auto on_add = [](entity, void*, void* data) noexcept {
            *static_cast<int*>(data) += 1;
            };
        auto on_remove = [](entity, void*, void* data) noexcept {
            *static_cast<int*>(data) += 1;
            };

        mgr.set_on_add<Position>(+on_add, &pos_added);
        mgr.set_on_remove<Position>(+on_remove, &pos_removed);
        mgr.set_on_add<Velocity>(+on_add, &vel_added);
        mgr.set_on_remove<Velocity>(+on_remove, &vel_removed);

        auto e1 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        print_item("add<Position> 触发 on_add", pos_added == 1);

        mgr.add(e1, Velocity{10, 0, 0});
        print_item("add<Velocity> 触发 on_add", vel_added == 1);

        mgr.add(e1, Position{2, 0, 0});  // 覆盖
        print_item("覆盖 add<Position> 再次触发 on_add", pos_added == 2);

        mgr.hard_remove<Position>(e1);
        print_item("hard_remove<Position> 触发 on_remove", pos_removed == 1);

        mgr.hard_remove<Velocity>(e1);
        print_item("hard_remove<Velocity> 触发 on_remove", vel_removed == 1);
    }

    // 即时信号：组件指针验证
    std::cout << "\n  [即时信号：组件指针有效性]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);

        auto on_add_pos = [](entity, void* comp, void*) noexcept {
            auto* p = static_cast<Position*>(comp);
            if (p) p->x = 999;
            };

        mgr.set_on_add<Position>(+on_add_pos);

        auto e1 = mgr.create_entity();
        mgr.add(e1, Position{42, 0, 0});

        auto* pos = mgr.get_ptr<Position>(e1);
        print_item("on_add 可修改组件指针", pos && pos->x == 999);
    }

    // 延迟信号：实体级
    std::cout << "\n  [延迟信号：实体级 flush]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);

        auto e1 = mgr.create_entity();
        auto _e3 = mgr.create_entity(); (void)_e3;
        mgr.delete_entity(e1);

        int created = 0, destroyed = 0;
        mgr.flush_entity_signals([&created, &destroyed](uint32_t type, uint32_t) noexcept {
            if (type == 0) created++;
            else destroyed++;
            });

        print_item("flush 后 created=2", created == 2);
        print_item("flush 后 destroyed=1", destroyed == 1);
        print_item("flush 后缓冲区为空", !mgr.has_pending_entity_signals());
    }

    // 延迟信号：组件级
    std::cout << "\n  [延迟信号：组件级 flush]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(20);

        auto e1 = mgr.create_entity();
        mgr.add(e1, Position{1, 0, 0});
        mgr.add(e1, Velocity{10, 0, 0});
        mgr.hard_remove<Position>(e1);
        mgr.add(e1, Health{80, 100});

        int added = 0, removed = 0;

        mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
            if (type == 0) added++;
            else removed++;
            });

        print_item("flush 后 added=3 (Pos+Vel+Health)", added == 3);
        print_item("flush 后 removed=1 (Pos)", removed == 1);
        print_item("flush 后缓冲区为空", !mgr.has_pending_component_signals());
    }

    // ========================================================
    // 14. 性能基准测试
    // ========================================================
    print_section(14, "性能基准测试");
    {
        const size_t entity_count = 500000;
        std::cout << "  实体总数: " << entity_count << "\n";

        ecs::manager ecss;
        Timer timer;

        // ---- 14.1 测试数据准备 ----
        print_perf_sub("14.1 测试数据准备");

        timer.reset();
        ecss.append_preallocated_entities(entity_count);
        print_perf("预分配实体", entity_count, timer.elapsed_ms());

        timer.reset();
        class_pool<entity> entities;
        entities.increase_capacity(entity_count);
        for (size_t i = 0; i < entity_count; ++i)
            entities.emplace_back(ecss.create_entity());
        print_perf("实体创建", entity_count, timer.elapsed_ms());

        // 预留容量
        ecss.reserve_component_capacity<Position>(entity_count);
        ecss.reserve_component_capacity<Velocity>(entity_count / 2);
        ecss.reserve_component_capacity<Health>(entity_count);
        ecss.reserve_component_capacity<Name>(entity_count / 10);
        ecss.reserve_component_capacity<Damage>(entity_count / 2);
        ecss.reserve_component_capacity<Armor>(entity_count / 2);
        ecss.reserve_component_capacity<Speed>(entity_count / 4);

        // 随机数据
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> pos_dist(-1000.0f, 1000.0f);
        std::uniform_real_distribution<float> vel_dist(-10.0f, 10.0f);
        std::uniform_int_distribution<int> hp_dist(1, 100);
        std::uniform_int_distribution<int> dmg_dist(1, 50);
        std::uniform_int_distribution<int> armor_dist(1, 200);

        const size_t vel_count = entity_count / 2;
        const size_t name_count = entity_count / 10;
        const size_t speed_count = entity_count / 4;

        class_pool<Position> positions;
        class_pool<Velocity> velocities;
        class_pool<Health> healths;
        class_pool<Name> names;
        class_pool<Damage> damages;
        class_pool<Armor> armors;
        class_pool<Speed> speeds;

        positions.increase_capacity(entity_count);
        velocities.increase_capacity(vel_count);
        healths.increase_capacity(entity_count);
        names.increase_capacity(name_count);
        damages.increase_capacity(vel_count);
        armors.increase_capacity(vel_count);
        speeds.increase_capacity(speed_count);

        for (size_t i = 0; i < entity_count; ++i) {
            positions.emplace_back(pos_dist(gen), pos_dist(gen), pos_dist(gen));
            healths.emplace_back(hp_dist(gen), 100);
        }
        for (size_t i = 0; i < vel_count; ++i) {
            velocities.emplace_back(vel_dist(gen), vel_dist(gen), vel_dist(gen));
            damages.emplace_back(dmg_dist(gen));
            armors.emplace_back(armor_dist(gen));
        }
        for (size_t i = 0; i < name_count; ++i)
            names.emplace_back("Entity_" + std::to_string(i));
        for (size_t i = 0; i < speed_count; ++i)
            speeds.emplace_back(vel_dist(gen) * 0.5f);

        // 批量添加
        timer.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), entity_count), std::span<const Position>(positions.data(), entity_count));
        print_perf("Position 批量添加", entity_count, timer.elapsed_ms());

        timer.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count), std::span<const Velocity>(velocities.data(), vel_count));
        print_perf("Velocity 批量添加", vel_count, timer.elapsed_ms());

        timer.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), entity_count), std::span<const Health>(healths.data(), entity_count));
        print_perf("Health 批量添加", entity_count, timer.elapsed_ms());

        timer.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), name_count), std::span<const Name>(names.data(), name_count));
        print_perf("Name 批量添加", name_count, timer.elapsed_ms());

        timer.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count), std::span<const Damage>(damages.data(), vel_count));
        print_perf("Damage 批量添加", vel_count, timer.elapsed_ms());

        timer.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count), std::span<const Armor>(armors.data(), vel_count));
        print_perf("Armor 批量添加", vel_count, timer.elapsed_ms());

        timer.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), speed_count), std::span<const Speed>(speeds.data(), speed_count));
        print_perf("Speed 批量添加", speed_count, timer.elapsed_ms());

        // 数据分布
        std::cout << "\n  ┌─ 数据分布\n";
        std::cout << "  │ Position: " << entity_count << " (100%)\n";
        std::cout << "  │ Velocity: " << vel_count << " (50%)\n";
        std::cout << "  │ Health:   " << entity_count << " (100%)\n";
        std::cout << "  │ Name:     " << name_count << " (10%)\n";
        std::cout << "  │ Damage:   " << vel_count << " (50%)\n";
        std::cout << "  │ Armor:    " << vel_count << " (50%)\n";
        std::cout << "  │ Speed:    " << speed_count << " (25%)\n";

        // ---- 14.2 单组件逐个添加 ----
        print_perf_sub("14.2 单组件逐个添加");
        {
            const size_t add_count = 100000;
            ecs::manager mgr2;
            mgr2.disable_track_changes();
            mgr2.disable_comp_signals();
            mgr2.append_preallocated_entities(add_count);
            class_pool<entity> add_ents;
            add_ents.increase_capacity(add_count);
            for (size_t i = 0; i < add_count; ++i)
                add_ents.emplace_back(mgr2.create_entity());

            timer.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Position{1.0f, 2.0f, 3.0f});
            print_perf("Position 逐个添加", add_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Velocity{1.0f, 0.0f, 0.0f});
            print_perf("Velocity 逐个添加", add_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Health{100, 100});
            print_perf("Health 逐个添加", add_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Damage{10});
            print_perf("Damage 逐个添加", add_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Armor{50});
            print_perf("Armor 逐个添加", add_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Speed{5.0f});
            print_perf("Speed 逐个添加", add_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Name{"Test"});
            print_perf("Name 逐个添加", add_count, timer.elapsed_ms());
        }

        // ---- 14.3 单组件查询 ----
        print_perf_sub("14.3 单组件查询");
        {
            const size_t query_count = 100000;
            std::uniform_int_distribution<size_t> idx_dist(0, entity_count - 1);

            timer.reset();
            size_t hit = 0;
            for (size_t i = 0; i < query_count; ++i) {
                auto* p = ecss.get_ptr<Position>(entities[idx_dist(gen)]);
                if (p) { hit++; volatile float d = p->x; (void)d; }
            }
            print_perf("get_ptr 单点查询", hit, timer.elapsed_ms());

            timer.reset();
            size_t batch_hit = 0;
            {
                class_pool<entity> batch_ents;
                batch_ents.resize(query_count);
                for (size_t i = 0; i < query_count; ++i)
                    batch_ents[i] = entities[idx_dist(gen)];
                class_pool<Position*> results;
                results.resize(query_count);
                ecss.get_ptr_batch<Position>(batch_ents.data(), results.data(), query_count);
                for (size_t i = 0; i < query_count; ++i)
                    if (results[i]) { batch_hit++; volatile float d = results[i]->x; (void)d; }
            }
            print_perf("get_ptr_batch 批量查询", batch_hit, timer.elapsed_ms());

            timer.reset();
            size_t pf_hit = 0;
            {
                class_pool<entity> pf_ents;
                pf_ents.resize(query_count);
                for (size_t i = 0; i < query_count; ++i)
                    pf_ents[i] = entities[idx_dist(gen)];
                constexpr size_t chunk = 16;
                for (size_t base = 0; base < query_count; base += chunk) {
                    size_t n = base + chunk <= query_count ? chunk : query_count - base;
                    for (size_t j = 0; j < n; ++j)
                        ecss.prefetch_ptr<Position>(pf_ents[base + j]);
                    for (size_t j = 0; j < n; ++j) {
                        auto* p = ecss.get_ptr<Position>(pf_ents[base + j]);
                        if (p) { pf_hit++; volatile float d = p->x; (void)d; }
                    }
                }
            }
            print_perf("prefetch+get 预取查询", pf_hit, timer.elapsed_ms());

            timer.reset();
            size_t trav = 0;
            ecss.view<Position>().for_each([&](Position& pos) {
                trav++;
                volatile float d = pos.x; (void)d;
            });
            print_perf("容器遍历 (for_each)", trav, timer.elapsed_ms());
        }

        // ---- 14.4 多组件视图查询 ----
        print_perf_sub("14.4 多组件视图查询");

        // 双组件
        timer.reset();
        size_t cnt_2a = 0;
        ecss.view<Position, Velocity>().for_each([&](Position& p, Velocity& v) {
            cnt_2a++;
            volatile float d = p.x * v.vx; (void)d;
        });
        print_perf("双组件 Pos+Vel", cnt_2a, timer.elapsed_ms());

        timer.reset();
        size_t cnt_2b = 0;
        ecss.view<Position, Health>().for_each([&](Position& p, Health& h) {
            cnt_2b++;
            volatile float d = p.x + static_cast<float>(h.current); (void)d;
        });
        print_perf("双组件 Pos+Hp", cnt_2b, timer.elapsed_ms());

        timer.reset();
        size_t cnt_2c = 0;
        ecss.view<Velocity, Health>().for_each([&](Velocity& v, Health& h) {
            cnt_2c++;
            volatile float d = v.vx + static_cast<float>(h.current); (void)d;
        });
        print_perf("双组件 Vel+Hp", cnt_2c, timer.elapsed_ms());

        // 三组件
        timer.reset();
        size_t cnt_3a = 0;
        ecss.view<Position, Velocity, Health>().for_each([&](Position& p, Velocity& v, Health& h) {
            cnt_3a++;
            volatile float d = p.x + v.vy + static_cast<float>(h.current); (void)d;
        });
        print_perf("三组件 Pos+Vel+Hp", cnt_3a, timer.elapsed_ms());

        timer.reset();
        size_t cnt_3b = 0;
        ecss.view<Position, Velocity, Damage>().for_each([&](Position& p, Velocity& v, Damage& dmg) {
            cnt_3b++;
            volatile float d = p.x + v.vx + static_cast<float>(dmg.amount); (void)d;
        });
        print_perf("三组件 Pos+Vel+Dmg", cnt_3b, timer.elapsed_ms());

        // 四组件
        timer.reset();
        size_t cnt_4 = 0;
        ecss.view<Position, Velocity, Health, Name>().for_each([&](Position& p, Velocity& v, Health& h, Name& n) {
            cnt_4++;
            volatile float d = p.x + v.vy + static_cast<float>(h.current) + static_cast<float>(n.value.size()); (void)d;
        });
        print_perf("四组件 Pos+Vel+Hp+Name", cnt_4, timer.elapsed_ms());

        // 五组件
        timer.reset();
        size_t cnt_5 = 0;
        ecss.view<Position, Velocity, Health, Damage, Armor>().for_each([&](Position& p, Velocity& v, Health& h, Damage& dmg, Armor& arm) {
            cnt_5++;
            volatile float d = p.x + v.vx + static_cast<float>(h.current + dmg.amount + arm.defense); (void)d;
        });
        print_perf("五组件 Pos+Vel+Hp+Dmg+Armor", cnt_5, timer.elapsed_ms());

        // 六组件
        timer.reset();
        size_t cnt_6 = 0;
        ecss.view<Position, Velocity, Health, Damage, Armor, Speed>().for_each([&](Position& p, Velocity& v, Health& h, Damage& dmg, Armor& arm, Speed& spd) {
            cnt_6++;
            volatile float d = p.x + v.vx + static_cast<float>(h.current) + static_cast<float>(dmg.amount) + static_cast<float>(arm.defense) + spd.value; (void)d;
        });
        print_perf("六组件 Pos+Vel+Hp+Dmg+Armor+Spd", cnt_6, timer.elapsed_ms());

        // 带entity
        timer.reset();
        size_t cnt_ent = 0;
        ecss.view<Position, Velocity>().for_each([&](entity e, Position& p, Velocity& v) {
            cnt_ent++;
            volatile float d = p.x + v.vx + static_cast<float>(e.parts_.index_); (void)d;
        });
        print_perf("双组件带entity Pos+Vel", cnt_ent, timer.elapsed_ms());

        timer.reset();
        size_t cnt_ent3 = 0;
        ecss.view<Position, Velocity, Health>().for_each([&](entity e, Position& p, Velocity& v, Health& h) {
            cnt_ent3++;
            volatile float d = p.x + v.vx + static_cast<float>(h.current) + static_cast<float>(e.parts_.index_); (void)d;
        });
        print_perf("三组件带entity Pos+Vel+Hp", cnt_ent3, timer.elapsed_ms());

        // ---- 14.5 排除/可选/OR视图 ----
        print_perf_sub("14.5 排除 / 可选 / OR 视图");

        timer.reset();
        size_t cnt_excl = 0;
        ecss.view<Position>(ecs::without<Velocity>).for_each([&](Position& p) {
            cnt_excl++; (void)p;
        });
        print_perf("exclude 排除视图", cnt_excl, timer.elapsed_ms());

        timer.reset();
        size_t cnt_with = 0;
        ecss.view<Position>(ecs::with<Health>).for_each([&](Position& p, Health* hp) {
            cnt_with++; (void)p; (void)hp;
        });
        print_perf("with 可选视图", cnt_with, timer.elapsed_ms());

        timer.reset();
        size_t cnt_or = 0;
        ecss.view_or<Position, Velocity>().for_each([&](entity, Position* p, Velocity* v) {
            cnt_or++; (void)p; (void)v;
        });
        print_perf("or_view OR视图", cnt_or, timer.elapsed_ms());

        timer.reset();
        size_t cnt_any = 0;
        ecss.view_any_of<Position, Velocity, Health>().for_each([&](Position* p, Velocity* v, Health* h) {
            cnt_any++; (void)p; (void)v; (void)h;
        });
        print_perf("any_of 任意匹配视图", cnt_any, timer.elapsed_ms());

        // ---- 14.6 Group 系统 ----
        print_perf_sub("14.6 Group 系统");

        timer.reset();
        size_t cnt_grp = 0;
        auto g = ecss.group<Position, Velocity>();
        g.for_each([&](Position& p, Velocity& v) {
            cnt_grp++;
            volatile float d = p.x * v.vx; (void)d;
        });
        print_perf("Non-Owning Group", cnt_grp, timer.elapsed_ms());

        timer.reset();
        size_t cnt_own = 0;
        auto og = ecss.group<Position, Velocity>(ecs::owned<Position>);
        og.for_each([&](Position& p, Velocity& v) {
            cnt_own++;
            volatile float d = p.x * v.vx; (void)d;
        });
        print_perf("Owning Group", cnt_own, timer.elapsed_ms());

        timer.reset();
        size_t cnt_reo = 0;
        auto rg = ecss.group<Position, Velocity>(ecs::reorder<Position>);
        rg.for_each([&](Position& p, Velocity& v) {
            cnt_reo++;
            volatile float d = p.x * v.vx; (void)d;
        });
        print_perf("Reorder Group", cnt_reo, timer.elapsed_ms());

        // ---- 14.7 运行时视图 ----
        print_perf_sub("14.7 运行时视图");

        timer.reset();
        size_t cnt_rt2 = 0;
        auto rv2 = ecss.runtime_view_create({
            type_id::get_type_id<Position>(),
            type_id::get_type_id<Velocity>()
        });
        rv2.for_each([&](entity e) {
            cnt_rt2++;
            auto* p = ecss.get_ptr_fast<Position>(e);
            auto* v = ecss.get_ptr_fast<Velocity>(e);
            volatile float d = p->x * v->vx; (void)d;
        });
        print_perf("runtime_view 双组件", cnt_rt2, timer.elapsed_ms());

        timer.reset();
        size_t cnt_rt3 = 0;
        auto rv3 = ecss.runtime_view_create({
            type_id::get_type_id<Position>(),
            type_id::get_type_id<Velocity>(),
            type_id::get_type_id<Health>()
        });
        rv3.for_each([&](entity e) {
            cnt_rt3++;
            auto* p = ecss.get_ptr_fast<Position>(e);
            auto* v = ecss.get_ptr_fast<Velocity>(e);
            auto* h = ecss.get_ptr_fast<Health>(e);
            volatile float d = p->x + v->vx + static_cast<float>(h->current); (void)d;
        });
        print_perf("runtime_view 三组件", cnt_rt3, timer.elapsed_ms());

        timer.reset();
        size_t cnt_rt_excl = 0;
        auto rv_excl = ecss.runtime_view_create(
            { type_id::get_type_id<Position>() },
            { type_id::get_type_id<Velocity>() }
        );
        rv_excl.for_each([&](entity e) {
            cnt_rt_excl++;
            volatile float d = static_cast<float>(e.parts_.index_); (void)d;
        });
        print_perf("runtime_view 排除视图", cnt_rt_excl, timer.elapsed_ms());

        // ---- 14.8 视图扩展 ----
        print_perf_sub("14.8 视图扩展 (page/sort/group/track)");

        timer.reset();
        size_t cnt_page = 0;
        {
            auto mv = ecss.view<Position, Velocity>();
            mv.page(0, cnt_2a).for_each([&](Position&, Velocity&) { cnt_page++; });
        }
        print_perf("page 分页视图", cnt_page, timer.elapsed_ms());

        timer.reset();
        size_t cnt_changed = 0;
        {
            auto mv = ecss.view<Position, Velocity>();
            auto cv = mv.track_changes();
            cv.for_each([&](Position&, Velocity&) { cnt_changed++; });
        }
        print_perf("track_changes 变更检测", cnt_changed, timer.elapsed_ms());

        // 排序/分组 (小数据集)
        {
            constexpr size_t sort_n = 10000;
            ecs::manager sort_mgr;
            for (size_t i = 0; i < sort_n; ++i) {
                auto e = sort_mgr.create_entity();
                sort_mgr.add(e, Position{static_cast<float>(rand() % 1000), static_cast<float>(rand() % 1000), 0});
                sort_mgr.add(e, Velocity{static_cast<float>(rand() % 100), 0, 0});
            }

            timer.reset();
            size_t cnt_sorted = 0;
            {
                auto mv = sort_mgr.view<Position, Velocity>();
                auto sv = mv.sorted_by_component<Position>(
                    [](const Position& a, const Position& b) { return a.x < b.x; });
                sv.for_each([&](Position&, Velocity&) { cnt_sorted++; });
            }
            print_perf("sorted_by_component 排序", cnt_sorted, timer.elapsed_ms());

            timer.reset();
            size_t cnt_grouped = 0;
            {
                auto sv = sort_mgr.view<Position>();
                auto gv = sv.sorted_by_component_value(
                    [](Position& p) -> int { return static_cast<int>(p.x) / 10; });
                gv.for_each([&](Position&) { cnt_grouped++; });
            }
            print_perf("sorted_by_component_value 分组", cnt_grouped, timer.elapsed_ms());
        }

        // ---- 14.9 过滤视图 ----
        print_perf_sub("14.9 过滤视图");

        timer.reset();
        size_t cnt_filt = 0;
        {
            auto fv = ecss.view_filtered<Position>([](Position& p) { return p.x > 0.0f; });
            fv.for_each([&](Position&) { cnt_filt++; });
        }
        print_perf("filter_view 过滤视图", cnt_filt, timer.elapsed_ms());

        size_t cnt_fand = 0;
        {
            auto fa = ecss.view_filtered<Position>([](Position& p) { return p.x > 0.0f; }).and_<Velocity>();
            timer.reset();
            constexpr int warmup = 50;
            for (int iter = 0; iter < warmup; ++iter)
            {
                cnt_fand = 0;
                fa.for_each([&](Position&, Velocity&) { cnt_fand++; });
            }
            double elapsed = timer.elapsed_ms() / warmup;
            print_perf("filter_and 过滤且视图", cnt_fand, elapsed);
        }

        size_t cnt_for = 0;
        {
            auto fo = ecss.view_filtered<Position>([](Position& p) { return p.x > 0.0f; }).or_<Velocity>();
            timer.reset();
            constexpr int warmup = 50;
            for (int iter = 0; iter < warmup; ++iter)
            {
                cnt_for = 0;
                fo.for_each([&](entity, Position* p, Velocity* v) {
                    cnt_for++; (void)p; (void)v;
                });
            }
            double elapsed = timer.elapsed_ms() / warmup;
            print_perf("filter_or 过滤或视图", cnt_for, elapsed);
        }

        // ---- 14.10 单点查询接口 ----
        print_perf_sub("14.10 单点查询接口");

        {
            std::uniform_int_distribution<size_t> idx_dist(0, entity_count - 1);

            timer.reset();
            for (size_t i = 0; i < 100000; ++i) {
                auto* p = ecss.get_ptr<Position>(entities[idx_dist(gen)]);
                volatile bool b = (p != nullptr); (void)b;
            }
            print_perf("get_ptr 随机查询", 100000, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < 100000; ++i) {
                auto* p = ecss.get_ptr_fast<Position>(entities[idx_dist(gen)]);
                volatile bool b = (p != nullptr); (void)b;
            }
            print_perf("get_ptr_fast 快速查询", 100000, timer.elapsed_ms());
        }

        // ---- 14.11 实体/组件操作 ----
        print_perf_sub("14.11 实体 / 组件操作");

        {
            const size_t op_count = 100000;
            ecs::manager mgr3;
            mgr3.append_preallocated_entities(op_count * 2);
            class_pool<entity> op_ents;
            op_ents.increase_capacity(op_count);

            timer.reset();
            for (size_t i = 0; i < op_count; ++i)
                op_ents.emplace_back(mgr3.create_entity());
            print_perf("实体创建", op_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < op_count; ++i)
                mgr3.add(op_ents[i], Position{1.0f, 0.0f, 0.0f});
            print_perf("组件添加 add()", op_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < op_count; ++i)
                mgr3.hard_remove<Position>(op_ents[i]);
            print_perf("组件硬删除 hard_remove", op_count, timer.elapsed_ms());

            for (size_t i = 0; i < op_count; ++i)
                mgr3.add(op_ents[i], Velocity{1.0f, 0.0f, 0.0f});

            timer.reset();
            for (size_t i = 0; i < op_count; ++i)
                mgr3.soft_remove<Velocity>(op_ents[i]);
            print_perf("组件软删除 soft_remove", op_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < op_count / 2; ++i)
                mgr3.delete_entity(op_ents[i]);
            print_perf("实体删除 delete_entity", op_count / 2, timer.elapsed_ms());
        }

        // ---- 14.12 class_pool 性能 ----
        print_perf_sub("14.12 class_pool 容器接口");
        {
            const size_t cp_count = 500000;

            // emplace_back
            timer.reset();
            class_pool<int> cp_em;
            cp_em.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i)
                cp_em.emplace_back(static_cast<int>(i));
            print_perf("class_pool emplace_back", cp_count, timer.elapsed_ms());

            // push_back_unchecked
            timer.reset();
            class_pool<int> cp_pb;
            cp_pb.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i)
                cp_pb.push_back_unchecked(static_cast<int>(i));
            print_perf("class_pool push_back_unchecked", cp_count, timer.elapsed_ms());

            // 范围构造（批量构造，公开接口）
            {
                class_pool<int> src(cp_count, 42);
                timer.reset();
                class_pool<int> cp_ab(src.begin(), src.end());
                print_perf("class_pool 范围构造(批量)", cp_count, timer.elapsed_ms());
            }

            // resize
            timer.reset();
            class_pool<int> cp_rz;
            cp_rz.resize(cp_count);
            print_perf("class_pool resize(cap)", cp_count, timer.elapsed_ms());

            // resize with value
            timer.reset();
            class_pool<int> cp_rzv;
            cp_rzv.resize(cp_count, 77);
            print_perf("class_pool resize(cap,val)", cp_count, timer.elapsed_ms());

            // increase_capacity(cap, value)
            timer.reset();
            class_pool<int> cp_ic;
            cp_ic.emplace_back(1);
            cp_ic.increase_capacity(cp_count, 99);
            print_perf("class_pool increase_capacity(cap,val)", cp_count, timer.elapsed_ms());

            // emplace_at (sparse)
            timer.reset();
            class_pool<int> cp_ea;
            cp_ea.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i)
                cp_ea.emplace_at(i, static_cast<int>(i));
            print_perf("class_pool emplace_at", cp_count, timer.elapsed_ms());

            // sparse_emplace_at
            timer.reset();
            class_pool<int> cp_sea;
            cp_sea.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i)
                cp_sea.sparse_emplace_at(i, static_cast<int>(i));
            print_perf("class_pool sparse_emplace_at", cp_count, timer.elapsed_ms());

            // sparse_erase_at
            timer.reset();
            for (size_t i = 0; i < cp_count; i += 2)
                cp_sea.sparse_erase_at(i);
            print_perf("class_pool sparse_erase_at (隔位)", cp_count / 2, timer.elapsed_ms());

            // erase
            timer.reset();
            class_pool<int> cp_er = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            for (int iter = 0; iter < 100000; ++iter) {
                class_pool<int> tmp = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
                tmp.erase(std::next(tmp.begin(), 5));
            }
            print_perf("class_pool erase", 100000, timer.elapsed_ms());

            // emplace (insert)
            timer.reset();
            for (int iter = 0; iter < 100000; ++iter) {
                class_pool<int> tmp = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
                tmp.emplace(std::next(tmp.begin(), 5), 99);
            }
            print_perf("class_pool emplace(insert)", 100000, timer.elapsed_ms());

            // pop_back
            timer.reset();
            class_pool<int> cp_pop;
            cp_pop.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_pop.emplace_back(static_cast<int>(i));
            for (size_t i = 0; i < cp_count; ++i) cp_pop.pop_back();
            print_perf("class_pool pop_back", cp_count, timer.elapsed_ms());

            // swap
            timer.reset();
            class_pool<int> cp_s1 = {1, 2, 3}, cp_s2 = {4, 5, 6, 7, 8};
            for (int iter = 0; iter < 1000000; ++iter) {
                cp_s1.swap(cp_s2);
            }
            print_perf("class_pool swap", 1000000, timer.elapsed_ms());

            // shrink_to_fit
            timer.reset();
            class_pool<int> cp_sf;
            cp_sf.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count / 2; ++i) cp_sf.emplace_back(static_cast<int>(i));
            cp_sf.shrink_to_fit();
            print_perf("class_pool shrink_to_fit", cp_count / 2, timer.elapsed_ms());

            // reduce_capacity
            timer.reset();
            class_pool<int> cp_rc;
            cp_rc.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_rc.emplace_back(static_cast<int>(i));
            cp_rc.reduce_capacity(cp_count / 2);
            print_perf("class_pool reduce_capacity", cp_count, timer.elapsed_ms());

            // reduce_capacity(dst)
            timer.reset();
            class_pool<int> cp_src2;
            cp_src2.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_src2.emplace_back(static_cast<int>(i));
            class_pool<int> cp_dst2;
            cp_src2.reduce_capacity(cp_count / 2, cp_dst2);
            print_perf("class_pool reduce_capacity(dst)", cp_count / 2, timer.elapsed_ms());

            // clear
            timer.reset();
            class_pool<int> cp_cl;
            cp_cl.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_cl.emplace_back(static_cast<int>(i));
            cp_cl.clear();
            print_perf("class_pool clear", cp_count, timer.elapsed_ms());

            // 遍历
            timer.reset();
            class_pool<int> cp_tr;
            cp_tr.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_tr.emplace_back(static_cast<int>(i));
            volatile long long sum = 0;
            for (auto it = cp_tr.begin(); it != cp_tr.end(); ++it) sum += *it;
            print_perf("class_pool 遍历 begin/end", cp_count, timer.elapsed_ms());

            // count (sparse)
            timer.reset();
            class_pool<int> cp_cnt;
            cp_cnt.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_cnt.emplace_back(static_cast<int>(i));
            for (size_t i = 0; i < cp_count; i += 2) cp_cnt.sparse_erase_at(i);
            size_t cnt_cp = cp_cnt.count();
            print_perf("class_pool count (sparse)", cnt_cp, timer.elapsed_ms());

            // is_constructed_at
            timer.reset();
            volatile bool bv = false;
            for (size_t i = 0; i < cp_count; ++i) bv = cp_tr.is_constructed_at(i);
            print_perf("class_pool is_constructed_at", cp_count, timer.elapsed_ms());
        }

        // ---- 14.13 void_any 性能 ----
        print_perf_sub("14.13 void_any 类型擦除容器");
        {
            const size_t va_count = 1000000;

            // 构造
            timer.reset();
            class_pool<void_any> va_pool;
            va_pool.increase_capacity(va_count);
            for (size_t i = 0; i < va_count; ++i)
                va_pool.emplace_back(static_cast<int>(i));
            print_perf("void_any 构造(T&&)", va_count, timer.elapsed_ms());

            // set
            timer.reset();
            for (size_t i = 0; i < va_count; ++i)
                va_pool[i].set(static_cast<double>(i));
            print_perf("void_any set()", va_count, timer.elapsed_ms());

            // has_value
            timer.reset();
            size_t hv = 0;
            for (size_t i = 0; i < va_count; ++i)
                if (va_pool[i].has_value()) ++hv;
            volatile size_t hv_sink = hv;
            (void)hv_sink;
            print_perf("void_any has_value()", va_count, timer.elapsed_ms());

            // type_id
            timer.reset();
            volatile int tid = 0;
            for (size_t i = 0; i < va_count; ++i)
                tid = va_pool[i].type_id();
            print_perf("void_any type_id()", va_count, timer.elapsed_ms());

            // get_ptr
            timer.reset();
            volatile double* dp = nullptr;
            for (size_t i = 0; i < va_count; ++i)
                dp = va_pool[i].get_ptr<double>();
            print_perf("void_any get_ptr<T>()", va_count, timer.elapsed_ms());

            // fast_get_ptr
            timer.reset();
            for (size_t i = 0; i < va_count; ++i)
                dp = va_pool[i].fast_get_ptr<double>();
            print_perf("void_any fast_get_ptr<T>()", va_count, timer.elapsed_ms());

            // get_ptr_unchecked
            timer.reset();
            for (size_t i = 0; i < va_count; ++i)
                dp = va_pool[i].get_ptr_unchecked<double>();
            print_perf("void_any get_ptr_unchecked<T>()", va_count, timer.elapsed_ms());

            // get
            timer.reset();
            for (size_t i = 0; i < va_count; ++i) {
                double v = va_pool[i].get<double>();
                (void)v;
            }
            print_perf("void_any get<T>()", va_count, timer.elapsed_ms());

            // reset
            timer.reset();
            for (size_t i = 0; i < va_count; ++i)
                va_pool[i].reset();
            print_perf("void_any reset()", va_count, timer.elapsed_ms());

            // 拷贝构造
            timer.reset();
            void_any va_src(42);
            for (size_t i = 0; i < va_count; ++i) {
                void_any va_copy_obj(va_src);
            }
            print_perf("void_any 拷贝构造", va_count, timer.elapsed_ms());

            // 移动构造
            timer.reset();
            for (size_t i = 0; i < va_count; ++i) {
                void_any va_tmp(42);
                void_any va_move(std::move(va_tmp));
            }
            print_perf("void_any 移动构造", va_count, timer.elapsed_ms());
        }

        // ---- 14.14 memory_pool 性能 ----
        print_perf_sub("14.14 memory_pool 内存池");
        {
            const size_t mp_count = 1000000;

            // allocate/deallocate
            timer.reset();
            memory_pool mp(1024 * 1024);
            class_pool<void*> ptrs;
            ptrs.increase_capacity(mp_count);
            for (size_t i = 0; i < mp_count; ++i)
                ptrs.emplace_back(mp.allocate(64));
            print_perf("memory_pool allocate(64)", mp_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < mp_count; ++i)
                mp.deallocate(ptrs[i]);
            print_perf("memory_pool deallocate", mp_count, timer.elapsed_ms());

            // construct/destroy
            timer.reset();
            class_pool<int*> iptrs;
            iptrs.increase_capacity(mp_count);
            for (size_t i = 0; i < mp_count; ++i)
                iptrs.emplace_back(mp.construct<int>(static_cast<int>(i)));
            print_perf("memory_pool construct<int>", mp_count, timer.elapsed_ms());

            timer.reset();
            for (size_t i = 0; i < mp_count; ++i)
                mp.destroy(iptrs[i]);
            print_perf("memory_pool destroy<int>", mp_count, timer.elapsed_ms());

            // increase_capacity
            timer.reset();
            memory_pool mp2(4096);
            mp2.increase_capacity(8 * 1024 * 1024);
            print_perf("memory_pool increase_capacity", 1, timer.elapsed_ms());

            // reduce_capacity
            timer.reset();
            mp2.reduce_capacity(0);
            print_perf("memory_pool reduce_capacity", 1, timer.elapsed_ms());

            // reset
            timer.reset();
            memory_pool mp3(4096);
            for (size_t i = 0; i < 10000; ++i) { void* p = mp3.allocate(64); (void)p; }
            mp3.reset();
            print_perf("memory_pool reset", 10000, timer.elapsed_ms());

            // total_allocated / total_used / empty / chunk_size
            timer.reset();
            volatile size_t ta = 0, tu = 0;
            volatile bool em = false;
            volatile size_t cs = 0;
            for (int i = 0; i < 1000000; ++i) {
                ta = mp3.total_allocated();
                tu = mp3.total_used();
                em = mp3.empty();
                cs = mp3.chunk_size();
            }
            print_perf("memory_pool 状态查询", 1000000, timer.elapsed_ms());
        }

        // ---- 14.15 operating_message 性能 ----
        print_perf_sub("14.15 operating_message 操作消息");
        {
            const size_t om_count = 1000000;

            // write_message
            timer.reset();
            operating_message om1;
            for (size_t i = 0; i < om_count; ++i) {
                om1.reset();
                om1.write_message(true, "msg", i);
            }
            print_perf("operating_message write_message", om_count, timer.elapsed_ms());

            // write_message_fmt
            timer.reset();
            operating_message om2;
            for (size_t i = 0; i < om_count; ++i) {
                om2.reset();
                om2.write_message_fmt(true, "fmt: {} + {}", i, i + 1);
            }
            print_perf("operating_message write_message_fmt", om_count, timer.elapsed_ms());

            // operator+=(string_view)
            timer.reset();
            for (size_t i = 0; i < om_count; ++i) {
                operating_message om3;
                om3 += "hello";
                om3 += " world";
            }
            print_perf("operating_message operator+=(str)", om_count * 2, timer.elapsed_ms());

            // operator+=(operating_message)
            timer.reset();
            for (size_t i = 0; i < om_count; ++i) {
                operating_message om4, om5;
                om5.write_message(true, "src");
                om4 += std::move(om5);
            }
            print_perf("operating_message operator+=(om&&)", om_count, timer.elapsed_ms());

            // reset / clear_message / set_switch_bool / get_switch_bool
            timer.reset();
            operating_message om6;
            for (size_t i = 0; i < om_count; ++i) {
                om6.reset();
                om6.set_switch_bool(false);
                volatile bool b = om6.get_switch_bool();
                om6.clear_message();
                (void)b;
            }
            print_perf("operating_message reset/clear/switch", om_count, timer.elapsed_ms());

            // read_message / operator bool
            timer.reset();
            om6.reset();
            om6.write_message(true, "test");
            for (size_t i = 0; i < om_count; ++i) {
                volatile bool b = (bool)om6;
                auto sv = om6.read_message();
                (void)sv; (void)b;
            }
            print_perf("operating_message read/bool", om_count, timer.elapsed_ms());
        }

        // ---- 14.16 id_allocation 性能 ----
        print_perf_sub("14.16 id_allocation ID分配器");
        {
            const size_t id_count = 1000000;

            // get_id
            timer.reset();
            id_allocation<int> ida;
            volatile int id_sink = 0;
            for (size_t i = 0; i < id_count; ++i)
                id_sink = ida.get_id();
            (void)id_sink;
            print_perf("id_allocation get_id", id_count, timer.elapsed_ms());

            // free_id
            timer.reset();
            class_pool<int> ids;
            ids.increase_capacity(id_count);
            for (size_t i = 0; i < id_count; ++i) ids.emplace_back(ida.get_id());
            for (size_t i = 0; i < id_count; ++i) ida.free_id(ids[i]);
            print_perf("id_allocation free_id", id_count, timer.elapsed_ms());

            // 回收再分配
            timer.reset();
            volatile int id_sink2 = 0;
            for (size_t i = 0; i < id_count; ++i)
                id_sink2 = ida.get_id();
            (void)id_sink2;
            print_perf("id_allocation 回收再分配", id_count, timer.elapsed_ms());

            // total_number_of_ids / maximum_id
            timer.reset();
            volatile size_t tn = 0, mx = 0;
            for (int i = 0; i < 1000000; ++i) {
                tn = ida.total_number_of_ids();
                mx = ida.maximum_id();
            }
            print_perf("id_allocation total/maximum", 1000000, timer.elapsed_ms());
        }

        // ---- 14.17 信号系统性能 ----
        print_perf_sub("14.17 生命周期信号系统");
        {
            const size_t sig_count = 500000;

            // 即时信号：实体创建/销毁
            {
                ecs::manager mgr;
                mgr.append_preallocated_entities(sig_count * 2);
                size_t created = 0, destroyed = 0;
                mgr.set_on_entity_created([](entity, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &created);
                mgr.set_on_entity_destroyed([](entity, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &destroyed);

                timer.reset();
                class_pool<entity> ents;
                ents.increase_capacity(sig_count);
                for (size_t i = 0; i < sig_count; ++i)
                    ents.emplace_back(mgr.create_entity());
                print_perf("即时信号 entity_created", sig_count, timer.elapsed_ms());

                timer.reset();
                for (size_t i = 0; i < sig_count; ++i)
                    mgr.delete_entity(ents[i]);
                print_perf("即时信号 entity_destroyed", sig_count, timer.elapsed_ms());
            }

            // 即时信号：组件 add/remove
            {
                ecs::manager mgr;
                mgr.append_preallocated_entities(sig_count);
                size_t added = 0, removed = 0;
                mgr.set_on_add<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &added);
                mgr.set_on_remove<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &removed);

                class_pool<entity> ents;
                ents.increase_capacity(sig_count);
                for (size_t i = 0; i < sig_count; ++i) ents.emplace_back(mgr.create_entity());

                timer.reset();
                for (size_t i = 0; i < sig_count; ++i)
                    mgr.add(ents[i], Position{1.0f, 0, 0});
                print_perf("即时信号 on_add<Position>", sig_count, timer.elapsed_ms());

                timer.reset();
                for (size_t i = 0; i < sig_count; ++i)
                    mgr.hard_remove<Position>(ents[i]);
                print_perf("即时信号 on_remove<Position>", sig_count, timer.elapsed_ms());
            }

            // 延迟信号：flush_entity_signals
            {
                ecs::manager mgr;
                mgr.disable_comp_signals();
                mgr.append_preallocated_entities(sig_count);
                class_pool<entity> ents;
                ents.increase_capacity(sig_count);
                for (size_t i = 0; i < sig_count; ++i) ents.emplace_back(mgr.create_entity());
                for (size_t i = 0; i < sig_count / 2; ++i) mgr.delete_entity(ents[i]);

                timer.reset();
                size_t created = 0, destroyed = 0;
                mgr.flush_entity_signals([&](uint32_t type, uint32_t) noexcept {
                    if (type == 0) created++;
                    else destroyed++;
                });
                print_perf("flush_entity_signals", sig_count, timer.elapsed_ms());
            }

            // 延迟信号：flush_component_signals
            {
                ecs::manager mgr;
                mgr.disable_track_changes();
                mgr.append_preallocated_entities(sig_count);
                class_pool<entity> ents;
                ents.increase_capacity(sig_count);
                for (size_t i = 0; i < sig_count; ++i) ents.emplace_back(mgr.create_entity());
                for (size_t i = 0; i < sig_count; ++i) mgr.add(ents[i], Position{1.0f, 0, 0});
                for (size_t i = 0; i < sig_count / 2; ++i) mgr.hard_remove<Position>(ents[i]);

                timer.reset();
                size_t added = 0, removed = 0;
                mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
                    if (type == 0) added++;
                    else removed++;
                });
                print_perf("flush_component_signals", sig_count + sig_count / 2, timer.elapsed_ms());
            }

            // has_pending_signals 查询
            timer.reset();
            ecs::manager mgr_chk;
            volatile bool hp = false;
            for (int i = 0; i < 1000000; ++i) {
                hp = mgr_chk.has_pending_entity_signals();
                hp = mgr_chk.has_pending_component_signals();
            }
            print_perf("has_pending_signals 查询", 1000000 * 2, timer.elapsed_ms());

            // enable/disable 信号开关
            timer.reset();
            for (int i = 0; i < 1000000; ++i) {
                mgr_chk.disable_comp_signals();
                mgr_chk.enable_comp_signals();
                mgr_chk.disable_track_changes();
                mgr_chk.enable_track_changes();
            }
            print_perf("enable/disable 信号开关", 1000000 * 4, timer.elapsed_ms());
        }

        // ---- 14.18 排序/重排接口性能 ----
        print_perf_sub("14.18 排序 / 重排接口");
        {
            constexpr size_t sort_n = 100000;
            ecs::manager sort_mgr;
            sort_mgr.append_preallocated_entities(sort_n);
            class_pool<entity> sort_ents;
            sort_ents.increase_capacity(sort_n);
            for (size_t i = 0; i < sort_n; ++i) {
                sort_ents.emplace_back(sort_mgr.create_entity());
                sort_mgr.add(sort_ents[i], Position{static_cast<float>(rand() % 10000), 0, 0});
                sort_mgr.add(sort_ents[i], Velocity{static_cast<float>(rand() % 1000), 0, 0});
            }

            // sort_entities_by_component
            timer.reset();
            sort_mgr.sort_entities_by_component<Position>(
                [](const Position& a, const Position& b) { return a.x < b.x; });
            print_perf("sort_entities_by_component", sort_n, timer.elapsed_ms());

            // reorder_by_component
            timer.reset();
            sort_mgr.reorder_by_component<Position, Velocity>(
                [](const Velocity& a, const Velocity& b) { return a.vx < b.vx; });
            print_perf("reorder_by_component", sort_n, timer.elapsed_ms());

            // sort_component_container
            timer.reset();
            sort_mgr.sort_component_container<Position>(
                [](const Position& a, const Position& b) { return a.x < b.x; });
            print_perf("sort_component_container", sort_n, timer.elapsed_ms());

            // single_view sorted_by_component
            timer.reset();
            {
                auto sv = sort_mgr.view<Position>();
                auto ssv = sv.sorted_by_component(
                    [](const Position& a, const Position& b) { return a.x < b.x; });
                size_t cnt = 0;
                ssv.for_each([&](Position&) { cnt++; });
                print_perf("single_view sorted_by_component", cnt, timer.elapsed_ms());
            }

            // multi_view sorted_by_component
            timer.reset();
            {
                auto mv = sort_mgr.view<Position, Velocity>();
                auto msv = mv.sorted_by_component<Position>(
                    [](const Position& a, const Position& b) { return a.x < b.x; });
                size_t cnt = 0;
                msv.for_each([&](Position&, Velocity&) { cnt++; });
                print_perf("multi_view sorted_by_component", cnt, timer.elapsed_ms());
            }

            // sorted_by_component_value 分组
            timer.reset();
            {
                auto sv = sort_mgr.view<Position>();
                auto gv = sv.sorted_by_component_value(
                    [](Position& p) -> int { return static_cast<int>(p.x) / 1000; });
                size_t groups = 0;
                gv.for_each_group([&](int, size_t, size_t) { groups++; });
                print_perf("sorted_by_component_value 分组", groups, timer.elapsed_ms());
            }
        }

        // ---- 14.19 其他管理器接口性能 ----
        print_perf_sub("14.19 其他管理器接口");
        {
            const size_t misc_count = 500000;

            // is_entity_valid
            timer.reset();
            ecs::manager mgr_m;
            mgr_m.append_preallocated_entities(misc_count);
            class_pool<entity> ents_m;
            ents_m.increase_capacity(misc_count);
            for (size_t i = 0; i < misc_count; ++i) ents_m.emplace_back(mgr_m.create_entity());
            volatile bool ev = false;
            for (size_t i = 0; i < misc_count; ++i) ev = mgr_m.is_entity_valid(ents_m[i]);
            print_perf("is_entity_valid", misc_count, timer.elapsed_ms());

            // get_entity_mask
            timer.reset();
            volatile uint64_t em = 0;
            for (size_t i = 0; i < misc_count; ++i) em = mgr_m.get_entity_mask(ents_m[i]);
            print_perf("get_entity_mask", misc_count, timer.elapsed_ms());

            // get_component_bit
            timer.reset();
            volatile uint64_t cb = 0;
            for (int i = 0; i < 1000000; ++i) cb = mgr_m.get_component_bit<Position>();
            print_perf("get_component_bit", 1000000, timer.elapsed_ms());

            // get_component_meta
            timer.reset();
            volatile const ecs::component_meta* cm = nullptr;
            int pid = type_id::get_type_id<Position>();
            for (int i = 0; i < 1000000; ++i) cm = mgr_m.get_component_meta(pid);
            print_perf("get_component_meta", 1000000, timer.elapsed_ms());

            // get_single_class_set
            timer.reset();
            volatile single_class_set* scs = nullptr;
            for (int i = 0; i < 1000000; ++i) scs = mgr_m.get_single_class_set<Position>();
            print_perf("get_single_class_set", 1000000, timer.elapsed_ms());

            // get_single_class_set_by_id
            timer.reset();
            for (int i = 0; i < 1000000; ++i) scs = mgr_m.get_single_class_set_by_id(pid);
            print_perf("get_single_class_set_by_id", 1000000, timer.elapsed_ms());

            // get_component_vector
            timer.reset();
            volatile class_pool<Position>* cv = nullptr;
            for (int i = 0; i < 1000000; ++i) cv = mgr_m.get_component_vector<Position>();
            print_perf("get_component_vector", 1000000, timer.elapsed_ms());

            // get_entity_manager
            timer.reset();
            for (int i = 0; i < 1000000; ++i) {
                auto& emr = mgr_m.get_entity_manager();
                (void)emr;
            }
            print_perf("get_entity_manager", 1000000, timer.elapsed_ms());

            // get_operating_message
            timer.reset();
            for (int i = 0; i < 1000000; ++i) {
                auto& omr = mgr_m.get_operating_message();
                (void)omr;
            }
            print_perf("get_operating_message", 1000000, timer.elapsed_ms());

            // reserve_component_capacity
            timer.reset();
            mgr_m.reserve_component_capacity<Health>(misc_count);
            print_perf("reserve_component_capacity", 1, timer.elapsed_ms());

            // delete_type_container
            for (size_t i = 0; i < misc_count; ++i) mgr_m.add(ents_m[i], Health{100, 100});
            timer.reset();
            mgr_m.delete_type_container<Health>();
            print_perf("delete_type_container", misc_count, timer.elapsed_ms());

            // hard_removec / soft_removec (链式)
            {
                ecs::manager mgr_ch;
                mgr_ch.append_preallocated_entities(misc_count);
                class_pool<entity> ents_ch;
                ents_ch.increase_capacity(misc_count);
                for (size_t i = 0; i < misc_count; ++i) {
                    ents_ch.emplace_back(mgr_ch.create_entity());
                    mgr_ch.add(ents_ch[i], Position{1.0f, 0, 0});
                }
                timer.reset();
                for (size_t i = 0; i < misc_count; ++i)
                    mgr_ch.hard_removec<Position>(ents_ch[i]);
                print_perf("hard_removec (链式)", misc_count, timer.elapsed_ms());

                for (size_t i = 0; i < misc_count; ++i) mgr_ch.add(ents_ch[i], Velocity{1.0f, 0, 0});
                timer.reset();
                for (size_t i = 0; i < misc_count; ++i)
                    mgr_ch.soft_removec<Velocity>(ents_ch[i]);
                print_perf("soft_removec (链式)", misc_count, timer.elapsed_ms());
            }

            // addc (链式)
            ecs::manager mgr_ac;
            mgr_ac.append_preallocated_entities(misc_count);
            class_pool<entity> ents_ac;
            ents_ac.increase_capacity(misc_count);
            for (size_t i = 0; i < misc_count; ++i) ents_ac.emplace_back(mgr_ac.create_entity());
            timer.reset();
            for (size_t i = 0; i < misc_count; ++i)
                mgr_ac.addc(ents_ac[i], Position{1.0f, 0, 0});
            print_perf("addc (链式)", misc_count, timer.elapsed_ms());

            // add(T, e) 反向参数
            ecs::manager mgr_rev;
            mgr_rev.append_preallocated_entities(misc_count);
            class_pool<entity> ents_rev;
            ents_rev.increase_capacity(misc_count);
            for (size_t i = 0; i < misc_count; ++i) ents_rev.emplace_back(mgr_rev.create_entity());
            timer.reset();
            for (size_t i = 0; i < misc_count; ++i)
                mgr_rev.add(Velocity{1.0f, 0, 0}, ents_rev[i]);
            print_perf("add(T, e) 反向参数", misc_count, timer.elapsed_ms());

            // prefetch_ptr_batch
            timer.reset();
            mgr_m.add(ents_m[0], Position{1, 0, 0});
            for (int iter = 0; iter < 100; ++iter)
                mgr_m.prefetch_ptr_batch<Position>(ents_m.data(), 64);
            print_perf("prefetch_ptr_batch", 100 * 64, timer.elapsed_ms());

            // single_class_set 直接接口
            {
                single_class_set scs_d;
                scs_d.increase_capacity(misc_count);
                class_pool<entity> ents_d;
                ents_d.increase_capacity(misc_count);
                for (size_t i = 0; i < misc_count; ++i) ents_d.emplace_back(entity(static_cast<uint32_t>(i), 1));

                timer.reset();
                for (size_t i = 0; i < misc_count; ++i)
                    scs_d.add(ents_d[i], Position{1.0f, 0, 0});
                print_perf("single_class_set add", misc_count, timer.elapsed_ms());

                timer.reset();
                for (size_t i = 0; i < misc_count; ++i) {
                    auto* p = scs_d.get_ptr<Position>(ents_d[i]);
                    volatile float fx = p ? p->x : 0;
                    (void)fx;
                }
                print_perf("single_class_set get_ptr", misc_count, timer.elapsed_ms());

                timer.reset();
                for (size_t i = 0; i < misc_count; ++i) {
                    auto* p = scs_d.get_ptr_fast<Position>(ents_d[i]);
                    volatile float fx = p ? p->x : 0;
                    (void)fx;
                }
                print_perf("single_class_set get_ptr_fast", misc_count, timer.elapsed_ms());

                timer.reset();
                for (size_t i = 0; i < misc_count; ++i)
                    scs_d.hard_remove(ents_d[i]);
                print_perf("single_class_set hard_remove", misc_count, timer.elapsed_ms());

                // add_batch
                for (size_t i = 0; i < misc_count; ++i) scs_d.add(ents_d[i], Position{1.0f, 0, 0});
                timer.reset();
                scs_d.clear();
                print_perf("single_class_set clear", misc_count, timer.elapsed_ms());

                // size / empty / get_type_id / get_operating_message
                timer.reset();
                volatile size_t sz = 0;
                volatile bool ey = false;
                volatile int ti = 0;
                for (int i = 0; i < 1000000; ++i) {
                    sz = scs_d.size();
                    ey = scs_d.empty();
                    ti = scs_d.get_type_id();
                }
                print_perf("single_class_set 状态查询", 1000000 * 3, timer.elapsed_ms());
            }
        }

        // ---- 14.20 entity / type_id 性能 ----
        print_perf_sub("14.20 entity / type_id 基础类型");
        {
            const size_t base_count = 1000000;

            // entity 构造
            timer.reset();
            entity e_test;
            volatile uint32_t e_idx = 0, e_ver = 0;
            for (size_t i = 0; i < base_count; ++i) {
                e_test = entity(static_cast<uint32_t>(i), static_cast<uint32_t>(i));
                e_idx = e_test.parts_.index_;
                e_ver = e_test.parts_.version_;
            }
            print_perf("entity 构造", base_count, timer.elapsed_ms());

            // entity is_valid / operator== / operator!=
            timer.reset();
            entity e1(1, 1), e2(1, 1), e3(2, 1);
            volatile bool b1 = false, b2 = false, b3 = false;
            for (size_t i = 0; i < base_count; ++i) {
                b1 = e1.is_valid();
                b2 = (e1 == e2);
                b3 = (e1 != e3);
            }
            print_perf("entity is_valid/==/!=", base_count * 3, timer.elapsed_ms());

            // std::hash<entity>
            timer.reset();
            std::hash<entity> eh;
            volatile size_t hv = 0;
            for (size_t i = 0; i < base_count; ++i)
                hv = eh(entity(static_cast<uint32_t>(i), 1));
            print_perf("std::hash<entity>", base_count, timer.elapsed_ms());

            // type_id::get_type_id
            timer.reset();
            volatile int tid = 0;
            for (int i = 0; i < base_count; ++i)
                tid = type_id::get_type_id<Position>();
            print_perf("type_id::get_type_id", base_count, timer.elapsed_ms());

            // entity_manager 掩码操作
            timer.reset();
            ecs::manager mgr_em;
            mgr_em.append_preallocated_entities(base_count);
            class_pool<entity> ents_em;
            ents_em.increase_capacity(base_count);
            for (size_t i = 0; i < base_count; ++i) ents_em.emplace_back(mgr_em.create_entity());
            mgr_em.add(ents_em[0], Position{1, 0, 0});
            volatile uint64_t mask = 0;
            for (size_t i = 0; i < base_count; ++i)
                mask = mgr_em.get_entity_mask(ents_em[i]);
            print_perf("entity_manager get_mask", base_count, timer.elapsed_ms());
        }

        // ---- 14.21 汇总 ----
        std::cout << "\n  ┌─ 匹配数汇总\n";
        std::cout << "  │ 双组件 Pos+Vel:          " << cnt_2a << "\n";
        std::cout << "  │ 双组件 Pos+Hp:           " << cnt_2b << "\n";
        std::cout << "  │ 双组件 Vel+Hp:           " << cnt_2c << "\n";
        std::cout << "  │ 三组件 Pos+Vel+Hp:       " << cnt_3a << "\n";
        std::cout << "  │ 三组件 Pos+Vel+Dmg:      " << cnt_3b << "\n";
        std::cout << "  │ 四组件 Pos+Vel+Hp+Name:  " << cnt_4 << "\n";
        std::cout << "  │ 五组件 +Dmg+Armor:       " << cnt_5 << "\n";
        std::cout << "  │ 六组件 +Spd:             " << cnt_6 << "\n";
        std::cout << "  │ 排除视图:                " << cnt_excl << "\n";
        std::cout << "  │ 可选视图:                " << cnt_with << "\n";
        std::cout << "  │ OR视图:                  " << cnt_or << "\n";
        std::cout << "  │ 任意匹配视图:            " << cnt_any << "\n";
    }
    std::cout << "\n══════════════════════════════════════════════════════\n"
              << "  全部接口测试完成\n"
              << "══════════════════════════════════════════════════════\n";
    return 0;
}
