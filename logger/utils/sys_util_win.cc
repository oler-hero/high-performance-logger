#include "utils/sys_util.h"  // 包含接口声明头文件

#ifdef _WIN32                 // Windows平台条件编译
#include <windows.h>          // Windows API主头文件
#include <process.h>          // 进程相关函数（如_getpid）

namespace logger {            // 进入logger命名空间

// 函数1：获取内存页大小
size_t GetPageSize() {
    SYSTEM_INFO si;                    // 定义系统信息结构体
    GetSystemInfo(&si);                // 调用Windows API获取系统信息
    return static_cast<size_t>(si.dwPageSize);  // 返回页大小并类型转换
}
// 作用：获取系统内存管理的最小单位
// 参数：无
// 返回值：内存页大小（字节）

// 函数2：获取进程ID
size_t GetProcessId() {
    return static_cast<size_t>(_getpid());  // 使用C运行时库函数获取进程ID
}
// 作用：获取当前进程的唯一标识符
// 参数：无  
// 返回值：进程ID

// 函数3：获取线程ID
size_t GetThreadId() {
    return static_cast<size_t>(GetCurrentThreadId());  // Windows API获取线程ID
}
// 作用：获取当前线程的唯一标识符
// 参数：无
// 返回值：线程ID

// 函数4：安全的时间转换
void LocalTime(std::tm* tm, std::time_t* now) {
    localtime_s(tm, now);  // Windows安全版本的localtime
}
// 作用：将时间戳转换为本地时间（线程安全）
// 参数：
//   - std::tm* tm: 输出参数，接收转换后的时间结构
//   - std::time_t* now: 输入参数，要转换的时间戳
// 返回值：void（通过指针参数输出）

}  // namespace logger

#endif  // _WIN32           // 结束Windows条件编译