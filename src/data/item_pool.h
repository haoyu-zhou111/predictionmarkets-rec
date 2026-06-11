#pragma once

#include <memory>
#include <unordered_map>
#include "common/type_define.h"

namespace predictionmarkets_rec {

struct ItemStat {
    uint32_t    show    = 0;    // 曝光数 n
    uint32_t    click   = 0;    // 点击数 k
};

struct ItemPoolData {
    std::unordered_map<ItemId, ItemStat> item_stats;
};

extern std::shared_ptr<const ItemPoolData> g_item_pool;

bool item_pool_init();

bool item_pool_load();

} // namespace predictionmarkets_rec
