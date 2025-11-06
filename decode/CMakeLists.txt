cmake_minimum_required(VERSION 3.20)
project(decoder)

add_executable(decoder decode.cc decode_formatter.cc)
target_link_libraries(decoder logger)
target_include_directories(decoder PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(decoder PRIVATE ${CMAKE_BINARY_DIR}/generated)
add_dependencies(decoder logger)
