#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace predictionmarkets_rec {

using ItemId    = std::string;
using UserId    = std::string;
using CateId    = std::string;
using json      = nlohmann::json;

} // namespace predictionmarkets_rec
