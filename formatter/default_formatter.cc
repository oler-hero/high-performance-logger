#include "formatter/default_formatter.h"

#include "utils/sys_util.h"

namespace logger {

void DefaultFormatter::Format(const LogMsg& msg, MemoryBuf* dest) {
    constexpr char kLogLevelStr[] = "TDIWEF";   //级别字符映射表：后面会转换类型取一个字符
    std::time_t now = std::time(nullptr);       //取当前时间戳（秒
    std::tm tm;                                 //声明一个 broken-down time 结构体（年月日时分秒）
    LocalTime(&tm, &now);                       //把 time_t 转成本地时间 tm
    char time_buf[32];                          //准备一个缓冲区输出时间字符串，长度 32 足够 "%Y-%m-%d %H:%M:%S"
    std::strftime(time_buf, sizeof(time_buf),"%Y-%m-%d %H:%M:%S", &tm);         //把 tm 格式化为形如 "2025-10-21 12:34:56" 的字符串，写入 time_buf。
    dest->append("[", 1);
    dest->append(time_buf, strlen(time_buf));   //用sizeof的话：会把尾部未使用的 `\0` 也追加进去，产生多余字符（不正确）
    dest->append("] [", 3);
    dest->append(1, kLogLevelStr[static_cast<int>(msg.level)]);
    dest->append("] [", 3);
    //追加源文件名（不带路径）。file_name 是 string_view，所以用 data()+size() 追加。
    //string_view 不拥有内存，这里只是把那段内存拷到目标字符串里。
    dest->append(msg.location.file_name.data(), msg.location.file_name.size());     //location是sourcelocation结构体，里面有stringview的filenaem
    dest->append(":", 1);
    dest->append(std::to_string(msg.location.line));
    dest->append("] [", 3);
    dest->append(std::to_string(GetProcessId()));
    dest->append(":", 1);
    dest->append(std::to_string(GetThreadId()));
    dest->append("] ", 2);
    dest->append(msg.message.data(), msg.message.size());       //msg是轻量级stringview，不拥有内存，这里只是把那段内存拷到目标字符串里


}



}