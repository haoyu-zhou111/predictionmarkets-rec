#include <thread>
#include <nlohmann/json.hpp>

#include "data/exp.h"
#include "data/feature_manifest.h"
#include "data/i2i_index.h"
#include "data/item_feature.h"
#include "data/item_pool.h"
#include "data/model.h"
#include "data/user_feature.h"
#include "fetcher.h"
#include "common/config.h"
#include "common/log.h"
#include "common/redis.h"

namespace predictionmarkets_rec {

namespace fetcher {

namespace {

void run_item_pool() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_config.sync.available_item_interval_ms));
        item_pool_load();
    }
}

// item_feature 本阶段停载，reload 线程暂停（架子保留待后续启用）
// void run_item_feature() {
//     while (true) {
//         std::this_thread::sleep_for(std::chrono::milliseconds(g_config.sync.item_feature_interval_ms));
//         item_feature_reload();
//     }
// }

void run_exp_config() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_config.sync.exp_config_interval_ms));
        exp_config_reload();
    }
}

bool recall_index_init() {
    if (!i2i_index_init(g_config.data_path.icf_index,   g_i2i_index_map["icf"]))   return false;
    if (!i2i_index_init(g_config.data_path.i2v_index,   g_i2i_index_map["i2v"]))   return false;
    if (!i2i_index_init(g_config.data_path.swing_index, g_i2i_index_map["swing"])) return false;
    return true;
}

} // namespace

bool init() {
    try {
        if (!item_pool_init())        return false;
        if (!item_feature_init())     return false;
        if (!user_feature_init())     return false;
        if (!feature_manifest_init()) return false;
        if (!recall_index_init())     return false;
        if (!lr_model_init())         return false;
        if (!exp_config_init())       return false;

        std::thread(run_item_pool).detach();
        // item_feature 本阶段停载（见 item_feature_init），reload 线程一并注释，架子保留
        // std::thread(run_item_feature).detach();
        std::thread(run_exp_config).detach();

        ALOG(INFO, "Fetcher init success: all data ready");
        return true;
    } catch (const std::exception& e) {
        ALOG(ERROR, "fetcher start failed: %s", e.what());
        return false;
    }
}

void fetch_user_context(Context& ctx) {
    std::vector<std::string> cmds;
    std::vector<std::string> keys;

    //black list
    cmds.emplace_back("smembers");
    keys.emplace_back(g_config.user_context.blacklist_redis_key + ctx.user_id);

    //history
    cmds.emplace_back("lrange");
    keys.emplace_back(g_config.user_context.history_redis_key + ctx.user_id);

    //followed
    cmds.emplace_back("lrange");
    keys.emplace_back(g_config.user_context.followed_redis_key + ctx.user_id);


    brpc::RedisResponse resp;
    bool ret = redis::get(cmds, keys, resp);
    if (!ret) {
        ALOG(ERROR, "fetch user context failed");
        return;
    }

    try {
        ctx.blacklist_set = redis::parse<std::unordered_set<std::string>>(resp.reply(0));
        ALOG(INFO, "get user black list success, length: %lu", ctx.blacklist_set.size());
    } catch (const std::exception& e) {
        ALOG(ERROR, "get user black list failed: %s", e.what());
    }

    try {
        std::vector<std::string> events = redis::parse<std::vector<std::string>>(resp.reply(1));
        ALOG(INFO, "get user history success, length: %lu", events.size());
        
        for (const std::string& ev : events) {
            try {
                json j = json::parse(ev);
                auto& item = j["item_id"];
                ctx.show_set.insert(item);
                ctx.show_list.emplace_back(item);
            
                if (j["clicked"]) {
                    ctx.click_list.emplace_back(item);
                }
            } catch (const std::exception& ex) {
                ALOG(WARNING, "parse events %s failed: %s", ev.c_str(), ex.what());
                continue;
            }
        }
    } catch (const std::exception& e) {
        ALOG(ERROR, "get user history failed: %s", e.what());
    }

    try {
        ctx.followed_list = redis::parse<std::vector<std::string>>(resp.reply(2));
        ALOG(INFO, "get user followed success, length: %lu", ctx.followed_list.size());
    } catch (const std::exception& e) {
        ALOG(ERROR, "get user followed failed: %s", e.what());
    }


    for (auto& item : ctx.click_list) {
        const auto it_iter = ctx.item_pool->items.find(item);
        if (it_iter == ctx.item_pool->items.end()) {
            continue;
        }

        const auto& it = it_iter->second;

        for (const CateId& c : it.cates) {
            if (ctx.recent_cates_freq.find(c) == ctx.recent_cates_freq.end()) {
                ctx.recent_cates_freq[c] = 0;
                ctx.recent_cates_vec.emplace_back(c);
            }
            ctx.recent_cates_freq[c]++;
        }

        for (const UserId& u : it.authors) {
            if (ctx.recent_authors_freq.find(u) == ctx.recent_authors_freq.end()) {
                ctx.recent_authors_freq[u] = 0;
                ctx.recent_authors_vec.emplace_back(u);
            }
            ctx.recent_authors_freq[u]++;
        }
    }

}    

} // namespace fetcher

} // namespace predictionmarkets_rec
