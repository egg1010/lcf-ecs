// test_perf_serialization.cpp - 性能测试: 序列化
#include "test_common.hpp"
#include "include/serialization/serialization.hpp"
#include <string>

using serialize::serialization;

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
        serialization s(mgr);
        s.set_checksum_enabled(false);

        std::string json;
        double ms = t_bench_best_ms([&]() {
            s.save_to_string<position>(json, serialization::format::json);
            g_sink += json.size();
        }, 3);
        t_perf("save JSON", N, ms);

        std::string bin;
        ms = t_bench_best_ms([&]() {
            s.save_to_string<position>(bin, serialization::format::binary);
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
            serialization s(src);
            s.set_checksum_enabled(false);
            s.save_to_string<position>(json, serialization::format::json);
            s.save_to_string<position>(bin, serialization::format::binary);
        }

        double ms = t_bench_best_ms([&]() {
            ecs::manager dst;
            serialization s(dst);
            s.set_checksum_enabled(false);
            auto r = s.load_from_string<position>(json);
            g_sink += r ? 1 : 0;
        }, 3);
        t_perf("load JSON", N, ms);

        ms = t_bench_best_ms([&]() {
            ecs::manager dst;
            serialization s(dst);
            s.set_checksum_enabled(false);
            auto r = s.load_from_string<position>(bin);
            g_sink += r ? 1 : 0;
        }, 3);
        t_perf("load binary", N, ms);
    }

    return t_end("serialization 性能 ");
}
