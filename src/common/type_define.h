#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace predictionmarkets_rec {

using ItemId    = std::string;
using UserId    = std::string;
using AnchorId  = std::string;
using DeviceId  = std::string;
using CateId    = std::string;
using json      = nlohmann::json;

using FeatureMap = std::unordered_map<std::string, std::string>;
using I2IIndex   = std::unordered_map<ItemId, std::vector<ItemId>>;
using U2IIndex   = std::unordered_map<UserId, std::vector<ItemId>>;

} // namespace predictionmarkets_rec
