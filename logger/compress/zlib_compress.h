#pragma once

#include "compress/compress.h"
#include <stdint.h>
#include <zlib.h>


namespace logger::compress {

struct ZStreamDeflateDeleter {
    void operator()(z_stream* stream) {     //为什么是operator()呢，因为 C++ 要求可调用对象，operator() 使对象可以像函数一样调用
        if (stream) {                       //看起来像直接调用一个函数，实际是对象
            deflateEnd(stream);
            delete stream;
        }
    }
};

struct ZStreamInflateDeleter {
    void operator()(z_stream* stream) {
        if (stream) {
            inflateEnd(stream);
            delete stream;
        }
    }

};

class ZlibCompress final : public Compression {
 public:
  ~ZlibCompress() override = default;

  size_t Compress(const void* input, size_t input_size, void* output, size_t output_size) override;

  std::string Decompress(const void* input, size_t input_size) override;

  void ResetStream() override;

  size_t CompressBound(size_t input_size) override;

 private:
  void InitCompressStream_();           // 新增：初始化压缩流
  void ResetUncompressStream_();        //只重置解压流（不重置压缩流）

 private:
  std::unique_ptr<z_stream, ZStreamDeflateDeleter> compress_stream_;
  std::unique_ptr<z_stream, ZStreamInflateDeleter> uncompress_stream_;
};

}
