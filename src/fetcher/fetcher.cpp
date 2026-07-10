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
        std::this_thread::sleep_for(std::chrono::milliseconds(g_config.sync.item_pool_interval_ms));
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

// i2i 索引本阶段无数据、召回不用，暂停加载（保留待恢复 icf/i2v/swing 召回时启用）
// bool recall_index_init() {
//     if (!i2i_index_init(g_config.data_path.icf_index,   g_i2i_index_map["icf"]))   return false;
//     if (!i2i_index_init(g_config.data_path.i2v_index,   g_i2i_index_map["i2v"]))   return false;
//     if (!i2i_index_init(g_config.data_path.swing_index, g_i2i_index_map["swing"])) return false;
//     return true;
// }

} // namespace

bool init() {
    try {
        if (!item_pool_init())        return false;
        // 本阶段召回只用全量（full），i2i 无索引；精排走 bandit 跳过特征+LR。
        // 故以下加载暂停（恢复对应链路时一并恢复）：
        // if (!item_feature_init())     return false;   // item 特征（本阶段停载）
        // if (!user_feature_init())     return false;   // 特征拼接用
        // if (!feature_manifest_init()) return false;   // 特征拼接用
        // if (!recall_index_init())     return false;   // icf/i2v/swing i2i 索引
        // if (!lr_model_init())         return false;   // LR 精排用
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
    [[maybe_unused]] const auto& uc = g_config.user_context;
    std::vector<std::vector<std::string>> commands = {
        // [暂不启用] 黑名单/曝光历史/关注；恢复时放开这三条并把下方各 reply 索引顺延
        // {"smembers", uc.blacklist_redis_key + ctx.anchor_id},                      // black list
        // {"lrange",   uc.history_redis_key  + ctx.anchor_id,      "0", "-1"},       // history
        // {"lrange",   uc.followed_redis_key + ctx.anchor_id,      "0", "-1"},       // followed
        {"lrange",   g_config.rec_history.key_prefix + ctx.anchor_id, "0", "-1"},  // rec history（软降权，不过滤）
    };

    brpc::RedisResponse resp;
    if (!redis::exec(commands, resp)) {
        ALOG(ERROR, "fetch user context failed");
        return;
    }

    // [暂不启用] 黑名单/曝光历史/关注解析；与上方三条命令一同放开，放开时 reply 索引顺延
    // try {
    //     ctx.blacklist_set = redis::parse<std::unordered_set<std::string>>(resp.reply(0));
    //     ALOG(INFO, "get user black list success, length: %lu", ctx.blacklist_set.size());
    // } catch (const std::exception& e) {
    //     ALOG(ERROR, "get user black list failed: %s", e.what());
    // }
    //
    // try {
    //     std::vector<std::string> events = redis::parse<std::vector<std::string>>(resp.reply(1));
    //     ALOG(INFO, "get user history success, length: %lu", events.size());
    //
    //     for (const std::string& ev : events) {
    //         try {
    //             json j = json::parse(ev);
    //             auto& item = j["item_id"];
    //             ctx.show_set.insert(item);
    //             ctx.show_list.emplace_back(item);
    //
    //             if (j["clicked"]) {
    //                 ctx.click_list.emplace_back(item);
    //             }
    //         } catch (const std::exception& ex) {
    //             ALOG(WARNING, "parse events %s failed: %s", ev.c_str(), ex.what());
    //             continue;
    //         }
    //     }
    // } catch (const std::exception& e) {
    //     ALOG(ERROR, "get user history failed: %s", e.what());
    // }
    //
    // try {
    //     ctx.followed_list = redis::parse<std::vector<std::string>>(resp.reply(2));
    //     ALOG(INFO, "get user followed success, length: %lu", ctx.followed_list.size());
    // } catch (const std::exception& e) {
    //     ALOG(ERROR, "get user followed failed: %s", e.what());
    // }

    // 历史推荐读取（当前唯一命令，reply(0)）：全量入 rec_exposed_set 做曝光软降权，最近 topk 作跨刷打散种子
    try {
        std::vector<std::string> hist = redis::parse<std::vector<std::string>>(resp.reply(0));

        std::string hist_dump;
        for (const auto& h : hist) {
            hist_dump += h;
            hist_dump += ' ';
        }
        ALOG(DEBUG, "rec history raw: count=%zu items=[%s]", hist.size(), hist_dump.c_str());

        ctx.rec_exposed_set.insert(hist.begin(), hist.end());   // 全量：曝光软降权用
        hist.resize(std::min(static_cast<size_t>(ctx.topk), hist.size()));
        ctx.rec_history_list = std::move(hist);                 // 最近 topk（新→旧）：跨刷打散种子
        ALOG(DEBUG, "get rec history success, exposed=%lu seed=%lu",
             ctx.rec_exposed_set.size(), ctx.rec_history_list.size());
    } catch (const std::exception& e) {
        ALOG(ERROR, "get rec history failed: %s", e.what());
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
