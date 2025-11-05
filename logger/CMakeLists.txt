# ===================== logger library =====================

# 平台特定源文件
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(UTIL_SRCS utils/sys_util_win.cc)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(UTIL_SRCS utils/sys_util_linux.cc)
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# 基础源文件（可按需扩展）
set(LOGGER_SOURCES
    log_handle.cc
    log_msg.cc
    formatter/default_formatter.cc
    sinks/console_sink.cc
    compress/zstd_compress.cc
    crypt/aes_crypt.cc
    crypt/crypt.cc
    ${UTIL_SRCS}
)

add_library(logger STATIC ${LOGGER_SOURCES})
target_include_directories(logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cryptopp")
  # 暴露 Crypto++ 頭文件
  target_include_directories(logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cryptopp)
  # 編譯並鏈接本地 Crypto++ 源碼為靜態庫
  file(GLOB CRYPTOPP_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cryptopp/*.cpp")
  if(CRYPTOPP_SOURCES)
    add_library(cryptopp_static STATIC ${CRYPTOPP_SOURCES})
    target_include_directories(cryptopp_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cryptopp)
    # 某些 Windows/MinGW 組合下可選關閉匯編以提升兼容性
    if(MINGW)
      target_compile_definitions(cryptopp_static PUBLIC CRYPTOPP_DISABLE_ASM=1)
    endif()
    target_link_libraries(logger PRIVATE cryptopp_static)
    message(STATUS "✓ cryptopp 已編譯並鏈接 (vendored)")
  else()
    message(WARNING "✗ 未找到 Crypto++ 源文件，可能導致鏈接錯誤")
  endif()
endif()

# 输出编译信息
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Logger sources: ${LOGGER_SOURCES}")


# ==================== 第三方库集成 ====================

# 选项：优先使用 third_party 源码，找不到时回退 find_package
option(USE_VENDORED_ZLIB "Use third_party/zlib if present" ON)
option(USE_VENDORED_ZSTD "Use third_party/zstd if present" ON)
option(USE_VENDORED_FMT "Use third_party/fmt if present" ON)

# -------------------- fmt 配置 --------------------
set(FMT_LIB_TARGET "")
if(USE_VENDORED_FMT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt/CMakeLists.txt")
  message(STATUS "使用本地源码构建 fmt")
  add_subdirectory(third_party/fmt)
  set(FMT_LIB_TARGET fmt::fmt)
else()
  find_package(fmt QUIET)
  if(fmt_FOUND)
    set(FMT_LIB_TARGET fmt::fmt)
  endif()
endif()

# -------------------- zlib 配置 --------------------
set(ZLIB_LIB_TARGET "")
set(ZLIB_INCLUDE_DIR "")
if(USE_VENDORED_ZLIB AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/CMakeLists.txt")
  message(STATUS "使用本地源码构建 zlib")
  set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/zlib)
  set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib" "${CMAKE_BINARY_DIR}/third_party/zlib")
  if(TARGET zlibstatic)
    set(ZLIB_LIB_TARGET zlibstatic)
  elseif(TARGET zlib)
    set(ZLIB_LIB_TARGET zlib)
  endif()
else()
  find_package(ZLIB QUIET)
  if(ZLIB_FOUND)
    set(ZLIB_LIB_TARGET ZLIB::ZLIB)
    set(ZLIB_INCLUDE_DIR ${ZLIB_INCLUDE_DIRS})
  endif()
endif()

# -------------------- zstd 配置 --------------------
set(ZSTD_LIB_TARGET "")
set(ZSTD_INCLUDE_DIR "")
if(USE_VENDORED_ZSTD AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/build/cmake/CMakeLists.txt")
  message(STATUS "使用本地源码构建 zstd")
  set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
  add_subdirectory(third_party/zstd/build/cmake EXCLUDE_FROM_ALL)
  set(ZSTD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/lib")
  if(TARGET libzstd_static)
    set(ZSTD_LIB_TARGET libzstd_static)
  elseif(TARGET zstd::libzstd_static)
    set(ZSTD_LIB_TARGET zstd::libzstd_static)
  elseif(TARGET zstd)
    set(ZSTD_LIB_TARGET zstd)
  endif()
else()
  find_package(zstd QUIET)
  if(TARGET zstd::libzstd_static)
    set(ZSTD_LIB_TARGET zstd::libzstd_static)
  elseif(TARGET ZSTD::zstd)
    set(ZSTD_LIB_TARGET ZSTD::zstd)
  endif()
endif()

# -------------------- 链接到 logger 库 --------------------
if(FMT_LIB_TARGET)
  target_link_libraries(logger PUBLIC ${FMT_LIB_TARGET})
  message(STATUS "✓ fmt 已链接: ${FMT_LIB_TARGET}")
else()
  message(WARNING "✗ fmt 未找到")
endif()

if(ZLIB_LIB_TARGET)
  target_link_libraries(logger PRIVATE ${ZLIB_LIB_TARGET})
  if(ZLIB_INCLUDE_DIR)
    target_include_directories(logger PUBLIC ${ZLIB_INCLUDE_DIR})
  endif()
  message(STATUS "✓ zlib 已链接: ${ZLIB_LIB_TARGET}")
  message(STATUS "  zlib 头文件路径: ${ZLIB_INCLUDE_DIR}")
else()
  message(WARNING "✗ zlib 未找到; logger 将不支持 zlib 压缩")
endif()

if(ZSTD_LIB_TARGET)
  target_link_libraries(logger PRIVATE ${ZSTD_LIB_TARGET})
  if(ZSTD_INCLUDE_DIR)
    target_include_directories(logger PUBLIC ${ZSTD_INCLUDE_DIR})
  endif()
  message(STATUS "✓ zstd 已链接: ${ZSTD_LIB_TARGET}")
  message(STATUS "  zstd 头文件路径: ${ZSTD_INCLUDE_DIR}")
else()
  message(WARNING "✗ zstd 未找到; logger 将不支持 zstd 压缩")
endif()


# ==================== Protobuf 配置与代码生成 ====================
option(USE_VENDORED_PROTOBUF "Use third_party/protobuf if present" ON)

set(PROTOBUF_LIB_TARGET "")
set(PROTOBUF_INCLUDE_DIR "")

if(USE_VENDORED_PROTOBUF AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf/CMakeLists.txt")
  message(STATUS "使用本地源码构建 protobuf")
  set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build protobuf tests" FORCE)
  set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "Build protobuf examples" FORCE)
  set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "Build protoc compiler" FORCE)
  set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
  set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "Link static runtime libraries" FORCE)
  set(protobuf_WITH_ZLIB OFF CACHE BOOL "Build with zlib support" FORCE)
  # 若本地存在 absl 源碼則嚴格使用本地依賴；否則允許 protobuf 自行抓取依賴
  if(EXISTS "${CMAKE_SOURCE_DIR}/build/_deps/absl-src/CMakeLists.txt")
    set(protobuf_LOCAL_DEPENDENCIES_ONLY ON CACHE BOOL "Do not fetch deps; use local packages only" FORCE)
  else()
    set(protobuf_LOCAL_DEPENDENCIES_ONLY OFF CACHE BOOL "Do not fetch deps; use local packages only" FORCE)
  endif()
  set(protobuf_INSTALL OFF CACHE BOOL "Disable protobuf install() to avoid export issues" FORCE)
  # 禁用 utf8_range 的安装和测试，避免导出依赖 absl 的 install(EXPORT) 错误
  set(utf8_range_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(utf8_range_ENABLE_TESTS OFF CACHE BOOL "" FORCE)

  # 提前提供本地 Abseil，避免 protobuf 尝试下载
  if(EXISTS "${CMAKE_SOURCE_DIR}/build/_deps/absl-src/CMakeLists.txt")
    message(STATUS "使用本地 absl 源码: ${CMAKE_SOURCE_DIR}/build/_deps/absl-src")
    add_subdirectory(${CMAKE_SOURCE_DIR}/build/_deps/absl-src ${CMAKE_BINARY_DIR}/absl EXCLUDE_FROM_ALL)
  endif()

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf/cmake/CMakeLists.txt")
    add_subdirectory(third_party/protobuf/cmake EXCLUDE_FROM_ALL)
  else()
    add_subdirectory(third_party/protobuf EXCLUDE_FROM_ALL)
  endif()

  set(PROTOBUF_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf/src")

  if(TARGET libprotobuf)
    set(PROTOBUF_LIB_TARGET libprotobuf)
  elseif(TARGET protobuf::libprotobuf)
    set(PROTOBUF_LIB_TARGET protobuf::libprotobuf)
  elseif(TARGET protobuf::libprotobuf-lite)
    set(PROTOBUF_LIB_TARGET protobuf::libprotobuf-lite)
  elseif(TARGET libprotobuf-lite)
    set(PROTOBUF_LIB_TARGET libprotobuf-lite)
  endif()
else()
  find_package(Protobuf QUIET)
  if(Protobuf_FOUND)
    set(PROTOBUF_LIB_TARGET protobuf::libprotobuf)
    set(PROTOBUF_INCLUDE_DIR ${Protobuf_INCLUDE_DIRS})
  endif()
endif()

if(PROTOBUF_LIB_TARGET)
  target_link_libraries(logger PUBLIC ${PROTOBUF_LIB_TARGET})
  if(PROTOBUF_INCLUDE_DIR)
    target_include_directories(logger PUBLIC ${PROTOBUF_INCLUDE_DIR})
  endif()
  message(STATUS "✓ protobuf 已链接: ${PROTOBUF_LIB_TARGET}")
  message(STATUS "  protobuf 头文件路径: ${PROTOBUF_INCLUDE_DIR}")
else()
  message(WARNING "✗ protobuf 未找到; logger 将不支持 protobuf 序列化")
endif()

# --------- 代码生成 ---------
if(TARGET protoc)
  message(STATUS "")
  message(STATUS "========== Protobuf 代码生成 ==========")
  message(STATUS "✓ 找到 protoc 编译器，开始配置 proto 代码生成")

  set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/effective_msg.proto
  )

  set(PROTO_OUTPUT_DIR ${CMAKE_BINARY_DIR}/generated)
  file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})
  message(STATUS "Proto 输出目录: ${PROTO_OUTPUT_DIR}")

  set(PROTO_GENERATED_SOURCES "")
  set(PROTO_GENERATED_HEADERS "")

  foreach(proto_file ${PROTO_FILES})
    get_filename_component(proto_name ${proto_file} NAME_WE)
    set(generated_cc ${PROTO_OUTPUT_DIR}/${proto_name}.pb.cc)
    set(generated_h  ${PROTO_OUTPUT_DIR}/${proto_name}.pb.h)

    list(APPEND PROTO_GENERATED_SOURCES ${generated_cc})
    list(APPEND PROTO_GENERATED_HEADERS ${generated_h})

    add_custom_command(
      OUTPUT ${generated_cc} ${generated_h}
      COMMAND protoc
      ARGS
        --cpp_out=${PROTO_OUTPUT_DIR}
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
        ${proto_file}
      DEPENDS ${proto_file} protoc
      COMMENT "正在从 ${proto_name}.proto 生成 C++ 代码..."
      VERBATIM
    )
    message(STATUS "  已注册: ${proto_name}.proto -> ${proto_name}.pb.{h,cc}")
  endforeach()

  target_sources(logger PRIVATE ${PROTO_GENERATED_SOURCES})
  target_include_directories(logger PUBLIC ${PROTO_OUTPUT_DIR})

  message(STATUS "")
  message(STATUS "将生成以下文件:")
  foreach(src ${PROTO_GENERATED_SOURCES})
    message(STATUS "  📄 ${src}")
  endforeach()
  foreach(hdr ${PROTO_GENERATED_HEADERS})
    message(STATUS "  📄 ${hdr}")
  endforeach()
  message(STATUS "========================================")
  message(STATUS "")
else()
  message(WARNING "")
  message(WARNING "✗ 未找到 protoc 编译器，无法生成 proto 代码")
  message(WARNING "  请检查 protobuf 是否成功构建")
  message(WARNING "")
endif()

