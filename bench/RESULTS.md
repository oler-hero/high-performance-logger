# 压测结果

## 环境

| 项 | 值 |
| --- | --- |
| CPU | AMD Ryzen 7 7745HX（8 核 16 线程） |
| 内存 | 16 GB |
| 系统 | Windows 11 |
| 编译器 | MinGW-w64 g++ 13.2.0，`-O3 -DNDEBUG` |
| 磁盘 | SSD |

> 说明：本机 MSVC 缺 Windows SDK，无法编译，因此用 MinGW 的 `-O3` 构建。
> 与 MSVC `/O2` 会有几个百分点的差异，但数量级和结论一致。

## 口径

- 计时区间只包住 `Log()` 调用本身，**不含**最后的 `Flush()` —— 这才是业务线程被阻塞的时间。
- 吞吐 = 总条数 / 所有线程跑完的时间；多线程时是聚合吞吐。
- `saved` = 1 - 落盘字节 / 原始载荷字节，**端到端**统计（含 protobuf、zstd 帧头、IV、AES 填充、ItemHeader、ChunkHeader）。
  这和 README 里"只算压缩器输入输出"的口径不同，小日志会明显更差。

## 结果

## 最新一轮：去掉全局锁 + 字典压缩

> 下面的"去锁前"表格保留，用于对比。这一节是当前代码的实测值。

### 去掉 Log() 里的全局锁（压缩/加密改为每线程独立）

| 配置 | 去锁前 | 去锁后 | 变化 |
| --- | --- | --- | --- |
| 1 线程 2 KB 随机 | 33.6 ~ 44.6k 条/s，P99 35~50 µs | 28 ~ 33k 条/s，P99 54 µs | 单线程略降（见下） |
| 8 线程 2 KB 随机 | 30.5 ~ 36.2k 条/s，P99 **4.3 ~ 7.0 ms** | **58 ~ 63k 条/s**，P99 **203 ~ 291 µs** | 吞吐约 1.8×，**尾延迟降 20 倍** |

**去锁后的完整吞吐（当前代码，8 线程）：**

| 场景 | 吞吐 | P50 | P99 |
| --- | --- | --- | --- |
| 200 B 业务日志 | **256,136 条/s** | 26 µs | 62 µs |
| 2 KB 业务日志 | **123,597 ~ 127,356 条/s** | 38 µs | 106 ~ 109 µs |
| 2 KB 随机串 | 62,501 条/s | 42 µs | 203 µs |
| 2 KB 业务日志（单线程） | 36,161 条/s | 26 µs | 42 µs |

瓶颈已经转移到**磁盘写入带宽**：2 KB 业务日志压缩 70%（每条落盘约 580 B），能跑到 12 万；
同样 2 KB 的随机串只能压 33%（每条约 1300 B），吞吐就掉到 6.2 万。落盘字节数几乎决定了上限。

单线程略降的原因没有完全定位：一部分是笔记本长时间压测后的热降频（同一配置在会话早期的数字明显更高），
一部分可能是 MinGW 的 `thread_local`（emutls）带来的额外间接调用。**多线程的收益是明确的**：
8 线程吞吐接近翻倍，P99 从毫秒级回到百微秒级。

### 字典压缩（针对小日志）

用 `zstd --train` 在 600 份 200 B 业务日志样本上训了 16 KB 字典，`Conf::dict_path` 指定即可启用。

| 配置 | 无字典 | 有字典 |
| --- | --- | --- |
| 200 B 业务日志 | **膨胀 32.05%** | **压缩 23.97%** |
| 100 B 业务日志 | 膨胀 84.06% | 膨胀 36.05% |
| 2 KB 业务日志 | 压缩 67.57% | 压缩 69.91% |

端到端验证：用 `crypt_example` 生成一对 ECDH 密钥，带字典写 200 条 → `decoder` 用私钥 + 同一份字典解回文本，
内容与写入一致；不带字典的旧路径也能正常解（向后兼容）。

100 B 仍然是膨胀的——IV（16 B）+ AES 填充 + ItemHeader（8 B）+ protobuf 字段头这些固定开销，
在 100 B 的载荷上占比太高，字典也救不回来。要根治只能改格式（多条记录合成一帧，或整块用一个 IV）。

---

## 去锁前的数据（保留对比）

### 2 KB 随机串（对应"每条 2000 个随机字符"）

| 线程 | 吞吐 | P50 | P99 | 落盘体积 |
| --- | --- | --- | --- | --- |
| 1 | 33,600 ~ 44,600 条/s | 21 ~ 28 µs | 35 ~ 50 µs | 降 33% |
| 2 | 34,000 条/s | 28 µs | 657 µs | 降 32.4% |
| 4 | 32,400 条/s | 28 µs | 2.9 ms | 降 32.8% |
| 8 | 30,500 ~ 36,200 条/s | 26 ~ 29 µs | 4.3 ~ 7.0 ms | 降 33.1% |

**多线程没有扩展性**：8 线程 3.0~3.6 万，和单线程 3.4~4.5 万基本持平甚至更低；
但 **P99 从 35~50 µs 恶化到 4~7 ms**（差两个数量级）。
原因是 `EffectiveSink::Log()` 里一把全局 `mutex_` 罩住了压缩 + 加密 + 写缓存，
业务线程在这把锁上排队，线程越多排队越长。

> **测量波动说明**：单线程同一配置在不同时间跑，结果会在 33.6k ~ 44.6k 之间浮动，
> 8 线程也会在 30.5k ~ 36.2k 之间浮动。做过交替 A/B（1 线程 / 8 线程轮流、每轮间隔 15 s）
> 之后确认：**波动来自磁盘回写竞争和笔记本热降频，不是代码差异**。
> 所以结论要写成"吞吐不随线程数增长"，而不是"线程越多越慢"——后者是跑测顺序造成的假象。
> 引用数字时建议用区间，或者取保守值"约 4 万条/秒（单线程，2 KB）"。

### 2 KB 业务日志（固定字段名 + 变化数值）

| 线程 | 吞吐 | P50 | P99 | 落盘体积 |
| --- | --- | --- | --- | --- |
| 1 | 45,921 条/s | 20 µs | 34 µs | 降 67.6% |

### 200 B 业务日志（更接近线上真实体量）

| 线程 | 吞吐 | P50 | P99 | 落盘体积 |
| --- | --- | --- | --- | --- |
| 1 | 75,550 条/s | 12 µs | 19 µs | **升 32.1%** |

小日志端到端是**变大**的：protobuf 字段头 + zstd 帧头 + 16 B IV + AES 填充 + 8 B ItemHeader
这些固定开销，在 200 B 的载荷上占比过高。压缩器内部确实压掉了一部分，但落盘反而更大。

## 修复前后对比（2 KB 随机串，单线程）

| 指标 | 修复前 | 修复后 | 变化 |
| --- | --- | --- | --- |
| 吞吐 | 5,778 条/s | 3.5 ~ 4.5 万条/s | **6.1 ~ 7.7×** |
| P50 | 156 µs | 21 ~ 28 µs | **5.6 ~ 7.4×** |
| P99 | 319 µs | 35 ~ 50 µs | 6.4 ~ 9.1× |

根因见 `micro_bench`：`AESCrypt::GenerateIV()` 每次调用都构造一个
`CryptoPP::AutoSeededRandomPool`，而构造这个对象要初始化熵池（走系统 CSPRNG），
单次 123 µs，占 P50 的 78%。

微基准拆分（2 KB 载荷）：

| 环节 | 修复前 | 修复后 |
| --- | --- | --- |
| GenerateIV | 123.06 µs | 0.26 µs |
| AES 加密（含 IV） | 133.96 µs | 5.07 µs |
| zstd 压缩（level 5） | 1.17 µs | 1.17 µs |
| Protobuf 序列化 | 0.15 µs | 0.15 µs |

## 复现

```bash
# 配置（需要 MinGW + Ninja；用 MSVC 时把 -G 换成 "Visual Studio 17 2022"）
cd build/mingw-release
cmake ../.. -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -Dprotobuf_BUILD_TESTS=OFF -DZLIB_BUILD_TESTING=OFF
cmake --build . --target bench_logger micro_bench retention_test -j 16

./bench/bench_logger.exe --threads 1 --records 50000 --size 2048 --mode random --dir bench_logs
./bench/bench_logger.exe --threads 8 --records 240000 --size 2048 --mode random --dir bench_logs
./bench/bench_logger.exe --threads 1 --records 50000 --size 2048 --mode template --dir bench_logs
./bench/bench_logger.exe --threads 1 --records 100000 --size 200 --mode template --dir bench_logs
./bench/micro_bench.exe
./bench/retention_test.exe
```

> MinGW 下 cryptopp 会因 `-Werror` + 未使用参数报错，需要临时把
> `logger/third_party/cryptopp/CMakeLists.txt:35` 的 `-Werror` 去掉（本仓库未保留此改动）。

## 淘汰逻辑验证

`retention_test`：`single_size = 1 MB`、`total_size = 3 MB`，先写 10.44 MB（9 个分卷），
再重建 sink 触发一次清理 → 剩 2 个文件 2.40 MB。

注意边界：修复前若 `total_size <= single_size`，清理会把**所有**文件删掉（包括最新的），
因为第一个文件就超过了总预算。现在已改为"永远保留最新一个文件"，`retention_test`
的场景 2 专门覆盖这个边界（10.44 MB / 9 个文件 → 保留 1 个 1.20 MB）。

---

## 本轮改动与验证

| # | 改动 | 文件 | 验证方式 | 结果 |
| --- | --- | --- | --- | --- |
| 1 | 析构里兜底 `Flush()` | `sinks/effective_sink.h` | `bench_logger --no-flush`（100 条，低于 80% 阈值，不触发中途落盘） | 不调 Flush 也能写出 0.13 MB，与显式 Flush 一致；修复前为 0 |
| 2 | `ChunkHeader::pub_key` 值初始化 + memcpy 截断 | `sinks/effective_sink.h` `.cc` | 代码审查 | 不再把 63 字节未初始化内存写进每个 chunk 头 |
| 3 | 淘汰时保留最新文件 | `sinks/effective_sink.cc` | `retention_test` 场景 2 | 9 文件 10.44 MB → 1 文件 1.20 MB（修复前为 0 文件） |
| 4 | 高频内部日志加开关 | `internal_log.h`、`sinks/effective_sink.cc` | 统计 24 万条压测的 stdout | 138 KB / 约 1000 行 → 983 B / 9 行 |
| 5 | 压缩/加密移出全局锁，每线程独立压缩器 | `compress/compress.h`、`zstd_compress.*`、`zlib_compress.h`、`sinks/effective_sink.*` | 8 线程压测 | 58~63k 条/s，P99 203~291 µs（改前 30~36k，P99 4~7 ms） |
| 6 | zstd 字典压缩（可选） | `sinks/effective_sink.*`、`decode/decode.cc` | 200 B / 100 B / 2 KB 对比 + 端到端解码 | 200 B：膨胀 32% → 压缩 24%；2 KB：67.6% → 69.9% |

`LOG_VERBOSE` 默认关闭，需要排查落盘路径时用 `-DLOGGER_INTERNAL_VERBOSE` 打开。

另外修了一个构建问题：`decode/CMakeLists.txt` 没有把 protobuf/abseil 的头文件路径传给
`decoder`（`logger` 是以 PRIVATE 方式链接 protobuf 的），换用非 MSVC 工具链时会编译失败。

