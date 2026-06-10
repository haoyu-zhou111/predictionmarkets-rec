#include <thread>
#include <nlohmann/json.hpp>

#include "../data/exp.h"
#include "../data/item_feature.h"
#include "../data/item_pool.h"
#include "fetcher.h"
#include "common/config.h"
#include "common/log.h"
#include "common/redis.h"

namespace ratus_rec {

namespace fetcher {

namespace {

void run_item_pool() {
    while (true) {
        item_pool_load();
        std::this_thread::sleep_for(std::chrono::milliseconds(g_config.sync.available_item_interval_ms));
    }
}

void run_exp_config() {
    while (true) {
        exp_config_reload();
        std::this_thread::sleep_for(std::chrono::milliseconds(g_config.sync.exp_config_interval_ms));
    }
}

} // namespace

bool init() {
    try {
        item_pool_init();
        std::thread(run_item_pool).detach();

        exp_config_init();
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
    } catch (const std::exception &e) {
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
            
                if (j["effective_sec"] >= 120) {
                    ctx.view_list.emplace_back(item);
                }
            } catch (const std::exception &ex) {
                ALOG(WARNING, "parse events %s failed: %s", ev.c_str(), ex.what());
                continue;
            }
        }
    } catch (const std::exception &e) {
        ALOG(ERROR, "get user history failed: %s", e.what());
    }

    try {
        ctx.followed_list = redis::parse<std::vector<std::string>>(resp.reply(2));
        ALOG(INFO, "get user followed success, length: %lu", ctx.followed_list.size());
    } catch (const std::exception &e) {
        ALOG(ERROR, "get user followed failed: %s", e.what());
    }


    for (auto& item : ctx.view_list) {
        const auto f_iter = ctx.item_feature->features.find(item);
        if (f_iter == std::end(ctx.item_feature->features)) {
            continue;
        }

        auto& features = f_iter->second;
        
        const auto cate_iter = features.find("cate");
        if (cate_iter != features.end()) {
            CateId c = static_cast<CateId>(cate_iter->second);
            if (ctx.recent_cates_freq.find(c) == ctx.recent_cates_freq.end()) {
                ctx.recent_cates_freq[c] = 0;
                ctx.recent_cates_vec.emplace_back(c);
            }
            ctx.recent_cates_freq[c]++;
        }

        const auto author_iter = features.find("author");
        if (author_iter != features.end()) {
            UserId u = static_cast<UserId>(author_iter->second);
            if (ctx.recent_authors_freq.find(u) == ctx.recent_authors_freq.end()) {
                ctx.recent_authors_freq[u] = 0;
                ctx.recent_authors_vec.emplace_back(u);
            }
            ctx.recent_authors_freq[u]++;
        }
    }

}    

} // namespace fetcher

} // namespace ratus_rec
