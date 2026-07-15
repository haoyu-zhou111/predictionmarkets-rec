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

// 当前日志文件基础名，始终写入 <dir>/predictionmarkets-rec.log；
// 跨天时该文件被重命名为 <dir>/predictionmarkets-rec.log.YYYYMMDD（内容归属日期）
constexpr char kLogName[] = "predictionmarkets-rec.log";

// 唯一文件槽：互斥锁、当前 .log 文件流、当前所在自然日
struct LogFile {
    std::mutex      mtx;
    std::ofstream   ofs;
    long            day_bucket = -1;   // YYYYMMDD，-1 表示进程启动后尚未打开
};

// tm 对应的 YYYYMMDD 自然日桶（定长十进制，随日期单调递增，可直接比较）
long day_bucket_of(const struct tm& t) {
    return (t.tm_year + 1900) * 10000L
         + (t.tm_mon + 1)     * 100L
         + t.tm_mday;
}

// 按文件名日期删除超过 retention_days 的过期归档（定义见文件后半，跨天开新文件时触发）
void clean_expired_logs(const std::string& dir, int retention_days);

// 自定义 LogSink：所有级别始终写入同一个 .log（按自然日归档切分），级别打在每行行内。
// brpc 对 SetLogSink 注册的 sink 始终同步调用，且 OnLogMessage 会被多线程并发调用，故持一把锁。
class FileLogSink : public logging::LogSink {
public:
    FileLogSink(const std::string& dir, int retention_days)
        : dir_(dir), retention_days_(retention_days) {
        if (!dir_.empty() && dir_.back() == '/') {
            dir_.pop_back();
        }
        cur_path_ = dir_ + "/" + kLogName;
    }

    bool OnLogMessage(int severity, const char* file, int line,
                      const butil::StringPiece& content) override {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        struct tm tm_buf;
        localtime_r(&tv.tv_sec, &tm_buf);

        long today = day_bucket_of(tm_buf);

        std::lock_guard<std::mutex> lock(file_.mtx);

        roll_if_needed(today);
        if (!file_.ofs.is_open()) {
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
        file_.ofs.write(prefix, n);
        file_.ofs.write(content.data(), content.size());
        file_.ofs.put('\n');

        // 每条即时刷盘：本服务日志量低，flush 开销可忽略，换取可实时 tail；
        // 且本 sink 常驻不析构，缓冲的 INFO/WARNING 到进程退出都不会落盘，必须主动 flush
        file_.ofs.flush();
        return true;   // 返回 true 接管，不再走 brpc 默认 stderr 输出
    }

private:
    // 确保 file_.ofs 指向当天的 .log；跨天则先把旧 .log 归档为 .log.<归属日> 再开新文件。
    // 调用方须持 file_.mtx。
    void roll_if_needed(long today) {
        if (file_.day_bucket == today && file_.ofs.is_open()) {
            return;   // 快路径：仍在当天且文件已开
        }

        if (file_.day_bucket == -1) {
            // 进程启动后首次写：沿用已存在的 .log；若它是往日残留（跨天重启），先归档它
            struct stat st;
            if (::stat(cur_path_.c_str(), &st) == 0 && st.st_size > 0) {
                struct tm mt;
                localtime_r(&st.st_mtime, &mt);
                long file_day = day_bucket_of(mt);
                if (file_day < today) {
                    archive_current(file_day);
                }
            }
        } else if (today > file_.day_bucket) {
            // 运行中跨天：当前 .log 内容归属 file_.day_bucket，先关闭再归档
            if (file_.ofs.is_open()) {
                file_.ofs.close();
            }
            archive_current(file_.day_bucket);
        }

        if (file_.ofs.is_open()) {
            file_.ofs.close();
        }
        file_.ofs.open(cur_path_, std::ios::app);
        file_.day_bucket = today;

        // 每次跨天开新文件时顺带清掉过期归档（无需独立线程）
        clean_expired_logs(dir_, retention_days_);
    }

    // 把当前 .log 重命名为 .log.<day>（day 为 YYYYMMDD）。
    // 目标已存在（时钟回拨/带 mtime 的文件恢复/多实例共目录等异常）则跳过，
    // 绝不覆盖已有归档；被跳过的旧数据继续留在 .log 里，采集不受影响。
    void archive_current(long day) {
        char target[512];
        snprintf(target, sizeof(target), "%s.%08ld", cur_path_.c_str(), day);
        struct stat st;
        if (::stat(target, &st) == 0) {
            return;
        }
        ::rename(cur_path_.c_str(), target);
    }

    static const char* level_name(int severity) {
        if (severity >= logging::BLOG_FATAL)   return "FATAL";
        if (severity == logging::BLOG_ERROR)   return "ERROR";
        if (severity == logging::BLOG_WARNING) return "WARNING";
        if (severity == logging::BLOG_NOTICE)  return "NOTICE";
        if (severity <  logging::BLOG_INFO)    return "DEBUG";
        return "INFO";
    }

    std::string dir_;
    std::string cur_path_;   // <dir>/predictionmarkets-rec.log
    LogFile     file_;
    int         retention_days_ = 0;
};

FileLogSink* g_file_log_sink = nullptr;   // 进程生命周期常驻，不释放

// 按文件名日期删除超过 retention_days 的归档（不依赖 mtime，mtime 会被拷贝/touch 改动）。
// retention_days <= 0 视为不清理。
void clean_expired_logs(const std::string& dir, int retention_days) {
    if (retention_days <= 0) {
        return;
    }
    time_t cutoff_time = time(nullptr) - static_cast<time_t>(retention_days) * 86400;
    struct tm tm_buf;
    localtime_r(&cutoff_time, &tm_buf);
    long cutoff = day_bucket_of(tm_buf);   // 早于此自然日的归档删除

    DIR* dp = opendir(dir.c_str());
    if (dp == nullptr) {
        return;
    }
    const std::string prefix = std::string(kLogName) + ".";   // predictionmarkets-rec.log.
    for (struct dirent* ent = readdir(dp); ent != nullptr; ent = readdir(dp)) {
        std::string name = ent->d_name;
        if (name.size() != prefix.size() + 8) {               // 前缀 + 8 位 YYYYMMDD
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

bool log_init(const std::string& dir, int level, int retention_days) {
    if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "create log dir failed: %s (errno=%d)\n", dir.c_str(), errno);
        return false;
    }

    g_file_log_sink = new FileLogSink(dir, retention_days);
    logging::SetLogSink(g_file_log_sink);
    logging::SetMinLogLevel(level);
    return true;
}

} // namespace predictionmarkets_rec
