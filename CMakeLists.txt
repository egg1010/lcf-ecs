cmake_minimum_required(VERSION 3.20)

project(lcf-ecs LANGUAGES CXX VERSION 1.0.0)

# 编译器检测: LCF_IS_GCC / LCF_IS_CLANG / LCF_IS_MSVC / LCF_IS_MINGW
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(LCF_IS_GCC ON)
    if(WIN32 AND CMAKE_CXX_COMPILER MATCHES "mingw|MINGW")
        set(LCF_IS_MINGW ON)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(LCF_IS_CLANG ON)
    if(WIN32 AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(LCF_IS_CLANG_CL ON)
    endif()
elseif(MSVC)
    set(LCF_IS_MSVC ON)
endif()

# ====================================================================
# INTERFACE 导出: header-only 库, 消费者 3 行接入 (GCC/Clang/MSVC 通用)
#   add_subdirectory(lcf-ecs)
#   add_executable(app main.cpp)
#   target_link_libraries(app lcf-ecs)
# 消费者环境保持纯净: 纯 -O2 无指令集 flags, 无需显式链接任何库文件,
# 必需选项 (C++20 / MSVC 预处理器与编码 / clang builtins) 由本 target 注入,
# SIMD 路径经运行时检测自动回退标量
# ====================================================================
add_library(lcf-ecs INTERFACE)
target_compile_features(lcf-ecs INTERFACE cxx_std_20)
target_include_directories(lcf-ecs INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
if(LCF_IS_MSVC OR LCF_IS_CLANG_CL)
    # /Zc:preprocessor: 传统 MSVC 预处理器把 __VA_ARGS__ 视为单一参数, 破坏反射宏
    # /utf-8: 头文件含 UTF-8 中文注释, 无 BOM 源码需显式指定编码
    target_compile_options(lcf-ecs INTERFACE /Zc:preprocessor /utf-8)
endif()
if(WIN32 AND LCF_IS_CLANG AND NOT LCF_IS_CLANG_CL)
    # clang (MSVC ABI, GNU 前端): __builtin_cpu_supports 引用 __cpu_model 符号,
    # Windows 下不自动链接 clang builtins runtime, 由 INTERFACE 注入 (消费者零感知)
    target_link_options(lcf-ecs INTERFACE -lclang_rt.builtins-x86_64)
endif()

# 被其他项目 add_subdirectory 引入时: 只导出 INTERFACE target, 立即返回,
# 不设置任何全局选项/构建类型/测试目标, 消费者编译环境完全由其自身控制
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    return()
endif()

# ====================================================================
# 以下仅顶层构建 (开发测试, 原行为不变)
# ====================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 移植性基线: 禁用全部指令集 flags 与 LTO, 验证纯 -O0 (Debug) 无额外命令下
#   所有模块可编译运行, SIMD 路径经运行时检测回退标量
option(LCF_NO_ISA "禁用指令集 flags (纯 -O0 移植性基线) / Disable ISA flags for portability baseline" OFF)
if(LCF_NO_ISA)
    set(LCF_ENABLE_LTO OFF CACHE BOOL "LTO disabled by LCF_NO_ISA" FORCE)
endif()

# 指令集: x86/x64 启用 AVX2/BMI/FMA; ARM64 使用标量回退
# MinGW GCC gotcha: -mavx2 生成 vmovdqa 需 32 字节栈对齐, 但 MinGW x64 ABI 仅保证
#   16 字节 → 随机 #GP 崩溃. GCC 不自动插入动态栈对齐 prologue.
#   解决: MinGW GCC 仅用 SSE2/BMI2; 代码内 AVX2 路径有 #ifdef __AVX2__ 标量回退.
#   MSVC /arch:AVX2 与 SysV ABI 无此问题 (自动插入对齐 prologue).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|i[3-6]86|x64" AND NOT LCF_NO_ISA)
    if(LCF_IS_MSVC OR LCF_IS_CLANG_CL)
        add_compile_options(/arch:AVX2)
    elseif(LCF_IS_MINGW)
        add_compile_options(-msse2 -mbmi -mbmi2 -mpopcnt -mtune=native)
    else()
        add_compile_options(-mavx2 -mfma -mbmi -mbmi2 -msse2)
    endif()
endif()

# LTO/IPO
# MinGW GCC gotcha: CMAKE_INTERPROCEDURAL_OPTIMIZATION 默认 -flto=auto,
#   lto-wrapper 只认首个 -flto, 且 MinGW 下 MAKEFLAGS 检测并行数失败 → 串行 LTRANS.
#   解决: 不用 CMAKE IPO, 手动 -flto=N + gcc-ar; 并复制 mingw32-make.exe 为 make.exe
#   (lto-wrapper 并行 LTRANS 需调用 make, 关联 GCC Bug 110710).
include(CheckIPOSupported)
check_ipo_supported(RESULT lcf_ipo_supported OUTPUT lcf_ipo_error)
option(LCF_ENABLE_LTO "启用链接时优化 (LTO/IPO) / Enable LTO" ON)

if(lcf_ipo_supported AND LCF_ENABLE_LTO)
    include(ProcessorCount)
    ProcessorCount(LCF_LTO_JOBS)
    if(LCF_LTO_JOBS LESS 2)
        set(LCF_LTO_JOBS 2)
    endif()

    if(LCF_IS_MSVC OR LCF_IS_CLANG_CL)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        set(LCF_LTO_FLAG_DESC "/GL + /LTCG")
    elseif(LCF_IS_GCC)
        add_compile_options(-flto=${LCF_LTO_JOBS})
        add_link_options(-flto=${LCF_LTO_JOBS})
        if(CMAKE_CXX_COMPILER_AR)
            set(CMAKE_AR "${CMAKE_CXX_COMPILER_AR}" CACHE FILEPATH "LTO archiver" FORCE)
        endif()
        if(CMAKE_CXX_COMPILER_RANLIB)
            set(CMAKE_RANLIB "${CMAKE_CXX_COMPILER_RANLIB}" CACHE FILEPATH "LTO ranlib" FORCE)
        endif()
        if(LCF_IS_MINGW)
            find_program(LCF_MAKE_EXE make)
            if(NOT LCF_MAKE_EXE)
                get_filename_component(LCF_GCC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
                find_program(LCF_MINGW_MAKE_EXE mingw32-make HINTS "${LCF_GCC_BIN_DIR}")
                if(LCF_MINGW_MAKE_EXE)
                    set(LCF_MAKE_COPY "${LCF_GCC_BIN_DIR}/make.exe")
                    if(NOT EXISTS "${LCF_MAKE_COPY}")
                        execute_process(
                            COMMAND "${CMAKE_COMMAND}" -E copy "${LCF_MINGW_MAKE_EXE}" "${LCF_MAKE_COPY}"
                            RESULT_VARIABLE LCF_MAKE_COPY_RC)
                        if(LCF_MAKE_COPY_RC EQUAL 0)
                            message(STATUS "LTO: 创建 make.exe 副本于 ${LCF_GCC_BIN_DIR} (启用并行 LTRANS)")
                        else()
                            message(WARNING "LTO: 无法创建 make.exe, lto-wrapper 将串行编译 LTRANS (rc=${LCF_MAKE_COPY_RC})")
                        endif()
                    endif()
                endif()
            endif()
        endif()
        set(LCF_LTO_FLAG_DESC "-flto=${LCF_LTO_JOBS}")
    elseif(LCF_IS_CLANG)
        add_compile_options(-flto=thin)
        add_link_options(-flto=thin)
        set(LCF_LTO_FLAG_DESC "-flto=thin")
    endif()

    add_compile_definitions(LCF_HAS_LTO=1)
    message(STATUS "LTO/IPO: enabled (${LCF_LTO_FLAG_DESC}, jobs=${LCF_LTO_JOBS})")
else()
    message(STATUS "LTO/IPO: disabled (supported=${lcf_ipo_supported}, option=${LCF_ENABLE_LTO})")
endif()

# 平台与编译器选项
if(LCF_IS_MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif()

if(LCF_IS_MSVC OR LCF_IS_CLANG_CL)
    # /Zc:preprocessor: 标准预处理器, 传统 MSVC 预处理器将 __VA_ARGS__ 视为单一参数,
    # 破坏 REFLECT_FOR_EACH_DATA 等可变参数宏的正确展开
    add_compile_options(/MP /utf-8 /Zc:preprocessor)
endif()

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

if(LCF_STACK_MINIMAL)
    add_compile_definitions(LCF_MINIMAL_STACK=1)
    message(STATUS "Stack: LCF_MINIMAL_STACK=1 (大栈缓冲堆化)")
endif()

if(LCF_IS_GCC OR LCF_IS_CLANG)
    add_compile_options(-Wall -Wextra)
endif()

if(LCF_IS_MSVC)
    add_compile_options(/W4)
endif()

if(LCF_IS_MSVC OR LCF_IS_CLANG_CL)
    message(STATUS "Parallel compile: /MP (MSVC native)")
else()
    message(STATUS "Parallel compile: via build system (--parallel N)")
endif()

# 构建选项: 3 类测试 (规范第 11 条: 模块功能 / 高层封装 / ECS 性能)
option(LCF_STACK_MINIMAL     "LCF_MINIMAL_STACK=1 全模块最低栈占用 (大缓冲堆化) / Minimal stack"  OFF)
option(BUILD_MODULE_TESTS    "构建模块功能测试 (test_module_*) / Module functional tests"        ON)
option(BUILD_HIGHLEVEL_TESTS "构建高层封装测试 (test_highlevel_*) / High-level wrapper tests"     ON)
option(BUILD_PERF_TESTS      "构建 ECS 性能测试 (test_perf_*, 百万为单位) / ECS performance tests" ON)

message(STATUS "Build options:")
message(STATUS "  BUILD_MODULE_TESTS    : ${BUILD_MODULE_TESTS}")
message(STATUS "  BUILD_HIGHLEVEL_TESTS : ${BUILD_HIGHLEVEL_TESTS}")
message(STATUS "  BUILD_PERF_TESTS      : ${BUILD_PERF_TESTS}")

# 统一配置: include 目录, UTF-8 (MSVC), Windows 进程清理
function(lcf_add_executable target_name source_file)
    add_executable(${target_name} ${source_file})
    target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/test)
    # 链接 INTERFACE 库: 获得 include/ 路径与 clang builtins 链接选项
    target_link_libraries(${target_name} PRIVATE lcf-ecs)
    if(LCF_IS_MSVC OR LCF_IS_CLANG_CL)
        target_compile_options(${target_name} PRIVATE /utf-8)
    endif()
    if(WIN32)
        file(WRITE "${CMAKE_BINARY_DIR}/kill_${target_name}.bat"
             "@taskkill /F /IM ${target_name}.exe /T >nul 2>&1\n@exit /b 0\n")
        add_custom_command(
            TARGET ${target_name} PRE_BUILD
            COMMAND "${CMAKE_BINARY_DIR}/kill_${target_name}.bat"
            COMMENT "Terminating ${target_name}.exe if it is running..."
        )
    endif()
endfunction()

# 3 类测试: 按类别自动发现源文件并注册可执行目标
#   test_module_*    模块功能测试 (part 层)
#   test_highlevel_* 高层封装测试 (ecs 层)
#   test_perf_*      ECS 性能测试 (百万为单位, 含缓存命中率)
# 辅助: 按 glob 注册 target, 并把 target 名累加到 out_var
function(lcf_add_test_sources pattern out_var)
    file(GLOB _srcs CONFIGURE_DEPENDS ${pattern})
    set(_targets ${${out_var}})
    foreach(_src ${_srcs})
        get_filename_component(_name ${_src} NAME_WE)
        lcf_add_executable(${_name} ${_src})
        list(APPEND _targets ${_name})
    endforeach()
    set(${out_var} ${_targets} PARENT_SCOPE)
endfunction()

set(LCF_MODULE_TARGETS)
set(LCF_HIGHLEVEL_TARGETS)
set(LCF_PERF_TARGETS)

if(BUILD_MODULE_TESTS)
    lcf_add_test_sources("${CMAKE_SOURCE_DIR}/test/*/test_module_*.cpp" LCF_MODULE_TARGETS)
endif()
if(BUILD_HIGHLEVEL_TESTS)
    lcf_add_test_sources("${CMAKE_SOURCE_DIR}/test/*/test_highlevel_*.cpp" LCF_HIGHLEVEL_TARGETS)
endif()
if(BUILD_PERF_TESTS)
    lcf_add_test_sources("${CMAKE_SOURCE_DIR}/test/*/test_perf_*.cpp" LCF_PERF_TARGETS)
endif()

# 类别聚合目标: 一次构建整类测试 (test_module / test_highlevel / test_perf)
if(LCF_MODULE_TARGETS)
    add_custom_target(test_module DEPENDS ${LCF_MODULE_TARGETS})
endif()
if(LCF_HIGHLEVEL_TARGETS)
    add_custom_target(test_highlevel DEPENDS ${LCF_HIGHLEVEL_TARGETS})
endif()
if(LCF_PERF_TARGETS)
    add_custom_target(test_perf DEPENDS ${LCF_PERF_TARGETS})
endif()

list(LENGTH LCF_MODULE_TARGETS    _n_mod)
list(LENGTH LCF_HIGHLEVEL_TARGETS _n_hl)
list(LENGTH LCF_PERF_TARGETS      _n_perf)
math(EXPR LCF_TEST_COUNT "${_n_mod} + ${_n_hl} + ${_n_perf}")
message(STATUS "  test targets : module=${_n_mod}  highlevel=${_n_hl}  perf=${_n_perf}  (total=${LCF_TEST_COUNT})")

# ============================================================================
# Unicode 数据重新生成 (手动触发, 不加入常规构建)
#   用法: cmake --build build --target gen_unicode_data
#   需本机可执行 python, 并能访问 https://www.unicode.org
# ============================================================================
find_program(PYTHON_EXECUTABLE python python3)
if(PYTHON_EXECUTABLE)
    add_custom_target(gen_unicode_data
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/include/part/utf8pp/gen_unicode_data.py
                --output ${CMAKE_SOURCE_DIR}/include/part/utf8pp/unicode_data.hpp
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Regenerating unicode_data.hpp from latest UCD"
        VERBATIM
    )
    message(STATUS "Custom target: gen_unicode_data (手动运行以更新 Unicode 数据)")
endif()
