cmake_minimum_required(VERSION 3.20)

project(usagec LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 原有的usagec可执行文件
add_executable(usagec usagec.cpp)

target_compile_options(usagec PRIVATE)



# 百万级性能测试可执行文件
add_executable(test test.cpp)


target_compile_options(test PRIVATE)


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