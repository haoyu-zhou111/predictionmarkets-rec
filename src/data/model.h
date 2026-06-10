#pragma once

#include <vector>
#include <string>

namespace ratus_rec {

struct LRModel {
    std::vector<float> weight;
    float bias;
    int hash_size;
};

extern LRModel g_model;

bool lr_model_load();

} // namespace ratus_rec

