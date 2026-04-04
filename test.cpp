#include "include/component.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <windows.h>
#include <iomanip> // 新增：用于格式化输出

// 组件定义
struct Position {
    float x, y, z;
    Position(float x = 0.0f, float y = 0.0f, float z = 0.0f) : x(x), y(y), z(z) {}
};

struct Velocity {
    float vx, vy, vz;
    Velocity(float vx = 0.0f, float vy = 0.0f, float vz = 0.0f) : vx(vx), vy(vy), vz(vz) {}
};

struct Health {
    int current;
    int max;
    Health(int current = 100, int max = 100) : current(current), max(max) {}
};

struct Name {
    std::string value;
    Name(const std::string& name = "Entity") : value(name) {}
};

// 性能计时器
class Timer {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    
public:
    Timer() { reset(); }
    
    void reset() {
        start_time = std::chrono::high_resolution_clock::now();
    }
    
    double elapsed_ms() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
    
    double elapsed_seconds() const {
        return elapsed_ms() / 1000.0;
    }
};

void print_performance_result(const std::string& operation, size_t count, double time_ms) {
    std::cout << std::fixed << std::setprecision(2);
    // 使用 setw 控制对齐，使输出更整齐
    std::cout << std::left << std::setw(25) << operation 
              << ": " << std::right << std::setw(8) << count 
              << " 次操作耗时 " << std::setw(8) << time_ms << " 毫秒 ("
              << std::setw(10) << (static_cast<double>(count) / time_ms) << " 次/毫秒, "
              << std::setw(12) << (static_cast<double>(count) / (time_ms / 1000.0)) << " 次/秒)" << std::endl;
}

int main() 
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const size_t entity_count = 1000000; // 百万级实体
    std::cout << "开始 ECS 性能测试，实体数量: " << entity_count << std::endl;
    
    // 初始化 ECS 管理器
    ecs::manager ecss;
    
    // 预分配实体（提高性能）
    Timer timer;
    ecss.append_preallocated_entities(entity_count);
    double prealloc_time = timer.elapsed_ms();
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "预分配 " << entity_count << " 个实体耗时 " << prealloc_time << " 毫秒" << std::endl;
    
    // 创建实体
    timer.reset();
    std::vector<entity> entities;
    entities.reserve(entity_count);
    
    for (size_t i = 0; i < entity_count; ++i) {
        entities.push_back(ecss.create_entity());
    }
    double create_time = timer.elapsed_ms();
    print_performance_result("实体创建", entity_count, create_time);
    
    // 预分配组件容器容量（关键性能优化）
    ecss.reserve_component_capacity<Position>(entity_count);
    ecss.reserve_component_capacity<Velocity>(entity_count / 2); // Velocity只添加50万次
    ecss.reserve_component_capacity<Health>(entity_count);
    ecss.reserve_component_capacity<Name>(entity_count / 10);   // Name只添加10万次

    // 添加 Position 组件
    timer.reset();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> pos_dist(-1000.0f, 1000.0f);
    
    for (size_t i = 0; i < entity_count; ++i) {
        ecss.add(entities[i], Position(pos_dist(gen), pos_dist(gen), pos_dist(gen)));
    }
    double add_pos_time = timer.elapsed_ms();
    print_performance_result("Position 组件添加", entity_count, add_pos_time);
    
    // 添加 Velocity 组件（50% 的实体）
    timer.reset();
    const size_t velocity_count = entity_count / 2;
    std::uniform_real_distribution<float> vel_dist(-10.0f, 10.0f);
    
    for (size_t i = 0; i < velocity_count; ++i) {
        ecss.add(entities[i], Velocity(vel_dist(gen), vel_dist(gen), vel_dist(gen)));
    }
    double add_vel_time = timer.elapsed_ms();
    print_performance_result("Velocity 组件添加", velocity_count, add_vel_time);
    
    // 添加 Health 组件（所有实体）
    timer.reset();
    std::uniform_int_distribution<int> health_dist(1, 100);
    
    for (size_t i = 0; i < entity_count; ++i) {
        ecss.add(entities[i], Health(health_dist(gen), 100));
    }
    double add_health_time = timer.elapsed_ms();
    print_performance_result("Health 组件添加", entity_count, add_health_time);
    
    // 添加 Name 组件（10% 的实体）
    timer.reset();
    const size_t name_count = entity_count / 10;
    
    for (size_t i = 0; i < name_count; ++i) {
        ecss.add(entities[i], Name("Entity_" + std::to_string(i)));
    }
    double add_name_time = timer.elapsed_ms();
    print_performance_result("Name 组件添加", name_count, add_name_time);
    
    // 查询组件性能测试
    std::cout << "\n--- 组件查询性能 ---" << std::endl;
    
    // 随机查询 Position 组件
    timer.reset();
    size_t successful_queries = 0;
    std::uniform_int_distribution<size_t> entity_index_dist(0, entity_count - 1);
    
    const size_t query_count = 100000; // 10万次查询
    for (size_t i = 0; i < query_count; ++i) {
        size_t idx = entity_index_dist(gen);
        auto* pos = ecss.get_ptr<Position>(entities[idx]);
        if (pos != nullptr) {
            successful_queries++;
            // 简单使用组件数据以防止编译器优化掉
            volatile float dummy = pos->x + pos->y + pos->z;
            (void)dummy;
        }
    }
    double query_pos_time = timer.elapsed_ms();
    print_performance_result("Position 组件查询", query_count, query_pos_time);
    std::cout << "成功查询次数: " << successful_queries << "/" << query_count << std::endl;
    
    // 获取组件容器并遍历
    std::cout << "\n--- 组件容器遍历 ---" << std::endl;
    timer.reset();
    auto pos_vector = ecss.get_component_vector<Position>();
    if (pos_vector) {
        size_t valid_components = 0;
        for (auto& comp : *pos_vector) {
            auto* pos = comp.get_ptr<Position>();
            if (pos != nullptr) {
                valid_components++;
                // 使用组件数据
                volatile float dummy = pos->x + pos->y + pos->z;
                (void)dummy;
            }
        }
        double iterate_time = timer.elapsed_ms();
        print_performance_result("Position 容器遍历", pos_vector->size(), iterate_time);
        std::cout << "有效组件数量: " << valid_components << "/" << pos_vector->size() << std::endl;
    }
}