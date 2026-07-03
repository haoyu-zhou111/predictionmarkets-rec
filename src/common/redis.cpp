#include "redis.h"
#include "config.h"

namespace predictionmarkets_rec {

namespace redis {

namespace {
    brpc::Channel redis_cli;
}

bool init() {
    brpc::ChannelOptions options;
    
    options.protocol            = brpc::PROTOCOL_REDIS;
    options.connection_type     = brpc::CONNECTION_TYPE_POOLED;
    options.connect_timeout_ms  = g_config.redis.connect_timeout_ms;
    options.timeout_ms          = g_config.redis.timeout_ms;
    options.max_retry           = g_config.redis.max_retry;

    char addr[256];
    snprintf(addr, sizeof(addr), "%s:%d", g_config.redis.host.c_str(), g_config.redis.port);
    if (redis_cli.Init(addr, &options) != 0) {
        ALOG(ERROR, "Failed to init redis channel");
        return false;
    }

    if (!g_config.redis.password.empty()) {
        brpc::Controller    cntl;
        brpc::RedisRequest  req;
        brpc::RedisResponse resp;
    
        req.AddCommand("AUTH %s", g_config.redis.password.c_str());
        redis_cli.CallMethod(nullptr, &cntl, &req, &resp, nullptr);
        if (cntl.Failed()) {
            ALOG(ERROR, "Redis auth failed: %s", cntl.ErrorText().c_str());
            return false;
        }
    }

    // 当前固定使用 db=0，SELECT 命令在连接池下仅对单条连接生效，不可靠。
    // 若后续需要切换 db，需与上游数仓对齐后统一处理。

    ALOG(INFO, "Redis client init successfully, addr: %s", addr);
    return true;
}

bool exec(const std::vector<std::vector<std::string>>& commands, brpc::RedisResponse& resp) {
    brpc::Controller    cntl;
    brpc::RedisRequest  req;

    for (const auto& cmd : commands) {
        if (cmd.empty()) {
            continue;
        }
        std::vector<butil::StringPiece> parts(cmd.begin(), cmd.end());
        if (!req.AddCommandByComponents(parts.data(), parts.size())) {
            ALOG(ERROR, "redis add command failed");
            return false;
        }
    }

    redis_cli.CallMethod(nullptr, &cntl, &req, &resp, nullptr);
    if (cntl.Failed()) {
        ALOG(ERROR, "redis exec failed: %s", cntl.ErrorText().c_str());
        return false;
    }

    return true;
}

} // namespace redis

} // namespace predictionmarkets_rec
