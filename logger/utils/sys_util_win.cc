#include "utils/sys_util.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>

namespace logger {

size_t GetPageSize() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return static_cast<size_t>(si.dwPageSize);
}

size_t GetProcessId() {
    return static_cast<size_t>(_getpid());
}

size_t GetThreadId() {
    return static_cast<size_t>(GetCurrentThreadId());
}

void LocalTime(std::tm* tm, std::time_t* now) {
    localtime_s(tm, now);  // Windows 安全版本
}

}  // namespace logger

#endif  // _WIN32

