#include <algorithm>
#include <string>

#include "recommend.h"
#include "bandit.h"
#include "feature.h"
#include "rank.h"
#include "recall.h"
#include "rerank.h"
#include "common/config.h"
#include "common/redis.h"

namespace predictionmarkets_rec {
namespace rec {

using json = nlohmann::json;

namespace {
json fill_response(Context& ctx) {
    std::vector<json> feed_array;
    feed_array.reserve(ctx.final_results.size());
    for (const auto& p : ctx.final_results) {
        feed_array.push_back({
            {"item_id", p.first},
            {"score", ctx.rank_scores[p.second]}
        });
    }

    return json{
        {"code", 200},
        {"message", "success"},
        {"request_id", ctx.request_id},
        {"timestamp", ctx.timestamp},
        {"data", {
            {"current_refresh_num", ctx.session_refresh_num},
            {"feed_list", feed_array},
            {"ext_info", {
                {"has_more", ctx.has_more},
                {"strategy", "base"}
            }}
        }}
    };
}

// 把本次结果写入 anchor 的推荐历史（LPUSH 最新 → LTRIM 保留最近 N 条 → EXPIRE），供下次软降权
void write_rec_history(Context& ctx) {
    if (ctx.anchor_id.empty() || ctx.final_results.empty()) {
        return;
    }
    const std::string key = g_config.rec_history.key_prefix + ctx.anchor_id;

    std::vector<std::string> lpush = {"LPUSH", key};
    for (const auto& p : ctx.final_results) {
        lpush.push_back(p.first);
    }

    brpc::RedisResponse resp;
    redis::exec({
        std::move(lpush),
        {"LTRIM",  key, "0", std::to_string(g_config.rec_history.max_len - 1)},
        {"EXPIRE", key, std::to_string(g_config.rec_history.ttl_sec)}
    }, resp);
}

} // namespace

json recommend(Context& ctx) {
    if (!ctx.item_pool) {
        return json{
            {"code", 500},
            {"message", "item pool not ready"},
            {"request_id", ctx.request_id},
            {"timestamp", ctx.timestamp},
            {"data", {
                {"current_refresh_num", ctx.session_refresh_num},
                {"feed_list", json::array()},
                {"ext_info", {
                    {"has_more", true},
                    {"strategy", "base"}
                }}
            }}
        };
    }

    recall(ctx);
    if (ctx.exp_config->groups[ctx.group_id].rank.bandit_enable) {
        bandit_rank(ctx);       // bandit 阶段跳过特征组合与 LR 精排
    } else {
        feature_combine(ctx);
        rank(ctx);
    }
    rerank(ctx);
    write_rec_history(ctx);
    return fill_response(ctx);
}

} // namespace rec
} // namespace predictionmarkets_rec
