#pragma once

#include "crypt/crypt.h"

namespace logger::crypt {

class AESCrypt final : public Crypt {
 public:
  //key是加密密钥,通常来自 GenECDHSharedSecret() 的返回值,或者来自 GenerateKey() 生成的随机密钥
  explicit AESCrypt(std::string key);        // 创建一个 AES 加密器对象
  ~AESCrypt() override;

  //生成一个随机的 AES 密钥（32 字节）,工具函数，不依赖对象状态（例如本地加密文件的时候自己使用）
  static std::string GenerateKey();

  // 生成一个随机的初始化向量（16 字节）
  //iv是啥：初始化向量，让相同明文产生不同密文
  //相同明文 + 相同密钥 + 不同 IV → 不同密文
  //攻击者看不出任何规律
  //返回值：std::string：16 字节的随机二进制数据
  //通常不需要手动调用，Encrypt（）会自动生成
  static std::string GenerateIV();

  //把明文加密成密文
  //加密时自动生成新IV，包含在输出中。输出格式：[IV(16字节)]+[密文]
  void Encrypt(const void* input, size_t input_size, std::string& output) override;

  // 把密文解密成明文
  std::string Decrypt(const void* data, size_t size) override;

 private:
  //因为用的string不是CryptoPP::SecByteBlock，所以咱们析构要写点东西出来彻底清除内存
  std::string key_;     //存储 AES 加密密钥（32 字节）
  //std::string iv_; 不要这个iv了，每次开始动态生成iv     
};
}