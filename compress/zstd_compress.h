#pragma once

#include "compress/compress.h"
#include <zstd.h>

namespace logger::compress {

class ZstdCompress final : public Compression {
 public:
  //为什么需要构造：核心原因：zstd 需要提前分配"上下文"
  ZstdCompress();

  //为什么不默认了：裸指针的析构只是把指针本身销毁（8字节内存），不会释放指向的资源。
  ~ZstdCompress() override;

  // ============================================================
  // 基础接口（实现抽象基类）
  // ============================================================
    
  size_t Compress(const void* data, size_t size, void* output, size_t output_size) override;

  std::string Decompress(const void* data, size_t size) override;

  void ResetStream() override;

  size_t CompressBound(size_t input_size) override;

  // ============================================================
  // 流式压缩接口
  // ============================================================

  /// @brief 流式压缩一块数据
  /// @param in 输入数据
  /// @param in_size 输入大小
  /// @param out 输出缓冲区
  /// @param out_cap 输出缓冲区容量
  /// @param finish 是否为最后一块（true=完成压缩）
  /// @return 实际写入的字节数
  size_t CompressStream(const void* in, size_t in_size, void* out, size_t out_cap, bool finish = false);  

  /// @brief 流式解压一块数据
  /// @param in 输入压缩数据
  /// @param in_size 输入大小
  /// @param out 输出缓冲区
  /// @param out_cap 输出缓冲区容量
  /// @return 实际写入的字节数
  size_t DecompressStream(const void* in, size_t in_size, void* out, size_t out_cap);

  // ============================================================
  // 字典压缩接口
  // ============================================================
  
  /// @brief 设置压缩字典（提高小数据块压缩率）
  /// @param dict 字典数据
  /// @param dict_size 字典大小
  /// @return 成功返回 true
  bool SetCompressionDictionary(const void* dict, size_t dict_size);

  /// @brief 设置解压字典（必须与压缩时使用的字典相同）
  /// @param dict 字典数据
  /// @param dict_size 字典大小
  /// @return 成功返回 true
  bool SetDecompressionDictionary(const void* dict, size_t dict_size);

 private:
  void ResetUncompressStream_();        // 重置解压上下文（dctx_），清空内部状态，准备解压新的独立数据块

 private:
  //为什么是指针类型：ZSTD_CCtx是"不透明类型"（opaque type），只能通过指针操作
  ZSTD_CCtx* cctx_;     //  压缩上下文(指针）     内部状态：哈希表，序列缓冲    算法：编码算法
  ZSTD_DCtx* dctx_;     //  解压上下文(指针)      内部状态：解码表，输出窗口    算法：解码算法

};
}
