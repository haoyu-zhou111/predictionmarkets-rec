#pragma once

#include <vector>
#include <string>

namespace predictionmarkets_rec {

struct LRModel {
    std::vector<float> weight;
    float bias;
    uint32_t hash_size;
};

extern LRModel g_model;

bool lr_model_init();

bool lr_model_load();

} // namespace predictionmarkets_rec

