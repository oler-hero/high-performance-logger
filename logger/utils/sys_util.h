#pragma once

#include <time.h>
#include <cstdint>
#include <ctime>
#include <string>

namespace logger {

/// @brief  获取系统内存页大小
/// @return 
size_t GetPageSize();

/// @brief  获取当前进程ID
/// @return 
size_t GetProcessId();

/// @brief 获取当前线程ID
/// @return 
size_t GetThreadId();

/// @brief 安全的本地时间转换
/// @param tm 输出参数，转换后的时间结构
/// @param now 输入参数，要转换的时间戳指针
void LocalTime(std::tm* tm, std::time_t* now);

}  // namespace logger
