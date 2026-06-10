#pragma once

#include <memory>
#include "common/type_define.h"

namespace ratus_rec {

struct RecallChannelConfig {
    bool    enable          = false;
    int     trigger_count   = 0;
    int     single_topk     = 0;
    int     topk            = 0;
};

struct RecallConfig {
    std::unordered_map<std::string, RecallChannelConfig> channel_config;
};

struct RankConfig {
};

struct RerankConfig {
    bool    enable      = false;
    int     author_gap  = 0;
    int     cate_gap    = 0;
};

struct OperationConfig {
    bool                        enable = false;
    std::vector<int>            positions;
    std::vector<std::string>    items;
};

struct ExpConfig {
    RecallConfig    recall;
    RankConfig      rank;
    RerankConfig    rerank;
    OperationConfig op;
};

struct ExpConfigData {
    std::vector<ExpConfig>  groups{10};
};

extern std::shared_ptr<const ExpConfigData> g_exp_merged_config;

bool exp_config_init();

void exp_config_reload();

uint32_t get_user_group(UserId user_id);

} // namespace ratus_rec
