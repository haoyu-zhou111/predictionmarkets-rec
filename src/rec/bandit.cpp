#include "bandit.h"
#include "common/utils.h"

namespace predictionmarkets_rec {

namespace rec {

// 汤普森采样：对每个候选在 Beta(1+k, 1+n-k) 后验上采样，
// 采样值即预估点击率，作为排序分写入 ctx.rank_scores
void bandit_rank(Context& ctx) {
    ctx.rank_scores.resize(ctx.candidates.size(), 0.0);
    const auto& stats = ctx.item_pool->item_stats;

    for (size_t i = 0; i < ctx.candidates.size(); i++) {
        uint32_t n = 0;
        uint32_t k = 0;
        auto stat_iter = stats.find(ctx.candidates[i]);
        if (stat_iter != stats.end()) {
            n = stat_iter->second.show;
            k = stat_iter->second.click;
        }

        uint32_t miss = n >= k ? n - k : 0;
        ctx.rank_scores[i] = beta_sample(1.0 + static_cast<double>(k), 1.0 + static_cast<double>(miss));
    }
}

} // namespace rec

} // namespace predictionmarkets_rec
