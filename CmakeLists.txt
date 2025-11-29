cmake_minimum_required(VERSION 3.20)

set(PROJECT_NAME "logger")

project(${PROJECT_NAME} VERSION 1.1.1.1)

add_definitions(-DFMT_HEADER_ONLY=1)
add_definitions(-DENABLE_LOG=1)

# 注释掉系统protobuf查找，使用项目内的protobuf
# find_package(Protobuf REQUIRED)

# 恢复项目内protobuf包含路径
set(INCDIRS . ${CMAKE_BINARY_DIR} logger logger/third_party/protobuf/src logger/third_party/fmt/include third_party/zlib logger/third_party/zlib logger/third_party/zstd/lib)
include_directories(${INCDIRS})

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    set(LIBS ${LIBS} Kernel32)
    # add_compile_options(/source-charset:utf-8 /execution-charset:utf-8)
    
    # 禁用各种警告
    add_compile_options(/wd4996)  # 禁用C4996警告（stdext弃用）
    add_compile_options(/wd4244)  # 禁用C4244警告（类型转换可能丢失数据）
    add_compile_options(/wd4267)  # 禁用size_t转换警告
    add_compile_options(/wd4018)  # 禁用有符号/无符号比较警告
    
    # 禁用所有警告作为错误
    add_compile_options(/W0)  # 关闭所有警告
    
    # generate pdb file
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE
        "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /std:c11")
    add_compile_options(/utf-8)
else()
    set(LIBS ${LIBS} pthread)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -fPIC")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -fPIC")
endif()

message(STATUS "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")

if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    add_definitions(-DDEBUG)
else()
    add_definitions(-DNDEBUG)
endif()

add_subdirectory(logger)
add_subdirectory(example)
add_subdirectory(decode)
