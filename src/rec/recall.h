#pragma once

#include "context.h"
#include "common/type_define.h"
#include <unordered_map>

namespace predictionmarkets_rec {

namespace rec {

const uint32_t CHANNEL_TYPE_COUNT = 4;

const std::unordered_map<std::string, uint32_t> CHANNEL = {
    {"global_hot",   0},
    {"cate_hot",     0},
    {"full",         0},
    
    {"icf",          1},
    {"i2v",          1},
    {"swing",        1},

    {"follow_u2i",   2},
    {"recent_u2i",   2},

    {"op",           3}
};

void recall(Context& ctx);

} // namespace rec

} // namespace predictionmarkets_rec
