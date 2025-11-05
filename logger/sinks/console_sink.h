#pragma once

#include "sinks/sink.h"

namespace logger {

class ConsoleSink : public LogSink {
 public:
  ConsoleSink();

  ~ConsoleSink() override = default;

  /// @brief 把 LogMsg 用 formatter_ 格式化后写到控制台（stdout/stderr）；线程安全与缓冲策略由实现决定
  /// @param msg  传入的日志消息
  void Log(const LogMsg& msg) override;

  /// @brief 注入/更换格式器；用 unique_ptr 表示“由 sink 独占管理其生命周期”（热切换安全）。
  /// @param formatter 格式化器
  void SetFormatter(std::unique_ptr<Formatter> formatter) override;

 private:
  std::unique_ptr<Formatter> formatter_;        //格式化器
};

}