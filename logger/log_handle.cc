#include "log_handle.h"

#include "formatter/formatter.h"
#include "sinks/sink.h"

namespace logger {

LogHandle::LogHandle(LogSinkPtrList sinks) : level_(LogLevel::kInfo) {
    //若写 for (auto sink : sinks_)，每次循环都会拷贝一个 shared_ptr，导致引用计数原子加/减一次（有额外开销）
    for (const auto& sink : sinks) {
        sinks_.push_back(std::move(sink));          // 会移动，sink 变空，不额外增减计数，更高效；
    }
}

LogHandle::LogHandle(LogSinkPtr sink) : level_(LogLevel::kInfo) {
    sinks_.push_back(std::move(sink));
}

void LogHandle::SetLevel(LogLevel& lvl) {
    level_.store(lvl);
}

LogLevel LogHandle::GetLevel() const {
    return level_;
}

void LogHandle::Log_(const LogMsg& msg) {
    for (const auto& sink : sinks_) {
        sink->Log(msg);
    }
}

void LogHandle::Log(LogLevel lvl, SourceLocation loc, StringView msg) {
    if (!ShouldLog_(lvl)) {
        return ;
    }

    LogMsg log_msg(loc, lvl, msg);

    Log_(log_msg);

}

}