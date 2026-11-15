cmake_minimum_required(VERSION 3.20)
project(decoder)

add_executable(decoder decode.cc decode_formatter.cc)
# decode.cc 直接包含了 effective_msg.pb.h，需要 protobuf/abseil 的头文件路径；
# logger 是以 PRIVATE 方式链接 protobuf 的，这些路径不会自动传过来。
target_include_directories(decoder PRIVATE
    ${CMAKE_SOURCE_DIR}/logger/third_party/protobuf/src
    ${CMAKE_SOURCE_DIR}/logger/third_party/abseil-cpp
    ${CMAKE_BINARY_DIR})
target_link_libraries(decoder logger libprotobuf)
