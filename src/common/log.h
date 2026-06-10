#pragma once

#include <butil/logging.h>
#include <butil/string_printf.h>

#define ALOG(level, fmt, ...) \
    LOG(level) << butil::string_printf(fmt, ##__VA_ARGS__)
