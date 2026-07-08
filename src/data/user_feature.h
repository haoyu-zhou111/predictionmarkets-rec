#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include "common/type_define.h"

namespace predictionmarkets_rec {

using UserFeatureData = std::unordered_map<UserId, FeatureMap>;

extern UserFeatureData g_user_feature;

bool user_feature_init();

bool user_feature_load();

} // namespace predictionmarkets_rec
