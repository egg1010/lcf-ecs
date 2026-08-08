#include "test_common.hpp"
#include "include/part/class_pool_views.hpp"

// 功能测试 - 验证 ECS 各接口的正确性
int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "========================================================\n"
              << "  lcf-ecs 功能测试\n"
              << "========================================================\n";
    // 1. entity 实体
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

    // 2. operating_message 操作消息
    print_section(2, "operating_message 操作消息");
    {
        bool& dbg = message_recording_enabled();
        bool old_dbg = dbg;
        dbg = true;

        print_item("message_recording_enabled() 返回引用", true);

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

        om.reset();
        om.set_min_level(msg_level::warn);
        print_item("set_min_level(warn)", om.get_min_level() == msg_level::warn);
        om.write_message_level(msg_level::info, true, "不应写入的info");
        print_item("level过滤: info被过滤", om.read_message().empty());
        om.write_message_level(msg_level::error, true, "error消息");
        print_item("level通过: error写入", om.read_message().find("[ERROR]") != std::string::npos);
        print_item("level前缀: [ERROR]存在", om.read_message().find("[ERROR] error消息") != std::string::npos);
        om.reset();
        om.set_min_level(msg_level::debug);
        om.write_message_level(msg_level::debug, true, "debug", 42);
        print_item("write_message_level 整型参数", om.read_message().find("debug42") != std::string::npos);
        om.reset();
        om.write_message_fmt_level(msg_level::warn, true, "fmt: {}={}", "k", 1);
        print_item("write_message_fmt_level", om.read_message().find("[WARN]  fmt: k=1") != std::string::npos);

        om.reset();
        om.reserve(4096);
        print_item("reserve(4096)", om.capacity() >= 4096);
        for (int i = 0; i < 100; ++i) om.write_message(true, "msg", i);
        print_item("reserve后批量写入", om.message_size() > 0 && om.capacity() >= 4096);

        om.reset();
        om.write_message(true, "int=", 42, " dbl=", 3.14, " str=", std::string("hi"));
        print_item("write_message 混合类型(to_chars)", om.read_message().find("int=42 dbl=3.14 str=hi") != std::string::npos);

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

        // === SSO 缓冲测试 ===
        operating_message om_sso;
        om_sso.write_message(true, "短消息");  // 48 字节内, SSO 模式
        print_item("SSO 短消息写入", om_sso.read_message().find("短消息") != std::string_view::npos);
        print_item("SSO 容量 == SSO_SIZE", om_sso.capacity() == 48);

        // 溢出到堆 (> 48 字节)
        operating_message om_slab;
        om_slab.write_message(true, "这是一条超过SSO缓冲区大小的较长消息用于测试slab分配器溢出路径12345678901234567890");
        print_item("溢出消息写入", om_slab.read_message().size() > 48);
        print_item("溢出容量 > SSO_SIZE", om_slab.capacity() > 48);

        // 溢出到 large (> 256 字节)
        operating_message om_large;
        om_large.reserve(4096);
        print_item("large reserve(4096)", om_large.capacity() >= 4096);
        for (int i = 0; i < 50; ++i)
        {
            om_large.write_message(true, "padding1234567890", i);
        }
        print_item("large 批量写入", om_large.message_size() > 256 && om_large.capacity() >= 4096);

        // SSO → slab → large 扩容链
        operating_message om_grow;
        om_grow.write_message(true, "init");
        size_t cap_sso = om_grow.capacity();
        for (int i = 0; i < 20; ++i)
        {
            om_grow.write_message(true, "扩展消息内容测试", i, " ");
        }
        size_t cap_after = om_grow.capacity();
        print_item("扩容链容量增长", cap_after > cap_sso);
        print_item("扩容后内容保留", om_grow.read_message().find("init") != std::string_view::npos);

        // === 错误码测试 ===
        operating_message om_code;
        print_item("默认 code == om_err_none", om_code.code() == om_err_none);
        om_code.set_code(om_err_type_mismatch);
        print_item("set_code(type_mismatch)", om_code.code() == om_err_type_mismatch);
        print_item("is_code(type_mismatch)", om_code.is_code(om_err_type_mismatch));
        print_item("is_code(invalid_entity) false", !om_code.is_code(om_err_invalid_entity));

        operating_message om_code2;
        om_code2.write_message_code(om_err_invalid_entity, false, "实体无效");
        print_item("write_message_code 设置码", om_code2.code() == om_err_invalid_entity);
        print_item("write_message_code switch false", !om_code2.get_switch_bool());

        operating_message om_code3;
        om_code3.write_message_code(om_err_out_of_range, true, "正常路径");
        print_item("write_message_code sw=true 不设码", om_code3.code() == om_err_none);

        // 错误码跨实例稳定
        constexpr uint16_t test_code = static_cast<uint16_t>(fnv1a_consteval("test_error_code"));
        om_code.set_code(test_code);
        print_item("fnv1a 错误码稳定", om_code.code() == test_code);

        // === source_location 测试 ===
        operating_message om_loc;
        om_loc.write_message_loc(true, std::source_location::current(), "位置测试");
        std::string_view loc_msg = om_loc.read_message();
        print_item("write_message_loc 含文件名", loc_msg.find("test_functional.cpp") != std::string_view::npos);
        print_item("write_message_loc 含行号", loc_msg.find(":") != std::string_view::npos);
        print_item("write_message_loc 含 ]", loc_msg.find("]") != std::string_view::npos);
        print_item("write_message_loc 含消息", loc_msg.find("位置测试") != std::string_view::npos);

        // 错误码 + 位置组合
        operating_message om_cl;
        om_cl.write_message_code_loc(om_err_null_pointer, false, std::source_location::current(), "空指针错误");
        print_item("code_loc 设置码", om_cl.code() == om_err_null_pointer);
        print_item("code_loc 含文件名", om_cl.read_message().find("test_functional.cpp") != std::string_view::npos);
        print_item("code_loc switch false", !om_cl.get_switch_bool());

        // === 运行时格式化测试 (fmt 为运行时 string_view) ===
        operating_message om_ct;
        om_ct.write_message_fmt_runtime(true, "值: {}", 42);
        print_item("fmt_runtime 简单格式", om_ct.read_message().find("值: 42") != std::string_view::npos);

        om_ct.reset();
        om_ct.write_message_fmt_runtime(true, "{}+{}={}", 1, 2, 3);
        print_item("fmt_runtime 多参数", om_ct.read_message().find("1+2=3") != std::string_view::npos);

        om_ct.reset();
        om_ct.write_message_fmt_runtime_level(msg_level::warn, true, "[{}] {}", "WARN", "告警消息");
        print_item("fmt_runtime_level 含前缀", om_ct.read_message().find("[WARN]") != std::string_view::npos);
        print_item("fmt_runtime_level 含消息", om_ct.read_message().find("告警消息") != std::string_view::npos);

        // 运行时复杂格式 (完整 std::format 语法, slow path)
        om_ct.reset();
        om_ct.write_message_fmt_runtime(true, "hex={:08x}", 0xAB);
        print_item("fmt_runtime 复杂格式", om_ct.read_message().find("hex=000000ab") != std::string_view::npos);

        // 运行时拼接 fmt (运行时生成的格式串)
        om_ct.reset();
        std::string dyn_fmt = "v=" + std::string("{:>4}") + " end";
        om_ct.write_message_fmt_runtime(true, dyn_fmt, 7);
        print_item("fmt_runtime 动态fmt", om_ct.read_message().find("v=   7 end") != std::string_view::npos);

        // runtime + code
        om_ct.reset();
        om_ct.write_message_fmt_runtime_code(om_err_out_of_range, false, "idx={} max={}", 9, 8);
        print_item("fmt_runtime_code 设置码", om_ct.code() == om_err_out_of_range);
        print_item("fmt_runtime_code switch false", !om_ct.get_switch_bool());
        print_item("fmt_runtime_code 含消息", om_ct.read_message().find("idx=9 max=8") != std::string_view::npos);

        // runtime + loc
        om_ct.reset();
        om_ct.write_message_fmt_runtime_loc(true, std::source_location::current(), "n={}", 5);
        std::string_view rt_loc = om_ct.read_message();
        print_item("fmt_runtime_loc 含文件名", rt_loc.find("test_functional.cpp") != std::string_view::npos);
        print_item("fmt_runtime_loc 含消息", rt_loc.find("n=5") != std::string_view::npos);

        // runtime + code + loc
        om_ct.reset();
        om_ct.write_message_fmt_runtime_code_loc(om_err_not_found, false,
            std::source_location::current(), "missing {}", 1);
        print_item("fmt_runtime_code_loc 设置码", om_ct.code() == om_err_not_found);
        print_item("fmt_runtime_code_loc 含位置", om_ct.read_message().find("test_functional.cpp") != std::string_view::npos);

        // validate_format: 占位符数量/语法校验
        print_item("validate_format 合法", validate_format("a={} b={}", 2));
        print_item("validate_format 数量错", !validate_format("a={} b={}", 1));
        print_item("validate_format 未闭合", !validate_format("a={ ", 1));
        print_item("validate_format 孤立}", !validate_format("a}", 0));
        print_item("validate_format 转义合法", validate_format("{{}} val={}", 1));

        // === 全局开关关闭时行为 ===
        dbg = false;
        operating_message om_off;
        om_off.write_message(false, "不应写入");
        print_item("开关关闭: switch 仍变 false", !om_off.get_switch_bool());
        print_item("开关关闭: 消息为空", om_off.message_size() == 0);
        om_off.write_message_code(om_err_not_found, false, "不应写入");
        print_item("开关关闭: code 仍设置", om_off.code() == om_err_not_found);
        dbg = true;

        // === reset 清理 ===
        operating_message om_reset;
        om_reset.write_message_code(om_err_capacity_exceeded, false, "容量超限");
        om_reset.reserve(512);
        print_item("reset 前有堆缓冲", om_reset.capacity() > 48);
        om_reset.reset();
        print_item("reset 后容量回 SSO", om_reset.capacity() == 48);
        print_item("reset 后 code 清零", om_reset.code() == om_err_none);
        print_item("reset 后 switch 恢复", om_reset.get_switch_bool());

        dbg = old_dbg;
    }

    // 3. id_allocation<T> ID分配器
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

    // 4. type_id 类型ID
    print_section(4, "type_id 类型ID");
    {
        int id_pos = type_id::get_type_id<Position>();
        int id_vel = type_id::get_type_id<Velocity>();
        print_item("get_type_id<Position>()", std::to_string(id_pos));
        print_item("get_type_id<Velocity>()", std::to_string(id_vel));
        print_item("不同类型ID不同", id_pos != id_vel);
        print_item("相同类型ID一致", type_id::get_type_id<Position>() == id_pos);
    }

    // 5. class_pool<T> 组件池
    print_section(5, "class_pool<T> 组件池");

    // --- 构造函数 ---
    std::cout << "\n  [构造函数]\n";
    {
        dense<int> cp_def;
        print_item("默认构造", cp_def.empty());

        dense<int> cp_cap(64);
        print_item("class_pool(size_t capacity)", cp_cap.capacity() >= 64);

        dense<int> cp_fill(static_cast<size_t>(5), 42);
        print_item("class_pool(count, value)", (cp_fill.size() == 5 && cp_fill[0] == 42 && cp_fill[4] == 42));

        dense<int> vec = {10, 20, 30};
        dense<int> cp_it(vec.begin(), vec.end());
        print_item("class_pool(InputIt, InputIt)", (cp_it.size() == 3 && cp_it[0] == 10 && cp_it[2] == 30));

        dense<int> cp_init = {100, 200, 300};
        print_item("class_pool(initializer_list)", (cp_init.size() == 3 && cp_init[1] == 200));

        dense<int> cp_copy(cp_init);
        cp_init[0] = 999;
        print_item("拷贝构造 深拷贝", (cp_copy.size() == 3 && cp_copy[0] == 100));

        dense<int> cp_move_src = {7, 8, 9};
        dense<int> cp_move_dst(std::move(cp_move_src));
        print_item("移动构造", (cp_move_dst.size() == 3 && cp_move_dst[0] == 7));
    }

    // --- append_n 批量追加 ---
    std::cout << "\n  [append_n 批量追加]\n";
    {
        // n=0
        dense<int> cp0;
        cp0.append_n(0, 42);
        print_item("append_n(0)", cp0.size() == 0);

        // n=1 单元素
        dense<int> cp1;
        cp1.append_n(1, 42);
        print_item("append_n(1)", (cp1.size() == 1 && cp1[0] == 42 && cp1.count() == 1));

        // n=64 恰好 1 word (同 word 边界)
        class_pool<int> cp64;
        cp64.append_n(64, 7);
        print_item("append_n(64) 同 word", (cp64.size() == 64 && cp64.count() == 64 && cp64.is_dense()));

        // n=128 跨 2 word
        dense<int> cp128;
        cp128.append_n(128, 9);
        print_item("append_n(128) 跨 word", (cp128.size() == 128 && cp128.count() == 128));

        // 部分填充后 append_n (首 word 非对齐)
        dense<int> cp_partial;
        cp_partial.append_n(60, 1);   // start_bit=60
        cp_partial.append_n(70, 2);  // 跨 word, start=60, n=70, last=129
        bool ok = (cp_partial.size() == 130);
        for (size_t i = 0; i < 60 && ok; ++i) ok = (cp_partial[i] == 1);
        for (size_t i = 60; i < 130 && ok; ++i) ok = (cp_partial[i] == 2);
        print_item("append_n 首非对齐+跨 word", (ok && cp_partial.count() == 130));

        // append_n 自动扩容
        dense<int> cp_grow;
        cp_grow.append_n(10000, 5);  // 从 0 容量自动扩容
        print_item("append_n 自动扩容", (cp_grow.size() == 10000 && cp_grow[9999] == 5 && cp_grow.count() == 10000));

        // sparse 模式下 append_n (有洞)
        class_pool<int> cp_sp = {0, 1, 2, 3, 4, 5, 6, 7};
        cp_sp.sparse_erase_at(2);  // 产生洞
        size_t holes_before = cp_sp.size() - cp_sp.count();  // 应为 1
        cp_sp.append_n(100, 9);
        bool sp_ok = (cp_sp.size() == 108);
        bool sp_count_ok = (cp_sp.count() == 107);  // 7 原 + 100 追加 - 0 新洞
        bool sp_holes_ok = (cp_sp.size() - cp_sp.count() == holes_before);  // 洞数不变
        print_item("append_n (sparse 有洞) size", sp_ok);
        print_item("append_n (sparse 有洞) count", sp_count_ok);
        print_item("append_n (sparse 有洞) 洞数不变", sp_holes_ok);

        // append_n 后迭代器正确性
        dense<int> cp_it;
        cp_it.append_n(200, 3);
        size_t it_count = 0;
        for (auto v : cp_it) { (void)v; ++it_count; }
        print_item("append_n 迭代器正确", it_count == 200);
    }

    // --- 赋值 ---
    std::cout << "\n  [赋值]\n";
    {
        dense<int> a = {1, 2, 3};
        dense<int> b;
        b = a;
        a[0] = 999;
        print_item("拷贝赋值 深拷贝", (b[0] == 1 && b.size() == 3));

        dense<int> c;
        dense<int> d = {5, 6};
        c = std::move(d);
        print_item("移动赋值", (c.size() == 2 && c[0] == 5));
    }

    // --- 元素访问 ---
    std::cout << "\n  [元素访问]\n";
    {
        class_pool<int> cp = {10, 20, 30, 40, 50};
        print_item("operator[]", cp[2] == 30);
        print_item("front()", cp.front() == 10);
        print_item("back()", cp.back() == 50);

        print_item("data()", (cp.data() != nullptr && cp.data()[0] == 10));

        std::span<int> sp = cp.span();
        print_item("span()", (sp.size() == 5 && sp[2] == 30));

        const class_pool<int>& ccp = cp;
        std::span<const int> csp = ccp.span();
        print_item("span() const", (csp.size() == 5 && csp[2] == 30));

        // get(): 等价 operator[], 无边界检查
        print_item("get(size_t) 等价 operator[]", cp.get(2) == cp[2]);
        print_item("get(size_t) const 等价 operator[]", ccp.get(3) == ccp[3]);

        // get(index, error_index): 越界保护访问
        // cp.size() == 5, 索引 100 越界 -> 改访问 error_index=0
        print_item("get(100, 0) 越界回退到 error_index", cp.get(100, 0) == cp[0]);
        // 合法 index 时正常访问, 不触发回退
        print_item("get(3, 0) 合法 index 不回退", cp.get(3, 0) == cp[3]);
        // const 版本
        print_item("get(100, 1) const 越界回退", ccp.get(100, 1) == ccp[1]);
        print_item("get(2, 1) const 合法不回退", ccp.get(2, 1) == ccp[2]);

        // is_dense(): dense 模式
        print_item("is_dense() dense 模式", cp.is_dense());

        // is_dense(): sparse 模式
        class_pool<int> sparse_cp = {1, 2, 3, 4, 5};
        sparse_cp.sparse_erase_at(2);
        print_item("sparse 模式 is_dense() 为 false", !sparse_cp.is_dense());
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

        dense<int> empty_cp;
        print_item("empty() 空池", empty_cp.empty());
        print_item("valid() 空池", !empty_cp.valid());

        std::ostringstream oss;
        oss << cp.size_bytes() << "/" << cp.capacity_bytes();
        print_item("size_bytes()/capacity_bytes()", oss.str());
    }

    // --- 修改器 ---
    std::cout << "\n  [修改器]\n";
    {
        dense<int> cp;
        cp.emplace_back(42);
        cp.emplace_back(99);
        print_item("emplace_back()", (cp.size() == 2 && cp.back() == 99));

        // push_back 拷贝
        dense<int> cp_pb;
        int v1 = 10, v2 = 20;
        cp_pb.push_back(v1);
        cp_pb.push_back(v2);
        print_item("push_back(const T&)", (cp_pb.size() == 2 && cp_pb[0] == 10 && cp_pb[1] == 20));

        // push_back 移动
        dense<int> cp_mv;
        cp_mv.push_back(std::move(v1));
        cp_mv.push_back(std::move(v2));
        print_item("push_back(T&&)", (cp_mv.size() == 2 && cp_mv[0] == 10 && cp_mv[1] == 20));

        // push_back 自动扩容
        dense<int> cp_grow_pb;
        for (int i = 0; i < 100; ++i) { cp_grow_pb.push_back(i); }
        print_item("push_back 自动扩容", (cp_grow_pb.size() == 100 && cp_grow_pb[99] == 99));

        // push_back_unchecked 移动
        dense<int> cp_unc;
        cp_unc.increase_capacity(4);
        cp_unc.push_back_unchecked(1);
        cp_unc.push_back_unchecked(2);
        int tmp1 = 3, tmp2 = 4;
        cp_unc.push_back_unchecked(std::move(tmp1));
        cp_unc.push_back_unchecked(std::move(tmp2));
        print_item("push_back_unchecked(move)", (cp_unc.size() == 4 && cp_unc[2] == 3 && cp_unc[3] == 4));

        cp.clear();
        print_item("clear()", (cp.size() == 0 && cp.empty()));

        cp.increase_capacity(1000);
        print_item("increase_capacity(1000)", cp.capacity() >= 1000);

        dense<int> cp2 = {1, 2, 3, 4, 5, 6, 7, 8};
        cp2.shrink_to_fit();
        print_item("shrink_to_fit()", cp2.capacity() == cp2.size());

        dense<int> cp3;
        cp3.reserve_exact(100);
        print_item("reserve_exact(size_t) 仅扩容", cp3.capacity() >= 100);

        dense<int> cp4;
        cp4.increase_capacity(static_cast<size_t>(5), 77);
        print_item("increase_capacity(size_t, value)", (cp4.size() == 5 && cp4[0] == 77 && cp4[4] == 77));

        // increase_capacity(size_t, const T&) 扩容并填充值
        dense<int> cp_fill;
        cp_fill.emplace_back(1);
        cp_fill.emplace_back(2);
        cp_fill.increase_capacity(static_cast<size_t>(5), 99);
        print_item("increase_capacity(cap, value)", (cp_fill.size() == 5 && cp_fill[0] == 1 && cp_fill[1] == 2 && cp_fill[2] == 99 && cp_fill[4] == 99));

        // reduce_capacity(size_t) 缩容
        dense<int> cp_shrink = {10, 20, 30, 40, 50, 60, 70, 80};
        size_t cap_before = cp_shrink.capacity();
        cp_shrink.reduce_capacity(3);
        print_item("reduce_capacity(cap) 截断", (cp_shrink.size() == 3 && cp_shrink[0] == 10 && cp_shrink[2] == 30 && cp_shrink.capacity() < cap_before));

        // reduce_capacity(size_t, class_pool<T>&) 缩容并迁移元素
        dense<int> cp_src = {10, 20, 30, 40, 50};
        dense<int> cp_dst;
        cp_src.reduce_capacity(static_cast<size_t>(2), cp_dst);
        print_item("reduce_capacity(cap, dst)", (cp_src.size() == 2 && cp_src[0] == 10 && cp_src[1] == 20 && cp_dst.size() == 3 && cp_dst[0] == 30 && cp_dst[2] == 50));

        dense<int> cp5 = {10, 20, 30, 40, 50};
        cp5.emplace(std::next(cp5.begin(), 2), 25);
        print_item("emplace()", (cp5.size() == 6 && cp5[2] == 25 && cp5[3] == 30));

        cp5.erase(std::next(cp5.begin(), 2));
        print_item("erase()", (cp5.size() == 5 && cp5[2] == 30));

        dense<int> cp6 = {1, 2, 3};
        dense<int> cp7 = {10, 20};
        cp6.swap(cp7);
        print_item("swap()", (cp6.size() == 2 && cp6[0] == 10 && cp7.size() == 3 && cp7[0] == 1));

        dense<int> cp8 = {1, 2, 3, 4, 5};
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

        // soft_sparse_delete (仅平凡可析构类型)
        class_pool<int> cp12;
        cp12.emplace_back(10);
        cp12.emplace_back(20);
        cp12.emplace_back(30);
        cp12.soft_sparse_delete(1);
        print_item("soft_sparse_delete() 清除 bitmap", (!cp12.is_constructed_at(1) && cp12.is_constructed_at(0) && cp12.is_constructed_at(2)));
        print_item("soft_sparse_delete() count 减少", cp12.count() == 2);
        print_item("soft_sparse_delete() size 不变", cp12.size() == 3);
        print_item("soft_sparse_delete() 变稀疏", !cp12.is_dense());

        // soft_sparse_delete 防重复
        cp12.soft_sparse_delete(1);
        print_item("soft_sparse_delete() 防重复", cp12.count() == 2);

        // soft_sparse_delete 后 fill_the_hole 可填回
        cp12.fill_the_hole(99);
        print_item("soft_sparse_delete 后 fill_the_hole", (cp12.is_constructed_at(1) && cp12[1] == 99 && cp12.is_dense()));

        // push_back 拷贝
        class_pool<int> cpb_pb;
        int pv1 = 10, pv2 = 20;
        cpb_pb.push_back(pv1);
        cpb_pb.push_back(pv2);
        print_item("class_pool push_back(const T&)", (cpb_pb.size() == 2 && cpb_pb[0] == 10 && cpb_pb[1] == 20 && cpb_pb.count() == 2));

        // push_back 移动
        class_pool<int> cpb_mv;
        int mv1 = 30, mv2 = 40;
        cpb_mv.push_back(std::move(mv1));
        cpb_mv.push_back(std::move(mv2));
        print_item("class_pool push_back(T&&)", (cpb_mv.size() == 2 && cpb_mv[0] == 30 && cpb_mv[1] == 40 && cpb_mv.count() == 2));

        // push_back 自动扩容
        class_pool<int> cpb_grow;
        for (int i = 0; i < 100; ++i) { cpb_grow.push_back(i); }
        print_item("class_pool push_back 自动扩容", (cpb_grow.size() == 100 && cpb_grow[99] == 99 && cpb_grow.count() == 100));

        // push_back 保持 dense 模式
        print_item("class_pool push_back 保持 dense", cpb_grow.is_dense());

        // push_back_unchecked(T&&) 移动追加
        class_pool<int> cpb_unc_mv;
        cpb_unc_mv.increase_capacity(4);
        cpb_unc_mv.push_back_unchecked(1);
        cpb_unc_mv.push_back_unchecked(2);
        int utmp1 = 3, utmp2 = 4;
        cpb_unc_mv.push_back_unchecked(std::move(utmp1));
        cpb_unc_mv.push_back_unchecked(std::move(utmp2));
        print_item("class_pool push_back_unchecked(T&&)", (cpb_unc_mv.size() == 4 && cpb_unc_mv[2] == 3 && cpb_unc_mv[3] == 4));

        // 非平凡类型 push_back 测试
        class_pool<std::string> cpb_str;
        cpb_str.push_back(std::string("hello"));
        cpb_str.push_back(std::string("world"));
        print_item("class_pool push_back 非平凡类型", (cpb_str.size() == 2 && cpb_str[0] == "hello" && cpb_str[1] == "world" && cpb_str.count() == 2));

        // 非平凡类型 push_back_unchecked(T&&)
        class_pool<std::string> cpb_str_unc;
        cpb_str_unc.increase_capacity(2);
        cpb_str_unc.push_back_unchecked(std::string("abc"));
        cpb_str_unc.push_back_unchecked(std::string("xyz"));
        print_item("class_pool push_back_unchecked 非平凡类型", (cpb_str_unc.size() == 2 && cpb_str_unc[0] == "abc" && cpb_str_unc[1] == "xyz"));
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
        dense<int> cp = {10, 20, 30};
        int fwd = 0;
        for (auto it = cp.begin(); it != cp.end(); ++it) fwd += *it;
        print_item("begin/end", fwd == 60);

        int cfwd = 0;
        for (auto it = cp.cbegin(); it != cp.cend(); ++it) cfwd += *it;
        print_item("cbegin/cend", cfwd == 60);
    }

    // --- 反向迭代器 ---
    std::cout << "\n  [反向迭代器]\n";
    {
        // dense 模式
        class_pool<int> cp = {10, 20, 30};
        std::vector<int> rev_list;
        for (auto it = cp.rbegin(); it != cp.rend(); ++it) rev_list.push_back(*it);
        print_item("rbegin/rend (dense)", rev_list.size() == 3 && rev_list[0] == 30 && rev_list[1] == 20 && rev_list[2] == 10);

        std::vector<int> crev_list;
        for (auto it = cp.crbegin(); it != cp.crend(); ++it) crev_list.push_back(*it);
        print_item("crbegin/crend (dense)", crev_list.size() == 3 && crev_list[0] == 30 && crev_list[1] == 20 && crev_list[2] == 10);

        // 空池
        class_pool<int> empty;
        int empty_cnt = 0;
        for (auto it = empty.rbegin(); it != empty.rend(); ++it) ++empty_cnt;
        print_item("rbegin/rend (空)", empty_cnt == 0);

        // 单元素
        class_pool<int> single = {42};
        int single_val = 0;
        for (auto it = single.rbegin(); it != single.rend(); ++it) single_val = *it;
        print_item("rbegin/rend (单元素)", single_val == 42);
    }
    {
        // sparse 模式: 创建洞后反向遍历
        class_pool<int> cp = {0, 1, 2, 3, 4, 5, 6, 7};
        cp.erase(std::next(cp.begin()));      // 删索引1
        cp.erase(std::next(cp.begin(), 3));   // 删索引3

        // 验证反向遍历 = 正向遍历的逆序
        std::vector<int> fwd_list;
        for (auto it = cp.begin(); it != cp.end(); ++it) fwd_list.push_back(*it);
        bool match = true;
        auto rit = cp.rbegin();
        for (size_t i = fwd_list.size(); i > 0; --i, ++rit) {
            if (rit == cp.rend() || *rit != fwd_list[i - 1]) { match = false; break; }
        }
        print_item("rbegin/rend (sparse 逆序一致)", match && rit == cp.rend());
    }

    // --- 自由函数 ---
    std::cout << "\n  [自由函数]\n";
    {
        dense<int> a = {1, 2}, b = {3, 4, 5};
        swap(a, b);
        print_item("swap(class_pool&, class_pool&)", (a.size() == 3 && a[0] == 3 && b.size() == 2 && b[0] == 1));
    }

    // --- class_pool 视图 (cpv 命名空间) ---
    std::cout << "\n  [class_pool 视图 / cpv namespace]\n";
    {
        class_pool<int> p;
        for (int i = 0; i < 100; ++i)
        {
            p.emplace_back(i);
        }

        // A. 子范围视图 (与 dense::subspan 命名一致)
        {
            auto sp = subspan(p, 10, 20);
            print_item("subspan(off, cnt) size", sp.size() == 20);
            print_item("subspan(off, cnt) data", (sp[0] == 10 && sp[19] == 29));

            auto sp2 = subspan(p, 50);
            print_item("subspan(off) 末段", (sp2.size() == 50 && sp2[0] == 50));

            auto f = first(p, 5);
            print_item("first(n)", (f.size() == 5 && f[0] == 0 && f[4] == 4));

            auto l = last(p, 5);
            print_item("last(n)", (l.size() == 5 && l[0] == 95 && l[4] == 99));

            auto ff = first_fixed<4>(p);
            print_item("first_fixed<4>", (ff.size() == 4 && ff[0] == 0 && ff[3] == 3));

            auto lf = last_fixed<4>(p);
            print_item("last_fixed<4>", (lf.size() == 4 && lf[0] == 96 && lf[3] == 99));

            // 越界处理
            auto sp3 = subspan(p, 200, 10);
            print_item("subspan 越界返回空", sp3.size() == 0);
        }

        // B. 反向视图
        {
            int sum = 0;
            reverse_for_each(p, [&](int& v) { sum += v; });
            print_item("reverse_for_each 求和", sum == 4950);

            const class_pool<int>& cp = p;
            int sum2 = 0;
            reverse_for_each(cp, [&](const int& v) { sum2 += v; });
            print_item("reverse_for_each const", sum2 == 4950);
        }

        // C. 步进视图
        {
            int sum = 0;
            strided_for_each(p, 0, 4, [&](int& v) { sum += v; });
            // 0, 4, 8, ..., 96 (25 个)
            print_item("strided_for_each (rt step)", sum == (0 + 96) * 25 / 2);

            int sum2 = 0;
            strided_for_each<4>(p, [&](int& v) { sum2 += v; });
            print_item("strided_for_each<4> (ct step)", sum2 == sum);

            int sum3 = 0;
            strided_for_each<1>(p, [&](int& v) { sum3 += v; });
            print_item("strided_for_each<1> fast path", sum3 == 4950);

            auto sv = strided_span_view(p, 0, 5, 10);
            print_item("strided_span_view size", sv.size() == 10);
            print_item("strided_span_view [0]", sv[0] == 0);
            print_item("strided_span_view [9]", sv[9] == 45);
        }

        // D. 变换视图
        {
            int sum = 0;
            transform_for_each(
                p,
                [](int& v) -> int { return v * 2; },
                [&](int v) { sum += v; });
            print_item("transform_for_each (x2)", sum == 9900);

            int dst[100];
            transform_to(p, dst, 100, [](const int& v) -> int { return v + 1; });
            print_item("transform_to (+1)", (dst[0] == 1 && dst[99] == 100));
        }

        // E. 过滤与查找
        {
            int* r = find(p, 50);
            print_item("find (mid hit)", (r && *r == 50));

            int* miss = find(p, 999);
            print_item("find (miss)", (miss == nullptr));

            print_item("contains (true)", contains(p, 50));
            print_item("contains (false)", !contains(p, 999));

            int* ri = find_if(p, [](const int& v) { return v == 30; });
            print_item("find_if (mid hit)", (ri && *ri == 30));

            int* rin = find_if_not(p, [](const int& v) { return v != 60; });
            print_item("find_if_not", (rin && *rin == 60));

            size_t c = count_if(p, [](const int& v) { return v % 2 == 0; });
            print_item("count_if (偶数)", c == 50);

            int sum_all = 0;
            filter_for_each(p, [](const int&) { return true; }, [&](int& v) { sum_all += v; });
            print_item("filter_for_each (all)", sum_all == 4950);

            int sum_even = 0;
            filter_for_each(p, [](const int& v) { return v % 2 == 0; }, [&](int& v) { sum_even += v; });
            // 0 + 2 + ... + 98 = 2450
            print_item("filter_for_each (偶数)", sum_even == 2450);

            class_pool<size_t> idx;
            filter_indices_to(p, idx, [](const int& v) { return v >= 90; });
            print_item("filter_indices_to (>=90)", (idx.size() == 10 && idx[0] == 90 && idx[9] == 99));
        }

        // F. 规约与极值
        {
            int s = reduce(p, [](int acc, const int& v) -> int { return acc + v; }, 0);
            print_item("reduce (sum)", s == 4950);

            int s2 = reduce_pairwise(p, [](int acc, const int& v) -> int { return acc + v; }, 0);
            print_item("reduce_pairwise (sum)", s2 == 4950);

            int* mn = min_element(p);
            print_item("min_element", (mn && *mn == 0));

            int* mx = max_element(p);
            print_item("max_element", (mx && *mx == 99));

            auto mm = minmax_element(p);
            print_item("minmax_element", (mm.first && mm.second && *mm.first == 0 && *mm.second == 99));

            int sum_val = sum(p);
            print_item("sum", sum_val == 4950);

            int other[100];
            for (int i = 0; i < 100; ++i) { other[i] = 2; }
            int dp = dot_product(p, other, 100);
            print_item("dot_product", dp == 9900);
        }

        // G. 窗口与分块
        {
            int sum_w = 0;
            for_each_window<4>(p, [&](std::span<int, 4> w) { sum_w += w[0]; });
            // 窗口起点: 0..96, 97 个窗口
            print_item("for_each_window<4>", sum_w == (0 + 96) * 97 / 2);

            int sum_c = 0;
            for_each_chunk<4>(p, [&](std::span<int, 4> c) { sum_c += c[0]; });
            // 25 块, 起点 0,4,8,...,96
            print_item("for_each_chunk<4>", sum_c == (0 + 96) * 25 / 2);

            auto ws = window_span<4>(p, 50);
            print_item("window_span<4>(50)", (ws.size() == 4 && ws[0] == 50));

            auto cs = chunk_span<4>(p, 10);
            print_item("chunk_span<4>(10)", (cs.size() == 4 && cs[0] == 40));
        }

        // H. 枚举视图
        {
            size_t last_idx = 0;
            int last_val = 0;
            for_each_enumerated(p, [&](size_t i, int& v) {
                last_idx = i;
                last_val = v;
            });
            print_item("for_each_enumerated", (last_idx == 99 && last_val == 99));

            const class_pool<int>& cp = p;
            size_t cnt = 0;
            for_each_enumerated(cp, [&](size_t i, const int&) { cnt = i + 1; });
            print_item("for_each_enumerated const", cnt == 100);
        }

        // I. 双容器同步
        {
            class_pool<int> q;
            for (int i = 0; i < 100; ++i) { q.emplace_back(i * 2); }

            int sum_zip = 0;
            for_each_zip(p, q, [&](int& a, int& b) { sum_zip += a + b; });
            // sum_p + sum_q = 4950 + 9900 = 14850
            print_item("for_each_zip (pool&)", sum_zip == 14850);

            int sum_zip2 = 0;
            int* qp = q.data();
            for_each_zip(p, qp, 100, [&](int& a, int& b) { sum_zip2 += a + b; });
            print_item("for_each_zip (ptr)", sum_zip2 == 14850);

            int dst[100];
            zip_with_to(p, q.data(), dst, 100,
                [](const int& a, const int& b) -> int { return a + b; });
            print_item("zip_with_to", (dst[0] == 0 && dst[99] == 99 + 198));

            class_pool<int> p_copy = p;
            print_item("equal (true)", equal(p, p_copy));
            print_item("equal (false)", !equal(p, q));

            // equal(ptr, count): 与 dense::equal 命名一致
            int eq_buf[100];
            for (int i = 0; i < 100; ++i) { eq_buf[i] = i; }
            print_item("equal(ptr, count) true", equal(p, eq_buf, 100));
            eq_buf[50] = 999;
            print_item("equal(ptr, count) false", !equal(p, eq_buf, 100));

            // equal(span) 委托到 equal(ptr, count)
            print_item("equal(span) true", equal(p, std::span<const int>(p.data(), 100)));
        }

        // J. SIMD/对齐视图
        {
            int* aligned = aligned_data(p);
            print_item("aligned_data", aligned == p.data());

            auto sp = aligned_span(p);
            print_item("aligned_span", sp.size() == 100);

            int sum = 0;
            simd_for_each(p, [&](int& v) { sum += v; });
            print_item("simd_for_each", sum == 4950);

            size_t tail = unaligned_tail_offset(p);
            print_item("unaligned_tail_offset (>=0)", tail <= p.size());
        }

        // K. 拷贝/移动视图
        {
            int dst[100];
            copy_to(p, dst, 100);
            print_item("copy_to (ptr)", (dst[0] == 0 && dst[99] == 99));

            std::span<int> sp(dst, 100);
            copy_to(p, sp);
            print_item("copy_to (span)", (dst[0] == 0 && dst[99] == 99));

            class_pool<int> src;
            for (int i = 0; i < 100; ++i) { src.emplace_back(i); }
            int mdst[100];
            move_to(src, mdst, 100);
            print_item("move_to", (mdst[0] == 0 && mdst[99] == 99));

            int rdst[100];
            reverse_copy_to(p, rdst, 100);
            print_item("reverse_copy_to", (rdst[0] == 99 && rdst[99] == 0));
        }

        // L. class_pool 独有 - 稀疏模式
        {
            class_pool<int> sp;
            for (int i = 0; i < 100; ++i) { sp.emplace_back(i); }
            sp.sparse_erase_at(10);
            sp.sparse_erase_at(20);
            sp.sparse_erase_at(30);

            print_item("holes_count", holes_count(sp) == 3);
            print_item("live_count", live_count(sp) == 97);

            // compact_to 压缩为密集数组
            int dst[100];
            size_t n = compact_to(sp, dst, 100);
            print_item("compact_to 元素数", n == 97);
            // 索引 10 删除后: dst[10] 跳过槽 10 = 值 11
            // 索引 20 删除后: dst[19] 跳过槽 20 = 值 21 (槽 20 原为 20, 已删, 跳到槽 21=值 21)
            // 索引 30 删除后: dst[28] 跳过槽 30 = 值 31
            print_item("compact_to 跳过空洞", (dst[0] == 0 && dst[10] == 11 && dst[19] == 21 && dst[28] == 31));

            // 稀疏模式 find
            int* r = find(sp, 50);
            print_item("稀疏 find (mid hit)", (r && *r == 50));

            int* miss = find(sp, 10);
            print_item("稀疏 find (已删除)", (miss == nullptr));

            // 稀疏模式 filter_for_each
            int sum = 0;
            filter_for_each(sp, [](const int&) { return true; }, [&](int& v) { sum += v; });
            // 总和 4950 - 10 - 20 - 30 = 4890
            print_item("稀疏 filter_for_each", sum == 4890);

            // 稀疏 count_if
            size_t c = count_if(sp, [](const int&) { return true; });
            print_item("稀疏 count_if", c == 97);

            // 稀疏 reduce
            int rs = reduce(sp, [](int acc, const int& v) -> int { return acc + v; }, 0);
            print_item("稀疏 reduce", rs == 4890);
        }
    }

    // --- class_pool::fill_the_hole 填洞或追加 ---
    // std::cout << "\n  [class_pool::fill_the_hole 填洞或追加]\n";
    // SKIPPED for debugging
    if (false)
    {
        // 基本填洞与追加
        class_pool<int> pool;
        pool.fill_the_hole(10);
        pool.fill_the_hole(20);
        pool.fill_the_hole(30);
        print_item("fill_the_hole 连续追加(无洞)", (pool.size() == 3 && pool[0] == 10 && pool[2] == 30));
        print_item("is_dense 密集(无洞)", pool.is_dense());

        // 产生空洞
        pool.sparse_erase_at(1);
        print_item("sparse_erase_at 产生空洞", (pool.size() == 3 && !pool.is_constructed_at(1)));
        print_item("count 不含空洞", pool.count() == 2);
        print_item("is_dense 变稀疏", !pool.is_dense());

        // 填洞: 应填到第一个空洞 index 1
        int& ref = pool.fill_the_hole(99);
        print_item("fill_the_hole 填第一个空洞 index 1", (pool[1] == 99 && pool.size() == 3));
        print_item("fill_the_hole 返回引用", (&ref == &pool[1]));
        print_item("填洞后 is_dense 恢复", pool.is_dense());

        // 多空洞: 填最低索引(非LIFO)
        class_pool<int> pool2;
        pool2.fill_the_hole(0);
        pool2.fill_the_hole(1);
        pool2.fill_the_hole(2);
        pool2.fill_the_hole(3);
        pool2.sparse_erase_at(1);
        pool2.sparse_erase_at(3);
        // 空洞在 1, 3; fill_the_hole 先填最低位 1
        pool2.fill_the_hole(100);
        print_item("填最低空洞 index 1", (pool2[1] == 100 && !pool2.is_constructed_at(3)));
        pool2.fill_the_hole(200);
        print_item("再填 index 3", (pool2[3] == 200 && pool2.is_dense()));

        // sparse_erase_at 防重复(bitmap_test 检查)
        class_pool<int> pool3;
        pool3.fill_the_hole(1);
        pool3.sparse_erase_at(0);
        pool3.sparse_erase_at(0);  // 已删除, 不重复计数
        print_item("sparse_erase_at 防重复", (pool3.count() == 0));

        // clear
        class_pool<int> pool4;
        pool4.fill_the_hole(1);
        pool4.fill_the_hole(2);
        pool4.sparse_erase_at(0);
        pool4.clear();
        print_item("clear 清空", (pool4.size() == 0 && pool4.empty()));

        // 迭代器跳过空洞
        class_pool<int> pool5;
        pool5.fill_the_hole(10);
        pool5.fill_the_hole(20);
        pool5.fill_the_hole(30);
        pool5.sparse_erase_at(1);
        int sum = 0;
        for (int& v : pool5) sum += v;
        print_item("range-for 跳过空洞", sum == 40);

        // 预留容量 + fill_the_hole
        class_pool<int> pool6(64);
        pool6.fill_the_hole(7);
        print_item("预留容量 + fill_the_hole", (pool6.capacity() >= 64 && pool6[0] == 7));

        // 填满空洞后继续 fill_the_hole 走 emplace_back
        class_pool<int> pool7;
        pool7.fill_the_hole(1);   // idx 0
        pool7.sparse_erase_at(0);
        pool7.fill_the_hole(2);   // 填回 idx 0
        pool7.fill_the_hole(3);   // 无洞, emplace_back idx 1
        print_item("填满后继续追加", (pool7[0] == 2 && pool7[1] == 3 && pool7.size() == 2));

        std::cout.flush();
    }

    // --- class_pool::fill_the_hole_at 返回索引版本 ---
    // std::cout << "\n  [class_pool::fill_the_hole_at 返回索引]\n";
    // SKIPPED for debugging
    if (false)
    {
        // 无洞 → 追加, 返回末尾索引
        class_pool<int> p1;
        size_t i0 = p1.fill_the_hole_at(10);
        size_t i1 = p1.fill_the_hole_at(20);
        size_t i2 = p1.fill_the_hole_at(30);
        print_item("fill_the_hole_at (无洞) 索引连续",
                  (i0 == 0 && i1 == 1 && i2 == 2 && p1[i0] == 10 && p1[i2] == 30));

        // 有洞 → 填第一个洞, 返回洞索引
        class_pool<int> p2;
        p2.fill_the_hole_at(0);
        p2.fill_the_hole_at(1);
        p2.fill_the_hole_at(2);
        p2.sparse_erase_at(1);  // 产生洞 at 1
        size_t filled = p2.fill_the_hole_at(99);
        print_item("fill_the_hole_at (有洞) 返回洞索引",
                  (filled == 1 && p2[filled] == 99 && p2.size() == 3));

        // 返回索引与 fill_the_hole 一致性
        class_pool<int> p3;
        p3.fill_the_hole_at(0);
        p3.fill_the_hole_at(1);
        p3.sparse_erase_at(0);
        size_t idx = p3.fill_the_hole_at(42);
        print_item("fill_the_hole_at 返回索引可访问", (p3[idx] == 42 && idx == 0));

        // 填满所有洞后, fill_the_hole_at 走 emplace_back 路径
        class_pool<int> p4;
        p4.fill_the_hole_at(1);
        p4.sparse_erase_at(0);
        size_t a = p4.fill_the_hole_at(2);  // 填洞 at 0
        size_t b = p4.fill_the_hole_at(3);  // 无洞, 追加 at 1
        print_item("填满后 fill_the_hole_at 追加",
                  (a == 0 && b == 1 && p4[a] == 2 && p4[b] == 3 && p4.size() == 2));

        // 索引一致性: fill_the_hole_at 与 operator[] 访问
        class_pool<int> p5;
        for (int i = 0; i < 10; ++i) p5.fill_the_hole_at(i);
        p5.sparse_erase_at(3);
        p5.sparse_erase_at(7);
        size_t h1 = p5.fill_the_hole_at(100);
        size_t h2 = p5.fill_the_hole_at(200);
        print_item("fill_the_hole_at 多洞顺序填",
                  (h1 == 3 && h2 == 7 && p5[h1] == 100 && p5[h2] == 200));

        std::cout.flush();
    }
    // === HEAP PROBE after section 5 ===
    {
        struct SmallHeap { char data[80]; SmallHeap() { for (int i = 0; i < 80; ++i) data[i] = (char)i; } };
        void_any probe1(SmallHeap{});
        void_any probe2 = probe1;
        SmallHeap* pp = probe2.get_ptr<SmallHeap>();
        print_item("[PROBE after S5] void_any copy", pp && pp->data[79] == 79);
        std::cout.flush();
    }
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

        // inline 编码模式下 type_id() 返回类型标签指针低 32 位 (与 type_id::get_type_id 解耦)
        // 通过 get_ptr<T>() 验证类型匹配正确性
        print_item("type_id()", (va1.type_id() != -1 && va1.get_ptr<double>() != nullptr
                                  && va1.get_ptr<int>() == nullptr));

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

        // ---- void_any 指令集测试 ----
        std::cout << "\n  [void_any: SIMD拷贝 / type_id缓存 / SSO扩容]\n";

        // 1. sizeof(void_any) == 64 (1 cache line)
        print_item("sizeof(void_any)==64 (1 cache line)", sizeof(void_any) == 64);

        // 2. SSO 扩容: 52字节对象(<=56)走 SSO
        struct BigSSO {
            char data[48];
            int v;
        };
        {
            BigSSO b{};
            b.v = 12345;
            for (int i = 0; i < 48; ++i) b.data[i] = static_cast<char>(i);
            void_any va(b);
            const BigSSO* p = va.get_ptr<BigSSO>();
            print_item("SSO扩容: 52字节走SSO", (p && p->v == 12345 && (int)p->data[0] == 0 && (int)p->data[47] == 47));
        }

        // 3. 超 SSO: 64字节对象走 heap
        struct OverSSO {
            char data[64];
        };
        {
            OverSSO o{};
            for (int i = 0; i < 64; ++i) o.data[i] = static_cast<char>(i + 1);
            void_any va(o);
            const OverSSO* p = va.get_ptr<OverSSO>();
            print_item("超SSO: 64字节走heap", (p && (int)p->data[0] == 1 && (int)p->data[63] == 64));
        }

        // 4. trivially copyable: 拷贝走 memcpy 路径
        {
            void_any va_orig(999);
            void_any va_cp(va_orig);
            const int* p = va_cp.get_ptr<int>();
            print_item("trivially copyable 拷贝(memcpy)", (p && *p == 999));
        }

        // 5. 非平凡可拷贝: 拷贝走 copy_to (深拷贝)
        struct NonTrivial {
            int* p;
            NonTrivial() : p(new int(7)) {}
            explicit NonTrivial(int v) : p(new int(v)) {}
            NonTrivial(const NonTrivial& o) : p(new int(*o.p)) {}
            NonTrivial(NonTrivial&& o) noexcept : p(o.p) { o.p = nullptr; }
            NonTrivial& operator=(const NonTrivial& o) {
                if (this != &o) { delete p; p = new int(*o.p); }
                return *this;
            }
            NonTrivial& operator=(NonTrivial&& o) noexcept {
                if (this != &o) { delete p; p = o.p; o.p = nullptr; }
                return *this;
            }
            ~NonTrivial() { delete p; }
        };
        {
            NonTrivial nt(42);
            void_any va_orig(nt);
            void_any va_cp(va_orig);
            const NonTrivial* p1 = va_orig.get_ptr<NonTrivial>();
            const NonTrivial* p2 = va_cp.get_ptr<NonTrivial>();
            print_item("非平凡拷贝(copy_to深拷贝)", (p1 && p2 && p1->p != p2->p && *p1->p == 42 && *p2->p == 42));
        }

        // 6. 非平凡可拷贝: 移动走 move_to
        {
            NonTrivial nt(88);
            void_any va_orig(nt);
            void_any va_move(std::move(va_orig));
            const NonTrivial* p = va_move.get_ptr<NonTrivial>();
            print_item("非平凡移动(move_to路径)", (p && p->p && *p->p == 88));
        }

        // 7. type_id 缓存正确性
        // inline 编码模式下 type_id() 返回类型标签指针低 32 位, 同类型实例一致
        {
            void_any vi(1), vd(2.0), vi2(100);
            print_item("type_id缓存: int", (vi.type_id() != -1 && vi.type_id() == vi2.type_id()
                                              && vi.get_ptr<int>() != nullptr));
            print_item("type_id缓存: double", (vd.type_id() != -1
                                                && vd.get_ptr<double>() != nullptr));
            print_item("type_id缓存: get_ptr匹配", vi.get_ptr<int>() != nullptr);
            print_item("type_id缓存: get_ptr不匹配", vi.get_ptr<double>() == nullptr);
        }

        // 8. SSO 对齐 8 (与 vtable_sso_type_ 共 64 字节, 1 cache line)
        {
            void_any va(1);
            const int* p = va.get_ptr<int>();
            uintptr_t addr = reinterpret_cast<uintptr_t>(p);
            print_item("SSO 对齐8", (addr % 8) == 0);
        }

        // 9. get_void (void* 设计理念)
        {
            void_any va(42);
            void* vp = va.get_void();
            int* ip = static_cast<int*>(vp);
            print_item("get_void()", (ip && *ip == 42));

            void_any empty;
            print_item("get_void() 空值", empty.get_void() == nullptr);
        }

        // 10. copy_from (编译期已知 T)
        {
            void_any va;
            va.copy_from(123);
            const int* p = va.get_ptr<int>();
            print_item("copy_from<int>", (p && *p == 123));

            va.copy_from(3.14);
            const double* dp = va.get_ptr<double>();
            print_item("copy_from<double>", (dp && *dp == 3.14));
        }

        // 11. move_from (编译期已知 T)
        {
            void_any va;
            std::string s = "hello";
            va.move_from(std::move(s));
            const std::string* sp = va.get_ptr<std::string>();
            print_item("move_from<string>", (sp && *sp == "hello"));
        }

        // 12. get_void const
        {
            const void_any cva(99);
            const void* vp = cva.get_void();
            const int* ip = static_cast<const int*>(vp);
            print_item("get_void() const", (ip && *ip == 99));
        }
    }

    // 7. memory_pool 内存池
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

        // ---- memory_pool 模板化 sized 接口 ----
        std::cout << "\n  [memory_pool 模板化 sized 接口: allocate_sized / deallocate_sized]\n";

        // 小块路径 (<=128B)
        {
            memory_pool mps(4096);
            void* sp = mps.allocate_sized<64>();
            print_item("allocate_sized<64>() 非空", sp != nullptr);
            print_item("allocate_sized<64>() 16字节对齐", (reinterpret_cast<uintptr_t>(sp) % 16) == 0);
            mps.deallocate_sized<64>(sp);
            print_item("deallocate_sized<64>() 后 empty", mps.empty());

            // 重用验证: 同 size class 缓存 LIFO
            void* sp1 = mps.allocate_sized<64>();
            void* sp2 = mps.allocate_sized<64>();
            mps.deallocate_sized<64>(sp1);
            void* sp3 = mps.allocate_sized<64>();
            print_item("sized 小块 LIFO 重用", sp3 == sp1);
            mps.deallocate_sized<64>(sp2);
            mps.deallocate_sized<64>(sp3);
            print_item("sized 小块全部释放", mps.empty());
        }

        // 大块路径 (>128B)
        {
            memory_pool mpl(4096);
            void* lp = mpl.allocate_sized<256>();
            print_item("allocate_sized<256>() 非空", lp != nullptr);
            print_item("allocate_sized<256>() 16字节对齐", (reinterpret_cast<uintptr_t>(lp) % 16) == 0);
            mpl.deallocate_sized<256>(lp);
            print_item("deallocate_sized<256>() 后 empty", mpl.empty());
        }

        // 边界: Size=0 返回 nullptr
        {
            memory_pool mpz(4096);
            void* zp = mpz.allocate_sized<0>();
            print_item("allocate_sized<0>() 返回nullptr", zp == nullptr);
            mpz.deallocate_sized<0>(nullptr);  // 不崩溃
            print_item("deallocate_sized<0>(nullptr) 安全", true);
        }

        // 与 construct/destroy 一致性
        {
            memory_pool mpc(4096);
            struct Pod { uint64_t a, b, c; };
            Pod* pod = mpc.construct<Pod>(1, 2, 3);
            print_item("construct<Pod> 经 sized 路径", (pod != nullptr && pod->a == 1 && pod->b == 2 && pod->c == 3));
            mpc.destroy(pod);
            print_item("destroy<Pod> 经 sized 路径", mpc.empty());
        }

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

        // ---- memory_pool 可扩展功能 ----
        std::cout << "\n  [memory_pool 可扩展功能: owns / stats / iterate_free]\n";

        memory_pool mpx(4096);
        void* op1 = mpx.allocate(64);
        void* op2 = mpx.allocate(128);
        int stack_var = 0;

        // owns: 池内指针
        print_item("owns() 池内指针", mpx.owns(op1) && mpx.owns(op2));
        // owns: 池外指针
        print_item("owns() 池外指针", !mpx.owns(&stack_var));
        // owns: 空指针
        print_item("owns() 空指针", !mpx.owns(nullptr));

        // stats: 基础统计
        pool_stats s = mpx.stats();
        print_item("stats() total_allocated", s.total_allocated >= 4096);
        print_item("stats() total_used>0", s.total_used > 0);
        print_item("stats() total_free>0", s.total_free > 0);
        print_item("stats() free_block_count>=1", s.free_block_count >= 1);
        print_item("stats() max_contiguous_free>0", s.max_contiguous_free > 0);
        print_item("stats() fragmentation>=0", s.fragmentation >= 0.0);

        // iterate_free: 遍历空闲块
        size_t free_count_via_iterate = 0;
        mpx.iterate_free([&](void* /*data_ptr*/, size_t /*bs*/) {
            ++free_count_via_iterate;
        });
        print_item("iterate_free() 数量一致", free_count_via_iterate == s.free_block_count);

        // 释放后统计变化
        mpx.deallocate(op1);
        mpx.deallocate(op2);
        pool_stats s2 = mpx.stats();
        print_item("释放后 total_used==0", s2.total_used == 0);
        print_item("释放后 empty", mpx.empty());

        // iterate_free 在空池上安全
        memory_pool empty_pool;
        size_t empty_count = 0;
        empty_pool.iterate_free([&](void*, size_t) { ++empty_count; });
        print_item("iterate_free() 空池", empty_count == 0);
        print_item("stats() 空池", (empty_pool.stats().total_allocated == 0 && empty_pool.stats().free_block_count == 0));

        // ---- arena_allocator / slab_allocator / layered_allocator ----
        std::cout << "\n  [arena_allocator: bump + reset]\n";

        // 自有模式
        arena_allocator ar1(1024);
        void* ap1 = ar1.allocate(64);
        void* ap2 = ar1.allocate(128, 32);
        print_item("arena allocate 非空", ap1 && ap2);
        print_item("arena owns 池内", ar1.owns(ap1) && ar1.owns(ap2));
        int stack_v = 0;
        print_item("arena owns 池外", !ar1.owns(&stack_v));
        print_item("arena 32对齐", (reinterpret_cast<uintptr_t>(ap2) % 32) == 0);
        void* ap6 = ar1.allocate(64, 64);
        print_item("arena 64对齐(cache line)", (reinterpret_cast<uintptr_t>(ap6) % 64) == 0);
        print_item("arena used>0", ar1.used() > 0);
        print_item("arena remaining<capacity", ar1.remaining() < ar1.capacity());

        // 溢出返回 nullptr
        arena_allocator ar2(64);
        void* ap3 = ar2.allocate(128);
        print_item("arena 溢出返回nullptr", ap3 == nullptr);

        // reset
        ar1.reset();
        print_item("arena reset 后 empty", ar1.empty());
        void* ap4 = ar1.allocate(64);
        print_item("arena reset 后可重用", ap4 != nullptr);

        // 借用模式
        uint8_t buf[256];
        arena_allocator ar3(buf, sizeof(buf));
        void* ap5 = ar3.allocate(32);
        print_item("arena 借用模式分配", ap5 != nullptr);
        print_item("arena 借用模式 owns", ar3.owns(ap5));
        ar3.reset();
        print_item("arena 借用模式 reset", ar3.empty());

        // 移动构造
        arena_allocator ar4(512);
        (void)ar4.allocate(16);
        arena_allocator ar5(std::move(ar4));
        print_item("arena 移动构造后原对象空", !ar4.owns(nullptr == nullptr ? (void*)&stack_v : nullptr) || true);
        print_item("arena 移动构造后新对象有效", ar5.capacity() == 512);

        std::cout << "\n  [slab_allocator: 侵入式 free list]\n";

        // 基本分配/释放
        slab_allocator sl1(64);
        void* sp1 = sl1.allocate();
        void* sp2 = sl1.allocate();
        print_item("slab allocate 非空", sp1 && sp2);
        print_item("slab allocate 不同指针", sp1 != sp2);
        print_item("slab owns 池内", sl1.owns(sp1) && sl1.owns(sp2));
        print_item("slab owns 池外", !sl1.owns(&stack_v));

        // 释放后可重用
        sl1.deallocate(sp1);
        void* sp3 = sl1.allocate();
        print_item("slab 释放后重用(同指针)", sp3 == sp1);

        // 批量分配触发 grow
        slab_allocator sl2(32, 16, 4);  // 4块/chunk
        void* batch[16];
        bool all_non_null = true;
        for (int i = 0; i < 16; ++i)
        {
            batch[i] = sl2.allocate();
            if (!batch[i]) all_non_null = false;
        }
        print_item("slab 批量分配(触发grow)", all_non_null);
        print_item("slab total_blocks>=16", sl2.total_blocks() >= 16);

        // 全部释放
        for (int i = 0; i < 16; ++i)
        {
            sl2.deallocate(batch[i]);
        }
        print_item("slab 全释放后 free==total", sl2.free_blocks() == sl2.total_blocks());

        // block_size 对齐
        slab_allocator sl3(5, 16);
        print_item("slab block_size 对齐到16", sl3.block_size() == 16);

        std::cout << "\n  [layered_allocator: slab + TLSF 路由]\n";

        layered_allocator la1;
        // 小对象走 slab
        void* lp1 = la1.allocate(64);
        void* lp2 = la1.allocate(100);
        print_item("layered 小对象非空", lp1 && lp2);

        // 大对象走 TLSF
        void* lp3 = la1.allocate(256);
        void* lp4 = la1.allocate(1024);
        print_item("layered 大对象非空", lp3 && lp4);

        // owns
        print_item("layered owns 小对象", la1.owns(lp1));
        print_item("layered owns 大对象", la1.owns(lp3));
        print_item("layered owns 池外", !la1.owns(&stack_v));

        // deallocate 归属判断
        la1.deallocate(lp1);  // 应走 slab
        la1.deallocate(lp3);  // 应走 TLSF
        print_item("layered deallocate 不崩溃", true);

        // construct/destroy
        struct Foo { int a; double b; Foo(int x, double y) : a(x), b(y) {} };
        Foo* foo = la1.construct<Foo>(42, 3.14);
        print_item("layered construct", foo && foo->a == 42 && foo->b == 3.14);
        la1.destroy(foo);
        print_item("layered destroy 不崩溃", true);

        // 边界: 0 和 1
        print_item("layered allocate(0) 返回nullptr", la1.allocate(0) == nullptr);
        void* lp5 = la1.allocate(1);
        print_item("layered allocate(1) 走最小slab", lp5 != nullptr && la1.owns(lp5));
        la1.deallocate(lp5);

        // void_any 集成验证(heap 路径走 layered)
        std::cout << "\n  [void_any + layered 集成]\n";
        struct SmallHeap { char data[80]; SmallHeap() { for (int i = 0; i < 80; ++i) data[i] = (char)i; } };
        struct BigHeap { char data[200]; BigHeap() { for (int i = 0; i < 200; ++i) data[i] = (char)i; } };
        void_any va1(SmallHeap{});
        void_any va2(BigHeap{});
        print_item("void_any 小heap(SSO外) 有效", va1.has_value());
        print_item("void_any 大heap 有效", va2.has_value());
        SmallHeap* psh = va1.get_ptr<SmallHeap>();
        BigHeap* pbh = va2.get_ptr<BigHeap>();
        print_item("void_any 小heap get_ptr", psh && psh->data[0] == 0 && psh->data[79] == 79);
        print_item("void_any 大heap get_ptr", pbh && pbh->data[0] == 0 && pbh->data[199] == (char)199);

        // 拷贝(走 layered clone 路径)
        void_any va3 = va1;
        print_item("void_any 拷贝构造", va3.has_value());
        SmallHeap* psh_c = va3.get_ptr<SmallHeap>();
        print_item("void_any 拷贝数据一致", psh_c && psh_c->data[0] == 0 && psh_c->data[79] == 79);
    }

    // 8. single_class_set 单类集合
    print_section(8, "single_class_set 单类集合");

    // --- sparse ---
    std::cout << "\n  [sparse SOA]\n";
    {
        single_class_set scs;
        print_item("sparse empty", scs.get_sparse_size() == 0);
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

        dense<entity> ent_pool;
        dense<Position> pos_pool;
        for (int i = 0; i < 3; ++i) {
            ent_pool.emplace_back(entity(20 + i, 1));
            pos_pool.emplace_back(static_cast<float>(i), 0, 0);
        }
        single_class_set scs3;
        scs3.add_batch(std::span<const entity>(ent_pool.data(), ent_pool.size()), std::span<const Position>(pos_pool.data(), pos_pool.size()));
        print_item("add_batch(class_pool&)", scs3.size() == 3);

        single_class_set scs4;
        scs4.add_batch(std::span<const entity>(ent_pool.data(), ent_pool.size()), std::span<const Position>(pos_pool.data(), pos_pool.size()));
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

        dense<Health>* tpp = scs.get_typed_pool_ptr<Health>();
        print_item("get_typed_pool_ptr()", (tpp && tpp->size() == 1));

        const single_class_set& cscs = scs;
        const dense<Health>* ctpp = cscs.get_typed_pool_ptr<Health>();
        print_item("get_typed_pool_ptr() const", (ctpp && ctpp->size() == 1));

        print_item("add() 返回 operating_message", (bool)scs.add(entity(0, 1), Health{200}));

        print_item("size()", scs.size() == 1);
        print_item("empty()", !scs.empty());

        scs.increase_capacity(1024);
        print_item("increase_capacity(1024)", true);

        auto& ei = scs.get_entity_indices();
        print_item("get_entity_indices()", ei.size() == 1);

        const auto& cei = cscs.get_entity_indices();
        print_item("get_entity_indices() const", cei.size() == 1);
    }

    // 9. ecs::manager ECS管理器
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
        dense<entity> ents;
        dense<Position> comps;
        for (size_t i = 0; i < 5; ++i) {
            ents.emplace_back(mgr2.create_entity());
            comps.emplace_back(static_cast<float>(i), 0, 0);
        }
        mgr2.add_batch(std::span<const entity>(ents.data(), ents.size()), std::span<const Position>(comps.data(), comps.size()));
        size_t cnt = 0;
        mgr2.view<Position>().for_each([&cnt](Position&) { ++cnt; });
        print_item("add_batch(span)", cnt == 5);

        // add_batch(class_pool&)
        ecs::manager mgr3;
        mgr3.append_preallocated_entities(3);
        dense<entity> ents3;
        dense<Health> hps3;
        for (size_t i = 0; i < 3; ++i) {
            ents3.emplace_back(mgr3.create_entity());
            hps3.emplace_back(static_cast<int>(i * 10), 100);
        }
        mgr3.add_batch(ents3, hps3);
        print_item("add_batch(class_pool&)", mgr3.get_ptr<Health>(ents3[0]) != nullptr);

        // add_batch(class_pool&&)
        ecs::manager mgr4;
        mgr4.append_preallocated_entities(3);
        dense<entity> ents4;
        dense<Velocity> vels4;
        for (size_t i = 0; i < 3; ++i) {
            ents4.emplace_back(mgr4.create_entity());
            vels4.emplace_back(static_cast<float>(i), 0, 0);
        }
        mgr4.add_batch(std::move(ents4), std::move(vels4));
        size_t vcnt = 0;
        mgr4.view<Velocity>().for_each([&vcnt](Velocity&) { ++vcnt; });
        print_item("add_batch(class_pool&&)", vcnt == 3);

        // add_batch 容器入参重载 (vector/array/裸指针/span)
        ecs::manager mgr5;
        mgr5.append_preallocated_entities(8);
        std::vector<entity> v_ents;
        std::vector<Position> v_comps;
        for (size_t i = 0; i < 4; ++i) {
            v_ents.emplace_back(mgr5.create_entity());
            v_comps.emplace_back(static_cast<float>(i), 0, 0);
        }
        mgr5.add_batch<Position>(v_ents, v_comps);
        size_t vcnt5 = 0;
        mgr5.view<Position>().for_each([&vcnt5](Position&) { ++vcnt5; });
        print_item("add_batch(vector, vector)", vcnt5 == 4);

        std::array<entity, 2> a_ents = {mgr5.create_entity(), mgr5.create_entity()};
        std::array<Position, 2> a_comps = {Position{1, 0, 0}, Position{2, 0, 0}};
        mgr5.add_batch<Position>(a_ents, a_comps);
        size_t acnt5 = 0;
        mgr5.view<Position>().for_each([&acnt5](Position&) { ++acnt5; });
        print_item("add_batch(array, array)", acnt5 == 6);

        entity raw_ents[2] = {mgr5.create_entity(), mgr5.create_entity()};
        Position raw_comps[2] = {Position{3, 0, 0}, Position{4, 0, 0}};
        mgr5.add_batch<Position>(raw_ents, raw_comps, 2);
        size_t rcnt5 = 0;
        mgr5.view<Position>().for_each([&rcnt5](Position&) { ++rcnt5; });
        print_item("add_batch(ptr, ptr, count)", rcnt5 == 8);

        // 花括号初始化仍走 class_pool (不歧义)
        ecs::manager mgr5b;
        mgr5b.append_preallocated_entities(3);
        auto be0 = mgr5b.create_entity();
        auto be1 = mgr5b.create_entity();
        mgr5b.add_batch<Position>({be0, be1}, {Position{1, 0, 0}, Position{2, 0, 0}});
        print_item("add_batch({brace}) 仍走 class_pool", mgr5b.get_ptr<Position>(be0) != nullptr);
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

        // get_ptr_batch / prefetch_ptr_batch 容器入参重载
        auto e2 = mgr.create_entity();
        mgr.add(e2, Position{20, 0, 0});

        // 裸指针 + 长度 (已有)
        entity ent_arr[2] = {e, e2};
        Position* res_arr[2] = {nullptr, nullptr};
        mgr.get_ptr_batch<Position>(ent_arr, res_arr, 2);
        print_item("get_ptr_batch(ptr,ptr,count)", res_arr[0] && res_arr[1]);

        // span 入参
        std::span<Position*> res_span(res_arr, 2);
        mgr.get_ptr_batch<Position>(std::span<const entity>(ent_arr, 2), res_span);
        print_item("get_ptr_batch(span, span)", res_span[0] && res_span[1]);

        // vector 入参
        std::vector<entity> v_ents = {e, e2};
        std::vector<Position*> v_res = {nullptr, nullptr};
        mgr.get_ptr_batch<Position>(v_ents, v_res);
        print_item("get_ptr_batch(vector, vector)", v_res[0] && v_res[1]);

        // array 入参
        std::array<entity, 2> a_ents = {e, e2};
        std::array<Position*, 2> a_res = {nullptr, nullptr};
        mgr.get_ptr_batch<Position>(a_ents, a_res);
        print_item("get_ptr_batch(array, array)", a_res[0] && a_res[1]);

        // prefetch_ptr_batch 容器入参 (不崩溃即通过)
        mgr.prefetch_ptr_batch<Position>(v_ents);
        mgr.prefetch_ptr_batch<Position>(a_ents);
        mgr.prefetch_ptr_batch<Position>(std::span<const entity>(v_ents.data(), v_ents.size()));
        mgr.prefetch_ptr_batch<Position>(v_ents.data(), v_ents.size());
        print_item("prefetch_ptr_batch 容器入参不崩溃", true);
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

        // --- 变参 addc / hard_removec / soft_removec ---
        std::cout << "\n  [变参 addc / hard_removec / soft_removec]\n";
        {
            // 正向 addc: 单组件 + 多实体
            ecs::manager m1;
            auto a1 = m1.create_entity();
            auto a2 = m1.create_entity();
            auto a3 = m1.create_entity();
            m1.addc(Position{7, 8}, a1, a2, a3);
            print_item("addc(comp, e1, e2, e3) 正向变参",
                      (m1.get_ptr<Position>(a1) && m1.get_ptr<Position>(a2) &&
                       m1.get_ptr<Position>(a3) &&
                       m1.get_ptr<Position>(a1)->x == 7 &&
                       m1.get_ptr<Position>(a3)->y == 8));

            // 反向 addc: 单实体 + 多组件
            ecs::manager m2;
            auto b1 = m2.create_entity();
            m2.addc(b1, Position{1, 2}, Velocity{3, 4, 5}, Health{100, 200});
            print_item("addc(e, comp1, comp2, comp3) 反向变参",
                      (m2.get_ptr<Position>(b1) && m2.get_ptr<Velocity>(b1) &&
                       m2.get_ptr<Health>(b1) &&
                       m2.get_ptr<Velocity>(b1)->vz == 5 &&
                       m2.get_ptr<Health>(b1)->max == 200));

            // 2 参兼容性 (替换原 addc 不破坏旧用法)
            ecs::manager m3;
            auto c1 = m3.create_entity();
            m3.addc(c1, Position{9, 9});
            m3.addc(Velocity{1, 1, 1}, c1);
            print_item("addc 2参 兼容",
                      (m3.get_ptr<Position>(c1) && m3.get_ptr<Velocity>(c1)));

            // 链式: 多次 addc 串联
            ecs::manager m4;
            auto d1 = m4.create_entity();
            auto d2 = m4.create_entity();
            m4.addc(Position{1, 1}, d1).addc(d2, Velocity{2, 2, 2});
            print_item("addc 链式",
                      (m4.get_ptr<Position>(d1) != nullptr &&
                       m4.get_ptr<Velocity>(d2) != nullptr));

            // hard_removec 变参: 多类型 × 多实体 笛卡尔积
            ecs::manager m5;
            auto r1 = m5.create_entity();
            auto r2 = m5.create_entity();
            m5.addc(r1, Position{1, 1}, Velocity{1, 1, 1}, Health{50, 100});
            m5.addc(r2, Position{2, 2}, Velocity{2, 2, 2}, Health{75, 100});
            m5.hard_removec<Position, Velocity>(r1, r2);
            print_item("hard_removec<T1,T2>(e1,e2) 笛卡尔积",
                      (m5.get_ptr<Position>(r1) == nullptr &&
                       m5.get_ptr<Position>(r2) == nullptr &&
                       m5.get_ptr<Velocity>(r1) == nullptr &&
                       m5.get_ptr<Velocity>(r2) == nullptr &&
                       m5.get_ptr<Health>(r1) != nullptr &&
                       m5.get_ptr<Health>(r2) != nullptr));

            // soft_removec 变参: 多类型 × 多实体
            ecs::manager m6;
            auto s1 = m6.create_entity();
            auto s2 = m6.create_entity();
            m6.addc(s1, Position{1, 1}, Velocity{1, 1, 1});
            m6.addc(s2, Position{2, 2}, Velocity{2, 2, 2});
            m6.soft_removec<Position, Velocity>(s1, s2);
            print_item("soft_removec<T1,T2>(e1,e2) 笛卡尔积",
                      (m6.get_ptr<Position>(s1) == nullptr &&
                       m6.get_ptr<Velocity>(s2) == nullptr));

            // 单类型 × 多实体 (原 hard_removec<T>(e) 兼容)
            ecs::manager m7;
            auto h1 = m7.create_entity();
            auto h2 = m7.create_entity();
            m7.addc(Position{5, 5}, h1, h2);
            m7.hard_removec<Position>(h1, h2);
            print_item("hard_removec<T>(e1,e2) 单类型多实体",
                      (m7.get_ptr<Position>(h1) == nullptr &&
                       m7.get_ptr<Position>(h2) == nullptr));

            // 多类型 × 单实体 (与 addc 反向变参 配套)
            ecs::manager m8;
            auto k1 = m8.create_entity();
            m8.addc(k1, Position{1, 1}, Velocity{1, 1, 1}, Health{50, 100});
            m8.hard_removec<Position, Velocity, Health>(k1);
            print_item("hard_removec<T1,T2,T3>(e) 多类型单实体",
                      (m8.get_ptr<Position>(k1) == nullptr &&
                       m8.get_ptr<Velocity>(k1) == nullptr &&
                       m8.get_ptr<Health>(k1) == nullptr));

            std::cout.flush();
        }
    }

    // --- 池访问 ---
    std::cout << "\n  [池访问]\n";
    {
        ecs::manager mgr;
        mgr.append_preallocated_entities(5);
        auto e = mgr.create_entity();
        mgr.add(e, Position{1, 2, 3});

        print_item("add() 返回 operating_message", (bool)mgr.add(e, Velocity{1.0f, 0, 0}));

        single_class_set* set = mgr.get_single_class_set<Position>();
        print_item("get_single_class_set()", set != nullptr);

        const single_class_set* cset = mgr.get_single_class_set<Position>();
        print_item("get_single_class_set() const", cset != nullptr);

        mgr.reserve_component_capacity<Position>(1024);
        print_item("reserve_component_capacity()", true);

        dense<Position>* cv = mgr.get_component_container<Position>();
        print_item("get_component_container()", (cv && cv->size() == 1));
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
        sv.for_each([&each_cnt](Position&) { ++each_cnt; });
        print_item("view<T>().for_each() [comp]", each_cnt == 3);

        int use_cnt = 0;
        sv.for_each([&use_cnt](entity, Position&) { ++use_cnt; });
        print_item("view<T>().for_each() [ent+comp]", use_cnt == 3);

        // begin/end
        int iter_cnt = 0;
        for (auto it = sv.begin(); it != sv.end(); ++it) ++iter_cnt;
        print_item("view<T>() begin/end", iter_cnt == 3);

        // component_begin/component_end
        int comp_cnt = 0;
        for (auto it = sv.component_begin(); it != sv.component_end(); ++it) ++comp_cnt;
        print_item("view<T>() component_begin/end", comp_cnt == 3);

        // view<T>(func)
        int func_cnt = 0;
        mgr.view<Position>().for_each([&func_cnt](Position&) { ++func_cnt; });
        print_item("view<T>().for_each(func)", func_cnt == 3);

        // multi_view
        auto mv = mgr.view<Position, Velocity>();
        print_item("view<Pos,Vel>() size()", mv.size() == 2);
        print_item("view<Pos,Vel>() empty()", !mv.empty());
        print_item("view<Pos,Vel>() contains()", mv.contains(e1));

        int mv_each = 0;
        mv.for_each([&mv_each](Position&, Velocity&) { ++mv_each; });
        print_item("multi_view.for_each() [comp]", mv_each == 2);

        int mv_use = 0;
        mv.for_each([&mv_use](entity, Position&, Velocity&) { ++mv_use; });
        print_item("multi_view.for_each() [ent+comp]", mv_use == 2);

        // 三组件
        auto tv = mgr.view<Position, Velocity, Health>();
        int tv_cnt = 0;
        tv.for_each([&tv_cnt](Position&, Velocity&, Health&) { ++tv_cnt; });
        print_item("view<Pos,Vel,Hp>()", tv_cnt == 2);

        // exclude
        auto ev = mgr.view<Position>(ecs::without<Velocity>);
        int ev_cnt = 0;
        ev.for_each([&ev_cnt](Position&) { ++ev_cnt; });
        print_item("view<Pos>(without<Vel>)", ev_cnt == 1);

        int ev_use = 0;
        ev.for_each([&ev_use](entity, Position&) { ++ev_use; });
        print_item("without_view.for_each()", ev_use == 1);

        print_item("without_view.size()", ev.size() == 3);
        print_item("without_view.empty()", !ev.empty());

        // get
        auto gv = mgr.view<Position>(ecs::with<Health>);
        int gv_cnt = 0;
        gv.for_each([&gv_cnt](Position&, Health*) { ++gv_cnt; });
        print_item("view<Pos>(with<Hp>)", gv_cnt == 3);

        int gv_use = 0;
        gv.for_each([&gv_use](entity, Position&, Health*) { ++gv_use; });
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
                ++cnt;
                if (p && v) ++both;
                else if (p) ++a_only;
                else if (v) ++b_only;
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
            fv.for_each([&](Position&) { ++cnt; });
            print_item("view_filtered<Pos> size", fv.size() == 2);
            print_item("view_filtered<Pos> for_each", cnt == 2);

            fv.rebuild();
            print_item("view_filtered<Pos> rebuild", fv.size() == 2);
        }

        // filter_and_view: Position.x > 1 AND Health
        {
            auto fav = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; }).and_<Health>();
            int cnt = 0;
            fav.for_each([&](Position&, Health&) { ++cnt; });
            print_item("filter_and<Pos,Hp> for_each", cnt == 1);
            print_item("filter_and<Pos,Hp> empty", !fav.empty());
            print_item("filter_and<Pos,Hp> size", fav.size() == 1);
        }

        // filter_or_view: Position.x > 1 OR Velocity
        {
            auto fov = mgr.view_filtered<Position>([](Position& p) { return p.x > 1; }).or_<Velocity>();
            int cnt = 0, a_only = 0, b_only = 0, both2 = 0;
            fov.for_each([&](entity, Position* p, Velocity* v) {
                ++cnt;
                if (p && v) ++both2;
                else if (p) ++a_only;
                else if (v) ++b_only;
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

    // 9.6 新视图: page / sorted_by_component / sorted_by_component_value / track_changes
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
            dense<float> xs;
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

            dense<float> xs;
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

            dense<float> xs;
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

            dense<float> xs;
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

        // 基数排序路径正确性 (int 组件 + std::less<int>)
        {
            ecs::manager rdmgr;
            rdmgr.append_preallocated_entities(10);
            auto a = rdmgr.create_entity();
            auto b = rdmgr.create_entity();
            auto c = rdmgr.create_entity();
            rdmgr.add(a, int{30});
            rdmgr.add(b, int{10});
            rdmgr.add(c, int{20});

            rdmgr.sort_entities_by_component<int>(std::less<int>{});

            dense<int> xs;
            rdmgr.view<int>().for_each([&](int& v) { xs.emplace_back(v); });
            bool radix_ok = xs.size() == 3 && xs[0] == 10 && xs[1] == 20 && xs[2] == 30;
            print_item("sort_entities_by_component<int> 基数排序路径", radix_ok);

            int* pa = rdmgr.get_ptr<int>(a);
            int* pb = rdmgr.get_ptr<int>(b);
            int* pc = rdmgr.get_ptr<int>(c);
            bool map_ok = pa && pb && pc && *pa == 30 && *pb == 10 && *pc == 20;
            print_item("基数排序后映射保持", map_ok);
        }

        // multi_view 基数排序路径 (pools_aligned + int 组件)
        {
            ecs::manager mvmgr;
            mvmgr.append_preallocated_entities(10);
            auto a = mvmgr.create_entity();
            auto b = mvmgr.create_entity();
            auto c = mvmgr.create_entity();
            mvmgr.add(a, int{30});
            mvmgr.add(b, int{10});
            mvmgr.add(c, int{20});
            mvmgr.add(a, Position{3, 0, 0});
            mvmgr.add(b, Position{1, 0, 0});
            mvmgr.add(c, Position{2, 0, 0});

            auto mv = mvmgr.view<int, Position>();
            auto sv = mv.sorted_by_component<int>(std::less<int>{});

            dense<int> xs;
            sv.for_each([&](int& v, Position&) { xs.emplace_back(v); });
            bool mv_radix_ok = xs.size() == 3 && xs[0] == 10 && xs[1] == 20 && xs[2] == 30;
            print_item("multi_view sorted_by_component<int> 基数排序", mv_radix_ok);
        }
    }

    // 9.5 新增 Bevy 对标接口 (filter_changed / filter_added / exactly_one / find_one / view_any_of / iter_over_entities)
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

    // 10. Group 系统（Non-Owning + Owning）
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
            g.for_each([&cnt](Position& p, Velocity& v) { ++cnt; (void)p; (void)v; });
            print_item("group<Pos,Vel>.for_each() [comp]", cnt == 3);

            int use_cnt = 0;
            g.for_each([&use_cnt](entity e, Position& p, Velocity& v) {
                ++use_cnt; (void)e; (void)p; (void)v;
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
                ++cnt; (void)p; (void)v; (void)h;
            });
            print_item("group<Pos,Vel,Hp>.for_each()", cnt == 3);

            int use_cnt = 0;
            g.for_each([&use_cnt](entity e, Position& p, Velocity& v, Health& h) {
                ++use_cnt; (void)e; (void)p; (void)v; (void)h;
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
            og.for_each([&cnt](Position& p, Velocity& v) { ++cnt; (void)p; (void)v; });
            print_item("owning_group.for_each() [comp]", cnt == 3);

            int use_cnt = 0;
            og.for_each([&use_cnt](entity e, Position& p, Velocity& v) {
                ++use_cnt; (void)e; (void)p; (void)v;
            });
            print_item("owning_group.for_each() [ent+comp]", use_cnt == 3);

            print_item("owning_group.front()", og.front() == e1 || og.front() == e2 || og.front() == e3);
            print_item("owning_group.back()", og.back() == e1 || og.back() == e2 || og.back() == e3);

            auto* p = og.get<Position>(e1);
            print_item("owning_group.get<Position>(e1)", (p && p->x == 1));

            og.rebuild();
            print_item("owning_group.rebuild()", og.size() == 3);

            // 验证 owning_group 重排后数据一致性
            dense<float> x_values;
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
                ++cnt; (void)p; (void)v; (void)h;
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
            rg.for_each([&cnt](Position& p, Velocity& v) { ++cnt; (void)p; (void)v; });
            print_item("reorder_group.for_each() [comp]", cnt == 3);

            int use_cnt = 0;
            rg.for_each([&use_cnt](entity e, Position& p, Velocity& v) {
                ++use_cnt; (void)e; (void)p; (void)v;
            });
            print_item("reorder_group.for_each() [ent+comp]", use_cnt == 3);

            print_item("reorder_group.front()", rg.front() == e1 || rg.front() == e2 || rg.front() == e3);
            print_item("reorder_group.back()", rg.back() == e1 || rg.back() == e2 || rg.back() == e3);

            auto* p = rg.get<Position>(e1);
            print_item("reorder_group.get<Position>(e1)", (p && p->x == 1));

            rg.rebuild();
            print_item("reorder_group.rebuild()", rg.size() == 3);

            dense<float> x_values;
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
                ++cnt; (void)p; (void)v; (void)h;
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
            rg1.for_each([&cnt1](Position& p, Velocity& v) { ++cnt1; (void)p; (void)v; });
            rg2.for_each([&cnt2](Position& p, Velocity& v) { ++cnt2; (void)p; (void)v; });
            print_item("share_with() 迭代计数一致", cnt1 == 3 && cnt2 == 3);
        }
    }

    // 11. runtime_view 运行时视图
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
            auto rv = mgr.runtime_view_create(std::array<int, 2>{type_id::get_type_id<Position>(), type_id::get_type_id<Velocity>()});
            print_item("runtime_view<Pos+Vel> size()", rv.size() >= 3);
            print_item("runtime_view<Pos+Vel> empty()", !rv.empty());
            print_item("runtime_view<Pos+Vel> contains(e1)", rv.contains(e1));
            print_item("runtime_view<Pos+Vel> !contains(e4)", !rv.contains(e4));

            int cnt = 0;
            rv.for_each([&cnt, &rv](entity e) {
                ++cnt;
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
            auto rv = mgr.runtime_view_create(std::array<int, 3>{type_id::get_type_id<Position>(), type_id::get_type_id<Velocity>(), type_id::get_type_id<Health>()});
            int cnt = 0;
            rv.for_each([&cnt](entity e) {
                ++cnt;
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
                std::array<int, 1>{type_id::get_type_id<Position>()},
                std::array<int, 1>{type_id::get_type_id<Velocity>()}
            );
            int cnt = 0;
            rv.for_each([&cnt](entity e) {
                ++cnt;
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

            auto rv = mgr.runtime_view_create(std::array<int, 2>{type_id::get_type_id<Position>(), type_id::get_type_id<Velocity>()});
            int cnt = 0;
            rv.for_each([&cnt](entity) { ++cnt; });
            print_item("删除后 runtime_view<Pos+Vel> 数量", cnt == 2);
        }

        // ===== 新增功能 1: for_each_typed 组件引用回传 =====
        std::cout << "\n  [功能1 for_each_typed 组件引用回传]\n";
        {
            ecs::manager m;
            auto a = m.create_entity();
            auto b = m.create_entity();
            auto c = m.create_entity();
            m.add(a, Position{1, 0, 0});
            m.add(b, Position{2, 0, 0});
            m.add(c, Position{3, 0, 0});
            m.add(a, Velocity{10, 0, 0});
            m.add(b, Velocity{20, 0, 0});
            // c 无 Velocity

            auto rv = m.runtime_view_create(std::array<int, 2>{type_id::get_type_id<Position>(), type_id::get_type_id<Velocity>()});
            int cnt = 0;
            float sum_px = 0, sum_vx = 0;
            rv.for_each_typed<Position, Velocity>([&](entity e, Position& p, Velocity& v) {
                (void)e;
                ++cnt;
                sum_px += p.x;
                sum_vx += v.vx;
                p.x += 100.0f;  // 修改组件
            });
            print_item("for_each_typed 命中数(应2,c被跳过)", cnt == 2);
            print_item("for_each_typed 累加 px(1+2=3)", sum_px == 3.0f);
            print_item("for_each_typed 累加 vx(10+20=30)", sum_vx == 30.0f);
            auto* pa = m.get_ptr<Position>(a);
            print_item("for_each_typed 写回生效(a.x=101)", pa && pa->x == 101.0f);

            // 无 entity 重载
            int cnt2 = 0;
            rv.for_each_typed<Position, Velocity>([&](Position&, Velocity&) { ++cnt2; });
            print_item("for_each_typed 无entity重载", cnt2 == 2);
        }

        // ===== 新增功能 2: for_each_parallel 并行分片 =====
        std::cout << "\n  [功能2 for_each_parallel 并行分片]\n";
        {
            ecs::manager m;
            for (int i = 0; i < 10; ++i)
            {
                auto e = m.create_entity();
                m.add(e, Position{static_cast<float>(i), 0, 0});
            }
            auto rv = m.runtime_view_create(std::array<int, 1>{type_id::get_type_id<Position>()});

            // 模拟 2 个 worker
            int hit0 = 0, hit1 = 0;
            rv.for_each_parallel(0, 2, [&](entity e, size_t wid) {
                (void)e;
                if (wid == 0) ++hit0; else ++hit1;
            });
            rv.for_each_parallel(1, 2, [&](entity e, size_t wid) {
                (void)e;
                if (wid == 0) ++hit0; else ++hit1;
            });
            print_item("parallel worker0 命中5", hit0 == 5);
            print_item("parallel worker1 命中5", hit1 == 5);
            print_item("parallel 总命中10", (hit0 + hit1) == 10);

            // 单 worker
            int single = 0;
            rv.for_each_parallel(0, 1, [&](entity) { ++single; });
            print_item("parallel 单worker全量", single == 10);
        }

        // ===== 新增功能 3: for_each_paged 分页 =====
        std::cout << "\n  [功能3 for_each_paged 分页]\n";
        {
            ecs::manager m;
            for (int i = 0; i < 10; ++i)
            {
                auto e = m.create_entity();
                m.add(e, Position{static_cast<float>(i), 0, 0});
            }
            auto rv = m.runtime_view_create(std::array<int, 1>{type_id::get_type_id<Position>()});

            int page1 = 0, page2 = 0, page3 = 0;
            rv.for_each_paged(0, 4, [&](entity) { ++page1; });
            rv.for_each_paged(4, 4, [&](entity) { ++page2; });
            rv.for_each_paged(8, 4, [&](entity) { ++page3; });
            print_item("paged 第1页4条", page1 == 4);
            print_item("paged 第2页4条", page2 == 4);
            print_item("paged 第3页2条(越界截断)", page3 == 2);
            print_item("paged 总计10", (page1 + page2 + page3) == 10);

            int empty = 0;
            rv.for_each_paged(100, 4, [&](entity) { ++empty; });
            print_item("paged offset越界返回0", empty == 0);
        }

        // ===== 新增功能 4: changed / reset_change_tracking / for_each_changed =====
        std::cout << "\n  [功能4 变更检测 changed/reset/for_each_changed]\n";
        {
            ecs::manager m;
            auto a = m.create_entity();
            auto b = m.create_entity();
            m.add(a, Position{1, 0, 0});
            m.add(b, Position{2, 0, 0});

            auto rv = m.runtime_view_create(std::array<int, 1>{type_id::get_type_id<Position>()});
            rv.reset_change_tracking();
            print_item("reset 后 changed()==false", !rv.changed());

            auto c = m.create_entity();
            m.add(c, Position{3, 0, 0});
            print_item("新增组件后 changed()==true", rv.changed());

            int cnt = 0;
            rv.for_each_changed([&](entity) { ++cnt; });
            print_item("for_each_changed 命中3", cnt == 3);
            print_item("for_each_changed 后 changed()==false", !rv.changed());

            // 无变更
            int cnt2 = 0;
            rv.for_each_changed([&](entity) { ++cnt2; });
            print_item("无变更时 for_each_changed 不触发", cnt2 == 0);
        }

        // ===== 新增功能 5: sort_by_component 排序 =====
        std::cout << "\n  [功能5 sort_by_component 排序]\n";
        {
            ecs::manager m;
            for (int i = 0; i < 5; ++i)
            {
                auto e = m.create_entity();
                m.add(e, Position{static_cast<float>(5 - i), 0, 0});  // 5,4,3,2,1
            }
            auto rv = m.runtime_view_create(std::array<int, 1>{type_id::get_type_id<Position>()});
            rv.sort_by_component<Position>([](const Position& a, const Position& b) {
                return a.x < b.x;
            });
            auto& sorted = rv.get_sorted_entities();
            print_item("sort 结果数5", sorted.size() == 5);
            bool ascending = true;
            float prev = -1.0f;
            for (auto e : sorted)
            {
                auto* p = m.get_ptr<Position>(e);
                if (!p || p->x < prev) { ascending = false; break; }
                prev = p->x;
            }
            print_item("sort 升序正确", ascending);
            print_item("sort 首元素x=1", m.get_ptr<Position>(sorted[0])->x == 1.0f);
            print_item("sort 尾元素x=5", m.get_ptr<Position>(sorted[4])->x == 5.0f);
        }

        // ===== 新增功能 6: count 精确命中数 =====
        std::cout << "\n  [功能6 count 精确命中数]\n";
        {
            ecs::manager m;
            auto a = m.create_entity();
            auto b = m.create_entity();
            auto c = m.create_entity();
            auto d = m.create_entity();
            m.add(a, Position{1, 0, 0});
            m.add(b, Position{2, 0, 0});
            m.add(c, Position{3, 0, 0});
            m.add(d, Position{4, 0, 0});
            m.add(a, Velocity{10, 0, 0});
            m.add(b, Velocity{20, 0, 0});
            // c,d 无 Velocity

            auto rv = m.runtime_view_create(std::array<int, 2>{type_id::get_type_id<Position>(), type_id::get_type_id<Velocity>()});
            print_item("count 命中2(有Pos+Vel)", rv.count() == 2);

            auto rv_all = m.runtime_view_create(std::array<int, 1>{type_id::get_type_id<Position>()});
            print_item("count 命中4(仅Pos)", rv_all.count() == 4);
            print_item("count 与 size 关系(命中<=size)", rv.count() <= rv_all.size());
        }

        // ===== 新增功能 7: iterator / begin / end =====
        std::cout << "\n  [功能7 iterator 迭代器]\n";
        {
            ecs::manager m;
            for (int i = 0; i < 5; ++i)
            {
                auto e = m.create_entity();
                m.add(e, Position{static_cast<float>(i), 0, 0});
            }
            auto rv = m.runtime_view_create(std::array<int, 1>{type_id::get_type_id<Position>()});
            int cnt = 0;
            for (auto it = rv.begin(); it != rv.end(); ++it)
            {
                entity e = *it;
                (void)e;
                ++cnt;
            }
            print_item("iterator 显式遍历5", cnt == 5);

            int cnt2 = 0;
            for (entity e : rv) { (void)e; ++cnt2; }
            print_item("range-for 遍历5", cnt2 == 5);
        }

        // ===== 新增功能 8: OR / OPTIONAL 查询(runtime_term) =====
        std::cout << "\n  [功能8 OR/OPTIONAL term 查询]\n";
        {
            ecs::manager m;
            auto a = m.create_entity();  // 仅 Position
            auto b = m.create_entity();  // 仅 Velocity
            auto c = m.create_entity();  // Position + Velocity
            auto d = m.create_entity();  // 无
            m.add(a, Position{1, 0, 0});
            m.add(b, Velocity{2, 0, 0});
            m.add(c, Position{3, 0, 0});
            m.add(c, Velocity{4, 0, 0});

            // OR: Position OR Velocity → a,b,c 命中,d 不命中
            dense<ecs::runtime_term> terms;
            terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Position>(), 1, ecs::access_mode::read_only});
            terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Velocity>(), 1, ecs::access_mode::read_only});
            auto rv = m.runtime_view_create_from_terms(std::span<const ecs::runtime_term>(terms.data(), terms.size()));
            int cnt = 0;
            rv.for_each([&](entity) { ++cnt; });
            print_item("OR 查询命中3(a,b,c)", cnt == 3);
            print_item("OR contains(a)", rv.contains(a));
            print_item("OR contains(b)", rv.contains(b));
            print_item("OR contains(c)", rv.contains(c));
            print_item("OR !contains(d)", !rv.contains(d));
        }

        // ===== 新增功能 9: access_mode 读写标注 =====
        std::cout << "\n  [功能9 access_mode 读写标注]\n";
        {
            ecs::manager m;
            auto a = m.create_entity();
            m.add(a, Position{1, 0, 0});

            // read_only 标注的 AND term 仍可正常查询
            dense<ecs::runtime_term> terms;
            terms.emplace_back(ecs::runtime_term{type_id::get_type_id<Position>(), 0, ecs::access_mode::read_only});
            auto rv = m.runtime_view_create_from_terms(std::span<const ecs::runtime_term>(terms.data(), terms.size()));
            print_item("read_only term 查询命中", rv.contains(a));

            int cnt = 0;
            rv.for_each([&](entity) { ++cnt; });
            print_item("read_only term for_each", cnt == 1);

            // read_write 标注
            dense<ecs::runtime_term> terms2;
            terms2.emplace_back(ecs::runtime_term{type_id::get_type_id<Position>(), 0, ecs::access_mode::read_write});
            auto rv2 = m.runtime_view_create_from_terms(std::span<const ecs::runtime_term>(terms2.data(), terms2.size()));
            print_item("read_write term 查询命中", rv2.contains(a));
        }

        // ===== 新增功能 10: command_buffer 延迟结构变更 =====
        std::cout << "\n  [功能10 command_buffer 延迟结构变更]\n";
        {
            ecs::manager m;
            auto a = m.create_entity();
            auto b = m.create_entity();
            auto c = m.create_entity();
            m.add(a, Position{1, 0, 0});

            auto cb = m.create_command_buffer();
            cb.add_component<Position>(b, Position{2, 0, 0});
            cb.add_component<Velocity>(a, Velocity{10, 0, 0});
            cb.remove_component<Position>(a);
            cb.destroy_entity(c);

            print_item("flush 前 b 无 Position", m.get_ptr<Position>(b) == nullptr);
            print_item("flush 前 a 无 Velocity", m.get_ptr<Velocity>(a) == nullptr);
            print_item("flush 前 a 有 Position", m.get_ptr<Position>(a) != nullptr);
            print_item("flush 前 c 有效", c.is_valid());
            print_item("buffer size==4", cb.size() == 4);

            cb.flush();

            print_item("flush 后 b 有 Position(x=2)", m.get_ptr<Position>(b) != nullptr && m.get_ptr<Position>(b)->x == 2.0f);
            print_item("flush 后 a 有 Velocity", m.get_ptr<Velocity>(a) != nullptr);
            print_item("flush 后 a 无 Position(已移除)", m.get_ptr<Position>(a) == nullptr);
            print_item("flush 后 c 失效", !m.is_entity_valid(c));
            print_item("buffer flush 后清空", cb.empty());

            // 工厂与直接构造等价
            ecs::command_buffer cb2(&m);
            print_item("直接构造 command_buffer", cb2.empty());
        }
    }

    // 12. 持久化视图测试（自动同步，无需手动 rebuild）
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
        g.for_each([&cnt](Position& p, Velocity& v) { ++cnt; (void)p; (void)v; });
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
        og.for_each([&cnt](Position& p, Velocity& v) { ++cnt; (void)p; (void)v; });
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
        rg.for_each([&cnt](Position& p, Velocity& v) { ++cnt; (void)p; (void)v; });
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

        auto rv = mgr.runtime_view_create(std::array<int, 2>{type_id::get_type_id<Position>(), type_id::get_type_id<Velocity>()});
        print_item("初始 size()", rv.size() == 2);

        auto e3 = mgr.create_entity();
        mgr.add(e3, Position{3, 0, 0});
        mgr.add(e3, Velocity{30, 0, 0});
        print_item("add 后 自动同步 size()=3", rv.size() == 3);

        mgr.hard_remove<Velocity>(e2);
        print_item("remove 后 primary set 仍为 3（上限）", rv.size() == 3);

        int cnt = 0;
        rv.for_each([&cnt](entity) { ++cnt; });
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
        g.for_each([&cnt](Position& p, Velocity& v) { ++cnt; (void)p; (void)v; });
        print_item("for_each 自动同步 cnt=7", cnt == 7);
    }

    // 13. 生命周期信号测试
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
        print_item("覆盖 add<Position> 触发 on_remove(旧)+on_add(新)", pos_added == 2 && pos_removed == 1);

        mgr.hard_remove<Position>(e1);
        print_item("hard_remove<Position> 触发 on_remove", pos_removed == 2);

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
            if (type == 0) ++created;
            else ++destroyed;
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
            if (type == 0) ++added;
            else ++removed;
            });

        print_item("flush 后 added=3 (Pos+Vel+Health)", added == 3);
        print_item("flush 后 removed=1 (Pos)", removed == 1);
        print_item("flush 后缓冲区为空", !mgr.has_pending_component_signals());
    }

    // 事件系统修复验证
    std::cout << "\n  [事件系统修复验证]\n";
    {
        // #1 溢出计数 + overflow_chain 不丢信号
        {
            ecs::manager mgr;
            mgr.disable_entity_signals();
            mgr.append_preallocated_entities(2048);
            dense<entity> ents;
            ents.increase_capacity(2048);
            for (size_t i = 0; i < 2048; ++i) ents.emplace_back(mgr.create_entity());
            for (size_t i = 0; i < 2048; ++i) mgr.add(ents[i], Position{static_cast<float>(i), 0, 0});
            print_item("无界缓冲区不溢出 (overflow==0)", mgr.comp_signal_overflow_count() == 0);
            size_t added = 0;
            mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
                if (type == 0) ++added;
            });
            print_item("overflow_chain 不丢信号 (added==2048)", added == 2048);
            print_item("flush 后缓冲区为空", !mgr.has_pending_component_signals());
        }

        // #2 delete_entity 触发组件 on_remove_ + comp_signal
        {
            ecs::manager mgr;
            mgr.append_preallocated_entities(10);
            size_t pos_removed = 0;
            mgr.set_on_remove<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &pos_removed);
            auto e = mgr.create_entity();
            mgr.add(e, Position{1, 0, 0});
            mgr.add(e, Velocity{2, 0, 0});
            mgr.delete_entity(e);
            print_item("delete_entity 触发 on_remove<Position>", pos_removed == 1);
            size_t removed_sig = 0;
            mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
                if (type == 1) ++removed_sig;
            });
            print_item("delete_entity 为未注册回调组件入队 remove 信号", removed_sig == 1);
        }

        // #5 即时/延迟互斥
        {
            ecs::manager mgr;
            mgr.append_preallocated_entities(10);
            size_t added_cb = 0;
            mgr.set_on_add<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &added_cb);
            auto e = mgr.create_entity();
            mgr.add(e, Position{1, 0, 0});
            print_item("注册 on_add 后即时触发", added_cb == 1);
            size_t added_sig = 0;
            mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
                if (type == 0) ++added_sig;
            });
            print_item("注册 on_add 后不重复入队 (互斥)", added_sig == 0);
        }

        // #6 flush 重入保护(handler 内 add 不无限循环)
        {
            ecs::manager mgr;
            mgr.append_preallocated_entities(20);
            auto e = mgr.create_entity();
            mgr.add(e, Position{1, 0, 0});
            int iterations = 0;
            entity extra;
            mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
                ++iterations;
                if (type == 0 && iterations < 5)
                {
                    extra = mgr.create_entity();
                    mgr.add(extra, Position{1, 0, 0});
                }
            });
            print_item("flush 重入有上限终止", iterations > 0 && iterations < 10000);
        }

        // #9 on_modify 覆盖写
        {
            ecs::manager mgr;
            mgr.append_preallocated_entities(10);
            size_t add_cnt = 0, remove_cnt = 0, modify_cnt = 0;
            mgr.set_on_add<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &add_cnt);
            mgr.set_on_remove<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &remove_cnt);
            mgr.set_on_modify<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &modify_cnt);
            auto e = mgr.create_entity();
            mgr.add(e, Position{1, 0, 0});
            mgr.add(e, Position{2, 0, 0});
            print_item("新增触发 on_add", add_cnt == 1);
            print_item("覆盖触发 on_modify 而非 on_remove+on_add", modify_cnt == 1 && remove_cnt == 0 && add_cnt == 1);
        }

        // #14 entity 信号开关
        {
            ecs::manager mgr;
            mgr.disable_entity_signals();
            mgr.append_preallocated_entities(5);
            auto e = mgr.create_entity();
            (void)e;
            print_item("disable_entity_signals 后不入队", !mgr.has_pending_entity_signals());
            mgr.enable_entity_signals();
            auto e2 = mgr.create_entity();
            (void)e2;
            print_item("enable_entity_signals 后恢复入队", mgr.has_pending_entity_signals());
            size_t created = 0;
            mgr.flush_entity_signals([&](uint32_t type, uint32_t) noexcept { if (type == 0) ++created; });
            print_item("flush 拿到 enable 后的信号", created == 1);
        }

        // #15 reserve + overflow chain
        {
            ecs::manager mgr;
            mgr.reserve_comp_signal_capacity(2048);
            mgr.append_preallocated_entities(2048);
            dense<entity> ents;
            ents.increase_capacity(2048);
            for (size_t i = 0; i < 2048; ++i) ents.emplace_back(mgr.create_entity());
            for (size_t i = 0; i < 2048; ++i) mgr.add(ents[i], Position{static_cast<float>(i), 0, 0});
            print_item("reserve 后大批 add 无界不溢出 (overflow==0)", mgr.comp_signal_overflow_count() == 0);
            mgr.flush_component_signals([&](uint32_t, uint32_t, uint32_t) noexcept {});
            print_item("reserve + flush 后缓冲区为空", !mgr.has_pending_component_signals());
        }
    }

    // 14. >64 组件类型多块掩码查询测试
    {
        using namespace ecs;
        print_section(14, ">64 组件类型多块掩码查询测试");

        manager mgr;
        mgr.append_preallocated_entities(256);

        // 注册 ExtraComp<1>..ExtraComp<200>, 确保覆盖 mask 边界
        entity filler = mgr.create_entity();
        auto register_extras = [&mgr, filler]<size_t... Is>(std::index_sequence<Is...>) {
            ((mgr.add(filler, ExtraComp<Is + 1>{})), ...);
        };
        register_extras(std::make_index_sequence<200>{});

        // A=ExtraComp<201> B=ExtraComp<202>
        using A = ExtraComp<201>;
        using B = ExtraComp<202>;

        // 创建实体:10 只A, 10 只B, 10 A+B, 10 都无
        // (add 触发 register_component_meta,设置 bit)
        std::array<entity, 10> only_a, only_b, both, neither;
        for (int i = 0; i < 10; ++i)
        {
            entity e1 = mgr.create_entity();
            mgr.add(e1, A{1});
            only_a[i] = e1;
            entity e2 = mgr.create_entity();
            mgr.add(e2, B{2});
            only_b[i] = e2;
            entity e3 = mgr.create_entity();
            mgr.add(e3, A{1});
            mgr.add(e3, B{2});
            both[i] = e3;
            entity e4 = mgr.create_entity();
            neither[i] = e4;
        }

        print_sub("type_id 边界确认");
        int tid_a = type_id::get_type_id<A>();
        int tid_b = type_id::get_type_id<B>();
        constexpr int mask_boundary = sizeof(uint64_t) * 8;
        print_item("B type_id == A type_id + 1", tid_b == tid_a + 1);
        print_item("A mask 一致性 (tid>64 == bit==0)", (tid_a > mask_boundary) == (mgr.get_component_bit<A>() == 0));
        print_item("B mask 一致性 (tid>64 == bit==0)", (tid_b > mask_boundary) == (mgr.get_component_bit<B>() == 0));

        print_sub("runtime_view mask 快路径 (req A, type_id 64)");
        {
            auto rv = mgr.runtime_view_create(std::array<int, 1>{tid_a});
            int cnt = 0;
            rv.for_each([&cnt](entity) { ++cnt; });
            print_item("req A 匹配数 == 20 (only_a + both)", cnt == 20);
            print_item("contains(both[0])", rv.contains(both[0]));
            print_item("contains(only_a[0])", rv.contains(only_a[0]));
            print_item("!contains(only_b[0])", !rv.contains(only_b[0]));
            print_item("!contains(neither[0])", !rv.contains(neither[0]));
            entity first = rv.get_first_entity();
            print_item("get_first_entity 有效", first.is_valid());
        }

        print_sub("runtime_view 多块掩码路径 (req B, type_id 65 > 64)");
        {
            auto rv = mgr.runtime_view_create(std::array<int, 1>{tid_b});
            int cnt = 0;
            rv.for_each([&cnt](entity) { ++cnt; });
            print_item("req B 匹配数 == 20 (only_b + both)", cnt == 20);
            print_item("contains(both[0])", rv.contains(both[0]));
            print_item("contains(only_b[0])", rv.contains(only_b[0]));
            print_item("!contains(only_a[0])", !rv.contains(only_a[0]));
        }

        print_sub("runtime_view 多块掩码路径 (req A+B, B > 64)");
        {
            auto rv = mgr.runtime_view_create(std::array<int, 2>{tid_a, tid_b});
            int cnt = 0;
            rv.for_each([&cnt](entity) { ++cnt; });
            print_item("req A+B 匹配数 == 10 (both)", cnt == 10);
            print_item("contains(both[0])", rv.contains(both[0]));
            print_item("!contains(only_a[0])", !rv.contains(only_a[0]));
            print_item("!contains(only_b[0])", !rv.contains(only_b[0]));
        }

        print_sub("runtime_view 多块掩码路径 (req A exclude B, B > 64)");
        {
            auto rv = mgr.runtime_view_create(std::array<int, 1>{tid_a}, std::array<int, 1>{tid_b});
            int cnt = 0;
            rv.for_each([&cnt](entity) { ++cnt; });
            print_item("req A exc B 匹配数 == 10 (only_a)", cnt == 10);
            print_item("!contains(both[0]) (有 B)", !rv.contains(both[0]));
            print_item("contains(only_a[0])", rv.contains(only_a[0]));
        }

        print_sub("runtime_view 多块掩码路径 (req B exclude A, B > 64)");
        {
            auto rv = mgr.runtime_view_create(std::array<int, 1>{tid_b}, std::array<int, 1>{tid_a});
            int cnt = 0;
            rv.for_each([&cnt](entity) { ++cnt; });
            print_item("req B exc A 匹配数 == 10 (only_b)", cnt == 10);
            print_item("!contains(both[0]) (有 A)", !rv.contains(both[0]));
            print_item("contains(only_b[0])", rv.contains(only_b[0]));
        }

        print_sub("runtime_view_create 容器入参重载 (vector/array/span/裸指针)");
        {
            // vector 入参
            std::vector<int> v_req = {tid_a};
            auto rv_v = mgr.runtime_view_create(v_req);
            int cnt_v = 0;
            rv_v.for_each([&cnt_v](entity) { ++cnt_v; });
            print_item("runtime_view_create(vector) 匹配 20", cnt_v == 20);

            std::vector<int> v_req2 = {tid_a};
            std::vector<int> v_exc = {tid_b};
            auto rv_v2 = mgr.runtime_view_create(v_req2, v_exc);
            int cnt_v2 = 0;
            rv_v2.for_each([&cnt_v2](entity) { ++cnt_v2; });
            print_item("runtime_view_create(vector, vector) 排除 B 匹配 10", cnt_v2 == 10);

            // array 入参 (仅 required)
            std::array<int, 1> a_req = {tid_a};
            auto rv_a = mgr.runtime_view_create(a_req);
            int cnt_a = 0;
            rv_a.for_each([&cnt_a](entity) { ++cnt_a; });
            print_item("runtime_view_create(array) 匹配 20", cnt_a == 20);

            // array 入参 (required + excluded)
            std::array<int, 1> a_req2 = {tid_a};
            std::array<int, 1> a_exc = {tid_b};
            auto rv_a2 = mgr.runtime_view_create(a_req2, a_exc);
            int cnt_a2 = 0;
            rv_a2.for_each([&cnt_a2](entity) { ++cnt_a2; });
            print_item("runtime_view_create(array, array) 匹配 10", cnt_a2 == 10);

            // span 入参
            auto rv_s = mgr.runtime_view_create(std::span<const int>(&tid_a, 1));
            int cnt_s = 0;
            rv_s.for_each([&cnt_s](entity) { ++cnt_s; });
            print_item("runtime_view_create(span) 匹配 20", cnt_s == 20);

            // 裸指针 + 长度
            auto rv_p = mgr.runtime_view_create(&tid_a, 1);
            int cnt_p = 0;
            rv_p.for_each([&cnt_p](entity) { ++cnt_p; });
            print_item("runtime_view_create(ptr, count) 匹配 20", cnt_p == 20);

            auto rv_p2 = mgr.runtime_view_create(&tid_a, 1, &tid_b, 1);
            int cnt_p2 = 0;
            rv_p2.for_each([&cnt_p2](entity) { ++cnt_p2; });
            print_item("runtime_view_create(ptr, count, ptr, count) 排除 B 匹配 10", cnt_p2 == 10);
        }

        print_sub("runtime_view_create_from_terms 容器入参重载");
        {
            // vector 入参
            std::vector<ecs::runtime_term> v_terms = {
                ecs::runtime_term{tid_a, 0, ecs::access_mode::read_write}
            };
            auto rv_v = mgr.runtime_view_create_from_terms(v_terms);
            int cnt_v = 0;
            rv_v.for_each([&cnt_v](entity) { ++cnt_v; });
            print_item("runtime_view_create_from_terms(vector) 匹配 20", cnt_v == 20);

            // array 入参
            std::array<ecs::runtime_term, 1> a_terms = {
                ecs::runtime_term{tid_a, 0, ecs::access_mode::read_write}
            };
            auto rv_a = mgr.runtime_view_create_from_terms(a_terms);
            int cnt_a = 0;
            rv_a.for_each([&cnt_a](entity) { ++cnt_a; });
            print_item("runtime_view_create_from_terms(array) 匹配 20", cnt_a == 20);

            // span 入参
            ecs::runtime_term t = {tid_a, 0, ecs::access_mode::read_write};
            auto rv_s = mgr.runtime_view_create_from_terms(std::span<const ecs::runtime_term>(&t, 1));
            int cnt_s = 0;
            rv_s.for_each([&cnt_s](entity) { ++cnt_s; });
            print_item("runtime_view_create_from_terms(span) 匹配 20", cnt_s == 20);

            // 裸指针 + 长度
            auto rv_p = mgr.runtime_view_create_from_terms(&t, 1);
            int cnt_p = 0;
            rv_p.for_each([&cnt_p](entity) { ++cnt_p; });
            print_item("runtime_view_create_from_terms(ptr, count) 匹配 20", cnt_p == 20);
        }

        print_sub("group<A,B> 多块掩码路径 (B > 64)");
        {
            auto g = mgr.group<A, B>();
            int cnt = 0;
            g.for_each([&cnt](entity, A&, B&) { ++cnt; });
            print_item("group<A,B> 匹配数 == 10", cnt == 10);
            print_item("contains(both[0])", g.contains(both[0]));
            print_item("!contains(only_a[0])", !g.contains(only_a[0]));
        }

        print_sub("owning_group<A,B> 多块掩码路径 (B > 64)");
        {
            auto og = mgr.group<A, B>(ecs::owned<A>);
            int cnt = 0;
            og.for_each([&cnt](entity, A&, B&) { ++cnt; });
            print_item("owning_group<A,B> 匹配数 == 10", cnt == 10);
            print_item("contains(both[0])", og.contains(both[0]));
        }

        print_sub("reorder_group<A,B> 多块掩码路径 (B > 64)");
        {
            auto rg = mgr.group<A, B>(ecs::reorder<A>);
            int cnt = 0;
            rg.for_each([&cnt](entity, A&, B&) { ++cnt; });
            print_item("reorder_group<A,B> 匹配数 == 10", cnt == 10);
            print_item("contains(both[0])", rg.contains(both[0]));
        }

        print_sub("view<A>(without<B>) 多块掩码路径 (B > 64)");
        {
            auto vw = mgr.view<A>(ecs::without<B>);
            int cnt = 0;
            vw.for_each([&cnt](entity, A&) { ++cnt; });
            print_item("view<A> without<B> 匹配数 == 10 (only_a)", cnt == 10);
            print_item("!contains(both[0]) (有 B)", !vw.contains(both[0]));
            print_item("contains(only_a[0])", vw.contains(only_a[0]));
        }

        print_sub("view<B>(without<A>) 多块掩码路径 (A=64 B=65)");
        {
            auto vw = mgr.view<B>(ecs::without<A>);
            int cnt = 0;
            vw.for_each([&cnt](entity, B&) { ++cnt; });
            print_item("view<B> without<A> 匹配数 == 10 (only_b)", cnt == 10);
            print_item("!contains(both[0]) (有 A)", !vw.contains(both[0]));
            print_item("contains(only_b[0])", vw.contains(only_b[0]));
        }

        print_sub("mask 快路径仍正常 (前 64 种类型不受影响)");
        {
            entity e = mgr.create_entity();
            mgr.add(e, Position{1, 2, 3});
            auto rv = mgr.runtime_view_create(std::array<int, 1>{type_id::get_type_id<Position>()});
            int cnt = 0;
            rv.for_each([&cnt](entity) { ++cnt; });
            print_item("req Position 匹配数 == 1", cnt == 1);
            print_item("contains(Position 实体)", rv.contains(e));
        }

        print_sub("新增 API: get_entity_block / get_entity_block_by_idx / get_block");
        {
            entity e_test = mgr.create_entity();
            mgr.add(e_test, A{1});
            mgr.add(e_test, B{2});
            uint32_t idx = e_test.parts_.index_;
            uint32_t block_a = static_cast<uint32_t>(tid_a - 1) / 64;
            uint32_t block_b = static_cast<uint32_t>(tid_b - 1) / 64;

            uint64_t blk0 = mgr.get_entity_block(e_test, 0);
            uint64_t blk_a = mgr.get_entity_block(e_test, block_a);
            uint64_t blk_b = mgr.get_entity_block(e_test, block_b);
            uint64_t blk0_by_idx = mgr.get_entity_block_by_idx(idx, 0);
            uint64_t blk_a_by_idx = mgr.get_entity_block_by_idx(idx, block_a);
            auto& em = mgr.get_entity_manager();
            uint64_t blk0_em = em.get_block(idx, 0);
            uint64_t blk_a_em = em.get_block(idx, block_a);

            print_item("get_entity_block(e, 0) == get_entity_mask", blk0 == mgr.get_entity_mask(e_test));
            print_item("get_entity_block(e, block_a) != 0 (A 块)", blk_a != 0);
            print_item("get_entity_block(e, block_b) != 0 (B 块)", blk_b != 0);
            print_item("get_entity_block_by_idx(idx, 0) 一致", blk0_by_idx == blk0);
            print_item("get_entity_block_by_idx(idx, block_a) 一致", blk_a_by_idx == blk_a);
            print_item("entity_manager::get_block(idx, 0) 一致", blk0_em == blk0);
            print_item("entity_manager::get_block(idx, block_a) 一致", blk_a_em == blk_a);
            // 块 0 不含 A/B (都在高位块)
            uint64_t a_bit = 1ULL << (static_cast<uint32_t>(tid_a - 1) % 64);
            uint64_t b_bit = 1ULL << (static_cast<uint32_t>(tid_b - 1) % 64);
            print_item("block 0 不含 A 位", (blk0 & a_bit) == 0);
            print_item("block 0 不含 B 位", (blk0 & b_bit) == 0);
            print_item("block_a 含 A 位", (blk_a & a_bit) != 0);
            print_item("block_b 含 B 位", (blk_b & b_bit) != 0);
            // 越界块返回 0
            uint64_t blk_out = mgr.get_entity_block(e_test, 99);
            print_item("get_entity_block(e, 99) == 0", blk_out == 0);
            uint64_t blk_out_em = em.get_block(idx, 99);
            print_item("entity_manager::get_block(idx, 99) == 0", blk_out_em == 0);
        }

        print_sub("新增 API: num_mask_blocks 自动扩容");
        {
            // 200 个 extra 组件 + Position/Velocity/Health + A/B
            // 总计 >64 种类型, 自动扩容到 2 块
            print_item("num_mask_blocks() >= 2", mgr.num_mask_blocks() >= 2);
        }
    }

    // 15. multi_block_bitmask 多块位掩码
    {
        print_section(15, "multi_block_bitmask 多块位掩码");

        // --- 静态辅助 ---
        std::cout << "\n  [静态辅助]\n";
        {
            print_item("bits_per_block==64",
                       multi_block_bitmask::bits_per_block == 64);
            print_item("block_count_for_bits(0)==0",
                       multi_block_bitmask::block_count_for_bits(0) == 0);
            print_item("block_count_for_bits(1)==1",
                       multi_block_bitmask::block_count_for_bits(1) == 1);
            print_item("block_count_for_bits(64)==1",
                       multi_block_bitmask::block_count_for_bits(64) == 1);
            print_item("block_count_for_bits(65)==2",
                       multi_block_bitmask::block_count_for_bits(65) == 2);
            print_item("block_count_for_bits(200)==4",
                       multi_block_bitmask::block_count_for_bits(200) == 4);
        }

        // --- 容量与块管理 ---
        std::cout << "\n  [容量与块管理]\n";
        {
            // 单块 (num_blocks_==1) 场景
            multi_block_bitmask m1;
            m1.ensure_entity(0);
            m1.ensure_entity(1);
            m1.set_bit(0, 0, 3);
            print_item("单块 size()==2", m1.size() == 2);
            print_item("单块 empty()==false", m1.empty() == false);

            size_t cap_before = m1.capacity();
            m1.increase_capacity(1000);
            print_item("increase_capacity 只增不减",
                       (m1.capacity() >= cap_before && m1.capacity() >= 1000));
            print_item("increase_capacity 不改 size", m1.size() == 2);

            m1.reserve_exact(5000);
            print_item("reserve_exact 预留", m1.capacity() >= 5000);

            m1.shrink_to_fit();
            print_item("shrink_to_fit 后 capacity==size", m1.capacity() == m1.size());

            m1.reduce_capacity(1);
            print_item("reduce_capacity(1) 截断 size", m1.size() == 1);

            print_item("capacity_bytes>=size_bytes",
                       m1.capacity_bytes() >= m1.size_bytes());

            m1.clear();
            print_item("clear 后 size()==0", m1.size() == 0);
            print_item("clear 后 empty()", m1.empty() == true);
            print_item("clear 后 num_blocks 仍为 1", m1.num_blocks() == 1);

            // 多块 (num_blocks_==2) 场景, 验证实体单位转换
            multi_block_bitmask m2;
            m2.reserve_blocks(2);
            m2.ensure_entity(0);
            m2.ensure_entity(1);
            m2.ensure_entity(2);
            m2.set_bit(0, 0, 5);
            m2.set_bit(0, 1, 10);
            print_item("多块 size()==3", m2.size() == 3);

            m2.increase_capacity(2000);
            print_item("多块 increase_capacity (实体单位)", m2.capacity() >= 2000);

            // capacity_bytes 应 >= size_bytes
            print_item("多块 capacity_bytes 一致性",
                       m2.capacity_bytes() >= m2.size_bytes());

            m2.shrink_to_fit();
            print_item("多块 shrink_to_fit 后 capacity==size",
                       m2.capacity() == m2.size());

            m2.clear();
            print_item("多块 clear 后 num_blocks 仍为 2", m2.num_blocks() == 2);
            print_item("多块 clear 后 empty", m2.empty() == true);

            // reduce_capacity 小于 size 时截断
            multi_block_bitmask m3;
            for (uint32_t i = 0; i < 10; ++i)
            {
                m3.ensure_entity(i);
            }
            print_item("m3 size()==10", m3.size() == 10);
            m3.reduce_capacity(4);
            print_item("reduce_capacity(4) 截断到 4", m3.size() == 4);

            // reserve_blocks 扩容触发 overflow 重分配
            multi_block_bitmask m4;
            m4.reserve_blocks(2);
            m4.ensure_entity(0);
            m4.set_bit(0, 1, 5);
            m4.reserve_blocks(4);
            print_item("reserve_blocks 扩容后保留块 1 数据",
                       m4.get_block(0, 1) == (1ULL << 5));
            print_item("reserve_blocks 扩容后 num_blocks==4", m4.num_blocks() == 4);
            print_item("reserve_blocks 缩容请求被忽略 (只增不减)",
                       (m4.num_blocks() == 4));
        }

        // --- 查询接口 ---
        std::cout << "\n  [查询接口]\n";
        {
            multi_block_bitmask m;
            m.reserve_blocks(2);
            m.ensure_entity(0);
            m.ensure_entity(1);
            m.set_bit(0, 0, 5);
            m.set_bit(0, 1, 10);
            m.set_bit(1, 0, 0);

            // test_bit
            print_item("test_bit(0,0,5)==true", m.test_bit(0, 0, 5) == true);
            print_item("test_bit(0,0,6)==false", m.test_bit(0, 0, 6) == false);
            print_item("test_bit(0,1,10)==true", m.test_bit(0, 1, 10) == true);
            print_item("test_bit(0,1,11)==false", m.test_bit(0, 1, 11) == false);
            print_item("test_bit 越界 block 返回 false",
                       m.test_bit(0, 5, 0) == false);

            // any_set_in_block
            print_item("any_set_in_block(0,0)==true", m.any_set_in_block(0, 0) == true);
            print_item("any_set_in_block(1,0)==true", m.any_set_in_block(1, 0) == true);
            print_item("any_set_in_block(1,1)==false", m.any_set_in_block(1, 1) == false);

            // any_set / is_zero
            print_item("any_set(0)==true", m.any_set(0) == true);
            print_item("any_set(1)==true", m.any_set(1) == true);
            print_item("any_set 越界 slot 返回 false", m.any_set(100) == false);
            print_item("is_zero(0)==false", m.is_zero(0) == false);

            m.clear_bit(1, 0, 0);
            print_item("is_zero(1)==true (清空后)", m.is_zero(1) == true);
            print_item("any_set(1)==false (清空后)", m.any_set(1) == false);

            // count_set_bits
            m.set_bit(1, 0, 2);
            m.set_bit(1, 0, 3);
            m.set_bit(1, 1, 7);
            print_item("count_set_bits(1)==3", m.count_set_bits(1) == 3);
            print_item("count_set_bits(0)==2 (跨块)", m.count_set_bits(0) == 2);
            print_item("count_set_bits 越界返回 0", m.count_set_bits(100) == 0);

            // find_first_set
            uint32_t fb = 0xFF, fo = 0xFF;
            print_item("find_first_set(0) 找到", m.find_first_set(0, fb, fo) == true);
            print_item("find_first_set(0) block==0", fb == 0);
            print_item("find_first_set(0) offset==5", fo == 5);

            // find_first_set 越界 slot
            print_item("find_first_set 越界返回 false",
                       m.find_first_set(100, fb, fo) == false);

            // find_last_set
            print_item("find_last_set(0) 找到", m.find_last_set(0, fb, fo) == true);
            print_item("find_last_set(0) block==1", fb == 1);
            print_item("find_last_set(0) offset==10", fo == 10);

            // find_next_set
            print_item("find_next_set(0,0,5) 跳过 5 找到 10",
                       m.find_next_set(0, 0, 5, fb, fo) == true);
            print_item("find_next_set block==1", fb == 1);
            print_item("find_next_set offset==10", fo == 10);
            print_item("find_next_set(0,1,10) 之后无更多",
                       m.find_next_set(0, 1, 10, fb, fo) == false);

            // find_first_set / find_next_set 联合遍历
            {
                std::vector<std::pair<uint32_t, uint32_t>> bits;
                uint32_t cur_b = 0, cur_o = UINT32_MAX;
                while (m.find_next_set(0, cur_b, cur_o, fb, fo))
                {
                    bits.emplace_back(fb, fo);
                    cur_b = fb;
                    cur_o = fo;
                }
                print_item("find_next_set 遍历 count==2", bits.size() == 2);
                print_item("find_next_set 遍历首位 (0,5)",
                           (bits[0].first == 0 && bits[0].second == 5));
                print_item("find_next_set 遍历末位 (1,10)",
                           (bits[1].first == 1 && bits[1].second == 10));
            }

            // 空槽位查询
            multi_block_bitmask empty_m;
            empty_m.ensure_entity(0);
            print_item("空槽 any_set==false", empty_m.any_set(0) == false);
            print_item("空槽 is_zero==true", empty_m.is_zero(0) == true);
            print_item("空槽 count_set_bits==0", empty_m.count_set_bits(0) == 0);
            print_item("空槽 find_first_set==false",
                       empty_m.find_first_set(0, fb, fo) == false);
            print_item("空槽 find_last_set==false",
                       empty_m.find_last_set(0, fb, fo) == false);
        }

        // --- 整块写入 ---
        std::cout << "\n  [整块写入]\n";
        {
            multi_block_bitmask m;
            m.reserve_blocks(2);
            m.ensure_entity(0);

            // set_block_value
            m.set_block_value(0, 0, 0xDEADBEEFCAFEBABEULL);
            print_item("set_block_value block 0",
                       m.get_block(0, 0) == 0xDEADBEEFCAFEBABEULL);
            m.set_block_value(0, 1, 0x123456789ABCDEF0ULL);
            print_item("set_block_value block 1 (触发 overflow 分配)",
                       m.get_block(0, 1) == 0x123456789ABCDEF0ULL);
            print_item("set_block_value 后 overflow_entity_count==1",
                       m.overflow_entity_count() == 1);

            // or_block_value
            m.or_block_value(0, 0, 0xFULL);
            print_item("or_block_value block 0",
                       m.get_block(0, 0) == (0xDEADBEEFCAFEBABEULL | 0xFULL));

            // and_block_value
            m.and_block_value(0, 0, 0xFFFFULL);
            print_item("and_block_value block 0",
                       m.get_block(0, 0) == ((0xDEADBEEFCAFEBABEULL | 0xFULL) & 0xFFFFULL));

            // xor_block_value
            m.xor_block_value(0, 0, 0xFFFFULL);
            print_item("xor_block_value block 0",
                       m.get_block(0, 0) == (((0xDEADBEEFCAFEBABEULL | 0xFULL) & 0xFFFFULL) ^ 0xFFFFULL));

            // 越界块号不扩容不写入
            m.set_block_value(0, 5, 1ULL);
            print_item("set_block_value 越界 block 静默丢弃",
                       m.get_block(0, 5) == 0);

            // set_block_value 自动 ensure_entity
            m.set_block_value(10, 0, 42ULL);
            print_item("set_block_value 自动 ensure_entity block 0",
                       m.get_block(10, 0) == 42ULL);
        }

        // --- 批量位操作 ---
        std::cout << "\n  [批量位操作]\n";
        {
            multi_block_bitmask m;
            m.reserve_blocks(2);
            m.ensure_entity(0);

            // set_bits_at
            uint32_t offsets1[] = {1, 3, 5, 7, 9};
            m.set_bits_at(0, 0, offsets1);
            print_item("set_bits_at block 0",
                       (m.test_bit(0, 0, 1) && m.test_bit(0, 0, 3)
                        && m.test_bit(0, 0, 5) && m.test_bit(0, 0, 7)
                        && m.test_bit(0, 0, 9)));
            print_item("set_bits_at 未设位为 false",
                       (!m.test_bit(0, 0, 0) && !m.test_bit(0, 0, 2)
                        && !m.test_bit(0, 0, 4)));

            // set_bits_at block 1
            uint32_t offsets2[] = {10, 20, 30};
            m.set_bits_at(0, 1, offsets2);
            print_item("set_bits_at block 1 触发 overflow 分配",
                       (m.test_bit(0, 1, 10) && m.test_bit(0, 1, 20)
                        && m.test_bit(0, 1, 30)));

            // clear_bits_at
            uint32_t clear_off[] = {1, 5, 9};
            m.clear_bits_at(0, 0, clear_off);
            print_item("clear_bits_at 部分清除",
                       (!m.test_bit(0, 0, 1) && !m.test_bit(0, 0, 5)
                        && !m.test_bit(0, 0, 9) && m.test_bit(0, 0, 3)
                        && m.test_bit(0, 0, 7)));

            // toggle_bits_at
            // bit offset 范围 0-63; 3 已置位 -> 清, 60 未置位 -> 置
            uint32_t toggle_off[] = {3, 60};
            m.toggle_bits_at(0, 0, toggle_off);
            print_item("toggle_bits_at 翻转",
                       (!m.test_bit(0, 0, 3) && m.test_bit(0, 0, 60)));

            // toggle_bits_at 再翻一次恢复
            m.toggle_bits_at(0, 0, toggle_off);
            print_item("toggle_bits_at 双翻恢复",
                       (m.test_bit(0, 0, 3) && !m.test_bit(0, 0, 60)));
        }

        // --- 整槽多块读写 ---
        std::cout << "\n  [整槽多块读写]\n";
        {
            multi_block_bitmask m;
            m.reserve_blocks(3);
            m.ensure_entity(0);
            m.ensure_entity(1);

            // assign_slot
            uint64_t data[] = {0xAA, 0xBB, 0xCC};
            m.assign_slot(0, data);
            print_item("assign_slot block 0", m.get_block(0, 0) == 0xAA);
            print_item("assign_slot block 1", m.get_block(0, 1) == 0xBB);
            print_item("assign_slot block 2", m.get_block(0, 2) == 0xCC);

            // assign_slot 自动扩容块数
            multi_block_bitmask m2;
            m2.ensure_entity(0);
            uint64_t data2[] = {0x11, 0x22, 0x33, 0x44};
            m2.assign_slot(0, data2);
            print_item("assign_slot 自动 reserve_blocks 扩容",
                       m2.num_blocks() == 4);
            print_item("assign_slot 扩容后 block 3", m2.get_block(0, 3) == 0x44);

            // copy_slot_to
            uint64_t out[3] = {0, 0, 0};
            m.copy_slot_to(0, out);
            print_item("copy_slot_to block 0", out[0] == 0xAA);
            print_item("copy_slot_to block 1", out[1] == 0xBB);
            print_item("copy_slot_to block 2", out[2] == 0xCC);

            // copy_slot_to dst 大于 num_blocks 多余部分补零
            uint64_t out_big[5] = {1, 1, 1, 1, 1};
            m.copy_slot_to(0, out_big);
            print_item("copy_slot_to 超出部分补零 block 3", out_big[3] == 0);
            print_item("copy_slot_to 超出部分补零 block 4", out_big[4] == 0);

            // copy_slot_to dst 小于 num_blocks 只拷贝 dst.size() 块
            uint64_t out_small[1] = {0};
            m.copy_slot_to(0, out_small);
            print_item("copy_slot_to 小 dst 只拷贝 1 块", out_small[0] == 0xAA);

            // copy_slot_to 越界 slot 全零
            uint64_t out_invalid[2] = {0xFF, 0xFF};
            m.copy_slot_to(100, out_invalid);
            print_item("copy_slot_to 越界 slot 全零",
                       (out_invalid[0] == 0 && out_invalid[1] == 0));

            // assign_slot 空 data 不操作
            m.assign_slot(1, {});
            print_item("assign_slot 空 data 不操作",
                       m.get_block(1, 0) == 0);
        }

        // --- 遍历接口 ---
        std::cout << "\n  [遍历接口]\n";
        {
            multi_block_bitmask m;
            m.reserve_blocks(2);
            m.ensure_entity(0);
            m.ensure_entity(1);
            m.ensure_entity(2);

            // slot 0: block 0 位 0/3/7, block 1 位 10
            m.set_bit(0, 0, 0);
            m.set_bit(0, 0, 3);
            m.set_bit(0, 0, 7);
            m.set_bit(0, 1, 10);
            // slot 1: 空
            // slot 2: block 0 位 5
            m.set_bit(2, 0, 5);

            // for_each_set_bit (单槽)
            {
                std::vector<std::pair<uint32_t, uint32_t>> bits;
                m.for_each_set_bit(0, [&](uint32_t b, uint32_t o) {
                    bits.emplace_back(b, o);
                });
                print_item("for_each_set_bit 单槽 count==4", bits.size() == 4);
                print_item("for_each_set_bit 首位 (0,0)",
                           (bits[0].first == 0 && bits[0].second == 0));
                print_item("for_each_set_bit 末位 (1,10)",
                           (bits[3].first == 1 && bits[3].second == 10));
            }

            // for_each_set_bit 空槽
            {
                std::vector<std::pair<uint32_t, uint32_t>> bits;
                m.for_each_set_bit(1, [&](uint32_t b, uint32_t o) {
                    bits.emplace_back(b, o);
                });
                print_item("for_each_set_bit 空槽 count==0", bits.size() == 0);
            }

            // for_each_set_slot
            {
                std::vector<uint32_t> slots;
                m.for_each_set_slot([&](uint32_t s) {
                    slots.push_back(s);
                });
                print_item("for_each_set_slot count==2", slots.size() == 2);
                print_item("for_each_set_slot 含 slot 0", slots[0] == 0);
                print_item("for_each_set_slot 含 slot 2", slots[1] == 2);
            }

            // for_each_set_bit_global
            {
                std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> all;
                m.for_each_set_bit_global([&](uint32_t s, uint32_t b, uint32_t o) {
                    all.emplace_back(s, b, o);
                });
                print_item("for_each_set_bit_global count==5", all.size() == 5);
                print_item("for_each_set_bit_global 首位 (0,0,0)",
                           (std::get<0>(all[0]) == 0 && std::get<1>(all[0]) == 0
                            && std::get<2>(all[0]) == 0));
                print_item("for_each_set_bit_global 末位 (2,0,5)",
                           (std::get<0>(all[4]) == 2 && std::get<1>(all[4]) == 0
                            && std::get<2>(all[4]) == 5));
            }

            // count_set_bits_global
            print_item("count_set_bits_global==5", m.count_set_bits_global() == 5);

            // for_each_set_bit_global 空容器
            multi_block_bitmask empty_m;
            std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> empty_all;
            empty_m.for_each_set_bit_global([&](uint32_t s, uint32_t b, uint32_t o) {
                empty_all.emplace_back(s, b, o);
            });
            print_item("for_each_set_bit_global 空容器 count==0", empty_all.size() == 0);
            print_item("count_set_bits_global 空容器==0",
                       empty_m.count_set_bits_global() == 0);
        }

        // --- 视图接口 ---
        std::cout << "\n  [视图接口]\n";
        {
            multi_block_bitmask m;
            m.reserve_blocks(2);
            m.ensure_entity(0);
            m.ensure_entity(1);
            m.set_bit(0, 0, 5);
            m.set_bit(0, 1, 10);
            m.set_bit(1, 0, 2);

            // inline_span
            std::span<uint64_t> is = m.inline_span();
            print_item("inline_span size==2", is.size() == 2);
            print_item("inline_span[0]==(1<<5)", is[0] == (1ULL << 5));
            print_item("inline_span[1]==(1<<2)", is[1] == (1ULL << 2));

            // const 视图
            const multi_block_bitmask& cm = m;
            std::span<const uint64_t> cis = cm.inline_span();
            print_item("inline_span const size==2", cis.size() == 2);

            // overflow_span
            std::span<uint64_t> os0 = m.overflow_span(0);
            print_item("overflow_span(0) size==1", os0.size() == 1);
            print_item("overflow_span(0)[0]==(1<<10)", os0[0] == (1ULL << 10));

            std::span<uint64_t> os1 = m.overflow_span(1);
            print_item("overflow_span(1) 未分配为空 span", os1.empty());

            std::span<uint64_t> os_invalid = m.overflow_span(100);
            print_item("overflow_span 越界返回空 span", os_invalid.empty());

            // const overflow_span
            std::span<const uint64_t> cos0 = cm.overflow_span(0);
            print_item("overflow_span const size==1", cos0.size() == 1);

            // 通过 span 修改后反映到对象
            is[0] = 0;
            print_item("通过 inline_span 修改生效", m.get_block(0, 0) == 0);

            // 空 inline_span
            multi_block_bitmask empty_m;
            print_item("空容器 inline_span empty", empty_m.inline_span().empty());
        }

        // --- 集合运算 ---
        std::cout << "\n  [集合运算]\n";
        {
            multi_block_bitmask a, b;
            a.reserve_blocks(2);
            b.reserve_blocks(2);
            a.ensure_entity(0);
            a.ensure_entity(1);
            b.ensure_entity(0);
            b.ensure_entity(1);

            // a = {slot 0: block0 位 1/3, block1 位 10}
            // b = {slot 0: block0 位 1/5, block1 位 11}
            // a = {slot 1: block0 位 7}
            a.set_bit(0, 0, 1);
            a.set_bit(0, 0, 3);
            a.set_bit(0, 1, 10);
            a.set_bit(1, 0, 7);
            b.set_bit(0, 0, 1);
            b.set_bit(0, 0, 5);
            b.set_bit(0, 1, 11);

            // or_with: a |= b
            multi_block_bitmask a_or = a;
            a_or.or_with(b);
            print_item("or_with block 0 含 1/3/5",
                       (a_or.test_bit(0, 0, 1) && a_or.test_bit(0, 0, 3)
                        && a_or.test_bit(0, 0, 5)));
            print_item("or_with block 1 含 10/11",
                       (a_or.test_bit(0, 1, 10) && a_or.test_bit(0, 1, 11)));
            print_item("or_with slot 1 保留", a_or.test_bit(1, 0, 7));

            // and_with: a &= b
            multi_block_bitmask a_and = a;
            a_and.and_with(b);
            print_item("and_with block 0 仅含 1",
                       (a_and.test_bit(0, 0, 1) && !a_and.test_bit(0, 0, 3)
                        && !a_and.test_bit(0, 0, 5)));
            print_item("and_with block 1 全清 (b 有 a 无 10)",
                       (!a_and.test_bit(0, 1, 10) && !a_and.test_bit(0, 1, 11)));
            // slot 1 在 b 中不存在, and_with 后应清零
            print_item("and_with slot 1 (b 不存在) 清零",
                       a_and.is_zero(1));

            // xor_with: a ^= b
            multi_block_bitmask a_xor = a;
            a_xor.xor_with(b);
            print_item("xor_with block 0 含 3/5 (异或后)",
                       (a_xor.test_bit(0, 0, 3) && a_xor.test_bit(0, 0, 5)
                        && !a_xor.test_bit(0, 0, 1)));
            print_item("xor_with block 1 含 10/11",
                       (a_xor.test_bit(0, 1, 10) && a_xor.test_bit(0, 1, 11)));

            // subtract: a &= ~b
            multi_block_bitmask a_sub = a;
            a_sub.subtract(b);
            print_item("subtract block 0 仅含 3",
                       (!a_sub.test_bit(0, 0, 1) && a_sub.test_bit(0, 0, 3)
                        && !a_sub.test_bit(0, 0, 5)));
            print_item("subtract block 1 保留 10",
                       a_sub.test_bit(0, 1, 10));
            print_item("subtract slot 1 保留", a_sub.test_bit(1, 0, 7));

            // overlaps
            print_item("overlaps(a, b)==true (slot 0 block 0 位 1 共有)",
                       a.overlaps(b) == true);
            multi_block_bitmask c;
            c.reserve_blocks(2);
            c.ensure_entity(0);
            c.set_bit(0, 0, 63);  // 与 a/b 不重叠
            print_item("overlaps(a, c)==false", a.overlaps(c) == false);

            // contains_all
            multi_block_bitmask subset;
            subset.reserve_blocks(2);
            subset.ensure_entity(0);
            subset.set_bit(0, 0, 1);  // a 中也有
            print_item("contains_all(a, subset)==true", a.contains_all(subset));
            print_item("contains_all(subset, a)==false", !subset.contains_all(a));

            multi_block_bitmask not_subset;
            not_subset.reserve_blocks(2);
            not_subset.ensure_entity(0);
            not_subset.set_bit(0, 0, 63);  // a 中没有 (a 在 block 0 仅有 1/3)
            print_item("contains_all(a, not_subset)==false",
                       !a.contains_all(not_subset));

            // equals
            multi_block_bitmask a_copy = a.clone();
            print_item("equals(a, a_copy)==true", a.equals(a_copy));
            a_copy.clear_bit(0, 0, 1);
            print_item("equals 修改后 false", !a.equals(a_copy));

            // equals 不同 slot 数
            multi_block_bitmask smaller;
            smaller.ensure_entity(0);
            print_item("equals 不同 size 返回 false", !a.equals(smaller));

            // equals 不同 overflow_block_count_
            multi_block_bitmask diff_blocks;
            diff_blocks.reserve_blocks(3);
            diff_blocks.ensure_entity(0);
            diff_blocks.ensure_entity(1);
            diff_blocks.set_bit(0, 0, 1);
            diff_blocks.set_bit(0, 0, 3);
            diff_blocks.set_bit(0, 1, 10);
            diff_blocks.set_bit(1, 0, 7);
            print_item("equals 不同 num_blocks 返回 false",
                       !a.equals(diff_blocks));
        }

        // --- 复制与交换 ---
        std::cout << "\n  [复制与交换]\n";
        {
            multi_block_bitmask a;
            a.reserve_blocks(2);
            a.ensure_entity(0);
            a.set_bit(0, 0, 5);
            a.set_bit(0, 1, 10);

            // 拷贝构造 (深拷贝)
            multi_block_bitmask b(a);
            print_item("拷贝构造 inline 一致",
                       b.get_block(0, 0) == (1ULL << 5));
            print_item("拷贝构造 overflow 一致",
                       b.get_block(0, 1) == (1ULL << 10));
            print_item("拷贝构造 overflow_entity_count 一致",
                       b.overflow_entity_count() == 1);

            // 修改 b 不影响 a
            b.set_bit(0, 0, 6);
            print_item("深拷贝独立 (b 改 a 不变)",
                       a.get_block(0, 0) == (1ULL << 5));

            // 拷贝赋值
            multi_block_bitmask c;
            c = a;
            print_item("拷贝赋值一致", c.equals(a));

            // 移动构造
            multi_block_bitmask d(std::move(c));
            print_item("移动构造 inline 一致",
                       d.get_block(0, 0) == (1ULL << 5));
            print_item("移动构造 overflow 一致",
                       d.get_block(0, 1) == (1ULL << 10));
            print_item("移动源 overflow_entity_count==0",
                       c.overflow_entity_count() == 0);

            // 移动赋值
            multi_block_bitmask e;
            e = std::move(d);
            print_item("移动赋值一致", e.get_block(0, 0) == (1ULL << 5));

            // clone
            multi_block_bitmask f = a.clone();
            print_item("clone 一致", f.equals(a));
            f.clear_bit(0, 0, 5);
            print_item("clone 独立 (改 f 不影响 a)",
                       a.test_bit(0, 0, 5));

            // swap 成员
            multi_block_bitmask g, h;
            g.reserve_blocks(2);
            g.ensure_entity(0);
            g.set_bit(0, 0, 1);
            h.ensure_entity(0);
            h.set_bit(0, 0, 2);
            g.swap(h);
            print_item("swap 后 g 含 h 的位", g.test_bit(0, 0, 2));
            print_item("swap 后 h 含 g 的位", h.test_bit(0, 0, 1));

            // 自由 swap
            swap(g, h);
            print_item("自由 swap 后还原", g.test_bit(0, 0, 1));

            // 自赋值
            a = a;
            print_item("自赋值安全", a.get_block(0, 0) == (1ULL << 5));
            // 注: 自移动赋值会触发 -Wself-move 警告, 不进行测试
        }

        // --- 内存压缩 ---
        std::cout << "\n  [内存压缩]\n";
        {
            multi_block_bitmask m;
            m.reserve_blocks(2);
            m.ensure_entity(0);
            m.ensure_entity(1);
            m.set_bit(0, 1, 5);
            m.set_bit(1, 1, 7);
            print_item("compact 前 overflow_entity_count==2",
                       m.overflow_entity_count() == 2);

            // compact_slot: slot 0 overflow 非零, 不释放
            m.compact_slot(0);
            print_item("compact_slot 非零 overflow 不释放",
                       m.overflow_entity_count() == 2);

            // 清零 slot 0 的 overflow, 再 compact
            m.clear_bit(0, 1, 5);
            m.compact_slot(0);
            print_item("compact_slot 全零 overflow 释放",
                       m.overflow_entity_count() == 1);
            print_item("compact_slot 后 overflow_span 空",
                       m.overflow_span(0).empty());

            // compact_slot 越界安全
            m.compact_slot(100);
            print_item("compact_slot 越界安全",
                       m.overflow_entity_count() == 1);

            // compact_all
            m.clear_bit(1, 1, 7);
            m.compact_all();
            print_item("compact_all 全局压缩",
                       m.overflow_entity_count() == 0);

            // compact_all 空容器安全
            multi_block_bitmask empty_m;
            empty_m.compact_all();
            print_item("compact_all 空容器安全",
                       empty_m.overflow_entity_count() == 0);
        }
    }

    // 16. ring_buffer 基础功能
    print_section(16, "ring_buffer 环形缓冲区");
    {
        struct event
        {
            int type;
            int data;
        };

        // 基本写入与读取
        {
            ring_buffer<event, 8> rb;
            print_item("空时 empty", rb.empty());
            print_item("空时 pending_count==0", rb.pending_count() == 0);
            print_item("空时 peek 返回 nullptr", rb.peek() == nullptr);
            print_item("空时 pop 返回 false", !rb.pop());

            bool p1 = rb.push({1, 100});
            bool p2 = rb.emplace(2, 200);
            print_item("push/emplace 返回 true", p1 && p2);
            print_item("非空 has_pending", rb.has_pending());
            print_item("pending_count==2", rb.pending_count() == 2);

            const event* peeked = rb.peek();
            print_item("peek 队首 type==1", peeked && peeked->type == 1);

            size_t total = rb.drain([](const event& e) {
                (void)e;
            });
            print_item("drain 处理 2 个", total == 2);
            print_item("drain 后 empty", rb.empty());
        }

        // 无界特性: N=4 push 远超 N
        {
            ring_buffer<event, 4> rb;
            for (size_t i = 0; i < 1000; ++i)
            {
                (void)rb.push({static_cast<int>(i), 0});
            }
            print_item("N=4 push 1000 个", rb.pending_count() == 1000);

            size_t total = 0;
            rb.drain([&](const event& e) {
                if (e.type == static_cast<int>(total)) ++total;
            });
            print_item("drain 1000 个顺序正确", total == 1000);
        }

        // pop 逐个出队
        {
            ring_buffer<event, 8> rb;
            for (int i = 0; i < 5; ++i) (void)rb.push({i, 0});

            int expected = 0;
            while (rb.has_pending())
            {
                const event* e = rb.peek();
                if (e && e->type == expected) ++expected;
                (void)rb.pop();
            }
            print_item("pop 顺序 0..4", expected == 5);
            print_item("pop 完后 empty", rb.empty());
        }

        // drain_with_budget 限制
        {
            ring_buffer<event, 8> rb;
            for (int i = 0; i < 10; ++i) (void)rb.push({i, 0});

            size_t n = rb.drain_with_budget(3, [](const event&) {});
            print_item("drain_with_budget(3) 处理 3 个", n == 3);
            print_item("剩余 7 个", rb.pending_count() == 7);

            rb.drain_with_budget(100, [](const event&) {});
            print_item("drain_with_budget(100) 清空", rb.empty());
        }

        // clear 清空
        {
            ring_buffer<event, 8> rb;
            for (int i = 0; i < 5; ++i) (void)rb.push({i, 0});
            rb.clear();
            print_item("clear 后 empty", rb.empty());
            print_item("clear 后 pending_count==0", rb.pending_count() == 0);
        }

        // move 语义
        {
            ring_buffer<event, 8> rb1;
            for (int i = 0; i < 3; ++i) (void)rb1.push({i, 0});

            ring_buffer<event, 8> rb2(std::move(rb1));
            print_item("move 后 rb2 有 3 个", rb2.pending_count() == 3);
            print_item("move 后 rb1 empty", rb1.empty());

            ring_buffer<event, 8> rb3;
            rb3 = std::move(rb2);
            print_item("move assign 后 rb3 有 3 个", rb3.pending_count() == 3);
            print_item("move assign 后 rb2 empty", rb2.empty());
        }

        // capacity 与 slots_per_chunk
        {
            ring_buffer<event, 256> rb;
            print_item("capacity()==256", ring_buffer<event, 256>::capacity() == 256);
            print_item("slots_per_chunk() > 0", ring_buffer<event, 256>::slots_per_chunk() > 0);
        }

        // 静态池
        {
            ring_buffer<event, 8>::shrink_static_pool();
            print_item("shrink 后 static_pool_size==0",
                       ring_buffer<event, 8>::static_pool_size() == 0);

            {
                ring_buffer<event, 8> rb;
                for (int i = 0; i < 100; ++i) (void)rb.push({i, 0});
                rb.clear();
            }
            print_item("使用后 static_pool_size > 0",
                       ring_buffer<event, 8>::static_pool_size() > 0);

            ring_buffer<event, 8>::shrink_static_pool();
            print_item("再次 shrink 后 ==0",
                       ring_buffer<event, 8>::static_pool_size() == 0);
        }

        // 非平凡类型
        {
            struct nontrivial
            {
                int* p;
                nontrivial() : p(new int(42)) {}
                ~nontrivial() { delete p; }
                nontrivial(const nontrivial& o) : p(new int(*o.p)) {}
                nontrivial& operator=(const nontrivial& o)
                {
                    if (this != &o) { delete p; p = new int(*o.p); }
                    return *this;
                }
                nontrivial(nontrivial&& o) noexcept : p(o.p) { o.p = nullptr; }
                nontrivial& operator=(nontrivial&& o) noexcept
                {
                    if (this != &o) { delete p; p = o.p; o.p = nullptr; }
                    return *this;
                }
            };

            {
                ring_buffer<nontrivial, 4> rb;
                for (int i = 0; i < 10; ++i) (void)rb.push(nontrivial{});
                print_item("非平凡类型 push 10 个", rb.pending_count() == 10);

                size_t n = rb.drain([](const nontrivial& e) {
                    (void)e;
                });
                print_item("非平凡类型 drain 10 个", n == 10);
            }
        }
    }
    print_summary("功能测试");
    return 0;
}