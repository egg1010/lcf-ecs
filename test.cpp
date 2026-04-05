#include "include/component.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <windows.h>
#include <iomanip>

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
};

void print_performance_result(const std::string& operation, size_t count, double time_ms) {
    std::cout << std::fixed << std::setprecision(2);
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
    const size_t entity_count = 1000000;
    std::cout << "开始 ECS 性能测试，实体数量: " << entity_count << std::endl;
    
    ecs::manager ecss;
    
    Timer timer;
    ecss.append_preallocated_entities(entity_count);
    double prealloc_time = timer.elapsed_ms();
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "预分配 " << entity_count << " 个实体耗时 " << prealloc_time << " 毫秒" << std::endl;
    
    timer.reset();
    std::vector<entity> entities;
    entities.reserve(entity_count);
    
    for (size_t i = 0; i < entity_count; ++i) {
        entities.push_back(ecss.create_entity());
    }
    double create_time = timer.elapsed_ms();
    print_performance_result("实体创建", entity_count, create_time);
    
    ecss.reserve_component_capacity<Position>(entity_count);
    ecss.reserve_component_capacity<Velocity>(entity_count / 2);
    ecss.reserve_component_capacity<Health>(entity_count);
    ecss.reserve_component_capacity<Name>(entity_count / 10);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> pos_dist(-1000.0f, 1000.0f);
    
    std::vector<Position> positions;
    positions.reserve(entity_count);
    for (size_t i = 0; i < entity_count; ++i) {
        positions.emplace_back(pos_dist(gen), pos_dist(gen), pos_dist(gen));
    }
    
    const size_t velocity_count = entity_count / 2;
    std::uniform_real_distribution<float> vel_dist(-10.0f, 10.0f);
    
    std::vector<Velocity> velocities;
    velocities.reserve(velocity_count);
    for (size_t i = 0; i < velocity_count; ++i) {
        velocities.emplace_back(vel_dist(gen), vel_dist(gen), vel_dist(gen));
    }
    
    std::uniform_int_distribution<int> health_dist(1, 100);
    
    std::vector<Health> healths;
    healths.reserve(entity_count);
    for (size_t i = 0; i < entity_count; ++i) {
        healths.emplace_back(health_dist(gen), 100);
    }
    
    const size_t name_count = entity_count / 10;
    
    std::vector<Name> names;
    names.reserve(name_count);
    for (size_t i = 0; i < name_count; ++i) {
        names.emplace_back("Entity_" + std::to_string(i));
    }
    
    timer.reset();
    ecss.add_batch(std::span<const entity>(entities), std::span<const Position>(positions));
    double add_pos_time = timer.elapsed_ms();
    print_performance_result("Position 组件添加", entity_count, add_pos_time);
    
    timer.reset();
    ecss.add_batch(std::span<const entity>(entities.data(), velocity_count), std::span<const Velocity>(velocities));
    double add_vel_time = timer.elapsed_ms();
    print_performance_result("Velocity 组件添加", velocity_count, add_vel_time);
    
    timer.reset();
    ecss.add_batch(std::span<const entity>(entities), std::span<const Health>(healths));
    double add_health_time = timer.elapsed_ms();
    print_performance_result("Health 组件添加", entity_count, add_health_time);
    
    timer.reset();
    ecss.add_batch(std::span<const entity>(entities.data(), name_count), std::span<const Name>(names));
    double add_name_time = timer.elapsed_ms();
    print_performance_result("Name 组件添加", name_count, add_name_time);
    
    std::cout << "\n--- 组件查询性能 ---" << std::endl;
    
    timer.reset();
    size_t successful_queries = 0;
    std::uniform_int_distribution<size_t> entity_index_dist(0, entity_count - 1);
    
    const size_t query_count = 100000;
    for (size_t i = 0; i < query_count; ++i) {
        size_t idx = entity_index_dist(gen);
        auto* pos = ecss.get_ptr<Position>(entities[idx]);
        if (pos != nullptr) {
            successful_queries++;
            volatile float dummy = pos->x + pos->y + pos->z;
            (void)dummy;
        }
    }
    double query_pos_time = timer.elapsed_ms();
    print_performance_result("Position 组件查询", query_count, query_pos_time);
    std::cout << "成功查询次数: " << successful_queries << "/" << query_count << std::endl;
    
    std::cout << "\n--- 组件容器遍历 ---" << std::endl;
    timer.reset();
    size_t valid_components = 0;
    auto view = ecss.view<Position>();
    view.each([&valid_components](Position& pos) {
        valid_components++;
        volatile float dummy = pos.x + pos.y + pos.z;
        (void)dummy;
    });
    double iterate_time = timer.elapsed_ms();
    print_performance_result("Position 容器遍历", valid_components, iterate_time);
    std::cout << "有效组件数量: " << valid_components << "/" << valid_components << std::endl;
}
