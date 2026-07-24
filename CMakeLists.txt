cmake_minimum_required(VERSION 3.20)

project(usagec LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ==========================================================================
# 指令集: 仅 x86/x64 启用 AVX2/BMI (ARM64/Apple Silicon 不支持)
# ==========================================================================
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|i[3-6]86|x64")
    if(MSVC)
        add_compile_options(/arch:AVX2)
    else()
        add_compile_options(-mavx2 -mbmi -mbmi2 -msse2)
    endif()
endif()

# ==========================================================================
# MinGW (Windows + GCC) 需要大对象文件支持
# ==========================================================================
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wa,-mbig-obj)
endif()

# ==========================================================================
# 并行编译: 编译器原生多线程编译支持
# MSVC: /MP (cl.exe 单次调用并行编译多个源文件)
# GCC/Clang: 编译器本身单文件编译单线程, 并行由构建系统负责 (cmake --build --parallel N)
# ==========================================================================
if(MSVC)
    add_compile_options(/MP)
    message(STATUS "Parallel compile: enabled (/MP)")
else()
    message(STATUS "Parallel compile: via build system (--parallel N)")
endif()

# ==========================================================================
# 构建选项: 可独立控制每个目标的构建
#   cmake -DBUILD_USAGEC=OFF -DBUILD_FUNCTIONAL_TESTS=OFF -DBUILD_PERF_TESTS=OFF ..
# ==========================================================================
option(BUILD_USAGEC "构建 usagec 示例程序" ON)
option(BUILD_FUNCTIONAL_TESTS "构建功能测试 (test_functional)" ON)
option(BUILD_PERF_TESTS "构建性能测试 (test_perf)" ON)
option(BUILD_DENSE_TESTS "构建 dense<T> 容器功能测试 (test_dense)" ON)
option(BUILD_DV_VS_VEC "构建 dense_view vs vector 对比测试 (test_dense_view_vs_vector)" ON)

message(STATUS "Build targets:")
message(STATUS "  usagec              : ${BUILD_USAGEC}")
message(STATUS "  test_functional     : ${BUILD_FUNCTIONAL_TESTS}")
message(STATUS "  test_perf           : ${BUILD_PERF_TESTS}")
message(STATUS "  test_dense          : ${BUILD_DENSE_TESTS}")
message(STATUS "  test_dense_view_vs_vector : ${BUILD_DV_VS_VEC}")

# ==========================================================================
# 可执行文件: usagec 示例程序
# ==========================================================================
if(BUILD_USAGEC)
    add_executable(usagec usagec.cpp)
    if(MSVC)
        target_compile_options(usagec PRIVATE /utf-8)
    endif()
    if(WIN32)
        file(WRITE "${CMAKE_BINARY_DIR}/kill_usagec.bat"
             "@taskkill /F /IM usagec.exe /T >nul 2>&1\n@exit /b 0\n")
        add_custom_command(
            TARGET usagec PRE_BUILD
            COMMAND "${CMAKE_BINARY_DIR}/kill_usagec.bat"
            COMMENT "Terminating usagec.exe if it is running..."
        )
    endif()
endif()

# ==========================================================================
# 可执行文件: 功能测试 (test_functional)
# ==========================================================================
if(BUILD_FUNCTIONAL_TESTS)
    add_executable(test_functional test/test_functional.cpp)
    target_include_directories(test_functional PRIVATE ${CMAKE_SOURCE_DIR})
    if(MSVC)
        target_compile_options(test_functional PRIVATE /utf-8)
    endif()
    if(WIN32)
        file(WRITE "${CMAKE_BINARY_DIR}/kill_test_functional.bat"
             "@taskkill /F /IM test_functional.exe /T >nul 2>&1\n@exit /b 0\n")
        add_custom_command(
            TARGET test_functional PRE_BUILD
            COMMAND "${CMAKE_BINARY_DIR}/kill_test_functional.bat"
            COMMENT "Terminating test_functional.exe if it is running..."
        )
    endif()
endif()

# ==========================================================================
# 可执行文件: 性能测试 (test_perf)
# ==========================================================================
if(BUILD_PERF_TESTS)
    add_executable(test_perf test/test_perf.cpp)
    target_include_directories(test_perf PRIVATE ${CMAKE_SOURCE_DIR})
    if(MSVC)
        target_compile_options(test_perf PRIVATE /utf-8)
    endif()
    if(WIN32)
        file(WRITE "${CMAKE_BINARY_DIR}/kill_test_perf.bat"
             "@taskkill /F /IM test_perf.exe /T >nul 2>&1\n@exit /b 0\n")
        add_custom_command(
            TARGET test_perf PRE_BUILD
            COMMAND "${CMAKE_BINARY_DIR}/kill_test_perf.bat"
            COMMENT "Terminating test_perf.exe if it is running..."
        )
    endif()
endif()

# ==========================================================================
# 可执行文件: dense<T> 容器功能测试
# ==========================================================================
if(BUILD_DENSE_TESTS)
    add_executable(test_dense test/test_dense.cpp)
    target_include_directories(test_dense PRIVATE ${CMAKE_SOURCE_DIR})
    if(MSVC)
        target_compile_options(test_dense PRIVATE /utf-8)
    endif()
    if(WIN32)
        file(WRITE "${CMAKE_BINARY_DIR}/kill_test_dense.bat"
             "@taskkill /F /IM test_dense.exe /T >nul 2>&1\n@exit /b 0\n")
        add_custom_command(
            TARGET test_dense PRE_BUILD
            COMMAND "${CMAKE_BINARY_DIR}/kill_test_dense.bat"
            COMMENT "Terminating test_dense.exe if it is running..."
        )
    endif()
endif()

# ==========================================================================
# 可执行文件: dense_view vs std::vector 对比测试
# ==========================================================================
if(BUILD_DV_VS_VEC)
    add_executable(test_dense_view_vs_vector test/test_dense_view_vs_vector.cpp)
    target_include_directories(test_dense_view_vs_vector PRIVATE ${CMAKE_SOURCE_DIR})
    if(MSVC)
        target_compile_options(test_dense_view_vs_vector PRIVATE /utf-8)
    endif()
    if(WIN32)
        file(WRITE "${CMAKE_BINARY_DIR}/kill_test_dense_view_vs_vector.bat"
             "@taskkill /F /IM test_dense_view_vs_vector.exe /T >nul 2>&1\n@exit /b 0\n")
        add_custom_command(
            TARGET test_dense_view_vs_vector PRE_BUILD
            COMMAND "${CMAKE_BINARY_DIR}/kill_test_dense_view_vs_vector.bat"
            COMMENT "Terminating test_dense_view_vs_vector.exe if it is running..."
        )
    endif()
endif()
