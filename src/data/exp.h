#pragma once

#include <memory>
#include "common/type_define.h"

namespace predictionmarkets_rec {

struct RecallChannelConfig {
    bool        enable          = false;
    uint32_t    trigger_count   = 0;
    uint32_t    single_topk     = 0;
    uint32_t    topk            = 0;
};

struct RecallConfig {
    std::unordered_map<std::string, RecallChannelConfig> channel_config;
};

struct RankConfig {
    bool    bandit_enable   = true;
};

struct RerankConfig {
    bool     enable         = false;
    uint32_t newest_top_k   = 0;        // 首刷前 N 位强制按发布时间降序出候选中最新内容（0=不启用）
    int      author_gap     = 0;
    int      cate_gap       = 0;
    uint32_t fresh_top_k    = 0;        // 首屏前 N 位要求 ≤ fresh_days 天（0=不启用）
    int      fresh_days     = 7;
    int      recent_days    = 14;       // 前 recent_screens 屏全部要求 ≤ recent_days 天
    int      recent_screens = 3;
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

uint32_t get_user_group(AnchorId anchor_id);

} // namespace predictionmarkets_rec
