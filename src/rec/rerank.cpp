#include <algorithm>

#include "rerank.h"
#include "common/log.h"

namespace predictionmarkets_rec {

namespace rec {

void rerank(Context& ctx) {
    auto& op_config = ctx.exp_config->groups[ctx.group_id].op;
    std::unordered_map<ItemId, std::string> author_map;
    std::unordered_map<ItemId, std::string> cate_map;
    std::unordered_set<std::string> op_items;
    std::vector<uint32_t> op_candidates;

    if (op_config.enable) {
        for (auto& item : op_config.items) {
            op_items.insert(item);
        }
    }

    for (size_t i = 0; i < ctx.candidates.size(); i++) {
        std::string& item = ctx.candidates[i];
        const auto it_iter = ctx.item_pool->items.find(item);
        if (it_iter != ctx.item_pool->items.end()) {
            const auto& it = it_iter->second;
            // 打散暂以主 tag / 主 author（列表首元素）为键，多 tag 打散待后续
            if (!it.cates.empty()) {
                cate_map[item] = it.cates.front();
            }
            if (!it.authors.empty()) {
                author_map[item] = it.authors.front();
            }
        }

        if (op_config.enable) {
            if (op_items.count(item)) {
                op_candidates.emplace_back(i);
            }
        }
    }

    size_t n = std::min(static_cast<size_t>(ctx.topk), ctx.candidates.size());
    std::unordered_set<uint32_t> op_positions;

    if (op_config.enable) {
        sort(op_candidates.begin(), op_candidates.end(), [&](const uint32_t& a, const uint32_t& b) {
            return ctx.rank_scores[a] > ctx.rank_scores[b];
        });

        for (size_t p : op_config.positions) {
            if (p >= 0 && p < n) {
                op_positions.insert(p);
            }
        
        }
    }

    std::unordered_set<int> used;
    std::unordered_map<std::string, int> author_last_position;
    std::unordered_map<std::string, int> cate_last_position;
    auto& rerank_config = ctx.exp_config->groups[ctx.group_id].rerank;

    std::vector<uint32_t> order(ctx.candidates.size());
    for (uint32_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](const uint32_t& a, const uint32_t& b) {
        return ctx.rank_scores[a] > ctx.rank_scores[b];
    });

    size_t op_idx = 0;
    for (size_t i = 0; i < n; i++) {
        int chosen = -1;

        if (op_config.enable && op_positions.count(i)) {
            while (op_idx < op_candidates.size() && used.count(op_candidates[op_idx])) {
                op_idx++;
            }
            if (op_idx < op_candidates.size()) {
                chosen = op_candidates[op_idx];
                op_idx++;
            }
        }

        if (chosen == -1) {
            int first = -1;
            for (auto j : order) {
                if (used.count(j)) {
                    continue;
                }
                if (first == -1) {
                    first = j;
                }

                std::string& item = ctx.candidates[j];
                if (rerank_config.enable) {
                    if (rerank_config.author_gap > 0) {
                        auto author_iter = author_map.find(item);
                        if (author_iter != author_map.end() &&
                            author_last_position.find(author_iter->second) != author_last_position.end() &&
                            static_cast<int>(i) - author_last_position[author_iter->second] < rerank_config.author_gap) {
                            continue;
                        }
                    }

                    if (rerank_config.cate_gap > 0) {
                        auto cate_iter = cate_map.find(item);
                        if (cate_iter != cate_map.end() &&
                            cate_last_position.find(cate_iter->second) != cate_last_position.end() &&
                            static_cast<int>(i) - cate_last_position[cate_iter->second] < rerank_config.cate_gap) {
                            continue;
                        }
                    }
                }

                chosen = j;
                break;
            }

            if (chosen == -1) {
                chosen = first;
            }
        }
    
        if (chosen == -1) {
            ALOG(ERROR, "can't reach here, something wrong in rerank part");
            break;
        }

        ItemId& chosen_item = ctx.candidates[chosen];
        if (rerank_config.enable) {
            if (rerank_config.author_gap > 0) {
                auto author_iter = author_map.find(chosen_item);
                if (author_iter != author_map.end()) {
                    author_last_position[author_iter->second] = i;
                }
            }
            if (rerank_config.cate_gap > 0) {
                auto cate_iter = cate_map.find(chosen_item);
                if (cate_iter != cate_map.end()) {
                    cate_last_position[cate_iter->second] = i;
                }
            }
        }            
        ctx.final_results.emplace_back(chosen_item, chosen);
        used.insert(chosen);
    }
}

} // namespace rec

} // namespace predictionmarkets_rec

