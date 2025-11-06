#pragma once

#include "log_common.h"
#include "log_factory.h"
#include "log_handle.h"

#define EXT_LOGGER_INIT(handle) logger::LogFactory::Instance().SetLogHandle(handle)
//初始化/替换全局日志句柄。通常在程序启动时调用。
//LogFactory::GetLogHandle()` 的声明就是 `VariadicLogHandle*`。
//你的宏里传进来的通常就是这个指针，所以静态类型是 `VariadicLogHandle*`。


//  - 先判空以防崩溃。
//  - 构造 SourceLocation 注入当前源码位置：__FILE__、__LINE__、__FUNCTION__。
//  - 调用句柄的 Log(loc, level, fmt, args...)（适配你实现的 VariadicLogHandle 可变参接口）。
//  - static_cast<const char*> 保证函数名按 const char* 传递（在不同编译器上更统一）。
#define LOGGER_CALL(handle, level, ...)                                                                          \
    if (handle) {                                                                                                \
        (handle)->Log(logger::SourceLocation{__FILE__, __LINE__, static_cast<const char*>(__FUNCTION__)}, level, \
                      __VA_ARGS__)                                                                               \
    }


//按“编译期激活级别”决定：Trace 日志宏是“真的发日志”还是“编译成空操作”，从而对禁用级别做到零运行时开销。
//两种用法同时支持: 既能传入自定义句柄（更灵活），也能直接用全局句柄（更方便）。
#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_TRACE   //预处理条件编译。两个宏一般是整数常量（如数值越小越啰嗦），当“当前激活级别”不高于 Trace 时，开启 Trace 日志代码。
#define LOG_LOGGER_TRACE(handle, ...) LOGGER_CALL(handle, logger::LogLevel::kTrace, __VA_ARGS__)    //定义可变参宏。显式传入句柄 handle，内部转发给前面定义的 LOGGER_CALL，并把等级固定为 kTrace，其余参数用 __VA_ARGS__ 原样拼接转发。
#define EXT_LOG_TRACE(...) LOG_LOGGER_TRACE(logger::LogFactory::Instance().GetLogHandle(), __VA_ARGS__)     //便捷宏。自动从工厂取全局句柄，再把所有参数转发给上面的宏；适合大多数场景不手动传句柄。
#else
#define LOG_LOGGER_TRACE(handle, ...) (void)0   //宏定义为“空操作”。(void)0 是标准惯用法：什么都不做，但在语法上像一条可放在语句位置的表达式，保证调用点编译通过且无开销。  
#define EXT_LOG_TRACEE(...) (void)0             //同上，禁用时全局版也为空操作。
#endif

//(void)0 是一个“空操作语句”。在禁用日志宏时，用它让宏展开后仍然是“一条合法语句”，吞掉调用处的分号，且不生成任何机器码。
//宏调用总是写成 MACRO(...);。禁用时展开成 (void)0;，在任何语句位置都合法，避免空展开导致奇怪的语法边界问题。
#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_DEBUG
#define LOG_LOGGER_DEBUG(handle, ...) LOGGER_CALL(handle, logger::LogLevel::kInfo, __VA_ARGS__)
#define EXT_LOG_DEBUG(...) LOG_LOGGER_DEBUG(logger::LogFactory::Instance().GetLogHandle(), __VA_RAGS__)
#else
#define LOG_LOGGER_DEBUG(handle, ...) (void)0
#define EXT_LOG_DEBUG(...) (void)0
#endif

#if LOGGER_ACTIVE_LEVEL <=LOGGER_LEVEL_INFO
#define LOG_LOGGER_INFO(handle, ...) LOGGER_CALL(handle, logger::LogLevel::kInfo, __VA_ARGS__)
#define EXT_LOG_INFO(...) LOG_LOGGER_INFO(logger::LogFactory::Instance().GetLogHandle(),__VA_ARGS__)
#else
#define LOG_LOGGER_INFO (void)0
#define EXT_LOG_INFO(...) (void)0
#endif

#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_WARN
#define LOG_LOGGER_WARN(handle, ...) LOGGER_CALL(handle, logger::LogLevel::kWarn, __VA_ARGS__)
#define EXT_LOG_WARN(...) LOG_LOGGER_WARN(logger::LogFactory::Instance().GetLogHandle(), __VA_ARGS__)
#else
#define LOG_LOGGER_WARN(handle, ...) (void)0
#define EXT_LOG_WARN(...) (void)0
#endif

#if LOGGER_AVTICE_LEVEL <= LOGGER_LEVEL_ERROR
#define LOG_LOGGER_ERROR(handle, ...) LOGGER_CALL(handle, logger::LogLevel::kError, __VA_ARGS__)
#define EXT_LOG_ERROR(...) LOG_LOGGER_ERROR(logger::LogFactory::Instance().GetLogHandle(), __VA_ARGS__)
#else
#define LOG_LOGGER_ERROR(handle, ...) (void)0
#define EXT_LOG_ERROR(...) (void)0
#endif

//- 命名是 Critical，但枚举值使用 kFatal，只是命名差异，语义上“最高/致命”。
#if LOGGER_ACTIVE_LEVEL <= LOGGER_LEVEL_CRITICAL
#define LOG_LOGGER_CRITICAL(handle, ...) LOGGER_CALL(handle, logger::LogLevel::kFatal, __VA_ARGS__)
#define EXT_LOG_CRITICAL(...) LOG_LOGGER_CRITICAL(logger::LogFactory::Instance().GetLogHandle(), __VA_ARGS__)
#else
#define LOG_LOGGER_CRITICAL(handle, ...) (void)0
#define EXT_LOG_CRITICAL(...) (void)0
#endif