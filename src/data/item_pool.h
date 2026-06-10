#pragma once

#include <memory>
#include <unordered_set>
#include "common/type_define.h"

namespace ratus_rec {

struct ItemPoolData {
    std::unordered_set<ItemId> item_set;
};

extern std::shared_ptr<const ItemPoolData> g_item_pool;

bool item_pool_init();

bool item_pool_load();

} // namespace ratus_rec
