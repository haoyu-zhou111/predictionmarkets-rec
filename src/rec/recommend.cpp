#include <algorithm>

#include "recommend.h"
#include "feature.h"
#include "rank.h"
#include "recall.h"
#include "rerank.h"

namespace ratus_rec {
namespace rec {

using json = nlohmann::json;

namespace {
json fill_response(Context& ctx) {
    std::vector<json> feed_array;
    feed_array.reserve(ctx.final_results.size());
    for (const auto& p : ctx.final_results) {
        feed_array.push_back({
            {"item_id", p.first},
            {"score", ctx.rank_scores[p.second]}
        });
    }

    return json{
        {"code", 200},
        {"message", "success"},
        {"request_id", ctx.request_id},
        {"timestamp", ctx.timestamp},
        {"data", {
            {"current_refresh_num", ctx.session_refresh_num},
            {"feed_list", feed_array},
            {"ext_info", {
                {"has_more", ctx.has_more},
                {"strategy", "base"}
            }}
        }}
    };
}

} // namespace

json recommend(Context& ctx) {
    if (!ctx.item_pool) {
        return json{
            {"code", 500},
            {"message", "item pool not ready"},
            {"request_id", ctx.request_id},
            {"timestamp", ctx.timestamp}
        };
    }

    recall(ctx);
    feature_combine(ctx);
    rank(ctx);
    rerank(ctx);
    return fill_response(ctx);
}

} // namespace rec
} // namespace ratus_rec
