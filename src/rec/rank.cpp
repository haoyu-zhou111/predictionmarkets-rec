#include "rank.h"
#include "data/model.h"

namespace predictionmarkets_rec {

namespace rec {

void rank(Context& ctx) {
    ctx.rank_scores.resize(ctx.candidates.size(), 0.0);
    for (size_t i = 0; i < ctx.candidates.size(); i++) {
        double s = g_model.bias;
        
        for (auto& idx : ctx.feature_idx[i]) {
            s += g_model.weight[idx];
        }

        ctx.rank_scores[i] = 1.0f / (1.0f + std::exp(-s));
    }
}

} // namespace rec

} // namespace predictionmarkets_rec
