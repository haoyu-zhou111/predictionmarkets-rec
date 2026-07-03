#include "filter.h"
#include "common/log.h"

namespace predictionmarkets_rec {

namespace rec {

bool check_item(Context& ctx, const ItemId& item_id) {
    if (!ctx.item_pool->items.count(item_id)) {
        ALOG(DEBUG, "item %s filtered by item pool", item_id.c_str());
        return false;
    }

    if (ctx.blacklist_set.count(item_id)) {
        ALOG(DEBUG, "item %s filtered by blacklist", item_id.c_str());
        return false;
    }

    // user_history 硬曝光过滤本阶段屏蔽：内容少，改由 rerank 对 rec_exposed 软降权（见 rerank）
    // if (ctx.show_set.count(item_id)) {
    //     ALOG(DEBUG, "item %s filtered by history", item_id.c_str());
    //     return false;
    // }

    return true;
}

} // namespace rec

} // namespace predictionmarkets_rec
