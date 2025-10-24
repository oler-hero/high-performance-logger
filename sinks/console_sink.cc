#include "sinks/console_sink.h"

#include "formatter/default_formatter.h"

namespace logger {

ConsoleSink::ConsoleSink() : formatter_(std::make_unique<DefaultFormatter>()) {}

void ConsoleSink::Log(const LogMsg& msg) {
    MemoryBuf buf;
    formatter_->Format(msg, &buf);
    //fwrite 面向 C 的字节 I/O，简单直接，常见于性能敏感路径
    fwrite(buf.data(), 1, buf.size(), stdout);          //把内存块 ptr 开始的 count 个元素写到 stream，每个元素大小为 size 字节。
    fwrite("\n", 1, 1, stdout);                         //再写一个换行
}

void ConsoleSink::SetFormatter(std::unique_ptr<Formatter> formatter) {
    formatter_ = std::move(formatter);      //调用 std::unique_ptr<Formatter> 的移动赋值
}


}