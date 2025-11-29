#include "utils/file_util.h"

namespace logger::fs {
  size_t GetFileSize(const std::filesystem::path& file_path) {
    if (std::filesystem::exists(file_path)) {   //如果文件存在返回文件大小
      return std::filesystem::file_size(file_path);
    }
    return 0;     //不存在返回0
  }
}
