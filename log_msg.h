#pragma once

#include "log_common.h"
namespace logger {

struct LogMsg {
    LogMsg(SourceLocation loc, LogLevel lvl, StringView msg);
    LogMsg(LogLevel lvl, StringView msg);

    //默认拷贝/赋值：便于在管线各阶段按值传递/缓存；成员都是轻量类型（整型+string_view）。
    LogMsg(const LogMsg&) = default;
    LogMsg& operator=(const LogMsg&) = default;

    SourceLocation location;
    LogLevel level;
    StringView message;
};

}