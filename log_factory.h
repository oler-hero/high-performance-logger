#pragma once

#include <memory>

namespace logger {

class VariadicLogHandle;
class LogFactory {
 public:
    LogFactory(const LogFactory&) = delete;
    LogFactory&operator=(const LogFactory&) = delete;
    //返回全局唯一的实例（单例入口）
    static LogFactory& Instance();
    //获取当前持有的日志句柄
    VariadicLogHandle* GetLogHandle();
    //注入，替换全局日志句柄
    void SetLogHandle(std::shared_ptr<VariadicLogHandle> log_handle);

 private:
    LogFactory();

 private:
    std::shared_ptr<VariadicLogHandle> log_handle_;

};
}

