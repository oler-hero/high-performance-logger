#include "crypt/crypt.h"

#include "aes.h"
#include "base64.h"
#include "cryptlib.h"
#include "eccrypto.h"
#include "filters.h"
#include "hex.h"
#include "modes.h"
#include "oids.h"
#include "osrng.h"

namespace logger {
namespace crypt {

// 为了方便使用 Crypto++ 的类型，引入常用的类型
using CryptoPP::byte;

std::string BinaryKeyToHex(const std::string& binary_key) {
    std::string hex_key;            //创建一个空字符串，用来存放最后的结果（十六进制字符串）
    CryptoPP::HexEncoder encoder;   //创建一个"十六进制编码器"（找来一个翻译官，专门把乱码翻译成十六进制）

    //attach只能接受一个指针：用 hex_key 创建了 StringSink 对象，但 StringSink 保存的是对 hex_key 的引用
    //StringSink(hex_key) = 创建一个"字符串水槽"，指向 hex_key
    //Attach = 把编码器的输出"接"到这个水槽上
    encoder.Attach(new CryptoPP::StringSink(hex_key));  
    //binary_key.data() = 获取二进制字符串的原始指针
    //reinterpret_cast<const byte*>(...) = 把 char* 转成 byte*（只是换个名字，内容不变）（从有符号字节变成了无符号字节）
    //size是数据有多长
    encoder.Put(reinterpret_cast<const byte*>(binary_key.data()), binary_key.size());       //喂数据给编码器
    encoder.MessageEnd();       //告诉编码器"输入结束了，把缓冲区里剩余的数据都处理完吧"  （告诉翻译官"没有更多要翻译的了，赶紧把手头的活干完"）
    return hex_key;
}

std::string HexKeyToBinary(const std::string& hex_key) {
    std::string binary_key;
    CryptoPP::HexDecoder decoder;   //解码器（把16进制弄成2进制）
    decoder.Attach(new CryptoPP::StringSink(binary_key));
    //为什么用无符号字符，因为put需要
    decoder.Put(reinterpret_cast<const byte*>(hex_key.data()), hex_key.size());      //：reinterpret_cast<const byte*>() 只转换 hex_key.data()，然后 hex_key.size() 是 Put() 的第二个参数。
    decoder.MessageEnd();
    return binary_key;
}

std::tuple<std::string, std::string> GenECDHKey() {
    CryptoPP::AutoSeededRandomPool rnd;     //创建一个随机数生成器 (密码学安全的)
    //EDCH:Elliptic Curve Diffie-Hellman（椭圆曲线 Diffie-Hellman）(一种密钥协商算法)
    //ECP = Elliptic Curve over Prime field（素数域上的椭圆曲线）  (椭圆曲线有很多种数学实现方式，这个是最常用的一种)
    //Domain = 参数域，包含椭圆曲线的所有参数  
    //用 "secp256r1 椭圆曲线参数" 来初始化它(指dh)
    CryptoPP::ECDH<CryptoPP::ECP>::Domain dh(CryptoPP::ASN1::secp256r1());      //创建一个基于 P-256 曲线的 ECDH 密钥协商系统
    //- SecByteBlock = 安全字节块（特殊的字节数组）（析构时自动把内存清零（更安全））
    //- dh.PrivateKeyLength() = 私钥需要多长？（对于 P-256 是 32 字节）
    //- dh.PublicKeyLength() = 公钥需要多长？（对于 P-256 是 65 字节）
    CryptoPP::SecByteBlock priv(dh.PrivateKeyLength()), pub(dh.PublicKeyLength());  //准备存储空间（准备两个保险箱，一个放钥匙（私钥），一个放锁（公钥））
    //- priv = 生成的私钥会写到这里
    //- pub = 生成的公钥会写到这里
    //内部：1. 用随机数生成一个私钥（32 字节随机数）
    //2. 用私钥计算出对应的公钥（数学运算）
    //3. 把结果写入 priv 和 pub
    dh.GenerateKeyPair(rnd, priv, pub);     //生成密钥对
    return std::make_tuple(std::string(reinterpret_cast<const char*>(priv.data()), priv.size()),
                            std::string(reinterpret_cast<const char*>(pub.data()), pub.size()));
}

std::string GenECDHSharedSecret(const std::string& client_pri, const std::string& server_pub) {
    using CryptoPP::byte;   //为了方便，后面可以直接写 byte 而不用写 CryptoPP::byte
    //和 GenECDHKey() 中一样，创建 ECDH 系统  (双方必须用同一个曲线（P-256），否则无法协商)
    CryptoPP::ECDH<CryptoPP::ECP>::Domain dh(CryptoPP::ASN1::secp256r1());
    //agreedvaluelength返回：它返回共享密钥的标准长度（对于 secp256r1 是 32 字节）。
    //共享密钥 = ECDH 计算后的固定长度结果，32字节
    //构造一个安全的存储空间
    CryptoPP::SecByteBlock shared(dh.AgreedValueLength());  //准备共享密钥的存储空间
    //正在构造一个包含公钥数据的安全字节数组(把字符串格式的密钥装进保险箱（SecByteBlock）)
    //std::string::data() 函数的定义是返回 const char*
    CryptoPP::SecByteBlock pub(reinterpret_cast<const byte*>(server_pub.data()), server_pub.size());
    CryptoPP::SecByteBlock priv(reinterpret_cast<const byte*>(client_pri.data()), client_pri.size());
    //- shared = 输出参数，计算结果写到这里
    //计算共享密钥
    if (!dh.Agree(shared, priv, pub)) {     //创造共享密钥，存在shared里面
        throw std::runtime_error("Failed to reach shared secret");  
    }
    return std::string(reinterpret_cast<const char*>(shared.data()), shared.size());
}


}  // namespace crypt
}  // namespace logger