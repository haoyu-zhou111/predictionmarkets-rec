#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

#include "rerank.h"
#include "common/log.h"

namespace predictionmarkets_rec {

namespace rec {

// 规则按优先级从弱到强声明（靠后=更强），序号即声明顺序（类似 Go iota）。
// 真实惩罚权重由 penalty_bit 外部位移得到；新增规则插到对应位置即可，无需手改权重。
// 逐坑位选 penalty 最小、同 penalty 分数最高者；penalty=0 即完全符合，最先出。
enum RerankRule : uint32_t {
    RULE_SCATTER = 0,   // 违反打散（最弱）
    RULE_RECENCY,       // 违反时效
    RULE_EXPOSED,       // 已曝光
    RULE_OP,            // 运营指定坑位却非 op 内容（最强）
};

constexpr uint32_t penalty_bit(RerankRule rule) {
    return 1u << rule;
}

// 把 penalty 位掩码还原成命中的规则名（弱→强），无命中返回 "-"，仅供日志诊断
std::string penalty_rules(uint32_t pen) {
    if (pen == 0) {
        return "-";
    }
    static const char* const names[] = {"scatter", "recency", "exposed", "op"};
    std::string s;
    for (uint32_t r = RULE_SCATTER; r <= RULE_OP; r++) {
        if (pen & penalty_bit(static_cast<RerankRule>(r))) {
            if (!s.empty()) {
                s += "|";
            }
            s += names[r];
        }
    }
    return s;
}

void rerank(Context& ctx) {
    auto& op_config     = ctx.exp_config->groups[ctx.group_id].op;
    auto& rerank_config = ctx.exp_config->groups[ctx.group_id].rerank;

    std::unordered_map<ItemId, std::string> author_map;
    std::unordered_map<ItemId, std::string> cate_map;
    std::unordered_map<ItemId, uint64_t>    created_map;
    std::unordered_set<std::string>         op_items;

    if (op_config.enable) {
        for (auto& item : op_config.items) {
            op_items.insert(item);
        }
    }

    // 预取每个候选的主 tag / 主 author / 发布时间（打散与时效用）
    for (auto& item : ctx.candidates) {
        const auto it_iter = ctx.item_pool->items.find(item);
        if (it_iter == ctx.item_pool->items.end()) {
            continue;
        }
        const auto& it = it_iter->second;
        if (!it.cates.empty()) {
            cate_map[item] = it.cates.front();
        }
        if (!it.authors.empty()) {
            author_map[item] = it.authors.front();
        }
        created_map[item] = it.created_at;
    }

    size_t n = std::min(static_cast<size_t>(ctx.topk), ctx.candidates.size());

    // 运营指定坑位集合（超出返回条数的忽略）
    std::unordered_set<size_t> op_positions;
    if (op_config.enable) {
        for (size_t p : op_config.positions) {
            if (p < n) {
                op_positions.insert(p);
            }
        }
    }

    std::unordered_set<int> used;
    std::unordered_map<std::string, int> author_last_position;
    std::unordered_map<std::string, int> cate_last_position;

    // 跨刷打散：非首页时，用最近 topk 条推荐历史作种子，置于位置 -1..-k（最新的在 -1）
    if (rerank_config.enable && ctx.session_refresh_num > 0) {
        for (size_t r = 0; r < ctx.rec_history_list.size(); r++) {
            const auto it_iter = ctx.item_pool->items.find(ctx.rec_history_list[r]);
            if (it_iter == ctx.item_pool->items.end()) {
                continue;
            }
            const auto& it = it_iter->second;
            int pos = -static_cast<int>(r + 1);
            if (rerank_config.author_gap > 0 && !it.authors.empty()) {
                author_last_position.emplace(it.authors.front(), pos);   // emplace 不覆盖：更近者保留
            }
            if (rerank_config.cate_gap > 0 && !it.cates.empty()) {
                cate_last_position.emplace(it.cates.front(), pos);
            }
        }
    }

    std::vector<uint32_t> order(ctx.candidates.size());
    for (uint32_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](const uint32_t& a, const uint32_t& b) {
        return ctx.rank_scores[a] > ctx.rank_scores[b];
    });

    const uint32_t refresh = ctx.session_refresh_num;
    const uint64_t now     = ctx.timestamp;

    // 候选 j 放在位置 pos 的软降权：运营坑位 > 曝光 > 时效 > 打散，越小越优先
    auto penalty_of = [&](uint32_t j, size_t pos) -> uint32_t {
        const std::string& item = ctx.candidates[j];
        uint32_t pen = 0;

        // 运营强插：指定坑位却非 op 内容 → 最强惩罚（op 内容无此惩罚，自然占位）
        if (op_config.enable && op_positions.count(pos) && !op_items.count(item)) {
            pen |= penalty_bit(RULE_OP);
        }

        if (ctx.rec_exposed_set.count(item)) {
            pen |= penalty_bit(RULE_EXPOSED);
        }

        if (rerank_config.enable) {
            // 时效：首屏前 fresh_top_k 位要 ≤fresh_days；前 recent_screens 屏全部 ≤recent_days
            auto ct = created_map.find(item);
            uint64_t created_at = ct != created_map.end() ? ct->second : 0;
            uint64_t age_day = now > created_at ? (now - created_at) / 86400 : 0;
            bool rec_bad = false;
            if (refresh < static_cast<uint32_t>(rerank_config.recent_screens) &&
                age_day > static_cast<uint64_t>(rerank_config.recent_days)) {
                rec_bad = true;
            }
            if (refresh == 0 && pos < rerank_config.fresh_top_k &&
                age_day > static_cast<uint64_t>(rerank_config.fresh_days)) {
                rec_bad = true;
            }
            if (rec_bad) {
                pen |= penalty_bit(RULE_RECENCY);
            }

            // 打散：与已放置（含跨刷种子）的同作者/同类目间隔不足则违反
            bool scat_bad = false;
            if (rerank_config.author_gap > 0) {
                auto a = author_map.find(item);
                if (a != author_map.end()) {
                    auto lp = author_last_position.find(a->second);
                    if (lp != author_last_position.end() &&
                        static_cast<int>(pos) - lp->second < rerank_config.author_gap) {
                        scat_bad = true;
                    }
                }
            }
            if (!scat_bad && rerank_config.cate_gap > 0) {
                auto c = cate_map.find(item);
                if (c != cate_map.end()) {
                    auto lp = cate_last_position.find(c->second);
                    if (lp != cate_last_position.end() &&
                        static_cast<int>(pos) - lp->second < rerank_config.cate_gap) {
                        scat_bad = true;
                    }
                }
            }
            if (scat_bad) {
                pen |= penalty_bit(RULE_SCATTER);
            }
        }

        return pen;
    };

    for (size_t i = 0; i < n; i++) {
        // 选 penalty 最小、同 penalty 分数最高（order 已按分数降序）的未用候选
        int chosen = -1;
        uint32_t best_pen = std::numeric_limits<uint32_t>::max();
        for (auto j : order) {
            if (used.count(j)) {
                continue;
            }
            uint32_t pen = penalty_of(j, i);
            if (pen < best_pen) {
                best_pen = pen;
                chosen = j;
                if (pen == 0) {
                    break;   // 已最优，提前结束
                }
            }
        }

        if (chosen == -1) {
            ALOG(ERROR, "can't reach here, something wrong in rerank part");
            break;
        }

        ItemId& chosen_item = ctx.candidates[chosen];
        ALOG(INFO, "[rerank] pos=%zu item=%s score=%.4f penalty=0x%x rules=%s",
             i, chosen_item.c_str(), ctx.rank_scores[chosen], best_pen,
             penalty_rules(best_pen).c_str());
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
