#pragma once

#include <memory>
#include <string>
#include <tuple>

namespace logger::crypt {

/// @brief 生成一对密钥(通信双方各自调用一次，生成自己的密钥对)
/// @return [私钥, 公钥]
std::tuple<std::string, std::string> GenECDHKey();

/// @brief 计算共享密钥
/// @param client_pri 自己的私钥 + 对方的公钥
/// @param server_pub 
/// @return 双方协商出的共享密钥（用于后续加密）(双方用不同参数计算，但结果相同)
std::string GenECDHSharedSecret(const std::string& client_pri, const std::string& server_pub);

/// @brief 二进制转十六进制
/// @param binary_key 密钥
/// @return 把密钥转成可读字符串，方便网络传输和存储（\x1a\x2b → "1a2b"）
std::string BinaryKeyToHex(const std::string& binary_key);

/// @brief 十六进制转二进制
/// @param hex_key 密钥
/// @return 把收到的十六进制字符串转回二进制，用于加密计算（"1a2b" → \x1a\x2b）
std::string HexKeyToBinary(const std::string& hex_key);

//抽象的加解密接口(可以有多种不同实现)
class Crypt {
 public:
  virtual ~Crypt() = default;

  //把明文加密成密文
  virtual void Encrypt(const void* input, size_t input_size, std::string& output) = 0;

  //把密文解密成明文
  virtual std::string Decrypt(const void* data, size_t size) = 0;
};

}