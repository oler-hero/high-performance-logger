#pragma once

#ifdef ENABLE_LOG

#include "third_party/fmt/include/fmt/chrono.h"
#include "third_party/fmt/include/fmt/core.h"


// ...表示：该宏可以接受​​0个或多个额外参数
//    \这个符号，表示下一行也是属于上一行的内容的意思，__VA_ARGS__ 是 C/C++ 预处理器中的可变参数宏，它会完整接收所有传入的参数。，fmt::format将参数格式化为字符串
#define LOG_INFO(...) \
    fmt::print("[INFO] [{}:{}] {} {}\n",__FILE__, __LINE__, std::chrono::system_clock::now(), fmt::format(__VA_ARGS__))
    //前三者均为调用时自动获取，传入的参数只会给fmt::format
#define LOG_DEBUG(...) \
    fmt::print("[DEBUG] [{}:{}] {} {}\n",__FILE__, __LINE__, std::chrono::system_clock::now(), fmt::format(__VA_ARGS__))

#define LOG_ERROR(...) \
    fmt::print("[ERROR] [{}:{}] {} {}\n",__FILE__, __LINE__, std::chrono::system_clock::now(), fmt::format(__VA_ARGS__))

#else

//使用空宏保证语法一致

#define LOG_INFO(...)

#define LOG_DEBUG(...)

#define LOG_ERROR(...)

#endif
