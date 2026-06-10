#pragma once

#include <vector>
#include <memory>
#include "../data/item_pool.h"
#include "../data/item_feature.h"
#include "../data/user_feature.h"
#include "../data/exp.h"
#include "rec.pb.h"

namespace ratus_rec {

struct Context {
    std::string                                                 user_id;
    uint32_t                                                    group_id;
    uint64_t                                                    timestamp;
    std::string                                                 request_id;
    std::string                                                 session_id;
    uint32_t                                                    session_refresh_num;
    uint32_t                                                    topk;
    std::vector<LastRefreshItem>                                last_refresh_items;
    bool                                                        has_more;   


    std::shared_ptr<const ItemPoolData>                         item_pool;
    std::shared_ptr<const ItemFeatureData>                      item_feature;
    std::shared_ptr<const ExpConfigData>                        exp_config;


    std::unordered_set<ItemId>                                  blacklist_set;
    std::unordered_set<ItemId>                                  show_set;
    std::vector<ItemId>                                         show_list;
    std::vector<ItemId>                                         view_list;
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

    void reset() {
        user_id                 = "";
        timestamp               = 0;
        request_id              = "";
        session_id              = "";
        session_refresh_num     = 0;
        topk                    = 10;
        has_more                = true;

        item_pool               = nullptr;
        item_feature            = nullptr;
        exp_config              = nullptr;

        last_refresh_items.clear();
        blacklist_set.clear();
        show_set.clear();
        show_list.clear();
        view_list.clear();
        followed_list.clear();

        context_feature_map.clear();
        
        recent_cates_freq.clear();
        recent_cates_vec.clear();
        recent_authors_freq.clear();
        recent_authors_vec.clear();

        follow_u2i_triggers.clear();
        recent_u2i_triggers.clear();
        follow_u2i_items_map.clear();
        recent_u2i_items_map.clear();

        candidates.clear();
        channels.clear();
        behavior_feature_vec.clear();
        feature_idx.clear();
        rank_scores.clear();

        final_results.clear();
    }


};

} // namespace ratus_rec
