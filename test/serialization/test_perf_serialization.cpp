// test_perf_serialization.cpp - 性能测试: 序列化
#include "test_common.hpp"
#include "include/serialization/manager.hpp"
#include <string>

using serialize::full_archive;
using serialize::archive_format;

struct position
{
    float x;
    float y;
    float z;
};

static volatile uint64_t g_sink = 0;

int main()
{
    t_begin("test_perf_serialization — 性能测试: 序列化 (百万为单位)");

    constexpr uint64_t N = 200'000;

    t_section(1, "保存");
    {
        ecs::manager mgr;
        mgr.preallocate_entities(N);
        for (uint64_t i = 0; i < N; ++i)
        {
            auto e = mgr.create_entity();
            mgr.add(e, position{static_cast<float>(i), 0.0f, 0.0f});
        }
        full_archive fa(mgr);
        fa.register_components<position>();
        fa.set_checksum_enabled(false);

        std::string json;
        operating_message save_error;
        double ms = t_bench_best_ms([&]() {
            save_error = fa.save_to_string(json, archive_format::json);
            g_sink += json.size();
        }, 3);
        t_perf("save JSON", N, ms);

        std::string bin;
        ms = t_bench_best_ms([&]() {
            save_error = fa.save_to_string(bin, archive_format::binary);
            g_sink += bin.size();
        }, 3);
        t_perf("save binary", N, ms);
    }

    t_section(2, "加载");
    {
        std::string json;
        std::string bin;
        {
            ecs::manager src;
            src.preallocate_entities(N);
            for (uint64_t i = 0; i < N; ++i)
            {
                auto e = src.create_entity();
                src.add(e, position{static_cast<float>(i), 0.0f, 0.0f});
            }
            full_archive sa(src);
            sa.register_components<position>();
            sa.set_checksum_enabled(false);
            operating_message save_error;
            save_error = sa.save_to_string(json, archive_format::json);
            save_error = sa.save_to_string(bin, archive_format::binary);
        }

        double ms = t_bench_best_ms([&]() {
            ecs::manager dst;
            full_archive da(dst);
            da.register_components<position>();
            da.set_checksum_enabled(false);
            operating_message load_error;
            load_error = da.load_from_string(json); const bool r = !load_error;
            g_sink += r ? 1 : 0;
        }, 3);
        t_perf("load JSON", N, ms);

        ms = t_bench_best_ms([&]() {
            ecs::manager dst;
            full_archive da(dst);
            da.register_components<position>();
            da.set_checksum_enabled(false);
            operating_message load_error;
            load_error = da.load_from_string(bin); const bool r = !load_error;
            g_sink += r ? 1 : 0;
        }, 3);
        t_perf("load binary", N, ms);
    }

    return t_end("serialization 性能 ");
}
