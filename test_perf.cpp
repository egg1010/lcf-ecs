#include "test_common.hpp"

// ============================================================
// 性能测试 - ECS 各接口性能基准 (原 test.cpp 第 15 节)
// ============================================================
int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "========================================================\n"
              << "  lcf-ecs 性能测试\n"
              << "========================================================\n";
    {
        const size_t entity_count = 1000000;
        std::cout << "  实体总数: " << entity_count << "\n";

        ecs::manager ecss;
        timer t;

    print_section(1, "基础类型 (entity / type_id)");
        // ---- 15.20 entity / type_id 性能 ----
        print_perf_sub("15.20 entity / type_id 基础类型");
        {
            const size_t base_count = 1000000;

            // entity 构造
            t.reset();
            entity e_test;
            volatile uint32_t e_idx = 0, e_ver = 0;
            for (size_t i = 0; i < base_count; ++i) {
                e_test = entity(static_cast<uint32_t>(i), static_cast<uint32_t>(i));
                e_idx = e_test.parts_.index_;
                e_ver = e_test.parts_.version_;
            }
            print_perf("entity 构造", base_count, t.elapsed_ms());

            // entity is_valid / operator== / operator!=
            t.reset();
            entity e1(1, 1), e2(1, 1), e3(2, 1);
            volatile bool b1 = false, b2 = false, b3 = false;
            for (size_t i = 0; i < base_count; ++i) {
                b1 = e1.is_valid();
                b2 = (e1 == e2);
                b3 = (e1 != e3);
            }
            print_perf("entity is_valid/==/!=", base_count * 3, t.elapsed_ms());

            // std::hash<entity>
            t.reset();
            std::hash<entity> eh;
            volatile size_t hv = 0;
            for (size_t i = 0; i < base_count; ++i)
                hv = eh(entity(static_cast<uint32_t>(i), 1));
            print_perf("std::hash<entity>", base_count, t.elapsed_ms());

            // type_id::get_type_id
            t.reset();
            volatile int tid = 0;
            for (int i = 0; i < base_count; ++i)
                tid = type_id::get_type_id<Position>();
            print_perf("type_id::get_type_id", base_count, t.elapsed_ms());

            // entity_manager 掩码操作
            t.reset();
            ecs::manager mgr_em;
            mgr_em.append_preallocated_entities(base_count);
            class_pool<entity> ents_em;
            ents_em.increase_capacity(base_count);
            for (size_t i = 0; i < base_count; ++i) ents_em.emplace_back(mgr_em.create_entity());
            mgr_em.add(ents_em[0], Position{1, 0, 0});
            volatile uint64_t mask = 0;
            for (size_t i = 0; i < base_count; ++i)
                mask = mgr_em.get_entity_mask(ents_em[i]);
            print_perf("entity_manager get_mask", base_count, t.elapsed_ms());
        }

    print_section(2, "class_pool 组件池");
        // ---- 15.12 class_pool 性能 ----
        print_perf_sub("15.12 class_pool 容器接口");
        {
            const size_t cp_count = 1000000;

            // emplace_back
            t.reset();
            class_pool<int> cp_em;
            cp_em.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i)
                cp_em.emplace_back(static_cast<int>(i));
            print_perf("class_pool emplace_back", cp_count, t.elapsed_ms());

            // push_back_unchecked
            t.reset();
            class_pool<int> cp_pb;
            cp_pb.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i)
                cp_pb.push_back_unchecked(static_cast<int>(i));
            print_perf("class_pool push_back_unchecked", cp_count, t.elapsed_ms());

            // 范围构造（批量构造，公开接口）
            {
                class_pool<int> src(cp_count, 42);
                t.reset();
                class_pool<int> cp_ab(src.begin(), src.end());
                print_perf("class_pool 范围构造(批量)", cp_count, t.elapsed_ms());
            }

            // resize
            t.reset();
            class_pool<int> cp_rz;
            cp_rz.reserve_exact(cp_count);
            print_perf("class_pool reserve_exact(cap)", cp_count, t.elapsed_ms());

            // resize with value
            t.reset();
            class_pool<int> cp_rzv;
            cp_rzv.resize(cp_count, 77);
            print_perf("class_pool resize(cap,val)", cp_count, t.elapsed_ms());

            // increase_capacity(cap, value)
            t.reset();
            class_pool<int> cp_ic;
            cp_ic.emplace_back(1);
            cp_ic.increase_capacity(cp_count, 99);
            print_perf("class_pool increase_capacity(cap,val)", cp_count, t.elapsed_ms());

            // emplace_at (sparse)
            t.reset();
            class_pool<int> cp_ea;
            cp_ea.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i)
                cp_ea.emplace_at(i, static_cast<int>(i));
            print_perf("class_pool emplace_at", cp_count, t.elapsed_ms());

            // sparse_emplace_at
            t.reset();
            class_pool<int> cp_sea;
            cp_sea.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i)
                cp_sea.sparse_emplace_at(i, static_cast<int>(i));
            print_perf("class_pool sparse_emplace_at", cp_count, t.elapsed_ms());

            // sparse_erase_at
            t.reset();
            for (size_t i = 0; i < cp_count; i += 2)
                cp_sea.sparse_erase_at(i);
            print_perf("class_pool sparse_erase_at (隔位)", cp_count / 2, t.elapsed_ms());

            // erase
            t.reset();
            class_pool<int> cp_er = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            for (int iter = 0; iter < 100000; ++iter) {
                class_pool<int> tmp = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
                tmp.erase(std::next(tmp.begin(), 5));
            }
            print_perf("class_pool erase", 100000, t.elapsed_ms());

            // emplace (insert)
            t.reset();
            for (int iter = 0; iter < 100000; ++iter) {
                class_pool<int> tmp = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
                tmp.emplace(std::next(tmp.begin(), 5), 99);
            }
            print_perf("class_pool emplace(insert)", 100000, t.elapsed_ms());

            // pop_back
            t.reset();
            class_pool<int> cp_pop;
            cp_pop.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_pop.emplace_back(static_cast<int>(i));
            for (size_t i = 0; i < cp_count; ++i) cp_pop.pop_back();
            print_perf("class_pool pop_back", cp_count, t.elapsed_ms());

            // swap
            t.reset();
            class_pool<int> cp_s1 = {1, 2, 3}, cp_s2 = {4, 5, 6, 7, 8};
            for (int iter = 0; iter < 1000000; ++iter) {
                cp_s1.swap(cp_s2);
            }
            print_perf("class_pool swap", 1000000, t.elapsed_ms());

            // shrink_to_fit
            t.reset();
            class_pool<int> cp_sf;
            cp_sf.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count / 2; ++i) cp_sf.emplace_back(static_cast<int>(i));
            cp_sf.shrink_to_fit();
            print_perf("class_pool shrink_to_fit", cp_count / 2, t.elapsed_ms());

            // reduce_capacity
            t.reset();
            class_pool<int> cp_rc;
            cp_rc.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_rc.emplace_back(static_cast<int>(i));
            cp_rc.reduce_capacity(cp_count / 2);
            print_perf("class_pool reduce_capacity", cp_count, t.elapsed_ms());

            // reduce_capacity(dst)
            t.reset();
            class_pool<int> cp_src2;
            cp_src2.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_src2.emplace_back(static_cast<int>(i));
            class_pool<int> cp_dst2;
            cp_src2.reduce_capacity(cp_count / 2, cp_dst2);
            print_perf("class_pool reduce_capacity(dst)", cp_count / 2, t.elapsed_ms());

            // clear
            t.reset();
            class_pool<int> cp_cl;
            cp_cl.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_cl.emplace_back(static_cast<int>(i));
            cp_cl.clear();
            print_perf("class_pool clear", cp_count, t.elapsed_ms());

            // 遍历 (dense)
            {
                class_pool<int> cp_tr;
                cp_tr.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_tr.emplace_back(static_cast<int>(i));
                volatile long long sum = 0;
                t.reset();
                for (auto it = cp_tr.begin(); it != cp_tr.end(); ++it) sum += *it;
                print_perf("class_pool 遍历 begin/end (dense)", cp_count, t.elapsed_ms());
            }

            // 遍历 (sparse 50%)
            {
                class_pool<int> cp_sp;
                cp_sp.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_sp.emplace_back(static_cast<int>(i));
                for (size_t i = 0; i < cp_count; i += 2) cp_sp.sparse_erase_at(i);
                volatile long long sum = 0;
                t.reset();
                for (auto it = cp_sp.begin(); it != cp_sp.end(); ++it) sum += *it;
                print_perf("class_pool 遍历 begin/end (sparse 50%)", cp_count / 2, t.elapsed_ms());
            }

            // for_each (dense, 单次) - 原始基线, sum += v 读改写
            {
                class_pool<int> cp_fe;
                cp_fe.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_fe.emplace_back(static_cast<int>(i));
                volatile long long sum = 0;
                t.reset();
                cp_fe.for_each([&sum](int& v) { sum += v; });
                print_perf("class_pool for_each (dense)", cp_count, t.elapsed_ms());
            }

            // for_each (dense, 10x) - 与 cbegin/cend 对齐 (sum = v, cache-hot)
            {
                class_pool<int> cp_fe10;
                cp_fe10.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_fe10.emplace_back(static_cast<int>(i));
                volatile int sum = 0;
                t.reset();
                for (int iter = 0; iter < 10; ++iter)
                {
                    cp_fe10.for_each([&sum](int& v) { sum = v; });
                }
                print_perf("class_pool for_each (dense, 10x)", cp_count * 10, t.elapsed_ms());
            }

            // for_each (sparse 50%)
            {
                class_pool<int> cp_fe2;
                cp_fe2.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_fe2.emplace_back(static_cast<int>(i));
                for (size_t i = 0; i < cp_count; i += 2) cp_fe2.sparse_erase_at(i);
                volatile long long sum = 0;
                t.reset();
                cp_fe2.for_each([&sum](int& v) { sum += v; });
                print_perf("class_pool for_each (sparse 50%)", cp_count / 2, t.elapsed_ms());
            }

            // count (sparse)
            t.reset();
            class_pool<int> cp_cnt;
            cp_cnt.increase_capacity(cp_count);
            for (size_t i = 0; i < cp_count; ++i) cp_cnt.emplace_back(static_cast<int>(i));
            for (size_t i = 0; i < cp_count; i += 2) cp_cnt.sparse_erase_at(i);
            size_t cnt_cp = cp_cnt.count();
            print_perf("class_pool count (sparse)", cnt_cp, t.elapsed_ms());

            // is_constructed_at
            t.reset();
            volatile bool bv = false;
            for (size_t i = 0; i < cp_count; ++i) bv = cp_cnt.is_constructed_at(i);
            print_perf("class_pool is_constructed_at", cp_count, t.elapsed_ms());
        }

        // ---- 15.21 class_pool 补充接口性能 ----
        print_perf_sub("15.21 class_pool 补充接口");
        {
            const size_t cp_count = 1000000;

            // fill_the_hole (无洞 fast path = emplace_back)
            {
                class_pool<int> cp_fh;
                cp_fh.increase_capacity(cp_count);
                t.reset();
                for (size_t i = 0; i < cp_count; ++i)
                    cp_fh.fill_the_hole(static_cast<int>(i));
                print_perf("class_pool fill_the_hole (无洞)", cp_count, t.elapsed_ms());
            }

            // fill_the_hole (有洞 slow path)
            {
                class_pool<int> cp_fh2;
                cp_fh2.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_fh2.fill_the_hole(static_cast<int>(i));
                // 产生空洞
                for (size_t i = 0; i < cp_count; i += 2)
                    cp_fh2.sparse_erase_at(static_cast<size_t>(i));
                t.reset();
                for (size_t i = 0; i < cp_count / 2; ++i)
                    cp_fh2.fill_the_hole(static_cast<int>(i));
                print_perf("class_pool fill_the_hole (有洞)", cp_count / 2, t.elapsed_ms());
            }

            // emplace_back_unchecked
            {
                class_pool<int> cp_ub;
                cp_ub.emplace_back(0);
                cp_ub.increase_capacity(cp_count + 1);
                t.reset();
                for (size_t i = 0; i < cp_count; ++i)
                    cp_ub.emplace_back_unchecked(static_cast<int>(i));
                print_perf("class_pool emplace_back_unchecked", cp_count, t.elapsed_ms());
            }

            // emplace_back_dense_unchecked
            {
                class_pool<int> cp_db;
                cp_db.emplace_back(0);
                cp_db.increase_capacity(cp_count + 1);
                t.reset();
                for (size_t i = 0; i < cp_count; ++i)
                    cp_db.emplace_back_dense_unchecked(static_cast<int>(i));
                print_perf("class_pool emplace_back_dense_unchecked", cp_count, t.elapsed_ms());
            }

            // append_n (方案 J: AVX2 批量追加)
            {
                class_pool<int> cp_an;
                cp_an.increase_capacity(cp_count + 1);
                t.reset();
                cp_an.append_n(cp_count, 42);
                print_perf("class_pool append_n", cp_count, t.elapsed_ms());

                // 验证正确性
                bool ok = (cp_an.size() == cp_count);
                for (size_t i = 0; i < cp_count && ok; ++i)
                    ok = (cp_an[i] == 42);
                print_item("append_n 正确性", ok);
                print_item("append_n is_dense", cp_an.is_dense());
                print_item("append_n count", cp_an.count() == cp_count);
            }

            // at / operator[] / front / back / data / span / get
            {
                class_pool<int> cp_acc;
                cp_acc.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_acc.emplace_back(static_cast<int>(i));

                t.reset();
                volatile int v_at = 0;
                for (size_t i = 0; i < cp_count; ++i) v_at = cp_acc.at(i);
                print_perf("class_pool at()", cp_count, t.elapsed_ms());

                t.reset();
                volatile int v_idx = 0;
                for (size_t i = 0; i < cp_count; ++i) v_idx = cp_acc[i];
                print_perf("class_pool operator[]", cp_count, t.elapsed_ms());

                t.reset();
                volatile int v_fb = 0;
                for (size_t i = 0; i < cp_count; ++i) { v_fb = cp_acc.front(); v_fb = cp_acc.back(); }
                print_perf("class_pool front/back", cp_count * 2, t.elapsed_ms());

                t.reset();
                volatile int* vp = nullptr;
                for (size_t i = 0; i < cp_count; ++i) vp = cp_acc.get(i);
                print_perf("class_pool get(i)", cp_count, t.elapsed_ms());

                t.reset();
                volatile int* vd = nullptr;
                for (int i = 0; i < 1000000; ++i) vd = cp_acc.data();
                print_perf("class_pool data()", 1000000, t.elapsed_ms());

                t.reset();
                std::span<int> sp;
                for (int i = 0; i < 1000000; ++i) sp = cp_acc.span();
                volatile size_t sp_sink = sp.size(); (void)sp_sink;
                print_perf("class_pool span()", 1000000, t.elapsed_ms());
            }

            // erase(first, last) 范围删除
            {
                class_pool<int> cp_er;
                cp_er.increase_capacity(cp_count + 10);
                for (size_t i = 0; i < cp_count; ++i) cp_er.emplace_back(static_cast<int>(i));
                t.reset();
                size_t erased = 0;
                while (cp_er.size() >= 100) {
                    auto first = cp_er.begin();
                    auto last = first;
                    for (size_t j = 0; j < 50; ++j) ++last;
                    cp_er.erase(first, last);
                    erased += 50;
                }
                print_perf("class_pool erase(first,last)", erased, t.elapsed_ms());
            }

            // is_dense / invalidate_count_cache / 容量查询
            {
                class_pool<int> cp_q;
                cp_q.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_q.emplace_back(static_cast<int>(i));

                t.reset();
                volatile bool d = false;
                for (int i = 0; i < 1000000; ++i) d = cp_q.is_dense();
                print_perf("class_pool is_dense", 1000000, t.elapsed_ms());

                t.reset();
                for (int i = 0; i < 1000000; ++i) cp_q.invalidate_count_cache();
                print_perf("class_pool invalidate_count_cache", 1000000, t.elapsed_ms());

                t.reset();
                volatile size_t sq = 0;
                volatile bool eq = false;
                for (int i = 0; i < 1000000; ++i) {
                    sq = cp_q.size();
                    sq = cp_q.capacity();
                    sq = cp_q.sparse_capacity();
                    sq = cp_q.size_bytes();
                    sq = cp_q.capacity_bytes();
                    eq = cp_q.empty();
                }
                print_perf("class_pool 容量查询", 1000000 * 6, t.elapsed_ms());
            }

            // cbegin/cend
            {
                class_pool<int> cp_c;
                cp_c.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_c.emplace_back(static_cast<int>(i));
                t.reset();
                volatile int sum = 0;
                for (int iter = 0; iter < 10; ++iter) {
                    for (auto it = cp_c.cbegin(); it != cp_c.cend(); ++it) sum = *it;
                }
                print_perf("class_pool cbegin/cend 遍历", cp_count * 10, t.elapsed_ms());
            }

            // 拷贝/移动构造赋值
            {
                class_pool<int> cp_orig;
                cp_orig.increase_capacity(cp_count);
                for (size_t i = 0; i < cp_count; ++i) cp_orig.emplace_back(static_cast<int>(i));

                t.reset();
                class_pool<int> cp_copy(cp_orig);
                print_perf("class_pool 拷贝构造", 1, t.elapsed_ms());

                t.reset();
                class_pool<int> cp_assign;
                cp_assign = cp_orig;
                print_perf("class_pool 拷贝赋值", 1, t.elapsed_ms());

                t.reset();
                class_pool<int> cp_move(std::move(cp_assign));
                print_perf("class_pool 移动构造", 1, t.elapsed_ms());

                t.reset();
                class_pool<int> cp_ma;
                cp_ma = std::move(cp_copy);
                print_perf("class_pool 移动赋值", 1, t.elapsed_ms());
            }
        }

    print_section(3, "void_any 类型擦除");
        // ---- 15.13 void_any 性能 ----
        print_perf_sub("15.13 void_any 类型擦除容器");
        {
            const size_t va_count = 1000000;

            // 构造
            t.reset();
            class_pool<void_any> va_pool;
            va_pool.increase_capacity(va_count);
            for (size_t i = 0; i < va_count; ++i)
                va_pool.emplace_back(static_cast<int>(i));
            print_perf("void_any 构造(T&&)", va_count, t.elapsed_ms());

            // set
            t.reset();
            for (size_t i = 0; i < va_count; ++i)
                va_pool[i].set(static_cast<double>(i));
            print_perf("void_any set()", va_count, t.elapsed_ms());

            // has_value
            t.reset();
            size_t hv = 0;
            for (size_t i = 0; i < va_count; ++i)
                if (va_pool[i].has_value()) ++hv;
            volatile size_t hv_sink = hv;
            (void)hv_sink;
            print_perf("void_any has_value()", va_count, t.elapsed_ms());

            // type_id
            t.reset();
            volatile int tid = 0;
            for (size_t i = 0; i < va_count; ++i)
                tid = va_pool[i].type_id();
            print_perf("void_any type_id()", va_count, t.elapsed_ms());

            // get_ptr
            t.reset();
            volatile double* dp = nullptr;
            for (size_t i = 0; i < va_count; ++i)
                dp = va_pool[i].get_ptr<double>();
            print_perf("void_any get_ptr<T>()", va_count, t.elapsed_ms());

            // fast_get_ptr
            t.reset();
            for (size_t i = 0; i < va_count; ++i)
                dp = va_pool[i].fast_get_ptr<double>();
            print_perf("void_any fast_get_ptr<T>()", va_count, t.elapsed_ms());

            // get_ptr_unchecked
            t.reset();
            for (size_t i = 0; i < va_count; ++i)
                dp = va_pool[i].get_ptr_unchecked<double>();
            print_perf("void_any get_ptr_unchecked<T>()", va_count, t.elapsed_ms());

            // get
            t.reset();
            for (size_t i = 0; i < va_count; ++i) {
                double v = va_pool[i].get<double>();
                (void)v;
            }
            print_perf("void_any get<T>()", va_count, t.elapsed_ms());

            // reset
            t.reset();
            for (size_t i = 0; i < va_count; ++i)
                va_pool[i].reset();
            print_perf("void_any reset()", va_count, t.elapsed_ms());

            // 拷贝构造
            t.reset();
            void_any va_src(42);
            for (size_t i = 0; i < va_count; ++i) {
                void_any va_copy_obj(va_src);
            }
            print_perf("void_any 拷贝构造", va_count, t.elapsed_ms());

            // 移动构造
            t.reset();
            for (size_t i = 0; i < va_count; ++i) {
                void_any va_tmp(42);
                void_any va_move(std::move(va_tmp));
            }
            print_perf("void_any 移动构造", va_count, t.elapsed_ms());
        }

    print_section(4, "memory_pool 内存池");
        // ---- 15.14 memory_pool 性能 ----
        print_perf_sub("15.14 memory_pool 内存池");
        {
            const size_t mp_count = 1000000;

            // allocate/deallocate
            t.reset();
            memory_pool mp(1024 * 1024);
            class_pool<void*> ptrs;
            ptrs.increase_capacity(mp_count);
            for (size_t i = 0; i < mp_count; ++i)
                ptrs.emplace_back(mp.allocate(64));
            print_perf("memory_pool allocate(64)", mp_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < mp_count; ++i)
                mp.deallocate(ptrs[i]);
            print_perf("memory_pool deallocate", mp_count, t.elapsed_ms());

            // construct/destroy
            t.reset();
            class_pool<int*> iptrs;
            iptrs.increase_capacity(mp_count);
            for (size_t i = 0; i < mp_count; ++i)
                iptrs.emplace_back(mp.construct<int>(static_cast<int>(i)));
            print_perf("memory_pool construct<int>", mp_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < mp_count; ++i)
                mp.destroy(iptrs[i]);
            print_perf("memory_pool destroy<int>", mp_count, t.elapsed_ms());

            // increase_capacity
            t.reset();
            memory_pool mp2(4096);
            mp2.increase_capacity(8 * 1024 * 1024);
            print_perf("memory_pool increase_capacity", 1, t.elapsed_ms());

            // reduce_capacity
            t.reset();
            mp2.reduce_capacity(0);
            print_perf("memory_pool reduce_capacity", 1, t.elapsed_ms());

            // reset
            t.reset();
            memory_pool mp3(4096);
            for (size_t i = 0; i < 10000; ++i) { void* p = mp3.allocate(64); (void)p; }
            mp3.reset();
            print_perf("memory_pool reset", 10000, t.elapsed_ms());

            // total_allocated / total_used / empty / chunk_size
            t.reset();
            volatile size_t ta = 0, tu = 0;
            volatile bool em = false;
            volatile size_t cs = 0;
            for (int i = 0; i < 1000000; ++i) {
                ta = mp3.total_allocated();
                tu = mp3.total_used();
                em = mp3.empty();
                cs = mp3.chunk_size();
            }
            print_perf("memory_pool 状态查询", 1000000, t.elapsed_ms());
        }

    print_section(5, "分配器 (arena / slab / layered)");
        // ---- 15.22 分配器补充接口性能 ----
        print_perf_sub("15.22 分配器补充 (memory_pool + arena + slab + layered)");
        {
            const size_t alloc_count = 1000000;

            // memory_pool 补充: owns / stats / iterate_free / empty / chunk_size
            {
                memory_pool mp;
                void* ptrs[10000];
                for (int i = 0; i < 10000; ++i) ptrs[i] = mp.allocate(64);

                t.reset();
                volatile bool own = false;
                for (int i = 0; i < 1000000; ++i) own = mp.owns(ptrs[i % 10000]);
                print_perf("memory_pool owns", 1000000, t.elapsed_ms());

                t.reset();
                pool_stats st;
                for (int i = 0; i < 1000000; ++i) st = mp.stats();
                volatile size_t st_sink = st.total_used; (void)st_sink;
                print_perf("memory_pool stats", 1000000, t.elapsed_ms());

                t.reset();
                volatile size_t cs = 0;
                volatile bool ey = false;
                volatile size_t ta = 0, tu = 0;
                for (int i = 0; i < 1000000; ++i) {
                    cs = mp.chunk_size();
                    ey = mp.empty();
                    ta = mp.total_allocated();
                    tu = mp.total_used();
                }
                print_perf("memory_pool 状态查询", 1000000 * 4, t.elapsed_ms());

                t.reset();
                size_t fc = 0;
                for (int i = 0; i < 100000; ++i) {
                    fc = 0;
                    mp.iterate_free([&](void*, size_t) { ++fc; });
                }
                print_perf("memory_pool iterate_free", 100000, t.elapsed_ms());

                for (int i = 0; i < 10000; ++i) mp.deallocate(ptrs[i]);
            }

            // arena_allocator: 自有模式
            {
                arena_allocator ar(16 * 1024 * 1024);
                t.reset();
                volatile void* p = nullptr;
                for (size_t i = 0; i < alloc_count; ++i) {
                    p = ar.allocate(16);
                    if (!p) { ar.reset(); p = ar.allocate(16); }
                }
                print_perf("arena_allocator allocate(16) 自有", alloc_count, t.elapsed_ms());

                t.reset();
                for (int i = 0; i < 1000000; ++i) ar.reset();
                print_perf("arena_allocator reset", 1000000, t.elapsed_ms());

                t.reset();
                volatile size_t au = 0, ac = 0, ar2 = 0;
                volatile bool ae = false;
                for (int i = 0; i < 1000000; ++i) {
                    au = ar.used(); ac = ar.capacity();
                    ar2 = ar.remaining(); ae = ar.empty();
                }
                print_perf("arena_allocator 状态查询", 1000000 * 4, t.elapsed_ms());

                void* q = ar.allocate(32);
                t.reset();
                volatile bool ao = false;
                for (int i = 0; i < 1000000; ++i) ao = ar.owns(q);
                print_perf("arena_allocator owns", 1000000, t.elapsed_ms());
            }

            // arena_allocator: 借用模式
            {
                static uint8_t buf[4 * 1024 * 1024];
                arena_allocator ar(buf, sizeof(buf));
                t.reset();
                volatile void* p = nullptr;
                for (size_t i = 0; i < alloc_count; ++i) {
                    p = ar.allocate(16);
                    if (!p) { ar.reset(); p = ar.allocate(16); }
                }
                print_perf("arena_allocator allocate(16) 借用", alloc_count, t.elapsed_ms());
            }

            // slab_allocator
            {
                slab_allocator sl(64);
                t.reset();
                class_pool<void*> ptrs;
                ptrs.increase_capacity(alloc_count);
                for (size_t i = 0; i < alloc_count; ++i) {
                    void* p = sl.allocate();
                    if (!p) {
                        for (void* q : ptrs) sl.deallocate(q);
                        ptrs.clear();
                        p = sl.allocate();
                    }
                    ptrs.emplace_back(p);
                }
                print_perf("slab_allocator allocate(64)", alloc_count, t.elapsed_ms());

                t.reset();
                for (void* p : ptrs) sl.deallocate(p);
                print_perf("slab_allocator deallocate", alloc_count, t.elapsed_ms());

                void* test_p = sl.allocate();
                t.reset();
                volatile bool so = false;
                for (int i = 0; i < 1000000; ++i) so = sl.owns(test_p);
                print_perf("slab_allocator owns", 1000000, t.elapsed_ms());

                t.reset();
                volatile size_t sb = 0, st2 = 0, sf = 0;
                volatile bool se = false;
                for (int i = 0; i < 1000000; ++i) {
                    sb = sl.block_size(); st2 = sl.total_blocks();
                    sf = sl.free_blocks(); se = sl.empty();
                }
                print_perf("slab_allocator 状态查询", 1000000 * 4, t.elapsed_ms());
            }

            // layered_allocator
            {
                layered_allocator la;
                t.reset();
                class_pool<void*> small_ptrs, big_ptrs;
                small_ptrs.increase_capacity(alloc_count);
                for (size_t i = 0; i < alloc_count; ++i) {
                    void* p = la.allocate(64);  // slab 路径
                    if (!p) {
                        for (void* q : small_ptrs) la.deallocate(q);
                        small_ptrs.clear();
                        p = la.allocate(64);
                    }
                    small_ptrs.emplace_back(p);
                }
                print_perf("layered_allocator allocate(64) slab", alloc_count, t.elapsed_ms());

                t.reset();
                for (void* p : small_ptrs) la.deallocate(p);
                print_perf("layered_allocator deallocate(slab)", alloc_count, t.elapsed_ms());

                t.reset();
                class_pool<void*> big_ptrs2;
                big_ptrs2.increase_capacity(alloc_count / 10);
                for (size_t i = 0; i < alloc_count / 10; ++i) {
                    void* p = la.allocate(256);  // TLSF 路径
                    if (!p) {
                        for (void* q : big_ptrs2) la.deallocate(q);
                        big_ptrs2.clear();
                        p = la.allocate(256);
                    }
                    big_ptrs2.emplace_back(p);
                }
                print_perf("layered_allocator allocate(256) TLSF", alloc_count / 10, t.elapsed_ms());

                t.reset();
                for (void* p : big_ptrs2) la.deallocate(p);
                print_perf("layered_allocator deallocate(TLSF)", alloc_count / 10, t.elapsed_ms());

                {
                    t.reset();
                    class_pool<int*> iptrs;
                    iptrs.increase_capacity(alloc_count);
                    for (size_t i = 0; i < alloc_count; ++i)
                        iptrs.emplace_back(la.construct<int>(static_cast<int>(i)));
                    print_perf("layered_allocator construct<int>", alloc_count, t.elapsed_ms());

                    t.reset();
                    for (size_t i = 0; i < alloc_count; ++i)
                        la.destroy(iptrs[i]);
                    print_perf("layered_allocator destroy<int>", alloc_count, t.elapsed_ms());
                }

                {
                    t.reset();
                    for (size_t i = 0; i < alloc_count; ++i) {
                        void* p = la.allocate(64);
                        if (p) la.deallocate(p, 64);
                    }
                    print_perf("layered_allocator deallocate(slab, n) size-aware", alloc_count, t.elapsed_ms());
                }

                {
                    t.reset();
                    for (size_t i = 0; i < alloc_count / 10; ++i) {
                        void* p = la.allocate(256);
                        if (p) la.deallocate(p, 256);
                    }
                    print_perf("layered_allocator deallocate(TLSF, n) size-aware", alloc_count / 10, t.elapsed_ms());
                }

                void* test_p = la.allocate(48);
                t.reset();
                volatile bool lo = false;
                for (int i = 0; i < 1000000; ++i) lo = la.owns(test_p);
                print_perf("layered_allocator owns", 1000000, t.elapsed_ms());

                t.reset();
                volatile size_t ls = 0;
                for (int i = 0; i < 1000000; ++i) ls = la.slab_max();
                print_perf("layered_allocator slab_max", 1000000, t.elapsed_ms());

                la.deallocate(test_p);
            }
        }

    print_section(6, "operating_message 操作消息");
        // ---- 15.15 operating_message 性能 ----
        print_perf_sub("15.15 operating_message 操作消息");
        {
            const size_t om_count = 1000000;
            volatile size_t sink = 0;

            // write_message (to_chars 整型路径)
            t.reset();
            operating_message om1;
            for (size_t i = 0; i < om_count; ++i) {
                om1.reset();
                om1.write_message(true, "msg", i);
            }
            sink += om1.message_size();
            print_perf("operating_message write_message (to_chars)", om_count, t.elapsed_ms());

            // write_message_fmt
            t.reset();
            operating_message om2;
            for (size_t i = 0; i < om_count; ++i) {
                om2.reset();
                om2.write_message_fmt(true, "fmt: {} + {}", i, i + 1);
            }
            sink += om2.message_size();
            print_perf("operating_message write_message_fmt", om_count, t.elapsed_ms());

            // write_message with reserve (无重分配)
            t.reset();
            operating_message om_rsv;
            om_rsv.reserve(4096);
            for (size_t i = 0; i < om_count; ++i) {
                om_rsv.reset();
                om_rsv.write_message(true, "msg", i);
            }
            sink += om_rsv.message_size();
            print_perf("operating_message write_message (reserve)", om_count, t.elapsed_ms());

            // write_message_level (带前缀)
            t.reset();
            operating_message om_lv;
            om_lv.set_min_level(msg_level::debug);
            for (size_t i = 0; i < om_count; ++i) {
                om_lv.reset();
                om_lv.write_message_level(msg_level::info, true, "msg", i);
            }
            sink += om_lv.message_size();
            print_perf("operating_message write_message_level", om_count, t.elapsed_ms());

            // write_message_fmt_level (带前缀)
            t.reset();
            operating_message om_lv2;
            om_lv2.set_min_level(msg_level::debug);
            for (size_t i = 0; i < om_count; ++i) {
                om_lv2.reset();
                om_lv2.write_message_fmt_level(msg_level::warn, true, "v={}", i);
            }
            sink += om_lv2.message_size();
            print_perf("operating_message write_message_fmt_level", om_count, t.elapsed_ms());

            // level 过滤快速路径 (全部被过滤)
            t.reset();
            operating_message om_f;
            om_f.set_min_level(msg_level::error);
            for (size_t i = 0; i < om_count; ++i) {
                om_f.reset();
                om_f.write_message_level(msg_level::debug, true, "filtered", i);
            }
            sink += om_f.message_size();
            print_perf("operating_message level过滤快速路径", om_count, t.elapsed_ms());

            // 混合类型 write_message (to_chars 多类型)
            t.reset();
            operating_message om_mix;
            for (size_t i = 0; i < om_count; ++i) {
                om_mix.reset();
                om_mix.write_message(true, "i=", i, " d=", 3.14, " s=", std::string_view("x"));
            }
            sink += om_mix.message_size();
            print_perf("operating_message write_message (混合类型)", om_count, t.elapsed_ms());

            // operator+=(string_view)  修复 DCE
            t.reset();
            for (size_t i = 0; i < om_count; ++i) {
                operating_message om3;
                om3 += "hello";
                om3 += " world";
                sink += om3.message_size();
            }
            print_perf("operating_message operator+=(str)", om_count * 2, t.elapsed_ms());

            // operator+=(operating_message)
            t.reset();
            for (size_t i = 0; i < om_count; ++i) {
                operating_message om4, om5;
                om5.write_message(true, "src");
                om4 += std::move(om5);
                sink += om4.message_size();
            }
            print_perf("operating_message operator+=(om&&)", om_count, t.elapsed_ms());

            // reset / clear_message / set_switch_bool / get_switch_bool
            t.reset();
            operating_message om6;
            for (size_t i = 0; i < om_count; ++i) {
                om6.reset();
                om6.set_switch_bool(false);
                volatile bool b = om6.get_switch_bool();
                om6.clear_message();
                (void)b;
            }
            print_perf("operating_message reset/clear/switch", om_count, t.elapsed_ms());

            // read_message / operator bool
            t.reset();
            om6.reset();
            om6.write_message(true, "test");
            for (size_t i = 0; i < om_count; ++i) {
                volatile bool b = (bool)om6;
                auto sv = om6.read_message();
                (void)sv; (void)b;
            }
            print_perf("operating_message read/bool", om_count, t.elapsed_ms());

            (void)sink;
        }

    print_section(7, "id_allocation ID 分配");
        // ---- 15.16 id_allocation 性能 ----
        print_perf_sub("15.16 id_allocation ID分配器");
        {
            const size_t id_count = 1000000;

            // get_id
            t.reset();
            id_allocation<int> ida;
            volatile int id_sink = 0;
            for (size_t i = 0; i < id_count; ++i)
                id_sink = ida.get_id();
            (void)id_sink;
            print_perf("id_allocation get_id", id_count, t.elapsed_ms());

            // free_id
            t.reset();
            class_pool<int> ids;
            ids.increase_capacity(id_count);
            for (size_t i = 0; i < id_count; ++i) ids.emplace_back(ida.get_id());
            for (size_t i = 0; i < id_count; ++i) ida.free_id(ids[i]);
            print_perf("id_allocation free_id", id_count, t.elapsed_ms());

            // 回收再分配
            t.reset();
            volatile int id_sink2 = 0;
            for (size_t i = 0; i < id_count; ++i)
                id_sink2 = ida.get_id();
            (void)id_sink2;
            print_perf("id_allocation 回收再分配", id_count, t.elapsed_ms());

            // total_number_of_ids / maximum_id
            t.reset();
            volatile size_t tn = 0, mx = 0;
            for (int i = 0; i < 1000000; ++i) {
                tn = ida.total_number_of_ids();
                mx = ida.maximum_id();
            }
            print_perf("id_allocation total/maximum", 1000000, t.elapsed_ms());
        }

    print_section(8, "single_class_set 组件集合");
        // ---- 15.23 single_class_set 补充接口性能 ----
        print_perf_sub("15.23 single_class_set 补充接口");
        {
            const size_t scs_count = 1000000;

            single_class_set scs;
            scs.increase_capacity(scs_count);
            class_pool<entity> ents;
            ents.increase_capacity(scs_count);
            for (size_t i = 0; i < scs_count; ++i) {
                ents.emplace_back(entity(static_cast<uint32_t>(i), 1));
                scs.add(ents[i], Position{static_cast<float>(i), 0, 0});
            }

            // add_batch (span 版本)
            {
                single_class_set scs_b;
                scs_b.increase_capacity(scs_count);
                class_pool<entity> e_arr;
                class_pool<Position> p_arr;
                e_arr.increase_capacity(scs_count / 10);
                p_arr.increase_capacity(scs_count / 10);
                for (size_t i = 0; i < scs_count / 10; ++i) {
                    e_arr.emplace_back(entity(static_cast<uint32_t>(i), 1));
                    p_arr.emplace_back(Position{static_cast<float>(i), 0, 0});
                }
                t.reset();
                scs_b.add_batch(std::span<const entity>(e_arr.data(), e_arr.size()), std::span<const Position>(p_arr.data(), p_arr.size()));
                print_perf("single_class_set add_batch(span)", scs_count / 10, t.elapsed_ms());
            }

            // add_batch (class_pool 左值)
            {
                single_class_set scs_b2;
                scs_b2.increase_capacity(scs_count);
                class_pool<entity> e_pool;
                class_pool<Position> p_pool;
                e_pool.increase_capacity(scs_count / 10);
                p_pool.increase_capacity(scs_count / 10);
                for (size_t i = 0; i < scs_count / 10; ++i) {
                    e_pool.emplace_back(entity(static_cast<uint32_t>(i), 1));
                    p_pool.emplace_back(Position{static_cast<float>(i), 0, 0});
                }
                t.reset();
                scs_b2.add_batch(e_pool, p_pool);
                print_perf("single_class_set add_batch(lvalue)", scs_count / 10, t.elapsed_ms());
            }

            // add_batch (class_pool 右值)
            {
                single_class_set scs_b3;
                scs_b3.increase_capacity(scs_count);
                class_pool<entity> e_pool;
                class_pool<Position> p_pool;
                e_pool.increase_capacity(scs_count / 10);
                p_pool.increase_capacity(scs_count / 10);
                for (size_t i = 0; i < scs_count / 10; ++i) {
                    e_pool.emplace_back(entity(static_cast<uint32_t>(i), 1));
                    p_pool.emplace_back(Position{static_cast<float>(i), 0, 0});
                }
                t.reset();
                scs_b3.add_batch(std::move(e_pool), std::move(p_pool));
                print_perf("single_class_set add_batch(rvalue)", scs_count / 10, t.elapsed_ms());
            }

            // soft_remove
            {
                single_class_set scs_sr;
                scs_sr.increase_capacity(scs_count);
                class_pool<entity> ents_sr;
                ents_sr.increase_capacity(scs_count);
                for (size_t i = 0; i < scs_count; ++i) {
                    ents_sr.emplace_back(entity(static_cast<uint32_t>(i), 1));
                    scs_sr.add(ents_sr[i], Position{0, 0, 0});
                }
                t.reset();
                for (size_t i = 0; i < scs_count; ++i)
                    scs_sr.soft_remove(ents_sr[i]);
                print_perf("single_class_set soft_remove", scs_count, t.elapsed_ms());
            }

            // get_version / get_version_unchecked
            t.reset();
            volatile uint32_t gv = 0;
            for (size_t i = 0; i < scs_count; ++i)
                gv = scs.get_version(static_cast<uint32_t>(i));
            print_perf("single_class_set get_version", scs_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < scs_count; ++i)
                gv = scs.get_version_unchecked(static_cast<uint32_t>(i));
            print_perf("single_class_set get_version_unchecked", scs_count, t.elapsed_ms());

            // get_dense_at
            t.reset();
            volatile uint32_t gd = 0;
            for (size_t i = 0; i < scs_count; ++i)
                gd = scs.get_dense_at(static_cast<uint32_t>(i));
            print_perf("single_class_set get_dense_at", scs_count, t.elapsed_ms());

            // prefetch_ptr / prefetch_ptr_data
            t.reset();
            for (size_t i = 0; i < scs_count; ++i)
                scs.prefetch_ptr(ents[i]);
            print_perf("single_class_set prefetch_ptr", scs_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < scs_count; ++i)
                scs.prefetch_ptr_data<Position>(ents[i]);
            print_perf("single_class_set prefetch_ptr_data", scs_count, t.elapsed_ms());

            // prefetch_ptr_batch
            t.reset();
            for (int iter = 0; iter < 100; ++iter)
                scs.prefetch_ptr_batch(ents.data(), 64);
            print_perf("single_class_set prefetch_ptr_batch", 100 * 64, t.elapsed_ms());

            // get_ptr_batch
            {
                class_pool<Position*> results;
                results.increase_capacity(64);
                t.reset();
                for (int iter = 0; iter < 10000; ++iter)
                    scs.get_ptr_batch<Position>(ents.data(), results.data(), 64);
                print_perf("single_class_set get_ptr_batch", 10000 * 64, t.elapsed_ms());
            }

            // sparse_dense_at_public / sparse_version_at_public
            t.reset();
            volatile uint32_t sda = 0;
            for (size_t i = 0; i < scs_count; ++i)
                sda = scs.sparse_dense_at_public(static_cast<uint32_t>(i));
            print_perf("single_class_set sparse_dense_at_public", scs_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < scs_count; ++i)
                sda = scs.sparse_version_at_public(static_cast<uint32_t>(i));
            print_perf("single_class_set sparse_version_at_public", scs_count, t.elapsed_ms());

            // get_sparse_size / get_page_directory_capacity / clear_hot_set
            t.reset();
            volatile size_t ss = 0, pc = 0;
            for (int i = 0; i < 1000000; ++i) {
                ss = scs.get_sparse_size();
                pc = scs.get_page_directory_capacity();
            }
            print_perf("single_class_set sparse_size/page_dir_cap", 1000000 * 2, t.elapsed_ms());

            t.reset();
            for (int i = 0; i < 1000000; ++i) scs.clear_hot_set();
            print_perf("single_class_set clear_hot_set", 1000000, t.elapsed_ms());

            // get_typed_pool_ptr / get_entity_indices / get_pool_version
            t.reset();
            volatile class_pool<Position>* tpp = nullptr;
            volatile class_pool<uint32_t>* eip = nullptr;
            volatile uint64_t pv = 0;
            for (int i = 0; i < 1000000; ++i) {
                tpp = scs.get_typed_pool_ptr<Position>();
                eip = &scs.get_entity_indices();
                pv = scs.get_pool_version();
            }
            print_perf("single_class_set 元数据查询", 1000000 * 3, t.elapsed_ms());

            // contains_entity
            t.reset();
            volatile bool ce = false;
            for (size_t i = 0; i < scs_count; ++i)
                ce = scs.contains_entity(ents[i]);
            print_perf("single_class_set contains_entity", scs_count, t.elapsed_ms());

            // increase_capacity
            t.reset();
            scs.increase_capacity(scs_count * 2);
            print_perf("single_class_set increase_capacity", 1, t.elapsed_ms());
        }

    print_section(9, "radix_sort 基数排序");
        // ---- 15.30 radix_sort 性能 ----
        print_perf_sub("15.30 radix_sort 基数排序");
        {
            const size_t rdx_count = 1000000;

            // radix_sort_entries (int key)
            {
                struct entry { int key; size_t index; };
                class_pool<entry> entries;
                entries.increase_capacity(rdx_count);
                std::mt19937 rng(42);
                for (size_t i = 0; i < rdx_count; ++i) {
                    entries.emplace_back(static_cast<int>(rng()), i);
                }
                t.reset();
                radix_sort_entries<int>(entries.data(), rdx_count);
                print_perf("radix_sort_entries<int>", rdx_count, t.elapsed_ms());
            }

            // radix_sort_entries (float key)
            {
                struct entry { float key; size_t index; };
                class_pool<entry> entries;
                entries.increase_capacity(rdx_count);
                std::mt19937 rng(123);
                for (size_t i = 0; i < rdx_count; ++i) {
                    entries.emplace_back(static_cast<float>(rng()), i);
                }
                t.reset();
                radix_sort_entries<float>(entries.data(), rdx_count);
                print_perf("radix_sort_entries<float>", rdx_count, t.elapsed_ms());
            }

            // radix_sort_entries (uint64_t key)
            {
                struct entry { uint64_t key; size_t index; };
                class_pool<entry> entries;
                entries.increase_capacity(rdx_count);
                std::mt19937_64 rng(456);
                for (size_t i = 0; i < rdx_count; ++i) {
                    entries.emplace_back(rng(), i);
                }
                t.reset();
                radix_sort_entries<uint64_t>(entries.data(), rdx_count);
                print_perf("radix_sort_entries<uint64_t>", rdx_count, t.elapsed_ms());
            }

            // radix_sort_indices (int)
            {
                class_pool<size_t> indices, temp;
                class_pool<int> keys;
                indices.increase_capacity(rdx_count);
                temp.increase_capacity(rdx_count);
                keys.increase_capacity(rdx_count);
                std::mt19937 rng(789);
                for (size_t i = 0; i < rdx_count; ++i) {
                    indices.emplace_back(i);
                    keys.emplace_back(static_cast<int>(rng()));
                }
                t.reset();
                radix_sort_indices<int>(indices.data(), keys.data(), rdx_count, temp.data());
                print_perf("radix_sort_indices<int>", rdx_count, t.elapsed_ms());
            }

            // radix_sort_indices (float)
            {
                class_pool<size_t> indices, temp;
                class_pool<float> keys;
                indices.increase_capacity(rdx_count);
                temp.increase_capacity(rdx_count);
                keys.increase_capacity(rdx_count);
                std::mt19937 rng(321);
                for (size_t i = 0; i < rdx_count; ++i) {
                    indices.emplace_back(i);
                    keys.emplace_back(static_cast<float>(rng()));
                }
                t.reset();
                radix_sort_indices<float>(indices.data(), keys.data(), rdx_count, temp.data());
                print_perf("radix_sort_indices<float>", rdx_count, t.elapsed_ms());
            }

            // radix_key
            {
                t.reset();
                volatile unsigned int rk = 0;
                for (size_t i = 0; i < rdx_count; ++i)
                    rk = radix_key(static_cast<int>(i));
                print_perf("radix_key<int>", rdx_count, t.elapsed_ms());
            }
        }

    print_section(10, "manager 核心接口");
        // ---- 15.2 单组件逐个添加 ----
        print_perf_sub("15.2 单组件逐个添加");
        {
            const size_t add_count = 1000000;
            ecs::manager mgr2;
            mgr2.disable_track_changes();
            mgr2.disable_comp_signals();
            mgr2.append_preallocated_entities(add_count);
            class_pool<entity> add_ents;
            add_ents.increase_capacity(add_count);
            for (size_t i = 0; i < add_count; ++i)
                add_ents.emplace_back(mgr2.create_entity());

            t.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Position{1.0f, 2.0f, 3.0f});
            print_perf("Position 逐个添加", add_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Velocity{1.0f, 0.0f, 0.0f});
            print_perf("Velocity 逐个添加", add_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Health{100, 100});
            print_perf("Health 逐个添加", add_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Damage{10});
            print_perf("Damage 逐个添加", add_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Armor{50});
            print_perf("Armor 逐个添加", add_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Speed{5.0f});
            print_perf("Speed 逐个添加", add_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < add_count; ++i)
                mgr2.add(add_ents[i], Name{"Test"});
            print_perf("Name 逐个添加", add_count, t.elapsed_ms());
        }

        // ---- 15.11 实体/组件操作 ----
        print_perf_sub("15.11 实体 / 组件操作");

        {
            const size_t op_count = 1000000;
            ecs::manager mgr3;
            mgr3.append_preallocated_entities(op_count * 2);
            class_pool<entity> op_ents;
            op_ents.increase_capacity(op_count);

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
                op_ents.emplace_back(mgr3.create_entity());
            print_perf("实体创建", op_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
                mgr3.add(op_ents[i], Position{1.0f, 0.0f, 0.0f});
            print_perf("组件添加 add()", op_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
                mgr3.hard_remove<Position>(op_ents[i]);
            print_perf("组件硬删除 hard_remove", op_count, t.elapsed_ms());

            for (size_t i = 0; i < op_count; ++i)
                mgr3.add(op_ents[i], Velocity{1.0f, 0.0f, 0.0f});

            t.reset();
            for (size_t i = 0; i < op_count; ++i)
                mgr3.soft_remove<Velocity>(op_ents[i]);
            print_perf("组件软删除 soft_remove", op_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < op_count / 2; ++i)
                mgr3.delete_entity(op_ents[i]);
            print_perf("实体删除 delete_entity", op_count / 2, t.elapsed_ms());
        }

        // ---- 15.17 信号系统性能 ----
        print_perf_sub("15.17 生命周期信号系统");
        {
            const size_t sig_count = 1000000;

            // 即时信号：实体创建/销毁
            {
                ecs::manager mgr;
                mgr.append_preallocated_entities(sig_count * 2);
                size_t created = 0, destroyed = 0;
                mgr.set_on_entity_created([](entity, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &created);
                mgr.set_on_entity_destroyed([](entity, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &destroyed);

                t.reset();
                class_pool<entity> ents;
                ents.increase_capacity(sig_count);
                for (size_t i = 0; i < sig_count; ++i)
                    ents.emplace_back(mgr.create_entity());
                print_perf("即时信号 entity_created", sig_count, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                    mgr.delete_entity(ents[i]);
                print_perf("即时信号 entity_destroyed", sig_count, t.elapsed_ms());
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

                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                    mgr.add(ents[i], Position{1.0f, 0, 0});
                print_perf("即时信号 on_add<Position>", sig_count, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < sig_count; ++i)
                    mgr.hard_remove<Position>(ents[i]);
                print_perf("即时信号 on_remove<Position>", sig_count, t.elapsed_ms());
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

                t.reset();
                size_t created = 0, destroyed = 0;
                mgr.flush_entity_signals([&](uint32_t type, uint32_t) noexcept {
                    if (type == 0) created++;
                    else destroyed++;
                });
                print_perf("flush_entity_signals", sig_count, t.elapsed_ms());
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

                t.reset();
                size_t added = 0, removed = 0;
                mgr.flush_component_signals([&](uint32_t type, uint32_t, uint32_t) noexcept {
                    if (type == 0) added++;
                    else removed++;
                });
                print_perf("flush_component_signals", sig_count + sig_count / 2, t.elapsed_ms());
            }

            // has_pending_signals 查询
            t.reset();
            ecs::manager mgr_chk;
            volatile bool hp = false;
            for (int i = 0; i < 1000000; ++i) {
                hp = mgr_chk.has_pending_entity_signals();
                hp = mgr_chk.has_pending_component_signals();
            }
            print_perf("has_pending_signals 查询", 1000000 * 2, t.elapsed_ms());

            // enable/disable 信号开关
            t.reset();
            for (int i = 0; i < 1000000; ++i) {
                mgr_chk.disable_comp_signals();
                mgr_chk.enable_comp_signals();
                mgr_chk.disable_track_changes();
                mgr_chk.enable_track_changes();
            }
            print_perf("enable/disable 信号开关", 1000000 * 4, t.elapsed_ms());
        }

        // ---- 15.18 排序/重排接口性能 ----
        print_perf_sub("15.18 排序 / 重排接口");
        {
            constexpr size_t sort_n = 1000000;
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
            t.reset();
            sort_mgr.sort_entities_by_component<Position>(
                [](const Position& a, const Position& b) { return a.x < b.x; });
            print_perf("sort_entities_by_component", sort_n, t.elapsed_ms());

            // reorder_by_component
            t.reset();
            sort_mgr.reorder_by_component<Position, Velocity>(
                [](const Velocity& a, const Velocity& b) { return a.vx < b.vx; });
            print_perf("reorder_by_component", sort_n, t.elapsed_ms());

            // sort_component_container
            t.reset();
            sort_mgr.sort_component_container<Position>(
                [](const Position& a, const Position& b) { return a.x < b.x; });
            print_perf("sort_component_container", sort_n, t.elapsed_ms());

            // single_view sorted_by_component
            t.reset();
            {
                auto sv = sort_mgr.view<Position>();
                auto ssv = sv.sorted_by_component(
                    [](const Position& a, const Position& b) { return a.x < b.x; });
                size_t cnt = 0;
                ssv.for_each([&](Position&) { cnt++; });
                print_perf("single_view sorted_by_component", cnt, t.elapsed_ms());
            }

            // multi_view sorted_by_component
            t.reset();
            {
                auto mv = sort_mgr.view<Position, Velocity>();
                auto msv = mv.sorted_by_component<Position>(
                    [](const Position& a, const Position& b) { return a.x < b.x; });
                size_t cnt = 0;
                msv.for_each([&](Position&, Velocity&) { cnt++; });
                print_perf("multi_view sorted_by_component", cnt, t.elapsed_ms());
            }

            // sorted_by_component_value 分组
            t.reset();
            {
                auto sv = sort_mgr.view<Position>();
                auto gv = sv.sorted_by_component_value(
                    [](Position& p) -> int { return static_cast<int>(p.x) / 1000; });
                size_t groups = 0;
                size_t grouped_cnt = 0;
                gv.for_each_group([&](int, size_t begin, size_t end) {
                    groups++;
                    grouped_cnt += end - begin;
                });
                print_perf("sorted_by_component_value 分组", grouped_cnt, t.elapsed_ms());
            }

            // sort_n / tiered_sort 小数量级性能
            {
                constexpr size_t small_n = 1000000;
                int* v5 = static_cast<int*>(::operator new(small_n * 5 * sizeof(int)));
                int* v10 = static_cast<int*>(::operator new(small_n * 10 * sizeof(int)));
                int* v16 = static_cast<int*>(::operator new(small_n * 16 * sizeof(int)));
                for (size_t i = 0; i < small_n * 5; ++i) v5[i] = rand();
                for (size_t i = 0; i < small_n * 10; ++i) v10[i] = rand();
                for (size_t i = 0; i < small_n * 16; ++i) v16[i] = rand();

                t.reset();
                for (size_t i = 0; i < small_n; ++i)
                    ::sort_n<5>(&v5[i * 5]);
                print_perf("sort_n<5> 排序网络", small_n * 5, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < small_n; ++i)
                    tiered_sort(&v5[i * 5], 5, std::less<int>{});
                print_perf("tiered_sort(n=5) 排序网络", small_n * 5, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < small_n; ++i)
                    ::sort_n<10>(&v10[i * 10]);
                print_perf("sort_n<10> 排序网络", small_n * 10, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < small_n; ++i)
                    ::sort_n<16>(&v16[i * 16]);
                print_perf("sort_n<16> 排序网络", small_n * 16, t.elapsed_ms());

                ::operator delete(v5);
                ::operator delete(v10);
                ::operator delete(v16);
            }
        }

        // ---- 15.19 其他管理器接口性能 ----
        print_perf_sub("15.19 其他管理器接口");
        {
            const size_t misc_count = 1000000;

            // is_entity_valid
            t.reset();
            ecs::manager mgr_m;
            mgr_m.append_preallocated_entities(misc_count);
            class_pool<entity> ents_m;
            ents_m.increase_capacity(misc_count);
            for (size_t i = 0; i < misc_count; ++i) ents_m.emplace_back(mgr_m.create_entity());
            volatile bool ev = false;
            for (size_t i = 0; i < misc_count; ++i) ev = mgr_m.is_entity_valid(ents_m[i]);
            print_perf("is_entity_valid", misc_count, t.elapsed_ms());

            // get_entity_mask
            t.reset();
            volatile uint64_t em = 0;
            for (size_t i = 0; i < misc_count; ++i) em = mgr_m.get_entity_mask(ents_m[i]);
            print_perf("get_entity_mask", misc_count, t.elapsed_ms());

            // get_component_bit
            t.reset();
            volatile uint64_t cb = 0;
            for (int i = 0; i < 1000000; ++i) cb = mgr_m.get_component_bit<Position>();
            print_perf("get_component_bit", 1000000, t.elapsed_ms());

            // get_component_meta
            t.reset();
            volatile const ecs::component_meta* cm = nullptr;
            int pid = type_id::get_type_id<Position>();
            for (int i = 0; i < 1000000; ++i) cm = mgr_m.get_component_meta(pid);
            print_perf("get_component_meta", 1000000, t.elapsed_ms());

            // get_single_class_set
            t.reset();
            volatile single_class_set* scs = nullptr;
            for (int i = 0; i < 1000000; ++i) scs = mgr_m.get_single_class_set<Position>();
            print_perf("get_single_class_set", 1000000, t.elapsed_ms());

            // get_single_class_set_by_id
            t.reset();
            for (int i = 0; i < 1000000; ++i) scs = mgr_m.get_single_class_set_by_id(pid);
            print_perf("get_single_class_set_by_id", 1000000, t.elapsed_ms());

            // get_component_container
            t.reset();
            volatile class_pool<Position>* cv = nullptr;
            for (int i = 0; i < 1000000; ++i) cv = mgr_m.get_component_container<Position>();
            print_perf("get_component_container", 1000000, t.elapsed_ms());

            // get_entity_manager
            t.reset();
            for (int i = 0; i < 1000000; ++i) {
                auto& emr = mgr_m.get_entity_manager();
                (void)emr;
            }
            print_perf("get_entity_manager", 1000000, t.elapsed_ms());

            // add 返回 operating_message (RVO 零拷贝)
            t.reset();
            for (int i = 0; i < 1000000; ++i) {
                auto omr = mgr_m.add(ents_m[0], Velocity{1.0f, 0, 0});
                (void)omr;
            }
            print_perf("manager::add 返回 operating_message (RVO)", 1000000, t.elapsed_ms());

            // reserve_component_capacity
            t.reset();
            mgr_m.reserve_component_capacity<Health>(misc_count);
            print_perf("reserve_component_capacity", 1, t.elapsed_ms());

            // delete_type_container
            for (size_t i = 0; i < misc_count; ++i) mgr_m.add(ents_m[i], Health{100, 100});
            t.reset();
            mgr_m.delete_type_container<Health>();
            print_perf("delete_type_container", misc_count, t.elapsed_ms());

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
                t.reset();
                for (size_t i = 0; i < misc_count; ++i)
                    mgr_ch.hard_removec<Position>(ents_ch[i]);
                print_perf("hard_removec (链式)", misc_count, t.elapsed_ms());

                for (size_t i = 0; i < misc_count; ++i) mgr_ch.add(ents_ch[i], Velocity{1.0f, 0, 0});
                t.reset();
                for (size_t i = 0; i < misc_count; ++i)
                    mgr_ch.soft_removec<Velocity>(ents_ch[i]);
                print_perf("soft_removec (链式)", misc_count, t.elapsed_ms());
            }

            // addc (链式)
            ecs::manager mgr_ac;
            mgr_ac.append_preallocated_entities(misc_count);
            class_pool<entity> ents_ac;
            ents_ac.increase_capacity(misc_count);
            for (size_t i = 0; i < misc_count; ++i) ents_ac.emplace_back(mgr_ac.create_entity());
            t.reset();
            for (size_t i = 0; i < misc_count; ++i)
                mgr_ac.addc(ents_ac[i], Position{1.0f, 0, 0});
            print_perf("addc (链式)", misc_count, t.elapsed_ms());

            // add(T, e) 反向参数
            ecs::manager mgr_rev;
            mgr_rev.append_preallocated_entities(misc_count);
            class_pool<entity> ents_rev;
            ents_rev.increase_capacity(misc_count);
            for (size_t i = 0; i < misc_count; ++i) ents_rev.emplace_back(mgr_rev.create_entity());
            t.reset();
            for (size_t i = 0; i < misc_count; ++i)
                mgr_rev.add(Velocity{1.0f, 0, 0}, ents_rev[i]);
            print_perf("add(T, e) 反向参数", misc_count, t.elapsed_ms());

            // prefetch_ptr_batch
            t.reset();
            mgr_m.add(ents_m[0], Position{1, 0, 0});
            for (int iter = 0; iter < 100; ++iter)
                mgr_m.prefetch_ptr_batch<Position>(ents_m.data(), 64);
            print_perf("prefetch_ptr_batch", 100 * 64, t.elapsed_ms());

            // single_class_set 直接接口
            {
                single_class_set scs_d;
                scs_d.increase_capacity(misc_count);
                class_pool<entity> ents_d;
                ents_d.increase_capacity(misc_count);
                for (size_t i = 0; i < misc_count; ++i) ents_d.emplace_back(entity(static_cast<uint32_t>(i), 1));

                t.reset();
                for (size_t i = 0; i < misc_count; ++i)
                    scs_d.add(ents_d[i], Position{1.0f, 0, 0});
                print_perf("single_class_set add", misc_count, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < misc_count; ++i) {
                    auto* p = scs_d.get_ptr<Position>(ents_d[i]);
                    volatile float fx = p ? p->x : 0;
                    (void)fx;
                }
                print_perf("single_class_set get_ptr", misc_count, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < misc_count; ++i) {
                    auto* p = scs_d.get_ptr_fast<Position>(ents_d[i]);
                    volatile float fx = p ? p->x : 0;
                    (void)fx;
                }
                print_perf("single_class_set get_ptr_fast", misc_count, t.elapsed_ms());

                t.reset();
                for (size_t i = 0; i < misc_count; ++i)
                    scs_d.hard_remove(ents_d[i]);
                print_perf("single_class_set hard_remove", misc_count, t.elapsed_ms());

                // add_batch
                for (size_t i = 0; i < misc_count; ++i) scs_d.add(ents_d[i], Position{1.0f, 0, 0});
                t.reset();
                scs_d.clear();
                print_perf("single_class_set clear", misc_count, t.elapsed_ms());

                // size / empty / get_type_id
                t.reset();
                volatile size_t sz = 0;
                volatile bool ey = false;
                volatile int ti = 0;
                for (int i = 0; i < 1000000; ++i) {
                    sz = scs_d.size();
                    ey = scs_d.empty();
                    ti = scs_d.get_type_id();
                }
                print_perf("single_class_set 状态查询", 1000000 * 3, t.elapsed_ms());
            }
        }

        // ---- 15.24 manager 补充接口性能 ----
        print_perf_sub("15.24 manager 补充接口 (on_modify / signal capacity / cached 预取)");
        {
            const size_t mgr_count = 1000000;

            // set_on_modify
            {
                ecs::manager mgr;
                mgr.append_preallocated_entities(mgr_count);
                size_t modify_cnt = 0;
                mgr.set_on_modify<Position>([](entity, void*, void* d) noexcept { (*static_cast<size_t*>(d))++; }, &modify_cnt);

                class_pool<entity> ents;
                ents.increase_capacity(mgr_count);
                for (size_t i = 0; i < mgr_count; ++i) ents.emplace_back(mgr.create_entity());

                t.reset();
                for (size_t i = 0; i < mgr_count; ++i) {
                    mgr.add(ents[i], Position{1.0f, 0, 0});       // 首次 add
                    mgr.add(ents[i], Position{2.0f, 0, 0});       // 覆盖写触发 on_modify
                }
                print_perf("manager set_on_modify 覆盖写", mgr_count, t.elapsed_ms());
            }

            // signal capacity / overflow
            {
                ecs::manager mgr;
                mgr.append_preallocated_entities(100);

                t.reset();
                mgr.reserve_entity_signal_capacity(2048);
                mgr.reserve_comp_signal_capacity(2048);
                print_perf("manager reserve_signal_capacity", 2, t.elapsed_ms());

                // 触发溢出
                for (int i = 0; i < 2000; ++i) { volatile auto e_ = mgr.create_entity(); (void)e_; }

                t.reset();
                volatile uint64_t ov = 0;
                for (int i = 0; i < 1000000; ++i) {
                    ov = mgr.entity_signal_overflow_count();
                    ov = mgr.comp_signal_overflow_count();
                }
                print_perf("manager overflow_count 查询", 1000000 * 2, t.elapsed_ms());

                t.reset();
                for (int i = 0; i < 1000000; ++i) {
                    mgr.reset_entity_signal_overflow_count();
                    mgr.reset_comp_signal_overflow_count();
                }
                print_perf("manager reset_overflow_count", 1000000 * 2, t.elapsed_ms());
            }

            // get_ptr_fast / cached 系列预取接口
            {
                ecs::manager mgr;
                mgr.append_preallocated_entities(mgr_count);
                class_pool<entity> ents;
                ents.increase_capacity(mgr_count);
                for (size_t i = 0; i < mgr_count; ++i) {
                    ents.emplace_back(mgr.create_entity());
                    mgr.add(ents[i], Position{static_cast<float>(i), 0, 0});
                }

                // get_ptr_fast (manager 级别)
                t.reset();
                volatile float fx = 0;
                for (size_t i = 0; i < mgr_count; ++i) {
                    auto* p = mgr.get_ptr_fast<Position>(ents[i]);
                    fx = p ? p->x : 0;
                }
                print_perf("manager get_ptr_fast", mgr_count, t.elapsed_ms());

                // cached 系列
                auto* set = mgr.get_single_class_set<Position>();
                t.reset();
                for (size_t i = 0; i < mgr_count; ++i) {
                    mgr.prefetch_ptr_cached<Position>(set, ents[i]);
                    mgr.prefetch_ptr_data_cached<Position>(set, ents[i]);
                    auto* p = mgr.get_ptr_fast_cached<Position>(set, ents[i]);
                    fx = p ? p->x : 0;
                }
                print_perf("manager cached 双级预取查询", mgr_count, t.elapsed_ms());

                // set_component_page_size_shift / get_component_page_size_shift
                t.reset();
                mgr.set_component_page_size_shift<Position>(12);
                print_perf("manager set_page_size_shift", 1, t.elapsed_ms());

                t.reset();
                volatile size_t pss = 0;
                for (int i = 0; i < 1000000; ++i)
                    pss = mgr.get_component_page_size_shift<Position>();
                print_perf("manager get_page_size_shift", 1000000, t.elapsed_ms());
            }
        }

        // ---- 15.25 View 系统补充接口性能 ----
        print_perf_sub("15.25 View 系统补充接口");
        {
            const size_t view_count = 500000;

            ecs::manager mgr;
            mgr.append_preallocated_entities(view_count);
            class_pool<entity> ents;
            ents.increase_capacity(view_count);
            for (size_t i = 0; i < view_count; ++i) {
                ents.emplace_back(mgr.create_entity());
                mgr.add(ents[i], Position{static_cast<float>(i), 0, 0});
                mgr.add(ents[i], Velocity{1.0f, 0, 0});
                if (i % 2 == 0) mgr.add(ents[i], Health{100, 100});
            }

            // single_view 索引接口
            {
                auto v = mgr.view<Position>();

                t.reset();
                entity fe{};
                for (int i = 0; i < 1000000; ++i) fe = v.get_first_entity();
                volatile uint64_t fe_sink = fe.handle_; (void)fe_sink;
                print_perf("single_view get_first_entity", 1000000, t.elapsed_ms());

                t.reset();
                entity le{};
                for (int i = 0; i < 1000000; ++i) le = v.get_last_entity();
                volatile uint64_t le_sink = le.handle_; (void)le_sink;
                print_perf("single_view get_last_entity", 1000000, t.elapsed_ms());

                t.reset();
                entity ee{};
                for (size_t i = 0; i < view_count; ++i) ee = v.get_entity_at_index(i);
                volatile uint64_t ee_sink = ee.handle_; (void)ee_sink;
                print_perf("single_view get_entity_at_index", view_count, t.elapsed_ms());

                t.reset();
                volatile Position* cp = nullptr;
                for (size_t i = 0; i < view_count; ++i) cp = v.get_component_at_index(i);
                print_perf("single_view get_component_at_index", view_count, t.elapsed_ms());

                t.reset();
                volatile Position* pe = nullptr;
                for (size_t i = 0; i < view_count; ++i) pe = v.get_component_for_entity(ents[i]);
                print_perf("single_view get_component_for_entity", view_count, t.elapsed_ms());

                t.reset();
                volatile bool ct = false;
                for (size_t i = 0; i < view_count; ++i) ct = v.contains(ents[i]);
                print_perf("single_view contains", view_count, t.elapsed_ms());

                t.reset();
                volatile size_t vs = 0;
                volatile bool ve = false;
                for (int i = 0; i < 1000000; ++i) { vs = v.size(); ve = v.empty(); }
                print_perf("single_view size/empty", 1000000 * 2, t.elapsed_ms());

                // component_begin / component_end
                t.reset();
                volatile float sum = 0;
                for (auto it = v.component_begin(); it != v.component_end(); ++it) sum = it->x;
                print_perf("single_view component_begin/end", view_count, t.elapsed_ms());
            }

            // multi_view: include_optional_component
            {
                auto v_opt = mgr.view<Position, Velocity>()
                    .include_optional_component<Health>();
                t.reset();
                volatile size_t cnt = 0;
                v_opt.for_each([&](entity, Position&, Velocity&, Health* h) { if (h) cnt = cnt + 1; });
                print_perf("multi_view include_optional_component", view_count, t.elapsed_ms());
            }

            // multi_view: get_component_for_entity / get_first/last/at
            {
                auto v2 = mgr.view<Position, Velocity>();

                t.reset();
                volatile Position* pp = nullptr;
                for (size_t i = 0; i < view_count; ++i) pp = v2.get_component_for_entity<Position>(ents[i]);
                print_perf("multi_view get_component_for_entity", view_count, t.elapsed_ms());

                t.reset();
                entity fe{};
                for (int i = 0; i < 1000000; ++i) fe = v2.get_first_entity();
                volatile uint64_t fe_sink = fe.handle_; (void)fe_sink;
                print_perf("multi_view get_first_entity", 1000000, t.elapsed_ms());

                t.reset();
                entity le{};
                for (int i = 0; i < 1000000; ++i) le = v2.get_last_entity();
                volatile uint64_t le_sink = le.handle_; (void)le_sink;
                print_perf("multi_view get_last_entity", 1000000, t.elapsed_ms());

                t.reset();
                entity ee{};
                for (size_t i = 0; i < view_count; ++i) ee = v2.get_entity_at_index(i);
                volatile uint64_t ee_sink = ee.handle_; (void)ee_sink;
                print_perf("multi_view get_entity_at_index", view_count, t.elapsed_ms());

                t.reset();
                volatile bool ct = false;
                for (size_t i = 0; i < view_count; ++i) ct = v2.contains(ents[i]);
                print_perf("multi_view contains", view_count, t.elapsed_ms());
            }

            // filter_changed
            {
                auto cv = mgr.view<Position>().filter_changed();
                t.reset();
                volatile size_t cnt = 0;
                cv.for_each([&](Position&) { cnt = cnt + 1; });
                print_perf("single_view filter_changed (首次)", view_count, t.elapsed_ms());

                // 修改部分组件后再次遍历
                for (size_t i = 0; i < view_count; i += 10)
                    mgr.add(ents[i], Position{999.0f, 0, 0});

                t.reset();
                cnt = 0;
                cv.for_each([&](Position&) { cnt = cnt + 1; });
                print_perf("single_view filter_changed (增量)", view_count / 10, t.elapsed_ms());

                cv.reset_tracking();
                t.reset();
                cv.for_each([&](Position&) {});
                print_perf("single_view filter_changed reset", 1, t.elapsed_ms());
            }

            // multi_view filter_changed
            {
                auto mcv = mgr.view<Position, Velocity>().filter_changed<Position>();
                t.reset();
                volatile size_t cnt = 0;
                mcv.for_each([&](Position&, Velocity&) { cnt = cnt + 1; });
                print_perf("multi_view filter_changed", view_count, t.elapsed_ms());
            }

            // filter_added
            {
                ecs::manager mgr_fa;
                mgr_fa.append_preallocated_entities(view_count);
                class_pool<entity> ents_fa;
                ents_fa.increase_capacity(view_count);

                auto av = mgr_fa.view<Position>().filter_added();

                // 先创建视图,再添加组件
                for (size_t i = 0; i < view_count; ++i) {
                    ents_fa.emplace_back(mgr_fa.create_entity());
                    mgr_fa.add(ents_fa[i], Position{static_cast<float>(i), 0, 0});
                }

                t.reset();
                volatile size_t cnt = 0;
                av.for_each([&](Position&) { cnt = cnt + 1; });
                print_perf("single_view filter_added (首次)", view_count, t.elapsed_ms());

                // 添加新组件
                for (size_t i = 0; i < view_count / 10; ++i) {
                    entity e = mgr_fa.create_entity();
                    mgr_fa.add(e, Position{1.0f, 0, 0});
                }

                t.reset();
                cnt = 0;
                av.for_each([&](Position&) { cnt = cnt + 1; });
                print_perf("single_view filter_added (增量)", view_count / 10, t.elapsed_ms());
            }

            // exactly_one
            {
                ecs::manager mgr_eo;
                mgr_eo.append_preallocated_entities(10);
                entity e1 = mgr_eo.create_entity();
                mgr_eo.add(e1, Position{42.0f, 0, 0});
                mgr_eo.add(e1, Velocity{1.0f, 0, 0});

                auto v_eo = mgr_eo.view<Position, Velocity>();
                t.reset();
                volatile float px = 0;
                for (int i = 0; i < 1000000; ++i) {
                    auto [p, v] = v_eo.exactly_one();
                    px = p.x;
                }
                print_perf("multi_view exactly_one", 1000000, t.elapsed_ms());

                auto v_eo2 = mgr_eo.view<Position>();
                t.reset();
                for (int i = 0; i < 1000000; ++i) {
                    auto& p = v_eo2.exactly_one();
                    px = p.x;
                }
                print_perf("single_view exactly_one", 1000000, t.elapsed_ms());
            }

            // find_one
            {
                auto v_fo = mgr.view<Position, Velocity>();
                t.reset();
                volatile Position* pp = nullptr;
                for (size_t i = 0; i < view_count; ++i) {
                    auto [p, v] = v_fo.find_one(ents[i]);
                    pp = p;
                }
                print_perf("multi_view find_one", view_count, t.elapsed_ms());
            }

            // iter_over_entities
            {
                class_pool<entity> targets;
                targets.increase_capacity(view_count / 10);
                for (size_t i = 0; i < view_count; i += 10)
                    targets.emplace_back(ents[i]);

                auto v_ie = mgr.view<Position, Velocity>().iter_over_entities(targets);
                t.reset();
                volatile size_t cnt = 0;
                v_ie.for_each([&](Position&, Velocity&) { cnt = cnt + 1; });
                print_perf("multi_view iter_over_entities", targets.size(), t.elapsed_ms());
            }
        }

        // ---- 15.26 Group 系统补充接口性能 ----
        print_perf_sub("15.26 Group 系统补充接口");
        {
            const size_t grp_count = 500000;

            ecs::manager mgr;
            mgr.append_preallocated_entities(grp_count);
            class_pool<entity> ents;
            ents.increase_capacity(grp_count);
            for (size_t i = 0; i < grp_count; ++i) {
                ents.emplace_back(mgr.create_entity());
                mgr.add(ents[i], Position{static_cast<float>(i), 0, 0});
                mgr.add(ents[i], Velocity{1.0f, 0, 0});
            }

            // Non-Owning Group: rebuild / front / back / get / contains
            {
                auto g = mgr.group<Position, Velocity>();

                t.reset();
                g.rebuild();
                print_perf("group rebuild (Non-Owning)", 1, t.elapsed_ms());

                t.reset();
                entity fe{};
                for (int i = 0; i < 1000000; ++i) fe = g.front();
                volatile uint64_t fe_sink = fe.handle_; (void)fe_sink;
                print_perf("group front (Non-Owning)", 1000000, t.elapsed_ms());

                t.reset();
                entity le{};
                for (int i = 0; i < 1000000; ++i) le = g.back();
                volatile uint64_t le_sink = le.handle_; (void)le_sink;
                print_perf("group back (Non-Owning)", 1000000, t.elapsed_ms());

                t.reset();
                volatile Position* gp = nullptr;
                for (size_t i = 0; i < grp_count; ++i) gp = g.get<Position>(ents[i]);
                print_perf("group get<T> (Non-Owning)", grp_count, t.elapsed_ms());

                t.reset();
                volatile bool ct = false;
                for (size_t i = 0; i < grp_count; ++i) ct = g.contains(ents[i]);
                print_perf("group contains (Non-Owning)", grp_count, t.elapsed_ms());

                t.reset();
                volatile size_t gs = 0;
                volatile bool ge = false;
                for (int i = 0; i < 1000000; ++i) { gs = g.size(); ge = g.empty(); }
                print_perf("group size/empty (Non-Owning)", 1000000 * 2, t.elapsed_ms());
            }

            // Owning Group: rebuild / front / back / get / contains
            {
                auto og = mgr.group<Position, Velocity>(ecs::owned<Position>);

                t.reset();
                og.rebuild();
                print_perf("owning_group rebuild", 1, t.elapsed_ms());

                t.reset();
                entity fe{};
                for (int i = 0; i < 1000000; ++i) fe = og.front();
                volatile uint64_t fe_sink = fe.handle_; (void)fe_sink;
                print_perf("owning_group front", 1000000, t.elapsed_ms());

                t.reset();
                entity le{};
                for (int i = 0; i < 1000000; ++i) le = og.back();
                volatile uint64_t le_sink = le.handle_; (void)le_sink;
                print_perf("owning_group back", 1000000, t.elapsed_ms());

                t.reset();
                volatile Position* gp = nullptr;
                for (size_t i = 0; i < grp_count; ++i) gp = og.get<Position>(ents[i]);
                print_perf("owning_group get<T>", grp_count, t.elapsed_ms());

                t.reset();
                volatile bool ct = false;
                for (size_t i = 0; i < grp_count; ++i) ct = og.contains(ents[i]);
                print_perf("owning_group contains", grp_count, t.elapsed_ms());
            }

            // Reorder Group: rebuild / front / back / get / contains / share_with
            {
                auto rg = mgr.group<Position, Velocity>(ecs::reorder<Position>);

                t.reset();
                rg.rebuild();
                print_perf("reorder_group rebuild", 1, t.elapsed_ms());

                t.reset();
                entity fe{};
                for (int i = 0; i < 1000000; ++i) fe = rg.front();
                volatile uint64_t fe_sink = fe.handle_; (void)fe_sink;
                print_perf("reorder_group front", 1000000, t.elapsed_ms());

                t.reset();
                entity le{};
                for (int i = 0; i < 1000000; ++i) le = rg.back();
                volatile uint64_t le_sink = le.handle_; (void)le_sink;
                print_perf("reorder_group back", 1000000, t.elapsed_ms());

                t.reset();
                volatile Position* gp = nullptr;
                for (size_t i = 0; i < grp_count; ++i) gp = rg.get<Position>(ents[i]);
                print_perf("reorder_group get<T>", grp_count, t.elapsed_ms());

                t.reset();
                volatile bool ct = false;
                for (size_t i = 0; i < grp_count; ++i) ct = rg.contains(ents[i]);
                print_perf("reorder_group contains", grp_count, t.elapsed_ms());

                // share_with
                auto rg2 = mgr.group<Position, Velocity>(ecs::reorder<Position>);
                t.reset();
                rg2.share_with(rg);
                print_perf("reorder_group share_with", 1, t.elapsed_ms());

                t.reset();
                volatile size_t gs = 0;
                for (int i = 0; i < 1000000; ++i) gs = rg2.size();
                print_perf("reorder_group shared size()", 1000000, t.elapsed_ms());
            }
        }

        // ---- 15.27 runtime_view 补充接口性能 ----
        print_perf_sub("15.27 runtime_view 补充接口");
        {
            const size_t rv_count = 500000;

            ecs::manager mgr;
            mgr.append_preallocated_entities(rv_count);
            class_pool<entity> ents;
            ents.increase_capacity(rv_count);
            for (size_t i = 0; i < rv_count; ++i) {
                ents.emplace_back(mgr.create_entity());
                mgr.add(ents[i], Position{static_cast<float>(i), 0, 0});
                mgr.add(ents[i], Velocity{1.0f, 0, 0});
            }

            int pos_id = type_id::get_type_id<Position>();
            int vel_id = type_id::get_type_id<Velocity>();

            auto rv = mgr.runtime_view_create({pos_id, vel_id});

            // for_each_typed
            t.reset();
            volatile size_t cnt = 0;
            rv.for_each_typed<Position, Velocity>([&](entity, Position&, Velocity&) { cnt = cnt + 1; });
            print_perf("runtime_view for_each_typed", rv_count, t.elapsed_ms());

            // for_each_parallel (单线程模拟 2 worker)
            t.reset();
            cnt = 0;
            rv.for_each_parallel(0, 2, [&](entity, size_t) { cnt = cnt + 1; });
            rv.for_each_parallel(1, 2, [&](entity) { cnt = cnt + 1; });
            print_perf("runtime_view for_each_parallel (2 worker)", rv_count, t.elapsed_ms());

            // for_each_paged
            t.reset();
            cnt = 0;
            rv.for_each_paged(0, rv_count / 2, [&](entity) { cnt = cnt + 1; });
            print_perf("runtime_view for_each_paged", rv_count / 2, t.elapsed_ms());

            // for_each_changed
            rv.reset_change_tracking();
            for (size_t i = 0; i < rv_count; i += 100)
                mgr.add(ents[i], Position{999.0f, 0, 0});

            t.reset();
            cnt = 0;
            if (rv.changed()) {
                rv.for_each_changed([&](entity) { cnt = cnt + 1; });
            }
            print_perf("runtime_view for_each_changed", rv_count, t.elapsed_ms());

            t.reset();
            volatile bool ch = false;
            for (int i = 0; i < 1000000; ++i) ch = rv.changed();
            print_perf("runtime_view changed()", 1000000, t.elapsed_ms());

            t.reset();
            for (int i = 0; i < 1000000; ++i) rv.reset_change_tracking();
            print_perf("runtime_view reset_change_tracking", 1000000, t.elapsed_ms());

            // size / empty / contains / get_ptr / get_first_entity
            t.reset();
            volatile size_t rs = 0;
            volatile bool re = false;
            for (int i = 0; i < 1000000; ++i) { rs = rv.size(); re = rv.empty(); }
            print_perf("runtime_view size/empty", 1000000 * 2, t.elapsed_ms());

            t.reset();
            volatile bool ct = false;
            for (size_t i = 0; i < rv_count; ++i) ct = rv.contains(ents[i]);
            print_perf("runtime_view contains", rv_count, t.elapsed_ms());

            t.reset();
            volatile Position* rp = nullptr;
            for (size_t i = 0; i < rv_count; ++i) rp = rv.get_ptr<Position>(ents[i]);
            print_perf("runtime_view get_ptr<T>", rv_count, t.elapsed_ms());

            t.reset();
            entity fe{};
            for (int i = 0; i < 1000000; ++i) fe = rv.get_first_entity();
            volatile uint64_t fe_sink = fe.handle_; (void)fe_sink;
            print_perf("runtime_view get_first_entity", 1000000, t.elapsed_ms());

            // iterator (begin/end)
            t.reset();
            cnt = 0;
            for (int iter = 0; iter < 3; ++iter) {
                for (auto it = rv.begin(); it != rv.end(); ++it) cnt = cnt + 1;
            }
            print_perf("runtime_view begin/end 迭代", rv_count * 3, t.elapsed_ms());

            // rebuild
            t.reset();
            rv.rebuild();
            print_perf("runtime_view rebuild", 1, t.elapsed_ms());

            // runtime_term (OR/OPTIONAL/NOT)
            {
                int hp_id = type_id::get_type_id<Health>();
                class_pool<ecs::runtime_term> terms;
                terms.emplace_back(ecs::runtime_term{pos_id, 0, ecs::access_mode::read_write});  // AND
                terms.emplace_back(ecs::runtime_term{vel_id, 1, ecs::access_mode::read_only});   // OR
                terms.emplace_back(ecs::runtime_term{hp_id, 2, ecs::access_mode::read_only});    // NOT
                terms.emplace_back(ecs::runtime_term{hp_id, 3, ecs::access_mode::read_only});    // OPTIONAL

                t.reset();
                auto rv_term = mgr.runtime_view_create_from_terms(std::move(terms));
                print_perf("runtime_view_create_from_terms", 1, t.elapsed_ms());

                t.reset();
                cnt = 0;
                rv_term.for_each([&](entity) { cnt = cnt + 1; });
                print_perf("runtime_view (term OR/NOT/OPTIONAL)", rv_count, t.elapsed_ms());
            }

            // count
            t.reset();
            volatile size_t rc = 0;
            for (int i = 0; i < 10; ++i) rc = rv.count();
            print_perf("runtime_view count()", 10, t.elapsed_ms());
        }

        // ---- 15.28 command_buffer 性能 ----
        print_perf_sub("15.28 command_buffer 延迟结构变更");
        {
            const size_t cb_count = 500000;

            ecs::manager mgr;
            mgr.append_preallocated_entities(cb_count);
            class_pool<entity> ents;
            ents.increase_capacity(cb_count);
            for (size_t i = 0; i < cb_count; ++i) ents.emplace_back(mgr.create_entity());

            // add_component 录制
            auto cb = mgr.create_command_buffer();
            t.reset();
            for (size_t i = 0; i < cb_count; ++i)
                cb.add_component<Position>(ents[i], Position{static_cast<float>(i), 0, 0});
            print_perf("command_buffer add_component 录制", cb_count, t.elapsed_ms());

            // size / empty
            t.reset();
            volatile size_t cs = 0;
            volatile bool ce = false;
            for (int i = 0; i < 1000000; ++i) { cs = cb.size(); ce = cb.empty(); }
            print_perf("command_buffer size/empty", 1000000 * 2, t.elapsed_ms());

            // flush
            t.reset();
            cb.flush();
            print_perf("command_buffer flush (add)", cb_count, t.elapsed_ms());

            // remove_component + destroy_entity 录制 + flush
            auto cb2 = mgr.create_command_buffer();
            for (size_t i = 0; i < cb_count; ++i)
                cb2.remove_component<Position>(ents[i]);
            for (size_t i = 0; i < cb_count / 2; ++i)
                cb2.destroy_entity(ents[i]);

            t.reset();
            cb2.flush();
            print_perf("command_buffer flush (remove+destroy)", cb_count + cb_count / 2, t.elapsed_ms());

            // clear
            auto cb3 = mgr.create_command_buffer();
            for (size_t i = 0; i < cb_count; ++i)
                cb3.add_component<Velocity>(ents[i], Velocity{1.0f, 0, 0});
            t.reset();
            cb3.clear();
            print_perf("command_buffer clear", cb_count, t.elapsed_ms());
        }

        // ---- 15.29 函数存储 (回调作为组件) 性能 ----
        print_perf_sub("15.29 函数存储 (回调作为组件)");
        {
            const size_t fn_count = 500000;

            struct CallbackComponent {
                std::function<void(int)> callback;
                CallbackComponent(std::function<void(int)> cb) : callback(std::move(cb)) {}
            };

            ecs::manager mgr;
            mgr.append_preallocated_entities(fn_count);
            class_pool<entity> ents;
            ents.increase_capacity(fn_count);
            for (size_t i = 0; i < fn_count; ++i) ents.emplace_back(mgr.create_entity());

            // 添加回调组件
            t.reset();
            for (size_t i = 0; i < fn_count; ++i) {
                mgr.add(ents[i], CallbackComponent([](int x) { (void)x; }));
            }
            print_perf("回调组件 add", fn_count, t.elapsed_ms());

            // 获取并调用
            t.reset();
            for (size_t i = 0; i < fn_count; ++i) {
                auto* cb = mgr.get_ptr<CallbackComponent>(ents[i]);
                if (cb) cb->callback(42);
            }
            print_perf("回调组件 get+调用", fn_count, t.elapsed_ms());

            // 通过 View 批量调用
            t.reset();
            mgr.view<CallbackComponent>().for_each([](entity, CallbackComponent& c) {
                c.callback(0);
            });
            print_perf("回调组件 view for_each 批量调用", fn_count, t.elapsed_ms());
        }

        // ---- 15.1 测试数据准备 ----
        print_perf_sub("15.1 测试数据准备");

        t.reset();
        ecss.append_preallocated_entities(entity_count);
        print_perf("预分配实体", entity_count, t.elapsed_ms());

        t.reset();
        class_pool<entity> entities;
        entities.increase_capacity(entity_count);
        for (size_t i = 0; i < entity_count; ++i)
            entities.emplace_back(ecss.create_entity());
        print_perf("实体创建", entity_count, t.elapsed_ms());

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
        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), entity_count), std::span<const Position>(positions.data(), entity_count));
        print_perf("Position 批量添加", entity_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count), std::span<const Velocity>(velocities.data(), vel_count));
        print_perf("Velocity 批量添加", vel_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), entity_count), std::span<const Health>(healths.data(), entity_count));
        print_perf("Health 批量添加", entity_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), name_count), std::span<const Name>(names.data(), name_count));
        print_perf("Name 批量添加", name_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count), std::span<const Damage>(damages.data(), vel_count));
        print_perf("Damage 批量添加", vel_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), vel_count), std::span<const Armor>(armors.data(), vel_count));
        print_perf("Armor 批量添加", vel_count, t.elapsed_ms());

        t.reset();
        ecss.add_batch(std::span<const entity>(entities.data(), speed_count), std::span<const Speed>(speeds.data(), speed_count));
        print_perf("Speed 批量添加", speed_count, t.elapsed_ms());

        // 数据分布
        std::cout << "\n  ┌─ 数据分布\n";
        std::cout << "  │ Position: " << entity_count << " (100%)\n";
        std::cout << "  │ Velocity: " << vel_count << " (50%)\n";
        std::cout << "  │ Health:   " << entity_count << " (100%)\n";
        std::cout << "  │ Name:     " << name_count << " (10%)\n";
        std::cout << "  │ Damage:   " << vel_count << " (50%)\n";
        std::cout << "  │ Armor:    " << vel_count << " (50%)\n";
        std::cout << "  │ Speed:    " << speed_count << " (25%)\n";

        // ---- 15.3 单组件查询 ----
        print_perf_sub("15.3 单组件查询");
        {
            const size_t query_count = 1000000;
            std::uniform_int_distribution<size_t> idx_dist(0, entity_count - 1);

            t.reset();
            size_t hit = 0;
            for (size_t i = 0; i < query_count; ++i) {
                auto* p = ecss.get_ptr<Position>(entities[idx_dist(gen)]);
                if (p) { hit++; volatile float d = p->x; (void)d; }
            }
            print_perf("get_ptr 单点查询", hit, t.elapsed_ms());

            t.reset();
            size_t batch_hit = 0;
            {
                class_pool<entity> batch_ents;
                batch_ents.reserve_exact(query_count);
                for (size_t i = 0; i < query_count; ++i)
                    batch_ents[i] = entities[idx_dist(gen)];
                class_pool<Position*> results;
                results.reserve_exact(query_count);
                ecss.get_ptr_batch<Position>(batch_ents.data(), results.data(), query_count);
                for (size_t i = 0; i < query_count; ++i)
                    if (results[i]) { batch_hit++; volatile float d = results[i]->x; (void)d; }
            }
            print_perf("get_ptr_batch 批量查询", batch_hit, t.elapsed_ms());

            t.reset();
            size_t pf_hit = 0;
            {
                class_pool<entity> pf_ents;
                pf_ents.reserve_exact(query_count);
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
            print_perf("prefetch+get 预取查询", pf_hit, t.elapsed_ms());

            t.reset();
            size_t cached_hit = 0;
            {
                class_pool<entity> cached_ents;
                cached_ents.reserve_exact(query_count);
                for (size_t i = 0; i < query_count; ++i)
                    cached_ents[i] = entities[idx_dist(gen)];
                auto* set = ecss.get_single_class_set<Position>();
                constexpr size_t pf_sparse = 16;
                constexpr size_t pf_data = 8;
                for (size_t i = 0; i < query_count; ++i)
                {
                    if (i + pf_sparse < query_count)
                        ecss.prefetch_ptr_cached<Position>(set, cached_ents[i + pf_sparse]);
                    if (i + pf_data < query_count)
                        ecss.prefetch_ptr_data_cached<Position>(set, cached_ents[i + pf_data]);
                    auto* p = ecss.get_ptr_fast_cached<Position>(set, cached_ents[i]);
                    if (p) { cached_hit++; volatile float d = p->x; (void)d; }
                }
            }
            print_perf("cached+双级预取 查询", cached_hit, t.elapsed_ms());

            t.reset();
            size_t ctx_hit = 0;
            {
                class_pool<entity> ctx_ents;
                ctx_ents.reserve_exact(query_count);
                for (size_t i = 0; i < query_count; ++i)
                    ctx_ents[i] = entities[idx_dist(gen)];
                ecs::query_context<Position> ctx(ecss);
                constexpr size_t pf_sparse = 16;
                constexpr size_t pf_data = 8;
                for (size_t i = 0; i < query_count; ++i)
                {
                    if (i + pf_sparse < query_count)
                        ctx.prefetch_sparse(ctx_ents[i + pf_sparse]);
                    if (i + pf_data < query_count)
                        ctx.prefetch_data(ctx_ents[i + pf_data]);
                    auto* p = ctx.get_ptr(ctx_ents[i]);
                    if (p) { ctx_hit++; volatile float d = p->x; (void)d; }
                }
            }
            print_perf("query_context 双级预取", ctx_hit, t.elapsed_ms());

            t.reset();
            size_t trav = 0;
            ecss.view<Position>().for_each([&](Position& pos) {
                trav++;
                volatile float d = pos.x; (void)d;
            });
            print_perf("容器遍历 (for_each)", trav, t.elapsed_ms());
        }

        // ---- 15.4 多组件视图查询 ----
        print_perf_sub("15.4 多组件视图查询");

        // 双组件
        t.reset();
        size_t cnt_2a = 0;
        ecss.view<Position, Velocity>().for_each([&](Position& p, Velocity& v) {
            cnt_2a++;
            volatile float d = p.x * v.vx; (void)d;
        });
        print_perf("双组件 Pos+Vel", cnt_2a, t.elapsed_ms());

        t.reset();
        size_t cnt_2b = 0;
        ecss.view<Position, Health>().for_each([&](Position& p, Health& h) {
            cnt_2b++;
            volatile float d = p.x + static_cast<float>(h.current); (void)d;
        });
        print_perf("双组件 Pos+Hp", cnt_2b, t.elapsed_ms());

        t.reset();
        size_t cnt_2c = 0;
        ecss.view<Velocity, Health>().for_each([&](Velocity& v, Health& h) {
            cnt_2c++;
            volatile float d = v.vx + static_cast<float>(h.current); (void)d;
        });
        print_perf("双组件 Vel+Hp", cnt_2c, t.elapsed_ms());

        // 三组件
        t.reset();
        size_t cnt_3a = 0;
        ecss.view<Position, Velocity, Health>().for_each([&](Position& p, Velocity& v, Health& h) {
            cnt_3a++;
            volatile float d = p.x + v.vy + static_cast<float>(h.current); (void)d;
        });
        print_perf("三组件 Pos+Vel+Hp", cnt_3a, t.elapsed_ms());

        t.reset();
        size_t cnt_3b = 0;
        ecss.view<Position, Velocity, Damage>().for_each([&](Position& p, Velocity& v, Damage& dmg) {
            cnt_3b++;
            volatile float d = p.x + v.vx + static_cast<float>(dmg.amount); (void)d;
        });
        print_perf("三组件 Pos+Vel+Dmg", cnt_3b, t.elapsed_ms());

        // 四组件
        t.reset();
        size_t cnt_4 = 0;
        ecss.view<Position, Velocity, Health, Name>().for_each([&](Position& p, Velocity& v, Health& h, Name& n) {
            cnt_4++;
            volatile float d = p.x + v.vy + static_cast<float>(h.current) + static_cast<float>(n.value.size()); (void)d;
        });
        print_perf("四组件 Pos+Vel+Hp+Name", cnt_4, t.elapsed_ms());

        // 五组件
        t.reset();
        size_t cnt_5 = 0;
        ecss.view<Position, Velocity, Health, Damage, Armor>().for_each([&](Position& p, Velocity& v, Health& h, Damage& dmg, Armor& arm) {
            cnt_5++;
            volatile float d = p.x + v.vx + static_cast<float>(h.current + dmg.amount + arm.defense); (void)d;
        });
        print_perf("五组件 Pos+Vel+Hp+Dmg+Armor", cnt_5, t.elapsed_ms());

        // 六组件
        t.reset();
        size_t cnt_6 = 0;
        ecss.view<Position, Velocity, Health, Damage, Armor, Speed>().for_each([&](Position& p, Velocity& v, Health& h, Damage& dmg, Armor& arm, Speed& spd) {
            cnt_6++;
            volatile float d = p.x + v.vx + static_cast<float>(h.current) + static_cast<float>(dmg.amount) + static_cast<float>(arm.defense) + spd.value; (void)d;
        });
        print_perf("六组件 Pos+Vel+Hp+Dmg+Armor+Spd", cnt_6, t.elapsed_ms());

        // 带entity
        t.reset();
        size_t cnt_ent = 0;
        ecss.view<Position, Velocity>().for_each([&](entity e, Position& p, Velocity& v) {
            cnt_ent++;
            volatile float d = p.x + v.vx + static_cast<float>(e.parts_.index_); (void)d;
        });
        print_perf("双组件带entity Pos+Vel", cnt_ent, t.elapsed_ms());

        t.reset();
        size_t cnt_ent3 = 0;
        ecss.view<Position, Velocity, Health>().for_each([&](entity e, Position& p, Velocity& v, Health& h) {
            cnt_ent3++;
            volatile float d = p.x + v.vx + static_cast<float>(h.current) + static_cast<float>(e.parts_.index_); (void)d;
        });
        print_perf("三组件带entity Pos+Vel+Hp", cnt_ent3, t.elapsed_ms());

        // ---- 15.5 排除/可选/OR视图 ----
        print_perf_sub("15.5 排除 / 可选 / OR 视图");

        t.reset();
        size_t cnt_excl = 0;
        ecss.view<Position>(ecs::without<Velocity>).for_each([&](Position& p) {
            cnt_excl++; (void)p;
        });
        print_perf("exclude 排除视图", cnt_excl, t.elapsed_ms());

        t.reset();
        size_t cnt_with = 0;
        ecss.view<Position>(ecs::with<Health>).for_each([&](Position& p, Health* hp) {
            cnt_with++; (void)p; (void)hp;
        });
        print_perf("with 可选视图", cnt_with, t.elapsed_ms());

        t.reset();
        size_t cnt_or = 0;
        ecss.view_or<Position, Velocity>().for_each([&](entity, Position* p, Velocity* v) {
            cnt_or++; (void)p; (void)v;
        });
        print_perf("or_view OR视图", cnt_or, t.elapsed_ms());

        t.reset();
        size_t cnt_any = 0;
        ecss.view_any_of<Position, Velocity, Health>().for_each([&](Position* p, Velocity* v, Health* h) {
            cnt_any++; (void)p; (void)v; (void)h;
        });
        print_perf("any_of 任意匹配视图", cnt_any, t.elapsed_ms());

        // ---- 15.6 Group 系统 ----
        print_perf_sub("15.6 Group 系统");

        t.reset();
        size_t cnt_grp = 0;
        auto g = ecss.group<Position, Velocity>();
        g.for_each([&](Position& p, Velocity& v) {
            cnt_grp++;
            volatile float d = p.x * v.vx; (void)d;
        });
        print_perf("Non-Owning Group", cnt_grp, t.elapsed_ms());

        t.reset();
        size_t cnt_own = 0;
        auto og = ecss.group<Position, Velocity>(ecs::owned<Position>);
        og.for_each([&](Position& p, Velocity& v) {
            cnt_own++;
            volatile float d = p.x * v.vx; (void)d;
        });
        print_perf("Owning Group", cnt_own, t.elapsed_ms());

        t.reset();
        size_t cnt_reo = 0;
        auto rg = ecss.group<Position, Velocity>(ecs::reorder<Position>);
        rg.for_each([&](Position& p, Velocity& v) {
            cnt_reo++;
            volatile float d = p.x * v.vx; (void)d;
        });
        print_perf("Reorder Group", cnt_reo, t.elapsed_ms());

        // ---- 15.7 运行时视图 ----
        print_perf_sub("15.7 运行时视图");

        t.reset();
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
        print_perf("runtime_view 双组件", cnt_rt2, t.elapsed_ms());

        t.reset();
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
        print_perf("runtime_view 三组件", cnt_rt3, t.elapsed_ms());

        t.reset();
        size_t cnt_rt_excl = 0;
        auto rv_excl = ecss.runtime_view_create(
            { type_id::get_type_id<Position>() },
            { type_id::get_type_id<Velocity>() }
        );
        rv_excl.for_each([&](entity e) {
            cnt_rt_excl++;
            volatile float d = static_cast<float>(e.parts_.index_); (void)d;
        });
        print_perf("runtime_view 排除视图", cnt_rt_excl, t.elapsed_ms());

        // ---- 15.8 视图扩展 ----
        print_perf_sub("15.8 视图扩展 (page/sort/group/track)");

        t.reset();
        size_t cnt_page = 0;
        {
            auto mv = ecss.view<Position, Velocity>();
            mv.page(0, cnt_2a).for_each([&](Position&, Velocity&) { cnt_page++; });
        }
        print_perf("page 分页视图", cnt_page, t.elapsed_ms());

        t.reset();
        size_t cnt_changed = 0;
        {
            auto mv = ecss.view<Position, Velocity>();
            auto cv = mv.track_changes();
            cv.for_each([&](Position&, Velocity&) { cnt_changed++; });
        }
        print_perf("track_changes 变更检测", cnt_changed, t.elapsed_ms());

        // 排序/分组 (小数据集)
        {
            constexpr size_t sort_n = 1000000;
            ecs::manager sort_mgr;
            for (size_t i = 0; i < sort_n; ++i) {
                auto e = sort_mgr.create_entity();
                sort_mgr.add(e, Position{static_cast<float>(rand() % 1000), static_cast<float>(rand() % 1000), 0});
                sort_mgr.add(e, Velocity{static_cast<float>(rand() % 100), 0, 0});
            }

            t.reset();
            size_t cnt_sorted = 0;
            {
                auto mv = sort_mgr.view<Position, Velocity>();
                auto sv = mv.sorted_by_component<Position>(
                    [](const Position& a, const Position& b) { return a.x < b.x; });
                sv.for_each([&](Position&, Velocity&) { cnt_sorted++; });
            }
            print_perf("sorted_by_component 排序", cnt_sorted, t.elapsed_ms());

            t.reset();
            size_t cnt_grouped = 0;
            {
                auto sv = sort_mgr.view<Position>();
                auto gv = sv.sorted_by_component_value(
                    [](Position& p) -> int { return static_cast<int>(p.x) / 10; });
                gv.for_each([&](Position&) { cnt_grouped++; });
            }
            print_perf("sorted_by_component_value 分组", cnt_grouped, t.elapsed_ms());
        }

        // ---- 15.9 过滤视图 ----
        print_perf_sub("15.9 过滤视图");

        t.reset();
        size_t cnt_filt = 0;
        {
            auto fv = ecss.view_filtered<Position>([](Position& p) { return p.x > 0.0f; });
            fv.for_each([&](Position&) { cnt_filt++; });
        }
        print_perf("filter_view 过滤视图", cnt_filt, t.elapsed_ms());

        size_t cnt_fand = 0;
        {
            auto fa = ecss.view_filtered<Position>([](Position& p) { return p.x > 0.0f; }).and_<Velocity>();
            t.reset();
            constexpr int warmup = 50;
            for (int iter = 0; iter < warmup; ++iter)
            {
                cnt_fand = 0;
                fa.for_each([&](Position&, Velocity&) { cnt_fand++; });
            }
            double elapsed = t.elapsed_ms() / warmup;
            print_perf("filter_and 过滤且视图", cnt_fand, elapsed);
        }

        size_t cnt_for = 0;
        {
            auto fo = ecss.view_filtered<Position>([](Position& p) { return p.x > 0.0f; }).or_<Velocity>();
            t.reset();
            constexpr int warmup = 50;
            for (int iter = 0; iter < warmup; ++iter)
            {
                cnt_for = 0;
                fo.for_each([&](entity, Position* p, Velocity* v) {
                    cnt_for++; (void)p; (void)v;
                });
            }
            double elapsed = t.elapsed_ms() / warmup;
            print_perf("filter_or 过滤或视图", cnt_for, elapsed);
        }

        // ---- 15.10 单点查询接口 ----
        print_perf_sub("15.10 单点查询接口");

        {
            std::uniform_int_distribution<size_t> idx_dist(0, entity_count - 1);
            const size_t query_count = 1000000;

            class_pool<entity> query_ents;
            query_ents.reserve_exact(query_count);
            for (size_t i = 0; i < query_count; ++i)
                query_ents[i] = entities[idx_dist(gen)];

            for (size_t i = 0; i < query_count; ++i)
            {
                auto* p = ecss.get_ptr<Position>(query_ents[i]);
                volatile bool b = (p != nullptr); (void)b;
            }

            t.reset();
            for (size_t i = 0; i < query_count; ++i)
            {
                auto* p = ecss.get_ptr<Position>(query_ents[i]);
                volatile bool b = (p != nullptr); (void)b;
            }
            print_perf("get_ptr 随机查询", query_count, t.elapsed_ms());

            t.reset();
            for (size_t i = 0; i < query_count; ++i)
            {
                auto* p = ecss.get_ptr_fast<Position>(query_ents[i]);
                volatile bool b = (p != nullptr); (void)b;
            }
            print_perf("get_ptr_fast 快速查询", query_count, t.elapsed_ms());

            class_pool<entity> rand_ents;
            rand_ents.reserve_exact(query_count);
            for (size_t i = 0; i < query_count; ++i)
                rand_ents[i] = entities[idx_dist(gen)];

            const auto* pos_set = ecss.get_single_class_set<Position>();
            constexpr size_t sparse_dist = 16;
            constexpr size_t data_dist = 4;

            for (size_t i = 0; i < sparse_dist; ++i)
                pos_set->prefetch_ptr(rand_ents[i]);
            for (size_t i = 0; i < data_dist; ++i)
                pos_set->prefetch_ptr_data<Position>(rand_ents[i]);

            t.reset();
            size_t pf_hit = 0;
            for (size_t i = 0; i < query_count; ++i)
            {
                if (i + sparse_dist < query_count)
                    pos_set->prefetch_ptr(rand_ents[i + sparse_dist]);
                if (i + data_dist < query_count)
                    pos_set->prefetch_ptr_data<Position>(rand_ents[i + data_dist]);
                auto* p = pos_set->get_ptr<Position>(rand_ents[i]);
                if (p) { ++pf_hit; volatile float d = p->x; (void)d; }
            }
            print_perf("get_ptr 随机查询 (双级预取)", pf_hit, t.elapsed_ms());

            class_pool<entity> cache_ents;
            cache_ents.reserve_exact(query_count);
            for (size_t i = 0; i < query_count; ++i)
                cache_ents[i] = entities[idx_dist(gen)];

            class_pool<uint64_t> cache_evict;
            constexpr size_t evict_bytes = 64 * 1024 * 1024;
            cache_evict.reserve_exact(evict_bytes / sizeof(uint64_t));
            volatile uint64_t evict_sink = 0;

            for (size_t rep = 0; rep < 2; ++rep)
            {
                for (size_t i = 0; i < cache_evict.size(); ++i)
                    evict_sink = cache_evict[i];
            }

            t.reset();
            size_t cold_hit = 0;
            for (size_t i = 0; i < query_count; ++i)
            {
                auto* p = pos_set->get_ptr<Position>(cache_ents[i]);
                if (p) { ++cold_hit; volatile float d = p->x; (void)d; }
            }
            double cold_ms = t.elapsed_ms();

            t.reset();
            size_t warm_hit = 0;
            for (size_t i = 0; i < query_count; ++i)
            {
                auto* p = pos_set->get_ptr<Position>(cache_ents[i]);
                if (p) { ++warm_hit; volatile float d = p->x; (void)d; }
            }
            double warm_ms = t.elapsed_ms();

            print_perf("缓存命中率 冷查询", query_count, cold_ms);
            print_perf("缓存命中率 暖查询", query_count, warm_ms);
            double efficiency = cold_ms > 0.01 ? (1.0 - warm_ms / cold_ms) * 100.0 : 0.0;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << efficiency << "%";
            print_item("缓存有效率", oss.str());
            (void)evict_sink;
        }

        // ---- 15.10b 分页稀疏 + 热集缓存 ----
        print_perf_sub("15.10b 分页稀疏 + 热集缓存");

        {
            const size_t query_count = 1000000;
            std::uniform_int_distribution<size_t> idx_dist(0, entity_count - 1);

            auto* pos_set = ecss.get_single_class_set<Position>();

            // 分页稀疏信息
            print_item("sparse_size", std::to_string(pos_set->get_sparse_size()));
            print_item("page_directory_capacity", std::to_string(pos_set->get_page_directory_capacity()));

            // 热集命中率测试：顺序扫描（热集命中）
            class_pool<entity> seq_ents;
            seq_ents.reserve_exact(query_count);
            for (size_t i = 0; i < query_count; ++i)
                seq_ents[i] = entities[i % entity_count];

            // 第一遍预热热集
            for (size_t i = 0; i < query_count; ++i)
            {
                auto* p = pos_set->get_ptr<Position>(seq_ents[i]);
                (void)p;
            }

            // 第二遍测热集命中
            t.reset();
            size_t hot_hit = 0;
            for (size_t i = 0; i < query_count; ++i)
            {
                auto* p = pos_set->get_ptr<Position>(seq_ents[i]);
                if (p) { ++hot_hit; volatile float d = p->x; (void)d; }
            }
            print_perf("热集命中 (顺序)", hot_hit, t.elapsed_ms());

            // 热集未命中测试：随机查询
            class_pool<entity> rand_ents2;
            rand_ents2.reserve_exact(query_count);
            for (size_t i = 0; i < query_count; ++i)
                rand_ents2[i] = entities[idx_dist(gen)];

            // 清空热集
            pos_set->clear_hot_set();

            t.reset();
            size_t miss_hit = 0;
            for (size_t i = 0; i < query_count; ++i)
            {
                auto* p = pos_set->get_ptr<Position>(rand_ents2[i]);
                if (p) { ++miss_hit; volatile float d = p->x; (void)d; }
            }
            print_perf("热集未命中 (随机)", miss_hit, t.elapsed_ms());

            // query_context + 热集
            class_pool<entity> ctx_ents2;
            ctx_ents2.reserve_exact(query_count);
            for (size_t i = 0; i < query_count; ++i)
                ctx_ents2[i] = entities[idx_dist(gen)];

            pos_set->clear_hot_set();

            t.reset();
            size_t ctx_hit2 = 0;
            {
                ecs::query_context<Position> ctx(ecss);
                for (size_t i = 0; i < query_count; ++i)
                {
                    auto* p = ctx.get_ptr(ctx_ents2[i]);
                    if (p) { ++ctx_hit2; volatile float d = p->x; (void)d; }
                }
            }
            print_perf("query_context+热集 (随机)", ctx_hit2, t.elapsed_ms());

            // query_context + 热集 + 双级预取
            class_pool<entity> pf_ents2;
            pf_ents2.reserve_exact(query_count);
            for (size_t i = 0; i < query_count; ++i)
                pf_ents2[i] = entities[idx_dist(gen)];

            pos_set->clear_hot_set();

            t.reset();
            size_t pf_ctx_hit = 0;
            {
                ecs::query_context<Position> ctx(ecss);
                constexpr size_t pf_sparse = 16;
                constexpr size_t pf_data = 8;
                for (size_t i = 0; i < query_count; ++i)
                {
                    if (i + pf_sparse < query_count)
                        ctx.prefetch_sparse(pf_ents2[i + pf_sparse]);
                    if (i + pf_data < query_count)
                        ctx.prefetch_data(pf_ents2[i + pf_data]);
                    auto* p = ctx.get_ptr(pf_ents2[i]);
                    if (p) { ++pf_ctx_hit; volatile float d = p->x; (void)d; }
                }
            }
            print_perf("query_context+热集+预取 (随机)", pf_ctx_hit, t.elapsed_ms());

            // 分页稀疏 vs flat 对比：直接 sparse_version_at_public
            class_pool<entity> sparse_ents;
            sparse_ents.reserve_exact(query_count);
            for (size_t i = 0; i < query_count; ++i)
                sparse_ents[i] = entities[idx_dist(gen)];

            t.reset();
            size_t sparse_hit = 0;
            for (size_t i = 0; i < query_count; ++i)
            {
                uint32_t ver = pos_set->sparse_version_at_public(sparse_ents[i].parts_.index_);
                if (ver == sparse_ents[i].parts_.version_) ++sparse_hit;
            }
            print_perf("sparse_version_at_public (随机)", sparse_hit, t.elapsed_ms());
        }

        // ---- 15.10c 分页大小运行时可配置 + flat/paged 模式 ----
        print_perf_sub("15.10c 分页大小运行时可配置 + flat/paged 模式");
        {
            auto* pos_set = ecss.get_single_class_set<Position>();
            print_item("is_flat_mode (1M实体)", pos_set->is_flat_mode() ? "true" : "false");
            size_t orig_shift = pos_set->get_page_size_shift();
            print_item("默认 shift", std::to_string(orig_shift));

            // 修改分页大小
            pos_set->set_page_size_shift(12);
            print_item("set shift=12 后", std::to_string(pos_set->get_page_size_shift()));

            bool all_valid = true;
            for (size_t i = 0; i < entity_count; ++i)
            {
                auto* p = pos_set->get_ptr<Position>(entities[i]);
                if (!p) { all_valid = false; break; }
            }
            print_item("shift=12 查询正确", all_valid ? "true" : "false");

            ecss.set_component_page_size_shift<Position>(8);
            print_item("manager shift=8", std::to_string(ecss.get_component_page_size_shift<Position>()));
            all_valid = true;
            for (size_t i = 0; i < entity_count; ++i)
            {
                auto* p = pos_set->get_ptr<Position>(entities[i]);
                if (!p) { all_valid = false; break; }
            }
            print_item("shift=8 查询正确", all_valid ? "true" : "false");

            pos_set->set_page_size_shift(orig_shift);

            // flat 模式测试 (小规模)
            ecs::manager fmgr;
            auto e0 = fmgr.create_entity();
            auto e1 = fmgr.create_entity();
            fmgr.add(e0, Position{1, 0, 0});
            fmgr.add(e1, Position{2, 0, 0});
            auto* fset = fmgr.get_single_class_set<Position>();
            print_item("小规模 is_flat_mode", fset->is_flat_mode() ? "true" : "false");
            auto* p0 = fmgr.get_ptr<Position>(e0);
            auto* p1 = fmgr.get_ptr<Position>(e1);
            print_item("flat 模式查询正确", p0 && p1 && p0->x == 1 && p1->x == 2);
        }

        // ---- 15.31 汇总 ----
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
    print_section(11, "缓存命中率测试");
        // ---- 11.1 CPU 频率校准 ----
        print_perf_sub("11.1 CPU 频率校准 (rdtsc)");
        {
            double cpu_ghz = estimate_cpu_ghz(100);
            std::cout << "  CPU 频率估算: " << std::fixed << std::setprecision(3)
                      << cpu_ghz << " GHz\n";
        }

        // ---- 11.2 class_pool<Position> 顺序 vs 随机访问 ----
        print_perf_sub("11.2 class_pool 顺序 vs 随机 (Position 12B × 100K = 1.2MB)");
        {
            const size_t cache_n = 100000;
            class_pool<Position> cp;
            cp.increase_capacity(cache_n);
            for (size_t i = 0; i < cache_n; ++i)
            {
                cp.emplace_back(static_cast<float>(i), 0, 0);
            }

            const Position* base = cp.data();
            const size_t stride = sizeof(Position);

            // 顺序访问 (缓存友好, 硬件预取生效)
            auto seq_addrs = make_sequential_addresses(base, cache_n, stride);
            auto seq_report = measure_cache_hits(seq_addrs);
            print_cache_report("顺序访问 (逐次)", seq_report);
            auto seq_batch = measure_cache_batch(seq_addrs, 10);
            print_cache_batch("顺序访问 (批量×10)", seq_batch);

            // 随机访问 (缓存不友好)
            auto rnd_addrs = make_random_addresses(base, cache_n, stride, 42);
            auto rnd_report = measure_cache_hits(rnd_addrs);
            print_cache_report("随机访问 (逐次)", rnd_report);
            auto rnd_batch = measure_cache_batch(rnd_addrs, 10);
            print_cache_batch("随机访问 (批量×10)", rnd_batch);

            double ratio = (seq_batch.net_cycles_per_access > 0)
                ? rnd_batch.net_cycles_per_access / seq_batch.net_cycles_per_access : 0;
            std::cout << "  >> 随机/顺序 延迟比: " << std::fixed << std::setprecision(2)
                      << ratio << " 倍\n";
        }

        // ---- 11.3 大组件 (128B) 顺序 vs 随机访问 ----
        print_perf_sub("11.3 class_pool 顺序 vs 随机 (Cache128B 128B × 100K = 12.8MB)");
        {
            struct Cache128B
            {
                char data[128];
                Cache128B(int v = 0) { data[0] = static_cast<char>(v); }
            };

            const size_t big_n = 100000;
            class_pool<Cache128B> cp_big;
            cp_big.increase_capacity(big_n);
            for (size_t i = 0; i < big_n; ++i)
            {
                cp_big.emplace_back(static_cast<int>(i));
            }

            const Cache128B* base_big = cp_big.data();
            const size_t stride_big = sizeof(Cache128B);

            auto seq_big = make_sequential_addresses(base_big, big_n, stride_big);
            auto seq_big_r = measure_cache_hits(seq_big);
            print_cache_report("大组件顺序 (逐次)", seq_big_r);
            auto seq_big_b = measure_cache_batch(seq_big, 5);
            print_cache_batch("大组件顺序 (批量×5)", seq_big_b);

            auto rnd_big = make_random_addresses(base_big, big_n, stride_big, 99);
            auto rnd_big_r = measure_cache_hits(rnd_big);
            print_cache_report("大组件随机 (逐次)", rnd_big_r);
            auto rnd_big_b = measure_cache_batch(rnd_big, 5);
            print_cache_batch("大组件随机 (批量×5)", rnd_big_b);

            double ratio_big = (seq_big_b.net_cycles_per_access > 0)
                ? rnd_big_b.net_cycles_per_access / seq_big_b.net_cycles_per_access : 0;
            std::cout << "  >> 随机/顺序 延迟比: " << std::fixed << std::setprecision(2)
                      << ratio_big << " 倍\n";
        }

        // ---- 11.4 ECS 组件数据 顺序 vs 随机访问 ----
        print_perf_sub("11.4 ECS 组件数据 顺序 vs 随机 (Position × 1M = 12MB)");
        {
            class_pool<Position>* pos_pool = ecss.get_component_container<Position>();
            if (pos_pool && pos_pool->size() > 0)
            {
                const Position* ecs_base = pos_pool->data();
                size_t ecs_n = pos_pool->size();
                size_t ecs_stride = sizeof(Position);

                auto ecs_seq = make_sequential_addresses(ecs_base, ecs_n, ecs_stride);
                auto ecs_seq_r = measure_cache_hits(ecs_seq);
                print_cache_report("ECS顺序 (逐次)", ecs_seq_r);
                auto ecs_seq_b = measure_cache_batch(ecs_seq, 3);
                print_cache_batch("ECS顺序 (批量×3)", ecs_seq_b);

                auto ecs_rnd = make_random_addresses(ecs_base, ecs_n, ecs_stride, 77);
                auto ecs_rnd_r = measure_cache_hits(ecs_rnd);
                print_cache_report("ECS随机 (逐次)", ecs_rnd_r);
                auto ecs_rnd_b = measure_cache_batch(ecs_rnd, 3);
                print_cache_batch("ECS随机 (批量×3)", ecs_rnd_b);

                double ecs_ratio = (ecs_seq_b.net_cycles_per_access > 0)
                    ? ecs_rnd_b.net_cycles_per_access / ecs_seq_b.net_cycles_per_access : 0;
                std::cout << "  >> 随机/顺序 延迟比: " << std::fixed << std::setprecision(2)
                          << ecs_ratio << " 倍\n";
            }
            else
            {
                std::cout << "  (ECS Position 数据不可用, 跳过)\n";
            }
        }

        // ---- 11.5 ECS get_ptr 延迟 (含稀疏表间接寻址) ----
        print_perf_sub("11.5 ECS get_ptr 延迟 (含稀疏表间接寻址)");
        {
            // benchmark_cycles 测量 get_ptr 延迟 (含稀疏表查找 + 数据访问)
            // 注: rdtscp 有 ~30 周期开销, 绝对值偏高, 但 get_ptr vs 直接访问的差值有意义
            size_t gi = 0;
            auto getptr_stats = benchmark_cycles(2000, 200, [&]() {
                entity e = entities[gi % entity_count];
                Position* p = ecss.get_ptr<Position>(e);
                volatile float sink = p ? p->x : 0.0f;
                (void)sink;
                ++gi;
            });
            print_stats("get_ptr 随机延迟", getptr_stats, "周期");

            // 对比: 直接数组访问 (无稀疏表开销)
            class_pool<Position>* pos_pool = ecss.get_component_container<Position>();
            if (pos_pool && pos_pool->size() > 0)
            {
                size_t di = 0;
                size_t pn = pos_pool->size();
                auto direct_stats = benchmark_cycles(2000, 200, [&]() {
                    Position& p = (*pos_pool)[di % pn];
                    volatile float sink = p.x;
                    (void)sink;
                    ++di;
                });
                print_stats("直接数组访问", direct_stats, "周期");

                double overhead = getptr_stats.mean - direct_stats.mean;
                std::cout << "  >> 稀疏表间接开销: " << std::fixed << std::setprecision(3)
                          << overhead << " 周期/次\n";
            }
        }

        // ---- 11.6 数据规模对缓存行为的影响 ----
        print_perf_sub("11.6 数据规模 vs 缓存行为 (Position 12B, batch×20)");
        {
            struct SizeCase { const char* name; size_t count; };
            SizeCase cases[] = {
                {"4K  (48KB  <L1)",    4096},
                {"32K (384KB >L1<L2)", 32768},
                {"256K(3MB   >L2<L3)", 262144},
                {"1M  (12MB  >L3)",    1048576},
            };

            for (const auto& tc : cases)
            {
                class_pool<Position> cp_sz;
                cp_sz.increase_capacity(tc.count);
                for (size_t i = 0; i < tc.count; ++i)
                {
                    cp_sz.emplace_back(static_cast<float>(i), 0, 0);
                }

                const Position* base = cp_sz.data();
                size_t stride = sizeof(Position);

                auto seq = make_sequential_addresses(base, tc.count, stride);
                auto rnd = make_random_addresses(base, tc.count, stride, 314);

                auto seq_b = measure_cache_batch(seq, 20);
                auto rnd_b = measure_cache_batch(rnd, 20);

                double ratio = (seq_b.net_cycles_per_access > 0)
                    ? rnd_b.net_cycles_per_access / seq_b.net_cycles_per_access : 0;
                std::cout << std::fixed << std::setprecision(3);
                std::cout << "  " << std::left << std::setw(30) << tc.name
                          << " | 顺序 " << std::setw(8) << seq_b.net_cycles_per_access << " 周期"
                          << " | 随机 " << std::setw(8) << rnd_b.net_cycles_per_access << " 周期"
                          << " | 比值 " << std::setw(6) << ratio << " 倍\n";
            }
        }

        // ---- 11.7 缓存层级自适应检测 ----
        print_perf_sub("11.7 缓存层级自适应检测");
        {
            std::cout << "  检测本地 CPU 缓存层级...\n";
            latency_thresholds auto_th = detect_cache_latency_thresholds();
            std::cout << "  >> 检测到 " << auto_th.cache_levels << " 级缓存\n";
            std::cout << "  >> L1 阈值: " << std::fixed << std::setprecision(1)
                      << auto_th.l1_max << " 周期\n";
            if (auto_th.cache_levels >= 2)
            {
                std::cout << "  >> L2 阈值: " << auto_th.l2_max << " 周期\n";
            }
            if (auto_th.cache_levels >= 3)
            {
                std::cout << "  >> L3 阈值: " << auto_th.l3_max << " 周期\n";
            }

            // 使用默认阈值 (3级) 和自适应阈值分别测量
            int buf[4096];
            auto addrs = make_sequential_addresses(buf, 4096, sizeof(int));
            std::cout << "  >> 默认三级阈值 → 测量结果:\n";
            cache_report r_default = measure_cache_hits(addrs);
            std::cout << "     L1: " << std::setprecision(1) << r_default.l1_hit_rate * 100 << "%"
                      << "  L2: " << r_default.l2_hit_rate * 100 << "%"
                      << "  L3: " << r_default.l3_hit_rate * 100 << "%"
                      << "  Miss: " << r_default.miss_rate * 100 << "%"
                      << "  levels=" << r_default.active_levels << "\n";

            std::cout << "  >> 自适应阈值 → 测量结果:\n";
            cache_report r_auto = measure_cache_hits(addrs, auto_th);
            std::cout << "     L1: " << std::setprecision(1) << r_auto.l1_hit_rate * 100 << "%"
                      << "  L2: " << r_auto.l2_hit_rate * 100 << "%"
                      << "  L3: " << r_auto.l3_hit_rate * 100 << "%"
                      << "  Miss: " << r_auto.miss_rate * 100 << "%"
                      << "  levels=" << r_auto.active_levels << "\n";

            // 验证: 1级缓存场景 (仅 L1)
            if (auto_th.cache_levels >= 1)
            {
                latency_thresholds th_l1_only = auto_th;
                th_l1_only.cache_levels = 1;
                std::cout << "  >> 仅 L1 缓存 (cache_levels=1) → 测量结果:\n";
                cache_report r_l1 = measure_cache_hits(addrs, th_l1_only);
                std::cout << "     L1: " << std::setprecision(1) << r_l1.l1_hit_rate * 100 << "%"
                          << "  Miss: " << r_l1.miss_rate * 100 << "%"
                          << "  levels=" << r_l1.active_levels << "\n";
            }
        }
    }
    print_summary("性能测试");
    return 0;
}