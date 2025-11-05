#pragma once

#include <memory>

#include "formatter/formatter.h"
#include "log_common.h"
#include "log_msg.h"

namespace logger {
class LogSink {
 public:
  virtual ~LogSink() = default;

  virtual void Log(const LogMsg& msg) = 0;

  //注入/更换格式化策略（Pattern/JSON/彩色等）
  //为什么unique，单一所有权，可热切换，sink析构它跟着析构
  virtual void SetFormatter(std::unique_ptr<Formatter> formatter) = 0;

  //主动冲刷缓冲到目标（落盘/落网）。默认空实现，便于不需要缓冲的后端（例如纯控制台）不用实现。
  virtual void Flush() {}

};

}