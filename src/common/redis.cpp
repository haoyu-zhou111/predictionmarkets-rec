#include <algorithm>
#include <memory>

#include <brpc/redis_cluster.h>
#include <brpc/controller.h>
#include <brpc/callback.h>

#include "redis.h"
#include "config.h"

namespace predictionmarkets_rec {

namespace redis {

namespace {
    brpc::RedisClusterChannel redis_cli;
}

bool init() {
    if (g_config.redis.nodes.empty()) {
        ALOG(ERROR, "redis nodes empty, check config");
        return false;
    }

    brpc::RedisClusterChannelOptions options;
    options.max_redirect                       = g_config.redis.max_redirect;
    options.channel_options.connect_timeout_ms = g_config.redis.connect_timeout_ms;
    options.channel_options.timeout_ms         = g_config.redis.timeout_ms;
    options.channel_options.max_retry          = g_config.redis.max_retry;

    // seed_nodes 形如 "ip:port,ip:port,..."，仅用于引导拓扑发现，之后按 CLUSTER SLOTS 路由
    std::string seeds;
    for (size_t i = 0; i < g_config.redis.nodes.size(); i++) {
        if (i > 0) {
            seeds += ",";
        }
        seeds += g_config.redis.nodes[i];
    }

    if (redis_cli.Init(seeds, &options) != 0) {
        ALOG(ERROR, "Failed to init redis cluster channel, seeds: %s", seeds.c_str());
        return false;
    }

    // 注：brpc RedisClusterChannel 暂不支持 AUTH，password 字段预留；当前集群无密码。
    ALOG(INFO, "Redis cluster client init successfully, seeds: %s", seeds.c_str());
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

BatchReplies exec_batch(const std::vector<std::vector<std::string>>& commands, size_t window) {
    BatchReplies out;
    out.replies.assign(commands.size(), nullptr);
    if (commands.empty()) {
        return out;
    }
    if (window == 0) {
        window = commands.size();
    }

    const size_t num_chunks = (commands.size() + window - 1) / window;
    out.responses.reserve(num_chunks);
    ALOG(INFO, "[TRACE] exec_batch: %lu commands, %lu chunks, window=%lu",
         commands.size(), num_chunks, window);

    std::vector<brpc::RedisRequest>                reqs(num_chunks);
    std::vector<std::unique_ptr<brpc::Controller>> cntls;
    std::vector<brpc::CallId>                      ids(num_chunks);
    cntls.reserve(num_chunks);

    // 分片组装并异步发出：每片一个 CallMethod，跑在独立 bthread，片间互不阻塞
    for (size_t c = 0; c < num_chunks; c++) {
        const size_t begin = c * window;
        const size_t end   = std::min(begin + window, commands.size());
        for (size_t k = begin; k < end; k++) {
            const auto& cmd = commands[k];
            std::vector<butil::StringPiece> parts(cmd.begin(), cmd.end());
            reqs[c].AddCommandByComponents(parts.data(), parts.size());
        }
        cntls.emplace_back(new brpc::Controller);
        out.responses.emplace_back(new brpc::RedisResponse);
        ids[c] = cntls[c]->call_id();
        redis_cli.CallMethod(nullptr, cntls[c].get(), &reqs[c],
                             out.responses[c].get(), brpc::DoNothing());
        ALOG(INFO, "[TRACE] exec_batch: dispatched chunk %lu/%lu", c + 1, num_chunks);
    }
    ALOG(INFO, "[TRACE] exec_batch: all %lu chunks dispatched, start joining", num_chunks);

    // 等待所有分片完成
    for (size_t c = 0; c < num_chunks; c++) {
        brpc::Join(ids[c]);
        ALOG(INFO, "[TRACE] exec_batch: joined chunk %lu/%lu", c + 1, num_chunks);
    }

    // 按原始下标回填 reply 指针（各条命令非空、位置对齐）；失败分片其命令保持 nullptr，调用方判空
    for (size_t c = 0; c < num_chunks; c++) {
        if (cntls[c]->Failed()) {
            ALOG(WARNING, "redis exec_batch chunk %lu failed: %s",
                 c, cntls[c]->ErrorText().c_str());
            continue;
        }
        const brpc::RedisResponse& resp = *out.responses[c];
        const size_t begin = c * window;
        const size_t end   = std::min(begin + window, commands.size());
        const size_t n     = static_cast<size_t>(resp.reply_size());
        for (size_t k = begin; k < end; k++) {
            const size_t local = k - begin;
            if (local < n) {
                out.replies[k] = &resp.reply(local);
            }
        }
    }

    return out;
}

} // namespace redis

} // namespace predictionmarkets_rec
