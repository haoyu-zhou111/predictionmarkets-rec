#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "common/type_define.h"

namespace ratus_rec {

struct ItemFeatureData {
    std::unordered_map<ItemId, std::unordered_map<std::string, std::string>> features;

    std::vector<ItemId> global_hot_items;
    std::unordered_map<CateId, std::vector<ItemId>> cate_hot_items;
};

extern std::shared_ptr<const ItemFeatureData> g_item_feature;

bool item_feature_load();

bool item_feature_reload();

} // namespace ratus_rec
