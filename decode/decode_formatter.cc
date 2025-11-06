#include "decode_formatter.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "effective_msg.pb.h"
/**
 * @brief EffectiveMsg custom pattern
 *
 * %l log_level
 * %D timestamp date
 * %S timestamp seconds
 * %M timestamp milliseconds
 * %p process_id
 * %t thread_id
 * %# line
 * %F file_name
 * %f func_name
 * %v log_info
 *
 * such as: [%l][%D:%S][%p:%t][%F:%f:%#]%v
 */

/// @brief 把数值级别映射为单字符 "V/D/I/W/E/F"
/// @param level 整型级别码。
/// @return 未识别返回 "U"。
static const char* GetLevelStr(int32_t level) {
    switch (level) {
        case 0:
            return "V";
        case 1:
            return "D";
        case 2:
            return "I";
        case 3:
            return "W";
        case 4:
            return "E";
        case 5:
            return "F";
        default:
            return "U";
    }
}

/// @brief  将毫秒时间戳 （转换）→ 本地时间字符串 "YYYY-MM-DD HH:MM:SS"
/// @param milliseconds 自纪元起的毫秒数。
/// @return 本地时间字符串
std::string MillisecondsToDateString(long long milliseconds) {
    std::chrono::system_clock::time_point tp =  //用毫秒数构造一个 system_clock 的时间点 tp。time_point 是“某个时钟上的时间位置”。
        std::chrono::system_clock::time_point(std::chrono::milliseconds(milliseconds));
    std::time_t time_tt =std::chrono::system_clock::to_time_t(tp);  //把时间点转成 time_t（秒精度）。注意这里会截断到秒，毫秒部分会丢失。(time_t:C 语言里的“时间戳”类型，表示自 1970-01-01 00:00:00 UTC 起经过的“秒数”（整型）。)
    std::tm* timeinfo = std::localtime(&time_tt);   //把 time_t（秒）转换成本地时区的 std::tm。
    std::ostringstream oss;     // 创建字符串输出流，用来拼装格式化后的文本。(字符串输出流，你可以像往 std::cout 那样把内容“流进去”，最后用 str() 取出拼好的字符串。)
    oss << std::put_time(timeinfo, "%Y-%m-%d %H:%M:%S");    //用 put_time 按格式把 tm 写入流，得到如 "2025-11-04 20:15:36" 的字符串。这里不包含毫秒。
    return oss.str();   //取出流里的字符串返回。
}

/// @brief 默认格式（当未设置自定义 pattern 时使用），把常用字段拼成一行：(    - 形如：[level][timestamp][pid:tid][file:func:line]log_info)
/// @param msg 日志对象。
/// @return 拼好的字符串
std::string CombineLogMsg(const EffectiveMsg& msg) {
    std::string output;             //创建 C++ 字符串，作为最终返回的可变缓冲。
    char buffer[1024] = {0};        //分配一个 1024 字节的 C 风格临时缓冲，并置零。用它先把“头部信息”格式化出来（更高效，少分配）。
    //默认格式
    snprintf(buffer, sizeof(buffer), "[%d][%lld][%d:%d][%s:%s:%d]", msg.level(), msg.timestamp(), msg.pid(), msg.tid(),
             msg.file_name().c_str(), msg.func_name().c_str(), msg.line());     //snprintf 会最多写入 sizeof(buffer)-1 个字符，并自动以 '\0' 结尾，多余内容会被截断（安全但可能丢尾部）。
    output.append(buffer);          //把“头部字符串”追加到 output。
    output.append(msg.log_info());  //把日志正文拼在后面（正文可能较长，std::string 会自动扩容）。
    return output;                  //返回整行文本（注意：这里不加换行，调用它的 DecodeFormatter::Format 里会统一追加 "\n"）。
}   

/// @brief  抽象基类，表示“一个占位符或一段字面量”的格式化单元。
class FlagFormatter {
 public:
  FlagFormatter() = default;
  virtual ~FlagFormatter() = default;

  virtual void Format(const EffectiveMsg& msg, std::string& dest) = 0;
};

/// @brief  聚合直接出现的字面量字符（非占位符），原样追加。
class AggregateFormatter final : public FlagFormatter {
 public:
    AggregateFormatter() = default;
    ~AggregateFormatter() override = default;

    void AddCh(char ch) { str_ += ch; } //收集一个普通字符，把它追加到内部缓冲 str_ 中(在编译 pattern 时，遇到非占位符字符就不断调用这个函数累计起来，减少碎片化 append。)

    void Format(const EffectiveMsg& msg, std::string& dest) override { dest.append(str_); }    //把收集到的字面量整体追加到 dest。不做任何解析或转换。

 private:
  std::string str_;
};

/// @brief  追加日志级别简写。(使用 GetLevelStr(msg.level()) 把整型级别映射到 V/D/I/W/E/F。)
class LogLevelFormatter final : public FlagFormatter {
 public:
  LogLevelFormatter() = default;
  ~LogLevelFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override { dest.append(GetLevelStr(msg.level())); }   //为什么不直接 append 数字？因为 pattern %l 通常希望看到简洁的级别字母（如 [I]），更易读、与常见日志约定一致。
};

/// @brief 追加“人类可读”的日期时间。
class TimestampDateFormatter final : public FlagFormatter {
 public:
  TimestampDateFormatter() = default;
  ~TimestampDateFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override {
    dest.append(MillisecondsToDateString(msg.timestamp()));
  }
};

/// @brief 追加秒级时间戳。
class TimestampSecondsFormatter final : public FlagFormatter {
 public:
  TimestampSecondsFormatter() = default;
  ~TimestampSecondsFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override {
    //to_string:把数值秒转为十进制字符串（例如 1730723456）。
    dest.append(std::to_string(msg.timestamp() / 1000));    //除1000：将毫秒转为“秒”（整数除法，向下取整，丢弃毫秒的小数部分）。
  }
};

/// @brief  追加毫秒级时间戳。
class TimestampMillisecondsFormatter final : public FlagFormatter {
 public:
  TimestampMillisecondsFormatter() = default;
  ~TimestampMillisecondsFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override {
    dest.append(std::to_string(msg.timestamp()));
  }
};

/// @brief 追加进程号
class ProcessIdFormatter final : public FlagFormatter {
 public:
  ProcessIdFormatter() = default;
  ~ProcessIdFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override {
    dest.append(std::to_string(msg.pid()));     //msg.pid() 是整数
  }
};

/// @brief 追加线程号
class ThreadIdFormatter final : public FlagFormatter {
 public:
  ThreadIdFormatter() = default;
  ~ThreadIdFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override {
    dest.append(std::to_string(msg.tid()));
  }
};

/// @brief 追加源码行号。
class LineFormatter final : public FlagFormatter {
 public: 
  LineFormatter() = default;
  ~LineFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override {
    dest.append(std::to_string(msg.line()));
  }
};

/// @brief 追加文件名。
class FileNameFormatter final : public FlagFormatter {
 public:
  FileNameFormatter() = default;
  ~FileNameFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override {
    dest.append(msg.file_name());
  }
};

/// @brief 追加函数名。
class FuncNameFormatter final : public FlagFormatter {
 public:
  FuncNameFormatter() = default;
  ~FuncNameFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override {
    dest.append(msg.func_name());
  }
};

/// @brief 追加日志正文。
class LogInfoFormatter final : public FlagFormatter {
 public: 
  LogInfoFormatter() = default;
  ~LogInfoFormatter() override = default;

  void Format(const EffectiveMsg& msg, std::string& dest) override {
    dest.append(msg.log_info());
  }
};

//vector：保证顺序，便于遍历执行。
//unique_ptr：存放多态对象（避免对象切片），并且自动管理生命周期（不需要手动 delete）。
//多态容器让你在一个 vector 里同时放“字面量格式器”和“占位符格式器”。
std::vector<std::unique_ptr<FlagFormatter>> flag_formatters_;   //std::vector<std::unique_ptr<FlagFormatter>> flag_formatters_ 是“格式化流水线”的容器。里面按顺序放了一串“小格式器”对象（多态：FlagFormatter 的不同子类），每个对象只负责把一小段内容追加到最终字符串里。

/// @brief 根据占位符字母创建并压入相应的 FlagFormatter 子类。
/// @param flag     - %l：LogLevelFormatter（日志级别）
                    // - %D：TimestampDateFormatter（日期时间：yyyy-MM-dd HH:mm:ss）
                    // - %S：TimestampSecondsFormatter（时间戳秒）
                    // - %M：TimestampMillisecondsFormatter（时间戳毫秒）
                    // - %p：ProcessIdFormatter（进程号）
                    // - %t：ThreadIdFormatter（线程号）
                    // - %#：LineFormatter（源码行号）
                    // - %F：FileNameFormatter（文件名）
                    // - %f：FuncNameFormatter（函数名）
                    // - %v：LogInfoFormatter（日志正文）
                    // - 其他字符：按字面量处理，等价于输出 "%x"。
void HandleFlag(char flag) {
    switch (flag) {
    case 'l':
      flag_formatters_.push_back(std::make_unique<LogLevelFormatter>());
      break;
    case 'D':
      flag_formatters_.push_back(std::make_unique<TimestampDateFormatter>());
      break;
    case 'S':
      flag_formatters_.push_back(std::make_unique<TimestampSecondsFormatter>());
      break;
    case 'M':
      flag_formatters_.push_back(std::make_unique<TimestampMillisecondsFormatter>());
      break;
    case 'p':
      flag_formatters_.push_back(std::make_unique<ProcessIdFormatter>());
      break;
    case 't':
      flag_formatters_.push_back(std::make_unique<ThreadIdFormatter>());
      break;
    case '#':
      flag_formatters_.push_back(std::make_unique<LineFormatter>());
      break;
    case 'F':
      flag_formatters_.push_back(std::make_unique<FileNameFormatter>());
      break;
    case 'f':
      flag_formatters_.push_back(std::make_unique<FuncNameFormatter>());
      break;
    case 'v':
      flag_formatters_.push_back(std::make_unique<LogInfoFormatter>());
      break;
    default:
      auto formatter = std::make_unique<AggregateFormatter>();
      formatter->AddCh('%');
      formatter->AddCh(flag);
      flag_formatters_.push_back(std::move(formatter));
      break;
}
}

/// @brief 解析 pattern 字符串：普通字符收集到 AggregateFormatter（聚合字面量）；    - 遇到 % 视为占位符起始，交给 HandleFlag 生成对应的小格式器；    - 解析结束后把最后的 AggregateFormatter 入列。
/// @param pattern 字符串
void CompilePattern(const std::string& pattern) {
  auto end = pattern.end();
  std::unique_ptr<AggregateFormatter> user_chars;
  flag_formatters_.clear();
  for (auto it = pattern.begin(); it != end; ++it) {
    if (*it == '%') {
      if (user_chars) {
        flag_formatters_.push_back(std::move(user_chars));
      }
      ++it;
      if (it == end) {
        break;
      }
      HandleFlag(*it);
    } else {
      if (!user_chars) {
        user_chars = std::make_unique<AggregateFormatter>();
      }
      user_chars->AddCh(*it);
    }
  }
  if (user_chars) {
    flag_formatters_.push_back(std::move(user_chars));
  }
}

void DecodeFormatter::SetPattern(const std::string& pattern) {
  CompilePattern(pattern);
}

void DecodeFormatter::Format(const EffectiveMsg& msg, std::string& dest) {
  if (!flag_formatters_.empty()) {
    //custom pattern
    for (auto& formatter : flag_formatters_) {
      formatter->Format(msg, dest);
    }
  } else {
    // default pattern
    dest.append(CombineLogMsg(msg));
  }
  dest.append("\n");
}

