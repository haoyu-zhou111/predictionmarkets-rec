#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "data/item_pool.h"
#include "data/item_feature.h"
#include "data/user_feature.h"
#include "data/exp.h"

namespace predictionmarkets_rec {

struct Context {
    UserId                                                      user_id;                // 上游 Ghost member.id（非 uuid）；与 author_id 非同一 id 空间
    DeviceId                                                    device_id;
    AnchorId                                                    anchor_id;              // 有 user_id 用 user_id，否则 device_id
    uint32_t                                                    group_id            = 0;
    uint64_t                                                    timestamp           = 0;
    std::string                                                 request_id;
    std::string                                                 session_id;
    uint32_t                                                    session_refresh_num = 0;
    uint32_t                                                    topk                = 10;
    bool                                                        has_more            = true;


    std::shared_ptr<const ItemPool>                             item_pool;   // 按付费状态选中的池（免费/全量）
    std::shared_ptr<const ItemFeatureData>                      item_feature;
    std::shared_ptr<const ExpConfigData>                        exp_config;


    std::unordered_set<ItemId>                                  blacklist_set;
    std::unordered_set<ItemId>                                  show_set;
    std::unordered_set<ItemId>                                  rec_exposed_set;        // 曾推给该 anchor 的 item（软降权，不做过滤）
    std::vector<ItemId>                                         rec_history_list;       // 推荐历史（新→旧），跨刷打散种子
    std::vector<ItemId>                                         show_list;
    std::vector<ItemId>                                         click_list;
    std::vector<UserId>                                         followed_list;

    FeatureMap                                                  context_feature_map;

    std::unordered_map<CateId, int>                             recent_cates_freq;
    std::vector<CateId>                                         recent_cates_vec;
    std::unordered_map<UserId, int>                             recent_authors_freq;
    std::vector<UserId>                                         recent_authors_vec;

    std::vector<UserId>                                         follow_u2i_triggers;
    std::vector<UserId>                                         recent_u2i_triggers;
    U2IIndex                                                    follow_u2i_items_map;
    U2IIndex                                                    recent_u2i_items_map;

    std::vector<ItemId>                                         candidates;
    std::unordered_map<ItemId, uint32_t>                        channels;
    std::vector<FeatureMap>                                     behavior_feature_vec;
    std::vector<std::vector<uint32_t>>                          feature_idx;
    std::vector<double>                                         rank_scores;

    std::vector<std::pair<ItemId, uint32_t>>                    final_results;
};

} // namespace predictionmarkets_rec
