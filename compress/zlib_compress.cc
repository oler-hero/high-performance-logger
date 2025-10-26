#include "compress/zlib_compress.h"
#include <string.h>

namespace logger::compress {

static bool IsCompressed(const void* input, size_t input_size) {
    if (!input || input_size < 2) {     //魔数至少占两字节
        return false;
    }

    uint16_t magic = *(uint16_t*)input;         //"把 input 当作 uint16_t* 指针，然后读取它指向的值"
    return (magic == 0x9c78 || magic == 0xda78 || magic == 0x5e78 || magic == 0x0178);      //魔数根据压缩级别/窗口大小，对应不同的
}

size_t ZlibCompress::CompressBound(size_t input_size) {
    return compressBound(input_size);       //用官方公式
}

void ZlibCompress::InitCompressStream_() {
    compress_stream_ = std::unique_ptr<z_stream, ZStreamDeflateDeleter>(new z_stream());
    memset(compress_stream_.get(), 0, sizeof(z_stream));    //必须清零，_stream` 是C风格结构体，成员默认是未定义值；zlib 要求在 `deflateInit/deflateInit2` 前处于干净初始状态

    // 这三者是 zlib 的“自定义内存分配器”接口：
    // `zalloc(voidpf opaque, uInt items, uInt size)`：分配函数指针
    // `zfree(voidpf opaque, voidpf address)`：释放函数指针
    // `opaque`：传给分配器的用户上下文
    // 设为 `Z_NULL` 表示使用 zlib 的默认分配/释放策略（通常就够用）。
    compress_stream_->zalloc = Z_NULL;
    compress_stream_->zfree = Z_NULL;
    compress_stream_->opaque = Z_NULL;

    //zlib 所有 API 返回“状态码”，类型是 `int`，取值是若干宏：
    //- 常见：`Z_OK(0)`、`Z_STREAM_END(1)`、`Z_BUF_ERROR(-5)`、`Z_MEM_ERROR(-4)`、`Z_STREAM_ERROR(-2)`、`Z_DATA_ERROR(-3)` 等。
    //- 所以用 `int ret` 来承接并判断是否初始化成功。
    int ret = deflateInit2(
        compress_stream_.get(),
        Z_BEST_COMPRESSION,
        Z_DEFLATED,
        MAX_WBITS,
        MAX_MEM_LEVEL,
        Z_DEFAULT_STRATEGY
    );

    if (ret != Z_OK) {
        compress_stream_.reset();       //初始化失败（如内存不足/参数错误）时，zlib 可能处于“半初始化”状态；立刻 reset() 能避免后续误用
    }
}

size_t ZlibCompress::Compress(const void* input, size_t input_size, void* output, size_t output_size) {
    if (!input || !output || input_size == 0) {
        return 0;
    }

    if (!compress_stream_) {
        InitCompressStream_();
        if (!compress_stream_) {
            return 0;
        }
    }

    compress_stream_->next_in =  (Bytef*)(input);       //从“只读指针”到“可写指针”需要去掉 const。用 C 风格强转会同时做 const 移除 + 指针类型转换
    compress_stream_->avail_in = static_cast<uInt>(input_size);     //用 static_cast 做显式窄化，并在窄化前做边界检查。
    compress_stream_->next_out = (Bytef*)output;
    compress_stream_->avail_out = static_cast<uInt>(output_size);

    //deflate(...) 的 ret：表示“本次压缩推进的状态”，可能是 Z_OK（继续）、Z_STREAM_END（完成）、Z_BUF_ERROR（缓冲不够）等。每次调用都要单独检查。
    int ret = deflate(compress_stream_.get(), Z_FINISH);    //这里是做压缩一次

    if (ret != Z_STREAM_END) {      //因为，Z_FINISH状态的压缩会返回这个值，其他的第二参数会返回Z_OK
        //压缩失败，重置流以便下次压缩
        deflateReset(compress_stream_.get());
        return 0;
    }

    //deflate() 每写出 1 字节，就把 avail_out 减 1。剩余可用空间 = avail_out
    //实际写出字节数 = 初始空间 - 剩余空间 = output_size - avail_out
    size_t out_len = output_size - compress_stream_->avail_out;

    //压缩后必须重置，否则下次无法使用
    //原因：zlib 内部维护“历史窗口/字典、哈希表、霍夫曼统计、比特缓冲”等状态。如果不重置而直接继续压缩，新的数据会被当成“同一条连续数据流”的后续，可能引用前一段历史窗口。
    //原因：你想要“一条条相互独立”的压缩块（便于单独存取/单独解压），就必须在每条完成后重置，让下一条从“干净状态”开始，避免跨块引用导致单块解压失败或结果受前块影响。
    deflateReset(compress_stream_.get());

    return out_len;
}

void ZlibCompress::ResetStream() {
    if (compress_stream_) {
        deflateReset(compress_stream_.get());       //存在时清空历史窗口/状态，但保留已分配内存（32KB 窗口、哈希表等），速度比销毁重建快很多
    } else {
        InitCompressStream_();
    }
    //为什么不是每次都初始化：
    //已有流再 Init 会造成二次初始化（逻辑错误/潜在泄漏），而 deflateReset 就是 zlib 官方提供的“复用同一流”的正确姿势
}

void ZlibCompress::ResetUncompressStream_() {   //重置解压流
    if (uncompress_stream_) {
        inflateReset(uncompress_stream_.get());
    } else {
        //不存在的话，更initcompressstream_最开始的逻辑差不多，先创造一个出来
        uncompress_stream_ = std::unique_ptr<z_stream, ZStreamInflateDeleter>(new z_stream());
        memset(uncompress_stream_.get(), 0, sizeof(z_stream));      // std::unique_ptr是一个类对象，无法直接隐式转换为原始指针

        // 这三者是 zlib 的“自定义内存分配器”接口：
        // `zalloc(voidpf opaque, uInt items, uInt size)`：分配函数指针
        // `zfree(voidpf opaque, voidpf address)`：释放函数指针
        // `opaque`：传给分配器的用户上下文
        uncompress_stream_->zalloc = Z_NULL;
        uncompress_stream_->zfree = Z_NULL;
        uncompress_stream_->opaque = Z_NULL;
        //把解压流的“输入端”置为“当前没有任何待处理数据”
        uncompress_stream_->avail_in = 0;           //告诉 zlib 现在没有输入字节
        uncompress_stream_->next_in = Z_NULL;       //输入指针为空，避免误用野指针。

        //解压流初始化
        int ret = inflateInit2(uncompress_stream_.get(), MAX_WBITS);
        if (ret != Z_OK) {
            uncompress_stream_.reset();     //初始化失败（如内存不足/参数错误）时，zlib 可能处于“半初始化”状态；立刻 reset() 能避免后续误用
        }

    }
}

std::string ZlibCompress::Decompress(const void* input, size_t input_size) {
    if (!input || input_size == 0) {
        return "";
    }

    //如果是压缩数据，重置解压流
    //原因：
    // 状态机合法性 inflate/deflate 有明确的流状态：HEAD → … → END。到达 Z_STREAM_END 后，这个流已“结束”，下一次继续调用会返回 Z_STREAM_ERROR；必须 inflateReset/deflateReset 才能再次使用。
    // 即便未到 END，流里仍有比特缓冲、校验累计、内部模式机位置；如果你把“新一帧”的 zlib 头字节当作“上一帧的继续”喂进去，会被当成普通数据解析，轻则 Z_DATA_ERROR，重则输出错乱。
    // 数据独立性（32KB 窗口）
    // zlib 用 32KB 滑动窗口（LZ77）。不重置时，第二段会继承第一段的“历史窗口”。这意味着编码端可能跨段引用历史；而解码端若单独拿第二段解压，没有第一段窗口，就必然失败/错。
    // 想要“每段都能独立解压”（日志条目/文件块可随机读取），编码端每段必须从“空窗口、重新读头”开始；解码端也要在遇到新帧前 reset，使状态回到 HEAD 模式、清空窗口与比特缓冲。
    if (IsCompressed(input, input_size)) {
        ResetUncompressStream_();
    }

    if (!uncompress_stream_) {
        return "";
    }

    //为什么还需要设置输入：
    //每次调用 inflate(...) 前都要设置它们，告诉 zlib“这次要解这段数据”。调用后 zlib 会更新这两个值
    //lib 不持有你的内存，缓冲区可能变了（指针无效/大小不同），所以每次都必须明确告诉它“数据在哪、还有多少”
    uncompress_stream_->next_in = (Bytef*)input;
    uncompress_stream_->avail_in = static_cast<uInt>(input_size);
    
    std::string output;
    output.reserve(input_size * 3);     //预分配内存，减少扩容

    while (uncompress_stream_->avail_in > 0) {
        char buffer[4096];
        uncompress_stream_->avail_out = sizeof(buffer);
        uncompress_stream_->next_out = (Bytef*)buffer;

        int ret = inflate(uncompress_stream_.get(), Z_SYNC_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {   //如果 ret 既不是 Z_OK，又不是 Z_STREAM_END，返回错误
            return "";
        }

        //为什么是这样计算
        //avail_out 是"剩余可写空间"，我们每次循环内部指定的
        size_t have = sizeof(buffer) - uncompress_stream_->avail_out;
        output.append(buffer, have);

        if (ret ==Z_STREAM_END) {
            break;
        }

    }
    return output;
}

}
