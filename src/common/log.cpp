#include "log.h"

#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace predictionmarkets_rec {

namespace {

// 每个级别一个文件槽：独立文件名前缀、互斥锁、当前小时文件流
struct LogFile {
    std::string     prefix;
    std::mutex      mtx;
    std::ofstream   ofs;
    long            hour_bucket = -1;   // YYYYMMDDHH，-1 表示尚未打开
};

// 自定义 LogSink：按 severity 把日志独占写入 info/warning/error 三个文件，按小时切分。
// brpc 对 SetLogSink 注册的 sink 始终同步调用，且 OnLogMessage 会被多线程并发调用，故每槽各持一把锁。
class FileLogSink : public logging::LogSink {
public:
    explicit FileLogSink(const std::string& dir) : dir_(dir) {
        if (!dir_.empty() && dir_.back() == '/') {
            dir_.pop_back();
        }
        info_.prefix    = "info";
        warning_.prefix = "warning";
        error_.prefix   = "error";
    }

    bool OnLogMessage(int severity, const char* file, int line,
                      const butil::StringPiece& content) override {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        struct tm tm_buf;
        localtime_r(&tv.tv_sec, &tm_buf);

        long bucket = (tm_buf.tm_year + 1900) * 1000000L
                    + (tm_buf.tm_mon + 1)     * 10000L
                    + tm_buf.tm_mday          * 100L
                    + tm_buf.tm_hour;

        LogFile& lf = pick(severity);
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
    // DEBUG(==INFO)/VERBOSE/INFO/NOTICE → info；WARNING → warning；ERROR/FATAL → error
    LogFile& pick(int severity) {
        if (severity >= logging::BLOG_ERROR)   return error_;
        if (severity == logging::BLOG_WARNING) return warning_;
        return info_;
    }

    static const char* level_name(int severity) {
        if (severity >= logging::BLOG_FATAL)   return "FATAL";
        if (severity == logging::BLOG_ERROR)   return "ERROR";
        if (severity == logging::BLOG_WARNING) return "WARNING";
        if (severity == logging::BLOG_NOTICE)  return "NOTICE";
        return "INFO";
    }

    std::string dir_;
    LogFile     info_;
    LogFile     warning_;
    LogFile     error_;
};

FileLogSink* g_file_log_sink = nullptr;   // 进程生命周期常驻，不释放

} // namespace

bool log_init(const std::string& dir, int level) {
    if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "create log dir failed: %s (errno=%d)\n", dir.c_str(), errno);
        return false;
    }

    g_file_log_sink = new FileLogSink(dir);
    logging::SetLogSink(g_file_log_sink);
    logging::SetMinLogLevel(level);
    return true;
}

} // namespace predictionmarkets_rec
