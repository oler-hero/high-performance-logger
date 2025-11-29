#pragma once

#include <chrono>
#include <string>
#include "internal_log.h"

namespace logger {

//利用了RAII管理的性能计时工具
class TimerCount {
 public:
  TimerCount(std::string_view info) : info_(info), start_(std::chrono::system_clock::now()) {}

  ~TimerCount() {
    auto end = std::chrono::system_clock::now();  //获取结束时间
    auto diff = std::chrono::duration_cast<std::chrono::microseconds> (end - start_); //计算时间差并返回为微秒
  }
 private:
  std::string_view info_;   //计数器描述信息
  std::chrono::time_point<std::chrono::system_clock> start_;  //开始时间点
};
} //namespace logger

//用于生成一个唯一的变量名字，##连接两个token
#define _TIMER_CONCAT(a, b) a##b

//timer_count + 当前行号 = 唯一变量名
//#define TIMER_COUNT(info) logger::TimerCount _TIMER_CONCAT(time_count, __LINE__)(info)

//临时禁用性能计时功能，避免运行时开销
#define TIMER_COUNT(info) void(0)