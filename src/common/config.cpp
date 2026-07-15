#include <fstream>

#include "config.h"

using json = nlohmann::json;

namespace predictionmarkets_rec {

Config g_config;

bool config_load(const std::string& conf_path) {
    std::ifstream ifs(conf_path);
    if (!ifs.is_open()) {
        ALOG(ERROR, "open config failed: %s", conf_path.c_str());
        return false;
    }

    json j;
    try {
        ifs >> j;

        auto server                                 = get_json_obj(j, "server");
        auto redis                                  = get_json_obj(j, "redis");
        auto exp                                    = get_json_obj(j, "exp");
        auto data_path                              = get_json_obj(j, "data_path");
        auto model_path                             = get_json_obj(j, "model_path");
        auto log                                    = get_json_obj(j, "log");
        auto sync                                   = get_json_obj(j, "sync");
        auto user_context                           = get_json_obj(j, "user_context");
        auto rec_history                            = get_json_obj(j, "rec_history");
        auto ghost                                  = get_json_obj(sync, "ghost");
        auto bandit                                 = get_json_obj(sync, "bandit");

        g_config.server.port                        = get_json(server, "port", 8080);

        g_config.redis.nodes                        = get_json(redis, "nodes", std::vector<std::string>{});
        g_config.redis.password                     = get_json(redis, "password", std::string{""});
        g_config.redis.timeout_ms                   = get_json(redis, "timeout_ms", 50);
        g_config.redis.connect_timeout_ms           = get_json(redis, "connect_timeout_ms", 200);
        g_config.redis.max_retry                    = get_json(redis, "max_retry", 2);
        g_config.redis.max_redirect                 = get_json(redis, "max_redirect", 5);
        g_config.redis.batch_window                 = get_json(redis, "batch_window", 64);

        g_config.exp.local_path                     = get_json(exp, "local_path", std::string{"./conf/exp.conf"});
        g_config.exp.redis_key                      = get_json(exp, "redis_key", std::string{"exp_config"});

        g_config.data_path.item_feature             = get_json(data_path, "item_feature", std::string{"./data/feature/item_feature"});
        g_config.data_path.user_feature             = get_json(data_path, "user_feature", std::string{"./data/feature/user_feature"});
        g_config.data_path.feature_manifest         = get_json(data_path, "feature_manifest", std::string{"./data/feature/feature_manifest"});
        g_config.data_path.icf_index                = get_json(data_path, "icf_index", std::string{"./data/recall/icf_index"});
        g_config.data_path.i2v_index                = get_json(data_path, "i2v_index", std::string{"./data/recall/i2v_index"});
        g_config.data_path.swing_index              = get_json(data_path, "swing_index", std::string{"./data/recall/swing_index"});

        g_config.model_path.ranking                 = get_json(model_path, "ranking", std::string{"./data/model/lr_model"});

        g_config.log.dir                            = get_json(log, "dir", std::string{"./log"});
        g_config.log.level                          = get_json(log, "level", 0);
        g_config.log.retention_days                 = get_json(log, "retention_days", 7);

        g_config.sync.item_pool_interval_ms         = get_json(sync, "item_pool_interval_ms", 1000);
        g_config.sync.item_feature_interval_ms      = get_json(sync, "item_feature_interval_ms", 300000);
        g_config.sync.exp_config_interval_ms        = get_json(sync, "exp_config_interval_ms", 60000);

        g_config.user_context.blacklist_redis_key   = get_json(user_context, "blacklist_redis_key", std::string{"user_black:"});
        g_config.user_context.history_redis_key     = get_json(user_context, "history_redis_key", std::string{"user_history:"});
        g_config.user_context.followed_redis_key    = get_json(user_context, "followed_redis_key", std::string{"user_followed:"});
        g_config.user_context.published_redis_key   = get_json(user_context, "published_redis_key", std::string{"user_published:"});

        g_config.rec_history.key_prefix             = get_json(rec_history, "key_prefix", std::string{"rec_hist:"});
        g_config.rec_history.ttl_sec                = get_json(rec_history, "ttl_sec", 259200);
        g_config.rec_history.max_len                = get_json(rec_history, "max_len", 200);

        g_config.sync.ghost.admin_api_url           = get_json(ghost, "admin_api_url", std::string{""});
        g_config.sync.ghost.admin_key               = get_json(ghost, "admin_key", std::string{""});
        g_config.sync.ghost.page_limit              = get_json(ghost, "page_limit", 100);
        g_config.sync.ghost.jwt_ttl_sec             = get_json(ghost, "jwt_ttl_sec", 300);
        g_config.sync.ghost.timeout_ms              = get_json(ghost, "timeout_ms", 3000);

        g_config.sync.bandit.post_stat_key_prefix   = get_json(bandit, "post_stat_key_prefix", std::string{"tracking:rec_sys:post:"});
        g_config.sync.bandit.impression_field       = get_json(bandit, "impression_field", std::string{"impression_total"});
        g_config.sync.bandit.click_field            = get_json(bandit, "click_field", std::string{"click_total"});

    } catch (...) {
        ALOG(ERROR, "parse config json failed: %s", conf_path.c_str());
        return false;
    }

    ALOG(INFO, "load config success: %s", conf_path.c_str());
    return true;
}

} // namespace predictionmarkets_rec
