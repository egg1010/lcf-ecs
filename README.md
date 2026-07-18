## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for more details.


# Minimum compilation standard c++20
# A lightweight ECS library
---
# Environment setup copies the include folder into the project
---
# Detailed usage instructions are in usage.md and usagec.cpp

## Build

### Prerequisites
- CMake 3.20+
- C++20 compiler: GCC (MinGW) / MSVC / Clang

### Windows (MinGW)
```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows (MSVC)
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Linux (GCC / Clang)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS (Clang)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

x86/x64 自动启用 AVX2/BMI 指令集, ARM64 (Apple Silicon 等) 使用标量回退。



# 最低编译标准c++20
# 一个轻量级ecs库
---
# 环境配置把include文件夹复制到项目中
---
# 具体使用方法在usage.md和usagec.cpp

## 构建

### 前置条件
- CMake 3.20+
- C++20 编译器: GCC (MinGW) / MSVC / Clang

### Windows (MinGW)
```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows (MSVC)
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Linux (GCC / Clang)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS (Clang)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

x86/x64 自动启用 AVX2/BMI 指令集, ARM64 (Apple Silicon 等) 使用标量回退。
