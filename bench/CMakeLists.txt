cmake_minimum_required(VERSION 3.16)
project(logger_bench)

add_executable(bench_logger bench_logger.cc)
target_link_libraries(bench_logger logger)

add_executable(micro_bench micro_bench.cc)
target_include_directories(micro_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/logger/third_party/protobuf/src
    ${CMAKE_SOURCE_DIR}/logger/third_party/abseil-cpp
    ${CMAKE_BINARY_DIR})
target_link_libraries(micro_bench logger libprotobuf)

add_executable(retention_test retention_test.cc)
target_link_libraries(retention_test logger)
