#include "compress/zstd_compress.h"

#include <cstring>

namespace logger::compress {

ZstdCompress::ZstdCompress() {
    cctx_ = ZSTD_createCCtx();
    //设置压缩级别,这里代表设置了级别5（1-19）
    ZSTD_CCtx_setParameter(cctx_, ZSTD_c_compressionLevel, 5);

    dctx_ = ZSTD_createDCtx();
}

ZstdCompress::~ZstdCompress() {
    if (cctx_) {
        ZSTD_freeCCtx(cctx_);
    }

    if (dctx_) {
        ZSTD_freeDCtx(dctx_);
    }
}

void ZstdCompress::ResetStream() {
    if (cctx_) {
        //第二个参数是重置模式：
        //ZSTD_reset_session_only只重置会话状态（保留压缩级别）  （推荐：连续压缩多个独立数据块使用）
        //ZSTD_reset_parameters重置参数设置
        //ZSTD_reset_session_and_parameters 完全重置
        ZSTD_CCtx_reset(cctx_, ZSTD_reset_session_only);
    }
}

void ZstdCompress::ResetUncompressStream_() {
    if(dctx_) {
        //为什么需要reset：具体详细见zlib.cc某处注释
        ZSTD_DCtx_reset(dctx_, ZSTD_reset_session_only);
    }
}

//工具函数：检测zstd魔数，检查该模块是否被压缩
static bool IsZSTDCompressed(const void* input, size_t input_size) {
    if (!input || input_size < 4) {     //ZSTD 魔数是 4 字节，数据少于 4 字节肯定不是 ZSTD 格式
        return false;
    }

    //两字节无字节序问题，但是四字节就开始有了
    //这里定义了两个魔数数组！
    //为什么：因为字节序的问题：ZSTD 的魔数本质上是一个 32 位整数：0x28B52FFD（十六进制）但不同 CPU 架构存储这个整数的方式不同：
    //比如有的是小端序，有的是大端序，所以俩都需要
    const uint8_t kMagicNumberBigEndian[] = {0x28, 0xb5, 0x2f, 0xfd};
    const uint8_t kMagicNumberLittleEndian[] = {0xfd, 0x2f, 0xb5, 0x28};

    //int memcmp(const void* ptr1, const void* ptr2, size_t num);
    //逐字节比较两块内存

    if (memcmp(input, kMagicNumberBigEndian, sizeof(kMagicNumberBigEndian)) == 0) {
        return true;
    }

    if (memcmp(input, kMagicNumberLittleEndian, sizeof(kMagicNumberLittleEndian)) == 0) {
        return true;
    }

    return false;
}

size_t ZstdCompress::Compress(const void* data, size_t size, void* output, size_t output_size) {
    if (!data || size == 0 || !output || output_size == 0) {
        return 0;
    }

    if (!cctx_) {
        return 0;
    }

    //zstd定义这个结构体，是为了方便我们的很多东西
    ZSTD_inBuffer input_buffer = {data, size, 0};
    ZSTD_outBuffer output_buffer = {output, output_size, 0};

    size_t ret;         //它告诉你：压缩的进度和状态   ==0结束， >0继续，其他为错误码
    do {
        //压缩一块数据，模式为：ZSTD_e_end,因为代码是一次性压缩
        //ZSTD_e_end        压缩结束指令，告诉 ZSTD"这是最后的数据，关闭压缩帧"
        //ZSTD_e_continue   继续压缩(流式中间块）
        //ZSTD_e_flush      刷新到边界（同步点）
        ret = ZSTD_compressStream2(cctx_, &output_buffer, &input_buffer, ZSTD_e_end);   
        if (ZSTD_isError(ret)) {        //判断是否错误
            ZSTD_CCtx_reset(cctx_, ZSTD_reset_session_only);    //只重置内容，不重置压缩级别
            return 0;
        }
    } while (ret > 0);      //循环条件

    //重置方便下次调用，为什么重置，因为状态机等：见zlib.cc某注释
    ZSTD_CCtx_reset(cctx_, ZSTD_reset_session_only);

    return output_buffer.pos;
}

std::string ZstdCompress::Decompress(const void* data, size_t size) {
    if (!data || size == 0) {
        return "";
    }

    if (!dctx_) {
        return "";
    }

    if (IsZSTDCompressed(data, size)) {
        ResetUncompressStream_();
    }

    std::string output;
    ZSTD_inBuffer input = {data, size, 0};

    char buffer[64 * 1024];     //64kb临时缓冲区

    while (input.pos < input.size) {
        ZSTD_outBuffer output_buffer = {buffer, sizeof(buffer), 0};

        size_t ret = ZSTD_decompressStream(dctx_, &output_buffer, &input);  //解压函数

        if (ZSTD_isError(ret)) {    //zstd自带的判断错误函数
            return "";
        }

        output.append(buffer, output_buffer.pos);   //向返回值添加数据

        if (ret == 0) {     //表示完成
            break;
        }
    }
    return output;
}

size_t ZstdCompress::CompressBound(size_t input_size) {
    return ZSTD_compressBound(input_size);
}

size_t ZstdCompress::CompressStream(const void* in, size_t in_size,
                                    void* out, size_t out_cap,
                                    bool finish) {
    if (!cctx_ || !out || out_cap == 0) {
        return 0;       //为什么不判断insize：流式压缩允许空输入：in_size == 0 + finish = true 表示"结束压缩"
    }

    ZSTD_inBuffer in_buf = {in, in_size, 0};
    ZSTD_outBuffer out_buf = {out, out_cap, 0};

    //finish=true时用ZSTD_e_end完成压缩，否则用ZSTD_e_continue
    ZSTD_EndDirective mode = finish ? ZSTD_e_end : ZSTD_e_continue;
    
    size_t ret = ZSTD_compressStream2(cctx_, &out_buf, &in_buf, mode);

    if (ZSTD_isError(ret)) {
        return 0;
    }

    //如果完成（ret==θ且finish=true），重置流以便复用
    if (ret == 0 && finish) {
        ZSTD_CCtx_reset(cctx_, ZSTD_reset_session_only);
    }

    return out_buf.pos;     //告诉调用者"这次写入了多少字节"
}

size_t ZstdCompress::DecompressStream(const void* in, size_t in_size,
                                      void* out,size_t out_cap) {
    if (!dctx_ || !out || out_cap) {
        return 0;
    }

    ZSTD_inBuffer in_buf = {in, in_size, 0};
    ZSTD_outBuffer out_buf = {out, out_cap, 0};

    size_t ret = ZSTD_decompressStream(dctx_, &out_buf, &in_buf);

    if (ZSTD_isError(ret)) {
        return 0;
    }
    //如果完成（ret==0），重置流
    if (ret == 0) {
        ZSTD_DCtx_reset(dctx_, ZSTD_reset_session_only);
    }

    return out_buf.pos;
}

bool ZstdCompress::SetCompressionDictionary(const void* dict, size_t dict_size) {
    if (!cctx_ || !dict ||dict_size) {
        return false;
    }

    size_t ret = ZSTD_CCtx_loadDictionary(cctx_, dict, dict_size);
    return !ZSTD_isError(ret);
}

bool ZstdCompress::SetDecompressionDictionary(const void* dict, size_t dict_size) {
    if (!dctx_ || !dict ||dict_size == 0) {
        return false;
    }

    size_t ret = ZSTD_DCtx_loadDictionary(dctx_, dict, dict_size);
    return !ZSTD_isError(ret);
}

}
