#pragma once

#include <string>

#include <butil/logging.h>
#include <butil/string_printf.h>

namespace predictionmarkets_rec {

// 初始化日志落盘：注册自定义 LogSink，所有级别写入同一文件 pm_rec_server.log，
// 按小时切分（pm_rec_server.log.YYYYMMDDHH），级别打在每行行内；滚动到新小时文件时
// 顺带按文件名小时戳清理超过 retention_hours 的旧分片（<=0 表示不清理）。
// level 为最小输出级别（brpc severity：VERBOSE/DEBUG=-1, INFO=0, NOTICE=1, WARNING=2,
// ERROR=3；level=-1 才放行 Release 下的 LOG(DEBUG)）
bool log_init(const std::string& dir, int level, int retention_hours);

} // namespace predictionmarkets_rec

#define ALOG(level, fmt, ...) \
    LOG(level) << butil::string_printf(fmt, ##__VA_ARGS__)
