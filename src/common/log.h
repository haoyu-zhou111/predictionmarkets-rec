#pragma once

#include <string>

#include <butil/logging.h>
#include <butil/string_printf.h>

namespace predictionmarkets_rec {

// 初始化日志落盘：注册自定义 LogSink，按级别（info/warning/error）独占写文件，
// 文件按小时切分；level 为最小输出级别（brpc severity：INFO=0, NOTICE=1, WARNING=2, ERROR=3）
bool log_init(const std::string& dir, int level);

} // namespace predictionmarkets_rec

#define ALOG(level, fmt, ...) \
    LOG(level) << butil::string_printf(fmt, ##__VA_ARGS__)
