cmake_minimum_required(VERSION 3.10)

# 禁用CryptoPP测试和文档构建
option(BUILD_TESTING "Build cryptopp tests" OFF)
option(BUILD_DOCUMENTATION "Build cryptopp documentation" OFF)

# 禁用汇编优化以避免符号缺失问题和简化构建过程
option(CRYPTOPP_DISABLE_ASM "Disable assembly optimizations" ON)

project(cryptopp)

# 添加所有源文件
file(GLOB CRYPTOPP_SOURCES *.cpp)

# 创建静态库
add_library(cryptopp STATIC ${CRYPTOPP_SOURCES})

# 为CryptoPP单独添加MSVC警告禁用选项
if(MSVC)
    target_compile_options(cryptopp PRIVATE 
        /wd4996  # 禁用stdext弃用警告
        /wd4244  # 禁用类型转换警告
        /W0      # 关闭所有警告
        /utf-8   # 使用UTF-8编码
    )
endif()

# 设置包含目录
target_include_directories(cryptopp PUBLIC .)

# 设置编译选项
if(MSVC)
    target_compile_options(cryptopp PRIVATE /W4 /utf-8)
else()
    target_compile_options(cryptopp PRIVATE -Wall -Wextra -Werror)
endif()




