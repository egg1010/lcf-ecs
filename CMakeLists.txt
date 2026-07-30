cmake_minimum_required(VERSION 3.20)

project(lcf-ecs LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

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

# 指令集: x86/x64 启用 AVX2/BMI/FMA; ARM64 使用标量回退
# MinGW GCC gotcha: -mavx2 生成 vmovdqa 需 32 字节栈对齐, 但 MinGW x64 ABI 仅保证
#   16 字节 → 随机 #GP 崩溃. GCC 不自动插入动态栈对齐 prologue.
#   解决: MinGW GCC 仅用 SSE2/BMI2; 代码内 AVX2 路径有 #ifdef __AVX2__ 标量回退.
#   MSVC /arch:AVX2 与 SysV ABI 无此问题 (自动插入对齐 prologue).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|i[3-6]86|x64")
    if(LCF_IS_MSVC OR LCF_IS_CLANG_CL)
        add_compile_options(/arch:AVX2)
    elseif(LCF_IS_MINGW)
        add_compile_options(-msse2 -mbmi -mbmi2)
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
    add_compile_options(/MP /utf-8)
endif()

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
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

# 构建选项
option(BUILD_FUNCTIONAL_TESTS "构建功能测试 (test_functional) / Build functional tests"      ON)
option(BUILD_PERF_TESTS       "构建性能测试 (test_perf) / Build performance test (full)"    ON)
option(BUILD_SCS_PERF_TESTS   "构建 single_class_set 性能测试 / Build scs perf test"        ON)
option(BUILD_MODULE_PERF_TESTS "构建各模块独立性能测试 / Build per-module perf tests"        ON)
option(BUILD_REFLECTION_TESTS "构建反射测试 / Build reflection tests"                       ON)
option(BUILD_SERIALIZATION_TESTS "构建序列化测试 / Build serialization tests"               ON)

message(STATUS "Build targets:")
message(STATUS "  test_functional              : ${BUILD_FUNCTIONAL_TESTS}")
message(STATUS "  test_perf                    : ${BUILD_PERF_TESTS}")
message(STATUS "  test_single_class_set_perf   : ${BUILD_SCS_PERF_TESTS}")
message(STATUS "  module perf tests            : ${BUILD_MODULE_PERF_TESTS}")
message(STATUS "  reflection tests             : ${BUILD_REFLECTION_TESTS}")
message(STATUS "  serialization tests          : ${BUILD_SERIALIZATION_TESTS}")

# 统一配置: include 目录, UTF-8 (MSVC), Windows 进程清理
function(lcf_add_executable target_name source_file)
    add_executable(${target_name} ${source_file})
    target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR})
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

if(BUILD_FUNCTIONAL_TESTS)
    lcf_add_executable(test_functional test/test_functional.cpp)
    lcf_add_executable(test_multi_view test/test_multi_view.cpp)
endif()

if(BUILD_PERF_TESTS)
    lcf_add_executable(test_perf test/test_perf.cpp)
endif()

if(BUILD_SCS_PERF_TESTS)
    lcf_add_executable(test_single_class_set_perf test/test_single_class_set_perf.cpp)
endif()

if(BUILD_MODULE_PERF_TESTS)
    # 容器模块
    lcf_add_executable(test_dense_perf               test/test_dense_perf.cpp)
    lcf_add_executable(test_class_pool_perf          test/test_class_pool_perf.cpp)
    lcf_add_executable(test_ring_buffer_perf         test/test_ring_buffer_perf.cpp)

    # 分配器模块
    lcf_add_executable(test_allocators_perf          test/test_allocators_perf.cpp)

    # 实体模块
    lcf_add_executable(test_entity_perf              test/test_entity_perf.cpp)
    lcf_add_executable(test_multi_block_bitmask_perf test/test_multi_block_bitmask_perf.cpp)

    # 辅助模块
    lcf_add_executable(test_type_id_perf             test/test_type_id_perf.cpp)
    lcf_add_executable(test_void_any_perf            test/test_void_any_perf.cpp)
    lcf_add_executable(test_sort_perf                test/test_sort_perf.cpp)
    lcf_add_executable(test_command_buffer_perf      test/test_command_buffer_perf.cpp)
    lcf_add_executable(test_utf8pp                   test/test_utf8pp.cpp)
    lcf_add_executable(test_utf8pp_perf              test/test_utf8pp_perf.cpp)
    lcf_add_executable(test_utf8_unicode             test/test_utf8_unicode.cpp)
    lcf_add_executable(test_utf8_view                test/test_utf8_view.cpp)
    lcf_add_executable(test_utf8_view_perf           test/test_utf8_view_perf.cpp)

    # 视图与组件模块
    lcf_add_executable(test_component_perf           test/test_component_perf.cpp)
    lcf_add_executable(test_single_view_perf         test/test_single_view_perf.cpp)
    lcf_add_executable(test_multi_view_perf          test/test_multi_view_perf.cpp)
    lcf_add_executable(test_group_perf               test/test_group_perf.cpp)
    lcf_add_executable(test_runtime_view_perf        test/test_runtime_view_perf.cpp)
    lcf_add_executable(test_reorder_perf              test/test_reorder_perf.cpp)
endif()

if(BUILD_REFLECTION_TESTS)
    lcf_add_executable(test_reflection                 test/test_reflection.cpp)
    lcf_add_executable(test_reflection_perf            test/test_reflection_perf.cpp)

    # 反射底层 part 模块测试
    lcf_add_executable(test_aggregate_reflect          test/test_aggregate_reflect.cpp)
    lcf_add_executable(test_aggregate_reflect_perf     test/test_aggregate_reflect_perf.cpp)
    lcf_add_executable(test_type_erasure               test/test_type_erasure.cpp)
    lcf_add_executable(test_type_erasure_perf          test/test_type_erasure_perf.cpp)
    lcf_add_executable(test_member_offset              test/test_member_offset.cpp)
    lcf_add_executable(test_member_offset_perf         test/test_member_offset_perf.cpp)

    # t_fun 测试
    lcf_add_executable(test_t_fun                      test/test_t_fun.cpp)
    lcf_add_executable(test_t_fun_perf                 test/test_t_fun_perf.cpp)
    lcf_add_executable(test_t_fun_as_component         test/test_t_fun_as_component.cpp)
endif()

if(BUILD_SERIALIZATION_TESTS)
    lcf_add_executable(test_json_writer                test/test_json_writer.cpp)
    lcf_add_executable(test_json_reader                test/test_json_reader.cpp)
    lcf_add_executable(test_serialization              test/test_serialization.cpp)
    lcf_add_executable(test_serialization_perf         test/test_serialization_perf.cpp)
    lcf_add_executable(test_codec_new                  test/test_codec_new.cpp)
endif()
