#pragma once

#include <memory>
#include <string>

namespace logger {
namespace compress {

class Compression {
 public:
  //虚析构，确保通过基类指针删除派生对象时正确析构
  virtual ~Compression() = default;
  
  /// @brief 一次性将内存块 input 压缩到 output
  /// @param input 待压缩的源数据。
  /// @param input_size 
  /// @param output 用户提供的目标缓冲区及其容量
  /// @param output_size 
  /// @return 实际写入到 output 的压缩字节数。
  virtual size_t Compress(const void* input, size_t input_size, void* output, size_t output_size) = 0;

  /// @brief 返回给定输入大小在“最坏情况下”的压缩上界（即所需目标缓冲区的最小安全容量）
  /// @param input_size 
  /// @return 
  //用于分配/检查 output 缓冲区，避免溢出。
  virtual size_t CompressBound(size_t input_size) = 0;

  /// @brief 一次性解压内存块 data，
  /// @param data 压缩数据指针
  /// @param size 压缩数据字节数
  /// @return 返回解压后的完整数据。(因为不知道解压之后会有多少数据，所以用string，自动分配，自动扩容，用户就不用管了)
  virtual std::string Decompress(const void* data, size_t size) = 0;

  /// @brief 重置内部的流式/状态（如词典、窗口、帧上下文等），让下一次压缩/解压从“干净状态”开始。
  virtual void ResetStream() = 0;           //对有状态的实现非常重要；对于无状态实现，这可以是空操作


};

}

}