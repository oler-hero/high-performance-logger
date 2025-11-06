#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>

#include "decode/decode_formatter.h"
#include "logger/compress/zstd_compress.h"
#include "logger/crypt/aes_crypt.h"
#include "logger/sinks/effective_sink.h"

using namespace logger;
using namespace logger::detail;

std::unique_ptr<DecodeFormatter> decode_formatter;  //“日志格式化器”的实例指针，独占所有权

/// @brief 把整个文件一次性读入内存，按字节返回为 std::vector<char>（适合中小文件）
/// @param file_path 输入日志文件路径。
/// @return 整个文件内容（二进制）放入 std::vector<char>。
std::vector<char> ReadFile(const std::string& file_path) {
    std::ifstream ifs(file_path, std::ios::binary);     //打开输入文件流（binary 模式，避免 Windows 下换行符被转换）。(二进制模式，避免文本模式下的行结束符转换（尤其是 Windows）。)
    if (!ifs) {
        throw std::runtime_error("ReadFile: open file failed");
    }
    ifs.seekg(0, std::ios::end);        //将“读取指针”移动到文件末尾。seekg：seek get（定位输入/读取指针）。(seekg(pos, dir)：把读取指针移动到相对 dir 的位置（beg/cur/end）。)
    auto size = ifs.tellg();            //询问当前读取位置（距文件首的偏移量）= 文件大小。tellg：tell get（查询读取指针位置）。
    ifs.seekg(0, std::ios::beg);        //把读取指针移回文件开头，为接下来的读取做准备。
    std::vector<char> buffer(size);     //按文件大小分配等长的字节缓冲区。
    ifs.read(buffer.data(), size);      //从当前指针开始一次性读取 size 字节到缓冲区。(buffer.data() 是指向 std::vector<char> 连续存储区首元素的指针，类型是 char*,而&buffer指向的是对象本身地址，无法使用)
    return buffer;                      //返回读到的所有字节
}

/// @brief 二进制追加模式写文件，不清空历史内容。
/// @param file_path 输出文件路径（文本会被追加写入）。
/// @param data 要写入的一段文本数据。
void AppendDataToFile(const std::string& file_path, const std::string& data) {
    std::ofstream ofs(file_path, std::ios::binary | std::ios::app);     //以二进制追加模式打开文件
    ofs.write(data.data(), data.size());                                //把data写入
}

std::unique_ptr<logger::compress::Compression> decompress;      //是“解压器接口”的实例指针，指向具体实现（运行时绑定）。

/// @brief 把一条“日志条目”的原始字节从 密文 → 明文字节 → Protobuf 对象 → 格式化文本，并把文本追加到 output
/// @param data 指向“单条日志条目负载”的起始地址（即条目头之后的密文字节）
/// @param size  条目负载的字节数
/// @param crypt  对称解密器（此处为 AESCrypt 实例，内部持有共享密钥）。
/// @param output 解码后的可读文本将被追加到此字符串末尾。
void DecodeItemData(char* data, size_t size, crypt::Crypt* crypt, std::string& output) {
    std::string decrypted = crypt->Decrypt(data, size);     //用对称密钥把 data[0..size) 解密，得到“解密后的原始字节”。返回的是字节序列，可能包含 0 字节，并非“可读文本”。
    std::string decompressed = decompress->Decompress(decrypted.data(), decrypted.size());      //对上一步的明文字节做解压（Zstd）。得到的是“序列化的 Protobuf 有效负载”（也就是 EffectiveMsg 的 wire-format 字节）。
    EffectiveMsg msg;       //声明一个空的 Protobuf 消息对象，用来承载一条日志的结构化数据。
    msg.ParseFromString(decompressed);      //Protobuf 的反序列化函数：把 decompressed 这串字节按 Protobuf wire 格式解析进 msg。
    std::string assemble;       //临时字符串，用来装“格式化后的”文本行。
    decode_formatter->Format(msg, assemble);    //按预设的 pattern（在 main 里 SetPattern(...)）把 msg 转成一行可读文本，结果写入 assemble（通常是追加）。
    output.append(assemble);    //把这一行文本追加到 output。注意换行符不在这里加（调用方在循环里统一 push_back('\n')）
}

/// @brief 处理“单个块”的解码与密钥协商。
/// @param data 指向“当前块的负载起始地址”（即块头之后的连续条目区域）
/// @param size  当前块负载总长度（字节）。
/// @param cli_pub_key  客户端公钥（块头中取出的 65 字节未压缩公钥，二进制形式）。
/// @param svr_pri_key 服务器私钥（命令行传入的十六进制字符串）。
/// @param output 本块内所有条目格式化后的文本，逐条追加于此（函数内每条后面会补一个换行）
void DecodeChunkData(char* data,
                     size_t size, 
                     const std::string& cli_pub_key, 
                     const std::string& svr_pri_key, 
                     std::string& output) {
    std::cout << "decode chunk" << size << std::endl;   //  - 打印块大小，便于观察进度。
    std::string svr_pri_key_bin = crypt::HexKeyToBinary(svr_pri_key);   //  - 把 HEX 私钥转为二进制字节串，供椭圆曲线计算。
    std::string shared_secret = crypt::GenECDHSharedSecret(svr_pri_key_bin, cli_pub_key);   //clipubkey已经是二进制了，  - 与客户端公钥做 ECDH，得到共享密钥（对称密钥来源）。
    //为什么第一个《》里是crypt而不是aescrypt：核心原因：做抽象解耦与可插拔。crypt::Crypt 是“解密器接口”（基类），AESCrypt 是一种具体实现。用 std::unique_ptr<crypt::Crypt> 可以随时替换为别的实现（如 ChaCha20、SM4、硬件 HSM 等）而不改调用处代码。
    std::unique_ptr<crypt::Crypt> crypt = std::make_unique<crypt::AESCrypt>(shared_secret);     //  - 用共享密钥构造 AES 解密器，用于每条日志的解密。
    size_t offset = 0;      //offset 是当前在块负载内的游标；
    size_t count =0;        //count 统计已处理条目数。
    while (offset < size) {     //  - 只要还没到块末尾，就继续解析下一条。
        ++count;
        if (count % 1000 == 0) {        //条目计数；每 1000 条打印一次进度。
            std::cout  << "decode item" << count << std::endl;
        }
        //这里做的事情，其实并不代表每个模块的魔数不一样，而是
        //这一步做的是“校验”：如果你在当前 offset 处读出的 magic 不等于常量，说明数据损坏或游标错位（解析失败），于是抛异常。
        //所以我们用游标和data计算得到这个魔数
        ItemHeader* item_header = reinterpret_cast<ItemHeader*>(data + offset); //  - 把当前位置的字节解释为 ItemHeader 结构指针，以读取 magic/size。
        if (item_header->magic != ItemHeader::kMagic) {    //  - 校验条目魔数，不匹配说明数据损坏或游标错位，抛异常终止（return 实际上多余）。
            throw std::runtime_error("DecodeChunkData: invalid item magic");
            return ;
        }
        offset += sizeof(ItemHeader);
        //因为解密器是unique_ptr：所以get() 的作用就是“取出被 unique_ptr 管理的原生指针”传进去，不改变所有权，不会释放对象。
        DecodeItemData(data + offset, item_header->size, crypt.get(), output);    //  - 解密 → 解压 → 反序列化 → 格式化，将文本追加到 output。
        offset += item_header->size;  //  - 游标跳过本条负载，到下一条的头部。
        output.push_back('\n');     //  - 为这一条日志追加换行，行与行分隔。
    }
}

/// @brief 解码整个文件（外层循环）。
/// @param input_file_path 输入日志文件路径。
/// @param pri_key 服务器私钥（十六进制字符串，用于与每块中的客户端公钥做 ECDH）。
/// @param output_file_path 文本输出文件路径（函数内按块多次追加写入）。
void DecodeFile(const std::string& input_file_path, const std::string& pri_key, const std::string& output_file_path) {
    auto input = ReadFile(input_file_path);     //  - 把整个输入文件一次性读到 std::vector<char>，后续都在内存里解析。
    if (input.size() < sizeof(ChunkHeader)) {   //  - 文件小于一个块头的大小，显然不是合法文件，直接报错。
        throw std::runtime_error("DecodeFile: input file is too small");
    }
    auto chunk_header = reinterpret_cast<ChunkHeader*>(input.data());   //此处只会读取前 sizeof(ChunkHeader) 个字节（即块头），用来做魔数校验和拿到本块大小；后续真正处理块内容时再用 input.data() + offset 继续解析下一段。
    if (chunk_header->magic != ChunkHeader::kMagic) {   //外层那次是"文件级预检”（只看第一个块头），目的是尽早发现明显的非本格式文件，快速失败。
        throw std::runtime_error("DecodeFile: invaild file magic");
        return ;
    }
    size_t offset = 0;  //  - 当前解析游标，指向文件内的字节偏移。初始化为从文件头开始。
    size_t file_size = input.size();    //  - 保存总大小，便于循环条件判断。
    std::string output;     //  - 用作“块级临时输出缓冲”，每处理完一个块就写入磁盘并清空。
    output.reserve(1024 * 1024);    //  - 预留 1MB 容量，减少块内多次字符串扩容开销（微优化）。
    while (offset < file_size) {
        ChunkHeader* chunk_header = reinterpret_cast<ChunkHeader*>(input.data() + offset);  //  - 把当前位置的字节解释为块头。
        if (chunk_header->magic != ChunkHeader::kMagic) {  //  - 再次校验本块魔数，防止游标错位或中途损坏。与开头那次校验作用不同：这里是“每块校验”。
            throw std::runtime_error("DecodeFile: invaild chunk magic");
            return ;
        }
        output.clear();     //  - 清空块级输出缓冲，准备承接本块的所有条目文本。
        offset += sizeof(ChunkHeader);  //  - 游标跳过块头，指向块负载起始处。
        DecodeChunkData(input.data() + offset, chunk_header->size, std::string(chunk_header->pub_key, 65), pri_key, output);    //  - 调用“块内解码”：
        offset += chunk_header->size;   //  - 游标跳过整个块负载，移动到下一个块的块头。
        AppendDataToFile(output_file_path, output);     //  - 把本块产出的文本一次性以“追加模式”写入输出文件。
    }
}

int main(int argc, char* argv[]) {
    // ./decode <file_path> <pri_key> <output_file>
#if 1   //    - #if 1 分支：走命令行模式；
    if (argc != 4) {    //  - 校验参数数量应为 4（包含程序名），即用户需要提供 3 个参数；不满足则打印用法并以错误码 1 退出。
        std::cerr << "Usage: ./decode <file_path> <pri_key> <output_file>" << std::endl;
        return 1;
    }
    std::string input_file_path = argv[1];  //  - 参数1：输入日志文件路径（待解码的二进制文件）。
    std::string pri_key = argv[2];          //  - 参数2：服务器私钥（HEX 文本，后续会转为二进制用于 ECDH）。
    std::string output_file_path = argv[3]; //    参数3：输出文本文件路径（解码后的可读日志）
#else   //    - #else 分支：走硬编码测试（开发调试方便）。
    std::string input_file_path = "D:/logger/loggerdemo_20241023210917.log";
    std::string pri_key = "FAA5BBE9017C96BF641D19D0144661885E831B5DDF52539EF1AB4790C05E665E";
    std::string output_file_path = "D:/logger/d.txt";
#endif
    try {   //  - 捕获内部流程（如读文件/魔数校验/ECDH/解密/解压/反序列化）抛出的异常；打印错误并以 1 退出。
        decode_formatter = std::make_unique<DecodeFormatter>();             //  - 初始化全局格式化器（后续 DecodeItemData 会用到，必须先初始化）。
        decode_formatter->SetPattern("[%l][%D:%S][%p:%t][%F:%f:%#]%v");     //  - 设置输出样式（级别/日期/线程/文件:函数:行号/消息体等占位符）。
        decompress = std::make_unique<logger::compress::ZstdCompress>();    //  - 选择具体解压实现（通过接口 Compression 进行多态绑定，可替换为 zlib 等）。
        DecodeFile(input_file_path, pri_key, output_file_path);             //  - 执行完整解码流程：读文件 → 逐块 ECDH→AES 解密 → Zstd 解压 → Protobuf 解析 → 格式化 → 逐块写出。
    } catch (const std::exception& e) {
        std::cerr << "Decode failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;   //成功退出
}


