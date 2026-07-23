#define OPENSSL_SUPPRESS_DEPRECATED
#include <fstream>
#include <thread>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <openssl/md5.h>

#include "exp.h"
#include "common/config.h"
#include "common/log.h"
#include "common/redis.h"

namespace predictionmarkets_rec {

std::shared_ptr<const ExpConfigData>    g_exp_merged_config;

namespace {

const std::string SALT = "_predictionmarkets";

// 各召回通道实际消费的可选参数（见 recall.cpp）。parse_config 仅对集合内通道读取对应
// 字段，避免对不适用通道误报 "param not found"。enable / topk 所有通道通用，不入集合。
// - single_topk：trigger_index_recall（按 trigger 分桶取数）的通道使用
// - trigger_count：有 trigger 的通路（i2i 取近 N 次点击、u2i 取近 N 个作者/关注）使用
const std::unordered_set<std::string> SINGLE_TOPK_CHANNELS =
    {"icf", "i2v", "swing", "cate_hot", "follow_u2i", "recent_u2i"};
const std::unordered_set<std::string> TRIGGER_COUNT_CHANNELS =
    {"icf", "i2v", "swing", "follow_u2i", "recent_u2i"};

json exp_local_base;
json exp_local_groups;

void parse_config(const json& config_json, ExpConfig& exp_config) {
    auto& recall_config = get_json_obj(config_json, "recall");
    try {
        for (auto& [channel, config] : recall_config.items()) {
            bool     enable        = get_json(config, "enable", false);
            uint32_t topk          = get_json(config, "topk",   0u);
            // single_topk / trigger_count 仅对实际消费它们的通道读取（见上方集合），
            // 不适用通道不读、默认 0，避免刷 "param not found" 告警
            uint32_t single_topk   = SINGLE_TOPK_CHANNELS.count(channel)
                                         ? get_json(config, "single_topk",   0u) : 0u;
            uint32_t trigger_count = TRIGGER_COUNT_CHANNELS.count(channel)
                                         ? get_json(config, "trigger_count", 0u) : 0u;
            exp_config.recall.channel_config.emplace(channel, RecallChannelConfig{
                enable,
                trigger_count,
                single_topk,
                topk
            });
        };
    } catch (const std::exception& e) {
        ALOG(ERROR, "parse recall config exception: %s", e.what());
    }

    auto& rank_config = get_json_obj(config_json, "rank");
    try {
        exp_config.rank.bandit_enable = get_json(rank_config, "bandit_enable", true);
    } catch (const std::exception& e) {
        ALOG(ERROR, "parse rank config exception: %s", e.what());
    }

    auto& rerank_config = get_json_obj(config_json, "rerank");
    try {
        exp_config.rerank.enable = get_json(rerank_config, "enable", false);
        exp_config.rerank.newest_top_k = get_json(rerank_config, "newest_top_k", 0u);
        exp_config.rerank.author_gap = get_json(rerank_config, "author_gap", 0);
        exp_config.rerank.cate_gap = get_json(rerank_config, "cate_gap", 0);
        exp_config.rerank.fresh_top_k = get_json(rerank_config, "fresh_top_k", 0u);
        exp_config.rerank.fresh_days = get_json(rerank_config, "fresh_days", 7);
        exp_config.rerank.recent_days = get_json(rerank_config, "recent_days", 14);
        exp_config.rerank.recent_screens = get_json(rerank_config, "recent_screens", 3);
    } catch (const std::exception& e) {
        ALOG(ERROR, "parse rerank config exception: %s", e.what());
    }

    auto& op_config = get_json_obj(config_json, "op");
    try {
        exp_config.op.enable = get_json(op_config, "enable", false);
        for (auto& p : get_json_arr(op_config, "positions")) {
            exp_config.op.positions.emplace_back(p);
        }
        for (auto& item : get_json_arr(op_config, "items")) {
            exp_config.op.items.emplace_back(static_cast<std::string>(item));
        }
    } catch (const std::exception& e) {
        ALOG(ERROR, "parse op config exception: %s", e.what());
    }
}

bool load_local_config() {
    auto new_cfg = std::make_shared<ExpConfigData>();
    const std::string& path = g_config.exp.local_path;

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        ALOG(ERROR, "load local exp config failed: %s", path.c_str());
        return false;
    }

    try {
        json j;
        ifs >> j;

        if (!j.contains("base")) {
            ALOG(ERROR, "base exp config not exists");
            return false;
        }

        exp_local_base = get_json_obj(j, "base");

        for (uint32_t i = 0; i < 10; ++i) {
            std::string group_key = "group_" + std::to_string(i);
            exp_local_groups[i] = get_json_obj(j, group_key);
        }

        ALOG(INFO, "load local exp config success: %s", path.c_str());
        return true;
    } catch (const std::exception& e) {
        ALOG(ERROR, "parse local exp config exception: %s", e.what());
        return false;
    }
}

bool merge_redis_config() {
    brpc::RedisResponse resp;

    bool ret = redis::exec({{"GET", g_config.exp.redis_key}}, resp);
    if (!ret) {
        ALOG(WARNING, "exp config key %s not found", g_config.exp.redis_key.c_str());
        return false;
    }
    try {
        auto json_str = redis::parse<std::string>(resp.reply(0));
        // 远端 exp 为空（key 不存在/取到空串）时退回纯本地配置，避免 json::parse("")
        // 抛异常导致 exp_config_init 无限重试卡死启动。空可能是真没配，也可能是
        // redis 读取 / 上游写入出了问题，打 WARNING 提示留意。
        json j;
        if (json_str.empty()) {
            ALOG(WARNING, "remote exp config empty (key=%s), fall back to local exp config",
                 g_config.exp.redis_key.c_str());
            j = json::object();
        } else {
            j = json::parse(json_str);
        }

        json exp_redis_base = get_json_obj(j, "base");

        json merged = exp_local_base;
        json_merge(merged, exp_redis_base);

        ExpConfigData exp_final;

        for (uint32_t i = 0; i < 10; ++i) {
            json final_json = merged;
            json_merge(final_json, exp_local_groups[i]);

            std::string group_key = "group_" + std::to_string(i);
            json exp_redis_group = get_json_obj(j, group_key);
            json_merge(final_json, exp_redis_group);

            parse_config(final_json, exp_final.groups[i]);
        }

        ALOG(INFO, "load redis exp config success");
        std::atomic_store(&g_exp_merged_config, std::make_shared<const ExpConfigData>(exp_final));
        return true;
    } catch (const std::exception& e) {
        ALOG(ERROR, "parse exp config failed: %s", e.what());
        return false;
    }
}

bool exp_config_load() {
    if (!load_local_config()) {
        return false;
    }

    if (!merge_redis_config()) {
        return false;
    }
    return true;
}

} // namespace

bool exp_config_init() {
    try {
        while (true) {
            if (exp_config_load()) {
                ALOG(INFO, "exp config init successfully");
                return true;
            }

            ALOG(ERROR, "exp config init failed, retry after 1s");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        ALOG(ERROR, "exp config init fatal error: %s", e.what());
        return false;
    }
}

void exp_config_reload() {
    if (!merge_redis_config()) {
        ALOG(WARNING, "exp config reload failed, using old exp_config");
    } else {
        ALOG(INFO, "exp config reload success");
    }
}

uint32_t get_user_group(AnchorId anchor_id) {
    std::string key = anchor_id + SALT;
    unsigned char md5[16];
    MD5((const unsigned char*)key.data(), key.size(), md5);

    uint64_t h = 0;
    for (size_t i = 0; i < 8; i++) {
        h = (h << 8) | md5[i];
    }

    return (uint32_t)(llabs((int64_t)h) % 10);
}

} // namespace predictionmarkets_rec
