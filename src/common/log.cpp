#include "log.h"

#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace predictionmarkets_rec {

namespace {

// 单一日志文件的基础名，实际文件为 <dir>/pm_rec_server.log.YYYYMMDDHH
constexpr char kLogName[] = "pm_rec_server.log";

// 唯一文件槽：文件名前缀、互斥锁、当前小时文件流
struct LogFile {
    std::string     prefix;
    std::mutex      mtx;
    std::ofstream   ofs;
    long            hour_bucket = -1;   // YYYYMMDDHH，-1 表示尚未打开
};

// tm 对应的 YYYYMMDDHH 小时桶（定长十进制，数值随时间单调递增，可直接比较）
long bucket_of(const struct tm& t) {
    return (t.tm_year + 1900) * 1000000L
         + (t.tm_mon + 1)     * 10000L
         + t.tm_mday          * 100L
         + t.tm_hour;
}

// 按文件名小时戳删除超过 retention_hours 的过期日志分片（定义见文件后半，滚动新文件时触发）
void clean_expired_logs(const std::string& dir, int retention_hours);

// 自定义 LogSink：所有级别写入同一个文件（按小时切分），级别打在每行行内。
// brpc 对 SetLogSink 注册的 sink 始终同步调用，且 OnLogMessage 会被多线程并发调用，故持一把锁。
class FileLogSink : public logging::LogSink {
public:
    FileLogSink(const std::string& dir, int retention_hours)
        : dir_(dir), retention_hours_(retention_hours) {
        if (!dir_.empty() && dir_.back() == '/') {
            dir_.pop_back();
        }
        file_.prefix = kLogName;
    }

    bool OnLogMessage(int severity, const char* file, int line,
                      const butil::StringPiece& content) override {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        struct tm tm_buf;
        localtime_r(&tv.tv_sec, &tm_buf);

        long bucket = bucket_of(tm_buf);

        LogFile& lf = file_;
        std::lock_guard<std::mutex> lock(lf.mtx);

        if (bucket != lf.hour_bucket) {
            if (lf.ofs.is_open()) {
                lf.ofs.close();
            }
            char name[512];
            snprintf(name, sizeof(name), "%s/%s.%04d%02d%02d%02d",
                     dir_.c_str(), lf.prefix.c_str(),
                     tm_buf.tm_year + 1900, tm_buf.tm_mon + 1,
                     tm_buf.tm_mday, tm_buf.tm_hour);
            lf.ofs.open(name, std::ios::app);
            lf.hour_bucket = bucket;

            // 每滚动到新小时文件时顺带清掉过期分片（生成/删除都按小时粒度，无需独立线程）
            clean_expired_logs(dir_, retention_hours_);
        }
        if (!lf.ofs.is_open()) {
            return true;   // 打不开就丢弃该条，不阻塞主流程
        }

        // 前缀：YYYY-MM-DD HH:MM:SS.us LEVEL tid file:line]
        char prefix[256];
        int n = snprintf(prefix, sizeof(prefix),
                         "%04d-%02d-%02d %02d:%02d:%02d.%06ld %s %ld %s:%d] ",
                         tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                         tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                         static_cast<long>(tv.tv_usec), level_name(severity),
                         static_cast<long>(syscall(SYS_gettid)), file, line);
        lf.ofs.write(prefix, n);
        lf.ofs.write(content.data(), content.size());
        lf.ofs.put('\n');

        // 每条即时刷盘：本服务日志量低，flush 开销可忽略，换取可实时 tail；
        // 且本 sink 常驻不析构，缓冲的 INFO/WARNING 到进程退出都不会落盘，必须主动 flush
        lf.ofs.flush();
        return true;   // 返回 true 接管，不再走 brpc 默认 stderr 输出
    }

private:
    static const char* level_name(int severity) {
        if (severity >= logging::BLOG_FATAL)   return "FATAL";
        if (severity == logging::BLOG_ERROR)   return "ERROR";
        if (severity == logging::BLOG_WARNING) return "WARNING";
        if (severity == logging::BLOG_NOTICE)  return "NOTICE";
        if (severity <  logging::BLOG_INFO)    return "DEBUG";
        return "INFO";
    }

    std::string dir_;
    LogFile     file_;
    int         retention_hours_ = 0;
};

FileLogSink* g_file_log_sink = nullptr;   // 进程生命周期常驻，不释放

// 按文件名小时戳删除超过 retention_hours 的日志分片（不依赖 mtime，mtime 会被拷贝/touch 改动）。
// retention_hours <= 0 视为不清理。
void clean_expired_logs(const std::string& dir, int retention_hours) {
    if (retention_hours <= 0) {
        return;
    }
    time_t cutoff_time = time(nullptr) - static_cast<time_t>(retention_hours) * 3600;
    struct tm tm_buf;
    localtime_r(&cutoff_time, &tm_buf);
    long cutoff = bucket_of(tm_buf);   // 早于此小时桶的分片删除

    DIR* dp = opendir(dir.c_str());
    if (dp == nullptr) {
        return;
    }
    const std::string prefix = std::string(kLogName) + ".";   // pm_rec_server.log.
    for (struct dirent* ent = readdir(dp); ent != nullptr; ent = readdir(dp)) {
        std::string name = ent->d_name;
        if (name.size() != prefix.size() + 10) {              // 前缀 + 10 位 YYYYMMDDHH
            continue;
        }
        if (name.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        std::string stamp = name.substr(prefix.size());
        if (stamp.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        if (std::stol(stamp) < cutoff) {
            ::unlink((dir + "/" + name).c_str());
        }
    }
    closedir(dp);
}

} // namespace

bool log_init(const std::string& dir, int level, int retention_hours) {
    if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "create log dir failed: %s (errno=%d)\n", dir.c_str(), errno);
        return false;
    }

    g_file_log_sink = new FileLogSink(dir, retention_hours);
    logging::SetLogSink(g_file_log_sink);
    logging::SetMinLogLevel(level);
    return true;
}

} // namespace predictionmarkets_rec
