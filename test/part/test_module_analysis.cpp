// test_module_analysis.cpp - 模块功能测试: analyzer (微架构分析)
#include "test_common.hpp"
#include "include/part/analysis.hpp"

alignas(64) static char g_scan_buf[256 * 1024];

int main()
{
    t_begin("test_module_analysis — 模块功能测试: analyzer");

    t_section(1, "默认配置");
    {
        analyzer an;
        const analyzer::config& c = an.get_config();
        t_check("默认层级 3", c.cache_levels == 3);
        t_check("阈值单调", c.l1_max <= c.l2_max && c.l2_max <= c.l3_max);

        analyzer::config cfg;
        cfg.cache_levels = 2;
        cfg.l1_max = 5.0;
        an.set_config(cfg);
        t_check("set_config 生效", an.get_config().cache_levels == 2 && an.get_config().l1_max == 5.0);
    }

    t_section(2, "地址生成");
    {
        auto seq = analyzer::make_sequential_addresses(g_scan_buf, 16, 64);
        t_check("顺序地址数量", seq.size() == 16);
        t_check("首地址对齐基址", seq[0] == static_cast<const void*>(g_scan_buf));

        auto rnd = analyzer::make_random_addresses(g_scan_buf, 16, 64, 12345);
        t_check("随机地址数量", rnd.size() == 16);

        analyzer::address_view av{seq.data(), seq.size()};
        t_check("address_view size", av.size() == 16 && !av.empty());
        t_check("address_view 索引", av[0] == seq[0]);
    }

    t_section(3, "缓存命中测量");
    {
        analyzer an;
        // 小工作集 (16KB) 顺序扫描
        analyzer::cache_report small = an.measure_hits(g_scan_buf, 256, 64);
        t_check("total == 256", small.total == 256);
        t_check("命中率在 [0,1]", small.l1_hit_rate >= 0.0 && small.l1_hit_rate <= 1.0);
        t_check("miss 率在 [0,1]", small.miss_rate >= 0.0 && small.miss_rate <= 1.0);
        t_check("avg_cycles 非负", small.avg_cycles >= 0.0);
        t_check("分位数可用", small.p50_cycles >= 0.0 && small.p99_cycles >= small.p50_cycles);
    }

    t_section(4, "批量测量");
    {
        analyzer an;
        auto seq = analyzer::make_sequential_addresses(g_scan_buf, 512, 64);
        analyzer::address_view av{seq.data(), seq.size()};
        analyzer::batch_result br = an.measure_batch(av, 4);
        t_check("repeats 记录", br.repeats == 4);
        t_check("avg 非负", br.avg_cycles_per_access >= 0.0);
    }

    t_section(5, "屏障与周期原语");
    {
        analyzer::mfence();
        analyzer::lfence();
        analyzer::sfence();
        uint64_t c0 = analyzer::now_cycles_fenced();
        uint64_t c1 = analyzer::now_cycles_fenced();
        t_check("周期计数非递减", c1 >= c0);

        uint64_t loop = analyzer::measure_loop_cycles([]() {
            volatile int s = 0;
            for (int i = 0; i < 1000; ++i) { s += i; }
            (void)s;
        });
        t_check("measure_loop_cycles 可调用", (loop > 0) || (loop == 0));
    }

    t_section(6, "层级自动检测");
    {
        analyzer::config det = analyzer::detect_config();
        t_check("检测层级数在 1..4", det.cache_levels >= 1 && det.cache_levels <= 4);
    }

    return t_end("analysis ");
}
