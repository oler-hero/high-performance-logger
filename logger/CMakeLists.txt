include_guard(GLOBAL)

include_directories(. third_party/cryptopp third_party/fmt/include third_party/zlib third_party/protobuf/src)

# Use the vendored Abseil copy instead of downloading it during configure.
set(FETCHCONTENT_SOURCE_DIR_ABSL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp" CACHE PATH "Vendored Abseil sources")

add_subdirectory(third_party/cryptopp)
# 恢复使用项目protobuf
add_subdirectory(third_party/protobuf/cmake protobuf)
add_subdirectory(third_party/zstd/build/cmake zstd)
add_subdirectory(third_party/zlib)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MMAP_SRCS mmap/mmap_aux.cc mmap/mmap_linux.cc)
    set(UTIL_SRCS utils/sys_util_linux.cc utils/sys_util.cc utils/file_util.cc)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(MMAP_SRCS mmap/mmap_aux.cc mmap/mmap_win.cc)
    set(UTIL_SRCS utils/sys_util_win.cc utils/sys_util.cc utils/file_util.cc)
else()
    message(FATAL_ERROR "unsupported.")
endif()

# 添加protobuf编译命令
set(PROTOC ${CMAKE_BINARY_DIR}/logger/protobuf/Debug/protoc.exe)
if(EXISTS ${PROTOC})
    # 使用add_custom_command确保在构建时生成pb文件
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/effective_msg.pb.cc ${CMAKE_BINARY_DIR}/effective_msg.pb.h
        COMMAND ${PROTOC} --cpp_out=${CMAKE_BINARY_DIR} -I=logger/proto logger/proto/effective_msg.proto
        DEPENDS logger/proto/effective_msg.proto
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating protobuf files"
    )
else()
    message(WARNING "protoc not found, effective_msg.pb.cc will not be generated")
endif()

# 检查protobuf文件是否存在，如果不存在则跳过
if(EXISTS ${CMAKE_BINARY_DIR}/effective_msg.pb.cc)
    set(PB_SRCS ${CMAKE_BINARY_DIR}/effective_msg.pb.cc)
else()
    message(WARNING "effective_msg.pb.cc not found, some features may be disabled")
    set(PB_SRCS "")
endif()

set(FORMATTER_SRCS formatter/default_formatter.cc formatter/effective_formatter.cc)
set(SINK_SRCS sinks/console_sink.cc sinks/effective_sink.cc)
set(COMPRESS_SRCS compress/zlib_compress.cc compress/zstd_compress.cc)
set(CRYPT_SRCS crypt/aes_crypt.cc crypt/crypt.cc)
set(CONTEXT_SRCS context/context.cc context/executor.cc context/thread_pool.cc)

set(SRCS
    log_factory.cc
    log_handle.cc
    log_msg.cc
    ${PB_SRCS}
    ${MMAP_SRCS}
    ${UTIL_SRCS}
    ${FORMATTER_SRCS}
    ${SINK_SRCS}
    ${COMPRESS_SRCS}
    ${CRYPT_SRCS}
    ${CONTEXT_SRCS}
)

message(STATUS "libs ${LIBS}")

add_library(logger STATIC ${SRCS})
# 恢复使用项目protobuf链接
target_link_libraries(logger PRIVATE ${LIBS} cryptopp libprotobuf libzstd_static zlib)
