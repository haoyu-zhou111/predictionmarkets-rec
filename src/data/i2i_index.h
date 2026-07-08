#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "common/type_define.h"

namespace predictionmarkets_rec {

extern std::unordered_map<std::string, I2IIndex> g_i2i_index_map;

bool i2i_index_init(const std::string& index_path, I2IIndex& index);

bool i2i_index_load(const std::string& index_path, I2IIndex& index);

} // namespace predictionmarkets_rec
