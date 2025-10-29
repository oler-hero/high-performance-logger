#pragma once

#include "formatter/formatter.h"

namespace logger {

//将日志消息（LogMsg）格式化成特定的输出格式
class EffectiveFormatter : public Formatter {
 public:
  /// @brief 读取 msg（日志消息对象）中的信息,格式化成特定的文本格式（比如 [2024-10-29 12:00:00] [ERROR] 错误消息）,写入 dest（内存缓冲区）
  /// @param msg 输入：要格式化的日志消息
  /// @param dest 输出：写入格式化后的文本
  void Format(const LogMsg& msg, MemoryBuf* dest) override;


};

}