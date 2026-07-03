#pragma once

#include <vector>
#include <memory>
#include "data/item_pool.h"
#include "data/item_feature.h"
#include "data/user_feature.h"
#include "data/exp.h"
#include "rec.pb.h"

namespace predictionmarkets_rec {

struct Context {
    std::string                                                 user_id;
    uint32_t                                                    group_id            = 0;
    uint64_t                                                    timestamp           = 0;
    std::string                                                 request_id;
    std::string                                                 session_id;
    uint32_t                                                    session_refresh_num = 0;
    uint32_t                                                    topk                = 10;
    std::vector<LastRefreshItem>                                last_refresh_items;
    bool                                                        has_more            = true;


    std::shared_ptr<const ItemPool>                             item_pool;   // 按付费状态选中的池（免费/全量）
    std::shared_ptr<const ItemFeatureData>                      item_feature;
    std::shared_ptr<const ExpConfigData>                        exp_config;


    std::unordered_set<ItemId>                                  blacklist_set;
    std::unordered_set<ItemId>                                  show_set;
    std::vector<ItemId>                                         show_list;
    std::vector<ItemId>                                         click_list;
    std::vector<UserId>                                         followed_list;

    std::unordered_map<std::string, std::string>                context_feature_map;

    std::unordered_map<CateId, int>                             recent_cates_freq;
    std::vector<CateId>                                         recent_cates_vec;
    std::unordered_map<UserId, int>                             recent_authors_freq;
    std::vector<UserId>                                         recent_authors_vec;

    std::vector<UserId>                                         follow_u2i_triggers;
    std::vector<UserId>                                         recent_u2i_triggers;
    std::unordered_map<UserId, std::vector<ItemId>>             follow_u2i_items_map;
    std::unordered_map<UserId, std::vector<ItemId>>             recent_u2i_items_map;

    std::vector<ItemId>                                         candidates;
    std::unordered_map<ItemId, uint32_t>                        channels;
    std::vector<std::unordered_map<std::string, std::string>>   behavior_feature_vec;
    std::vector<std::vector<uint32_t>>                          feature_idx;
    std::vector<double>                                         rank_scores;

    std::vector<std::pair<ItemId, uint32_t>>                    final_results;
};

} // namespace predictionmarkets_rec
