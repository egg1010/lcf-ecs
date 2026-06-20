// =============================================================================
// lcf-ecs 库完整接口使用示例
// =============================================================================
// 本文件演示库中所有公开接口的使用方法
// 编译: g++ -std=c++20 -O2 -Wall -Wextra -I. usagec.cpp -o usagec.exe
// =============================================================================

#include "include/component.hpp"
#include "include/class_pool.hpp"
#include "include/void_any.hpp"
#include "include/memory_pool.hpp"
#include "include/id_.hpp"
#include "include/type_id.hpp"

#include <iostream>
#include <iomanip>
#include <span>
#include <functional>
#include <string>
#include <windows.h>

// =============================================================================
// 示例组件定义
// =============================================================================

struct Position { int x, y; };
struct Velocity { int dx, dy; };
struct Health   { int hp, max_hp; };
struct Name     { std::string name; };

struct CallbackComponent
{
    std::function<void(int)> callback;
    CallbackComponent(std::function<void(int)> cb) : callback(std::move(cb)) {}
};

// =============================================================================
// 辅助输出工具
// =============================================================================

static constexpr int BOX_WIDTH = 57;

static void print_header(int num, const char* title)
{
    std::cout << "\n";
    std::cout << "\u250c";
    for (int i = 0; i < BOX_WIDTH; ++i) std::cout << "\u2550";
    std::cout << "\u2510\n";

    std::cout << "\u2551  " << num << ". " << std::left << std::setw(BOX_WIDTH - 4) << title << "\u2551\n";

    std::cout << "\u2518";
    for (int i = 0; i < BOX_WIDTH; ++i) std::cout << "\u2550";
    std::cout << "\u2514\n";
}

static void print_sub(const char* title)
{
    std::cout << "\n  \u2500\u2500 " << title << " \u2500\u2500\n";
}

static void print_kv(const char* key, const auto& val, int w = 28)
{
    std::cout << "    " << std::left << std::setw(w) << key << "= " << val << "\n";
}

// =============================================================================
// 1. entity 实体
// =============================================================================
static void demo_entity()
{
    print_header(1, "entity \u5b9e\u4f53");

    print_sub("\u9ed8\u8ba4\u6784\u9020");
    entity e1;
    print_kv("entity e1;  e1.is_valid()", e1.is_valid());
    print_kv("e1.handle_", e1.handle_);

    print_sub("\u5e26\u53c2\u6784\u9020");
    entity e2(3, 1);
    print_kv("entity e2(3, 1);  e2.parts_.index_", e2.parts_.index_);
    print_kv("e2.parts_.version_", e2.parts_.version_);
    print_kv("e2.handle_", e2.handle_);

    print_sub("\u6bd4\u8f83\u8fd0\u7b97\u7b26");
    entity e3(3, 1);
    entity e4(3, 2);
    print_kv("e2 == e3 (3,1)==(3,1)", (e2 == e3));
    print_kv("e2 != e1 (3,1)!=(0,0)", (e2 != e1));
    print_kv("e2 != e4 (3,1)!=(3,2)", (e2 != e4));

    print_sub("\u54c8\u5e0c\u652f\u6301 (std::hash<entity>)");
    std::unordered_map<entity, int> map;
    map[e2] = 42;
    print_kv("unordered_map[e2]", map[e2]);
    print_kv("hash(e2)", std::hash<entity>{}(e2));

    print_sub("is_valid() \u5224\u65ad");
    entity e5(0, 0);
    print_kv("entity(0,0).is_valid()", e5.is_valid());
    entity e6(1, 0);
    print_kv("entity(1,0).is_valid()", e6.is_valid());
}

// =============================================================================
// 2. operating_message 操作消息
// =============================================================================
static void demo_operating_message()
{
    print_header(2, "operating_message \u64cd\u4f5c\u6d88\u606f");

    print_sub("\u9ed8\u8ba4\u6784\u9020\u4e0e operator bool()");
    operating_message msg;
    print_kv("operating_message msg;  (bool)msg", (bool)msg);

    print_sub("write_message(sw, args...)");
    msg.write_message(false, "\u9519\u8bef\u4fe1\u606f: ", "\u53d1\u751f\u5f02\u5e38");
    print_kv("\u5199\u5165\u5931\u8d25\u540e (bool)msg", (bool)msg);

    print_sub("\u7c98\u6027 false \u8bed\u4e49");
    msg.write_message(true, "\u5c1d\u8bd5\u6062\u590d\u7684\u6d88\u606f");
    print_kv("\u518d\u6b21 write_message(true,...) \u540e", (bool)msg);

    print_sub("read_message()");
    std::cout << "    \u6d88\u606f\u5185\u5bb9: [" << msg.read_message() << "]\n";

    print_sub("reset()");
    msg.reset();
    print_kv("reset() \u540e (bool)msg", (bool)msg);
    print_kv("reset() \u540e\u6d88\u606f\u957f\u5ea6", msg.read_message().length());

    print_sub("write_message_fmt(sw, fmt, args...)");
    msg.write_message_fmt(true, "\u683c\u5f0f\u5316: x={}, y={}", 10, 20);
    std::cout << "    \u683c\u5f0f\u5316\u6d88\u606f: [" << msg.read_message() << "]\n";

    print_sub("operator+=(string_view)");
    msg += "\u8ffd\u52a0\u5b57\u7b26\u4e32\n";
    std::cout << "    \u8ffd\u52a0\u540e: [" << msg.read_message() << "]\n";

    print_sub("operator+=(operating_message&&)");
    operating_message msg2;
    msg2.write_message(false, "\u53e6\u4e00\u4e2a\u9519\u8bef\n");
    msg += std::move(msg2);
    print_kv("\u5408\u5e76\u540e (bool)msg", (bool)msg);

    print_sub("operator+=(const operating_message&)");
    operating_message msg3;
    msg3.write_message(false, "\u7b2c\u4e09\u4e2a\u9519\u8bef\n");
    msg += msg3;
    print_kv("\u5408\u5e76\u540e (bool)msg", (bool)msg);

    print_sub("set_switch_bool / get_switch_bool");
    operating_message msg4;
    msg4.set_switch_bool(false);
    print_kv("set_switch_bool(false); get_switch_bool()", msg4.get_switch_bool());
    msg4.set_switch_bool(true);
    print_kv("set_switch_bool(true);  get_switch_bool()", msg4.get_switch_bool());

    print_sub("get_switch_bool() const");
    const operating_message& cmsg = msg4;
    print_kv("const \u7248\u672c get_switch_bool()", cmsg.get_switch_bool());

    print_sub("clear_message()");
    msg4.write_message(true, "\u4e34\u65f6\u6d88\u606f\n");
    print_kv("clear_message \u524d\u6d88\u606f\u957f\u5ea6", msg4.read_message().length());
    msg4.clear_message();
    print_kv("clear_message \u540e\u6d88\u606f\u957f\u5ea6", msg4.read_message().length());
    print_kv("clear_message \u540e switch \u72b6\u6001", msg4.get_switch_bool());

    print_sub("operator<<");
    operating_message msg5;
    msg5.write_message(true, "\u8f93\u51fa\u5230 ostream \u7684\u6d88\u606f");
    std::cout << "    std::cout << msg5: [" << msg5 << "]\n";

    print_sub("\u5168\u5c40\u5f00\u5173 ecs_debug_messages()");
    ecs_debug_messages() = false;
    operating_message msg6;
    msg6.write_message(false, "\u8fd9\u6761\u5b57\u7b26\u4e32\u4e0d\u4f1a\u88ab\u5199\u5165");
    print_kv("\u7981\u7528\u540e\u6d88\u606f\u957f\u5ea6", msg6.read_message().length());
    print_kv("\u7981\u7528\u540e switch \u72b6\u6001", msg6.get_switch_bool());
    ecs_debug_messages() = true;

    print_sub("\u79fb\u52a8/\u62f7\u8d1d\u6784\u9020\u4e0e\u8d4b\u503c");
    operating_message msg7;
    msg7.write_message(true, "\u539f\u59cb\u6d88\u606f\n");
    operating_message msg8(std::move(msg7));   // \u79fb\u52a8\u6784\u9020
    print_kv("\u79fb\u52a8\u6784\u9020\u540e msg8 \u6d88\u606f", msg8.read_message().length());
    operating_message msg9(msg8);              // \u62f7\u8d1d\u6784\u9020
    print_kv("\u62f7\u8d1d\u6784\u9020\u540e msg9 \u6d88\u606f", msg9.read_message().length());
    operating_message msg10;
    msg10 = std::move(msg9);                   // \u79fb\u52a8\u8d4b\u503c
    print_kv("\u79fb\u52a8\u8d4b\u503c\u540e msg10 \u6d88\u606f", msg10.read_message().length());
    operating_message msg11;
    msg11 = msg10;                             // \u62f7\u8d1d\u8d4b\u503c
    print_kv("\u62f7\u8d1d\u8d4b\u503c\u540e msg11 \u6d88\u606f", msg11.read_message().length());
}

// =============================================================================
// 3. class_pool<T> 核心容器
// =============================================================================
static void demo_class_pool()
{
    print_header(3, "class_pool<T> \u6838\u5fc3\u5bb9\u5668");

    print_sub("\u6784\u9020\u51fd\u6570");
    class_pool<int> pool;                                     // \u9ed8\u8ba4\u6784\u9020
    class_pool<int> pool2(100);                               // \u9884\u7559\u5bb9\u91cf
    class_pool<int> pool3(static_cast<size_t>(5), 42);        // 5\u4e2a42
    class_pool<int> pool4(pool3.begin(), pool3.end());        // \u8fed\u4ee3\u5668\u8303\u56f4
    class_pool<int> pool5 = {10, 20, 30, 40, 50};            // \u521d\u59cb\u5316\u5217\u8868
    print_kv("class_pool() \u9ed8\u8ba4\u6784\u9020", pool.size());
    print_kv("class_pool(100) \u9884\u7559\u5bb9\u91cf", pool2.capacity());
    print_kv("class_pool(5, 42) \u5927\u5c0f", pool3.size());
    print_kv("\u8fed\u4ee3\u5668\u8303\u56f4\u6784\u9020", pool4.size());
    print_kv("\u521d\u59cb\u5316\u5217\u8868\u6784\u9020", pool5.size());

    print_sub("emplace_back / \u5c3e\u90e8\u6784\u9020");
    pool.emplace_back(10);
    pool.emplace_back(20);
    pool.emplace_back(30);
    std::cout << "    \u5185\u5bb9: ";
    for (auto v : pool) std::cout << v << " ";
    std::cout << "\n";

    print_sub("\u5143\u7d20\u8bbf\u95ee: operator[] / at / front / back / get / data / span");
    print_kv("pool[1]", pool[1]);
    print_kv("pool.at(2)", pool.at(2));
    print_kv("pool.front()", pool.front());
    print_kv("pool.back()", pool.back());
    int* gp = pool.get(1);
    print_kv("pool.get(1)", (gp ? *gp : -1));
    print_kv("pool.data()[0]", pool.data()[0]);
    std::span<int> s = pool.span();
    print_kv("pool.span().size()", s.size());
    const class_pool<int>& cpool = pool;
    std::span<const int> cs = cpool.span();
    print_kv("pool.span() const \u5927\u5c0f", cs.size());

    print_sub("\u5bb9\u91cf\u67e5\u8be2: size / capacity / sparse_capacity / empty / count / valid");
    print_kv("pool.size()", pool.size());
    print_kv("pool.capacity()", pool.capacity());
    print_kv("pool.sparse_capacity()", pool.sparse_capacity());
    print_kv("pool.empty()", pool.empty());
    print_kv("pool.count()", pool.count());
    print_kv("pool.valid()", pool.valid());
    class_pool<int> empty_pool;
    print_kv("empty_pool.valid()", empty_pool.valid());
    print_kv("pool.size_bytes()", pool.size_bytes());
    print_kv("pool.capacity_bytes()", pool.capacity_bytes());

    print_sub("修改器: increase_capacity / shrink_to_fit / clear / pop_back");
    class_pool<int> pool6;
    pool6.emplace_back(1);
    pool6.emplace_back(2);
    pool6.emplace_back(3);
    pool6.increase_capacity(1000);
    print_kv("increase_capacity(1000) \u540e capacity", pool6.capacity());
    pool6.shrink_to_fit();
    print_kv("shrink_to_fit() \u540e capacity", pool6.capacity());
    pool6.pop_back();
    std::cout << "    pop_back \u540e: ";
    for (auto v : pool6) std::cout << v << " ";
    std::cout << "\n";
    pool6.clear();
    print_kv("clear() 后 empty()", pool6.empty());

    print_sub("increase_capacity(cap, value) / reduce_capacity(cap) / reduce_capacity(cap, dst)");
    class_pool<int> pool6b;
    pool6b.emplace_back(10);
    pool6b.emplace_back(20);
    pool6b.increase_capacity(static_cast<size_t>(5), 99);
    std::cout << "    increase_capacity(5, 99) 后: ";
    for (auto v : pool6b) std::cout << v << " ";
    std::cout << "\n";

    class_pool<int> pool6c = {1, 2, 3, 4, 5, 6, 7, 8};
    pool6c.reduce_capacity(3);
    std::cout << "    reduce_capacity(3) 后: ";
    for (auto v : pool6c) std::cout << v << " ";
    std::cout << "\n";

    class_pool<int> pool6d = {100, 200, 300, 400, 500};
    class_pool<int> pool6e;
    pool6d.reduce_capacity(static_cast<size_t>(2), pool6e);
    std::cout << "    reduce_capacity(2, dst) 后 src: ";
    for (auto v : pool6d) std::cout << v << " ";
    std::cout << "\n";
    std::cout << "    reduce_capacity(2, dst) 后 dst: ";
    for (auto v : pool6e) std::cout << v << " ";
    std::cout << "\n";

    print_sub("resize(size_t) / resize(size_t, value)");
    class_pool<int> pool7;
    pool7.emplace_back(1);
    pool7.emplace_back(2);
    pool7.resize(5, 99);
    std::cout << "    resize(5, 99) \u540e: ";
    for (auto v : pool7) std::cout << v << " ";
    std::cout << "\n";
    class_pool<int> pool7b;
    pool7b.emplace_back(1);
    pool7b.emplace_back(2);
    pool7b.resize(100);
    print_kv("resize(100) \u540e size", pool7b.size());
    print_kv("resize(100) \u540e capacity", pool7b.capacity());

    print_sub("emplace(pos, args...) / erase(pos) / \u63d2\u5165\u5220\u9664");
    class_pool<int> pool8;
    pool8.emplace_back(1);
    pool8.emplace_back(3);
    pool8.emplace(std::next(pool8.begin(), 1), 2);
    std::cout << "    emplace(begin()+1, 2) 后: ";
    for (auto v : pool8) std::cout << v << " ";
    std::cout << "\n";
    pool8.erase(std::next(pool8.begin(), 1));
    std::cout << "    erase(begin()+1) 后: ";
    for (auto v : pool8) std::cout << v << " ";
    std::cout << "\n";

    print_sub("swap / \u4ea4\u6362");
    class_pool<int> poolA = {1, 2};
    class_pool<int> poolB = {9, 8, 7};
    poolA.swap(poolB);
    std::cout << "    swap \u540e poolA: ";
    for (auto v : poolA) std::cout << v << " ";
    std::cout << "\n";
    std::cout << "    swap \u540e poolB: ";
    for (auto v : poolB) std::cout << v << " ";
    std::cout << "\n";

    print_sub("\u975e\u6210\u5458 swap \u81ea\u7531\u51fd\u6570");
    swap(poolA, poolB);
    std::cout << "    \u81ea\u7531 swap \u540e poolA: ";
    for (auto v : poolA) std::cout << v << " ";
    std::cout << "\n";

    print_sub("emplace_at / sparse_emplace_at / sparse_erase_at");
    class_pool<int> pool9;
    pool9.emplace_back(10);
    pool9.emplace_back(20);
    pool9.emplace_back(30);
    pool9.emplace_at(10, 999);
    print_kv("emplace_at(10, 999) \u540e pool9[10]", pool9[10]);
    pool9.emplace_at(10, 888);  // \u5df2\u6784\u9020\uff0c\u4e0d\u8986\u76d6
    print_kv("\u518d\u6b21 emplace_at(10, 888)", pool9[10]);
    pool9.sparse_emplace_at(10, 777);  // \u5148\u6790\u6784\u518d\u6784\u9020\uff0c\u8986\u76d6
    print_kv("sparse_emplace_at(10, 777)", pool9[10]);
    pool9.sparse_erase_at(10);
    print_kv("sparse_erase_at(10) \u540e size", pool9.size());

    print_sub("\u7a00\u758f/bitmap: is_constructed_at / is_dense / recompute_is_dense / invalidate_count_cache");
    class_pool<int> pool10;
    pool10.emplace_back(1);
    pool10.emplace_back(2);
    pool10.emplace_back(3);
    print_kv("\u8fde\u7eed\u6dfb\u52a0\u540e is_dense()", pool10.is_dense());
    pool10.sparse_erase_at(1);
    print_kv("sparse_erase_at(1) 后 is_dense()", pool10.is_dense());
    print_kv("is_constructed_at(0)", pool10.is_constructed_at(0));
    print_kv("is_constructed_at(1)", pool10.is_constructed_at(1));
    pool10.invalidate_count_cache();
    print_kv("invalidate_count_cache() \u540e count()", pool10.count());

    print_sub("range-for 遍历（自动跳过未构造）");
    class_pool<int> pool11;
    pool11.emplace_back(10);
    pool11.emplace_back(20);
    pool11.emplace_back(30);
    pool11.sparse_erase_at(1);
    std::cout << "    非 const range-for: ";
    for (int& v : pool11) std::cout << v << " ";
    std::cout << "\n";
    const class_pool<int>& cpool11 = pool11;
    std::cout << "    const range-for:     ";
    for (const int& v : cpool11) std::cout << v << " ";
    std::cout << "\n";

    print_sub("迭代器: begin/end / cbegin/cend");
    class_pool<int> pool12 = {100, 200, 300};
    std::cout << "    \u6b63\u5411 (begin/end): ";
    for (auto it = pool12.begin(); it != pool12.end(); ++it) std::cout << *it << " ";
    std::cout << "\n";
    std::cout << "    const (cbegin/cend): ";
    for (auto it = pool12.cbegin(); it != pool12.cend(); ++it) std::cout << *it << " ";
    std::cout << "\n";

    print_sub("\u62f7\u8d1d\u6784\u9020 / \u62f7\u8d1d\u8d4b\u503c / \u79fb\u52a8\u6784\u9020 / \u79fb\u52a8\u8d4b\u503c");
    class_pool<int> pool13(pool12);
    std::cout << "    \u62f7\u8d1d\u6784\u9020: ";
    for (auto v : pool13) std::cout << v << " ";
    std::cout << "\n";
    class_pool<int> pool14;
    pool14 = pool12;
    std::cout << "    \u62f7\u8d1d\u8d4b\u503c: ";
    for (auto v : pool14) std::cout << v << " ";
    std::cout << "\n";
    class_pool<int> pool15(std::move(pool13));
    std::cout << "    \u79fb\u52a8\u6784\u9020: ";
    for (auto v : pool15) std::cout << v << " ";
    std::cout << "\n";
    class_pool<int> pool16;
    pool16 = std::move(pool14);
    std::cout << "    \u79fb\u52a8\u8d4b\u503c: ";
    for (auto v : pool16) std::cout << v << " ";
    std::cout << "\n";
}

// =============================================================================
// 4. void_any 类型擦除存储
// =============================================================================
static void demo_void_any()
{
    print_header(4, "void_any \u7c7b\u578b\u64e6\u9664\u5b58\u50a8");

    print_sub("\u6784\u9020\u51fd\u6570");
    void_any a1(42);
    void_any a2(std::string("hello"));
    void_any a3;  // \u9ed8\u8ba4\u6784\u9020
    print_kv("void_any(42) has_value()", a1.has_value());
    print_kv("void_any(string) has_value()", a2.has_value());
    print_kv("void_any() has_value()", a3.has_value());

    print_sub("type_id()");
    print_kv("a1.type_id()", a1.type_id());
    print_kv("a2.type_id()", a2.type_id());
    print_kv("a3.type_id()", a3.type_id());

    print_sub("get_ptr<T> / get_ptr<T> const");
    int* pi = a1.get_ptr<int>();
    print_kv("a1.get_ptr<int>()", (pi ? *pi : -1));
    double* pd = a1.get_ptr<double>();
    print_kv("a1.get_ptr<double>() (\u7c7b\u578b\u4e0d\u5339\u914d)", (pd ? "non-null" : "nullptr"));
    const void_any& ca1 = a1;
    const int* cpi = ca1.get_ptr<int>();
    print_kv("a1.get_ptr<int>() const", (cpi ? *cpi : -1));

    print_sub("fast_get_ptr<T> / fast_get_ptr<T> const");
    int* pif = a1.fast_get_ptr<int>();
    print_kv("a1.fast_get_ptr<int>()", (pif ? *pif : -1));
    const int* cpif = ca1.fast_get_ptr<int>();
    print_kv("a1.fast_get_ptr<int>() const", (cpif ? *cpif : -1));

    print_sub("get_ptr_unchecked<T> / get_ptr_unchecked<T> const");
    int* piu = a1.get_ptr_unchecked<int>();
    print_kv("a1.get_ptr_unchecked<int>()", (piu ? *piu : -1));
    const int* cpiu = ca1.get_ptr_unchecked<int>();
    print_kv("a1.get_ptr_unchecked<int>() const", (cpiu ? *cpiu : -1));

    print_sub("get<T>");
    int val = a1.get<int>();
    print_kv("a1.get<int>()", val);

    print_sub("set()");
    a1.set(99);
    print_kv("a1.set(99) \u540e", *a1.get_ptr<int>());

    print_sub("reset()");
    a1.reset();
    print_kv("a1.reset() \u540e has_value()", a1.has_value());

    print_sub("\u62f7\u8d1d\u6784\u9020 / \u79fb\u52a8\u6784\u9020");
    void_any a4(std::string("world"));
    void_any a5(a4);              // \u62f7\u8d1d\u6784\u9020
    void_any a6(std::move(a4));   // \u79fb\u52a8\u6784\u9020
    print_kv("\u62f7\u8d1d\u6784\u9020 a5", *a5.get_ptr<std::string>());
    print_kv("\u79fb\u52a8\u6784\u9020 a6", *a6.get_ptr<std::string>());

    print_sub("\u62f7\u8d1d\u8d4b\u503c / \u79fb\u52a8\u8d4b\u503c");
    void_any a7;
    a7 = a5;                      // \u62f7\u8d1d\u8d4b\u503c
    void_any a8;
    a8 = std::move(a6);           // \u79fb\u52a8\u8d4b\u503c
    print_kv("\u62f7\u8d1d\u8d4b\u503c a7", *a7.get_ptr<std::string>());
    print_kv("\u79fb\u52a8\u8d4b\u503c a8", *a8.get_ptr<std::string>());
}

// =============================================================================
// 5. type_id 类型ID
// =============================================================================
static void demo_type_id()
{
    print_header(5, "type_id \u7c7b\u578bID");

    print_sub("get_type_id<T>()");
    int id1 = type_id::get_type_id<int>();
    int id2 = type_id::get_type_id<double>();
    int id3 = type_id::get_type_id<int>();
    int id4 = type_id::get_type_id<Position>();
    int id5 = type_id::get_type_id<Velocity>();
    print_kv("get_type_id<int>()", id1);
    print_kv("get_type_id<double>()", id2);
    print_kv("get_type_id<int>() \u518d\u6b21\u8c03\u7528", id3);
    print_kv("id1 == id3 (\u540c\u7c7b\u578b\u76f8\u540cID)", (id1 == id3));
    print_kv("id1 != id2 (\u4e0d\u540c\u7c7b\u578b\u4e0d\u540cID)", (id1 != id2));
    print_kv("get_type_id<Position>()", id4);
    print_kv("get_type_id<Velocity>()", id5);
}

// =============================================================================
// 6. id_allocation<T> ID分配器
// =============================================================================
static void demo_id_allocation()
{
    print_header(6, "id_allocation<T> ID\u5206\u914d\u5668");

    id_allocation<uint32_t> allocator;

    print_sub("get_id()");
    uint32_t id1 = allocator.get_id();
    uint32_t id2 = allocator.get_id();
    uint32_t id3 = allocator.get_id();
    print_kv("\u7b2c1\u6b21\u5206\u914d", id1);
    print_kv("\u7b2c2\u6b21\u5206\u914d", id2);
    print_kv("\u7b2c3\u6b21\u5206\u914d", id3);

    print_sub("maximum_id()");
    print_kv("maximum_id()", allocator.maximum_id());

    print_sub("free_id() / \u56de\u6536\u590d\u7528");
    allocator.free_id(id2);
    print_kv("\u91ca\u653e id2 \u540e total_number_of_ids()", allocator.total_number_of_ids());
    uint32_t id4 = allocator.get_id();
    print_kv("\u518d\u6b21\u5206\u914d (\u590d\u7528\u5df2\u91ca\u653eID)", id4);
    print_kv("\u590d\u7528\u540e total_number_of_ids()", allocator.total_number_of_ids());
}

// =============================================================================
// 7. memory_pool 内存池
// =============================================================================
static void demo_memory_pool()
{
    print_header(7, "memory_pool \u5185\u5b58\u6c60");

    print_sub("\u6784\u9020\u51fd\u6570");
    memory_pool pool;
    print_kv("memory_pool() \u9ed8\u8ba4 chunk_size", pool.chunk_size());
    memory_pool pool2(8192);
    print_kv("memory_pool(8192) chunk_size", pool2.chunk_size());

    print_sub("memory_block");
    memory_block blk;
    print_kv("memory_block() data==nullptr", (blk.data_ == nullptr));
    print_kv("memory_block() size", blk.size_);
    // memory_block(data, size) \u548c\u79fb\u52a8\u6784\u9020\u4e5f\u652f\u6301

    print_sub("allocate / deallocate");
    void* p1 = pool.allocate(64);
    void* p2 = pool.allocate(128);
    print_kv("allocate(64)", (p1 ? "\u6210\u529f" : "\u5931\u8d25"));
    print_kv("allocate(128)", (p2 ? "\u6210\u529f" : "\u5931\u8d25"));
    print_kv("total_used()", pool.total_used());
    pool.deallocate(p1);
    pool.deallocate(p2);
    print_kv("deallocate \u540e total_used()", pool.total_used());

    print_sub("construct<T> / destroy<T>");
    std::string* s = pool.construct<std::string>("hello pool");
    print_kv("construct<string>(\"hello pool\")", *s);
    pool.destroy(s);

    print_sub("total_allocated / total_used / chunk_size / empty");
    print_kv("total_allocated()", pool.total_allocated());
    print_kv("total_used()", pool.total_used());
    print_kv("chunk_size()", pool.chunk_size());
    print_kv("empty()", pool.empty());

    print_sub("increase_capacity / reduce_capacity / reset");
    pool.increase_capacity(1024 * 1024);
    print_kv("increase_capacity(1MB) \u540e total_allocated()", pool.total_allocated());
    pool.reduce_capacity(4096);
    print_kv("reduce_capacity(4096) \u540e total_allocated()", pool.total_allocated());
    pool.reset();
    print_kv("reset() \u540e empty()", pool.empty());
    print_kv("reset() \u540e total_allocated()", pool.total_allocated());

    print_sub("\u79fb\u52a8\u6784\u9020/\u8d4b\u503c");
    memory_pool pool3;
    (void)pool3.allocate(64);
    memory_pool pool4(std::move(pool3));
    print_kv("\u79fb\u52a8\u6784\u9020\u540e pool3.empty()", pool3.empty());
    print_kv("\u79fb\u52a8\u6784\u9020\u540e pool4.total_used()", pool4.total_used());
}

// =============================================================================
// 8. single_class_set 单组件集合
// =============================================================================
static void demo_single_class_set()
{
    print_header(8, "single_class_set \u5355\u7ec4\u4ef6\u96c6\u5408");

    print_sub("\u6784\u9020\u51fd\u6570");
    single_class_set set_default;
    print_kv("\u9ed8\u8ba4\u6784\u9020 size()", set_default.size());
    single_class_set set_r(500);
    print_kv("\u9884\u7559\u5bb9\u91cf\u6784\u9020 size()", set_r.size());
    entity e0(0, 1);
    single_class_set set_eo(e0, Position{5, 5});
    print_kv("\u5b9e\u4f53+\u5bf9\u8c61\u6784\u9020 size()", set_eo.size());

    print_sub("sparse_entry");
    sparse_entry entry;
    print_kv("sparse_entry() is_valid()", entry.is_valid());
    print_kv("sparse_entry() dense_index_", entry.dense_index_);
    print_kv("sparse_entry() version_", entry.version_);

    single_class_set set;
    entity e1(1, 1), e2(2, 1), e3(3, 1);

    print_sub("add()");
    set.add(e1, Position{10, 20});
    set.add(e2, Position{30, 40});
    set.add(e3, Position{50, 60});
    print_kv("add 3\u4e2a\u7ec4\u4ef6\u540e size()", set.size());

    print_sub("\u8986\u76d6\u6dfb\u52a0");
    set.add(e1, Position{100, 200});
    Position* p = set.get_ptr<Position>(e1);
    print_kv("\u8986\u76d6\u540e e1 Position", (p ? std::to_string(p->x) + "," + std::to_string(p->y) : "null"));

    print_sub("add_batch(class_pool&, class_pool&)");
    class_pool<entity> ents = {entity(4, 1), entity(5, 1)};
    class_pool<Position> comps = {Position{7, 8}, Position{9, 10}};
    set.add_batch(ents, comps);
    print_kv("add_batch(lvalue) \u540e size()", set.size());

    print_sub("add_batch(span, span)");
    entity span_ents[] = {entity(6, 1), entity(7, 1)};
    Position span_comps[] = {Position{11, 12}, Position{13, 14}};
    set.add_batch(std::span<const entity>(span_ents, 2), std::span<const Position>(span_comps, 2));
    print_kv("add_batch(span) \u540e size()", set.size());

    print_sub("add_batch(&&, &&)");
    class_pool<entity> r_ents = {entity(8, 1)};
    class_pool<Position> r_comps = {Position{15, 16}};
    set.add_batch(std::move(r_ents), std::move(r_comps));
    print_kv("add_batch(rvalue) \u540e size()", set.size());

    print_sub("get_ptr<T> / get_ptr<T> const");
    Position* p1 = set.get_ptr<Position>(e1);
    print_kv("get_ptr<Position>(e1)", (p1 ? std::to_string(p1->x) + "," + std::to_string(p1->y) : "null"));
    const single_class_set& cset = set;
    const Position* cp1 = cset.get_ptr<Position>(e1);
    print_kv("get_ptr<Position>(e1) const", (cp1 ? std::to_string(cp1->x) + "," + std::to_string(cp1->y) : "null"));

    print_sub("get_ptr_fast<T> / get_ptr_fast<T> const");
    Position* pf = set.get_ptr_fast<Position>(e2);
    print_kv("get_ptr_fast<Position>(e2)", (pf ? std::to_string(pf->x) + "," + std::to_string(pf->y) : "null"));
    const Position* cpf = cset.get_ptr_fast<Position>(e2);
    print_kv("get_ptr_fast<Position>(e2) const", (cpf ? std::to_string(cpf->x) + "," + std::to_string(cpf->y) : "null"));

    print_sub("get_version / get_version_unchecked");
    print_kv("get_version(1)", set.get_version(1));
    print_kv("get_version_unchecked(1)", set.get_version_unchecked(1));

    print_sub("hard_remove / soft_remove");
    set.soft_remove(e1);
    print_kv("soft_remove(e1) \u540e get_ptr", (set.get_ptr<Position>(e1) ? "\u975e\u7a7a" : "\u7a7a"));
    set.hard_remove(e2);
    print_kv("hard_remove(e2) \u540e size()", set.size());

    print_sub("clear / increase_capacity / empty");
    set.increase_capacity(10000);
    set.clear();
    print_kv("clear() \u540e empty()", set.empty());

    print_sub("get_type_id()");
    single_class_set set2;
    set2.add(e1, Position{1, 1});
    int& tid = set2.get_type_id();
    print_kv("get_type_id()", tid);

    print_sub("get_typed_pool_ptr<T> / get_typed_pool_ptr<T> const");
    class_pool<Position>* tpool = set2.get_typed_pool_ptr<Position>();
    print_kv("get_typed_pool_ptr() size()", (tpool ? tpool->size() : 0));
    const class_pool<Position>* ctpool = cset.get_typed_pool_ptr<Position>();
    print_kv("get_typed_pool_ptr() const size()", (ctpool ? ctpool->size() : 0));

    print_sub("get_entity_indices / get_entity_indices const");
    class_pool<uint32_t>& indices = set2.get_entity_indices();
    print_kv("get_entity_indices() size()", indices.size());
    const class_pool<uint32_t>& cindices = set2.get_entity_indices();
    print_kv("get_entity_indices() const size()", cindices.size());

    print_sub("get_operating_message()");
    operating_message& msg = set2.get_operating_message();
    print_kv("get_operating_message() \u72b6\u6001", (bool)msg);

    print_sub("\u79fb\u52a8\u6784\u9020/\u8d4b\u503c");
    single_class_set set3;
    set3.add(e1, Position{1, 1});
    single_class_set set4(std::move(set3));
    print_kv("\u79fb\u52a8\u6784\u9020\u540e set4.size()", set4.size());
    single_class_set set5;
    set5 = std::move(set4);
    print_kv("\u79fb\u52a8\u8d4b\u503c\u540e set5.size()", set5.size());
}

// =============================================================================
// 9. ecs::manager ECS管理器
// =============================================================================
static void demo_manager()
{
    print_header(9, "ecs::manager ECS\u7ba1\u7406\u5668");

    ecs::manager mgr;

    print_sub("\u5b9e\u4f53\u7ba1\u7406");
    mgr.append_preallocated_entities(1000);
    entity e1 = mgr.create_entity();
    entity e2 = mgr.create_entity();
    entity e3 = mgr.create_entity();
    entity e4 = mgr.create_entity();
    print_kv("create_entity() e1.parts_.index_", e1.parts_.index_);
    print_kv("create_entity() e2.parts_.index_", e2.parts_.index_);
    print_kv("is_entity_valid(e1)", mgr.is_entity_valid(e1));

    print_sub("add(entity, component) / add(component, entity)");
    mgr.add(e1, Position{1, 2});
    mgr.add(e2, Position{3, 4});
    mgr.add(e3, Position{5, 6});
    mgr.add(Velocity{10, 20}, e1);
    mgr.add(Velocity{30, 40}, e2);
    print_kv("add \u5b8c\u6210", "\u6210\u529f");

    print_sub("addc \u94fe\u5f0f\u6dfb\u52a0");
    mgr.addc(e1, Health{100, 100})
       .addc(e2, Health{80, 100})
       .addc(Health{60, 100}, e3)
       .addc(e1, Name{"Alice"})
       .addc(e2, Name{"Bob"});
    print_kv("addc \u94fe\u5f0f\u6dfb\u52a0", "\u5b8c\u6210");

    print_sub("get_ptr<T> / get_ptr<T> const");
    Position* pos = mgr.get_ptr<Position>(e1);
    print_kv("get_ptr<Position>(e1)", (pos ? std::to_string(pos->x) + "," + std::to_string(pos->y) : "null"));
    const ecs::manager& cmgr = mgr;
    const Position* cpos = cmgr.get_ptr<Position>(e1);
    print_kv("get_ptr<Position>(e1) const", (cpos ? std::to_string(cpos->x) + "," + std::to_string(cpos->y) : "null"));

    print_sub("get_ptr_fast<T> / get_ptr_fast<T> const");
    Velocity* vel = mgr.get_ptr_fast<Velocity>(e1);
    print_kv("get_ptr_fast<Velocity>(e1)", (vel ? std::to_string(vel->dx) + "," + std::to_string(vel->dy) : "null"));
    const Velocity* cvel = cmgr.get_ptr_fast<Velocity>(e1);
    print_kv("get_ptr_fast<Velocity>(e1) const", (cvel ? std::to_string(cvel->dx) + "," + std::to_string(cvel->dy) : "null"));

    print_sub("add_batch \u4e09\u79cd\u91cd\u8f7d");
    class_pool<entity> batch_ents = {mgr.create_entity(), mgr.create_entity()};
    class_pool<Health> batch_comps = {Health{50, 50}, Health{70, 70}};
    mgr.add_batch(batch_ents, batch_comps);
    print_kv("add_batch(class_pool&, class_pool&)", "\u5b8c\u6210");

    std::span<const entity> ent_span(batch_ents.data(), batch_ents.size());
    std::span<const Health> comp_span(batch_comps.data(), batch_comps.size());
    mgr.add_batch(ent_span, comp_span);
    print_kv("add_batch(span, span)", "\u5b8c\u6210");

    class_pool<entity> rv_ents = {mgr.create_entity()};
    class_pool<Health> rv_comps = {Health{33, 33}};
    mgr.add_batch(std::move(rv_ents), std::move(rv_comps));
    print_kv("add_batch(&&, &&)", "\u5b8c\u6210");

    print_sub("get_single_class_set<T> / get_single_class_set<T> const");
    single_class_set* pos_set = mgr.get_single_class_set<Position>();
    print_kv("get_single_class_set<Position>() size", (pos_set ? pos_set->size() : 0));
    const single_class_set* cpos_set = cmgr.get_single_class_set<Position>();
    print_kv("get_single_class_set<Position>() const size", (cpos_set ? cpos_set->size() : 0));

    print_sub("get_component_vector<T>");
    class_pool<Position>* pos_vec = mgr.get_component_vector<Position>();
    print_kv("get_component_vector<Position>() size", (pos_vec ? pos_vec->size() : 0));

    print_sub("reserve_component_capacity<T>");
    mgr.reserve_component_capacity<Position>(10000);
    print_kv("reserve_component_capacity<Position>(10000)", "\u5b8c\u6210");

    print_sub("get_operating_message()");
    operating_message& msg = mgr.get_operating_message();
    print_kv("get_operating_message() \u72b6\u6001", (bool)msg);

    print_sub("soft_remove / hard_remove");
    mgr.soft_remove<Health>(e1);
    print_kv("soft_remove<Health>(e1) \u540e", (mgr.get_ptr<Health>(e1) ? "\u975e\u7a7a" : "\u7a7a"));
    mgr.hard_remove<Velocity>(e2);
    print_kv("hard_remove<Velocity>(e2) \u540e", (mgr.get_ptr<Velocity>(e2) ? "\u975e\u7a7a" : "\u7a7a"));

    print_sub("soft_removec / hard_removec \u94fe\u5f0f\u5220\u9664");
    mgr.soft_removec<Name>(e1).soft_removec<Name>(e2);
    print_kv("soft_removec<Name> \u94fe\u5f0f\u5220\u9664", "\u5b8c\u6210");
    entity e5 = mgr.create_entity();
    entity e6 = mgr.create_entity();
    mgr.add(e5, Velocity{1, 1});
    mgr.add(e6, Velocity{2, 2});
    mgr.hard_removec<Velocity>(e5).hard_removec<Velocity>(e6);
    print_kv("hard_removec<Velocity> \u94fe\u5f0f\u5220\u9664", "\u5b8c\u6210");

    print_sub("delete_type_container<T>");
    mgr.delete_type_container<Position>();
    print_kv("delete_type_container<Position>() \u540e", (mgr.get_single_class_set<Position>() ? "\u5b58\u5728" : "\u7a7a"));

    print_sub("delete_entity");
    mgr.delete_entity(e4);
    print_kv("delete_entity(e4) \u540e valid", mgr.is_entity_valid(e4));
}

// =============================================================================
// 10. View 系统
// =============================================================================
static void demo_views()
{
    print_header(10, "View \u89c6\u56fe\u7cfb\u7edf");

    ecs::manager mgr;
    mgr.append_preallocated_entities(100);

    for (int i = 0; i < 5; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add(e, Position{i * 10, i * 20});
        mgr.add(e, Velocity{i, -i});
        if (i < 3) mgr.add(e, Health{100 - i * 10, 100});
        if (i < 2) mgr.add(e, Name{std::string("Entity") + std::to_string(i)});
    }

    print_sub("single_view<T>: size / empty / contains");
    auto pos_view = mgr.view<Position>();
    print_kv("view<Position>.size()", pos_view.size());
    print_kv("view<Position>.empty()", pos_view.empty());
    entity test_e(0, 1);
    print_kv("view<Position>.contains(entity0)", pos_view.contains(test_e));

    print_sub("single_view<T>: for_each");
    std::cout << "    for_each [comp]:      ";
    pos_view.for_each([](Position& p) {
        std::cout << "(" << p.x << "," << p.y << ") ";
    });
    std::cout << "\n";
    std::cout << "    for_each [ent+comp]:  ";
    pos_view.for_each([](entity e, Position& p) {
        std::cout << "[" << e.parts_.index_ << "](" << p.x << "," << p.y << ") ";
    });
    std::cout << "\n";

    print_sub("single_view<T>: begin/end / component_begin/component_end");
    std::cout << "    \u5b9e\u4f53\u8fed\u4ee3: ";
    for (auto e : pos_view) std::cout << e.parts_.index_ << " ";
    std::cout << "\n";
    std::cout << "    \u7ec4\u4ef6\u8fed\u4ee3: ";
    for (auto it = pos_view.component_begin(); it != pos_view.component_end(); ++it)
        std::cout << "(" << it->x << "," << it->y << ") ";
    std::cout << "\n";

    print_sub("view<T>().for_each(func) \u5feb\u6377\u5199\u6cd5");
    std::cout << "    view<Position>().for_each(func): ";
    mgr.view<Position>().for_each([](Position& p) { std::cout << p.x << " "; });
    std::cout << "\n";

    print_sub("multi_view<T1, T2, ...>: size / empty / contains / for_each");
    auto dual_view = mgr.view<Position, Velocity>();
    print_kv("view<Position,Velocity>.size()", dual_view.size());
    print_kv("view<Position,Velocity>.empty()", dual_view.empty());
    print_kv("view<Position,Velocity>.contains(e0)", dual_view.contains(test_e));
    std::cout << "    for_each [comp]:      ";
    dual_view.for_each([](Position& p, Velocity& v) {
        std::cout << "P(" << p.x << "," << p.y << ")V(" << v.dx << "," << v.dy << ") ";
    });
    std::cout << "\n";
    std::cout << "    for_each [ent+comp]:  ";
    dual_view.for_each([](entity e, Position& p, Velocity& v) {
        std::cout << "[" << e.parts_.index_ << "]P(" << p.x << ")V(" << v.dx << ") ";
    });
    std::cout << "\n";

    print_sub("multi_view \u4e09\u7ec4\u4ef6/\u56db\u7ec4\u4ef6");
    auto tri_view = mgr.view<Position, Velocity, Health>();
    print_kv("view<Pos,Vel,Health>.size()", tri_view.size());
    tri_view.for_each([](entity e, Position&, Velocity&, Health& h) {
        std::cout << "    [" << e.parts_.index_ << "] hp=" << h.hp << "\n";
    });
    auto quad_view = mgr.view<Position, Velocity, Health, Name>();
    print_kv("view<Pos,Vel,Health,Name>.size()", quad_view.size());
    quad_view.for_each([](entity e, Position&, Velocity&, Health&, Name& n) {
        std::cout << "    [" << e.parts_.index_ << "] name=" << n.name << "\n";
    });

    print_sub("single_view_without: size / empty / for_each");
    auto excl_view = mgr.view<Position>(ecs::without<Health>);
    print_kv("view<Pos>(without<Health>).size()", excl_view.size());
    print_kv("view<Pos>(without<Health>).empty()", excl_view.empty());
    std::cout << "    for_each [comp]:      ";
    excl_view.for_each([](Position& p) {
        std::cout << "(" << p.x << "," << p.y << ") ";
    });
    std::cout << "\n";
    std::cout << "    for_each [ent+comp]:  ";
    excl_view.for_each([](entity e, Position& p) {
        std::cout << "[" << e.parts_.index_ << "](" << p.x << "," << p.y << ") ";
    });
    std::cout << "\n";

    print_sub("\u6392\u9664\u591a\u4e2a\u7c7b\u578b");
    auto excl_multi = mgr.view<Position>(ecs::without<Health, Name>);
    print_kv("view<Pos>(without<Health,Name>).size()", excl_multi.size());

    print_sub("single_view_with: size / empty / for_each");
    auto get_view = mgr.view<Position>(ecs::with<Health>);
    print_kv("view<Pos>(with<Health>).size()", get_view.size());
    print_kv("view<Pos>(with<Health>).empty()", get_view.empty());
    std::cout << "    for_each [comp]:      ";
    get_view.for_each([](Position& p, Health* h) {
        std::cout << "P(" << p.x << ")";
        if (h) std::cout << "[hp=" << h->hp << "]";
        else   std::cout << "[\u65e0hp]";
        std::cout << " ";
    });
    std::cout << "\n";
    std::cout << "    for_each [ent+comp]:  ";
    get_view.for_each([](entity e, Position&, Health* h) {
        std::cout << "[" << e.parts_.index_ << "]";
        if (h) std::cout << "[hp=" << h->hp << "] ";
        else   std::cout << "[\u65e0hp] ";
    });
    std::cout << "\n";

    print_sub("\u83b7\u53d6\u591a\u4e2a\u53ef\u9009\u7ec4\u4ef6");
    auto get_multi = mgr.view<Position>(ecs::with<Health, Name>);
    get_multi.for_each([](Position& p, Health* h, Name* n) {
        std::cout << "    P(" << p.x << ")";
        if (h) std::cout << " H(hp=" << h->hp << ")";
        if (n) std::cout << " N(" << n->name << ")";
        std::cout << "\n";
    });
}

// =============================================================================
// 11. 高级视图：OR / filter / filter_and / filter_or
// =============================================================================
static void demo_advanced_views()
{
    print_header(11, "\u9ad8\u7ea7\u89c6\u56fe: OR / filter / filter_and / filter_or");

    ecs::manager mgr;
    mgr.append_preallocated_entities(10);
    auto e1 = mgr.create_entity();
    auto e2 = mgr.create_entity();
    auto e3 = mgr.create_entity();
    auto e4 = mgr.create_entity();
    mgr.add(e1, Position{1, 0});
    mgr.add(e2, Position{2, 0});
    mgr.add(e3, Position{3, 0});
    mgr.add(e1, Velocity{10, 0});
    mgr.add(e2, Velocity{20, 0});
    mgr.add(e4, Velocity{40, 0});  // e4 \u53ea\u6709 Velocity
    mgr.add(e1, Health{80, 100});
    mgr.add(e2, Health{90, 100});

    print_sub("or_view: Position OR Velocity (\u96f6\u5206\u914d\uff0cnullable \u6307\u9488)");
    auto ov = mgr.view_or<Position, Velocity>();
    ov.for_each([](entity e, Position* p, Velocity* v) {
        std::cout << "    e" << e.parts_.index_ << ": ";
        if (p) std::cout << "P(" << p->x << ") ";
        if (v) std::cout << "V(" << v->dx << ") ";
        std::cout << "\n";
    });

    print_sub("filter_view: Position.x > 1 (\u8c13\u8bcd\u4e0b\u63a8)");
    auto fv = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; });
    std::cout << "    size=" << fv.size() << " ";
    fv.for_each([](Position& p) { std::cout << "P(" << p.x << ") "; });
    std::cout << "\n";

    print_sub("filter_and_view: Position.x > 1 AND Health");
    auto fav = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; }).and_<Health>();
    fav.for_each([](entity e, Position& p, Health& h) {
        std::cout << "    e" << e.parts_.index_ << " P(" << p.x << ") Hp(" << h.hp << ")\n";
    });

    print_sub("filter_or_view: Position.x > 1 OR Velocity");
    auto fov = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; }).or_<Velocity>();
    fov.for_each([](entity e, Position* p, Velocity* v) {
        std::cout << "    e" << e.parts_.index_ << ": ";
        if (p) std::cout << "P(" << p->x << ") ";
        if (v) std::cout << "V(" << v->dx << ") ";
        std::cout << "\n";
    });
}

// =============================================================================
// 12. Group 系统（Non-Owning + Owning）
// =============================================================================
static void demo_group()
{
    print_header(12, "Group 系统（Non-Owning + Owning）");

    ecs::manager mgr;
    mgr.append_preallocated_entities(100);

    for (int i = 0; i < 5; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add(e, Position{i * 10, i * 20});
        mgr.add(e, Velocity{i, -i});
        if (i < 3) mgr.add(e, Health{100 - i * 10, 100});
    }

    print_sub("Non-OwningGroup: group<Position, Velocity>()");
    {
        auto g = mgr.group<Position, Velocity>();
        print_kv("group<Pos,Vel>.size()", g.size());
        print_kv("group<Pos,Vel>.empty()", g.empty());
        entity test_e(0, 1);
        print_kv("group<Pos,Vel>.contains(e0)", g.contains(test_e));

        std::cout << "    for_each [comp]:      ";
        g.for_each([](Position& p, Velocity& v) {
            std::cout << "P(" << p.x << "," << p.y << ")V(" << v.dx << "," << v.dy << ") ";
        });
        std::cout << "\n";

        std::cout << "    for_each [ent+comp]:  ";
        g.for_each([](entity e, Position& p, Velocity& v) {
            std::cout << "[" << e.parts_.index_ << "]P(" << p.x << ")V(" << v.dx << ") ";
        });
        std::cout << "\n";

        print_kv("group<Pos,Vel>.front()", g.front().parts_.index_);
        print_kv("group<Pos,Vel>.back()", g.back().parts_.index_);

        auto* pos = g.get<Position>(g.front());
        print_kv("group<Pos,Vel>.get<Position>(front)", (pos ? std::to_string(pos->x) + "," + std::to_string(pos->y) : "null"));

        // rebuild
        g.rebuild();
        print_kv("group<Pos,Vel>.rebuild() size", g.size());
    }

    print_sub("Non-OwningGroup 三组件: group<Position, Velocity, Health>()");
    {
        auto g = mgr.group<Position, Velocity, Health>();
        print_kv("group<Pos,Vel,Health>.size()", g.size());
        g.for_each([](entity e, Position& p, Velocity&, Health& h) {
            std::cout << "    [" << e.parts_.index_ << "] P(" << p.x << ") hp=" << h.hp << "\n";
        });
    }

    print_sub("OwningGroup: group<Position, Velocity>(owned<Position>)");
    {
        auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);
        print_kv("group<Pos,Vel>(owned<Pos>).size()", og.size());
        print_kv("group<Pos,Vel>(owned<Pos>).empty()", og.empty());

        std::cout << "    for_each [comp]:      ";
        og.for_each([](Position& p, Velocity& v) {
            std::cout << "P(" << p.x << "," << p.y << ")V(" << v.dx << "," << v.dy << ") ";
        });
        std::cout << "\n";

        std::cout << "    for_each [ent+comp]:  ";
        og.for_each([](entity e, Position& p, Velocity& v) {
            std::cout << "[" << e.parts_.index_ << "]P(" << p.x << ")V(" << v.dx << ") ";
        });
        std::cout << "\n";

        print_kv("owning_group.front()", og.front().parts_.index_);
        print_kv("owning_group.back()", og.back().parts_.index_);

        auto* pos = og.get<Position>(og.front());
        print_kv("owning_group.get<Position>(front)", (pos ? std::to_string(pos->x) + "," + std::to_string(pos->y) : "null"));

        og.rebuild();
        print_kv("owning_group.rebuild() size", og.size());
    }

    print_sub("OwningGroup 三组件: group<Position, Velocity, Health>(owned<Position>)");
    {
        auto og = mgr.group<Position, Velocity, Health>(ecs::owned<Position>);
        print_kv("group<Pos,Vel,Health>(owned<Pos>).size()", og.size());
        og.for_each([](entity e, Position& p, Velocity&, Health& h) {
            std::cout << "    [" << e.parts_.index_ << "] P(" << p.x << ") hp=" << h.hp << "\n";
        });
    }
}

// =============================================================================
// 13. runtime_view 运行时视图
// =============================================================================
static void demo_runtime_view()
{
    print_header(13, "runtime_view 运行时视图");

    ecs::manager mgr;
    mgr.append_preallocated_entities(100);

    for (int i = 0; i < 5; ++i)
    {
        entity e = mgr.create_entity();
        mgr.add(e, Position{i * 10, i * 20});
        mgr.add(e, Velocity{i, -i});
        if (i < 3) mgr.add(e, Health{100 - i * 10, 100});
    }

    print_sub("实体掩码");
    {
        auto e = mgr.create_entity();
        mgr.add(e, Position{99, 0});
        uint64_t mask = mgr.get_entity_mask(e);
        std::cout << "    entity mask: 0x" << std::hex << mask << std::dec << "\n";
        print_kv("掩码含 Position", (mask & mgr.get_component_bit<Position>()) != 0);
    }

    print_sub("双组件运行时视图: runtime_view_create({Pos, Vel})");
    {
        auto rv = mgr.runtime_view_create({
            type_id::get_type_id<Position>(),
            type_id::get_type_id<Velocity>()
        });
        print_kv("runtime_view.size()", rv.size());
        print_kv("runtime_view.empty()", rv.empty());

        std::cout << "    for_each [entity]:    ";
        rv.for_each([&](entity e) {
            auto* p = rv.get_ptr<Position>(e);
            auto* v = rv.get_ptr<Velocity>(e);
            std::cout << "[" << e.parts_.index_ << "]P(" << p->x << ")V(" << v->dx << ") ";
        });
        std::cout << "\n";
    }

    print_sub("三组件运行时视图: runtime_view_create({Pos, Vel, Health})");
    {
        auto rv = mgr.runtime_view_create({
            type_id::get_type_id<Position>(),
            type_id::get_type_id<Velocity>(),
            type_id::get_type_id<Health>()
        });
        print_kv("runtime_view.size()", rv.size());
        rv.for_each([&](entity e) {
            auto* p = rv.get_ptr<Position>(e);
            auto* h = rv.get_ptr<Health>(e);
            std::cout << "    [" << e.parts_.index_ << "] P(" << p->x << ") hp=" << h->hp << "\n";
        });
    }

    print_sub("排除视图: runtime_view_create({Pos}, {Vel})");
    {
        auto rv = mgr.runtime_view_create(
            { type_id::get_type_id<Position>() },
            { type_id::get_type_id<Velocity>() }
        );
        print_kv("runtime_view.size()", rv.size());
        rv.for_each([&](entity e) {
            std::cout << "    [" << e.parts_.index_ << "] 有Position无Velocity\n";
        });
    }

    print_sub("删除后掩码自动更新");
    {
        auto e = mgr.create_entity();
        mgr.add(e, Position{1, 0});
        mgr.add(e, Velocity{2, 0});
        uint64_t before = mgr.get_entity_mask(e);
        mgr.hard_remove<Velocity>(e);
        uint64_t after = mgr.get_entity_mask(e);
        std::cout << "    删除前掩码: 0x" << std::hex << before << std::dec << "\n";
        std::cout << "    删除后掩码: 0x" << std::hex << after << std::dec << "\n";
        print_kv("Velocity位已清除", (after & mgr.get_component_bit<Velocity>()) == 0);
    }
}

// =============================================================================
// 15. 函数存储
// =============================================================================
static void demo_function_storage()
{
    print_header(15, "\u51fd\u6570\u5b58\u50a8\u4e0e\u8c03\u7528");

    ecs::manager mgr;
    entity e = mgr.create_entity();

    print_sub("\u5b58\u50a8 lambda \u4f5c\u4e3a\u7ec4\u4ef6");
    mgr.add(e, CallbackComponent([](int x) {
        std::cout << "    Lambda \u88ab\u8c03\u7528: x=" << x << "\n";
    }));
    print_kv("\u6dfb\u52a0 CallbackComponent", "\u6210\u529f");

    print_sub("\u83b7\u53d6\u5e76\u8c03\u7528");
    auto* cb = mgr.get_ptr<CallbackComponent>(e);
    if (cb)
    {
        std::cout << "    \u8c03\u7528 callback(42):\n";
        cb->callback(42);
    }
}

// =============================================================================
// 14. 生命周期信号
// =============================================================================
static void demo_lifecycle_signals()
{
    print_header(14, "\u751f\u547d\u5468\u671f\u4fe1\u53f7");

    ecs::manager mgr;
    mgr.append_preallocated_entities(100);

    // 即时信号：实体级
    print_sub("\u5373\u65f6\u4fe1\u53f7\uff1a\u5b9e\u4f53\u521b\u5efa/\u9500\u6bc1");
    {
        int created = 0, destroyed = 0;

        auto on_created = [](entity, void* data) noexcept {
            *static_cast<int*>(data) += 1;
        };
        auto on_destroyed = [](entity, void* data) noexcept {
            *static_cast<int*>(data) += 1;
        };

        mgr.set_on_entity_created(+on_created, &created);
        mgr.set_on_entity_destroyed(+on_destroyed, &destroyed);

        entity e1 = mgr.create_entity();
        auto _e2 = mgr.create_entity(); (void)_e2;
        std::cout << "    create_entity x2:  created=" << created << std::endl;

        mgr.delete_entity(e1);
        std::cout << "    delete_entity x1:   destroyed=" << destroyed << std::endl;
    }

    // 即时信号：组件级
    print_sub("\u5373\u65f6\u4fe1\u53f7\uff1a\u7ec4\u4ef6\u6dfb\u52a0/\u79fb\u9664");
    {
        int pos_added = 0, pos_removed = 0;

        auto on_add = [](entity, void*, void* data) noexcept {
            *static_cast<int*>(data) += 1;
        };
        auto on_remove = [](entity, void*, void* data) noexcept {
            *static_cast<int*>(data) += 1;
        };

        mgr.set_on_add<Position>(+on_add, &pos_added);
        mgr.set_on_remove<Position>(+on_remove, &pos_removed);

        entity e = mgr.create_entity();
        mgr.add(e, Position{10, 20});
        std::cout << "    add<Position>:       pos_added=" << pos_added << std::endl;

        mgr.add(e, Position{30, 40});  // 覆盖
        std::cout << "    add<Position>(覆盖): pos_added=" << pos_added << std::endl;

        mgr.hard_remove<Position>(e);
        std::cout << "    hard_remove<Position>: pos_removed=" << pos_removed << std::endl;
    }

    // 延迟信号：flush
    print_sub("\u5ef6\u8fdf\u4fe1\u53f7\uff1aflush \u6279\u91cf\u5904\u7406");
    {
        for (int i = 0; i < 3; ++i)
        {
            auto e = mgr.create_entity();
            mgr.add(e, Position{i, 0});
        }

        int added = 0;
        mgr.flush_component_signals([&added](uint32_t type, uint32_t, uint32_t) noexcept {
            if (type == 0) added++;
        });

        std::cout << "    flush_component_signals: 3 Position adds = " << added << std::endl;
        std::cout << "    has_pending: " << (mgr.has_pending_component_signals() ? "true" : "false") << std::endl;
    }
}

// =============================================================================
// 主函数
// =============================================================================
int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "\u250c";
    for (int i = 0; i < BOX_WIDTH; ++i) std::cout << "\u2550";
    std::cout << "\u2510\n";

    std::cout << "\u2551  " << std::left << std::setw(BOX_WIDTH - 2)
              << "lcf-ecs \u5b8c\u6574\u63a5\u53e3\u4f7f\u7528\u793a\u4f8b"
              << "\u2551\n";

    std::cout << "\u2518";
    for (int i = 0; i < BOX_WIDTH; ++i) std::cout << "\u2550";
    std::cout << "\u2514\n";

    demo_entity();              // 1. \u5b9e\u4f53
    demo_operating_message();   // 2. \u64cd\u4f5c\u6d88\u606f
    demo_class_pool();          // 3. \u6838\u5fc3\u5bb9\u5668
    demo_void_any();            // 4. \u7c7b\u578b\u64e6\u9664\u5b58\u50a8
    demo_type_id();             // 5. \u7c7b\u578bID
    demo_id_allocation();       // 6. ID\u5206\u914d\u5668
    demo_memory_pool();         // 7. \u5185\u5b58\u6c60
    demo_single_class_set();    // 8. \u5355\u7ec4\u4ef6\u96c6\u5408
    demo_manager();             // 9. ECS\u7ba1\u7406\u5668
    demo_views();               // 10. View\u7cfb\u7edf
    demo_advanced_views();      // 11. \u9ad8\u7ea7\u89c6\u56fe
    demo_group();               // 12. Group \u7cfb\u7edf
    demo_runtime_view();        // 13. runtime_view 运行时视图
    demo_lifecycle_signals();   // 14. 生命周期信号
    demo_function_storage();    // 15. 函数存储\u51fd\u6570\u5b58\u50a8

    std::cout << "\n";
    std::cout << "\u250c";
    for (int i = 0; i < BOX_WIDTH; ++i) std::cout << "\u2550";
    std::cout << "\u2510\n";

    std::cout << "\u2551  " << std::left << std::setw(BOX_WIDTH - 2)
              << "\u6240\u6709\u793a\u4f8b\u6267\u884c\u5b8c\u6bd5"
              << "\u2551\n";

    std::cout << "\u2518";
    for (int i = 0; i < BOX_WIDTH; ++i) std::cout << "\u2550";
    std::cout << "\u2514\n";

    return 0;
}
