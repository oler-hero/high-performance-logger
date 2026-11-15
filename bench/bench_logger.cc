// 日志库压测：测“业务线程调用 Log() 到返回”的耗时与吞吐。
//
// 口径说明（面试时必须能讲清楚）：
//   1. 计时区间只包住 Log() 调用本身，不含最后的 Flush()。
//      这才是“业务线程被阻塞多久”的真实成本。
//   2. 吞吐 = 总条数 / 所有线程跑完的时间，多线程时是聚合吞吐。
//   3. 延迟分位数按每条 Log() 的纳秒耗时统计，P99 反映长尾。
//   4. 落盘体积统计目录下 .log 文件总和，不含 mmap 缓存文件。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "logger/logger.h"
#include "logger/log_variadic_handle.h"
#include "logger/sinks/effective_sink.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  int threads = 1;
  int records = 200000;
  int size = 2048;
  std::string mode = "random";  // random | template
  std::string dir = "bench_logs";
  int use_fmt = 0;              // 0: 直接调 Log(level, loc, view)；1: 走 EXT_LOG_INFO 宏（含 fmt 格式化）
  int no_flush = 0;             // 1: 不显式调 Flush，直接析构 sink，用来验证析构里有没有兜底刷盘
  std::string dict;             // 可选的 zstd 压缩字典文件
  std::string pub;              // 可选：覆盖服务端公钥（配合 crypt_example 做端到端解码验证）
};

Options ParseArgs(int argc, char* argv[]) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    std::string k = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "missing value for " << name << std::endl;
        std::exit(2);
      }
      return argv[++i];
    };
    if (k == "--threads") {
      opt.threads = std::stoi(next("--threads"));
    } else if (k == "--records") {
      opt.records = std::stoi(next("--records"));
    } else if (k == "--size") {
      opt.size = std::stoi(next("--size"));
    } else if (k == "--mode") {
      opt.mode = next("--mode");
    } else if (k == "--dir") {
      opt.dir = next("--dir");
    } else if (k == "--fmt") {
      opt.use_fmt = std::stoi(next("--fmt"));
    } else if (k == "--no-flush") {
      opt.no_flush = 1;
    } else if (k == "--dict") {
      opt.dict = next("--dict");
    } else if (k == "--pub") {
      opt.pub = next("--pub");
    } else {
      std::cerr << "unknown arg: " << k << std::endl;
      std::exit(2);
    }
  }
  if (opt.threads < 1) opt.threads = 1;
  if (opt.records < opt.threads) opt.records = opt.threads;
  return opt;
}

// 业务日志风格：固定字段名 + 高冗余，贴近真实服务端日志
std::string MakeTemplatePayload(int size, int seq) {
  static const char* kTokens[] = {
      "user_id=", "order_id=", "amount=", "status=PAID", "channel=alipay",
      "retry=0", "latency_ms=", "trace=7f3a9c2e1b4d5f60", "span=0012",
      "msg=\"create order success\"", "region=cn-hangzhou", "ver=2.1.3",
  };
  std::string s;
  s.reserve(size + 64);
  s += "2026-11-14 16:20:31.482 [INFO ] [order-svc] ";
  int i = 0;
  while (static_cast<int>(s.size()) < size) {
    s += kTokens[i % (sizeof(kTokens) / sizeof(kTokens[0]))];
    s += std::to_string((seq * 131 + i * 17) % 100000);
    s += ' ';
    ++i;
  }
  s.resize(size);
  return s;
}

std::string MakeRandomPayload(int size, uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> dist('a', 'z');
  std::string s(size, 'a');
  for (int i = 0; i < size; ++i) s[i] = static_cast<char>(dist(rng));
  return s;
}

double Percentile(const std::vector<uint32_t>& sorted, double p) {
  if (sorted.empty()) return 0.0;
  size_t idx = static_cast<size_t>(p * (sorted.size() - 1));
  return static_cast<double>(sorted[idx]);
}

uint64_t DirLogBytes(const std::filesystem::path& dir) {
  uint64_t total = 0;
  std::error_code ec;
  for (auto it = std::filesystem::directory_iterator(dir, ec);
       !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    if (it->path().extension() == ".log") {
      total += it->file_size(ec);
    }
  }
  return total;
}

}  // namespace

int main(int argc, char* argv[]) {
  Options opt = ParseArgs(argc, argv);

  // 预生成 32 份载荷轮流使用：避免把“造字符串”的时间算进压测
  std::vector<std::string> payloads;
  payloads.reserve(32);
  for (int i = 0; i < 32; ++i) {
    payloads.push_back(opt.mode == "template" ? MakeTemplatePayload(opt.size, i)
                                              : MakeRandomPayload(opt.size, 12345u + i));
  }

  std::error_code ec;
  std::filesystem::remove_all(opt.dir, ec);
  // 故意不创建目录：验证 EffectiveSink 自己会把多级目录建出来

  logger::EffectiveSink::Conf conf;
  conf.dir = opt.dir;
  conf.prefix = "bench";
  // 服务端公钥（P-256 未压缩点），仅用于压测，不涉及真实私钥
  conf.pub_key =
      "04827405069030E26A211C973C8710E6FBE79B5CAA364AC111FB171311902277537F8852EADD17EB339EB7CD0BA2490A58CDED2C702DFC1E"
      "FC7EDB544B869F039C";
  conf.single_size = logger::megabytes{4};
  // 压测期间不能让淘汰逻辑删文件，给一个极大的上限
  conf.total_size = logger::megabytes{200 * 1024};
  conf.interval = std::chrono::minutes{60};
  conf.dict_path = opt.dict;
  if (!opt.pub.empty()) conf.pub_key = opt.pub;

  auto sink = std::make_shared<logger::EffectiveSink>(conf);
  auto handle = std::make_shared<logger::VariadicLogHandle>(sink);
  // 注意：VariadicLogHandle 自己声明的 Log 模板会隐藏基类的
  // Log(LogLevel, SourceLocation, StringView)，所以这里取基类指针调用。
  logger::LogHandle* base_handle = handle.get();
  if (opt.use_fmt) {
    EXT_LOGGER_INIT(handle);
  }

  const int per_thread = opt.records / opt.threads;
  const int total_records = per_thread * opt.threads;

  std::vector<std::vector<uint32_t>> lat_us(opt.threads);
  std::vector<std::thread> workers;
  workers.reserve(opt.threads);

  // 屏障：所有线程就位后同时开跑，避免先启动的线程多算时间
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};

  auto worker = [&](int tid) {
    auto& lat = lat_us[tid];
    lat.reserve(per_thread);
    ready.fetch_add(1);
    while (!go.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    const auto& pl = payloads[tid % payloads.size()];
    logger::SourceLocation loc{__FILE__, __LINE__, __FUNCTION__};
    for (int i = 0; i < per_thread; ++i) {
      auto t0 = Clock::now();
      if (opt.use_fmt) {
        EXT_LOG_INFO("{}", pl);
      } else {
        base_handle->Log(logger::LogLevel::kInfo, loc, pl);
      }
      auto t1 = Clock::now();
      lat.push_back(static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1000));
    }
  };

  for (int t = 0; t < opt.threads; ++t) workers.emplace_back(worker, t);
  while (ready.load() != opt.threads) std::this_thread::yield();

  auto begin = Clock::now();
  go.store(true, std::memory_order_release);
  for (auto& w : workers) w.join();
  auto end = Clock::now();

  double secs = std::chrono::duration<double>(end - begin).count();
  double flush_secs = 0.0;

  if (opt.no_flush) {
    // 不显式 Flush，直接把 sink 析构掉：
    // 如果析构里没有兜底刷盘，缓存里不满阈值的这一批就永远不会出现在 .log 里
    if (opt.use_fmt) logger::LogFactory::Instance().SetLogHandle(nullptr);
    handle.reset();
    sink.reset();
  } else {
    auto flush_begin = Clock::now();
    sink->Flush();
    auto flush_end = Clock::now();
    flush_secs = std::chrono::duration<double>(flush_end - flush_begin).count();
  }

  std::vector<uint32_t> all;
  size_t total_n = 0;
  for (auto& v : lat_us) total_n += v.size();
  all.reserve(total_n);
  for (auto& v : lat_us) all.insert(all.end(), v.begin(), v.end());
  std::sort(all.begin(), all.end());

  uint64_t disk = DirLogBytes(opt.dir);
  double raw_mb = static_cast<double>(total_records) * opt.size / 1024.0 / 1024.0;
  double disk_mb = static_cast<double>(disk) / 1024.0 / 1024.0;

  std::cout.setf(std::ios::fixed);
  std::cout.precision(2);
  std::cout << "mode=" << opt.mode << " payload=" << opt.size << "B"
            << " threads=" << opt.threads << " records=" << total_records << "\n";
  std::cout << "write_sec=" << secs << "  throughput=" << (total_records / secs)
            << " rec/s  payload_rate=" << (raw_mb / secs) << " MB/s\n";
  std::cout << "latency_us: p50=" << Percentile(all, 0.50) << " p90=" << Percentile(all, 0.90)
            << " p99=" << Percentile(all, 0.99) << " p999=" << Percentile(all, 0.999)
            << " max=" << (all.empty() ? 0 : all.back()) << "\n";
  std::cout << "flush_sec=" << flush_secs << "\n";
  std::cout << "raw=" << raw_mb << "MB disk=" << disk_mb << "MB saved="
            << (raw_mb > 0 ? (1.0 - disk_mb / raw_mb) * 100.0 : 0.0) << "%\n";
  return 0;
}
