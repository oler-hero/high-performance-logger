#include "crypt/aes_crypt.h"

#include <algorithm>
#include <stdexcept>

#include "aes.h"
#include "base64.h"
#include "cryptlib.h"
#include "eccrypto.h"
#include "filters.h"
#include "hex.h"
#include "modes.h"
#include "oids.h"
#include "osrng.h"


namespace logger::crypt {

using CryptoPP::byte;

AESCrypt::AESCrypt(std::string key) {
    if (key.size() != 32) {
        throw std::invalid_argument(
            "aes-256密钥必须是32字节,当前：" + std::to_string(key.size())
        );
    }
    key_ = std::move(key);
}

AESCrypt::~AESCrypt() {
    //三步清零，确保密钥不残留在内存中
    std::fill(key_.begin(), key_.end(), 0);     //填充0
    key_.clear();                               //清空
    key_.shrink_to_fit();                       //释放内存
}

//返回string因为 string 自动管理内存
std::string AESCrypt::GenerateKey() {
    CryptoPP::AutoSeededRandomPool rng;     //密码学安全的随机数生成器
    byte key[32];                           //aes-256需要32位密钥
    rng.GenerateBlock(key, sizeof(key));    //"用 rng 生成随机数据，填充到 key 数组中，填充 32 字节"
    return std::string(reinterpret_cast<const char*>(key), sizeof(key));    //接口统一，密钥都用string
    //为什么返回string   :  1. 函数签名要求<br>2. 方便内存管理<br>3. 统一接口
}

std::string AESCrypt::GenerateIV() {
    CryptoPP::AutoSeededRandomPool rng;
    //CryptoPP::AES::BLOCKSIZE 是什么？ :这是一个常量（编译期就确定的数字）值为16,为什么用：可读性，防止错误
    //byte 类型专门用来表示字节数据，为什么byte
    //1.IV = 16 字节的随机二进制数据
    //2.Crypto++ 的 API 要求
    //3.与其他密码学函数兼容
    byte iv[CryptoPP::AES::BLOCKSIZE];  //aes块大小固定16字节，这是一个16的数组
    rng.GenerateBlock(iv, sizeof(iv));
    return std::string(reinterpret_cast<const char*>(iv), sizeof(iv));
}

// input - 明文数据的指针
// input_size - 明文数据的长度
// output - 输出参数（引用），加密后的结果写入这里
void AESCrypt::Encrypt(const void* input, size_t input_size, std::string& output) {
    //步骤1：生成随机IV（每次加密都不同）
    std::string iv = GenerateIV();

    //步骤2：创建AES-CBC加密器
    //拆解：CBC = Cipher Block Chaining（密码块链接模式）一种aes的加密模式（CBC = 把明文分块，每块和前一块的密文异或后再加密）
    //<>里的表示使用AES = Advanced Encryption Standard（高级加密标准）
    //encryption加密模式（而不是解密 Decryption）
    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption encryptor;

    //作用：配置加密器，使其知道：用哪个密钥加密，用哪个IV初始化
    encryptor.SetKeyWithIV(     // 同时设置密钥和 IV
        // 把 std::string 的 key_ 转成 byte* 指针
        reinterpret_cast<const byte*>(key_.data()),key_.size(),     //这里在设置密钥
        reinterpret_cast<const byte*>(iv.data())                    //配置iv，把 IV（std::string）转成 byte* 指针
    );

    //步骤3：加密数据
    std::string ciphertext;     // 创建空字符串，用来存放加密结果
    CryptoPP::StringSource(     //数据源：StringSource(把数据喂给后续的处理管道)
        reinterpret_cast<const byte*>(input),input_size,    //明文数据的起始地址（转成byte*类型）,和数据长度
        true,       //pumall = true,表示一次性处理所有数据而不是分批处理
        new CryptoPP::StreamTransformationFilter(   //流式转换过滤器，负责实际的加密操作，处理填充
            encryptor,  //(之前创建的)告诉过滤器用这个加密器处理数据    
            new CryptoPP::StringSink(ciphertext),    //数据处理完后，写入ciphertext字符串
            //解密时需要知道填充了多少字节，不填充无法区别填充和原文，因为有点填充是字节数不够16，有的是刚好16字节，但是也需要填充
            CryptoPP::StreamTransformationFilter::PKCS_PADDING      //PKCS7填充
        )
    );

    //步骤4：输出格式=IV+密文
    //为什么要包含IV？因为解密时需要相同的IV
    output.clear();
    output.reserve(iv.size() + ciphertext.size());      //预分配内存，避免多次重新分配
    output.append(iv);              //前16字节是iv
    output.append(ciphertext);      //后面是密文
}

std::string AESCrypt::Decrypt(const void* data, size_t size) {
    //步骤1：验证输入长度(至少要有IV 的16字节)      //为什么常量是16，因为aes是块加密算法，每一块的大小是16
    if (size < CryptoPP::AES::BLOCKSIZE) {      //CryptoPP::AES::BLOCKSIZE 是什么？ :这是一个常量（编译期就确定的数字）值为16,为什么用：可读性，防止错误
        return "";      //IV 必须和块大小一样（16字节）才能进行异或运算
    }

    //步骤2:提取iv和密文
    const byte* input = reinterpret_cast<const byte*>(data);
    const byte* iv = input;                                     //前16字节是iv, 把数据的起始地址赋给 input
    const byte* ciphertext = input + CryptoPP::AES::BLOCKSIZE;  //后面是密文,ciphertext 指向跳过 IV 后的位置
    size_t ciphertext_size = size - CryptoPP::AES::BLOCKSIZE;

    //步骤3：创建AES-CBC解密器
    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption decryptor;
    decryptor.SetKeyWithIV( 
        reinterpret_cast<const byte*>(key_.data()), key_.size(),    //密钥
        iv                                                          //iv ,从密文提取的
    );

    //步骤4：解密数据（带错误处理）
    std::string plaintext;      //存储解密的数据
    try {
        CryptoPP::StringSource(
            ciphertext, ciphertext_size,
            true,  //pumpAll
            new CryptoPP::StreamTransformationFilter(
                decryptor,
                new CryptoPP::StringSink(plaintext),
                CryptoPP::StreamTransformationFilter::PKCS_PADDING  //PKCS7填充
            )
        );
    } catch (const CryptoPP::Exception& e) {
        throw std::runtime_error("解密失败：" + std::string(e.what()));
    }
    return plaintext;

}



}