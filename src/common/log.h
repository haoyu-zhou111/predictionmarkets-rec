#pragma once

#include <string>

#include <butil/logging.h>
#include <butil/string_printf.h>

namespace predictionmarkets_rec {

// 初始化日志落盘：注册自定义 LogSink，所有级别始终写入同一个当前文件
// predictionmarkets-rec.log，级别打在每行行内。按自然日切分：跨天时把旧的
// predictionmarkets-rec.log 重命名为 predictionmarkets-rec.log.YYYYMMDD（归属日期），
// 新日志继续写回 predictionmarkets-rec.log；同时按文件名日期清理超过 retention_days
// 天的归档（<=0 表示不清理）。
// level 为最小输出级别（brpc severity：VERBOSE/DEBUG=-1, INFO=0, NOTICE=1, WARNING=2,
// ERROR=3；level=-1 才放行 Release 下的 LOG(DEBUG)）
bool log_init(const std::string& dir, int level, int retention_days);

} // namespace predictionmarkets_rec

#define ALOG(level, fmt, ...) \
    LOG(level) << butil::string_printf(fmt, ##__VA_ARGS__)
