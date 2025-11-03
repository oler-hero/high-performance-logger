#pragma once

//原子变量、时间、文件系统、互斥锁。
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>

#include "compress/compress.h"  //压缩算法抽象接口
#include "context/context.h"    //任务调度/线程池相关（这里用到ctx::TaskRunnerTag）
#include "crypt/crypt.h"        //加密算法抽象接口。
#include "mmap/mmap_aux.h"      //mmap辅助封装，用作内存映射的缓存区。
#include "sinks/sink.h"         //日志接收器基类接口。
#include "space.h"              //容量单位类型（如megabytes、bytes）及转换

namespace logger {

namespace detail {
    struct ChunkHeader {
        //魔数编译期就需要固定值，static表示每个实例使用的都是同一东西。
        //为什么uint64：固定宽度、无符号，跨平台二进制布局稳定；64 位提供更大的取值空间，魔数冲突概率极低；同时与结构体的 8 字节对齐自然契合，序列化/反序列化更稳
        static constexpr uint64_t kMagic = 0xdeadbeefdada1100;  //16 进制字面量只能包含 0-9、a-f（或大写 A-F）所以不能有t
        uint64_t magic;         //实例中的魔数字段，默认等于 kMagic
        uint64_t size;          //ChunkHeader::size 表示“本次写盘这一块负载的长度（字节数）”，不包含 ChunkHeader 自身。
        char pub_key[128];      //客户端公钥的序列化（或其二进制/文本存储位置），便于服务端解密。

        ChunkHeader() : magic(kMagic), size(0) {}   //默认构造
    };

    struct ItemHeader {
        static constexpr uint32_t kMagic = 0xbe5fball;      //条目级魔数，校验 cache 结构。
        uint32_t magic;     //实例魔数字段。
        uint32_t size;      //本条日志数据的长度（压缩+加密后的字节数）

        ItemHeader() : magic(kMagic), size(0) {}    //默认构造
    };
}   //namespace detail

class EffectiveSink final : public LogSink {
 public: 
  //- 用途：配置落盘策略及输出位置。
  struct Conf {
    std::filesystem::path dir;          //日志文件目录。
    std::string prefix;                 //文件名前缀；实际文件名形如 {prefix}_{yyyyMMddHHmmss（datetime）}[_{index}].log。
    std::string pub_key;                //服务器端公钥（通常hex或文本），用于 ECDH→对称密钥交换/派生
    std::chrono::minutes interval{5};   //清理任务的触发间隔（定期淘汰旧文件）
    megabytes single_size{4};           //单个日志文件的滚动上限（达到则切新文件）
    megabytes total_size{100};          //目录下所有日志文件的总容量上限（超过则删除旧文件）
  };

  explicit EffectiveSink(Conf conf);

  ~EffectiveSink() override = default;

  /// @brief 入口：每条日志调用一次(语义：格式化→压缩→加密→写入主缓存；当缓存利用率超过阈值时触发“交换缓存并异步落盘”)
  /// @param msg 一条结构化日志信息（时间、级别、文本等）
  void Log(const LogMsg& msg) override;

  void SetFormatter(std::unique_ptr<Formatter> formatter) override;

  /// @brief 立刻推动缓存内容刷到文件：通常交换缓存、投递落盘并等待后台任务空闲。
  void Flush() override;

 private:
  /// @brief 交换主从缓存指针。主缓存继续接收新日志，从缓存被后台线程顺序读出并落盘
  void SwapCache_();

  /// @brief 判断是否需要把主缓存的内容交换出去并准备写盘（通常依据利用率阈值，例如 >0.8）
  /// @return 是否满足利用率
  bool NeedCacheToFile_();

  /// @brief 将一条日志（已压缩+加密的 payload）写入主缓存，前置一个 ItemHeader
  /// @param data 日志二进制数据起始地址。
  /// @param size 数据长度。
  void WirteToCache_(const void* data, uint32_t size);

  /// @brief 把“落盘任务”投递到后台执行器（真正写文件在 CacheToFile_）,为什么需要它，前台线程不会阻塞，任务交给后台异步执行.解耦,函数职责划分
  void PrepareToFile_();

  /// @brief 后台执行：将从缓存的数据整体按“块”写入文件：
  void CacheToFile_();

  /// @brief 生成/返回当前应写入的日志文件路径（以 prefix + 时间戳 为基名。若超出 single_size 则滚动到新的文件名（可能带 _index））
  /// @return 
  std::filesystem::path GetFilePath_();

  /// @brief 周期清理：扫描目录下 .log 文件，按修改时间从新到旧累加体积，超过 total_size 的部分全部删除。
  void ElimateFiles_();

 private:
  Conf conf_;                                       //配置副本，供整个生命周期使用。
  std::mutex mutex_;                                //保护共享状态（尤其是写入 mmap 缓存、交换缓存等需要互斥的区域）
  std::unique_ptr<Formatter> formatter_;            //日志格式化策略对象，Log 时把 LogMsg 变为字节序列
  ctx::TaskRunnerTag task_runner_;                  //只是“标签”，让执行器用它找到对应线程池；用于投递异步任务（落盘、定时清理、周期轮转），把重活放后台，前台写不被阻塞。
  std::unique_ptr<crypt::Crypt> crypt_;             //加密器接口实例（实现中是 AESCrypt），输入压缩后的数据，输出密文。
  std::unique_ptr<compress::Compression> compress_; //压缩器接口实例（实现中是 ZstdCompress 或 ZlibCompress），输入明文/格式化后的日志，输出压缩数据。
  std::unique_ptr<MMapAux> master_cache_;           //主缓存（mmap，顺序 append，极快）；前台线程把格式化后的日志直接写这里，减少 syscalls 与拷贝。
  std::unique_ptr<MMapAux> slave_cache_;            //从缓存，和主缓存成对使用做“双缓冲”：当需要落盘/轮转时，原子交换 master↔slave，后台线程独占读取 slave 进行压缩加密落盘，前台继续写新的 master，实现“写入无阻塞”。
  std::filesystem::path log_file_path_;             //当前落盘文件路径；用于切卷（按大小/时间/数量）时更新，以及后台写盘时确定目标文件。
  std::string client_pub_key_;                      //客户端公钥（通过 ECDH 与服务端公钥派生共享密钥；也写入 ChunkHeader）用于密钥派生/校验，且会写入 chunk 头部，便于服务端解密与追踪来源
  std::string compressed_buf_;                        //压缩阶段的复用临时缓冲，避免每次分配/释放带来的抖动与碎片；需在互斥保护下使用或只在后台线程使用。
  std::string encryped_buf_;                        //加密输出复用缓冲；同理减少分配开销与缓存失效，提高吞吐
  std::atomic<bool> is_slave_free_{true};           //从缓存是否空闲的原子标志
};

}
