cmake_minimum_required(VERSION 3.20)

project(usagec LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用 AVX2/BMI 指令集 (项目规范要求性能关键函数使用 SIMD 优化)
if(NOT MSVC)
    add_compile_options(-mavx2 -mbmi -mbmi2 -msse2)
endif()
if(MSVC)
    add_compile_options(/arch:AVX2)
endif()

# 原有的usagec可执行文件
add_executable(usagec usagec.cpp)

# 百万级性能测试可执行文件
add_executable(test test.cpp)

# AVX2 增加寄存器溢出栈开销, 提升栈大小防止溢出
if(WIN32)
    if(MSVC)
        target_link_options(test PRIVATE -Wl,/STACK:33554432)
        target_link_options(usagec PRIVATE -Wl,/STACK:33554432)
    else()
        target_link_options(test PRIVATE -Wl,--stack,33554432)
        target_link_options(usagec PRIVATE -Wl,--stack,33554432)
    endif()
endif()

target_compile_options(usagec PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_compile_options(test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)



if(WIN32)
    add_custom_command(
        TARGET usagec
        PRE_BUILD
        COMMAND taskkill /F /IM usagec.exe /T 2>nul || echo Process not running
        COMMENT "Terminating usagec.exe if it is running..."
    )
    
    add_custom_command(
        TARGET test
        PRE_BUILD
        COMMAND taskkill /F /IM test.exe /T 2>nul || echo Process not running
        COMMENT "Terminating test.exe if it is running..."
    )
    
endif()