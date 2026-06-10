#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "common/type_define.h"

namespace ratus_rec {

using I2IIndex = std::unordered_map<ItemId, std::vector<ItemId>>;

extern std::unordered_map<std::string, I2IIndex> g_i2i_index_map;

bool i2i_index_load(const std::string& index_path, I2IIndex &index);

} // namespace ratus_rec
