#pragma once

#include <string>
#include "common/log.h"
#include "common/utils.h"

namespace predictionmarkets_rec {

struct ServerConf {
    int port       = 8080;
    int worker_num = 8;
};

struct RedisConf {
    std::string host               = "127.0.0.1";
    int         port               = 6379;
    std::string password;
    int         timeout_ms         = 5;
    int         connect_timeout_ms = 200;
    int         max_retry          = 2;
};

struct ExpConf {
    std::string local_path = "./conf/exp.conf";
    std::string redis_key  = "exp_config";
};

struct DataPathConf {
    std::string item_feature        = "./data/feature/item_feature";
    std::string user_feature        = "./data/feature/user_feature";
    std::string feature_manifest    = "./data/feature/feature_manifest";

    std::string icf_index           = "./data/recall/icf_index";
    std::string i2v_index           = "./data/recall/i2v_index";
    std::string swing_index         = "./data/recall/swing_index";
};

struct ModelPathConf {
    std::string ranking = "./data/model/lr_model";
};

struct LogConf {
    std::string dir   = "./log";
    int         level = 0;
};

struct UserContextConf {
    std::string blacklist_redis_key     = "user_black:";
    std::string history_redis_key       = "user_history:";
    std::string followed_redis_key      = "user_followed:";
    std::string published_redis_key     = "user_published:";
};

struct GhostConf {
    std::string admin_api_url;                          // 形如 https://host/ghost/api/admin
    std::string admin_key;                              // Admin API Key："id:secret"
    int         page_limit  = 100;                      // 分页每页条数
    int         jwt_ttl_sec = 300;                      // JWT 有效期（秒）
    int         timeout_ms  = 3000;                     // 单次请求超时
};

struct BanditConf {
    std::string post_stat_key_prefix = "tracking:rec_sys:post:"; // 每 item 一个 hash，key = prefix + item_id
    std::string impression_field     = "impression_total";       // hash field：总曝光数
    std::string click_field          = "click_total";            // hash field：总点击数
};

// 后台定时同步：三个 reload 间隔 + item 数据源（ghost/bandit）子配置
struct SyncConf {
    int         item_pool_interval_ms    = 1000;        // item_pool 轮询（拉 ghost + 回填 bandit n/k）
    int         item_feature_interval_ms = 300000;      // item_feature reload（本阶段停载，保留语义）
    int         exp_config_interval_ms   = 60000;       // 实验配置 reload
    GhostConf   ghost;
    BanditConf  bandit;
};

struct Config {
    ServerConf      server;
    RedisConf       redis;
    ExpConf         exp;
    DataPathConf    data_path;
    ModelPathConf   model_path;
    LogConf         log;
    SyncConf        sync;
    UserContextConf user_context;
};

extern Config g_config;

bool config_load(const std::string& path);

} // namespace predictionmarkets_rec
