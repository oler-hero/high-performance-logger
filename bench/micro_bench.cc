// 微基准：把 Log() 的固定开销拆开，定位 P50 156µs 到底花在哪。
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "effective_msg.pb.h"
#include "logger/compress/zstd_compress.h"
#include "logger/crypt/aes_crypt.h"

using Clock = std::chrono::steady_clock;

template <typename F>
double TimeUs(int n, F&& f) {
  auto t0 = Clock::now();
  for (int i = 0; i < n; ++i) f(i);
  auto t1 = Clock::now();
  return std::chrono::duration<double, std::micro>(t1 - t0).count() / n;
}

int main() {
  const int kIters = 20000;
  const int kSize = 2048;

  std::string plain(kSize, 'x');
  for (int i = 0; i < kSize; ++i) plain[i] = 'a' + (i * 37 % 26);

  std::string key(32, 'k');
  logger::crypt::AESCrypt aes(key);
  logger::compress::ZstdCompress zstd;

  std::string out;
  out.resize(zstd.CompressBound(kSize));
  size_t compressed = zstd.Compress(plain.data(), plain.size(), out.data(), out.size());
  std::string compressed_str = out.substr(0, compressed);

  printf("payload=%dB compressed=%zuB\n", kSize, compressed);

  double iv = TimeUs(kIters, [](int) { logger::crypt::AESCrypt::GenerateIV(); });
  printf("GenerateIV (AutoSeededRandomPool per call) : %8.2f us/op\n", iv);

  double enc = TimeUs(kIters, [&](int) { aes.Encrypt(compressed_str.data(), compressed_str.size(), out); });
  printf("AES Encrypt (含 GenerateIV)                : %8.2f us/op\n", enc);

  double zc = TimeUs(kIters, [&](int) { zstd.Compress(plain.data(), plain.size(), out.data(), out.size()); });
  printf("zstd Compress level5                      : %8.2f us/op\n", zc);

  EffectiveMsg msg;
  msg.set_level(2);
  msg.set_timestamp(1763100000000LL);
  msg.set_pid(1234);
  msg.set_tid(5678);
  msg.set_line(88);
  msg.set_file_name("order_service.cc");
  msg.set_func_name("HandleCreateOrder");
  msg.set_log_info(plain);
  std::string pb;
  double pb_us = TimeUs(kIters, [&](int) {
    pb.resize(msg.ByteSizeLong());
    msg.SerializeToArray(pb.data(), static_cast<int>(pb.size()));
  });
  printf("Protobuf serialize                        : %8.2f us/op  (%zuB)\n", pb_us, pb.size());
  return 0;
}
