#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include "common/type_define.h"

namespace predictionmarkets_rec {

struct ItemFeatureData {
    std::unordered_map<ItemId, FeatureMap> features;
};

extern std::shared_ptr<const ItemFeatureData> g_item_feature;

bool item_feature_init();

bool item_feature_load();

bool item_feature_reload();

} // namespace predictionmarkets_rec
