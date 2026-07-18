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
# 可执行文件
# ==========================================================================
add_executable(usagec usagec.cpp)
add_executable(test test.cpp)

# ==========================================================================
# 栈大小: AVX2 增加寄存器溢出开销, 提升栈大小防止溢出
# ==========================================================================
if(WIN32)
    # 检测链接器类型选择栈大小标志
    get_filename_component(_linker_name "${CMAKE_LINKER}" NAME_WE)
    string(TOLOWER "${_linker_name}" _linker_lower)
    if(_linker_lower STREQUAL "lld-link")
        # clang++ + lld-link: 通过 -Wl, 传递 /STACK:
        set(_stack_flag "-Wl,/STACK:33554432")
    elseif(MSVC)
        # MSVC link.exe 直接接受 /STACK:
        set(_stack_flag "/STACK:33554432")
    else()
        # GNU ld / lld: 使用 --stack
        set(_stack_flag "-Wl,--stack,33554432")
    endif()
    target_link_options(test PRIVATE ${_stack_flag})
    target_link_options(usagec PRIVATE ${_stack_flag})
    unset(_linker_name)
    unset(_linker_lower)
    unset(_stack_flag)
elseif(APPLE)
    target_link_options(test PRIVATE -Wl,-stack_size,0x2000000)
    target_link_options(usagec PRIVATE -Wl,-stack_size,0x2000000)
endif()

# ==========================================================================
# MSVC 兼容编译器 (MSVC / clang-cl): UTF-8 源码编码
# ==========================================================================
if(MSVC)
    target_compile_options(usagec PRIVATE /utf-8)
    target_compile_options(test PRIVATE /utf-8)
endif()

# ==========================================================================
# Windows: 编译前终止正在运行的进程
# 用批处理脚本避免 Ninja 生成器的引号嵌套问题
# ==========================================================================
if(WIN32)
    file(WRITE "${CMAKE_BINARY_DIR}/kill_usagec.bat" "@taskkill /F /IM usagec.exe /T >nul 2>&1\n@exit /b 0\n")
    file(WRITE "${CMAKE_BINARY_DIR}/kill_test.bat" "@taskkill /F /IM test.exe /T >nul 2>&1\n@exit /b 0\n")
    add_custom_command(
        TARGET usagec PRE_BUILD
        COMMAND "${CMAKE_BINARY_DIR}/kill_usagec.bat"
        COMMENT "Terminating usagec.exe if it is running..."
    )
    add_custom_command(
        TARGET test PRE_BUILD
        COMMAND "${CMAKE_BINARY_DIR}/kill_test.bat"
        COMMENT "Terminating test.exe if it is running..."
    )
endif()
