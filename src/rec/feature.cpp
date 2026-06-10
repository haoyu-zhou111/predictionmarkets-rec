#include "feature.h"
#include "context.h"
#include "recall.h"
#include "../data/model.h"
#include "../data/feature_manifest.h"
#include "../common/utils.h"

namespace ratus_rec {

namespace rec {

namespace {

void generate_single(
    const std::vector<std::string>& feature_list,
    const std::unordered_map<std::string, std::string>& feature_map,
    std::vector<uint32_t>& idx_vec
) {
    
    for (auto& feature_name : feature_list) {
        auto feature_iter = feature_map.find(feature_name);
        if (feature_iter != feature_map.end()) {
            
            std::string key = feature_name + ";";
            key += feature_iter->second;

            idx_vec.emplace_back(feature_hash(key, g_model.hash_size));
        }
    }
}

void generate_pair(
    const std::vector<std::pair<std::string, std::string>>& feature_pair,
    const std::unordered_map<std::string, std::string>& first, 
    const std::unordered_map<std::string, std::string>& second,
    std::vector<uint32_t>& idx_vec
) {

    for (auto& [first_name, second_name] : feature_pair) {
        auto first_iter = first.find(first_name);
        if (first_iter == first.end()) {
            continue;
        }

        auto second_iter = second.find(second_name);
        if (second_iter == second.end()) {
            continue;
        }

        std::string key = first_name + "_";
        key += second_name;
        key += ";";
        key += first_iter->second;
        key += "_";
        key += second_iter->second;
    
        idx_vec.emplace_back(feature_hash(key, g_model.hash_size));
    }
}

} // namespace

void feature_combine(Context& ctx) {
    ctx.feature_idx.resize(ctx.candidates.size());
    ctx.behavior_feature_vec.resize(ctx.candidates.size());
    for (size_t i = 0; i < ctx.candidates.size(); ++i) {
        auto& idx_vec = ctx.feature_idx[i];
        
        auto user_feature_iter = g_user_feature.find(ctx.user_id);
        if (user_feature_iter != g_user_feature.end()) {
            generate_single(g_feature_manifest.user_fea, user_feature_iter->second, idx_vec);
        }

        auto item_feature_iter = ctx.item_feature->features.find(ctx.candidates[i]);
        if (item_feature_iter != ctx.item_feature->features.end()) {
            generate_single(g_feature_manifest.item_fea, item_feature_iter->second, idx_vec);

            generate_pair(g_feature_manifest.ii_cross_fea, item_feature_iter->second, item_feature_iter->second, idx_vec);

            if (user_feature_iter != g_user_feature.end()) {
                generate_pair(g_feature_manifest.ui_cross_fea, user_feature_iter->second, item_feature_iter->second, idx_vec);
            }
        }

        generate_single(g_feature_manifest.context_fea, ctx.context_feature_map, idx_vec);

        uint32_t channel_binary = ctx.channels[ctx.candidates[i]];
        for (uint32_t c = 0; c <= MAX_CHANNEL_TYPE; c++) {
            std::string key = "channel_" + std::to_string(c);
            if (channel_binary & (1 << c)) {
                ctx.behavior_feature_vec[i][key] = "1";
            } else {
                ctx.behavior_feature_vec[i][key] = "0";
            }
        }


        /*
        last watch time
        last watch author time
        last watch cate time

        recent_cates top3
        recent_authors top3

        recent_cates X item_cate
        recent_authors X item_author
        item_cate in recent_cates freq
        item_author in recent_authors freq
       
        */
        generate_single(g_feature_manifest.behavior_fea, ctx.behavior_feature_vec[i], idx_vec);
        
    }
}

} // namespace rec

} // namespace ratus_rec
