# 高性能日志系统

[English](README.md) | 简体中文

一个基于 **mmap 双缓冲**、**流式压缩**、**ECDH + AES 加密** 和 **Protobuf 序列化** 的 C++17 异步日志库。目标是在业务线程不被阻塞的前提下写入大量日志，并保证落盘数据经过压缩与加密。

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)

## 特性

- **异步非阻塞写入**：业务线程只做格式化和向 mmap 缓存追加，压缩、加密、落盘全部交给后台执行器。
- **mmap 双缓冲持久化**：主缓存使用率超过阈值时与从缓存交换，前台继续写新主缓存，后台线程把从缓存写盘。
- **流式压缩**：按条流式压缩，支持 Zstd / Zlib；压缩上下文和输入输出缓冲复用，避免反复分配。
- **端到端加密**：每个 chunk 通过 ECDH 协商密钥，日志负载使用 AES 加密，私钥只存在于解码端。
- **Protobuf 记录编码**：级别、时间戳、pid、tid、文件名、函数名、行号、消息体按 `.proto` 结构化序列化，解码时可自定义输出格式。
- **插件化架构**：Sink、Formatter、Compressor、Cryptor 均面向抽象接口，可替换实现。
- **日志轮转与淘汰**：按单文件大小轮转，目录总容量超限时删除最旧文件，并周期性清理。
- **编译期级别裁剪**：被禁用的日志级别展开为 `(void)0`，运行时零开销。
- **跨平台**：POSIX / Win32 平台差异层，统一 mmap、文件与系统工具接口。
- **离线解码工具**：把二进制日志还原为可读文本，支持自定义格式串。

## 架构

```mermaid
flowchart LR
    A["业务线程<br/>EXT_LOG_INFO(...)"] --> B["格式化<br/>LogMsg -> EffectiveMsg"]
    B --> C["压缩<br/>Zstd / Zlib（流式）"]
    C --> D["加密<br/>ECDH + AES"]
    D --> E["mmap 主缓存"]
    E -->|"达到阈值后交换"| F["mmap 从缓存"]
    F --> G["后台执行器<br/>线程池"]
    G --> H["日志文件<br/>轮转 + 淘汰"]
    H --> I["decode 工具<br/>+ 服务端私钥"]
    I --> J["明文日志"]
```

写入流程：

1. `EXT_LOG_*` 宏 / `LogHandle::Log` 先按级别过滤，再用 `fmt` 完成格式化。
2. `EffectiveSink` 把日志序列化为 Protobuf，流式压缩，再用 ECDH 派生出的 AES 密钥加密。
3. 加密后的记录以 `ItemHeader + payload` 的形式追加到 mmap **主缓存**。
4. 主缓存超过利用率阈值时交换主从缓存，后台执行器把从缓存写入磁盘。
5. 按 `single_size` 轮转文件；总大小超过 `total_size` 时删除最旧的文件。

### 落盘格式

```text
ChunkHeader                                      ItemHeader        payload
+----------------+----------------+-------------+ +-----------+----+ +-------------------+
| magic (8B)     | size (8B)      | pub_key     | | magic 4B  | sz | | AES 密文          |
| 0xdeadbeef...  | 负载字节数     | (128B)      | | size 4B   |    | | (zstd/zlib 数据)  |
+----------------+----------------+-------------+ +-----------+----+ +-------------------+
```

每个 chunk 都携带客户端临时公钥，解码端用服务端私钥即可派生出相同密钥，因此日志文件可以离线解码。

## 构建

前置条件：

- CMake 3.20+
- 支持 C++17 的编译器（Visual Studio 2022 / GCC / Clang）
- Python 3
- Windows 需要 Visual Studio 2022 与 `v143` 工具集

所有依赖都已放在 `logger/third_party/`，构建不需要联网：

| 库 | 版本 |
| --- | --- |
| zstd | 1.5.8 |
| zlib | 1.3.1.1 |
| protobuf | 6.34.0 |
| abseil | 20250512.1 |
| cryptopp | 8.9.0 |
| fmt | 12.0.0 |

### Windows

```powershell
python script/build_windows.py
```

默认 Debug 配置，产物位于：

```text
build/x64-Debug/logger/Debug/logger.lib
build/x64-Debug/example/Debug/logger_example.exe
build/x64-Debug/example/Debug/macro_logger_example.exe
build/x64-Debug/example/Debug/crypt_example.exe
build/x64-Debug/example/Debug/zstd_example.exe
build/x64-Debug/decode/Debug/decoder.exe
```

### Linux

```bash
python3 script/build_linux.py
```

> `script/bin/protoc` 是预编译的 x86-64 Linux 可执行文件，用于在 CMake 之前生成 `effective_msg.pb.*`。其他架构请替换为对应平台的 `protoc`。

## 快速开始

```cpp
#include "logger/logger.h"
#include "logger/sinks/effective_sink.h"

int main() {
  logger::EffectiveSink::Conf conf;
  conf.dir = "./logs";                       // 输出目录
  conf.prefix = "app";                       // 文件名前缀
  conf.pub_key = "<服务端公钥 hex>";         // 解码端的 ECDH 公钥
  conf.single_size = logger::megabytes{4};   // 单文件 4 MB 轮转
  conf.total_size = logger::megabytes{100};  // 目录最多保留 100 MB
  conf.interval = std::chrono::minutes{5};   // 清理周期

  auto sink = std::make_shared<logger::EffectiveSink>(conf);
  auto handle = std::make_shared<logger::VariadicLogHandle>(sink);
  EXT_LOGGER_INIT(handle);  // 安装全局句柄

  EXT_LOG_INFO("server started, port={}", 8080);
  EXT_LOG_WARN("slow request: {} ms", 350);
  EXT_LOG_ERROR("request failed: {}", "timeout");

  sink->Flush();  // 同步刷出当前缓存
  return 0;
}
```

说明：

- `EXT_LOG_*` 使用 `EXT_LOGGER_INIT` 安装的全局句柄，也可以直接调用 `handle->Log(...)`。
- `ConsoleSink` 可以和 `EffectiveSink` 组合使用，`LogHandle` 支持同时挂载多个 Sink。
- 日志级别通过 `LOGGER_ACTIVE_LEVEL` 在编译期裁剪（默认全部开启）。例如定义为 `LOGGER_LEVEL_WARN` 后，低级别调用会直接从二进制中消失。

### CMake 集成

```cmake
add_subdirectory(path/to/high-performance-logger)

add_executable(app main.cc)
target_link_libraries(app PRIVATE logger)
target_include_directories(app PRIVATE path/to/high-performance-logger)
target_compile_definitions(app PRIVATE FMT_HEADER_ONLY=1)
```

## 生成密钥与解码日志

`crypt_example` 会打印一组 ECDH 密钥对和一个 AES 示例密钥：

```bash
./crypt_example
# server_pri_hex: ...
# server_pub_hex: ...
```

把 `server_pub_hex` 填入 `EffectiveSink::Conf::pub_key`，`server_pri_hex` 只保存在解码端。

解码日志文件：

```bash
./decoder <日志文件> <服务端私钥hex> <输出文本文件>
```

解码流程为：校验 chunk 头 → 用内嵌的客户端公钥做 ECDH → AES 解密 → Zstd 解压 → Protobuf 反序列化 → 按格式串输出。格式串在 `decode/decode.cc` 中设置，例如 `[%l][%D:%S][%p:%t][%F:%f:%#]%v`。

## 目录结构

```text
.
├── CMakeLists.txt
├── logger/                      # 核心静态库
│   ├── logger.h                 # EXT_LOG_* 宏
│   ├── log_handle.h             # 级别过滤 + 多 Sink 分发
│   ├── log_variadic_handle.h    # 基于 fmt 的可变参 Log()
│   ├── log_factory.h            # 全局句柄单例
│   ├── log_msg.h                # 日志消息模型
│   ├── compress/                # Zstd / Zlib 压缩
│   ├── context/                 # 执行器与线程池
│   ├── crypt/                   # ECDH + AES
│   ├── formatter/               # 文本与 Protobuf 格式化
│   ├── mmap/                    # mmap 缓存（POSIX / Win32）
│   ├── sinks/                   # 控制台与持久化 Sink
│   ├── utils/                   # 系统与文件工具
│   ├── proto/effective_msg.proto
│   └── third_party/             # 第三方依赖源码
├── example/                     # 可运行示例
├── decode/                      # 离线解码工具
└── script/                      # 构建脚本与内置 protoc
```

## 性能

`example/logger_example.cc` 和 `example/macro_logger_example.cc` 会写入 100 万条 2 KB 日志并打印耗时，可以直接在自己的机器上复现：

```bash
./logger_example
./macro_logger_example
```

吞吐与压缩率取决于消息大小、日志级别、压缩算法和磁盘性能。粗略参考：在桌面级 CPU 上异步写入路径按 10 万条/秒设计，重复度较高的文本日志经 Zstd 压缩后通常能降到原体积的 20% 左右。

## 已知限制

- 日志格式为自定义二进制格式，必须使用配套解码器和服务端私钥才能还原。
- 解码器目前是单线程，并会把整个文件读入内存。
- 内置的 `protoc` 与平台绑定（Windows `protoc.exe`、Linux x86-64 `protoc`）。

## 许可证

[MIT](LICENSE)
