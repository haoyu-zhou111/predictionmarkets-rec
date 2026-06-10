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

struct SyncConf {
    int         available_item_interval_ms = 1000;
    std::string available_item_api;
    int         available_item_timeout_ms  = 300;
    int         item_feature_interval_ms   = 300000;
    int         exp_config_interval_ms     = 60000;
};

struct UserContextConf {
    std::string blacklist_redis_key     = "user_black:";
    std::string history_redis_key       = "user_history:";
    std::string followed_redis_key      = "user_followed:";
    std::string published_redis_key     = "user_published:";
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
