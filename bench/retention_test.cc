// 验证“超量淘汰”定时任务是否真的会执行，以及极端配置下会不会把日志删光。
//
// 思路：EffectiveSink 构造时会立刻投递一次 ElimateFiles_（repeat=-1 表示无限重复，
// 首次是立即执行的），所以不需要等 1 分钟的 interval：
//   1. 先写一批日志，把目录撑到远超 total_size；
//   2. 销毁 sink；
//   3. 用同样的配置重新构造一个 sink —— 构造期的那次清理就会删掉旧文件；
//   4. 对比前后目录占用。
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <thread>

#include "logger/log_variadic_handle.h"
#include "logger/sinks/effective_sink.h"

namespace {

uint64_t DirBytes(const std::filesystem::path& dir, int* file_count) {
  uint64_t total = 0;
  int n = 0;
  std::error_code ec;
  for (auto it = std::filesystem::directory_iterator(dir, ec);
       !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    if (it->path().extension() == ".log") {
      total += it->file_size(ec);
      ++n;
    }
  }
  if (file_count) *file_count = n;
  return total;
}

logger::EffectiveSink::Conf MakeConf(const std::string& dir, int single_mb, int total_mb) {
  logger::EffectiveSink::Conf conf;
  conf.dir = dir;
  conf.prefix = "ret";
  conf.pub_key =
      "04827405069030E26A211C973C8710E6FBE79B5CAA364AC111FB171311902277537F8852EADD17EB339EB7CD0BA2490A58CDED2C702DFC1E"
      "FC7EDB544B869F039C";
  // 注意：Conf 里 single_size / total_size 的字段类型是 megabytes，
  // 传 kilobytes{256} 会被 space 的转换构造函数用整数除法截断成 0（另一个坑）。
  conf.single_size = logger::megabytes{single_mb};
  conf.total_size = logger::megabytes{total_mb};
  conf.interval = std::chrono::minutes{1};
  return conf;
}

struct Result {
  int before_n;
  uint64_t before_bytes;
  int after_n;
  uint64_t after_bytes;
};

Result RunScenario(const std::string& dir, int single_mb, int total_mb, int records) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);

  std::string payload(2048, 'x');
  std::mt19937 rng(2026);
  std::uniform_int_distribution<int> dist('a', 'z');
  for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(dist(rng));

  {
    auto sink = std::make_shared<logger::EffectiveSink>(MakeConf(dir, single_mb, total_mb));
    logger::LogHandle handle(sink);
    logger::SourceLocation loc{__FILE__, __LINE__, __FUNCTION__};
    for (int i = 0; i < records; ++i) {
      handle.Log(logger::LogLevel::kInfo, loc, payload);
    }
    sink->Flush();
  }

  Result r{};
  r.before_bytes = DirBytes(dir, &r.before_n);

  {
    // 构造期的 POST_REPEATED_TASK 会立即执行一次 ElimateFiles_
    auto sink = std::make_shared<logger::EffectiveSink>(MakeConf(dir, single_mb, total_mb));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
  }

  r.after_bytes = DirBytes(dir, &r.after_n);
  return r;
}

}  // namespace

int main() {
  bool ok = true;

  {
    Result r = RunScenario("retention_logs", 1, 3, 8000);
    printf("[场景1 正常配置 single=1MB total=3MB]\n");
    printf("  清理前: %d 个文件, %.2f MB\n", r.before_n,
           static_cast<double>(r.before_bytes) / 1024.0 / 1024.0);
    printf("  清理后: %d 个文件, %.2f MB\n", r.after_n,
           static_cast<double>(r.after_bytes) / 1024.0 / 1024.0);
    bool pass = r.after_bytes < r.before_bytes / 2 && r.after_n > 0;
    printf("  %s\n\n", pass ? "PASS: 超量淘汰生效，且保留了最新文件" : "FAIL");
    ok = ok && pass;
  }

  {
    // 极端配置：总预算比单个分卷还小。修复前会把所有文件删光（包括最新的）。
    Result r = RunScenario("retention_logs_edge", 1, 1, 8000);
    printf("[场景2 极端配置 single=1MB total=1MB]\n");
    printf("  清理前: %d 个文件, %.2f MB\n", r.before_n,
           static_cast<double>(r.before_bytes) / 1024.0 / 1024.0);
    printf("  清理后: %d 个文件, %.2f MB\n", r.after_n,
           static_cast<double>(r.after_bytes) / 1024.0 / 1024.0);
    bool pass = r.after_n == 1;
    printf("  %s\n\n", pass ? "PASS: 保留了最新 1 个文件，没有删光" : "FAIL: 文件被删光或数量异常");
    ok = ok && pass;
  }

  return ok ? 0 : 1;
}
