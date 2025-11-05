#include "formatter/effective_formatter.h"

#include <chrono>

#include "effective_msg.pb.h"
#include "utils/sys_util.h"

namespace logger {

void EffectiveFormatter::Format(const LogMsg& msg, MemoryBuf* dest) {
    EffectiveMsg effective_msg;
    effective_msg.set_level(static_cast<int>(msg.level));   //
    //timesincerpoch:转换为从 Unix 纪元（1970-01-01 00:00:00）开始的时长(返回：duration 对象（可能是纳秒、微秒等）)
    //.count:获取数值
    effective_msg.set_timestamp(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    effective_msg.set_pid(GetProcessId());
    effective_msg.set_tid(GetThreadId());
    effective_msg.set_line(msg.location.line);
    effective_msg.set_file_name(msg.location.file_name.data(), msg.location.file_name.size());  //stringview类型无法用其构造，所以用这种方法类似于拷贝
    effective_msg.set_func_name(msg.location.func_name.data(), msg.location.func_name.size());
    effective_msg.set_log_info(msg.message.data(), msg.message.size());     //设置日志内容
    size_t len = effective_msg.ByteSizeLong();      //计算消息序列化后需要多少字节
    dest->resize(len);                              //调整字符串的​​实际长度​​为n，并可能填充或截断内容
    effective_msg.SerializeToArray(dest->data(), len);      // 将 protobuf 消息序列化为二进制字节流，写入缓冲区
}

}   //namespace logger