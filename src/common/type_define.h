#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace ratus_rec {

using ItemId    = std::string;
using UserId    = std::string;
using CateId    = std::string;
using json      = nlohmann::json;

} // namespace ratus_rec
