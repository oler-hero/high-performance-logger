#include "sinks/effective_sink.h"

#include <fstream>

#include "compress/zlib_compress.h"
#include "compress/zstd_compress.h"
#include "crypt/aes_crypt.h"
#include "defer.h"
#include "formatter/effective_formatter.h"
#include "internal_log.h"
#include "utils/file_util.h"
#include "utils/sys_util.h"
#include "utils/timer_count.h"

namespace logger {
//做空实现，因为高性能sink不需要
void EffectiveSink::SetFormatter(std::unique_ptr<Formatter> formatter) {}

void EffectiveSink::SwapCache_() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::swap(master_cache_, slave_cache_);
}

bool EffectiveSink::NeedCacheToFile_() {
    return master_cache_->GetRatio() > 0.8;
}

void EffectiveSink::WirteToCache_(const void* data, uint32_t size) {
    detail::ItemHeader item_header;
    item_header.size = size;
    master_cache_->Push(&item_header, sizeof(item_header));
    master_cache_->Push(data, size);
}

std::filesystem::path EffectiveSink::GetFilePath_() {
    //这里是在进行一个内部组装，为什么用lambda：局部性：在 GetFilePath_() 内部，不污染外部命名空间
    //可访问成员：[this] 捕获当前对象，能访问 conf_.dir、conf_.prefix，简洁：无需单独声明，代码集中
    //同理使用auto的原因是因为用auto比直接声明的性能更好，避免 std::function 的间接调用开销
    auto GetDataFilePath = [this]() -> std::filesystem::path {
        std::time_t now = std::time(nullptr);   //获取当前时间戳，nullptr 表示不需要输出参数
        std::tm tm;                             //std::tm：存储年月日时分秒等字段的结构体
        LocalTime(&tm, &now);                   //转换为本地时间，不用自带localtime是因为localtime() 返回静态缓冲区指针，线程不安全
        char time_buf[32] = {0};                //32 字节缓冲区，足够存放格式化后的时间字符串
        std::strftime(time_buf, sizeof(time_buf), "%Y%m%d%H%M%S", &tm);     //格式化时间为字符串
        return (conf_.dir / (conf_.prefix + "_" + time_buf));
    };

    if (log_file_path_.empty()) {   //检查文件名字是否是空的
        log_file_path_ = GetDataFilePath().string() + ".log";   //GetDateTimePath() 返回 std::filesystem::path,所以需要.string才能用于瓶装字符串
    } else {    
        auto file_size = fs::GetFileSize(log_file_path_);               
        bytes single_bytes = space_cast<bytes>(conf_.single_size);      
        if (file_size > single_bytes.count()) {     //检查是否需要轮转，是就进行，否就直接返回已存在的文件名（不用操作，函数末尾返回）
            auto data_time_path = GetDataFilePath();
            auto file_path = data_time_path.string() + ".log";
            if (std::filesystem::exists(file_path)) {       //看文件名是否存在，存在就进行更新index环节
                int index = 0;
                //std::filesystem::directory_iterator表示遍历指定目录中的条目（文件和子目录）
                for (auto& p : std::filesystem::directory_iterator(conf_.dir)) {
                    //filename获取路径中的文件名部分（不含目录）
                    //string:将 path 转为 std::string
                    if (p.path().filename().string().find(data_time_path.filename().string()) != std::string::npos) {
                        ++index;
                    }
                }
                log_file_path_ = data_time_path.string() + "_" + std::to_string(index) + ".log";//转换成字符串才能拼接
            } else {
                log_file_path_ = file_path;
            }
        }
    }
    LOG_INFO("EffectiveSink::GetFilePath_: log_file_path={}", log_file_path_.string());
    return log_file_path_;
}

void EffectiveSink::ElimateFiles_() {
    LOG_INFO("EffectiveSink::ElimateFiles_: start");
    std::vector<std::filesystem::path> files;
    for (auto&p : std::filesystem::directory_iterator(conf_.dir)) {
        //path获取该条目的完整路径,extension获取文件扩展名
        if (p.path().extension() == ".log") {
            files.push_back(p.path());  //保存完整路径
        }
    }

    //按文件的最后修改时间从新到旧排序。
    std::sort(files.begin(), files.end(), [](const std::filesystem::path& lsh, const std::filesystem::path& rsh) {
        return std::filesystem::last_write_time(lsh) > std::filesystem::last_write_time(rsh);
    });
    // 如果 lhs 的修改时间 > rhs 的修改时间
    // → 返回 true → lhs 排在前面 → 新文件在前

    size_t total_bytes = space_cast<bytes>(conf_.total_size).count();
    size_t used_bytes = 0;
    for (auto& file : files) {
        used_bytes += fs::GetFileSize(file);
        if (used_bytes > total_bytes) { 
            LOG_INFO("EffectiveSink::ElimateFiles_: remove file={}", file.string());
            std::filesystem::remove(file);
        }
    }
}

void EffectiveSink::CacheToFile_() {    
    TIMER_COUNT("CacheToFile_");
    if (is_slave_free_.load()) {    //非空闲表示有数据待写入,这里是如果空闲就返回
        return;
    }

    if (slave_cache_->Empty()) {
        is_slave_free_.store(true);
        return;
    }

    //包裹的原因:作用域管理，RAII自动关闭文件，代码清晰
    {
        auto file_path = GetFilePath_();
        detail::ChunkHeader chunk_header;
        chunk_header.size = slave_cache_->Size();        //返回的是有效数据大小
        //memcpy要求传入指针，client_pub_key是string，所以用data获取其指针
        memcpy(chunk_header.pub_key, client_pub_key_.data(), client_pub_key_.size());
        std::ofstream ofs(file_path, std::ios::binary | std::ios::app);     //二进制追加模式打开文件
        //ofstream::write() 要求 const char*,reinterpret_cast 将指针按位重新解释为 char*
        ofs.write(reinterpret_cast<char*>(&chunk_header), sizeof(chunk_header));
        ofs.write(reinterpret_cast<char*>(slave_cache_->Data()), chunk_header.size);
        ofs.close();
    }

    slave_cache_->Clear();
    is_slave_free_.store(true);
}

void EffectiveSink::PrepareToFile_() {
    POST_TASK(task_runner_, [this](){ CacheToFile_(); });
}

void EffectiveSink::Flush() {
    TIMER_COUNT("Flush");       //性能计时宏,如果启用，会测量 Flush() 的执行时间
    PrepareToFile_();           //处理可能存在的待处理任务
    WAIT_TASK_IDLE(task_runner_);   //把队列刷干净

    if (is_slave_free_.load()) {    //如果 slave_cache_ 空闲，将 master_cache_ 的数据交换过来待处理
        is_slave_free_.store(false);
        SwapCache_();
    }
    PrepareToFile_();               //处理可能存在的任务
    WAIT_TASK_IDLE(task_runner_);   //刷干净队列，因为线程池使用 FIFO（先进先出）队列，任务按入队顺序被取出执行，所以最后的一个空任务结束就代表队列干净了
}

void EffectiveSink::Log(const LogMsg& msg) {
    //static thread_local:每个线程有自己独立的静态缓冲区,线程内复用，避免重复分配,线程间隔离，无需同步
    static thread_local MemoryBuf buf;  
    formatter_->Format(msg, &buf);      //第一步先格式化成字符串
    if (master_cache_->Empty()) {
        compress_->ResetStream();   //压缩器是有状态的，如果 master_cache_ 为空，说明：
        //上一个批次的数据已经全部写入并清空
        //现在是新的批次开始
        //需要重置压缩器状态，从"干净状态"开始
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);       //加锁保护全流程
        compressed_buf_.reserve(compress_->CompressBound(buf.size()));      //预留足够空间
        size_t compressed_size = 
            compress_->Compress(buf.data(), buf.size(), compressed_buf_.data(), compressed_buf_.size());    //压缩时存储返回值用于判断是否出错
        if (compressed_size == 0) {     //出错写出错并返回
            LOG_ERROR("EffectiveSink::Log: compress failed");
            return;
        }
        encryped_buf_.clear();      //将 size() 置为 0，容量通常保留，便于复用,因为上一次加密的数据可能还在
        encryped_buf_.reserve(compressed_size + 16);    //compressed_size + 16：+ 16 是为了容纳加密算法可能添加的开销,+16因为块大小是16
        if (encryped_buf_.empty()) {    //错误则写日志并返回
            LOG_ERROR("EffectiveSink::Log: encrypt failed");
            return;
        }
        WirteToCache_(&encryped_buf_, encryped_buf_.size());    //将日志写入主缓存
    }
    if (NeedCacheToFile_()) {
        if (is_slave_free_.load()) {
            is_slave_free_.store(false);
            SwapCache_();
        }
        PrepareToFile_();   //理论上是一种并发保护方法，但是貌似冗余了
    }
}

EffectiveSink::EffectiveSink(Conf conf) : conf_(std::move(conf)) {
    LOG_INFO("EffectiveSink: dir={}, prefix={}, pub_key={}, interval={}, single_size={}, total_size={}," 
            conf_.dir.string(), conf_.prefix, conf_.pub_key, conf_.interval.count(), conf_.single_size.count(),
            conf_.total_size.count());      //自定义的 space 里，.count() 返回内部的标量值,chrono同理
    if (std::filesystem::exists(conf_.dir)) {   //- exists：检查目录是否存在
        std::filesystem::create_directories(conf_.dir);     //- create_directories：不存在则创建（包括父目录）
    }   
    task_runner_ = NEW_TASK_RUNNER(10086);      //- NEW_TASK_RUNNER(10086)：创建标识为 10086 的线程池
    formatter_ = std::make_unique<EffectiveFormatter>();    //不加括号就不是生成对象，只是指向函数模板本身并没有调用它
    auto ecdh_key = crypt::GenECDHKey();    //一个不隶属与类，隶属于crypt空间的函数，生成一对公私钥
    auto client_pri = std::get<0>(ecdh_key);    //客户端私钥（本地保存）
    client_pub_key_ = std::get<1>(ecdh_key);    //客户端公钥（写入日志文件头部）
    LOG_INFO("EffectiveSink: client pub size", client_pub_key_.size());     //打印方便阅读和定位
    std::string svr_pub_key_bin = crypt::HexKeyToBinary(conf_.pub_key);     //将服务器公钥从十六进制转为二进制
    //client_pri 来自 GenECDHKey() 返回的就是二进制（32 字节），可直接用。
    //conf_.pub_key 多半是为了存储/传输而用的十六进制文本（长度通常 130 字符，P-256 未压缩公钥），所以要先 HexKeyToBinary 变回 65 字节原始公钥；
    std::string shared_secret = crypt::GenECDHSharedSecret(client_pri, svr_pub_key_bin);    //用客户端私钥和服务器公钥计算共享密钥    
    crypt_ = std::make_unique<crypt::AESCrypt>(shared_secret);  //- 使用共享密钥初始化 AES 加密器
    compress_ = std::make_unique<compress::ZstdCompress>();     //- 创建 Zstd 压缩器
    master_cache_ = std::make_unique<MMapAux>(conf_.dir / "master_cache");  //- master_cache_：主缓存（前台写入）
    slave_cache_  = std::make_unique<MMapAux>(conf_.dir / "slave_cache");   //- slave_cache_：从缓存（后台落盘）
    if (!master_cache_ || !slave_cache_) {
        throw std::runtime_error("EffectiveSink::EffectiveSink: create mmap failed");
    }

    if (!slave_cache_->Empty()) {       //启动时若 slave_cache_ 里有“上次残留的数据”，就立刻触发一次落盘并“等它完成”，保证状态干净后再继续初始化。
        is_slave_free_.store(false);
        PrepareToFile_();
        WAIT_TASK_IDLE(task_runner_);   //是的，这会阻塞当前线程；但这里只在构造函数（启动清理）和 Flush()（显式同步刷新）用，是“需要同步完成”的场景。正常 Log() 路径不会这么阻塞。
    }

    if (!master_cache_->Empty()) {
        if (is_slave_free_.load()) {
            is_slave_free_.store(false);
            SwapCache_();     
        }
        PrepareToFile_();
    }
    
    //- 投递重复任务，每 conf_.interval 执行一次 ElimateFiles_()
    //- -1 表示无限重复
    //- 作用：定期清理旧日志文件，控制总大小
    POST_REPEATED_TASK(task_runner_, [this]() { ElimateFiles_(); }, conf_.interval, -1);
}

}