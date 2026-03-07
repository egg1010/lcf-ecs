#include "include/component.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>

// 测试组件定义
struct Position {
    int x, y;
    Position(int x = 0, int y = 0) : x(x), y(y) {}
};

struct Velocity {
    int dx, dy;
    Velocity(int dx = 0, int dy = 0) : dx(dx), dy(dy) {}
};

struct Health {
    int hp;
    Health(int hp = 100) : hp(hp) {}
};

// 性能测试工具类
class PerformanceTester {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    
public:
    void start() {
        start_time = std::chrono::high_resolution_clock::now();
    }
    
    double elapsed_ms() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        return duration.count() / 1000.0;
    }
    
    double elapsed_us() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        return duration.count() / 1000.0;
    }
};

// 打印分隔线
void print_separator(const std::string& title = "") {
    if (!title.empty()) {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << " " << title << std::endl;
        std::cout << std::string(60, '=') << std::endl;
    } else {
        std::cout << std::string(60, '-') << std::endl;
    }
}

// 格式化输出性能数据
void print_performance_stats(const std::string& operation, 
                           double total_time_ms, 
                           size_t count, 
                           const std::string& unit = "entity") {
    double avg_time_us = (total_time_ms * 1000.0) / count;
    double ops_per_second = count / (total_time_ms / 1000.0);
    
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "操作: " << std::setw(30) << std::left << operation << std::endl;
    std::cout << "  总耗时:        " << std::setw(12) << total_time_ms << " ms" << std::endl;
    std::cout << "  操作数量:      " << std::setw(12) << count << " " << unit << "s" << std::endl;
    std::cout << "  平均耗时:      " << std::setw(12) << avg_time_us << " μs/" << unit << std::endl;
    std::cout << "  吞吐量:        " << std::setw(12) << std::scientific << ops_per_second << " " << unit << "s/秒" << std::endl;
    std::cout << std::defaultfloat;
}

// 百万级实体测试
void test_million_entities() {
    print_separator("百万级实体性能测试");
    
    // 创建ECS管理器（使用独立内存块模式，更适合大规模测试）
    auto ecs = ecs::manager::create(vao::Enable_stack_memory, ecs::ecs_option::On_different_memory_blocks);
    
    const size_t entity_count = 1000000; // 100万实体
    std::vector<entity> entities;
    entities.reserve(entity_count);
    
    PerformanceTester timer;
    
    // 1. 创建百万实体
    std::cout << "正在创建 " << entity_count << " 个实体..." << std::endl;
    timer.start();
    for (size_t i = 0; i < entity_count; ++i) {
        entities.push_back(ecs->create_entity());
    }
    double create_time = timer.elapsed_ms();
    print_performance_stats("创建实体", create_time, entity_count);
    
    // 2. 为所有实体添加Position组件
    std::cout << "\n正在为所有实体添加Position组件..." << std::endl;
    timer.start();
    for (size_t i = 0; i < entity_count; ++i) {
        ecs->add(entities[i], Position(static_cast<int>(i), static_cast<int>(i * 2)));
    }
    double add_position_time = timer.elapsed_ms();
    print_performance_stats("添加Position组件", add_position_time, entity_count, "component");
    
    // 3. 为所有实体添加Velocity组件
    std::cout << "\n正在为所有实体添加Velocity组件..." << std::endl;
    timer.start();
    for (size_t i = 0; i < entity_count; ++i) {
        ecs->add(Velocity(static_cast<int>(i % 10), static_cast<int>((i + 1) % 10)), entities[i]);
    }
    double add_velocity_time = timer.elapsed_ms();
    print_performance_stats("添加Velocity组件", add_velocity_time, entity_count, "component");
    
    // 4. 验证操作消息
    auto msg = ecs->get_operating_message();
    if (msg) {
        std::cout << "\n✓ 所有操作成功完成！" << std::endl;
    } else {
        std::cout << "\n✗ 操作出现错误: " << msg.read_messge() << std::endl;
        return;
    }
    
    // 5. 测试组件访问性能
    std::cout << "\n正在测试组件访问性能..." << std::endl;
    timer.start();
    size_t valid_components = 0;
    for (size_t i = 0; i < entity_count; ++i) {
        auto pos = ecs->get_ptr<Position>(entities[i]);
        auto vel = ecs->get_ptr<Velocity>(entities[i]);
        if (pos && vel) {
            valid_components++;
            // 验证数据正确性
            if (pos->x != static_cast<int>(i) || pos->y != static_cast<int>(i * 2)) {
                std::cout << "✗ Position数据验证失败！索引: " << i << std::endl;
                break;
            }
            if (vel->dx != static_cast<int>(i % 10) || vel->dy != static_cast<int>((i + 1) % 10)) {
                std::cout << "✗ Velocity数据验证失败！索引: " << i << std::endl;
                break;
            }
        }
    }
    double access_time = timer.elapsed_ms();
    print_performance_stats("组件访问与验证", access_time, entity_count, "entity");
    std::cout << "  有效组件对数量: " << valid_components << " / " << entity_count 
              << " (" << (valid_components * 100.0 / entity_count) << "%)" << std::endl;
    
    // 6. 测试批量删除部分实体
    const size_t delete_count = 100000;
    std::cout << "\n正在测试批量删除性能（删除前" << delete_count << "个实体）..." << std::endl;
    timer.start();
    for (size_t i = 0; i < delete_count; ++i) {
        ecs->delete_entitys(entities[i]);
    }
    double delete_time = timer.elapsed_ms();
    print_performance_stats("删除实体", delete_time, delete_count);
    
    // 7. 测试组件容器获取
    std::cout << "\n正在测试组件容器获取..." << std::endl;
    timer.start();
    auto position_set = ecs->get_single_class_set<Position>();
    auto velocity_set = ecs->get_single_class_set<Velocity>();
    auto position_vector = ecs->get_component_vector<Position>();
    auto velocity_vector = ecs->get_component_vector<Velocity>();
    double container_time = timer.elapsed_ms();
    
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "获取组件容器耗时: " << container_time << " ms" << std::endl;
    std::cout << std::defaultfloat;
    
    if (position_set && velocity_set && position_vector && velocity_vector) {
        std::cout << "Position容器大小: " << position_vector->size() << std::endl;
        std::cout << "Velocity容器大小: " << velocity_vector->size() << std::endl;
        std::cout << "剩余实体数量: " << (entity_count - delete_count) << std::endl;
    }
    
    // 8. 内存清理测试
    std::cout << "\n正在测试内存清理..." << std::endl;
    timer.start();
    ecs->delete_type_container<Position>();
    ecs->delete_type_container<Velocity>();
    double cleanup_time = timer.elapsed_ms();
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "清理组件类型容器耗时: " << cleanup_time << " ms" << std::endl;
    std::cout << std::defaultfloat;
    
    print_separator("百万级测试完成");
}

// 压力测试：频繁添加/删除组件
void stress_test() {
    print_separator("压力测试（频繁添加/删除）");
    
    auto ecs = ecs::manager::create(vao::Enable_stack_memory, ecs::ecs_option::On_different_memory_blocks);
    
    const size_t iterations = 100000; // 10万次迭代
    auto entity = ecs->create_entity();
    
    PerformanceTester timer;
    
    std::cout << "正在进行 " << iterations << " 次组件添加/删除循环..." << std::endl;
    timer.start();
    for (size_t i = 0; i < iterations; ++i) {
        ecs->add(entity, Health(static_cast<int>(i % 1000)));
        ecs->soft_remove<Health>(entity);
    }
    double stress_time = timer.elapsed_ms();
    print_performance_stats("组件添加/删除循环", stress_time, iterations * 2, "operation");
    
    
    print_separator("压力测试完成");
}

int main() 
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    try {
        print_separator("ECS框架百万级性能测试");
        std::cout << "编译标准: C++20" << std::endl;
        std::cout << "内存模式: Enable_stack_memory" << std::endl;
        std::cout << "ECS模式: On_different_memory_blocks" << std::endl;
        std::cout << "测试时间: " << __DATE__ << " " << __TIME__ << std::endl;
        
        // 运行百万级实体测试
        test_million_entities();
        
        // 运行压力测试
        stress_test();
        
        print_separator("所有测试完成");
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\n✗ 测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "\n✗ 测试过程中发生未知异常！" << std::endl;
        return 1;
    }
}