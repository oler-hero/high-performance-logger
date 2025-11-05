#pragma once

#include "log_common.h"
#include "log_msg.h"

namespace logger {

class Formatter {
 public:
  virtual ~Formatter() = default;

  //纯虚函数，强制派生类实现“如何把一条日志（LogMsg）转成文本”的策略
  //dest:输出目标缓冲
  virtual void Format(const LogMsg& msg, MemoryBuf* dest) = 0;
    //   用指针的目的
    // 表达“输出参数”：被填写/修改。看到 dest->... 一眼能知道是写入目标。
    // 允许可选/判空：需要时可检查 if (!dest) return;；引用做不到。
    // 复用外部缓冲：调用方把可复用的 MemoryBuf 传进来，Formatter 只负责写入，避免函数返回值带来的分配/拷贝


};


}