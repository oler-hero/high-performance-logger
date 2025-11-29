#pragma once

#include <stdint.h>
#include <filesystem>

namespace logger {
namespace fs {
/// @brief  获取文件大小的函数
/// @param file_path 对象（文件路径
/// @return size_t 类型（文件大小，以字节为单位）
size_t GetFileSize(const std::filesystem::path& file_path);
}  // namespace fs
}  // namespace logger
