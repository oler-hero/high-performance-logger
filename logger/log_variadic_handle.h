#pragma once

#include "log_handle.h"

namespace logger {

class VariadicLogHandle : public LogHandle {
 public:
  using LogHandle::LogHandle;
  
  //继承基类构造函数（可直接用基类那套构造：传 1 个或多个 LogSink）。
  template <typename ...Args>
  void Log(SourceLocation loc, LogLevel lvl, fmt::basic_string_view<char> fmt, Args&&... args) {
    Log_(loc, lvl, fmt, std::forward<Args>(args)...);
   }
  
  template <typename ...Args>
  void Log(LogLevel lvl, fmt::basic_string_view<char> fmt, Args&&... args) {
    Log(SourceLocation{}, lvl, fmt, std::forward<Args>(args)...);
  }
  
 private:
  template <typename ...Args> 
   void Log_(SourceLocation loc, LogLevel lvl, fmt::basic_string_view<char> fmt, Args&&... args) {
    if (!ShouldLog_(loc)) {
        return;
    }
    fmt::basic_memory_buffer<char, 256> buf;
    fmt::detail::vformat_to(buf, fmt, fmt::make_fommat_args(std::forward<Args>(args)...));
    LogMsg msg(loc, lvl, StringView(buf.data(), buf.size()));
    LogHandle::Log_(msg);
   }
};
}