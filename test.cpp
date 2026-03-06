#include "include/component.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <algorithm>

// 测试组件定义
struct Position {
    float x, y, z;
    Position() : x(0.0f), y(0.0f), z(0.0f) {}
    Position(float x, float y, float z) : x(x), y(y), z(z) {}
};

struct Velocity {
    float vx, vy, vz;
    Velocity() : vx(0.0f), vy(0.0f), vz(0.0f) {}
    Velocity(float vx, float vy, float vz) : vx(vx), vy(vy), vz(vz) {}
};

struct Health {
    int current;
    int maxss;
    Health() : current(100), maxss(100) {}
    Health(int current, int maxs) : current(current), maxss(maxs) {}
};

// 性能测试工具类
class PerformanceTester {
public:
    static void print_time(const std::string& operation, const std::chrono::high_resolution_clock::time_point& start, 
                          const std::chrono::high_resolution_clock::time_point& end, size_t count = 0) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double seconds = duration.count() / 1000000.0;
        std::cout << operation << ": " << duration.count() << " microseconds";
        if (count > 0) {
            std::cout << " (" << static_cast<size_t>(count / seconds) << " ops/sec, " << (seconds / count * 1000000) << " us/op)";
        }
        std::cout << std::endl;
    }
};

// 百万级实体测试
void test_million_entities() {
    const size_t entity_count = 1000000; // 100万实体
    std::cout << "=== 百万级ECS性能测试 ===" << std::endl;
    std::cout << "测试实体数量: " << entity_count << std::endl;
    
    // 使用独立内存块模式进行测试（避免全局状态影响）
    auto ecs = ecs::manager::create(vao::Enable_stack_memory, ecs::ecs_option::On_different_memory_blocks);
    
    // 1. 创建百万实体
    std::cout << "\n1. 创建 " << entity_count << " 个实体..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<entity> entities;
    entities.reserve(entity_count);
    
    for (size_t i = 0; i < entity_count; ++i) {
        entities.push_back(ecs->create_entity());
    }
    auto end = std::chrono::high_resolution_clock::now();
    PerformanceTester::print_time("实体创建", start, end, entity_count);
    
    // 2. 为所有实体添加Position组件
    std::cout << "\n2. 为所有实体添加Position组件..." << std::endl;
    start = std::chrono::high_resolution_clock::now();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> pos_dist(-1000.0f, 1000.0f);
    
    for (size_t i = 0; i < entity_count; ++i) {
        ecs->add(entities[i], Position(pos_dist(gen), pos_dist(gen), pos_dist(gen)));
    }
    end = std::chrono::high_resolution_clock::now();
    PerformanceTester::print_time("Position组件添加", start, end, entity_count);
    
    // 3. 为50%的实体添加Velocity组件
    std::cout << "\n3. 为 " << (entity_count / 2) << " 个实体添加Velocity组件..." << std::endl;
    start = std::chrono::high_resolution_clock::now();
    std::uniform_real_distribution<float> vel_dist(-10.0f, 10.0f);
    
    for (size_t i = 0; i < entity_count / 2; ++i) {
        ecs->add(entities[i], Velocity(vel_dist(gen), vel_dist(gen), vel_dist(gen)));
    }
    end = std::chrono::high_resolution_clock::now();
    PerformanceTester::print_time("Velocity组件添加", start, end, entity_count / 2);
    
    // 4. 为25%的实体添加Health组件
    std::cout << "\n4. 为 " << (entity_count / 4) << " 个实体添加Health组件..." << std::endl;
    start = std::chrono::high_resolution_clock::now();
    std::uniform_int_distribution<int> health_dist(1, 100);
    
    for (size_t i = 0; i < entity_count / 4; ++i) {
        ecs->add(entities[i], Health(health_dist(gen), 100));
    }
    end = std::chrono::high_resolution_clock::now();
    PerformanceTester::print_time("Health组件添加", start, end, entity_count / 4);
    
    // 5. 随机查询组件性能测试
    std::cout << "\n5. 随机查询 " << (entity_count / 10) << " 次Position组件..." << std::endl;
    start = std::chrono::high_resolution_clock::now();
    std::uniform_int_distribution<size_t> index_dist(0, entity_count - 1);
    size_t successful_queries = 0;
    
    for (size_t i = 0; i < entity_count / 10; ++i) {
        size_t idx = index_dist(gen);
        Position* pos = ecs->get_ptr<Position>(entities[idx]);
        if (pos != nullptr) {
            successful_queries++;
            // 简单使用组件数据
            pos->x += 0.1f;
        }
    }
    end = std::chrono::high_resolution_clock::now();
    PerformanceTester::print_time("Position组件查询", start, end, entity_count / 10);
    std::cout << "成功查询次数: " << successful_queries << std::endl;
    
    // 6. 遍历所有Position组件
    std::cout << "\n6. 遍历所有Position组件..." << std::endl;
    auto pos_set = ecs->get_single_class_set<Position>();
    if (pos_set) {
        start = std::chrono::high_resolution_clock::now();
        size_t processed = 0;
        for (auto& obj : *pos_set) {
            Position* pos = obj.get_ptr<Position>();
            if (pos) {
                pos->x *= 1.01f; // 简单处理
                processed++;
            }
        }
        end = std::chrono::high_resolution_clock::now();
        PerformanceTester::print_time("Position组件遍历", start, end, processed);
        std::cout << "处理组件数量: " << processed << std::endl;
    }
    
    // 7. 删除部分实体和组件
    std::cout << "\n7. 删除 " << (entity_count / 10) << " 个实体的Health组件..." << std::endl;
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < entity_count / 10; ++i) {
        ecs->remove<Health>(entities[i]);
    }
    end = std::chrono::high_resolution_clock::now();
    PerformanceTester::print_time("Health组件删除", start, end, entity_count / 10);
    
    // 8. 完全删除部分实体
    std::cout << "\n8. 完全删除 " << (entity_count / 100) << " 个实体..." << std::endl;
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < entity_count / 100; ++i) {
        ecs->hard_delete_entitys(entities[i]);
    }
    end = std::chrono::high_resolution_clock::now();
    PerformanceTester::print_time("实体完全删除", start, end, entity_count / 100);
    
    // 9. 内存使用情况（通过容器大小估算）
    std::cout << "\n9. 内存使用情况估算:" << std::endl;
    if (auto pos_vec = ecs->get_component_vector<Position>()) {
        std::cout << "Position组件数量: " << pos_vec->size() << std::endl;
    }
    if (auto vel_vec = ecs->get_component_vector<Velocity>()) {
        std::cout << "Velocity组件数量: " << vel_vec->size() << std::endl;
    }
    if (auto health_vec = ecs->get_component_vector<Health>()) {
        std::cout << "Health组件数量: " << health_vec->size() << std::endl;
    }
    
    std::cout << "\n=== 测试完成 ===" << std::endl;
}

// 压力测试：大量实体的创建和销毁循环
void stress_test() {
    const size_t batch_size = 100000; // 每批10万实体
    const size_t iterations = 10;     // 10轮
    
    std::cout << "\n=== 压力测试 ===" << std::endl;
    std::cout << "每批实体数量: " << batch_size << ", 轮数: " << iterations << std::endl;
    
    auto ecs = ecs::manager::create(vao::Enable_stack_memory, ecs::ecs_option::On_different_memory_blocks);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; ++iter) {
        // 创建一批实体并添加组件
        std::vector<entity> entities;
        entities.reserve(batch_size);
        
        for (size_t i = 0; i < batch_size; ++i) {
            entity e = ecs->create_entity();
            entities.push_back(e);
            ecs->add(e, Position(static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3)));
            if (i % 2 == 0) {
                ecs->add(e, Velocity(1.0f, 2.0f, 3.0f));
            }
        }
        
        // 查询一些组件
        for (size_t i = 0; i < batch_size; i += 1000) {
            Position* pos = ecs->get_ptr<Position>(entities[i]);
            if (pos) {
                pos->x += iter;
            }
        }
        
        // 完全删除这批实体
        for (auto& e : entities) {
            ecs->hard_delete_entitys(e);
        }
        
        std::cout << "完成第 " << (iter + 1) << " 轮" << std::endl;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    PerformanceTester::print_time("压力测试总时间", start, end);
    
    std::cout << "=== 压力测试完成 ===" << std::endl;
}

int main() 
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        std::cout << "开始ECS百万级性能测试..." << std::endl;
        
        // 运行百万级实体测试
        test_million_entities();
        
        // 运行压力测试
        stress_test();
        
        std::cout << "\n所有测试完成！" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "测试过程中发生未知异常" << std::endl;
        return 1;
    }
}