#pragma once

#include "context.h"
#include <nlohmann/json.hpp>

namespace predictionmarkets_rec {
namespace rec {

nlohmann::json recommend(Context& ctx);

}
}
