#include "utils/sys_util.h"

#ifdef __linux__
#include <unisted.h>
#include <sys/syscall.h>

namespace logger {

size_t GetPageSize() {
    return getpagesize();
}

size_t GetProcessId() {
    return static_cast<size_t>(::getpid());
} 

size_t GetThreadId() {
    return static_cast<size_t>(::gettid());
}

void LocalTime(std::tm* tm, std::time_t* now) {
    localtime_r(now, tm);
}

}

#endif